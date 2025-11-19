# Phase 10 Linux: X11 and Wayland WSI

**Linux-specific WSI implementation with GBM optimization**

## Overview

This document covers Linux WSI implementation for Venus Plus:
- **Primary path**: GBM + DMA-BUF (zero-copy to compositor)
- **Fallback path**: wl_shm / XPutImage (CPU buffers)

The system automatically detects GBM support and falls back to CPU buffers when unavailable.

**Duration**: 10 days (Days 86-95)
**Prerequisites**: Phase 10 Base complete

---

## Architecture

### Path Selection

```cpp
class LinuxWSI : public PlatformWSI {
public:
    static std::unique_ptr<LinuxWSI> create(VkSurfaceKHR surface) {
        // Detect display server
        bool is_wayland = (getenv("WAYLAND_DISPLAY") != nullptr);

        // Try GBM first (primary path)
        if (try_init_gbm()) {
            if (is_wayland) {
                return std::make_unique<WaylandGBM_WSI>(surface);
            } else {
                return std::make_unique<X11_DRI3_WSI>(surface);
            }
        }

        // Fallback to CPU buffers
        if (is_wayland) {
            return std::make_unique<WaylandSHM_WSI>(surface);
        } else {
            return std::make_unique<X11_PutImage_WSI>(surface);
        }
    }

private:
    static bool try_init_gbm() {
        // Try to find a usable DRM render node
        for (int i = 128; i < 136; i++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);

            int fd = open(path, O_RDWR);
            if (fd < 0) continue;

            struct gbm_device* gbm = gbm_create_device(fd);
            if (gbm) {
                gbm_device_destroy(gbm);
                close(fd);
                return true;
            }
            close(fd);
        }
        return false;
    }
};
```

### Method Comparison

| Method | Zero-copy | Requirements | Performance |
|--------|-----------|--------------|-------------|
| X11 DRI3 + GBM | Yes | DRI3, Mesa GPU | Best |
| Wayland DMA-BUF + GBM | Yes | linux-dmabuf, Mesa GPU | Best |
| X11 XPutImage | No | Basic X11 | Good |
| Wayland wl_shm | No | Basic Wayland | Good |

---

## Primary Path: GBM + DMA-BUF

### X11 with DRI3/Present

```cpp
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <gbm.h>

class X11_DRI3_WSI : public PlatformWSI {
private:
    // X11 connection
    xcb_connection_t* conn;
    xcb_window_t window;

    // GBM device
    int drm_fd;
    struct gbm_device* gbm;

    // Buffers
    struct Buffer {
        struct gbm_bo* bo;
        int dma_buf_fd;
        void* mapped;
        void* map_data;    // MUST store for gbm_bo_unmap
        uint32_t stride;
        size_t size;
        xcb_pixmap_t pixmap;
        bool in_use;       // Track if compositor is using this buffer
    };
    std::vector<Buffer> buffers;

    // Present extension event handling
    xcb_special_event_t* present_event;
    uint32_t present_event_id;

    uint32_t width, height;

public:
    bool init_buffers(uint32_t w, uint32_t h,
                      VkFormat format, uint32_t count) override {
        width = w;
        height = h;

        // Open DRM device
        drm_fd = find_and_open_drm_device();
        if (drm_fd < 0) return false;

        gbm = gbm_create_device(drm_fd);
        if (!gbm) {
            close(drm_fd);
            return false;
        }

        // Check DRI3 support
        if (!check_dri3_support(conn)) {
            gbm_device_destroy(gbm);
            close(drm_fd);
            return false;
        }

        buffers.resize(count);

        for (uint32_t i = 0; i < count; i++) {
            // Create GBM buffer object
            buffers[i].bo = gbm_bo_create(
                gbm, w, h,
                vkformat_to_gbm(format),
                GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING
            );

            if (!buffers[i].bo) {
                destroy();
                return false;
            }

            // Get DMA-BUF fd
            buffers[i].dma_buf_fd = gbm_bo_get_fd(buffers[i].bo);
            buffers[i].size = gbm_bo_get_stride(buffers[i].bo) * h;

            // Map for CPU write
            buffers[i].mapped = gbm_bo_map(
                buffers[i].bo, 0, 0, w, h,
                GBM_BO_TRANSFER_WRITE,
                &buffers[i].stride, &buffers[i].map_data
            );

            if (!buffers[i].mapped) {
                destroy();
                return false;
            }

            buffers[i].in_use = false;

            // Import into X11 as pixmap
            buffers[i].pixmap = xcb_generate_id(conn);
            xcb_dri3_pixmap_from_buffer(
                conn,
                buffers[i].pixmap,
                window,
                buffers[i].size,
                w, h,
                gbm_bo_get_stride(buffers[i].bo),
                depth_from_format(format),
                bpp_from_format(format),
                buffers[i].dma_buf_fd
            );
        }

        // Register for Present events to track buffer idle status
        present_event_id = xcb_generate_id(conn);
        xcb_present_select_input(conn, present_event_id, window,
                                  XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY);
        present_event = xcb_register_for_special_xge(
            conn, &xcb_present_id, present_event_id, NULL);

        xcb_flush(conn);
        return true;
    }

    BufferInfo get_buffer(uint32_t index) override {
        // Wait for buffer to be released by compositor
        while (buffers[index].in_use) {
            process_present_events();
        }
        return {
            .data = buffers[index].mapped,
            .stride = buffers[index].stride
        };
    }

    void end_buffer_access(uint32_t index) override {
        // Unmap to sync CPU writes using the correct map_data handle
        gbm_bo_unmap(buffers[index].bo, buffers[index].map_data);
    }

    void present(uint32_t index) override {
        static uint32_t serial = 0;

        buffers[index].in_use = true;  // Mark as owned by compositor

        // Present via DRI3 Present extension
        xcb_present_pixmap(
            conn,
            window,
            buffers[index].pixmap,
            serial++,
            XCB_NONE,               // valid region (full)
            XCB_NONE,               // update region (full)
            0, 0,                   // x, y offset
            XCB_NONE,               // target CRTC
            XCB_NONE,               // wait fence
            XCB_NONE,               // idle fence
            XCB_PRESENT_OPTION_NONE,
            0, 0, 0,                // target MSC, divisor, remainder
            0, NULL                 // notifies
        );

        xcb_flush(conn);

        // Re-map will happen in get_buffer() after compositor releases buffer
    }

private:
    void process_present_events() {
        xcb_generic_event_t* event;
        while ((event = xcb_poll_for_special_event(conn, present_event))) {
            xcb_present_generic_event_t* pge = (xcb_present_generic_event_t*)event;

            if (pge->evtype == XCB_PRESENT_EVENT_IDLE_NOTIFY) {
                xcb_present_idle_notify_event_t* idle =
                    (xcb_present_idle_notify_event_t*)event;

                // Find which buffer this pixmap belongs to
                for (size_t i = 0; i < buffers.size(); i++) {
                    if (buffers[i].pixmap == idle->pixmap) {
                        buffers[i].in_use = false;

                        // Re-map the buffer for next use
                        buffers[i].mapped = gbm_bo_map(
                            buffers[i].bo, 0, 0, width, height,
                            GBM_BO_TRANSFER_WRITE,
                            &buffers[i].stride, &buffers[i].map_data
                        );
                        break;
                    }
                }
            }

            free(event);
        }
    }

    void destroy() override {
        for (auto& buf : buffers) {
            if (buf.pixmap) {
                xcb_free_pixmap(conn, buf.pixmap);
            }
            if (buf.mapped && buf.bo && buf.map_data) {
                gbm_bo_unmap(buf.bo, buf.map_data);  // Use map_data, not mapped!
            }
            if (buf.bo) {
                gbm_bo_destroy(buf.bo);
            }
        }
        buffers.clear();

        if (gbm) {
            gbm_device_destroy(gbm);
            gbm = nullptr;
        }
        if (drm_fd >= 0) {
            close(drm_fd);
            drm_fd = -1;
        }
    }

private:
    int find_and_open_drm_device() {
        for (int i = 128; i < 136; i++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);
            int fd = open(path, O_RDWR);
            if (fd >= 0) return fd;
        }
        return -1;
    }

    bool check_dri3_support(xcb_connection_t* c) {
        xcb_dri3_query_version_reply_t* reply = xcb_dri3_query_version_reply(
            c,
            xcb_dri3_query_version(c, 1, 2),
            NULL
        );
        bool supported = (reply != NULL);
        free(reply);
        return supported;
    }

    uint32_t vkformat_to_gbm(VkFormat format) {
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
};
```

### Wayland with DMA-BUF

```cpp
#include <wayland-client.h>
#include <linux-dmabuf-unstable-v1-client-protocol.h>
#include <gbm.h>

class WaylandGBM_WSI : public PlatformWSI {
private:
    // Wayland
    wl_display* display;
    wl_surface* surface;
    zwp_linux_dmabuf_v1* dmabuf;

    // GBM
    int drm_fd;
    struct gbm_device* gbm;

    // Buffers
    struct Buffer {
        struct gbm_bo* bo;
        void* mapped;
        void* map_data;    // MUST store for gbm_bo_unmap
        uint32_t stride;
        wl_buffer* wl_buf;
        bool in_use;       // Track compositor ownership
    };
    std::vector<Buffer> buffers;

    uint32_t width, height;

    // Buffer release callback
    static void buffer_release(void* data, wl_buffer* buffer) {
        Buffer* buf = (Buffer*)data;
        buf->in_use = false;

        // Re-map for next use
        // Note: Need access to width/height, stored in parent class
    }

    static const wl_buffer_listener buffer_listener;

public:
    bool init_buffers(uint32_t w, uint32_t h,
                      VkFormat format, uint32_t count) override {
        width = w;
        height = h;

        // Initialize GBM
        drm_fd = find_and_open_drm_device();
        if (drm_fd < 0) return false;

        gbm = gbm_create_device(drm_fd);
        if (!gbm) {
            close(drm_fd);
            return false;
        }

        buffers.resize(count);

        for (uint32_t i = 0; i < count; i++) {
            // Create GBM buffer
            buffers[i].bo = gbm_bo_create(
                gbm, w, h,
                vkformat_to_gbm(format),
                GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING
            );

            if (!buffers[i].bo) {
                destroy();
                return false;
            }

            // Map for CPU write - store map_data!
            buffers[i].mapped = gbm_bo_map(
                buffers[i].bo, 0, 0, w, h,
                GBM_BO_TRANSFER_WRITE,
                &buffers[i].stride, &buffers[i].map_data
            );

            buffers[i].in_use = false;

            // Create wl_buffer from DMA-BUF
            int fd = gbm_bo_get_fd(buffers[i].bo);

            zwp_linux_buffer_params_v1* params =
                zwp_linux_dmabuf_v1_create_params(dmabuf);

            zwp_linux_buffer_params_v1_add(
                params,
                fd,
                0,                              // plane index
                0,                              // offset
                buffers[i].stride,
                DRM_FORMAT_MOD_LINEAR >> 32,
                DRM_FORMAT_MOD_LINEAR & 0xFFFFFFFF
            );

            buffers[i].wl_buf = zwp_linux_buffer_params_v1_create_immed(
                params,
                w, h,
                vkformat_to_drm(format),
                0
            );

            // Add release listener
            wl_buffer_add_listener(buffers[i].wl_buf, &buffer_listener, &buffers[i]);

            zwp_linux_buffer_params_v1_destroy(params);
            close(fd);
        }

        return true;
    }

    BufferInfo get_buffer(uint32_t index) override {
        // Wait for buffer to be released by compositor
        while (buffers[index].in_use) {
            wl_display_dispatch(display);
        }

        // Re-map if needed (after release)
        if (!buffers[index].mapped) {
            buffers[index].mapped = gbm_bo_map(
                buffers[index].bo, 0, 0, width, height,
                GBM_BO_TRANSFER_WRITE,
                &buffers[index].stride, &buffers[index].map_data
            );
        }

        return {
            .data = buffers[index].mapped,
            .stride = buffers[index].stride
        };
    }

    void end_buffer_access(uint32_t index) override {
        // Unmap using correct map_data handle
        gbm_bo_unmap(buffers[index].bo, buffers[index].map_data);
        buffers[index].mapped = nullptr;
    }

    void present(uint32_t index) override {
        buffers[index].in_use = true;  // Mark as owned by compositor

        // Present
        wl_surface_attach(surface, buffers[index].wl_buf, 0, 0);
        wl_surface_damage_buffer(surface, 0, 0, width, height);
        wl_surface_commit(surface);
        wl_display_flush(display);
    }

    void destroy() override {
        for (auto& buf : buffers) {
            if (buf.wl_buf) {
                wl_buffer_destroy(buf.wl_buf);
            }
            if (buf.mapped && buf.bo && buf.map_data) {
                gbm_bo_unmap(buf.bo, buf.map_data);  // Use map_data!
            }
            if (buf.bo) {
                gbm_bo_destroy(buf.bo);
            }
        }
        buffers.clear();

        if (gbm) gbm_device_destroy(gbm);
        if (drm_fd >= 0) close(drm_fd);
    }

private:
    int find_and_open_drm_device() {
        for (int i = 128; i < 136; i++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);
            int fd = open(path, O_RDWR);
            if (fd >= 0) return fd;
        }
        return -1;
    }
};

// Static listener definition
const wl_buffer_listener WaylandGBM_WSI::buffer_listener = {
    .release = WaylandGBM_WSI::buffer_release
};
```

---

## Fallback Path: CPU Buffers

### X11 with XPutImage

Used when DRI3 or GBM is unavailable:

```cpp
#include <xcb/xcb.h>
#include <xcb/xcb_image.h>

class X11_PutImage_WSI : public PlatformWSI {
private:
    xcb_connection_t* conn;
    xcb_window_t window;
    xcb_gcontext_t gc;

    struct Buffer {
        void* data;
        size_t size;
        xcb_image_t* image;
    };
    std::vector<Buffer> buffers;

    uint32_t width, height;

public:
    bool init_buffers(uint32_t w, uint32_t h,
                      VkFormat format, uint32_t count) override {
        width = w;
        height = h;

        // Create graphics context
        gc = xcb_generate_id(conn);
        xcb_create_gc(conn, gc, window, 0, NULL);

        buffers.resize(count);
        size_t stride = w * 4;  // RGBA
        size_t size = stride * h;

        for (uint32_t i = 0; i < count; i++) {
            // Allocate CPU buffer
            buffers[i].data = malloc(size);
            buffers[i].size = size;

            if (!buffers[i].data) {
                destroy();
                return false;
            }

            // Create XCB image
            buffers[i].image = xcb_image_create_native(
                conn,
                w, h,
                XCB_IMAGE_FORMAT_Z_PIXMAP,
                24,  // depth
                buffers[i].data,
                size,
                (uint8_t*)buffers[i].data
            );
        }

        return true;
    }

    BufferInfo get_buffer(uint32_t index) override {
        return {
            .data = buffers[index].data,
            .stride = width * 4  // Tightly packed for XPutImage
        };
    }

    void end_buffer_access(uint32_t index) override {
        // No-op for CPU buffers
    }

    void present(uint32_t index) override {
        // Put image to window
        xcb_image_put(
            conn,
            window,
            gc,
            buffers[index].image,
            0, 0,  // x, y
            0      // left pad
        );

        xcb_flush(conn);
    }

    void destroy() override {
        for (auto& buf : buffers) {
            if (buf.image) {
                xcb_image_destroy(buf.image);
            }
            if (buf.data) {
                free(buf.data);
            }
        }
        buffers.clear();

        if (gc) {
            xcb_free_gc(conn, gc);
            gc = 0;
        }
    }
};
```

### Wayland with wl_shm

Used when linux-dmabuf or GBM is unavailable:

```cpp
#include <wayland-client.h>
#include <sys/mman.h>

class WaylandSHM_WSI : public PlatformWSI {
private:
    wl_display* display;
    wl_surface* surface;
    wl_shm* shm;

    struct Buffer {
        int fd;
        void* data;
        size_t size;
        uint32_t stride;
        wl_shm_pool* pool;
        wl_buffer* buffer;
        bool in_use;        // Track compositor ownership
    };
    std::vector<Buffer> buffers;

    uint32_t width, height;

    // Buffer release callback - called when compositor is done with buffer
    static void buffer_release(void* data, wl_buffer* buffer) {
        Buffer* buf = (Buffer*)data;
        buf->in_use = false;
    }

    static const wl_buffer_listener buffer_listener;

public:
    bool init_buffers(uint32_t w, uint32_t h,
                      VkFormat format, uint32_t count) override {
        width = w;
        height = h;

        buffers.resize(count);
        uint32_t stride = w * 4;
        size_t size = stride * h;

        for (uint32_t i = 0; i < count; i++) {
            // Create shared memory file
            buffers[i].fd = memfd_create("venus_frame", MFD_CLOEXEC);
            if (buffers[i].fd < 0) {
                destroy();
                return false;
            }

            if (ftruncate(buffers[i].fd, size) < 0) {
                destroy();
                return false;
            }

            // Map memory
            buffers[i].data = mmap(NULL, size, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, buffers[i].fd, 0);
            if (buffers[i].data == MAP_FAILED) {
                destroy();
                return false;
            }
            buffers[i].size = size;
            buffers[i].stride = stride;
            buffers[i].in_use = false;

            // Create wl_shm_pool
            buffers[i].pool = wl_shm_create_pool(shm, buffers[i].fd, size);

            // Create wl_buffer
            buffers[i].buffer = wl_shm_pool_create_buffer(
                buffers[i].pool,
                0,                              // offset
                w, h,
                stride,
                WL_SHM_FORMAT_ARGB8888
            );

            // Add release listener
            wl_buffer_add_listener(buffers[i].buffer, &buffer_listener, &buffers[i]);
        }

        return true;
    }

    BufferInfo get_buffer(uint32_t index) override {
        // Wait for buffer to be released by compositor
        while (buffers[index].in_use) {
            wl_display_dispatch(display);
        }
        return {
            .data = buffers[index].data,
            .stride = buffers[index].stride
        };
    }

    void end_buffer_access(uint32_t index) override {
        // No-op for wl_shm (no unmap needed)
    }

    void present(uint32_t index) override {
        buffers[index].in_use = true;  // Mark as owned by compositor

        wl_surface_attach(surface, buffers[index].buffer, 0, 0);
        wl_surface_damage_buffer(surface, 0, 0, width, height);
        wl_surface_commit(surface);
        wl_display_flush(display);
    }

    void destroy() override {
        for (auto& buf : buffers) {
            if (buf.buffer) {
                wl_buffer_destroy(buf.buffer);
            }
            if (buf.pool) {
                wl_shm_pool_destroy(buf.pool);
            }
            if (buf.data && buf.data != MAP_FAILED) {
                munmap(buf.data, buf.size);
            }
            if (buf.fd >= 0) {
                close(buf.fd);
            }
        }
        buffers.clear();
    }
};

// Static listener definition
const wl_buffer_listener WaylandSHM_WSI::buffer_listener = {
    .release = WaylandSHM_WSI::buffer_release
};
```

---

## Surface Creation

### X11 Surface

```cpp
VkResult vkCreateXcbSurfaceKHR(
    VkInstance instance,
    const VkXcbSurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface) {

    auto* surface = new VenusSurface();
    surface->type = SURFACE_TYPE_XCB;
    surface->xcb.connection = pCreateInfo->connection;
    surface->xcb.window = pCreateInfo->window;

    *pSurface = (VkSurfaceKHR)surface;
    return VK_SUCCESS;
}

VkResult vkCreateXlibSurfaceKHR(
    VkInstance instance,
    const VkXlibSurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface) {

    auto* surface = new VenusSurface();
    surface->type = SURFACE_TYPE_XLIB;
    surface->xlib.display = pCreateInfo->dpy;
    surface->xlib.window = pCreateInfo->window;

    // Get XCB connection from Xlib display
    surface->xcb.connection = XGetXCBConnection(pCreateInfo->dpy);
    surface->xcb.window = (xcb_window_t)pCreateInfo->window;

    *pSurface = (VkSurfaceKHR)surface;
    return VK_SUCCESS;
}
```

### Wayland Surface

```cpp
VkResult vkCreateWaylandSurfaceKHR(
    VkInstance instance,
    const VkWaylandSurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface) {

    auto* surface = new VenusSurface();
    surface->type = SURFACE_TYPE_WAYLAND;
    surface->wayland.display = pCreateInfo->display;
    surface->wayland.surface = pCreateInfo->surface;

    *pSurface = (VkSurfaceKHR)surface;
    return VK_SUCCESS;
}
```

### Surface Capabilities

```cpp
VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surfaceHandle,
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) {

    auto* surface = (VenusSurface*)surfaceHandle;

    // Get window size
    uint32_t width, height;
    if (surface->type == SURFACE_TYPE_XCB) {
        xcb_get_geometry_reply_t* geom = xcb_get_geometry_reply(
            surface->xcb.connection,
            xcb_get_geometry(surface->xcb.connection, surface->xcb.window),
            NULL
        );
        width = geom->width;
        height = geom->height;
        free(geom);
    } else {
        // Wayland - get from surface or use default
        width = 800;
        height = 600;
    }

    *pSurfaceCapabilities = {
        .minImageCount = 2,
        .maxImageCount = 8,
        .currentExtent = {width, height},
        .minImageExtent = {1, 1},
        .maxImageExtent = {4096, 4096},
        .maxImageArrayLayers = 1,
        .supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT
    };

    return VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    uint32_t* pSurfaceFormatCount,
    VkSurfaceFormatKHR* pSurfaceFormats) {

    static const VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };

    if (!pSurfaceFormats) {
        *pSurfaceFormatCount = 2;
        return VK_SUCCESS;
    }

    uint32_t count = std::min(*pSurfaceFormatCount, 2u);
    memcpy(pSurfaceFormats, formats, count * sizeof(VkSurfaceFormatKHR));
    *pSurfaceFormatCount = count;

    return (count < 2) ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    uint32_t* pPresentModeCount,
    VkPresentModeKHR* pPresentModes) {

    static const VkPresentModeKHR modes[] = {
        VK_PRESENT_MODE_FIFO_KHR,       // Always supported
        VK_PRESENT_MODE_MAILBOX_KHR,    // Triple buffering
    };

    if (!pPresentModes) {
        *pPresentModeCount = 2;
        return VK_SUCCESS;
    }

    uint32_t count = std::min(*pPresentModeCount, 2u);
    memcpy(pPresentModes, modes, count * sizeof(VkPresentModeKHR));
    *pPresentModeCount = count;

    return (count < 2) ? VK_INCOMPLETE : VK_SUCCESS;
}
```

---

## Task Breakdown

| Day | Tasks |
|-----|-------|
| 1 | Surface creation (X11 XCB, Xlib, Wayland) |
| 2 | Surface capability queries |
| 3-4 | X11 DRI3 + GBM implementation |
| 5-6 | Wayland DMA-BUF + GBM implementation |
| 7 | X11 XPutImage fallback |
| 8 | Wayland wl_shm fallback |
| 9 | Path detection and auto-selection |
| 10 | Testing and debugging |

---

## Build Requirements

### Dependencies

```bash
# Ubuntu/Debian
sudo apt-get install \
    libxcb1-dev \
    libxcb-dri3-dev \
    libxcb-present-dev \
    libxcb-image0-dev \
    libx11-xcb-dev \
    libwayland-dev \
    libgbm-dev \
    libdrm-dev

# Fedora
sudo dnf install \
    libxcb-devel \
    xcb-util-image-devel \
    libX11-xcb-devel \
    wayland-devel \
    mesa-libgbm-devel \
    libdrm-devel
```

### CMake

```cmake
# Find packages
find_package(X11 REQUIRED)
find_package(XCB COMPONENTS XCB DRI3 PRESENT IMAGE)
pkg_check_modules(WAYLAND wayland-client)
pkg_check_modules(GBM gbm)
pkg_check_modules(DRM libdrm)

# Linux WSI sources
if(UNIX AND NOT APPLE)
    target_sources(venus_icd PRIVATE
        client/wsi/linux_wsi.cpp
        client/wsi/x11_dri3.cpp
        client/wsi/x11_putimage.cpp
        client/wsi/wayland_dmabuf.cpp
        client/wsi/wayland_shm.cpp
    )

    target_link_libraries(venus_icd PRIVATE
        ${X11_LIBRARIES}
        ${XCB_LIBRARIES}
    )

    if(GBM_FOUND AND DRM_FOUND)
        target_compile_definitions(venus_icd PRIVATE VENUS_HAS_GBM)
        target_link_libraries(venus_icd PRIVATE
            ${GBM_LIBRARIES}
            ${DRM_LIBRARIES}
        )
    endif()

    if(WAYLAND_FOUND)
        target_compile_definitions(venus_icd PRIVATE VENUS_HAS_WAYLAND)
        target_link_libraries(venus_icd PRIVATE ${WAYLAND_LIBRARIES})
    endif()
endif()
```

---

## Testing

### Test 1: Auto-detection

```bash
# Should auto-detect and use GBM if available
./test-app/phase10_wsi_test

# Expected output:
# Detecting WSI method...
# Found DRM device: /dev/dri/renderD128
# GBM device created successfully
# Using: X11 DRI3 + GBM (zero-copy)
```

### Test 2: Force Fallback

```bash
# Test fallback path
VENUS_WSI_FORCE_FALLBACK=1 ./test-app/phase10_wsi_test

# Expected output:
# Detecting WSI method...
# Forcing fallback mode
# Using: X11 XPutImage (CPU buffer)
```

### Test 3: Windowed Rendering

```cpp
// test-app/phase10/phase10_wsi_test.cpp
int main() {
    // Create window and surface
    // Create swapchain
    // Render loop with rotating triangle
    // Report FPS and method used
}
```

---

## Format Negotiation

### Problem

The server might use formats (e.g., HDR 10-bit) that the local DRM device doesn't support.

### Solution: Query and Restrict

Only advertise formats that both the server GPU and local display can handle:

```cpp
VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    uint32_t* pSurfaceFormatCount,
    VkSurfaceFormatKHR* pSurfaceFormats) {

    // Query local GBM/DRM supported formats
    std::vector<uint32_t> local_formats = query_local_drm_formats();

    // Query server supported formats
    std::vector<VkSurfaceFormatKHR> server_formats = query_server_formats();

    // Return intersection
    std::vector<VkSurfaceFormatKHR> supported;
    for (const auto& fmt : server_formats) {
        uint32_t drm_fmt = vkformat_to_drm(fmt.format);
        if (std::find(local_formats.begin(), local_formats.end(), drm_fmt)
            != local_formats.end()) {
            supported.push_back(fmt);
        }
    }

    // Always include basic formats as fallback
    if (supported.empty()) {
        supported.push_back({VK_FORMAT_B8G8R8A8_UNORM,
                            VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
    }

    // Return results
    if (!pSurfaceFormats) {
        *pSurfaceFormatCount = supported.size();
        return VK_SUCCESS;
    }

    uint32_t count = std::min(*pSurfaceFormatCount, (uint32_t)supported.size());
    memcpy(pSurfaceFormats, supported.data(), count * sizeof(VkSurfaceFormatKHR));
    *pSurfaceFormatCount = count;

    return (count < supported.size()) ? VK_INCOMPLETE : VK_SUCCESS;
}

std::vector<uint32_t> query_local_drm_formats() {
    std::vector<uint32_t> formats;

    if (gbm) {
        // GBM path - query device
        // Note: GBM doesn't have a direct format query API
        // Test common formats
        static const uint32_t test_formats[] = {
            GBM_FORMAT_ARGB8888,
            GBM_FORMAT_XRGB8888,
            GBM_FORMAT_ABGR8888,
            GBM_FORMAT_XBGR8888,
            // Add HDR formats if needed
            // GBM_FORMAT_ARGB2101010,
            // GBM_FORMAT_XRGB2101010,
        };

        for (uint32_t fmt : test_formats) {
            struct gbm_bo* bo = gbm_bo_create(gbm, 64, 64, fmt,
                                              GBM_BO_USE_LINEAR);
            if (bo) {
                formats.push_back(fmt);
                gbm_bo_destroy(bo);
            }
        }
    } else {
        // CPU fallback - support basic formats only
        formats.push_back(GBM_FORMAT_ARGB8888);
        formats.push_back(GBM_FORMAT_XRGB8888);
    }

    return formats;
}

uint32_t vkformat_to_drm(VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return GBM_FORMAT_ARGB8888;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return GBM_FORMAT_ABGR8888;
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            return GBM_FORMAT_ARGB2101010;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return GBM_FORMAT_ABGR2101010;
        default:
            return 0;  // Unsupported
    }
}
```

### HDR Support (Future)

For HDR formats (10-bit, 16-bit float):

1. **Local GPU must support the format** - Check via GBM
2. **Display must support HDR** - Check via DRM properties
3. **Compositor must support HDR** - Wayland has `wp_color_manager_v1`

For now, we only support 8-bit formats. HDR can be added later when:
- Server-side format conversion is implemented
- HDR metadata transfer is supported

---

## Performance Expectations

| Method | 1080p FPS (LAN) | Copy Count |
|--------|-----------------|------------|
| DRI3 + GBM | 45-60 | 2 (server→net, net→gbm) |
| XPutImage | 30-45 | 3 (server→net, net→cpu, cpu→X) |
| Wayland DMA-BUF | 45-60 | 2 |
| Wayland wl_shm | 30-45 | 3 |

---

## Success Criteria

- [ ] Surface creation works (X11 and Wayland)
- [ ] GBM path works with Intel/AMD Mesa drivers
- [ ] Fallback path works without GBM
- [ ] Auto-detection selects correct path
- [ ] Frames display correctly
- [ ] No tearing or artifacts
- [ ] Acceptable FPS (30+ on LAN)
- [ ] Clean shutdown, no leaks
