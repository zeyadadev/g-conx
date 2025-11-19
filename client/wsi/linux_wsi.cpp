#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "wsi/linux_wsi.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include "wsi/platform_wsi.h"
#include "wsi/linux_surface.h"
#include "utils/logging.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include <drm_fourcc.h>
#include <gbm.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <xcb/xcb_event.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>

#ifdef VENUS_PLUS_HAS_WAYLAND
#include <sys/mman.h>
#include <sys/syscall.h>
#include <wayland-client.h>
#include "wsi/wayland_dmabuf_protocol.h"
#endif

namespace venus_plus {

namespace {

bool decompress_rle(const VenusFrameHeader& frame,
                    const uint8_t* data,
                    std::vector<uint8_t>* output) {
    if (!data || !output) {
        return false;
    }
    output->clear();
    size_t offset = 0;
    while (offset < frame.payload_size) {
        if (offset + 2 > frame.payload_size) {
            return false;
        }
        uint8_t tag = data[offset++];
        uint8_t count = data[offset++];
        if (tag == 1) {
            if (offset >= frame.payload_size) {
                return false;
            }
            uint8_t value = data[offset++];
            output->insert(output->end(), count, value);
        } else {
            if (offset + count > frame.payload_size) {
                return false;
            }
            output->insert(output->end(), data + offset, data + offset + count);
            offset += count;
        }
    }
    if (frame.uncompressed_size != 0 &&
        output->size() != frame.uncompressed_size) {
        return false;
    }
    return true;
}

uint32_t bytes_per_pixel(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        default:
            return 4;
    }
}

uint32_t clamp_stride(uint32_t width, VkFormat format) {
    return width * bytes_per_pixel(format);
}

bool copy_rows(uint8_t* dst,
               uint32_t dst_stride,
               const uint8_t* src,
               uint32_t src_stride,
               uint32_t width,
               uint32_t height,
               VkFormat format) {
    if (!dst || !src) {
        return false;
    }
    uint32_t row_bytes = clamp_stride(width, format);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src_row = src + static_cast<size_t>(y) * src_stride;
        uint8_t* dst_row = dst + static_cast<size_t>(y) * dst_stride;
        std::memcpy(dst_row, src_row, row_bytes);
    }
    return true;
}

constexpr uint32_t kMinRenderNode = 128;
constexpr uint32_t kMaxRenderNode = 191;

int open_render_node(const char* override_path = nullptr) {
    if (override_path) {
        int fd = open(override_path, O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            return fd;
        }
        VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to open render node " << override_path
                                    << ": " << std::strerror(errno);
        return -1;
    }
    for (uint32_t i = kMinRenderNode; i <= kMaxRenderNode; ++i) {
        char path[64];
        std::snprintf(path, sizeof(path), "/dev/dri/renderD%u", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            return fd;
        }
    }
    VP_LOG_STREAM_WARN(CLIENT) << "[WSI] No DRM render node available";
    return -1;
}

uint32_t vk_format_to_gbm(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return GBM_FORMAT_ARGB8888;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return GBM_FORMAT_ABGR8888;
        default:
            return GBM_FORMAT_ARGB8888;
    }
}

uint32_t vk_format_to_drm(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return DRM_FORMAT_ARGB8888;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return DRM_FORMAT_ABGR8888;
        default:
            return DRM_FORMAT_ARGB8888;
    }
}

uint8_t depth_from_format(VkFormat format) {
    (void)format;
    return 32;
}

uint8_t bpp_from_format(VkFormat format) {
    (void)format;
    return 32;
}

class Backend {
public:
    virtual ~Backend() = default;
    virtual bool init(const LinuxSurface& surface,
                      uint32_t width,
                      uint32_t height,
                      VkFormat format,
                      uint32_t image_count) = 0;
    virtual void present(const VenusFrameHeader& frame,
                         const uint8_t* data,
                         uint32_t stride) = 0;
    virtual void shutdown() = 0;
};

class XcbGbmBackend : public Backend {
public:
    bool init(const LinuxSurface& surface,
              uint32_t width,
              uint32_t height,
              VkFormat format,
              uint32_t image_count) override {
        if (!surface.xcb.connection || surface.xcb.window == 0) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Invalid XCB surface for GBM path";
            return false;
        }

        conn_ = surface.xcb.connection;
        window_ = surface.xcb.window;
        width_ = width;
        height_ = height;
        format_ = format;

        drm_fd_ = open_render_node(nullptr);
        if (drm_fd_ < 0) {
            return false;
        }
        gbm_device_ = gbm_create_device(drm_fd_);
        if (!gbm_device_) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to create GBM device";
            close(drm_fd_);
            drm_fd_ = -1;
            return false;
        }

        if (!check_dri3_support()) {
            VP_LOG_STREAM_WARN(CLIENT) << "[WSI] X server lacks DRI3 support";
            shutdown();
            return false;
        }

        present_event_id_ = xcb_generate_id(conn_);
        xcb_present_select_input(conn_, present_event_id_, window_,
                                 XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY);
        present_queue_ = xcb_register_for_special_xge(conn_, &xcb_present_id,
                                                      present_event_id_, nullptr);
        if (!present_queue_) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to register present event queue";
            shutdown();
            return false;
        }

        buffers_.resize(std::max<uint32_t>(2u, image_count));
        for (auto& buf : buffers_) {
            if (!allocate_buffer(buf)) {
                shutdown();
                return false;
            }
        }
        xcb_flush(conn_);
        return true;
    }

    void present(const VenusFrameHeader& frame,
                 const uint8_t* data,
                 uint32_t stride) override {
        if (!conn_ || buffers_.empty() || !data) {
            return;
        }
        process_events(false);
        Buffer& buf = buffers_[frame.image_index % buffers_.size()];
        wait_for_buffer(buf);
        if (!buf.mapped && !remap_buffer(buf)) {
            return;
        }
        copy_rows(static_cast<uint8_t*>(buf.mapped), buf.stride,
                  data, stride, width_, height_, format_);
        if (buf.mapped && buf.map_data) {
            gbm_bo_unmap(buf.bo, buf.map_data);
            buf.mapped = nullptr;
            buf.map_data = nullptr;
        }

        buf.in_use = true;
        const uint32_t serial = present_serial_++;
        xcb_present_pixmap(conn_, window_, buf.pixmap, serial,
                           0, 0, 0, 0, XCB_NONE, XCB_NONE, XCB_NONE,
                           XCB_PRESENT_OPTION_NONE, 0, 0, 0, 0, nullptr);
        xcb_flush(conn_);
    }

    void shutdown() override {
        if (conn_ && present_queue_) {
            xcb_unregister_for_special_event(conn_, present_queue_);
            present_queue_ = nullptr;
        }

        for (auto& buf : buffers_) {
            destroy_buffer(buf);
        }
        buffers_.clear();

        if (gbm_device_) {
            gbm_device_destroy(gbm_device_);
            gbm_device_ = nullptr;
        }
        if (drm_fd_ >= 0) {
            close(drm_fd_);
            drm_fd_ = -1;
        }
    }

private:
    struct Buffer {
        gbm_bo* bo = nullptr;
        void* mapped = nullptr;
        void* map_data = nullptr;
        uint32_t stride = 0;
        xcb_pixmap_t pixmap = 0;
        bool in_use = false;
    };

    bool allocate_buffer(Buffer& buf) {
        buf.bo = gbm_bo_create(gbm_device_, width_, height_,
                               vk_format_to_gbm(format_),
                               GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
        if (!buf.bo) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to create GBM BO";
            return false;
        }
        buf.mapped = gbm_bo_map(buf.bo, 0, 0, width_, height_,
                                GBM_BO_TRANSFER_WRITE, &buf.stride, &buf.map_data);
        if (!buf.mapped) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to map GBM BO";
            return false;
        }
        buf.pixmap = xcb_generate_id(conn_);
        int fd = gbm_bo_get_fd(buf.bo);
        if (fd < 0) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to export DMA-BUF";
            return false;
        }
        xcb_dri3_pixmap_from_buffer(conn_, buf.pixmap, window_,
                                    buf.stride * height_, width_, height_,
                                    buf.stride, depth_from_format(format_),
                                    bpp_from_format(format_), fd);
        close(fd);
        buf.in_use = false;
        return true;
    }

    bool remap_buffer(Buffer& buf) {
        buf.mapped = gbm_bo_map(buf.bo, 0, 0, width_, height_,
                                GBM_BO_TRANSFER_WRITE, &buf.stride, &buf.map_data);
        if (!buf.mapped) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to remap GBM BO";
            return false;
        }
        return true;
    }

    void destroy_buffer(Buffer& buf) {
        if (buf.pixmap) {
            xcb_free_pixmap(conn_, buf.pixmap);
            buf.pixmap = 0;
        }
        if (buf.mapped && buf.map_data && buf.bo) {
            gbm_bo_unmap(buf.bo, buf.map_data);
            buf.mapped = nullptr;
            buf.map_data = nullptr;
        }
        if (buf.bo) {
            gbm_bo_destroy(buf.bo);
            buf.bo = nullptr;
        }
        buf.in_use = false;
    }

    bool check_dri3_support() {
        xcb_dri3_query_version_cookie_t cookie =
            xcb_dri3_query_version(conn_, 1, 2);
        xcb_dri3_query_version_reply_t* reply =
            xcb_dri3_query_version_reply(conn_, cookie, nullptr);
        const bool supported = reply != nullptr;
        free(reply);
        return supported;
    }

    bool process_events(bool block) {
        if (!present_queue_) {
            return false;
        }
        while (true) {
            xcb_generic_event_t* generic = block
                ? xcb_wait_for_special_event(conn_, present_queue_)
                : xcb_poll_for_special_event(conn_, present_queue_);
            if (!generic) {
                return block;
            }
            auto* event = reinterpret_cast<xcb_present_generic_event_t*>(generic);
            if (event->evtype == XCB_PRESENT_EVENT_IDLE_NOTIFY) {
                auto* idle = reinterpret_cast<xcb_present_idle_notify_event_t*>(generic);
                for (auto& buf : buffers_) {
                    if (buf.pixmap == idle->pixmap) {
                        buf.in_use = false;
                        if (!buf.mapped) {
                            remap_buffer(buf);
                        }
                        break;
                    }
                }
            }
            free(generic);
            if (!block) {
                break;
            }
        }
        return true;
    }

    void wait_for_buffer(Buffer& buf) {
        while (buf.in_use) {
            if (!process_events(true)) {
                break;
            }
        }
    }

    xcb_connection_t* conn_ = nullptr;
    xcb_window_t window_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    VkFormat format_ = VK_FORMAT_B8G8R8A8_UNORM;
    int drm_fd_ = -1;
    gbm_device* gbm_device_ = nullptr;
    xcb_special_event_t* present_queue_ = nullptr;
    uint32_t present_event_id_ = 0;
    uint32_t present_serial_ = 0;
    std::vector<Buffer> buffers_;
};

class XcbCpuBackend : public Backend {
public:
    bool init(const LinuxSurface& surface,
              uint32_t width,
              uint32_t height,
              VkFormat format,
              uint32_t image_count) override {
        (void)format;
        if (!surface.xcb.connection || surface.xcb.window == 0) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Invalid XCB surface";
            return false;
        }
        conn_ = surface.xcb.connection;
        window_ = surface.xcb.window;
        width_ = width;
        height_ = height;
        depth_ = surface.xcb.depth ? surface.xcb.depth : 32;
        stride_ = clamp_stride(width_, VK_FORMAT_B8G8R8A8_UNORM);

        gc_ = xcb_generate_id(conn_);
        xcb_create_gc(conn_, gc_, window_, 0, nullptr);

        buffers_.resize(std::max<uint32_t>(1u, image_count));
        for (auto& buffer : buffers_) {
            buffer.pixels.resize(static_cast<size_t>(stride_) * height_);
        }

        VP_LOG_STREAM_INFO(CLIENT) << "[WSI] XCB CPU backend initialized";
        return true;
    }

    void present(const VenusFrameHeader& frame,
                 const uint8_t* data,
                 uint32_t stride) override {
        if (!conn_ || buffers_.empty() || !data) {
            return;
        }
        CpuBuffer& buf = buffers_[frame.image_index % buffers_.size()];
        copy_rows(buf.pixels.data(), stride_, data, stride,
                  width_, height_, VK_FORMAT_B8G8R8A8_UNORM);

        const uint32_t data_size = stride_ * height_;
        xcb_put_image(conn_, XCB_IMAGE_FORMAT_Z_PIXMAP,
                      window_, gc_, width_, height_,
                      0, 0, 0, depth_, data_size,
                      buf.pixels.data());
        xcb_flush(conn_);
    }

    void shutdown() override {
        if (conn_ && gc_) {
            xcb_free_gc(conn_, gc_);
        }
        gc_ = 0;
        buffers_.clear();
    }

private:
    struct CpuBuffer {
        std::vector<uint8_t> pixels;
    };

    xcb_connection_t* conn_ = nullptr;
    uint32_t window_ = 0;
    xcb_gcontext_t gc_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint8_t depth_ = 32;
    uint32_t stride_ = 0;
    std::vector<CpuBuffer> buffers_;
};

#ifdef VENUS_PLUS_HAS_WAYLAND

class WaylandShmBackend : public Backend {
public:
    bool init(const LinuxSurface& surface,
              uint32_t width,
              uint32_t height,
              VkFormat format,
              uint32_t image_count) override {
        (void)format;
        display_ = surface.wayland.display;
        surface_ = surface.wayland.surface;
        if (!display_ || !surface_) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Invalid Wayland surface";
            return false;
        }

        if (!bind_globals()) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to bind wl_shm";
            return false;
        }

        width_ = width;
        height_ = height;
        stride_ = clamp_stride(width_, VK_FORMAT_B8G8R8A8_UNORM);

        buffers_.resize(std::max<uint32_t>(2u, image_count));
        for (auto& buf : buffers_) {
            if (!create_buffer(buf)) {
                return false;
            }
        }

        VP_LOG_STREAM_INFO(CLIENT) << "[WSI] Wayland wl_shm backend initialized";
        return true;
    }

    void present(const VenusFrameHeader& frame,
                 const uint8_t* data,
                 uint32_t stride) override {
        if (!display_ || !surface_ || buffers_.empty() || !data) {
            return;
        }

        ShmBuffer& buf = buffers_[frame.image_index % buffers_.size()];
        wait_for_buffer(buf);

        copy_rows(static_cast<uint8_t*>(buf.data), buf.stride,
                  data, stride, width_, height_, VK_FORMAT_B8G8R8A8_UNORM);

        buf.busy = true;
        wl_surface_attach(surface_, buf.buffer, 0, 0);
        wl_surface_damage_buffer(surface_, 0, 0, width_, height_);
        wl_surface_commit(surface_);
        wl_display_flush(display_);
    }

    void shutdown() override {
        for (auto& buf : buffers_) {
            destroy_buffer(buf);
        }
        buffers_.clear();
        if (shm_) {
            wl_shm_destroy(shm_);
            shm_ = nullptr;
        }
        if (registry_) {
            wl_registry_destroy(registry_);
            registry_ = nullptr;
        }
    }

private:
    struct ShmBuffer {
        int fd = -1;
        void* data = nullptr;
        size_t size = 0;
        uint32_t stride = 0;
        wl_shm_pool* pool = nullptr;
        wl_buffer* buffer = nullptr;
        bool busy = false;
    };

    static void handle_buffer_release(void* data, wl_buffer* buffer) {
        (void)buffer;
        auto* self = static_cast<ShmBuffer*>(data);
        if (self) {
            self->busy = false;
        }
    }

    static const wl_buffer_listener kBufferListener;

    bool bind_globals() {
        registry_ = wl_display_get_registry(display_);
        if (!registry_) {
            return false;
        }

        wl_registry_add_listener(registry_, &kRegistryListener, this);
        wl_display_roundtrip(display_);
        wl_display_roundtrip(display_);
        return shm_ != nullptr;
    }

    static void registry_global(void* data,
                                wl_registry* registry,
                                uint32_t name,
                                const char* interface,
                                uint32_t version) {
        (void)registry;
        auto* self = static_cast<WaylandShmBackend*>(data);
        if (!self || !interface) {
            return;
        }
        if (std::strcmp(interface, wl_shm_interface.name) == 0) {
            self->shm_ = static_cast<wl_shm*>(
                wl_registry_bind(self->registry_, name, &wl_shm_interface,
                                  std::min<uint32_t>(version, 1u)));
        }
    }

    static void registry_remove(void* data, wl_registry* registry, uint32_t name) {
        (void)data;
        (void)registry;
        (void)name;
    }

    static const wl_registry_listener kRegistryListener;

    static int create_shm_file(size_t size) {
#ifdef MFD_CLOEXEC
        {
            int fd = memfd_create("venus_plus_wsi", MFD_CLOEXEC);
            if (fd >= 0) {
                if (ftruncate(fd, static_cast<off_t>(size)) == 0) {
                    return fd;
                }
                close(fd);
                return -1;
            }
        }
#endif
#ifdef SYS_memfd_create
        {
            int fd = syscall(SYS_memfd_create, "venus_plus_wsi", 0);
            if (fd >= 0) {
                if (ftruncate(fd, static_cast<off_t>(size)) == 0) {
                    return fd;
                }
                close(fd);
                return -1;
            }
        }
#endif
        return -1;
    }

    bool create_buffer(ShmBuffer& buf) {
        buf.stride = stride_;
        buf.size = static_cast<size_t>(buf.stride) * height_;
        buf.fd = create_shm_file(buf.size);
        if (buf.fd < 0) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to create memfd: " << std::strerror(errno);
            return false;
        }

        buf.data = mmap(nullptr, buf.size, PROT_READ | PROT_WRITE, MAP_SHARED, buf.fd, 0);
        if (buf.data == MAP_FAILED) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] mmap failed";
            close(buf.fd);
            buf.fd = -1;
            return false;
        }

        buf.pool = wl_shm_create_pool(shm_, buf.fd, buf.size);
        buf.buffer = wl_shm_pool_create_buffer(buf.pool, 0, width_, height_,
                                               buf.stride, WL_SHM_FORMAT_ARGB8888);
        wl_buffer_add_listener(buf.buffer, &kBufferListener, &buf);
        buf.busy = false;
        return true;
    }

    void destroy_buffer(ShmBuffer& buf) {
        if (buf.buffer) {
            wl_buffer_destroy(buf.buffer);
            buf.buffer = nullptr;
        }
        if (buf.pool) {
            wl_shm_pool_destroy(buf.pool);
            buf.pool = nullptr;
        }
        if (buf.data && buf.data != MAP_FAILED) {
            munmap(buf.data, buf.size);
        }
        if (buf.fd >= 0) {
            close(buf.fd);
            buf.fd = -1;
        }
        buf.data = nullptr;
        buf.size = 0;
        buf.busy = false;
    }

    void wait_for_buffer(ShmBuffer& buf) {
        while (buf.busy) {
            if (wl_display_dispatch(display_) < 0) {
                break;
            }
        }
    }

    wl_display* display_ = nullptr;
    wl_surface* surface_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_shm* shm_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t stride_ = 0;
    std::vector<ShmBuffer> buffers_;
};

const wl_buffer_listener WaylandShmBackend::kBufferListener = {
    WaylandShmBackend::handle_buffer_release,
};

const wl_registry_listener WaylandShmBackend::kRegistryListener = {
    WaylandShmBackend::registry_global,
    WaylandShmBackend::registry_remove,
};

class WaylandDmabufBackend : public Backend {
public:
    bool init(const LinuxSurface& surface,
              uint32_t width,
              uint32_t height,
              VkFormat format,
              uint32_t image_count) override {
        display_ = surface.wayland.display;
        surface_ = surface.wayland.surface;
        if (!display_ || !surface_) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Invalid Wayland surface for DMA-BUF";
            return false;
        }

        drm_fd_ = open_render_node(nullptr);
        if (drm_fd_ < 0) {
            return false;
        }
        gbm_device_ = gbm_create_device(drm_fd_);
        if (!gbm_device_) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to create GBM device";
            close(drm_fd_);
            drm_fd_ = -1;
            return false;
        }

        if (!bind_globals()) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] wl_drm globals missing";
            shutdown();
            return false;
        }

        width_ = width;
        height_ = height;
        format_ = format;
        buffers_.resize(std::max<uint32_t>(2u, image_count));
        for (auto& buf : buffers_) {
            if (!create_buffer(buf)) {
                shutdown();
                return false;
            }
        }
        return true;
    }

    void present(const VenusFrameHeader& frame,
                 const uint8_t* data,
                 uint32_t stride) override {
        if (!surface_ || !display_ || buffers_.empty()) {
            return;
        }
        flush_events();
        Buffer& buf = buffers_[frame.image_index % buffers_.size()];
        wait_for_buffer(buf);
        if (!buf.mapped && !remap_buffer(buf)) {
            return;
        }
        copy_rows(static_cast<uint8_t*>(buf.mapped), buf.stride,
                  data, stride, width_, height_, format_);
        if (buf.mapped && buf.map_data) {
            gbm_bo_unmap(buf.bo, buf.map_data);
            buf.mapped = nullptr;
            buf.map_data = nullptr;
        }
        buf.busy = true;
        wl_surface_attach(surface_, buf.buffer, 0, 0);
        wl_surface_damage_buffer(surface_, 0, 0, width_, height_);
        wl_surface_commit(surface_);
        wl_display_flush(display_);
    }

    void shutdown() override {
        for (auto& buf : buffers_) {
            destroy_buffer(buf);
        }
        buffers_.clear();
        if (dmabuf_) {
            zwp_linux_dmabuf_v1_destroy(dmabuf_);
            dmabuf_ = nullptr;
        }
        if (registry_) {
            wl_registry_destroy(registry_);
            registry_ = nullptr;
        }
        if (gbm_device_) {
            gbm_device_destroy(gbm_device_);
            gbm_device_ = nullptr;
        }
        if (drm_fd_ >= 0) {
            close(drm_fd_);
            drm_fd_ = -1;
        }
    }

private:
    struct Buffer {
        gbm_bo* bo = nullptr;
        void* mapped = nullptr;
        void* map_data = nullptr;
        uint32_t stride = 0;
        wl_buffer* buffer = nullptr;
        bool busy = false;
    };

    static void buffer_release(void* data, wl_buffer* buffer) {
        (void)buffer;
        auto* self = static_cast<Buffer*>(data);
        if (self) {
            self->busy = false;
        }
    }

    static const wl_buffer_listener kBufferListener;

    static void registry_global(void* data,
                                wl_registry* registry,
                                uint32_t name,
                                const char* interface,
                                uint32_t version) {
        auto* self = static_cast<WaylandDmabufBackend*>(data);
        if (!self || !interface) {
            return;
        }
        if (std::strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0) {
            uint32_t ver = std::min<uint32_t>(version, 4u);
            self->dmabuf_ = static_cast<zwp_linux_dmabuf_v1*>(
                wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, ver));
        }
    }

    static void registry_remove(void* data, wl_registry* registry, uint32_t name) {
        (void)data;
        (void)registry;
        (void)name;
    }

    static const wl_registry_listener kRegistryListener;

    bool bind_globals() {
        registry_ = wl_display_get_registry(display_);
        if (!registry_) {
            return false;
        }
        wl_registry_add_listener(registry_, &kRegistryListener, this);
        wl_display_roundtrip(display_);
        wl_display_roundtrip(display_);
        return dmabuf_ != nullptr;
    }

    bool create_buffer(Buffer& buf) {
        buf.bo = gbm_bo_create(gbm_device_, width_, height_,
                               vk_format_to_gbm(format_),
                               GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
        if (!buf.bo) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to create GBM BO for Wayland";
            return false;
        }
        buf.mapped = gbm_bo_map(buf.bo, 0, 0, width_, height_,
                                GBM_BO_TRANSFER_WRITE, &buf.stride, &buf.map_data);
        if (!buf.mapped) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to map GBM BO";
            return false;
        }
        int fd = gbm_bo_get_fd(buf.bo);
        if (fd < 0) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to export DMA-BUF";
            return false;
        }
        uint64_t modifier = gbm_bo_get_modifier(buf.bo);
        auto* params = zwp_linux_dmabuf_v1_create_params(dmabuf_);
        zwp_linux_buffer_params_v1_add(params, fd, 0, 0, buf.stride,
                                       static_cast<uint32_t>(modifier >> 32),
                                       static_cast<uint32_t>(modifier & 0xffffffff));
        buf.buffer = zwp_linux_buffer_params_v1_create_immed(
            params, width_, height_, vk_format_to_drm(format_), 0);
        wl_buffer_add_listener(buf.buffer, &kBufferListener, &buf);
        zwp_linux_buffer_params_v1_destroy(params);
        close(fd);
        buf.busy = false;
        return true;
    }

    bool remap_buffer(Buffer& buf) {
        buf.mapped = gbm_bo_map(buf.bo, 0, 0, width_, height_,
                                GBM_BO_TRANSFER_WRITE, &buf.stride, &buf.map_data);
        return buf.mapped != nullptr;
    }

    void destroy_buffer(Buffer& buf) {
        if (buf.buffer) {
            wl_buffer_destroy(buf.buffer);
            buf.buffer = nullptr;
        }
        if (buf.mapped && buf.map_data && buf.bo) {
            gbm_bo_unmap(buf.bo, buf.map_data);
            buf.mapped = nullptr;
            buf.map_data = nullptr;
        }
        if (buf.bo) {
            gbm_bo_destroy(buf.bo);
            buf.bo = nullptr;
        }
        buf.busy = false;
    }

    void wait_for_buffer(Buffer& buf) {
        while (buf.busy) {
            if (wl_display_dispatch(display_) < 0) {
                break;
            }
        }
    }

    void flush_events() {
        while (wl_display_dispatch_pending(display_) > 0) {
        }
    }

    wl_display* display_ = nullptr;
    wl_surface* surface_ = nullptr;
    wl_registry* registry_ = nullptr;
    zwp_linux_dmabuf_v1* dmabuf_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    VkFormat format_ = VK_FORMAT_B8G8R8A8_UNORM;
    int drm_fd_ = -1;
    gbm_device* gbm_device_ = nullptr;
    std::vector<Buffer> buffers_;
};

const wl_buffer_listener WaylandDmabufBackend::kBufferListener = {
    WaylandDmabufBackend::buffer_release,
};

const wl_registry_listener WaylandDmabufBackend::kRegistryListener = {
    WaylandDmabufBackend::registry_global,
    WaylandDmabufBackend::registry_remove,
};

#endif // VENUS_PLUS_HAS_WAYLAND

class LinuxWSI : public PlatformWSI {
public:
    enum class BackendKind {
        kXcbCpu,
        kXcbGbm,
        kWaylandShm,
        kWaylandDmabuf,
    };

    LinuxWSI(const LinuxSurface& surface, BackendKind kind, bool allow_fallback)
        : surface_(surface), backend_kind_(kind), allow_fallback_(allow_fallback) {}

    bool init(const VkSwapchainCreateInfoKHR& info, uint32_t image_count) override {
        width_ = info.imageExtent.width;
        height_ = info.imageExtent.height;
        format_ = info.imageFormat;
        image_count_ = image_count;

        while (true) {
            backend_ = create_backend();
            if (!backend_) {
                VP_LOG_STREAM_WARN(CLIENT) << "[WSI] No suitable Linux backend found";
                return false;
            }
            VP_LOG_STREAM_INFO(CLIENT) << "[WSI] Linux backend: " << backend_name(backend_kind_);
            if (backend_->init(surface_, width_, height_, format_, image_count_)) {
                return true;
            }
            backend_->shutdown();
            backend_.reset();
            if (!try_fallback()) {
                return false;
            }
        }
    }

    void handle_frame(const VenusFrameHeader& frame, const uint8_t* data) override {
        if (!backend_ || !data) {
            return;
        }

        const uint8_t* payload = data;
        size_t payload_size = frame.payload_size;
        if (frame.compression == FrameCompressionType::RLE) {
            if (!decompress_rle(frame, data, &decode_buffer_)) {
                VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Failed to decode frame";
                return;
            }
            payload = decode_buffer_.data();
            payload_size = decode_buffer_.size();
        } else if (frame.compression != FrameCompressionType::NONE) {
            VP_LOG_STREAM_ERROR(CLIENT) << "[WSI] Unsupported compression";
            return;
        }

        uint32_t stride = frame.stride;
        if (stride == 0) {
            stride = clamp_stride(frame.width, format_);
        }
        if (payload_size < static_cast<size_t>(stride) * frame.height) {
            VP_LOG_STREAM_WARN(CLIENT) << "[WSI] Frame payload shorter than expected";
            return;
        }

        backend_->present(frame, payload, stride);
    }

    void shutdown() override {
        if (backend_) {
            backend_->shutdown();
            backend_.reset();
        }
    }

private:
    static const char* backend_name(BackendKind kind) {
        switch (kind) {
            case BackendKind::kXcbCpu:
                return "X11 CPU (XPutImage)";
            case BackendKind::kXcbGbm:
                return "X11 GBM (DRI3)";
            case BackendKind::kWaylandShm:
                return "Wayland wl_shm";
            case BackendKind::kWaylandDmabuf:
                return "Wayland DMA-BUF";
        }
        return "Unknown";
    }

    std::unique_ptr<Backend> create_backend() {
        switch (backend_kind_) {
            case BackendKind::kXcbCpu:
                return std::make_unique<XcbCpuBackend>();
            case BackendKind::kXcbGbm:
                return std::make_unique<XcbGbmBackend>();
            case BackendKind::kWaylandShm:
                #ifdef VENUS_PLUS_HAS_WAYLAND
                return std::make_unique<WaylandShmBackend>();
                #else
                VP_LOG_STREAM_WARN(CLIENT) << "[WSI] Wayland wl_shm backend unavailable at build";
                return nullptr;
                #endif
            case BackendKind::kWaylandDmabuf:
                #ifdef VENUS_PLUS_HAS_WAYLAND
                return std::make_unique<WaylandDmabufBackend>();
                #else
                VP_LOG_STREAM_WARN(CLIENT)
                    << "[WSI] Wayland DMA-BUF backend unavailable (no wayland-client)";
                return nullptr;
                #endif
        }
        return nullptr;
    }

    bool try_fallback() {
        if (!allow_fallback_) {
            return false;
        }
        if (surface_.type == LinuxSurfaceType::kXcb && backend_kind_ != BackendKind::kXcbCpu) {
            VP_LOG_STREAM_WARN(CLIENT) << "[WSI] Falling back to X11 CPU backend";
            backend_kind_ = BackendKind::kXcbCpu;
            return true;
        }
        if (surface_.type == LinuxSurfaceType::kWayland &&
            backend_kind_ != BackendKind::kWaylandShm) {
            VP_LOG_STREAM_WARN(CLIENT) << "[WSI] Falling back to Wayland wl_shm backend";
            backend_kind_ = BackendKind::kWaylandShm;
            return true;
        }
        return false;
    }

    LinuxSurface surface_ = {};
    BackendKind backend_kind_ = BackendKind::kXcbCpu;
    bool allow_fallback_ = true;
    std::unique_ptr<Backend> backend_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    uint32_t image_count_ = 0;
    std::vector<uint8_t> decode_buffer_;
};

} // namespace

static std::string env_force_path() {
    const char* env = std::getenv("VENUS_WSI_FORCE_PATH");
    if (!env) {
        return {};
    }
    std::string value(env);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static bool should_force_headless(const std::string& value) {
    return value == "headless" || value == "none";
}

static LinuxWSI::BackendKind choose_backend_kind(const LinuxSurface& surface,
                                                 const std::string& force) {
    if (surface.type == LinuxSurfaceType::kXcb) {
        if (force == "x11-cpu" || force == "xcb-cpu") {
            return LinuxWSI::BackendKind::kXcbCpu;
        }
        if (force == "x11-gbm") {
            return LinuxWSI::BackendKind::kXcbGbm;
        }
        return LinuxWSI::BackendKind::kXcbGbm;
    }
    if (surface.type == LinuxSurfaceType::kWayland) {
        if (force == "wayland-shm") {
            return LinuxWSI::BackendKind::kWaylandShm;
        }
        if (force == "wayland-dmabuf" || force == "wayland-gbm") {
            return LinuxWSI::BackendKind::kWaylandDmabuf;
        }
        return LinuxWSI::BackendKind::kWaylandDmabuf;
    }
    return LinuxWSI::BackendKind::kXcbCpu;
}

std::shared_ptr<PlatformWSI> create_linux_platform_wsi(VkSurfaceKHR surface_handle) {
    if (!is_linux_surface(surface_handle)) {
        return nullptr;
    }
    LinuxSurface* surface = get_linux_surface(surface_handle);
    if (!surface) {
        return nullptr;
    }
    std::string force = env_force_path();
    if (should_force_headless(force)) {
        VP_LOG_STREAM_WARN(CLIENT) << "[WSI] Forcing headless WSI due to VENUS_WSI_FORCE_PATH";
        return nullptr;
    }
    auto kind = choose_backend_kind(*surface, force);
    bool allow_fallback = force.empty();
    auto wsi = std::make_shared<LinuxWSI>(*surface, kind, allow_fallback);
    return wsi;
}

} // namespace venus_plus

#endif // defined(__linux__) && !defined(__ANDROID__)
