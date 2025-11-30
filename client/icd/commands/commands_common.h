#pragma once

// Common header for all ICD command implementations
// Shared includes, globals, helpers, and utilities

// Includes from original file (relative to client/ directory)
#include "icd/icd_entrypoints.h"
#include "icd/icd_instance.h"
#include "icd/icd_device.h"
#include "network/network_client.h"
#include "state/handle_allocator.h"
#include "state/instance_state.h"
#include "state/device_state.h"
#include "state/resource_state.h"
#include "state/query_state.h"
#include "state/pipeline_state.h"
#include "state/shadow_buffer.h"
#include "state/command_buffer_state.h"
#include "state/sync_state.h"
#include "state/swapchain_state.h"
#include "wsi/platform_wsi.h"
#include "protocol/memory_transfer.h"
#include "protocol/frame_transfer.h"
#include "protocol/remote_perf.h"
#include "branding.h"

// Venus protocol headers are C code - must be wrapped in extern "C" for C++ compilation
extern "C" {
#include "vn_protocol_driver.h"
#include "vn_ring.h"
}

#include "utils/logging.h"
#include <algorithm>
#include <array>
#include <iterator>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <atomic>
#include <new>
#include <vector>
#include <string>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include "wsi/linux_surface.h"
#include <xcb/xcb.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>

// Namespace
using namespace venus_plus;

// Logging macros
#define ICD_LOG_ERROR() VP_LOG_STREAM_ERROR(CLIENT)
#define ICD_LOG_WARN() VP_LOG_STREAM_WARN(CLIENT)
#define ICD_LOG_INFO() VP_LOG_STREAM_INFO(CLIENT)

inline bool memory_trace_enabled() {
    static const bool enabled = []() {
        const char* env = std::getenv("VENUS_TRACE_MEM");
        return env && env[0] != '0';
    }();
    return enabled;
}

inline bool latency_mode_enabled() {
    static const bool enabled = []() {
        const char* env = std::getenv("VENUS_LATENCY_MODE");
        return env && env[0] != '0';
    }();
    return enabled;
}

inline bool invalidate_on_wait_enabled() {
    static const bool enabled = []() {
        const char* env = std::getenv("VENUS_INVALIDATE_ON_WAIT");
        if (!env) {
            return true;
        }
        return !(env[0] == '0' || env[0] == 'f' || env[0] == 'F' || env[0] == 'n' || env[0] == 'N');
    }();
    return enabled;
}

inline VkDeviceSize invalidate_max_bytes() {
    static const VkDeviceSize max_bytes = []() -> VkDeviceSize {
        const char* env = std::getenv("VENUS_INVALIDATE_MAX_BYTES");
        if (!env || env[0] == '\0') {
            return 16 * 1024 * 1024; // default 16 MiB
        }
        char* end = nullptr;
        long long parsed = std::strtoll(env, &end, 10);
        if (end && (*end == 'm' || *end == 'M')) {
            parsed *= 1024LL * 1024LL;
        } else if (end && (*end == 'k' || *end == 'K')) {
            parsed *= 1024LL;
        }
        if (parsed <= 0) {
            return 0;
        }
        return static_cast<VkDeviceSize>(parsed);
    }();
    return max_bytes;
}

inline const std::unordered_set<uint64_t>& invalidate_handle_whitelist() {
    static const std::unordered_set<uint64_t> whitelist = []() {
        std::unordered_set<uint64_t> set;
        const char* env = std::getenv("VENUS_INVALIDATE_HANDLES");
        if (!env || env[0] == '\0') {
            return set;
        }
        std::string input(env);
        size_t start = 0;
        while (start < input.size()) {
            size_t comma = input.find(',', start);
            std::string token = input.substr(start, comma == std::string::npos ? input.size() - start : comma - start);
            if (!token.empty()) {
                uint64_t value = std::strtoull(token.c_str(), nullptr, 0);
                if (value != 0) {
                    set.insert(value);
                }
            }
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
        return set;
    }();
    return whitelist;
}

// Global connection state (defined in commands_common.cpp)
extern NetworkClient g_client;
extern vn_ring g_ring;
extern bool g_connected;

// Common helper functions (inline for performance)

inline bool ensure_connected() {
    if (!g_connected) {
        // Read server address from environment variables or use defaults
        const char* host = std::getenv("VENUS_SERVER_HOST");
        const char* port_str = std::getenv("VENUS_SERVER_PORT");

        std::string server_host = host ? host : "127.0.0.1";
        int server_port = port_str ? std::atoi(port_str) : 5556;

        // Validate port range
        if (server_port <= 0 || server_port > 65535) {
            ICD_LOG_ERROR() << "Invalid VENUS_SERVER_PORT: " << server_port
                           << " (must be 1-65535), using default 5556\n";
            server_port = 5556;
        }

        ICD_LOG_INFO() << "Connecting to Venus server at "
                       << server_host << ":" << server_port << "\n";

        if (!g_client.connect(server_host.c_str(), server_port)) {
            ICD_LOG_ERROR() << "Failed to connect to server at "
                           << server_host << ":" << server_port << "\n";
            return false;
        }

        g_ring.client = &g_client;
        g_connected = true;

        ICD_LOG_INFO() << "Successfully connected to Venus server\n";
    }
    return true;
}

inline bool ensure_command_buffer_tracked(VkCommandBuffer commandBuffer, const char* func_name) {
    if (!g_command_buffer_state.has_command_buffer(commandBuffer)) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " called with unknown command buffer\n";
        return false;
    }
    return true;
}

inline bool ensure_command_buffer_recording(VkCommandBuffer commandBuffer, const char* func_name) {
    if (!ensure_command_buffer_tracked(commandBuffer, func_name)) {
        return false;
    }
    CommandBufferLifecycleState state = g_command_buffer_state.get_buffer_state(commandBuffer);
    if (state != CommandBufferLifecycleState::RECORDING) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " requires RECORDING state (current="
                  << static_cast<int>(state) << ")\n";
        return false;
    }
    return true;
}

inline bool ensure_queue_tracked(VkQueue queue, VkQueue* remote_out) {
    if (!remote_out) {
        return false;
    }
    if (queue == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Queue handle is NULL\n";
        return false;
    }
    VkQueue remote_queue = g_device_state.get_remote_queue(queue);
    if (remote_queue == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Queue not tracked on client\n";
        return false;
    }
    *remote_out = remote_queue;
    return true;
}

inline VkCommandBuffer get_remote_command_buffer_handle(VkCommandBuffer commandBuffer) {
    VkCommandBuffer remote = g_command_buffer_state.get_remote_command_buffer(commandBuffer);
    if (remote != VK_NULL_HANDLE) {
        return remote;
    }
    IcdCommandBuffer* icd_cb = icd_command_buffer_from_handle(commandBuffer);
    return icd_cb ? icd_cb->remote_handle : VK_NULL_HANDLE;
}

inline VkPhysicalDevice get_remote_physical_device_handle(VkPhysicalDevice physicalDevice,
                                                          const char* func_name) {
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    if (!state) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " called with unknown physical device (no instance state)\n";
        return VK_NULL_HANDLE;
    }
    for (const auto& entry : state->physical_devices) {
        if (entry.local_handle == physicalDevice) {
            return entry.remote_handle;
        }
    }
    ICD_LOG_ERROR() << "[Client ICD] " << func_name << " unable to find remote handle for " << physicalDevice << "\n";
    return VK_NULL_HANDLE;
}

// Helper function: extension matching
inline bool matches_extension(const char* name, const char* const* list, size_t count) {
    if (!name || name[0] == '\0') {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(name, list[i]) == 0) {
            return true;
        }
    }
    return false;
}

// Helper function: check if WSI instance extension
inline bool is_wsi_instance_extension(const char* name) {
    static constexpr const char* kInstanceWsiExtensions[] = {
        "VK_KHR_surface",
        "VK_KHR_wayland_surface",
        "VK_KHR_xcb_surface",
        "VK_KHR_xlib_surface",
        "VK_KHR_win32_surface",
        "VK_KHR_android_surface",
        "VK_KHR_get_surface_capabilities2",
        "VK_KHR_surface_protected_capabilities",
        "VK_EXT_swapchain_colorspace",
        "VK_KHR_display",
        "VK_EXT_display_surface_counter",
        "VK_KHR_get_display_properties2",
        "VK_EXT_acquire_drm_display",
    };
    return matches_extension(
        name,
        kInstanceWsiExtensions,
        sizeof(kInstanceWsiExtensions) / sizeof(kInstanceWsiExtensions[0]));
}

// Helper function: check if WSI device extension
inline bool is_wsi_device_extension(const char* name) {
    static constexpr const char* kDeviceWsiExtensions[] = {
        "VK_KHR_swapchain",
        "VK_KHR_display_swapchain",
        "VK_KHR_incremental_present",
        "VK_EXT_display_control",
        "VK_EXT_full_screen_exclusive",
        "VK_EXT_swapchain_colorspace",
        "VK_EXT_surface_maintenance1",
        "VK_NV_present_barrier",
        "VK_QCOM_render_pass_store_ops",
        "VK_EXT_acquire_xlib_display",
    };
    return matches_extension(
        name,
        kDeviceWsiExtensions,
        sizeof(kDeviceWsiExtensions) / sizeof(kDeviceWsiExtensions[0]));
}

// Helper function: check if platform supports WSI extension
inline bool platform_supports_wsi_extension(const char* name, bool is_instance_extension) {
    if (!name) {
        return false;
    }
    if (is_instance_extension) {
        static constexpr const char* kSupportedInstanceExtensions[] = {
            "VK_KHR_surface",
            "VK_KHR_xcb_surface",
            "VK_KHR_xlib_surface",
            "VK_KHR_wayland_surface",
        };
        return matches_extension(
            name,
            kSupportedInstanceExtensions,
            sizeof(kSupportedInstanceExtensions) / sizeof(kSupportedInstanceExtensions[0]));
    } else {
        static constexpr const char* kSupportedDeviceExtensions[] = {
            "VK_KHR_swapchain",
        };
        return matches_extension(
            name,
            kSupportedDeviceExtensions,
            sizeof(kSupportedDeviceExtensions) / sizeof(kSupportedDeviceExtensions[0]));
    }
}

// Helper function: should we filter this instance extension
inline bool should_filter_instance_extension(const VkExtensionProperties& prop) {
    const char* name = prop.extensionName;
    if (!name || name[0] == '\0') {
        return false;
    }
    if (is_wsi_instance_extension(name) && !platform_supports_wsi_extension(name, true)) {
        return true;
    }
    return false;
}

// Helper function: should we filter this device extension
inline bool should_filter_device_extension(const VkExtensionProperties& prop) {
    const char* name = prop.extensionName;
    if (!name || name[0] == '\0') {
        return false;
    }

    if (is_wsi_device_extension(name) && !platform_supports_wsi_extension(name, false)) {
        return true;
    }

    static constexpr const char* kUnsupportedPrefixes[] = {
        "VK_KHR_video",
        "VK_STD_vulkan_video",
        "VK_EXT_video",
        "VK_NV_video",
        "VK_AMD_video",
    };

    for (const char* prefix : kUnsupportedPrefixes) {
        const size_t prefix_len = std::strlen(prefix);
        if (std::strncmp(name, prefix, prefix_len) == 0) {
            return true;
        }
    }
    return false;
}

// Helper function: find semaphore type info in pNext chain
inline const VkSemaphoreTypeCreateInfo* find_semaphore_type_info(const VkSemaphoreCreateInfo* info) {
    if (!info) {
        return nullptr;
    }
    const VkBaseInStructure* header = reinterpret_cast<const VkBaseInStructure*>(info->pNext);
    while (header) {
        if (header->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) {
            return reinterpret_cast<const VkSemaphoreTypeCreateInfo*>(header);
        }
        header = header->pNext;
    }
    return nullptr;
}

// Flush any pending submit batches (implemented in sync_commands.cpp)
VkResult venus_flush_submit_accumulator();

// Helper function: find timeline semaphore submit info in pNext chain
inline const VkTimelineSemaphoreSubmitInfo* find_timeline_submit_info(const void* pNext) {
    const VkBaseInStructure* header = reinterpret_cast<const VkBaseInStructure*>(pNext);
    while (header) {
        if (header->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
            return reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(header);
        }
        header = header->pNext;
    }
    return nullptr;
}

// Helper function: log pipeline executable stub warning once
inline void log_pipeline_exec_stub_once() {
    static bool warned = false;
    if (!warned) {
        ICD_LOG_WARN() << "[Client ICD] VK_KHR_pipeline_executable_properties not implemented yet; "
                       << "reporting empty statistics\n";
        warned = true;
    }
}

// Helper structures for command buffer commands
struct DependencyInfoStorage {
    VkDependencyInfo info{};
    std::vector<VkMemoryBarrier2> memory_barriers;
    std::vector<VkBufferMemoryBarrier2> buffer_barriers;
    std::vector<VkImageMemoryBarrier2> image_barriers;
};

struct RenderingInfoStorage {
    VkRenderingInfo info{};
    std::vector<VkRenderingAttachmentInfo> color_attachments;
    VkRenderingAttachmentInfo depth_attachment{};
    VkRenderingAttachmentInfo stencil_attachment{};
    bool has_depth = false;
    bool has_stencil = false;
};

// Helper function: check payload size
inline bool check_payload_size(size_t payload_size) {
    if (payload_size > std::numeric_limits<uint32_t>::max()) {
        ICD_LOG_ERROR() << "[Client ICD] Payload exceeds protocol limit (" << payload_size << " bytes)\n";
        return false;
    }
    return true;
}

// Helper function: translate attachment view handles
inline bool translate_attachment_view(VkRenderingAttachmentInfo* attachment,
                                      const char* func_name) {
    if (!attachment) {
        return true;
    }
    if (attachment->imageView != VK_NULL_HANDLE) {
        VkImageView remote_view = g_resource_state.get_remote_image_view(attachment->imageView);
        if (remote_view == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] " << func_name << ": image view not tracked\n";
            return false;
        }
        attachment->imageView = remote_view;
    }
    if (attachment->resolveImageView != VK_NULL_HANDLE) {
        VkImageView remote_resolve = g_resource_state.get_remote_image_view(attachment->resolveImageView);
        if (remote_resolve == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] " << func_name << ": resolve image view not tracked\n";
            return false;
        }
        attachment->resolveImageView = remote_resolve;
    }
    return true;
}

// Helper function: populate rendering info with handle translation
inline bool populate_rendering_info(const VkRenderingInfo* src,
                                    RenderingInfoStorage* storage,
                                    const char* func_name) {
    if (!src || !storage) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " missing rendering info\n";
        return false;
    }
    *storage = RenderingInfoStorage{};
    storage->info = *src;

    if (src->colorAttachmentCount > 0) {
        if (!src->pColorAttachments) {
            ICD_LOG_ERROR() << "[Client ICD] " << func_name << " missing color attachments\n";
            return false;
        }
        storage->color_attachments.assign(src->pColorAttachments,
                                          src->pColorAttachments + src->colorAttachmentCount);
        for (auto& attachment : storage->color_attachments) {
            if (!translate_attachment_view(&attachment, func_name)) {
                return false;
            }
        }
        storage->info.pColorAttachments = storage->color_attachments.data();
    } else {
        storage->info.pColorAttachments = nullptr;
    }

    if (src->pDepthAttachment) {
        storage->depth_attachment = *src->pDepthAttachment;
        if (!translate_attachment_view(&storage->depth_attachment, func_name)) {
            return false;
        }
        storage->info.pDepthAttachment = &storage->depth_attachment;
        storage->has_depth = true;
    } else {
        storage->info.pDepthAttachment = nullptr;
        storage->has_depth = false;
    }

    if (src->pStencilAttachment) {
        storage->stencil_attachment = *src->pStencilAttachment;
        if (!translate_attachment_view(&storage->stencil_attachment, func_name)) {
            return false;
        }
        storage->info.pStencilAttachment = &storage->stencil_attachment;
        storage->has_stencil = true;
    } else {
        storage->info.pStencilAttachment = nullptr;
        storage->has_stencil = false;
    }

    return true;
}

// Helper function: populate dependency info with handle translation
inline bool populate_dependency_info(const VkDependencyInfo* src,
                                     DependencyInfoStorage* storage,
                                     const char* func_name) {
    if (!src || !storage) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " missing dependency info\n";
        return false;
    }
    *storage = DependencyInfoStorage{};
    storage->info = *src;

    if (src->memoryBarrierCount > 0) {
        if (!src->pMemoryBarriers) {
            ICD_LOG_ERROR() << "[Client ICD] " << func_name << " missing memory barriers\n";
            return false;
        }
        storage->memory_barriers.assign(src->pMemoryBarriers,
                                        src->pMemoryBarriers + src->memoryBarrierCount);
        storage->info.pMemoryBarriers = storage->memory_barriers.data();
    } else {
        storage->info.pMemoryBarriers = nullptr;
    }

    if (src->bufferMemoryBarrierCount > 0) {
        if (!src->pBufferMemoryBarriers) {
            ICD_LOG_ERROR() << "[Client ICD] " << func_name << " missing buffer barriers\n";
            return false;
        }
        storage->buffer_barriers.assign(src->pBufferMemoryBarriers,
                                        src->pBufferMemoryBarriers + src->bufferMemoryBarrierCount);
        for (uint32_t i = 0; i < src->bufferMemoryBarrierCount; ++i) {
            VkBuffer remote_buffer = g_resource_state.get_remote_buffer(storage->buffer_barriers[i].buffer);
            if (storage->buffer_barriers[i].buffer != VK_NULL_HANDLE && remote_buffer == VK_NULL_HANDLE) {
                ICD_LOG_ERROR() << "[Client ICD] " << func_name << " buffer barrier " << i << " not tracked\n";
                return false;
            }
            storage->buffer_barriers[i].buffer = remote_buffer;
        }
        storage->info.pBufferMemoryBarriers = storage->buffer_barriers.data();
    } else {
        storage->info.pBufferMemoryBarriers = nullptr;
    }

    if (src->imageMemoryBarrierCount > 0) {
        if (!src->pImageMemoryBarriers) {
            ICD_LOG_ERROR() << "[Client ICD] " << func_name << " missing image barriers\n";
            return false;
        }
        storage->image_barriers.assign(src->pImageMemoryBarriers,
                                       src->pImageMemoryBarriers + src->imageMemoryBarrierCount);
        for (uint32_t i = 0; i < src->imageMemoryBarrierCount; ++i) {
            VkImage remote_image = g_resource_state.get_remote_image(storage->image_barriers[i].image);
            if (storage->image_barriers[i].image != VK_NULL_HANDLE && remote_image == VK_NULL_HANDLE) {
                ICD_LOG_ERROR() << "[Client ICD] " << func_name << " image barrier " << i << " not tracked\n";
                return false;
            }
            storage->image_barriers[i].image = remote_image;
        }
        storage->info.pImageMemoryBarriers = storage->image_barriers.data();
    } else {
        storage->info.pImageMemoryBarriers = nullptr;
    }

    return true;
}

// Helper function: validate memory offset for buffer/image binding
inline bool validate_memory_offset(const VkMemoryRequirements& requirements,
                                   VkDeviceSize memory_size,
                                   VkDeviceSize offset) {
    if (requirements.alignment != 0 && (offset % requirements.alignment) != 0) {
        return false;
    }
    if (memory_size != 0 && offset + requirements.size > memory_size) {
        return false;
    }
    return true;
}

// Helper function: validate buffer regions for copy commands
inline bool validate_buffer_regions(uint32_t count, const void* regions, const char* func_name) {
    if (count == 0 || !regions) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " requires valid regions\n";
        return false;
    }
    return true;
}

// Helper function: ensure remote buffer handle exists
inline bool ensure_remote_buffer(VkBuffer buffer, VkBuffer* remote, const char* func_name) {
    *remote = g_resource_state.get_remote_buffer(buffer);
    if (*remote == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " buffer not tracked\n";
        return false;
    }
    return true;
}

// Helper function: ensure remote image handle exists
inline bool ensure_remote_image(VkImage image, VkImage* remote, const char* func_name) {
    *remote = g_resource_state.get_remote_image(image);
    if (*remote == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " image not tracked\n";
        return false;
    }
    return true;
}

// Helper function: allocate Linux surface (WSI)
#if defined(__linux__) && !defined(__ANDROID__)
inline VkResult allocate_linux_surface(LinuxSurfaceType type, VkSurfaceKHR* out_surface) {
    if (!out_surface) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    auto* surface = new (std::nothrow) LinuxSurface();
    if (!surface) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    surface->type = type;
    *out_surface = reinterpret_cast<VkSurfaceKHR>(surface);
    return VK_SUCCESS;
}
#endif

// Forward declarations for helper functions implemented in specific command files
VkResult send_transfer_memory_data(VkDeviceMemory memory,
                                   VkDeviceSize offset,
                                   VkDeviceSize size,
                                   const void* data);
VkResult read_memory_data(VkDeviceMemory memory,
                         VkDeviceSize offset,
                         VkDeviceSize size,
                         void* dst);
bool send_swapchain_command(const void* request,
                            size_t request_size,
                            std::vector<uint8_t>* reply);

struct FlushBatchPayload {
    std::vector<uint8_t> payload;
    std::vector<ShadowCoherentRange> ranges;
};

struct InvalidateBatchPayload {
    std::vector<uint8_t> request;
    std::vector<ShadowCoherentRange> ranges;
    size_t total_bytes = 0;
    size_t largest_range = 0;
    size_t skipped_dirty = 0;
    size_t skipped_large = 0;
    size_t skipped_handle = 0;
};

inline void finalize_flush_ranges(const FlushBatchPayload& batch) {
    for (const auto& range : batch.ranges) {
        g_shadow_buffer_manager.finalize_coherent_range_flush(range);
    }
}

inline VkResult build_flush_payload(VkDevice device, FlushBatchPayload* out) {
    if (!out) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    out->payload.clear();
    out->ranges.clear();
    if (device == VK_NULL_HANDLE) {
        return VK_SUCCESS;
    }

    g_shadow_buffer_manager.collect_dirty_coherent_ranges(device, &out->ranges);
    if (out->ranges.empty()) {
        return VK_SUCCESS;
    }

    size_t total_bytes = 0;
    for (const auto& range : out->ranges) {
        if (!range.data || range.size == 0) {
            continue;
        }
        if (range.size > static_cast<VkDeviceSize>(std::numeric_limits<size_t>::max())) {
            ICD_LOG_ERROR() << "[Client ICD] Flush range too large";
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        total_bytes += static_cast<size_t>(range.size);
    }

    if (out->ranges.size() > std::numeric_limits<uint32_t>::max()) {
        ICD_LOG_ERROR() << "[Client ICD] Too many ranges to flush";
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    const size_t header_bytes = sizeof(TransferMemoryBatchHeader) +
                                out->ranges.size() * sizeof(TransferMemoryRange);
    const size_t payload_size = header_bytes + total_bytes;
    if (total_bytes == 0) {
        return VK_SUCCESS;
    }
    if (!check_payload_size(payload_size)) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    out->payload.resize(payload_size);
    auto* header = reinterpret_cast<TransferMemoryBatchHeader*>(out->payload.data());
    header->command = VENUS_PLUS_CMD_TRANSFER_MEMORY_BATCH;
    header->range_count = static_cast<uint32_t>(out->ranges.size());
    auto* range_out = reinterpret_cast<TransferMemoryRange*>(out->payload.data() + sizeof(TransferMemoryBatchHeader));
    uint8_t* data_out = reinterpret_cast<uint8_t*>(range_out + out->ranges.size());

    size_t copied = 0;
    for (size_t i = 0; i < out->ranges.size(); ++i) {
        const auto& range = out->ranges[i];
        VkDeviceMemory remote_mem = g_resource_state.get_remote_memory(range.memory);
        if (remote_mem == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Missing remote memory handle for flush";
            finalize_flush_ranges(*out);
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        range_out[i].memory_handle = reinterpret_cast<uint64_t>(remote_mem);
        range_out[i].offset = range.offset;
        range_out[i].size = range.size;
        if (!range.data || range.size == 0) {
            continue;
        }
        g_shadow_buffer_manager.prepare_coherent_range_flush(range);
        std::memcpy(data_out + copied, range.data, static_cast<size_t>(range.size));
        copied += static_cast<size_t>(range.size);
    }

    return VK_SUCCESS;
}

inline VkResult send_flush_payload(const FlushBatchPayload& batch) {
    if (batch.payload.empty()) {
        return VK_SUCCESS;
    }

    if (!g_client.send(batch.payload.data(), batch.payload.size())) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to send batch memory transfer";
        finalize_flush_ranges(batch);
        return VK_ERROR_DEVICE_LOST;
    }

    std::vector<uint8_t> reply;
    if (!g_client.receive(reply) || reply.size() < sizeof(VkResult)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to receive batch transfer reply";
        finalize_flush_ranges(batch);
        return VK_ERROR_DEVICE_LOST;
    }

    VkResult result = VK_ERROR_DEVICE_LOST;
    std::memcpy(&result, reply.data(), sizeof(VkResult));
    finalize_flush_ranges(batch);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] Batch memory transfer failed: " << result << "\n";
    }
    return result;
}

inline VkResult flush_host_coherent_mappings(VkDevice device) {
    FlushBatchPayload batch;
    VkResult prep = build_flush_payload(device, &batch);
    if (prep != VK_SUCCESS || batch.payload.empty()) {
        return prep;
    }
    return send_flush_payload(batch);
}

inline void finalize_invalidate_ranges(const InvalidateBatchPayload& payload) {
    for (const auto& range : payload.ranges) {
        g_shadow_buffer_manager.finalize_coherent_range_invalidate(range);
    }
}

inline void update_invalidate_timing(uint64_t elapsed_us, uint64_t total_bytes, bool trace_mem) {
    static std::atomic<uint64_t> timing_calls{0};
    static std::atomic<uint64_t> timing_total_us{0};
    static std::atomic<uint64_t> timing_total_bytes{0};
    static std::atomic<uint64_t> timing_max_us{0};

    const uint64_t calls = timing_calls.fetch_add(1) + 1;
    timing_total_us.fetch_add(elapsed_us);
    timing_total_bytes.fetch_add(total_bytes);
    uint64_t prev_max = timing_max_us.load(std::memory_order_relaxed);
    while (elapsed_us > prev_max &&
           !timing_max_us.compare_exchange_weak(prev_max, elapsed_us, std::memory_order_relaxed)) {
    }
    if (trace_mem && (calls % 100 == 0)) {
        const uint64_t total_us = timing_total_us.load(std::memory_order_relaxed);
        const uint64_t total_b = timing_total_bytes.load(std::memory_order_relaxed);
        const uint64_t max_us = timing_max_us.load(std::memory_order_relaxed);
        const double avg_us = calls ? static_cast<double>(total_us) / static_cast<double>(calls) : 0.0;
        const double avg_b = calls ? static_cast<double>(total_b) / static_cast<double>(calls) : 0.0;
        VP_LOG_STREAM_INFO(MEMORY) << "[Coherence] auto-invalidate summary: calls=" << calls
                                   << " avg_us=" << avg_us
                                   << " avg_bytes=" << avg_b
                                   << " max_us=" << max_us;
    }
}

inline VkResult build_invalidate_payload(VkDevice device,
                                         InvalidateBatchPayload* out,
                                         bool trace_mem) {
    if (!out) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    out->request.clear();
    out->ranges.clear();
    out->total_bytes = 0;
    out->largest_range = 0;
    out->skipped_dirty = 0;
    out->skipped_large = 0;
    out->skipped_handle = 0;

    if (device == VK_NULL_HANDLE) {
        return VK_SUCCESS;
    }
    const VkDeviceSize kMaxInvalidateBytes = invalidate_max_bytes(); // cap to avoid huge copies
    static std::atomic<bool> warned_skip{false};
    if (!invalidate_on_wait_enabled() || kMaxInvalidateBytes == 0) {
        return VK_SUCCESS;
    }
    std::vector<ShadowCoherentRange> ranges;
    g_shadow_buffer_manager.collect_host_coherent_ranges(device, &ranges);
    std::vector<ShadowCoherentRange> eligible;
    eligible.reserve(ranges.size());

    static std::mutex seen_mutex;
    static std::unordered_set<uint64_t> seen_handles;
    const auto& handle_whitelist = invalidate_handle_whitelist();

    for (const auto& range : ranges) {
        if (!range.data || range.size == 0) {
            continue;
        }
        if (range.size > kMaxInvalidateBytes) {
            ++out->skipped_large;
            if (!warned_skip.exchange(true)) {
                ICD_LOG_WARN() << "[Client ICD] Skipping host-coherent invalidate for large mapped range ("
                               << range.size << " bytes); data visibility relies on explicit vkInvalidateMappedMemoryRanges\n";
            }
            continue;
        }
        if (g_shadow_buffer_manager.range_has_dirty_pages(range)) {
            ++out->skipped_dirty;
            continue;
        }
        const uint64_t handle_key_val = reinterpret_cast<uint64_t>(range.memory);
        if (!handle_whitelist.empty() && handle_whitelist.count(handle_key_val) == 0) {
            ++out->skipped_handle;
            continue;
        }
        if (range.size > static_cast<VkDeviceSize>(std::numeric_limits<size_t>::max())) {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        out->total_bytes += static_cast<size_t>(range.size);
        out->largest_range = std::max(out->largest_range, static_cast<size_t>(range.size));
        eligible.push_back(range);
        if (trace_mem) {
            const uint64_t handle_key = reinterpret_cast<uint64_t>(range.memory);
            bool first_time = false;
            {
                std::lock_guard<std::mutex> lock(seen_mutex);
                first_time = seen_handles.insert(handle_key).second;
            }
            if (first_time) {
                VP_LOG_STREAM_INFO(MEMORY) << "[Coherence] auto-invalidate eligible handle=" << range.memory
                                           << " size=" << range.size
                                           << " threshold=" << kMaxInvalidateBytes;
            }
        }
    }

    if (trace_mem && (!eligible.empty() || out->skipped_dirty > 0 || out->skipped_large > 0)) {
        VP_LOG_STREAM_INFO(MEMORY) << "[Coherence] auto-invalidate: ranges=" << ranges.size()
                                   << " eligible=" << eligible.size()
                                   << " bytes=" << out->total_bytes
                                   << " largest=" << out->largest_range
                                   << " skipped_dirty=" << out->skipped_dirty
                                   << " skipped_large=" << out->skipped_large
                                   << " skipped_handle=" << out->skipped_handle
                                   << " cap=" << kMaxInvalidateBytes;
    }

    if (eligible.empty()) {
        return VK_SUCCESS;
    }

    if (eligible.size() > std::numeric_limits<uint32_t>::max()) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    const size_t header_bytes = sizeof(ReadMemoryBatchHeader) +
                                eligible.size() * sizeof(ReadMemoryRange);
    const size_t payload_size = header_bytes;
    if (!check_payload_size(payload_size)) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    out->request.resize(payload_size);
    auto* header = reinterpret_cast<ReadMemoryBatchHeader*>(out->request.data());
    header->command = VENUS_PLUS_CMD_READ_MEMORY_BATCH;
    header->range_count = static_cast<uint32_t>(eligible.size());
    auto* range_out = reinterpret_cast<ReadMemoryRange*>(out->request.data() + sizeof(ReadMemoryBatchHeader));
    for (size_t i = 0; i < eligible.size(); ++i) {
        const auto& range = eligible[i];
        VkDeviceMemory remote_mem = g_resource_state.get_remote_memory(range.memory);
        if (remote_mem == VK_NULL_HANDLE) {
            finalize_invalidate_ranges({{}, eligible});
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        range_out[i].memory_handle = reinterpret_cast<uint64_t>(remote_mem);
        range_out[i].offset = range.offset;
        range_out[i].size = range.size;
        g_shadow_buffer_manager.prepare_coherent_range_invalidate(range);
    }

    out->ranges = std::move(eligible);
    return VK_SUCCESS;
}

inline VkResult apply_invalidate_reply(const InvalidateBatchPayload& payload,
                                       const std::vector<uint8_t>& reply,
                                       uint64_t elapsed_us,
                                       bool trace_mem) {
    if (payload.request.empty()) {
        return VK_SUCCESS;
    }
    if (reply.size() < sizeof(ReadMemoryBatchReplyHeader)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to receive read batch reply";
        finalize_invalidate_ranges(payload);
        return VK_ERROR_DEVICE_LOST;
    }

    const auto* request_header =
        reinterpret_cast<const ReadMemoryBatchHeader*>(payload.request.data());

    ReadMemoryBatchReplyHeader reply_header = {};
    std::memcpy(&reply_header, reply.data(), sizeof(reply_header));
    if (reply_header.result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] Read batch failed: " << reply_header.result << "\n";
        finalize_invalidate_ranges(payload);
        return reply_header.result;
    }

    if (reply_header.range_count != request_header->range_count) {
        ICD_LOG_ERROR() << "[Client ICD] Read batch range count mismatch";
        finalize_invalidate_ranges(payload);
        return VK_ERROR_DEVICE_LOST;
    }

    size_t expected_payload = 0;
    for (const auto& range : payload.ranges) {
        expected_payload += static_cast<size_t>(range.size);
    }
    if (reply.size() != sizeof(ReadMemoryBatchReplyHeader) + expected_payload) {
        ICD_LOG_ERROR() << "[Client ICD] Read batch payload size mismatch";
        finalize_invalidate_ranges(payload);
        return VK_ERROR_DEVICE_LOST;
    }

    const uint8_t* data_ptr = reply.data() + sizeof(ReadMemoryBatchReplyHeader);
    size_t consumed = 0;
    for (const auto& range : payload.ranges) {
        if (range.size == 0) {
            continue;
        }
        std::memcpy(range.data, data_ptr + consumed, static_cast<size_t>(range.size));
        consumed += static_cast<size_t>(range.size);
        g_shadow_buffer_manager.finalize_coherent_range_invalidate(range);
    }

    update_invalidate_timing(elapsed_us, static_cast<uint64_t>(payload.total_bytes), trace_mem);
    return VK_SUCCESS;
}

inline VkResult invalidate_host_coherent_mappings(VkDevice device) {
    const bool trace_mem = memory_trace_enabled();
    InvalidateBatchPayload payload;
    VkResult prep = build_invalidate_payload(device, &payload, trace_mem);
    if (prep != VK_SUCCESS || payload.request.empty()) {
        return prep;
    }

    const auto send_start = std::chrono::steady_clock::now();
    if (!g_client.send(payload.request.data(), payload.request.size())) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to send read batch request";
        finalize_invalidate_ranges(payload);
        return VK_ERROR_DEVICE_LOST;
    }

    std::vector<uint8_t> reply;
    if (!g_client.receive(reply)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to receive read batch reply";
        finalize_invalidate_ranges(payload);
        return VK_ERROR_DEVICE_LOST;
    }

    const auto send_end = std::chrono::steady_clock::now();
    const uint64_t elapsed_us =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(send_end - send_start).count());

    return apply_invalidate_reply(payload, reply, elapsed_us, trace_mem);
}
