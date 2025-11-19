#include "icd_entrypoints.h"
#include "icd_instance.h"
#include "icd_device.h"
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
#include "branding.h"
#include "vn_protocol_driver.h"
#include "vn_ring.h"
#include "utils/logging.h"
#include <algorithm>
#include <array>
#include <iterator>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>
#include <string>

#if defined(__linux__) && !defined(__ANDROID__)
#include "wsi/linux_surface.h"
#include <xcb/xcb.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#endif

using namespace venus_plus;

#define ICD_LOG_ERROR() VP_LOG_STREAM_ERROR(CLIENT)
#define ICD_LOG_WARN() VP_LOG_STREAM_WARN(CLIENT)
#define ICD_LOG_INFO() VP_LOG_STREAM_INFO(CLIENT)

// For Phase 1-2, we'll use a simple global connection
static NetworkClient g_client;
static vn_ring g_ring = {};
static bool g_connected = false;

// Constructor - runs when the shared library is loaded
__attribute__((constructor))
static void icd_init() {
    ICD_LOG_INFO() << "\n===========================================\n";
    ICD_LOG_INFO() << "VENUS PLUS ICD LOADED!\n";
    ICD_LOG_INFO() << "===========================================\n\n";
}

static bool ensure_connected() {
    if (!g_connected) {
        // TODO: Get host/port from env variable
        if (!g_client.connect("127.0.0.1", 5556)) {
            return false;
        }
        g_ring.client = &g_client;
        g_connected = true;
    }
    return true;
}

static bool ensure_command_buffer_tracked(VkCommandBuffer commandBuffer, const char* func_name) {
    if (!g_command_buffer_state.has_command_buffer(commandBuffer)) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " called with unknown command buffer\n";
        return false;
    }
    return true;
}

static bool ensure_command_buffer_recording(VkCommandBuffer commandBuffer, const char* func_name) {
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

static VkCommandBuffer get_remote_command_buffer_handle(VkCommandBuffer commandBuffer) {
    VkCommandBuffer remote = g_command_buffer_state.get_remote_command_buffer(commandBuffer);
    if (remote != VK_NULL_HANDLE) {
        return remote;
    }
    IcdCommandBuffer* icd_cb = icd_command_buffer_from_handle(commandBuffer);
    return icd_cb ? icd_cb->remote_handle : VK_NULL_HANDLE;
}

static bool ensure_queue_tracked(VkQueue queue, VkQueue* remote_out) {
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

static const VkSemaphoreTypeCreateInfo* find_semaphore_type_info(const VkSemaphoreCreateInfo* info) {
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

static bool check_payload_size(size_t payload_size) {
    if (payload_size > std::numeric_limits<uint32_t>::max()) {
        ICD_LOG_ERROR() << "[Client ICD] Payload exceeds protocol limit (" << payload_size << " bytes)\n";
        return false;
    }
    return true;
}

static bool send_swapchain_command(const void* request,
                                   size_t request_size,
                                   std::vector<uint8_t>* reply) {
    if (!reply) {
        return false;
    }
    if (!g_client.send(request, request_size)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to send swapchain command";
        return false;
    }
    if (!g_client.receive(*reply)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to receive swapchain reply";
        return false;
    }
    return true;
}

static VkPhysicalDevice get_remote_physical_device_handle(VkPhysicalDevice physicalDevice,
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

static bool matches_extension(const char* name, const char* const* list, size_t count) {
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

static bool is_wsi_instance_extension(const char* name) {
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
        "VK_EXT_surface_maintenance1",
        "VK_EXT_headless_surface",
        "VK_EXT_directfb_surface",
        "VK_EXT_metal_surface",
        "VK_GOOGLE_surfaceless_query",
        "VK_MVK_ios_surface",
        "VK_MVK_macos_surface",
        "VK_QNX_screen_surface",
    };
    return matches_extension(
        name,
        kInstanceWsiExtensions,
        sizeof(kInstanceWsiExtensions) / sizeof(kInstanceWsiExtensions[0]));
}

static bool is_wsi_device_extension(const char* name) {
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

static bool platform_supports_wsi_extension(const char* name, bool is_instance_extension) {
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
        for (const char* ext : kSupportedInstanceExtensions) {
            if (std::strcmp(name, ext) == 0) {
                return true;
            }
        }
        return false;
    }
    if (std::strcmp(name, "VK_KHR_swapchain") == 0) {
        return true;
    }
    return false;
}

static bool should_filter_instance_extension(const VkExtensionProperties& prop) {
    const char* name = prop.extensionName;
    if (!name || name[0] == '\0') {
        return false;
    }
    if (is_wsi_instance_extension(name) && !platform_supports_wsi_extension(name, true)) {
        return true;
    }
    return false;
}

static bool should_filter_device_extension(const VkExtensionProperties& prop) {
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

static VkResult send_transfer_memory_data(VkDeviceMemory memory,
                                          VkDeviceSize offset,
                                          VkDeviceSize size,
                                          const void* data) {
    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);
    if (remote_memory == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Missing remote memory mapping for transfer\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (size == 0) {
        return VK_SUCCESS;
    }
    if (!data) {
        ICD_LOG_ERROR() << "[Client ICD] Transfer requested with null data pointer\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (size > static_cast<VkDeviceSize>(std::numeric_limits<size_t>::max())) {
        ICD_LOG_ERROR() << "[Client ICD] Transfer size exceeds host limits\n";
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    const size_t payload_size = sizeof(TransferMemoryDataHeader) + static_cast<size_t>(size);
    if (!check_payload_size(payload_size)) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    std::vector<uint8_t> payload(payload_size);
    TransferMemoryDataHeader header = {};
    header.command = VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA;
    header.memory_handle = reinterpret_cast<uint64_t>(remote_memory);
    header.offset = static_cast<uint64_t>(offset);
    header.size = static_cast<uint64_t>(size);

    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), data, static_cast<size_t>(size));

    if (!g_client.send(payload.data(), payload.size())) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to send memory transfer message\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::vector<uint8_t> reply;
    if (!g_client.receive(reply)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to receive memory transfer reply\n";
        return VK_ERROR_DEVICE_LOST;
    }

    if (reply.size() < sizeof(VkResult)) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid reply size for memory transfer\n";
        return VK_ERROR_DEVICE_LOST;
    }

    VkResult result = VK_ERROR_DEVICE_LOST;
    std::memcpy(&result, reply.data(), sizeof(VkResult));
    return result;
}

static VkResult read_memory_data(VkDeviceMemory memory,
                                 VkDeviceSize offset,
                                 VkDeviceSize size,
                                 void* dst) {
    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);
    if (remote_memory == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Missing remote memory mapping for read\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (size == 0) {
        return VK_SUCCESS;
    }
    if (!dst) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    if (size > static_cast<VkDeviceSize>(std::numeric_limits<size_t>::max())) {
        ICD_LOG_ERROR() << "[Client ICD] Read size exceeds host limits\n";
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    ReadMemoryDataRequest request = {};
    request.command = VENUS_PLUS_CMD_READ_MEMORY_DATA;
    request.memory_handle = reinterpret_cast<uint64_t>(remote_memory);
    request.offset = static_cast<uint64_t>(offset);
    request.size = static_cast<uint64_t>(size);

    if (!check_payload_size(sizeof(request))) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    if (!g_client.send(&request, sizeof(request))) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to send read memory request\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::vector<uint8_t> reply;
    if (!g_client.receive(reply)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to receive read memory reply\n";
        return VK_ERROR_DEVICE_LOST;
    }

    if (reply.size() < sizeof(VkResult)) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid reply for read memory request\n";
        return VK_ERROR_DEVICE_LOST;
    }

    VkResult result = VK_ERROR_DEVICE_LOST;
    std::memcpy(&result, reply.data(), sizeof(VkResult));
    if (result != VK_SUCCESS) {
        return result;
    }

    const size_t payload_size = reply.size() - sizeof(VkResult);
    if (payload_size != static_cast<size_t>(size)) {
        ICD_LOG_ERROR() << "[Client ICD] Read reply size mismatch (" << payload_size
                  << " vs " << size << ")\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::memcpy(dst, reply.data() + sizeof(VkResult), payload_size);
    return VK_SUCCESS;
}

#if defined(__linux__) && !defined(__ANDROID__)

static VkResult allocate_linux_surface(LinuxSurfaceType type,
                                       VkSurfaceKHR* out_surface) {
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

#if defined(__linux__) && !defined(__ANDROID__)

VKAPI_ATTR VkResult VKAPI_CALL vkCreateXcbSurfaceKHR(
    VkInstance instance,
    const VkXcbSurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface) {
    (void)instance;
    (void)pAllocator;
    if (!pCreateInfo || !pSurface || !pCreateInfo->connection || !pCreateInfo->window) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkSurfaceKHR handle = VK_NULL_HANDLE;
    VkResult result = allocate_linux_surface(LinuxSurfaceType::kXcb, &handle);
    if (result != VK_SUCCESS) {
        return result;
    }
    auto* surface = get_linux_surface(handle);
    surface->xcb.connection = pCreateInfo->connection;
    surface->xcb.window = pCreateInfo->window;
    query_linux_surface_extent(*surface);
    *pSurface = handle;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateXlibSurfaceKHR(
    VkInstance instance,
    const VkXlibSurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface) {
    (void)instance;
    (void)pAllocator;
    if (!pSurface || !pCreateInfo || !pCreateInfo->dpy) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    xcb_connection_t* connection = XGetXCBConnection(pCreateInfo->dpy);
    if (!connection) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkSurfaceKHR handle = VK_NULL_HANDLE;
    VkResult result = allocate_linux_surface(LinuxSurfaceType::kXcb, &handle);
    if (result != VK_SUCCESS) {
        return result;
    }
    auto* surface = get_linux_surface(handle);
    surface->xcb.connection = connection;
    surface->xcb.window = static_cast<uint32_t>(pCreateInfo->window);
    query_linux_surface_extent(*surface);
    *pSurface = handle;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateWaylandSurfaceKHR(
    VkInstance instance,
    const VkWaylandSurfaceCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSurfaceKHR* pSurface) {
    (void)instance;
    (void)pAllocator;
    if (!pSurface || !pCreateInfo || !pCreateInfo->display || !pCreateInfo->surface) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkSurfaceKHR handle = VK_NULL_HANDLE;
    VkResult result = allocate_linux_surface(LinuxSurfaceType::kWayland, &handle);
    if (result != VK_SUCCESS) {
        return result;
    }
    auto* surface = get_linux_surface(handle);
    surface->wayland.display = pCreateInfo->display;
    surface->wayland.surface = pCreateInfo->surface;
    *pSurface = handle;
    return VK_SUCCESS;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice physicalDevice,
    uint32_t queueFamilyIndex,
    xcb_connection_t* connection,
    xcb_visualid_t visual_id) {
    (void)physicalDevice;
    (void)queueFamilyIndex;
    (void)connection;
    (void)visual_id;
    return VK_TRUE;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceXlibPresentationSupportKHR(
    VkPhysicalDevice physicalDevice,
    uint32_t queueFamilyIndex,
    Display* dpy,
    VisualID visualID) {
    (void)physicalDevice;
    (void)queueFamilyIndex;
    (void)dpy;
    (void)visualID;
    return VK_TRUE;
}

VKAPI_ATTR VkBool32 VKAPI_CALL vkGetPhysicalDeviceWaylandPresentationSupportKHR(
    VkPhysicalDevice physicalDevice,
    uint32_t queueFamilyIndex,
    wl_display* display) {
    (void)physicalDevice;
    (void)queueFamilyIndex;
    (void)display;
    return VK_TRUE;
}

#endif

VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(
    VkInstance instance,
    VkSurfaceKHR surface,
    const VkAllocationCallbacks* pAllocator) {
    (void)instance;
    (void)pAllocator;
#if defined(__linux__) && !defined(__ANDROID__)
    if (is_linux_surface(surface)) {
        auto* info = get_linux_surface(surface);
        delete info;
        return;
    }
#endif
    (void)surface;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice physicalDevice,
    uint32_t queueFamilyIndex,
    VkSurfaceKHR surface,
    VkBool32* pSupported) {
    (void)physicalDevice;
    (void)queueFamilyIndex;
    (void)surface;
    if (!pSupported) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    *pSupported = VK_TRUE;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR* pSurfaceCapabilities) {
    (void)physicalDevice;
    if (!pSurfaceCapabilities) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
#if defined(__linux__) && !defined(__ANDROID__)
    VkExtent2D extent = {800, 600};
    bool variable_extent = false;
    if (is_linux_surface(surface)) {
        LinuxSurface* info = get_linux_surface(surface);
        if (info) {
            if (info->type == LinuxSurfaceType::kWayland) {
                variable_extent = true;
            } else {
                extent = query_linux_surface_extent(*info);
            }
        }
    }
    VkSurfaceCapabilitiesKHR caps = {};
    caps.minImageCount = 2;
    caps.maxImageCount = 8;
    caps.currentExtent = variable_extent ? VkExtent2D{0xFFFFFFFFu, 0xFFFFFFFFu} : extent;
    caps.minImageExtent = {1, 1};
    caps.maxImageExtent = {4096, 4096};
    caps.maxImageArrayLayers = 1;
    caps.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    caps.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    caps.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
                                   VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR |
                                   VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR |
                                   VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    caps.supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                               VK_IMAGE_USAGE_SAMPLED_BIT;
    *pSurfaceCapabilities = caps;
    return VK_SUCCESS;
#else
    (void)surface;
    return VK_ERROR_EXTENSION_NOT_PRESENT;
#endif
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    uint32_t* pSurfaceFormatCount,
    VkSurfaceFormatKHR* pSurfaceFormats) {
    (void)physicalDevice;
    (void)surface;
    if (!pSurfaceFormatCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    static const VkSurfaceFormatKHR kFormats[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    if (!pSurfaceFormats) {
        *pSurfaceFormatCount = static_cast<uint32_t>(std::size(kFormats));
        return VK_SUCCESS;
    }
    uint32_t copy_count = std::min(*pSurfaceFormatCount,
                                   static_cast<uint32_t>(std::size(kFormats)));
    std::memcpy(pSurfaceFormats, kFormats, copy_count * sizeof(VkSurfaceFormatKHR));
    if (*pSurfaceFormatCount < static_cast<uint32_t>(std::size(kFormats))) {
        *pSurfaceFormatCount = copy_count;
        return VK_INCOMPLETE;
    }
    *pSurfaceFormatCount = copy_count;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice,
    VkSurfaceKHR surface,
    uint32_t* pPresentModeCount,
    VkPresentModeKHR* pPresentModes) {
    (void)physicalDevice;
    (void)surface;
    if (!pPresentModeCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    static const VkPresentModeKHR kModes[] = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_MAILBOX_KHR,
    };
    if (!pPresentModes) {
        *pPresentModeCount = static_cast<uint32_t>(std::size(kModes));
        return VK_SUCCESS;
    }
    uint32_t copy_count = std::min(*pPresentModeCount,
                                   static_cast<uint32_t>(std::size(kModes)));
    std::memcpy(pPresentModes, kModes, copy_count * sizeof(VkPresentModeKHR));
    if (*pPresentModeCount < static_cast<uint32_t>(std::size(kModes))) {
        *pPresentModeCount = copy_count;
        return VK_INCOMPLETE;
    }
    *pPresentModeCount = copy_count;
    return VK_SUCCESS;
}

static const VkTimelineSemaphoreSubmitInfo* find_timeline_submit_info(const void* pNext) {
    const VkBaseInStructure* header = reinterpret_cast<const VkBaseInStructure*>(pNext);
    while (header) {
        if (header->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
            return reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(header);
        }
        header = header->pNext;
    }
    return nullptr;
}

extern "C" {

// Forward declarations for device-level functions (needed by vkGetDeviceProcAddr)
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator);
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue);

// ICD interface version negotiation
VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion) {
    ICD_LOG_INFO() << "[Client ICD] vk_icdNegotiateLoaderICDInterfaceVersion called\n";
    ICD_LOG_INFO() << "[Client ICD] Loader requested version: " << *pSupportedVersion << "\n";

    // Use ICD interface version 7 (latest version)
    // Version 7 adds support for additional loader features
    if (*pSupportedVersion > 7) {
        *pSupportedVersion = 7;
    }

    ICD_LOG_INFO() << "[Client ICD] Negotiated version: " << *pSupportedVersion << "\n";
    return VK_SUCCESS;
}

// ICD GetInstanceProcAddr
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName) {
    ICD_LOG_INFO() << "[Client ICD] vk_icdGetInstanceProcAddr called for: " << (pName ? pName : "NULL");

    if (pName == nullptr) {
        ICD_LOG_INFO() << " -> returning nullptr\n";
        return nullptr;
    }

    // Return our implementations
    if (strcmp(pName, "vkEnumerateInstanceVersion") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumerateInstanceVersion\n";
        return (PFN_vkVoidFunction)vkEnumerateInstanceVersion;
    }
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumerateInstanceExtensionProperties\n";
        return (PFN_vkVoidFunction)vkEnumerateInstanceExtensionProperties;
    }
    if (strcmp(pName, "vkCreateInstance") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateInstance\n";
        return (PFN_vkVoidFunction)vkCreateInstance;
    }
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetInstanceProcAddr\n";
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    }
    if (strcmp(pName, "vkDestroyInstance") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroyInstance\n";
        return (PFN_vkVoidFunction)vkDestroyInstance;
    }
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumeratePhysicalDevices\n";
        return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceFeatures\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceFormatProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceFormatProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceImageFormatProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties;
    }
    if (strcmp(pName, "vkCreateImageView") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateImageView\n";
        return (PFN_vkVoidFunction)vkCreateImageView;
    }
    if (strcmp(pName, "vkDestroyImageView") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroyImageView\n";
        return (PFN_vkVoidFunction)vkDestroyImageView;
    }
    if (strcmp(pName, "vkCreateBufferView") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateBufferView\n";
        return (PFN_vkVoidFunction)vkCreateBufferView;
    }
    if (strcmp(pName, "vkDestroyBufferView") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroyBufferView\n";
        return (PFN_vkVoidFunction)vkDestroyBufferView;
    }
    if (strcmp(pName, "vkCreateSampler") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateSampler\n";
        return (PFN_vkVoidFunction)vkCreateSampler;
    }
    if (strcmp(pName, "vkDestroySampler") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroySampler\n";
        return (PFN_vkVoidFunction)vkDestroySampler;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceImageFormatProperties2KHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceImageFormatProperties2\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties2;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceProperties2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceProperties2KHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceProperties2\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties2;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceQueueFamilyProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceQueueFamilyProperties2KHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceQueueFamilyProperties2\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties2;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceMemoryProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceMemoryProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceMemoryProperties2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceMemoryProperties2KHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceMemoryProperties2\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties2;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceFeatures2") == 0 ||
        strcmp(pName, "vkGetPhysicalDeviceFeatures2KHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceFeatures2\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures2;
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetDeviceProcAddr\n";
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    }
    if (strcmp(pName, "vkCreateDevice") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateDevice\n";
        return (PFN_vkVoidFunction)vkCreateDevice;
    }
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumerateDeviceExtensionProperties\n";
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    }
    if (strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkEnumerateDeviceLayerProperties\n";
        return (PFN_vkVoidFunction)vkEnumerateDeviceLayerProperties;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceSparseImageFormatProperties") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceSparseImageFormatProperties\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSparseImageFormatProperties;
    }
#if defined(VK_USE_PLATFORM_XCB_KHR)
    if (strcmp(pName, "vkCreateXcbSurfaceKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateXcbSurfaceKHR\n";
        return (PFN_vkVoidFunction)vkCreateXcbSurfaceKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceXcbPresentationSupportKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceXcbPresentationSupportKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceXcbPresentationSupportKHR;
    }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (strcmp(pName, "vkCreateXlibSurfaceKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateXlibSurfaceKHR\n";
        return (PFN_vkVoidFunction)vkCreateXlibSurfaceKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceXlibPresentationSupportKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceXlibPresentationSupportKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceXlibPresentationSupportKHR;
    }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (strcmp(pName, "vkCreateWaylandSurfaceKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateWaylandSurfaceKHR\n";
        return (PFN_vkVoidFunction)vkCreateWaylandSurfaceKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceWaylandPresentationSupportKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceWaylandPresentationSupportKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceWaylandPresentationSupportKHR;
    }
#endif
    if (strcmp(pName, "vkDestroySurfaceKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroySurfaceKHR\n";
        return (PFN_vkVoidFunction)vkDestroySurfaceKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceSurfaceSupportKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceSurfaceSupportKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceSupportKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceSurfaceCapabilitiesKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceSurfaceFormatsKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceSurfaceFormatsKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfaceFormatsKHR;
    }
    if (strcmp(pName, "vkGetPhysicalDeviceSurfacePresentModesKHR") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetPhysicalDeviceSurfacePresentModesKHR\n";
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceSurfacePresentModesKHR;
    }
    if (strcmp(pName, "vkCreateFence") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateFence\n";
        return (PFN_vkVoidFunction)vkCreateFence;
    }
    if (strcmp(pName, "vkDestroyFence") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroyFence\n";
        return (PFN_vkVoidFunction)vkDestroyFence;
    }
    if (strcmp(pName, "vkGetFenceStatus") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetFenceStatus\n";
        return (PFN_vkVoidFunction)vkGetFenceStatus;
    }
    if (strcmp(pName, "vkResetFences") == 0) {
        ICD_LOG_INFO() << " -> returning vkResetFences\n";
        return (PFN_vkVoidFunction)vkResetFences;
    }
    if (strcmp(pName, "vkWaitForFences") == 0) {
        ICD_LOG_INFO() << " -> returning vkWaitForFences\n";
        return (PFN_vkVoidFunction)vkWaitForFences;
    }
    if (strcmp(pName, "vkCreateSemaphore") == 0) {
        ICD_LOG_INFO() << " -> returning vkCreateSemaphore\n";
        return (PFN_vkVoidFunction)vkCreateSemaphore;
    }
    if (strcmp(pName, "vkDestroySemaphore") == 0) {
        ICD_LOG_INFO() << " -> returning vkDestroySemaphore\n";
        return (PFN_vkVoidFunction)vkDestroySemaphore;
    }
    if (strcmp(pName, "vkGetSemaphoreCounterValue") == 0) {
        ICD_LOG_INFO() << " -> returning vkGetSemaphoreCounterValue\n";
        return (PFN_vkVoidFunction)vkGetSemaphoreCounterValue;
    }
    if (strcmp(pName, "vkSignalSemaphore") == 0) {
        ICD_LOG_INFO() << " -> returning vkSignalSemaphore\n";
        return (PFN_vkVoidFunction)vkSignalSemaphore;
    }
    if (strcmp(pName, "vkWaitSemaphores") == 0) {
        ICD_LOG_INFO() << " -> returning vkWaitSemaphores\n";
        return (PFN_vkVoidFunction)vkWaitSemaphores;
    }
    if (strcmp(pName, "vkQueueSubmit") == 0) {
        ICD_LOG_INFO() << " -> returning vkQueueSubmit\n";
        return (PFN_vkVoidFunction)vkQueueSubmit;
    }
    if (strcmp(pName, "vkQueueWaitIdle") == 0) {
        ICD_LOG_INFO() << " -> returning vkQueueWaitIdle\n";
        return (PFN_vkVoidFunction)vkQueueWaitIdle;
    }
    if (strcmp(pName, "vkDeviceWaitIdle") == 0) {
        ICD_LOG_INFO() << " -> returning vkDeviceWaitIdle\n";
        return (PFN_vkVoidFunction)vkDeviceWaitIdle;
    }

    ICD_LOG_INFO() << " -> NOT FOUND, returning nullptr\n";
    return nullptr;
}

// Standard vkGetInstanceProcAddr (required by spec)
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    return vk_icdGetInstanceProcAddr(instance, pName);
}

// ICD GetPhysicalDeviceProcAddr (required for ICD interface version 3+)
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* pName) {
    ICD_LOG_INFO() << "[Client ICD] vk_icdGetPhysicalDeviceProcAddr called for: " << (pName ? pName : "NULL");

    if (pName == nullptr) {
        ICD_LOG_INFO() << " -> returning nullptr\n";
        return nullptr;
    }

    PFN_vkVoidFunction func = vk_icdGetInstanceProcAddr(instance, pName);
    if (!func) {
        ICD_LOG_INFO() << " -> Not found (nullptr)\n";
    }
    return func;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* pApiVersion) {
    ICD_LOG_INFO() << "[Client ICD] vkEnumerateInstanceVersion called\n";

    // Return our supported Vulkan API version (1.3)
    // This is a static value, no server communication needed
    *pApiVersion = VK_API_VERSION_1_3;

    ICD_LOG_INFO() << "[Client ICD] Returning version: 1.3.0\n";
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkEnumerateInstanceExtensionProperties called\n";

    // We don't support layers
    if (pLayerName != nullptr) {
        ICD_LOG_INFO() << "[Client ICD] Layer requested: " << pLayerName << " -> VK_ERROR_LAYER_NOT_PRESENT\n";
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (!pPropertyCount) {
        ICD_LOG_ERROR() << "[Client ICD] pPropertyCount is NULL\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    uint32_t remote_count = 0;
    VkResult count_result = vn_call_vkEnumerateInstanceExtensionProperties(
        &g_ring, pLayerName, &remote_count, nullptr);
    if (count_result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to query instance extension count: " << count_result << "\n";
        return count_result;
    }

    std::vector<VkExtensionProperties> remote_props;
    if (remote_count > 0) {
        remote_props.resize(remote_count);
        uint32_t write_count = remote_count;
        VkResult list_result = vn_call_vkEnumerateInstanceExtensionProperties(
            &g_ring, pLayerName, &write_count, remote_props.data());
        if (list_result != VK_SUCCESS && list_result != VK_INCOMPLETE) {
            ICD_LOG_ERROR() << "[Client ICD] Failed to fetch instance extensions: " << list_result << "\n";
            return list_result;
        }
        remote_props.resize(write_count);
        if (list_result == VK_INCOMPLETE) {
            ICD_LOG_WARN() << "[Client ICD] Server reported VK_INCOMPLETE while fetching instance extensions\n";
        }
    }

    std::vector<VkExtensionProperties> filtered;
    filtered.reserve(remote_props.size());
    for (const auto& prop : remote_props) {
        if (should_filter_instance_extension(prop)) {
            ICD_LOG_WARN() << "[Client ICD] Filtering unsupported instance extension: " << prop.extensionName << "\n";
        } else {
            filtered.push_back(prop);
        }
    }

    const uint32_t filtered_count = static_cast<uint32_t>(filtered.size());
    if (!pProperties) {
        *pPropertyCount = filtered_count;
        ICD_LOG_INFO() << "[Client ICD] Returning instance extension count: " << filtered_count << "\n";
        return VK_SUCCESS;
    }

    const uint32_t requested = *pPropertyCount;
    const uint32_t copy_count = std::min(filtered_count, requested);
    for (uint32_t i = 0; i < copy_count; ++i) {
        pProperties[i] = filtered[i];
    }
    *pPropertyCount = filtered_count;

    if (copy_count < filtered_count) {
        ICD_LOG_INFO() << "[Client ICD] Provided " << copy_count << " instance extensions (need " << filtered_count
                  << "), returning VK_INCOMPLETE\n";
        return VK_INCOMPLETE;
    }

    ICD_LOG_INFO() << "[Client ICD] Returning " << copy_count << " instance extensions\n";
    return VK_SUCCESS;
}

// vkCreateInstance - Phase 2
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateInstance called\n";

    if (!pCreateInfo || !pInstance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to connect to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // 1. Allocate ICD instance structure (required for version 5 dispatch table)
    IcdInstance* icd_instance = new IcdInstance();
    if (!icd_instance) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    // Initialize loader dispatch - will be filled by loader after we return
    icd_instance->loader_data = nullptr;

    icd_instance->remote_handle = VK_NULL_HANDLE;

    VkResult wire_result = vn_call_vkCreateInstance(&g_ring, pCreateInfo, pAllocator, &icd_instance->remote_handle);
    if (wire_result != VK_SUCCESS) {
        delete icd_instance;
        return wire_result;
    }

    // Return the ICD instance as the VkInstance handle. The loader will populate
    // icd_instance->loader_data after we return.
    *pInstance = icd_instance_to_handle(icd_instance);

    // Track the mapping between the loader-visible handle and the remote handle.
    g_instance_state.add_instance(*pInstance, icd_instance->remote_handle);

    ICD_LOG_INFO() << "[Client ICD] Instance created successfully\n";
    ICD_LOG_INFO() << "[Client ICD] Loader handle: " << *pInstance
              << ", remote handle: " << icd_instance->remote_handle << "\n";
    return VK_SUCCESS;
}

// vkDestroyInstance - Phase 2
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyInstance called for instance: " << instance << "\n";

    if (instance == VK_NULL_HANDLE) {
        return;
    }

    // Get ICD instance structure
    IcdInstance* icd_instance = icd_instance_from_handle(instance);
    VkInstance loader_handle = icd_instance_to_handle(icd_instance);

    if (g_connected) {
        vn_async_vkDestroyInstance(&g_ring, icd_instance->remote_handle, pAllocator);
    }

    if (g_instance_state.has_instance(loader_handle)) {
        g_instance_state.remove_instance(loader_handle);
    } else {
        ICD_LOG_ERROR() << "[Client ICD] Warning: Instance not tracked during destroy\n";
    }

    // Free the ICD instance structure
    delete icd_instance;

    ICD_LOG_INFO() << "[Client ICD] Instance destroyed\n";
}

// vkEnumeratePhysicalDevices - Phase 2
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices) {

    ICD_LOG_INFO() << "[Client ICD] vkEnumeratePhysicalDevices called\n";

    if (!pPhysicalDeviceCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdInstance* icd_instance = icd_instance_from_handle(instance);
    InstanceState* state = g_instance_state.get_instance(instance);
    if (!icd_instance || !state) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid instance state\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkInstance remote_instance = icd_instance->remote_handle;
    uint32_t requested_count = (pPhysicalDevices && *pPhysicalDeviceCount) ? *pPhysicalDeviceCount : 0;
    std::vector<VkPhysicalDevice> remote_devices;
    if (pPhysicalDevices && requested_count > 0) {
        remote_devices.resize(requested_count);
    }

    VkResult wire_result = vn_call_vkEnumeratePhysicalDevices(
        &g_ring,
        remote_instance,
        pPhysicalDeviceCount,
        pPhysicalDevices && requested_count > 0 ? remote_devices.data() : nullptr);

    if (wire_result != VK_SUCCESS) {
        return wire_result;
    }

    ICD_LOG_INFO() << "[Client ICD] Server reported " << *pPhysicalDeviceCount << " device(s)\n";

    if (!pPhysicalDevices) {
        return VK_SUCCESS;
    }

    const uint32_t returned = std::min<uint32_t>(remote_devices.size(), *pPhysicalDeviceCount);
    remote_devices.resize(returned);

    std::vector<PhysicalDeviceEntry> new_entries;
    new_entries.reserve(remote_devices.size());
    std::vector<VkPhysicalDevice> local_devices;
    local_devices.reserve(remote_devices.size());

    for (VkPhysicalDevice remote : remote_devices) {
        auto existing = std::find_if(
            state->physical_devices.begin(),
            state->physical_devices.end(),
            [remote](const PhysicalDeviceEntry& entry) {
                return entry.remote_handle == remote;
            });

        VkPhysicalDevice local = VK_NULL_HANDLE;
        if (existing != state->physical_devices.end()) {
            local = existing->local_handle;
        } else {
            local = g_handle_allocator.allocate<VkPhysicalDevice>();
        }

        new_entries.emplace_back(local, remote);
        local_devices.push_back(local);
    }

    state->physical_devices = std::move(new_entries);

    for (uint32_t i = 0; i < local_devices.size(); ++i) {
        pPhysicalDevices[i] = local_devices[i];
        ICD_LOG_INFO() << "[Client ICD] Physical device " << i << " local=" << local_devices[i]
                  << " remote=" << remote_devices[i] << "\n";
    }

    return VK_SUCCESS;
}

// vkGetPhysicalDeviceFeatures - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures* pFeatures) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceFeatures called\n";

    if (!pFeatures) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pFeatures, 0, sizeof(VkPhysicalDeviceFeatures));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceFeatures(&g_ring, remote_device, pFeatures);
    ICD_LOG_INFO() << "[Client ICD] Returned features from server\n";
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2* pFeatures) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceFeatures2 called\n";

    if (!pFeatures) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pFeatures, 0, sizeof(VkPhysicalDeviceFeatures2));
        return;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceFeatures2");
    if (remote_device == VK_NULL_HANDLE) {
        memset(pFeatures, 0, sizeof(VkPhysicalDeviceFeatures2));
        return;
    }

    vn_call_vkGetPhysicalDeviceFeatures2(&g_ring, remote_device, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2* pFeatures) {
    vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
}

// vkGetPhysicalDeviceFormatProperties - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties* pFormatProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceFormatProperties called\n";

    if (!pFormatProperties) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pFormatProperties, 0, sizeof(VkFormatProperties));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceFormatProperties(&g_ring, remote_device, format, pFormatProperties);
}

// vkGetPhysicalDeviceImageFormatProperties - Phase 2 stub
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkImageFormatProperties* pImageFormatProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceImageFormatProperties called\n";

    if (!pImageFormatProperties) {
        ICD_LOG_ERROR() << "[Client ICD] pImageFormatProperties is NULL\n";
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceImageFormatProperties");
    if (remote_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkGetPhysicalDeviceImageFormatProperties(
        &g_ring, remote_device, format, type, tiling, usage, flags, pImageFormatProperties);
    if (result != VK_SUCCESS) {
        ICD_LOG_WARN() << "[Client ICD] vkGetPhysicalDeviceImageFormatProperties returned " << result << "\n";
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceImageFormatProperties2 called\n";

    if (!pImageFormatInfo || !pImageFormatProperties) {
        ICD_LOG_ERROR() << "[Client ICD] pImageFormatInfo/pImageFormatProperties is NULL\n";
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceImageFormatProperties2");
    if (remote_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkGetPhysicalDeviceImageFormatProperties2(
        &g_ring, remote_device, pImageFormatInfo, pImageFormatProperties);
    if (result != VK_SUCCESS) {
        ICD_LOG_WARN() << "[Client ICD] vkGetPhysicalDeviceImageFormatProperties2 returned " << result << "\n";
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {
    return vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, pImageFormatInfo, pImageFormatProperties);
}

// vkGetPhysicalDeviceProperties - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceProperties called\n";

    if (!pProperties) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pProperties, 0, sizeof(VkPhysicalDeviceProperties));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceProperties(&g_ring, remote_device, pProperties);
    ICD_LOG_INFO() << "[Client ICD] Returned device properties from server: " << pProperties->deviceName << "\n";
    vp_branding_apply_properties(pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceProperties2 called\n";

    if (!pProperties) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pProperties, 0, sizeof(VkPhysicalDeviceProperties2));
        return;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceProperties2");
    if (remote_device == VK_NULL_HANDLE) {
        memset(pProperties, 0, sizeof(VkPhysicalDeviceProperties2));
        return;
    }

    vn_call_vkGetPhysicalDeviceProperties2(&g_ring, remote_device, pProperties);
    vp_branding_apply_properties2(pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2* pProperties) {
    vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
}

// vkGetPhysicalDeviceQueueFamilyProperties - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties* pQueueFamilyProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceQueueFamilyProperties called\n";

    if (!pQueueFamilyPropertyCount) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        *pQueueFamilyPropertyCount = 0;
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceQueueFamilyProperties(&g_ring, remote_device, pQueueFamilyPropertyCount, pQueueFamilyProperties);

    if (pQueueFamilyProperties) {
        ICD_LOG_INFO() << "[Client ICD] Returned " << *pQueueFamilyPropertyCount << " queue families from server\n";
    } else {
        ICD_LOG_INFO() << "[Client ICD] Returning queue family count: " << *pQueueFamilyPropertyCount << "\n";
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2* pQueueFamilyProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceQueueFamilyProperties2 called\n";

    if (!pQueueFamilyPropertyCount) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        *pQueueFamilyPropertyCount = 0;
        return;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceQueueFamilyProperties2");
    if (remote_device == VK_NULL_HANDLE) {
        *pQueueFamilyPropertyCount = 0;
        return;
    }

    vn_call_vkGetPhysicalDeviceQueueFamilyProperties2(
        &g_ring, remote_device, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2KHR(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2* pQueueFamilyProperties) {
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

// vkGetPhysicalDeviceMemoryProperties - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties* pMemoryProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceMemoryProperties called\n";

    if (!pMemoryProperties) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pMemoryProperties, 0, sizeof(VkPhysicalDeviceMemoryProperties));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceMemoryProperties(&g_ring, remote_device, pMemoryProperties);
    ICD_LOG_INFO() << "[Client ICD] Returned memory properties from server: "
              << pMemoryProperties->memoryTypeCount << " types, "
              << pMemoryProperties->memoryHeapCount << " heaps\n";
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2* pMemoryProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceMemoryProperties2 called\n";

    if (!pMemoryProperties) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pMemoryProperties, 0, sizeof(VkPhysicalDeviceMemoryProperties2));
        return;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceMemoryProperties2");
    if (remote_device == VK_NULL_HANDLE) {
        memset(pMemoryProperties, 0, sizeof(VkPhysicalDeviceMemoryProperties2));
        return;
    }

    vn_call_vkGetPhysicalDeviceMemoryProperties2(&g_ring, remote_device, pMemoryProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2* pMemoryProperties) {
    vkGetPhysicalDeviceMemoryProperties2(physicalDevice, pMemoryProperties);
}

// vkGetDeviceProcAddr - Phase 3
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    ICD_LOG_INFO() << "[Client ICD] vkGetDeviceProcAddr called for: " << (pName ? pName : "NULL");

    if (!pName) {
        ICD_LOG_INFO() << " -> nullptr\n";
        return nullptr;
    }

    // Critical: vkGetDeviceProcAddr must return itself
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceProcAddr\n";
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    }

    // Phase 3: Device-level functions
    if (strcmp(pName, "vkGetDeviceQueue") == 0) {
        ICD_LOG_INFO() << " -> vkGetDeviceQueue\n";
        return (PFN_vkVoidFunction)vkGetDeviceQueue;
    }
    if (strcmp(pName, "vkDestroyDevice") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyDevice\n";
        return (PFN_vkVoidFunction)vkDestroyDevice;
    }
    if (strcmp(pName, "vkAllocateMemory") == 0) {
        ICD_LOG_INFO() << " -> vkAllocateMemory\n";
        return (PFN_vkVoidFunction)vkAllocateMemory;
    }
    if (strcmp(pName, "vkFreeMemory") == 0) {
        ICD_LOG_INFO() << " -> vkFreeMemory\n";
        return (PFN_vkVoidFunction)vkFreeMemory;
    }
    if (strcmp(pName, "vkMapMemory") == 0) {
        ICD_LOG_INFO() << " -> vkMapMemory\n";
        return (PFN_vkVoidFunction)vkMapMemory;
    }
    if (strcmp(pName, "vkUnmapMemory") == 0) {
        ICD_LOG_INFO() << " -> vkUnmapMemory\n";
        return (PFN_vkVoidFunction)vkUnmapMemory;
    }
    if (strcmp(pName, "vkFlushMappedMemoryRanges") == 0) {
        ICD_LOG_INFO() << " -> vkFlushMappedMemoryRanges\n";
        return (PFN_vkVoidFunction)vkFlushMappedMemoryRanges;
    }
    if (strcmp(pName, "vkInvalidateMappedMemoryRanges") == 0) {
        ICD_LOG_INFO() << " -> vkInvalidateMappedMemoryRanges\n";
        return (PFN_vkVoidFunction)vkInvalidateMappedMemoryRanges;
    }
    if (strcmp(pName, "vkCreateBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCreateBuffer\n";
        return (PFN_vkVoidFunction)vkCreateBuffer;
    }
    if (strcmp(pName, "vkDestroyBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyBuffer\n";
        return (PFN_vkVoidFunction)vkDestroyBuffer;
    }
    if (strcmp(pName, "vkGetBufferMemoryRequirements") == 0) {
        ICD_LOG_INFO() << " -> vkGetBufferMemoryRequirements\n";
        return (PFN_vkVoidFunction)vkGetBufferMemoryRequirements;
    }
    if (strcmp(pName, "vkBindBufferMemory") == 0) {
        ICD_LOG_INFO() << " -> vkBindBufferMemory\n";
        return (PFN_vkVoidFunction)vkBindBufferMemory;
    }
    if (strcmp(pName, "vkCreateImage") == 0) {
        ICD_LOG_INFO() << " -> vkCreateImage\n";
        return (PFN_vkVoidFunction)vkCreateImage;
    }
    if (strcmp(pName, "vkDestroyImage") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyImage\n";
        return (PFN_vkVoidFunction)vkDestroyImage;
    }
    if (strcmp(pName, "vkCreateImageView") == 0) {
        ICD_LOG_INFO() << " -> vkCreateImageView\n";
        return (PFN_vkVoidFunction)vkCreateImageView;
    }
    if (strcmp(pName, "vkDestroyImageView") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyImageView\n";
        return (PFN_vkVoidFunction)vkDestroyImageView;
    }
    if (strcmp(pName, "vkCreateBufferView") == 0) {
        ICD_LOG_INFO() << " -> vkCreateBufferView\n";
        return (PFN_vkVoidFunction)vkCreateBufferView;
    }
    if (strcmp(pName, "vkDestroyBufferView") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyBufferView\n";
        return (PFN_vkVoidFunction)vkDestroyBufferView;
    }
    if (strcmp(pName, "vkCreateSampler") == 0) {
        ICD_LOG_INFO() << " -> vkCreateSampler\n";
        return (PFN_vkVoidFunction)vkCreateSampler;
    }
    if (strcmp(pName, "vkDestroySampler") == 0) {
        ICD_LOG_INFO() << " -> vkDestroySampler\n";
        return (PFN_vkVoidFunction)vkDestroySampler;
    }
    if (strcmp(pName, "vkGetImageMemoryRequirements") == 0) {
        ICD_LOG_INFO() << " -> vkGetImageMemoryRequirements\n";
        return (PFN_vkVoidFunction)vkGetImageMemoryRequirements;
    }
    if (strcmp(pName, "vkBindImageMemory") == 0) {
        ICD_LOG_INFO() << " -> vkBindImageMemory\n";
        return (PFN_vkVoidFunction)vkBindImageMemory;
    }
    if (strcmp(pName, "vkCreateShaderModule") == 0) {
        ICD_LOG_INFO() << " -> vkCreateShaderModule\n";
        return (PFN_vkVoidFunction)vkCreateShaderModule;
    }
    if (strcmp(pName, "vkDestroyShaderModule") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyShaderModule\n";
        return (PFN_vkVoidFunction)vkDestroyShaderModule;
    }
    if (strcmp(pName, "vkCreateDescriptorSetLayout") == 0) {
        ICD_LOG_INFO() << " -> vkCreateDescriptorSetLayout\n";
        return (PFN_vkVoidFunction)vkCreateDescriptorSetLayout;
    }
    if (strcmp(pName, "vkDestroyDescriptorSetLayout") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyDescriptorSetLayout\n";
        return (PFN_vkVoidFunction)vkDestroyDescriptorSetLayout;
    }
    if (strcmp(pName, "vkCreateDescriptorPool") == 0) {
        ICD_LOG_INFO() << " -> vkCreateDescriptorPool\n";
        return (PFN_vkVoidFunction)vkCreateDescriptorPool;
    }
    if (strcmp(pName, "vkDestroyDescriptorPool") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyDescriptorPool\n";
        return (PFN_vkVoidFunction)vkDestroyDescriptorPool;
    }
    if (strcmp(pName, "vkResetDescriptorPool") == 0) {
        ICD_LOG_INFO() << " -> vkResetDescriptorPool\n";
        return (PFN_vkVoidFunction)vkResetDescriptorPool;
    }
    if (strcmp(pName, "vkAllocateDescriptorSets") == 0) {
        ICD_LOG_INFO() << " -> vkAllocateDescriptorSets\n";
        return (PFN_vkVoidFunction)vkAllocateDescriptorSets;
    }
    if (strcmp(pName, "vkFreeDescriptorSets") == 0) {
        ICD_LOG_INFO() << " -> vkFreeDescriptorSets\n";
        return (PFN_vkVoidFunction)vkFreeDescriptorSets;
    }
    if (strcmp(pName, "vkUpdateDescriptorSets") == 0) {
        ICD_LOG_INFO() << " -> vkUpdateDescriptorSets\n";
        return (PFN_vkVoidFunction)vkUpdateDescriptorSets;
    }
    if (strcmp(pName, "vkCreatePipelineLayout") == 0) {
        ICD_LOG_INFO() << " -> vkCreatePipelineLayout\n";
        return (PFN_vkVoidFunction)vkCreatePipelineLayout;
    }
    if (strcmp(pName, "vkDestroyPipelineLayout") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyPipelineLayout\n";
        return (PFN_vkVoidFunction)vkDestroyPipelineLayout;
    }
    if (strcmp(pName, "vkCreatePipelineCache") == 0) {
        ICD_LOG_INFO() << " -> vkCreatePipelineCache\n";
        return (PFN_vkVoidFunction)vkCreatePipelineCache;
    }
    if (strcmp(pName, "vkDestroyPipelineCache") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyPipelineCache\n";
        return (PFN_vkVoidFunction)vkDestroyPipelineCache;
    }
    if (strcmp(pName, "vkGetPipelineCacheData") == 0) {
        ICD_LOG_INFO() << " -> vkGetPipelineCacheData\n";
        return (PFN_vkVoidFunction)vkGetPipelineCacheData;
    }
    if (strcmp(pName, "vkMergePipelineCaches") == 0) {
        ICD_LOG_INFO() << " -> vkMergePipelineCaches\n";
        return (PFN_vkVoidFunction)vkMergePipelineCaches;
    }
    if (strcmp(pName, "vkCreateQueryPool") == 0) {
        ICD_LOG_INFO() << " -> vkCreateQueryPool\n";
        return (PFN_vkVoidFunction)vkCreateQueryPool;
    }
    if (strcmp(pName, "vkDestroyQueryPool") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyQueryPool\n";
        return (PFN_vkVoidFunction)vkDestroyQueryPool;
    }
    if (strcmp(pName, "vkResetQueryPool") == 0) {
        ICD_LOG_INFO() << " -> vkResetQueryPool\n";
        return (PFN_vkVoidFunction)vkResetQueryPool;
    }
    if (strcmp(pName, "vkGetQueryPoolResults") == 0) {
        ICD_LOG_INFO() << " -> vkGetQueryPoolResults\n";
        return (PFN_vkVoidFunction)vkGetQueryPoolResults;
    }
    if (strcmp(pName, "vkCreateSwapchainKHR") == 0) {
        ICD_LOG_INFO() << " -> vkCreateSwapchainKHR\n";
        return (PFN_vkVoidFunction)vkCreateSwapchainKHR;
    }
    if (strcmp(pName, "vkDestroySwapchainKHR") == 0) {
        ICD_LOG_INFO() << " -> vkDestroySwapchainKHR\n";
        return (PFN_vkVoidFunction)vkDestroySwapchainKHR;
    }
    if (strcmp(pName, "vkGetSwapchainImagesKHR") == 0) {
        ICD_LOG_INFO() << " -> vkGetSwapchainImagesKHR\n";
        return (PFN_vkVoidFunction)vkGetSwapchainImagesKHR;
    }
    if (strcmp(pName, "vkAcquireNextImageKHR") == 0) {
        ICD_LOG_INFO() << " -> vkAcquireNextImageKHR\n";
        return (PFN_vkVoidFunction)vkAcquireNextImageKHR;
    }
    if (strcmp(pName, "vkAcquireNextImage2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkAcquireNextImage2KHR\n";
        return (PFN_vkVoidFunction)vkAcquireNextImage2KHR;
    }
    if (strcmp(pName, "vkQueuePresentKHR") == 0) {
        ICD_LOG_INFO() << " -> vkQueuePresentKHR\n";
        return (PFN_vkVoidFunction)vkQueuePresentKHR;
    }
    if (strcmp(pName, "vkCreateRenderPass") == 0) {
        ICD_LOG_INFO() << " -> vkCreateRenderPass\n";
        return (PFN_vkVoidFunction)vkCreateRenderPass;
    }
    if (strcmp(pName, "vkCreateRenderPass2") == 0 || strcmp(pName, "vkCreateRenderPass2KHR") == 0) {
        ICD_LOG_INFO() << " -> vkCreateRenderPass2\n";
        return (PFN_vkVoidFunction)vkCreateRenderPass2;
    }
    if (strcmp(pName, "vkDestroyRenderPass") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyRenderPass\n";
        return (PFN_vkVoidFunction)vkDestroyRenderPass;
    }
    if (strcmp(pName, "vkCreateFramebuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCreateFramebuffer\n";
        return (PFN_vkVoidFunction)vkCreateFramebuffer;
    }
    if (strcmp(pName, "vkDestroyFramebuffer") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyFramebuffer\n";
        return (PFN_vkVoidFunction)vkDestroyFramebuffer;
    }
    if (strcmp(pName, "vkCreateComputePipelines") == 0) {
        ICD_LOG_INFO() << " -> vkCreateComputePipelines\n";
        return (PFN_vkVoidFunction)vkCreateComputePipelines;
    }
    if (strcmp(pName, "vkCreateGraphicsPipelines") == 0) {
        ICD_LOG_INFO() << " -> vkCreateGraphicsPipelines\n";
        return (PFN_vkVoidFunction)vkCreateGraphicsPipelines;
    }
    if (strcmp(pName, "vkDestroyPipeline") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyPipeline\n";
        return (PFN_vkVoidFunction)vkDestroyPipeline;
    }
    if (strcmp(pName, "vkGetImageSubresourceLayout") == 0) {
        ICD_LOG_INFO() << " -> vkGetImageSubresourceLayout\n";
        return (PFN_vkVoidFunction)vkGetImageSubresourceLayout;
    }
    if (strcmp(pName, "vkCreateCommandPool") == 0) {
        ICD_LOG_INFO() << " -> vkCreateCommandPool\n";
        return (PFN_vkVoidFunction)vkCreateCommandPool;
    }
    if (strcmp(pName, "vkDestroyCommandPool") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyCommandPool\n";
        return (PFN_vkVoidFunction)vkDestroyCommandPool;
    }
    if (strcmp(pName, "vkResetCommandPool") == 0) {
        ICD_LOG_INFO() << " -> vkResetCommandPool\n";
        return (PFN_vkVoidFunction)vkResetCommandPool;
    }
    if (strcmp(pName, "vkAllocateCommandBuffers") == 0) {
        ICD_LOG_INFO() << " -> vkAllocateCommandBuffers\n";
        return (PFN_vkVoidFunction)vkAllocateCommandBuffers;
    }
    if (strcmp(pName, "vkFreeCommandBuffers") == 0) {
        ICD_LOG_INFO() << " -> vkFreeCommandBuffers\n";
        return (PFN_vkVoidFunction)vkFreeCommandBuffers;
    }
    if (strcmp(pName, "vkBeginCommandBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkBeginCommandBuffer\n";
        return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    }
    if (strcmp(pName, "vkEndCommandBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkEndCommandBuffer\n";
        return (PFN_vkVoidFunction)vkEndCommandBuffer;
    }
    if (strcmp(pName, "vkResetCommandBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkResetCommandBuffer\n";
        return (PFN_vkVoidFunction)vkResetCommandBuffer;
    }
    if (strcmp(pName, "vkCmdCopyBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyBuffer\n";
        return (PFN_vkVoidFunction)vkCmdCopyBuffer;
    }
    if (strcmp(pName, "vkCmdCopyImage") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyImage\n";
        return (PFN_vkVoidFunction)vkCmdCopyImage;
    }
    if (strcmp(pName, "vkCmdBlitImage") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBlitImage\n";
        return (PFN_vkVoidFunction)vkCmdBlitImage;
    }
    if (strcmp(pName, "vkCmdCopyBufferToImage") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyBufferToImage\n";
        return (PFN_vkVoidFunction)vkCmdCopyBufferToImage;
    }
    if (strcmp(pName, "vkCmdCopyImageToBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyImageToBuffer\n";
        return (PFN_vkVoidFunction)vkCmdCopyImageToBuffer;
    }
    if (strcmp(pName, "vkCmdFillBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCmdFillBuffer\n";
        return (PFN_vkVoidFunction)vkCmdFillBuffer;
    }
    if (strcmp(pName, "vkCmdUpdateBuffer") == 0) {
        ICD_LOG_INFO() << " -> vkCmdUpdateBuffer\n";
        return (PFN_vkVoidFunction)vkCmdUpdateBuffer;
    }
    if (strcmp(pName, "vkCmdClearColorImage") == 0) {
        ICD_LOG_INFO() << " -> vkCmdClearColorImage\n";
        return (PFN_vkVoidFunction)vkCmdClearColorImage;
    }
    if (strcmp(pName, "vkCmdBeginRenderPass") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBeginRenderPass\n";
        return (PFN_vkVoidFunction)vkCmdBeginRenderPass;
    }
    if (strcmp(pName, "vkCmdEndRenderPass") == 0) {
        ICD_LOG_INFO() << " -> vkCmdEndRenderPass\n";
        return (PFN_vkVoidFunction)vkCmdEndRenderPass;
    }
    if (strcmp(pName, "vkCmdBindPipeline") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBindPipeline\n";
        return (PFN_vkVoidFunction)vkCmdBindPipeline;
    }
    if (strcmp(pName, "vkCmdBindVertexBuffers") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBindVertexBuffers\n";
        return (PFN_vkVoidFunction)vkCmdBindVertexBuffers;
    }
    if (strcmp(pName, "vkCmdSetViewport") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetViewport\n";
        return (PFN_vkVoidFunction)vkCmdSetViewport;
    }
    if (strcmp(pName, "vkCmdSetScissor") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetScissor\n";
        return (PFN_vkVoidFunction)vkCmdSetScissor;
    }
    if (strcmp(pName, "vkCmdDraw") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDraw\n";
        return (PFN_vkVoidFunction)vkCmdDraw;
    }
    if (strcmp(pName, "vkCmdBindDescriptorSets") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBindDescriptorSets\n";
        return (PFN_vkVoidFunction)vkCmdBindDescriptorSets;
    }
    if (strcmp(pName, "vkCmdDispatch") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDispatch\n";
        return (PFN_vkVoidFunction)vkCmdDispatch;
    }
    if (strcmp(pName, "vkCmdDispatchIndirect") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDispatchIndirect\n";
        return (PFN_vkVoidFunction)vkCmdDispatchIndirect;
    }
    if (strcmp(pName, "vkCmdDispatchBase") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDispatchBase\n";
        return (PFN_vkVoidFunction)vkCmdDispatchBase;
    }
    if (strcmp(pName, "vkCmdDispatchBaseKHR") == 0) {
        ICD_LOG_INFO() << " -> vkCmdDispatchBaseKHR\n";
        return (PFN_vkVoidFunction)vkCmdDispatchBaseKHR;
    }
    if (strcmp(pName, "vkCmdPushConstants") == 0) {
        ICD_LOG_INFO() << " -> vkCmdPushConstants\n";
        return (PFN_vkVoidFunction)vkCmdPushConstants;
    }
    if (strcmp(pName, "vkCmdPipelineBarrier") == 0) {
        ICD_LOG_INFO() << " -> vkCmdPipelineBarrier\n";
        return (PFN_vkVoidFunction)vkCmdPipelineBarrier;
    }
    if (strcmp(pName, "vkCmdResetQueryPool") == 0) {
        ICD_LOG_INFO() << " -> vkCmdResetQueryPool\n";
        return (PFN_vkVoidFunction)vkCmdResetQueryPool;
    }
    if (strcmp(pName, "vkCmdBeginQuery") == 0) {
        ICD_LOG_INFO() << " -> vkCmdBeginQuery\n";
        return (PFN_vkVoidFunction)vkCmdBeginQuery;
    }
    if (strcmp(pName, "vkCmdEndQuery") == 0) {
        ICD_LOG_INFO() << " -> vkCmdEndQuery\n";
        return (PFN_vkVoidFunction)vkCmdEndQuery;
    }
    if (strcmp(pName, "vkCmdWriteTimestamp") == 0) {
        ICD_LOG_INFO() << " -> vkCmdWriteTimestamp\n";
        return (PFN_vkVoidFunction)vkCmdWriteTimestamp;
    }
    if (strcmp(pName, "vkCmdCopyQueryPoolResults") == 0) {
        ICD_LOG_INFO() << " -> vkCmdCopyQueryPoolResults\n";
        return (PFN_vkVoidFunction)vkCmdCopyQueryPoolResults;
    }
    if (strcmp(pName, "vkCmdSetEvent") == 0) {
        ICD_LOG_INFO() << " -> vkCmdSetEvent\n";
        return (PFN_vkVoidFunction)vkCmdSetEvent;
    }
    if (strcmp(pName, "vkCmdResetEvent") == 0) {
        ICD_LOG_INFO() << " -> vkCmdResetEvent\n";
        return (PFN_vkVoidFunction)vkCmdResetEvent;
    }
    if (strcmp(pName, "vkCmdWaitEvents") == 0) {
        ICD_LOG_INFO() << " -> vkCmdWaitEvents\n";
        return (PFN_vkVoidFunction)vkCmdWaitEvents;
    }
    if (strcmp(pName, "vkCreateEvent") == 0) {
        ICD_LOG_INFO() << " -> vkCreateEvent\n";
        return (PFN_vkVoidFunction)vkCreateEvent;
    }
    if (strcmp(pName, "vkDestroyEvent") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyEvent\n";
        return (PFN_vkVoidFunction)vkDestroyEvent;
    }
    if (strcmp(pName, "vkGetEventStatus") == 0) {
        ICD_LOG_INFO() << " -> vkGetEventStatus\n";
        return (PFN_vkVoidFunction)vkGetEventStatus;
    }
    if (strcmp(pName, "vkSetEvent") == 0) {
        ICD_LOG_INFO() << " -> vkSetEvent\n";
        return (PFN_vkVoidFunction)vkSetEvent;
    }
    if (strcmp(pName, "vkResetEvent") == 0) {
        ICD_LOG_INFO() << " -> vkResetEvent\n";
        return (PFN_vkVoidFunction)vkResetEvent;
    }
    if (strcmp(pName, "vkCreateFence") == 0) {
        ICD_LOG_INFO() << " -> vkCreateFence\n";
        return (PFN_vkVoidFunction)vkCreateFence;
    }
    if (strcmp(pName, "vkDestroyFence") == 0) {
        ICD_LOG_INFO() << " -> vkDestroyFence\n";
        return (PFN_vkVoidFunction)vkDestroyFence;
    }
    if (strcmp(pName, "vkGetFenceStatus") == 0) {
        ICD_LOG_INFO() << " -> vkGetFenceStatus\n";
        return (PFN_vkVoidFunction)vkGetFenceStatus;
    }
    if (strcmp(pName, "vkResetFences") == 0) {
        ICD_LOG_INFO() << " -> vkResetFences\n";
        return (PFN_vkVoidFunction)vkResetFences;
    }
    if (strcmp(pName, "vkWaitForFences") == 0) {
        ICD_LOG_INFO() << " -> vkWaitForFences\n";
        return (PFN_vkVoidFunction)vkWaitForFences;
    }
    if (strcmp(pName, "vkCreateSemaphore") == 0) {
        ICD_LOG_INFO() << " -> vkCreateSemaphore\n";
        return (PFN_vkVoidFunction)vkCreateSemaphore;
    }
    if (strcmp(pName, "vkDestroySemaphore") == 0) {
        ICD_LOG_INFO() << " -> vkDestroySemaphore\n";
        return (PFN_vkVoidFunction)vkDestroySemaphore;
    }
    if (strcmp(pName, "vkGetSemaphoreCounterValue") == 0) {
        ICD_LOG_INFO() << " -> vkGetSemaphoreCounterValue\n";
        return (PFN_vkVoidFunction)vkGetSemaphoreCounterValue;
    }
    if (strcmp(pName, "vkSignalSemaphore") == 0) {
        ICD_LOG_INFO() << " -> vkSignalSemaphore\n";
        return (PFN_vkVoidFunction)vkSignalSemaphore;
    }
    if (strcmp(pName, "vkWaitSemaphores") == 0) {
        ICD_LOG_INFO() << " -> vkWaitSemaphores\n";
        return (PFN_vkVoidFunction)vkWaitSemaphores;
    }
    if (strcmp(pName, "vkQueueSubmit") == 0) {
        ICD_LOG_INFO() << " -> vkQueueSubmit\n";
        return (PFN_vkVoidFunction)vkQueueSubmit;
    }
    if (strcmp(pName, "vkQueueWaitIdle") == 0) {
        ICD_LOG_INFO() << " -> vkQueueWaitIdle\n";
        return (PFN_vkVoidFunction)vkQueueWaitIdle;
    }
    if (strcmp(pName, "vkDeviceWaitIdle") == 0) {
        ICD_LOG_INFO() << " -> vkDeviceWaitIdle\n";
        return (PFN_vkVoidFunction)vkDeviceWaitIdle;
    }

    ICD_LOG_INFO() << " -> NOT IMPLEMENTED, returning nullptr\n";
    return nullptr;
}

// vkEnumerateDeviceExtensionProperties - Phase 9.1
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkEnumerateDeviceExtensionProperties called\n";

    if (!pPropertyCount) {
        ICD_LOG_ERROR() << "[Client ICD] pPropertyCount is NULL\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Device layers are not supported per spec
    if (pLayerName != nullptr) {
        ICD_LOG_ERROR() << "[Client ICD] Layer requested: " << pLayerName << " -> VK_ERROR_LAYER_NOT_PRESENT\n";
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkEnumerateDeviceExtensionProperties");
    if (remote_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    uint32_t remote_count = 0;
    VkResult count_result =
        vn_call_vkEnumerateDeviceExtensionProperties(&g_ring, remote_device, pLayerName, &remote_count, nullptr);
    if (count_result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to query device extension count: " << count_result << "\n";
        return count_result;
    }

    std::vector<VkExtensionProperties> remote_props;
    if (remote_count > 0) {
        remote_props.resize(remote_count);
        uint32_t write_count = remote_count;
        VkResult list_result = vn_call_vkEnumerateDeviceExtensionProperties(
            &g_ring, remote_device, pLayerName, &write_count, remote_props.data());
        if (list_result != VK_SUCCESS && list_result != VK_INCOMPLETE) {
            ICD_LOG_ERROR() << "[Client ICD] Failed to fetch device extensions: " << list_result << "\n";
            return list_result;
        }
        remote_props.resize(write_count);
        if (list_result == VK_INCOMPLETE) {
            ICD_LOG_WARN() << "[Client ICD] Server reported VK_INCOMPLETE while fetching extensions (extensions may have changed)\n";
        }
    }

    std::vector<VkExtensionProperties> filtered;
    filtered.reserve(remote_props.size());
    for (const auto& prop : remote_props) {
        if (!should_filter_device_extension(prop)) {
            filtered.push_back(prop);
        } else {
            ICD_LOG_WARN() << "[Client ICD] Filtering unsupported device extension: " << prop.extensionName << "\n";
        }
    }

    const uint32_t filtered_count = static_cast<uint32_t>(filtered.size());
    if (!pProperties) {
        *pPropertyCount = filtered_count;
        ICD_LOG_INFO() << "[Client ICD] Returning device extension count: " << filtered_count << "\n";
        return VK_SUCCESS;
    }

    const uint32_t requested = *pPropertyCount;
    const uint32_t copy_count = std::min(filtered_count, requested);
    for (uint32_t i = 0; i < copy_count; ++i) {
        pProperties[i] = filtered[i];
    }

    *pPropertyCount = filtered_count;
    if (copy_count < filtered_count) {
        ICD_LOG_INFO() << "[Client ICD] Provided " << copy_count << " extensions (need " << filtered_count << "), returning VK_INCOMPLETE\n";
        return VK_INCOMPLETE;
    }

    ICD_LOG_INFO() << "[Client ICD] Returning " << copy_count << " device extensions\n";
    return VK_SUCCESS;
}

// vkEnumerateDeviceLayerProperties - Phase 9.1
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkEnumerateDeviceLayerProperties called\n";

    if (!pPropertyCount) {
        ICD_LOG_ERROR() << "[Client ICD] pPropertyCount is NULL\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkEnumerateDeviceLayerProperties");
    if (remote_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkEnumerateDeviceLayerProperties(
        &g_ring, remote_device, pPropertyCount, pProperties);

    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pPropertyCount) {
        ICD_LOG_INFO() << "[Client ICD] Returning " << *pPropertyCount << " layer properties"
                       << (result == VK_INCOMPLETE ? " (VK_INCOMPLETE)" : "") << "\n";
    } else if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        ICD_LOG_ERROR() << "[Client ICD] vkEnumerateDeviceLayerProperties failed: " << result << "\n";
    }

    return result;
}

// vkGetPhysicalDeviceSparseImageFormatProperties - Phase 2 stub
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkSampleCountFlagBits samples,
    VkImageUsageFlags usage,
    VkImageTiling tiling,
    uint32_t* pPropertyCount,
    VkSparseImageFormatProperties* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceSparseImageFormatProperties called\n";

    if (!pPropertyCount) {
        return;
    }

    // For Phase 2: Return 0 sparse properties (we don't support sparse)
    *pPropertyCount = 0;
}

// vkCreateDevice - Phase 3
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateDevice called\n";

    if (!pCreateInfo || !pDevice) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_physical_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_physical_device = entry.remote_handle;
                break;
            }
        }
    }

    if (remote_physical_device == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to find remote physical device\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Allocate ICD device structure (required for loader dispatch table)
    IcdDevice* icd_device = new IcdDevice();
    if (!icd_device) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    // Initialize loader_data - will be filled by loader after we return
    icd_device->loader_data = nullptr;
    icd_device->physical_device = physicalDevice;
    icd_device->remote_handle = VK_NULL_HANDLE;

    // Call server to create device
    VkResult result = vn_call_vkCreateDevice(&g_ring, remote_physical_device, pCreateInfo, pAllocator, &icd_device->remote_handle);

    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateDevice failed: " << result << "\n";
        delete icd_device;
        return result;
    }

    // Return the ICD device as VkDevice handle
    *pDevice = icd_device_to_handle(icd_device);

    // Store device mapping
    g_device_state.add_device(*pDevice, icd_device->remote_handle, physicalDevice);

    ICD_LOG_INFO() << "[Client ICD] Device created successfully (local=" << *pDevice
              << ", remote=" << icd_device->remote_handle << ")\n";
    return VK_SUCCESS;
}

// vkDestroyDevice - Phase 3
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(
    VkDevice device,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyDevice called for device: " << device << "\n";

    if (device == VK_NULL_HANDLE) {
        return;
    }

    // Get ICD device structure
    IcdDevice* icd_device = icd_device_from_handle(device);

    // Clean up any command pools/buffers owned by this device
    std::vector<VkCommandBuffer> buffers_to_free;
    g_command_buffer_state.remove_device(device, &buffers_to_free, nullptr);
    for (VkCommandBuffer buffer : buffers_to_free) {
        IcdCommandBuffer* icd_cb = icd_command_buffer_from_handle(buffer);
        delete icd_cb;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        // Still clean up local resources
        g_resource_state.remove_device_resources(device);
        g_pipeline_state.remove_device_resources(device);
        g_query_state.remove_device(device);
        g_sync_state.remove_device(device);
        g_shadow_buffer_manager.remove_device(device);
        std::vector<SwapchainInfo> removed_swapchains;
        g_swapchain_state.remove_device_swapchains(device, &removed_swapchains);
        for (auto& info : removed_swapchains) {
            if (info.wsi) {
                info.wsi->shutdown();
            }
        }
        g_device_state.remove_device(device);
        delete icd_device;
        return;
    }

    // Call server to destroy device
    vn_async_vkDestroyDevice(&g_ring, icd_device->remote_handle, pAllocator);

    // Drop resource tracking for this device
    g_resource_state.remove_device_resources(device);
    g_pipeline_state.remove_device_resources(device);
    g_query_state.remove_device(device);
    g_sync_state.remove_device(device);
    g_shadow_buffer_manager.remove_device(device);
    std::vector<SwapchainInfo> removed_swapchains;
    g_swapchain_state.remove_device_swapchains(device, &removed_swapchains);
    for (auto& info : removed_swapchains) {
        if (info.wsi) {
            info.wsi->shutdown();
        }
    }

    // Remove from state
    g_device_state.remove_device(device);

    // Free the ICD device structure
    delete icd_device;

    ICD_LOG_INFO() << "[Client ICD] Device destroyed\n";
}

// vkGetDeviceQueue - Phase 3
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t queueIndex,
    VkQueue* pQueue) {

    ICD_LOG_INFO() << "[Client ICD] vkGetDeviceQueue called (device=" << device
              << ", family=" << queueFamilyIndex << ", index=" << queueIndex << ")\n";

    if (!pQueue) {
        ICD_LOG_ERROR() << "[Client ICD] pQueue is NULL\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        *pQueue = VK_NULL_HANDLE;
        return;
    }

    // Get ICD device structure
    IcdDevice* icd_device = icd_device_from_handle(device);

    // Allocate ICD queue structure (required for loader dispatch table)
    IcdQueue* icd_queue = new IcdQueue();
    if (!icd_queue) {
        *pQueue = VK_NULL_HANDLE;
        return;
    }

    // Initialize queue structure
    icd_queue->loader_data = nullptr;  // Loader will fill this
    icd_queue->parent_device = device;
    icd_queue->family_index = queueFamilyIndex;
    icd_queue->queue_index = queueIndex;
    icd_queue->remote_handle = VK_NULL_HANDLE;

    // Call server to get queue (synchronous so we can track remote handle)
    vn_call_vkGetDeviceQueue(&g_ring, icd_device->remote_handle, queueFamilyIndex, queueIndex, &icd_queue->remote_handle);

    // Return the ICD queue as VkQueue handle
    *pQueue = icd_queue_to_handle(icd_queue);

    // Store queue mapping
    g_device_state.add_queue(device, *pQueue, icd_queue->remote_handle, queueFamilyIndex, queueIndex);

    ICD_LOG_INFO() << "[Client ICD] Queue retrieved (local=" << *pQueue
              << ", remote=" << icd_queue->remote_handle << ")\n";
}

// vkAllocateMemory - Phase 4
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice device,
    const VkMemoryAllocateInfo* pAllocateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDeviceMemory* pMemory) {

    ICD_LOG_INFO() << "[Client ICD] vkAllocateMemory called\n";

    if (!pAllocateInfo || !pMemory) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkAllocateMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkAllocateMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDevice remote_device = icd_device->remote_handle;

    VkDeviceMemory remote_memory = VK_NULL_HANDLE;
    VkResult result = vn_call_vkAllocateMemory(&g_ring, remote_device, pAllocateInfo, pAllocator, &remote_memory);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkAllocateMemory failed: " << result << "\n";
        return result;
    }

    VkDeviceMemory local_memory = g_handle_allocator.allocate<VkDeviceMemory>();
    g_resource_state.add_memory(device, local_memory, remote_memory, *pAllocateInfo);
    *pMemory = local_memory;

    ICD_LOG_INFO() << "[Client ICD] Memory allocated (local=" << *pMemory
              << ", remote=" << remote_memory
              << ", size=" << pAllocateInfo->allocationSize << ")\n";
    return VK_SUCCESS;
}

// vkFreeMemory - Phase 4
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice device,
    VkDeviceMemory memory,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkFreeMemory called\n";

    if (memory == VK_NULL_HANDLE) {
        return;
    }

    ShadowBufferMapping mapping = {};
    if (g_shadow_buffer_manager.remove_mapping(memory, &mapping)) {
        if (mapping.data) {
            std::free(mapping.data);
        }
        ICD_LOG_ERROR() << "[Client ICD] Warning: Memory freed while still mapped, dropping local shadow buffer\n";
    }

    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkFreeMemory\n";
        g_resource_state.remove_memory(memory);
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkFreeMemory\n";
        g_resource_state.remove_memory(memory);
        return;
    }

    if (remote_memory == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote memory handle missing in vkFreeMemory\n";
        g_resource_state.remove_memory(memory);
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkFreeMemory(&g_ring, icd_device->remote_handle, remote_memory, pAllocator);
    g_resource_state.remove_memory(memory);
    ICD_LOG_INFO() << "[Client ICD] Memory freed (local=" << memory << ", remote=" << remote_memory << ")\n";
}

// vkMapMemory - Phase 8
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(
    VkDevice device,
    VkDeviceMemory memory,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkMemoryMapFlags flags,
    void** ppData) {

    ICD_LOG_INFO() << "[Client ICD] vkMapMemory called\n";

    if (!ppData) {
        ICD_LOG_ERROR() << "[Client ICD] vkMapMemory requires valid ppData\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    *ppData = nullptr;

    if (flags != 0) {
        ICD_LOG_ERROR() << "[Client ICD] vkMapMemory flags must be zero (got " << flags << ")\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkMapMemory\n";
        return VK_ERROR_DEVICE_LOST;
    }

    if (!g_device_state.has_device(device) || !g_resource_state.has_memory(memory)) {
        ICD_LOG_ERROR() << "[Client ICD] vkMapMemory called with unknown device or memory\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (g_shadow_buffer_manager.is_mapped(memory)) {
        ICD_LOG_ERROR() << "[Client ICD] Memory already mapped\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkDevice memory_device = g_resource_state.get_memory_device(memory);
    if (memory_device != device) {
        ICD_LOG_ERROR() << "[Client ICD] Memory belongs to different device\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkDeviceSize memory_size = g_resource_state.get_memory_size(memory);
    if (size == VK_WHOLE_SIZE) {
        if (offset >= memory_size) {
            ICD_LOG_ERROR() << "[Client ICD] vkMapMemory offset beyond allocation size\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        size = memory_size - offset;
    }

    if (offset + size > memory_size) {
        ICD_LOG_ERROR() << "[Client ICD] vkMapMemory range exceeds allocation (offset=" << offset
                  << ", size=" << size << ", alloc=" << memory_size << ")\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    DeviceEntry* device_entry = g_device_state.get_device(device);
    if (!device_entry) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to find device entry during vkMapMemory\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkPhysicalDeviceMemoryProperties mem_props = {};
    vkGetPhysicalDeviceMemoryProperties(device_entry->physical_device, &mem_props);

    uint32_t type_index = g_resource_state.get_memory_type_index(memory);
    if (type_index >= mem_props.memoryTypeCount) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid memory type index during vkMapMemory\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkMemoryPropertyFlags property_flags = mem_props.memoryTypes[type_index].propertyFlags;
    if ((property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
        ICD_LOG_ERROR() << "[Client ICD] Memory type is not HOST_VISIBLE\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    bool host_coherent = (property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

    void* shadow_ptr = nullptr;
    if (!g_shadow_buffer_manager.create_mapping(device, memory, offset, size, host_coherent, &shadow_ptr)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to allocate shadow buffer for mapping\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkResult read_result = read_memory_data(memory, offset, size, shadow_ptr);
    if (read_result != VK_SUCCESS) {
        ShadowBufferMapping mapping = {};
        g_shadow_buffer_manager.remove_mapping(memory, &mapping);
        if (mapping.data) {
            std::free(mapping.data);
        }
        return read_result;
    }

    *ppData = shadow_ptr;
    ICD_LOG_INFO() << "[Client ICD] Memory mapped (size=" << size << ", offset=" << offset << ")\n";
    return VK_SUCCESS;
}

// vkUnmapMemory - Phase 8
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(
    VkDevice device,
    VkDeviceMemory memory) {

    ICD_LOG_INFO() << "[Client ICD] vkUnmapMemory called\n";

    if (memory == VK_NULL_HANDLE) {
        return;
    }

    ShadowBufferMapping mapping = {};
    if (!g_shadow_buffer_manager.remove_mapping(memory, &mapping)) {
        ICD_LOG_ERROR() << "[Client ICD] vkUnmapMemory: memory was not mapped\n";
        return;
    }

    if (mapping.device != device) {
        ICD_LOG_ERROR() << "[Client ICD] vkUnmapMemory: device mismatch\n";
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Lost connection before flushing vkUnmapMemory\n";
        if (mapping.data) {
            std::free(mapping.data);
        }
        return;
    }

    if (mapping.size > 0 && mapping.data) {
        VkResult result = send_transfer_memory_data(memory, mapping.offset, mapping.size, mapping.data);
        if (result != VK_SUCCESS) {
            ICD_LOG_ERROR() << "[Client ICD] Failed to transfer memory on unmap: " << result << "\n";
        } else {
            ICD_LOG_INFO() << "[Client ICD] Transferred " << mapping.size << " bytes on unmap\n";
        }
    }

    if (mapping.data) {
        std::free(mapping.data);
    }
}

// vkFlushMappedMemoryRanges - Phase 8
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const VkMappedMemoryRange* pMemoryRanges) {

    ICD_LOG_INFO() << "[Client ICD] vkFlushMappedMemoryRanges called (count=" << memoryRangeCount << ")\n";

    if (memoryRangeCount == 0) {
        return VK_SUCCESS;
    }
    if (!pMemoryRanges) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (!ensure_connected()) {
        return VK_ERROR_DEVICE_LOST;
    }

    for (uint32_t i = 0; i < memoryRangeCount; ++i) {
        const VkMappedMemoryRange& range = pMemoryRanges[i];
        ShadowBufferMapping mapping = {};
        if (!g_shadow_buffer_manager.get_mapping(range.memory, &mapping)) {
            ICD_LOG_ERROR() << "[Client ICD] vkFlushMappedMemoryRanges: memory not mapped\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        if (mapping.device != device) {
            ICD_LOG_ERROR() << "[Client ICD] vkFlushMappedMemoryRanges: device mismatch\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        if (range.offset < mapping.offset) {
            ICD_LOG_ERROR() << "[Client ICD] vkFlushMappedMemoryRanges: offset before mapping\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        VkDeviceSize relative_offset = range.offset - mapping.offset;
        if (relative_offset > mapping.size) {
            ICD_LOG_ERROR() << "[Client ICD] vkFlushMappedMemoryRanges: offset beyond mapping size\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        VkDeviceSize flush_size = range.size;
        if (flush_size == VK_WHOLE_SIZE) {
            flush_size = mapping.size - relative_offset;
        }
        if (relative_offset + flush_size > mapping.size) {
            ICD_LOG_ERROR() << "[Client ICD] vkFlushMappedMemoryRanges: range exceeds mapping size\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        if (flush_size == 0) {
            continue;
        }

        const uint8_t* src = static_cast<const uint8_t*>(mapping.data);
        VkResult result = send_transfer_memory_data(range.memory,
                                                    range.offset,
                                                    flush_size,
                                                    src + static_cast<size_t>(relative_offset));
        if (result != VK_SUCCESS) {
            return result;
        }
    }

    return VK_SUCCESS;
}

// vkInvalidateMappedMemoryRanges - Phase 8
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const VkMappedMemoryRange* pMemoryRanges) {

    ICD_LOG_INFO() << "[Client ICD] vkInvalidateMappedMemoryRanges called (count=" << memoryRangeCount << ")\n";

    if (memoryRangeCount == 0) {
        return VK_SUCCESS;
    }
    if (!pMemoryRanges) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (!ensure_connected()) {
        return VK_ERROR_DEVICE_LOST;
    }

    for (uint32_t i = 0; i < memoryRangeCount; ++i) {
        const VkMappedMemoryRange& range = pMemoryRanges[i];
        ShadowBufferMapping mapping = {};
        if (!g_shadow_buffer_manager.get_mapping(range.memory, &mapping)) {
            ICD_LOG_ERROR() << "[Client ICD] vkInvalidateMappedMemoryRanges: memory not mapped\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        if (mapping.device != device) {
            ICD_LOG_ERROR() << "[Client ICD] vkInvalidateMappedMemoryRanges: device mismatch\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        if (range.offset < mapping.offset) {
            ICD_LOG_ERROR() << "[Client ICD] vkInvalidateMappedMemoryRanges: offset before mapping\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        VkDeviceSize relative_offset = range.offset - mapping.offset;
        if (relative_offset > mapping.size) {
            ICD_LOG_ERROR() << "[Client ICD] vkInvalidateMappedMemoryRanges: offset beyond mapping size\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }

        VkDeviceSize read_size = range.size;
        if (read_size == VK_WHOLE_SIZE) {
            read_size = mapping.size - relative_offset;
        }
        if (relative_offset + read_size > mapping.size) {
            ICD_LOG_ERROR() << "[Client ICD] vkInvalidateMappedMemoryRanges: range exceeds mapping size\n";
            return VK_ERROR_MEMORY_MAP_FAILED;
        }
        if (read_size == 0) {
            continue;
        }

        uint8_t* dst = static_cast<uint8_t*>(mapping.data);
        VkResult result = read_memory_data(range.memory,
                                           range.offset,
                                           read_size,
                                           dst + static_cast<size_t>(relative_offset));
        if (result != VK_SUCCESS) {
            return result;
        }
    }

    return VK_SUCCESS;
}

// vkCreateBuffer - Phase 4
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice device,
    const VkBufferCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkBuffer* pBuffer) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateBuffer called\n";

    if (!pCreateInfo || !pBuffer) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDevice remote_device = icd_device->remote_handle;

    VkBuffer remote_buffer = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateBuffer(&g_ring, remote_device, pCreateInfo, pAllocator, &remote_buffer);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateBuffer failed: " << result << "\n";
        return result;
    }

    VkBuffer local_buffer = g_handle_allocator.allocate<VkBuffer>();
    g_resource_state.add_buffer(device, local_buffer, remote_buffer, *pCreateInfo);
    *pBuffer = local_buffer;

    ICD_LOG_INFO() << "[Client ICD] Buffer created (local=" << *pBuffer
              << ", remote=" << remote_buffer
              << ", size=" << pCreateInfo->size << ")\n";
    return VK_SUCCESS;
}

// vkDestroyBuffer - Phase 4
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice device,
    VkBuffer buffer,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyBuffer called\n";

    if (buffer == VK_NULL_HANDLE) {
        return;
    }

    VkBuffer remote_buffer = g_resource_state.get_remote_buffer(buffer);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyBuffer\n";
        g_resource_state.remove_buffer(buffer);
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyBuffer\n";
        g_resource_state.remove_buffer(buffer);
        return;
    }

    if (remote_buffer == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote buffer handle missing\n";
        g_resource_state.remove_buffer(buffer);
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyBuffer(&g_ring, icd_device->remote_handle, remote_buffer, pAllocator);
    g_resource_state.remove_buffer(buffer);
    ICD_LOG_INFO() << "[Client ICD] Buffer destroyed (local=" << buffer << ", remote=" << remote_buffer << ")\n";
}

// vkGetBufferMemoryRequirements - Phase 4
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(
    VkDevice device,
    VkBuffer buffer,
    VkMemoryRequirements* pMemoryRequirements) {

    ICD_LOG_INFO() << "[Client ICD] vkGetBufferMemoryRequirements called\n";

    if (!pMemoryRequirements) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetBufferMemoryRequirements\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    VkBuffer remote_buffer = g_resource_state.get_remote_buffer(buffer);
    if (remote_buffer == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked in vkGetBufferMemoryRequirements\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetBufferMemoryRequirements(&g_ring, icd_device->remote_handle, remote_buffer, pMemoryRequirements);
    g_resource_state.cache_buffer_requirements(buffer, *pMemoryRequirements);

    ICD_LOG_INFO() << "[Client ICD] Buffer memory requirements: size=" << pMemoryRequirements->size
              << ", alignment=" << pMemoryRequirements->alignment << "\n";
}

static bool validate_memory_offset(const VkMemoryRequirements& requirements,
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

// vkBindBufferMemory - Phase 4
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(
    VkDevice device,
    VkBuffer buffer,
    VkDeviceMemory memory,
    VkDeviceSize memoryOffset) {

    ICD_LOG_INFO() << "[Client ICD] vkBindBufferMemory called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkBindBufferMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_resource_state.has_buffer(buffer) || !g_resource_state.has_memory(memory)) {
        ICD_LOG_ERROR() << "[Client ICD] Buffer or memory not tracked in vkBindBufferMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (g_resource_state.buffer_is_bound(buffer)) {
        ICD_LOG_ERROR() << "[Client ICD] Buffer already bound to memory\n";
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkMemoryRequirements cached_requirements;
    if (g_resource_state.get_cached_buffer_requirements(buffer, &cached_requirements)) {
        VkDeviceSize memory_size = g_resource_state.get_memory_size(memory);
        if (!validate_memory_offset(cached_requirements, memory_size, memoryOffset)) {
            ICD_LOG_ERROR() << "[Client ICD] Buffer bind validation failed (offset=" << memoryOffset << ")\n";
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
    }

    VkBuffer remote_buffer = g_resource_state.get_remote_buffer(buffer);
    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);
    if (remote_buffer == VK_NULL_HANDLE || remote_memory == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote handles missing in vkBindBufferMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkBindBufferMemory(&g_ring, icd_device->remote_handle, remote_buffer, remote_memory, memoryOffset);
    if (result == VK_SUCCESS) {
        g_resource_state.bind_buffer(buffer, memory, memoryOffset);
        ICD_LOG_INFO() << "[Client ICD] Buffer bound to memory (buffer=" << buffer
                  << ", memory=" << memory << ", offset=" << memoryOffset << ")\n";
    } else {
        ICD_LOG_ERROR() << "[Client ICD] Server rejected vkBindBufferMemory: " << result << "\n";
    }
    return result;
}

// vkCreateImage - Phase 4
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(
    VkDevice device,
    const VkImageCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImage* pImage) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateImage called\n";

    if (!pCreateInfo || !pImage) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateImage\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateImage\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDevice remote_device = icd_device->remote_handle;

    VkImage remote_image = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateImage(&g_ring, remote_device, pCreateInfo, pAllocator, &remote_image);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateImage failed: " << result << "\n";
        return result;
    }

    VkImage local_image = g_handle_allocator.allocate<VkImage>();
    g_resource_state.add_image(device, local_image, remote_image, *pCreateInfo);
    *pImage = local_image;

    ICD_LOG_INFO() << "[Client ICD] Image created (local=" << *pImage
              << ", remote=" << remote_image
              << ", format=" << pCreateInfo->format
              << ", extent=" << pCreateInfo->extent.width << "x"
              << pCreateInfo->extent.height << ")\n";
    return VK_SUCCESS;
}

// vkDestroyImage - Phase 4
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice device,
    VkImage image,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyImage called\n";

    if (image == VK_NULL_HANDLE) {
        return;
    }

    VkImage remote_image = g_resource_state.get_remote_image(image);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyImage\n";
        g_resource_state.remove_image(image);
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyImage\n";
        g_resource_state.remove_image(image);
        return;
    }

    if (remote_image == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote image handle missing\n";
        g_resource_state.remove_image(image);
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyImage(&g_ring, icd_device->remote_handle, remote_image, pAllocator);
    g_resource_state.remove_image(image);
    ICD_LOG_INFO() << "[Client ICD] Image destroyed (local=" << image << ", remote=" << remote_image << ")\n";
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(
    VkDevice device,
    const VkImageViewCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkImageView* pView) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateImageView called\n";

    if (!pCreateInfo || !pView) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateImageView\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateImageView\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_resource_state.has_image(pCreateInfo->image)) {
        ICD_LOG_ERROR() << "[Client ICD] Image not tracked in vkCreateImageView\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImage remote_image = g_resource_state.get_remote_image(pCreateInfo->image);
    if (remote_image == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote image handle missing for vkCreateImageView\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImageViewCreateInfo remote_info = *pCreateInfo;
    remote_info.image = remote_image;

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkImageView remote_view = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateImageView(&g_ring,
                                                icd_device->remote_handle,
                                                &remote_info,
                                                pAllocator,
                                                &remote_view);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateImageView failed: " << result << "\n";
        return result;
    }

    VkImageView local = g_handle_allocator.allocate<VkImageView>();
    g_resource_state.add_image_view(device, local, remote_view, pCreateInfo->image);
    *pView = local;
    ICD_LOG_INFO() << "[Client ICD] Image view created (local=" << local << ", remote=" << remote_view << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(
    VkDevice device,
    VkImageView imageView,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyImageView called\n";

    if (imageView == VK_NULL_HANDLE) {
        return;
    }

    VkImageView remote_view = g_resource_state.get_remote_image_view(imageView);
    g_resource_state.remove_image_view(imageView);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyImageView\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyImageView\n";
        return;
    }

    if (remote_view == VK_NULL_HANDLE) {
        ICD_LOG_WARN() << "[Client ICD] Remote image view handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyImageView(&g_ring, icd_device->remote_handle, remote_view, pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Image view destroyed (local=" << imageView << ", remote=" << remote_view << ")\n";
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBufferView(
    VkDevice device,
    const VkBufferViewCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkBufferView* pView) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateBufferView called\n";

    if (!pCreateInfo || !pView) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateBufferView\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateBufferView\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_resource_state.has_buffer(pCreateInfo->buffer)) {
        ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked in vkCreateBufferView\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBuffer remote_buffer = g_resource_state.get_remote_buffer(pCreateInfo->buffer);
    if (remote_buffer == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote buffer handle missing for vkCreateBufferView\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkBufferViewCreateInfo remote_info = *pCreateInfo;
    remote_info.buffer = remote_buffer;

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkBufferView remote_view = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateBufferView(&g_ring,
                                                 icd_device->remote_handle,
                                                 &remote_info,
                                                 pAllocator,
                                                 &remote_view);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateBufferView failed: " << result << "\n";
        return result;
    }

    VkBufferView local = g_handle_allocator.allocate<VkBufferView>();
    g_resource_state.add_buffer_view(device,
                                     local,
                                     remote_view,
                                     pCreateInfo->buffer,
                                     pCreateInfo->format,
                                     pCreateInfo->offset,
                                     pCreateInfo->range);
    *pView = local;
    ICD_LOG_INFO() << "[Client ICD] Buffer view created (local=" << local << ", remote=" << remote_view << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyBufferView(
    VkDevice device,
    VkBufferView bufferView,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyBufferView called\n";

    if (bufferView == VK_NULL_HANDLE) {
        return;
    }

    VkBufferView remote_view = g_resource_state.get_remote_buffer_view(bufferView);
    g_resource_state.remove_buffer_view(bufferView);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyBufferView\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyBufferView\n";
        return;
    }

    if (remote_view == VK_NULL_HANDLE) {
        ICD_LOG_WARN() << "[Client ICD] Remote buffer view handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyBufferView(&g_ring, icd_device->remote_handle, remote_view, pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Buffer view destroyed (local=" << bufferView << ", remote=" << remote_view << ")\n";
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(
    VkDevice device,
    const VkSamplerCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSampler* pSampler) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateSampler called\n";

    if (!pCreateInfo || !pSampler) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateSampler\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateSampler\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkSampler remote_sampler = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateSampler(&g_ring,
                                              icd_device->remote_handle,
                                              pCreateInfo,
                                              pAllocator,
                                              &remote_sampler);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateSampler failed: " << result << "\n";
        return result;
    }

    VkSampler local = g_handle_allocator.allocate<VkSampler>();
    g_resource_state.add_sampler(device, local, remote_sampler);
    *pSampler = local;
    ICD_LOG_INFO() << "[Client ICD] Sampler created (local=" << local << ", remote=" << remote_sampler << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySampler(
    VkDevice device,
    VkSampler sampler,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroySampler called\n";

    if (sampler == VK_NULL_HANDLE) {
        return;
    }

    VkSampler remote_sampler = g_resource_state.get_remote_sampler(sampler);
    g_resource_state.remove_sampler(sampler);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroySampler\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroySampler\n";
        return;
    }

    if (remote_sampler == VK_NULL_HANDLE) {
        ICD_LOG_WARN() << "[Client ICD] Remote sampler handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroySampler(&g_ring, icd_device->remote_handle, remote_sampler, pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Sampler destroyed (local=" << sampler << ", remote=" << remote_sampler << ")\n";
}

// vkGetImageMemoryRequirements - Phase 4
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice device,
    VkImage image,
    VkMemoryRequirements* pMemoryRequirements) {

    ICD_LOG_INFO() << "[Client ICD] vkGetImageMemoryRequirements called\n";

    if (!pMemoryRequirements) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetImageMemoryRequirements\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    VkImage remote_image = g_resource_state.get_remote_image(image);
    if (remote_image == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Image not tracked in vkGetImageMemoryRequirements\n";
        memset(pMemoryRequirements, 0, sizeof(VkMemoryRequirements));
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetImageMemoryRequirements(&g_ring, icd_device->remote_handle, remote_image, pMemoryRequirements);
    g_resource_state.cache_image_requirements(image, *pMemoryRequirements);

    ICD_LOG_INFO() << "[Client ICD] Image memory requirements: size=" << pMemoryRequirements->size
              << ", alignment=" << pMemoryRequirements->alignment << "\n";
}

// vkBindImageMemory - Phase 4
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(
    VkDevice device,
    VkImage image,
    VkDeviceMemory memory,
    VkDeviceSize memoryOffset) {

    ICD_LOG_INFO() << "[Client ICD] vkBindImageMemory called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkBindImageMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_resource_state.has_image(image) || !g_resource_state.has_memory(memory)) {
        ICD_LOG_ERROR() << "[Client ICD] Image or memory not tracked in vkBindImageMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (g_resource_state.image_is_bound(image)) {
        ICD_LOG_ERROR() << "[Client ICD] Image already bound to memory\n";
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkMemoryRequirements cached_requirements = {};
    VkDeviceSize memory_size = g_resource_state.get_memory_size(memory);
    if (g_resource_state.get_cached_image_requirements(image, &cached_requirements)) {
        if (!validate_memory_offset(cached_requirements, memory_size, memoryOffset)) {
            ICD_LOG_ERROR() << "[Client ICD] Image bind validation failed (offset=" << memoryOffset << ")\n";
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }
    }

    VkImage remote_image = g_resource_state.get_remote_image(image);
    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);
    if (remote_image == VK_NULL_HANDLE || remote_memory == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote handles missing in vkBindImageMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkBindImageMemory(&g_ring, icd_device->remote_handle, remote_image, remote_memory, memoryOffset);
    if (result == VK_SUCCESS) {
        g_resource_state.bind_image(image, memory, memoryOffset);
        ICD_LOG_INFO() << "[Client ICD] Image bound to memory (image=" << image
                  << ", memory=" << memory << ", offset=" << memoryOffset << ")\n";
    } else {
        ICD_LOG_ERROR() << "[Client ICD] Server rejected vkBindImageMemory: " << result << "\n";
    }
    return result;
}

// vkGetImageSubresourceLayout - Phase 4
VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout(
    VkDevice device,
    VkImage image,
    const VkImageSubresource* pSubresource,
    VkSubresourceLayout* pLayout) {

    ICD_LOG_INFO() << "[Client ICD] vkGetImageSubresourceLayout called\n";

    if (!pSubresource || !pLayout) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pLayout, 0, sizeof(VkSubresourceLayout));
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetImageSubresourceLayout\n";
        memset(pLayout, 0, sizeof(VkSubresourceLayout));
        return;
    }

    VkImage remote_image = g_resource_state.get_remote_image(image);
    if (remote_image == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Image not tracked in vkGetImageSubresourceLayout\n";
        memset(pLayout, 0, sizeof(VkSubresourceLayout));
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetImageSubresourceLayout(&g_ring, icd_device->remote_handle, remote_image, pSubresource, pLayout);
}

// vkCreateShaderModule - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice device,
    const VkShaderModuleCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkShaderModule* pShaderModule) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateShaderModule called\n";

    if (!pCreateInfo || !pShaderModule || !pCreateInfo->pCode || pCreateInfo->codeSize == 0 ||
        (pCreateInfo->codeSize % 4) != 0) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateShaderModule\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateShaderModule\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkShaderModule remote_module = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateShaderModule(&g_ring,
                                                   icd_device->remote_handle,
                                                   pCreateInfo,
                                                   pAllocator,
                                                   &remote_module);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateShaderModule failed: " << result << "\n";
        return result;
    }

    VkShaderModule local = g_handle_allocator.allocate<VkShaderModule>();
    g_pipeline_state.add_shader_module(device, local, remote_module, pCreateInfo->codeSize);
    *pShaderModule = local;

    ICD_LOG_INFO() << "[Client ICD] Shader module created (local=" << local
              << ", remote=" << remote_module << ")\n";
    return VK_SUCCESS;
}

// vkDestroyShaderModule - Phase 9
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(
    VkDevice device,
    VkShaderModule shaderModule,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyShaderModule called\n";

    if (shaderModule == VK_NULL_HANDLE) {
        return;
    }

    VkShaderModule remote_module = g_pipeline_state.get_remote_shader_module(shaderModule);
    g_pipeline_state.remove_shader_module(shaderModule);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyShaderModule\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyShaderModule\n";
        return;
    }

    if (remote_module == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Missing remote shader module handle\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyShaderModule(&g_ring, icd_device->remote_handle, remote_module, pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Shader module destroyed (local=" << shaderModule << ")\n";
}

// vkCreateDescriptorSetLayout - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice device,
    const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDescriptorSetLayout* pSetLayout) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateDescriptorSetLayout called\n";

    if (!pCreateInfo || !pSetLayout) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateDescriptorSetLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateDescriptorSetLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDescriptorSetLayout remote_layout = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateDescriptorSetLayout(&g_ring,
                                                          icd_device->remote_handle,
                                                          pCreateInfo,
                                                          pAllocator,
                                                          &remote_layout);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateDescriptorSetLayout failed: " << result << "\n";
        return result;
    }

    VkDescriptorSetLayout local = g_handle_allocator.allocate<VkDescriptorSetLayout>();
    g_pipeline_state.add_descriptor_set_layout(device, local, remote_layout);
    *pSetLayout = local;
    ICD_LOG_INFO() << "[Client ICD] Descriptor set layout created (local=" << local << ")\n";
    return VK_SUCCESS;
}

// vkDestroyDescriptorSetLayout - Phase 9
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice device,
    VkDescriptorSetLayout descriptorSetLayout,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyDescriptorSetLayout called\n";

    if (descriptorSetLayout == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorSetLayout remote_layout =
        g_pipeline_state.get_remote_descriptor_set_layout(descriptorSetLayout);
    g_pipeline_state.remove_descriptor_set_layout(descriptorSetLayout);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyDescriptorSetLayout\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyDescriptorSetLayout\n";
        return;
    }

    if (remote_layout == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor set layout handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyDescriptorSetLayout(&g_ring,
                                          icd_device->remote_handle,
                                          remote_layout,
                                          pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Descriptor set layout destroyed (local=" << descriptorSetLayout
              << ")\n";
}

// vkCreateDescriptorPool - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice device,
    const VkDescriptorPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDescriptorPool* pDescriptorPool) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateDescriptorPool called\n";

    if (!pCreateInfo || !pDescriptorPool) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateDescriptorPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateDescriptorPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDescriptorPool remote_pool = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateDescriptorPool(&g_ring,
                                                     icd_device->remote_handle,
                                                     pCreateInfo,
                                                     pAllocator,
                                                     &remote_pool);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateDescriptorPool failed: " << result << "\n";
        return result;
    }

    VkDescriptorPool local = g_handle_allocator.allocate<VkDescriptorPool>();
    g_pipeline_state.add_descriptor_pool(device, local, remote_pool, pCreateInfo->flags);
    *pDescriptorPool = local;
    ICD_LOG_INFO() << "[Client ICD] Descriptor pool created (local=" << local << ")\n";
    return VK_SUCCESS;
}

// vkDestroyDescriptorPool - Phase 9
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyDescriptorPool called\n";

    if (descriptorPool == VK_NULL_HANDLE) {
        return;
    }

    VkDescriptorPool remote_pool = g_pipeline_state.get_remote_descriptor_pool(descriptorPool);
    g_pipeline_state.remove_descriptor_pool(descriptorPool);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyDescriptorPool\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyDescriptorPool\n";
        return;
    }

    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor pool handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyDescriptorPool(&g_ring,
                                     icd_device->remote_handle,
                                     remote_pool,
                                     pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Descriptor pool destroyed (local=" << descriptorPool << ")\n";
}

// vkResetDescriptorPool - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkResetDescriptorPool(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    VkDescriptorPoolResetFlags flags) {

    ICD_LOG_INFO() << "[Client ICD] vkResetDescriptorPool called\n";

    if (descriptorPool == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkResetDescriptorPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDescriptorPool remote_pool = g_pipeline_state.get_remote_descriptor_pool(descriptorPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor pool handle missing\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkResetDescriptorPool(&g_ring,
                                                    icd_device->remote_handle,
                                                    remote_pool,
                                                    flags);
    if (result == VK_SUCCESS) {
        g_pipeline_state.reset_descriptor_pool(descriptorPool);
    } else {
        ICD_LOG_ERROR() << "[Client ICD] vkResetDescriptorPool failed: " << result << "\n";
    }
    return result;
}

// vkAllocateDescriptorSets - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice device,
    const VkDescriptorSetAllocateInfo* pAllocateInfo,
    VkDescriptorSet* pDescriptorSets) {

    ICD_LOG_INFO() << "[Client ICD] vkAllocateDescriptorSets called\n";

    if (!pAllocateInfo || (!pDescriptorSets && pAllocateInfo->descriptorSetCount > 0)) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkAllocateDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pAllocateInfo->descriptorSetCount == 0) {
        return VK_SUCCESS;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkAllocateDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!pAllocateInfo->pSetLayouts) {
        ICD_LOG_ERROR() << "[Client ICD] Layout array missing in vkAllocateDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDescriptorPool remote_pool =
        g_pipeline_state.get_remote_descriptor_pool(pAllocateInfo->descriptorPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor pool handle missing\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkDescriptorSetLayout> remote_layouts(pAllocateInfo->descriptorSetCount);
    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        remote_layouts[i] =
            g_pipeline_state.get_remote_descriptor_set_layout(pAllocateInfo->pSetLayouts[i]);
        if (remote_layouts[i] == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Descriptor set layout not tracked for allocation\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    VkDescriptorSetAllocateInfo remote_info = *pAllocateInfo;
    remote_info.descriptorPool = remote_pool;
    remote_info.pSetLayouts = remote_layouts.data();

    IcdDevice* icd_device = icd_device_from_handle(device);
    std::vector<VkDescriptorSet> remote_sets(pAllocateInfo->descriptorSetCount);
    VkResult result = vn_call_vkAllocateDescriptorSets(&g_ring,
                                                       icd_device->remote_handle,
                                                       &remote_info,
                                                       remote_sets.data());
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkAllocateDescriptorSets failed: " << result << "\n";
        return result;
    }

    for (uint32_t i = 0; i < pAllocateInfo->descriptorSetCount; ++i) {
        VkDescriptorSet local = g_handle_allocator.allocate<VkDescriptorSet>();
        g_pipeline_state.add_descriptor_set(device,
                                            pAllocateInfo->descriptorPool,
                                            pAllocateInfo->pSetLayouts[i],
                                            local,
                                            remote_sets[i]);
        pDescriptorSets[i] = local;
    }

    ICD_LOG_INFO() << "[Client ICD] Allocated " << pAllocateInfo->descriptorSetCount
              << " descriptor set(s)\n";
    return VK_SUCCESS;
}

// vkFreeDescriptorSets - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkFreeDescriptorSets(
    VkDevice device,
    VkDescriptorPool descriptorPool,
    uint32_t descriptorSetCount,
    const VkDescriptorSet* pDescriptorSets) {

    ICD_LOG_INFO() << "[Client ICD] vkFreeDescriptorSets called (count=" << descriptorSetCount << ")\n";

    if (descriptorSetCount == 0) {
        return VK_SUCCESS;
    }
    if (!pDescriptorSets) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkFreeDescriptorSets\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDescriptorPool remote_pool = g_pipeline_state.get_remote_descriptor_pool(descriptorPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote descriptor pool handle missing\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkDescriptorSet> remote_sets(descriptorSetCount);
    for (uint32_t i = 0; i < descriptorSetCount; ++i) {
        remote_sets[i] = g_pipeline_state.get_remote_descriptor_set(pDescriptorSets[i]);
        if (remote_sets[i] == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Descriptor set not tracked during free\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkFreeDescriptorSets(&g_ring,
                                                   icd_device->remote_handle,
                                                   remote_pool,
                                                   descriptorSetCount,
                                                   remote_sets.data());
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < descriptorSetCount; ++i) {
            g_pipeline_state.remove_descriptor_set(pDescriptorSets[i]);
        }
        ICD_LOG_INFO() << "[Client ICD] Freed " << descriptorSetCount << " descriptor set(s)\n";
    } else {
        ICD_LOG_ERROR() << "[Client ICD] vkFreeDescriptorSets failed: " << result << "\n";
    }
    return result;
}

// vkUpdateDescriptorSets - Phase 9
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice device,
    uint32_t descriptorWriteCount,
    const VkWriteDescriptorSet* pDescriptorWrites,
    uint32_t descriptorCopyCount,
    const VkCopyDescriptorSet* pDescriptorCopies) {

    ICD_LOG_INFO() << "[Client ICD] vkUpdateDescriptorSets called (writes=" << descriptorWriteCount
              << ", copies=" << descriptorCopyCount << ")\n";

    if (descriptorWriteCount == 0 && descriptorCopyCount == 0) {
        return;
    }

    if ((!pDescriptorWrites && descriptorWriteCount > 0) ||
        (!pDescriptorCopies && descriptorCopyCount > 0)) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid descriptor write/copy arrays\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkUpdateDescriptorSets\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    std::vector<VkWriteDescriptorSet> remote_writes(descriptorWriteCount);
    std::vector<std::vector<VkDescriptorBufferInfo>> buffer_infos(descriptorWriteCount);
    std::vector<std::vector<VkDescriptorImageInfo>> image_infos(descriptorWriteCount);
    std::vector<std::vector<VkBufferView>> texel_views(descriptorWriteCount);

    for (uint32_t i = 0; i < descriptorWriteCount; ++i) {
        const VkWriteDescriptorSet& src = pDescriptorWrites[i];
        VkWriteDescriptorSet& dst = remote_writes[i];
        dst = src;
        dst.dstSet = g_pipeline_state.get_remote_descriptor_set(src.dstSet);
        if (dst.dstSet == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Descriptor set not tracked in vkUpdateDescriptorSets\n";
            return;
        }

        switch (src.descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            if (!src.pBufferInfo) {
                ICD_LOG_ERROR() << "[Client ICD] Missing buffer info for descriptor update\n";
                return;
            }
            buffer_infos[i].resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                buffer_infos[i][j] = src.pBufferInfo[j];
                if (buffer_infos[i][j].buffer != VK_NULL_HANDLE) {
                    buffer_infos[i][j].buffer =
                        g_resource_state.get_remote_buffer(src.pBufferInfo[j].buffer);
                    if (buffer_infos[i][j].buffer == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked for descriptor update\n";
                        return;
                    }
                }
            }
            dst.pBufferInfo = buffer_infos[i].data();
            dst.pImageInfo = nullptr;
            dst.pTexelBufferView = nullptr;
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            if (!src.pImageInfo) {
                ICD_LOG_ERROR() << "[Client ICD] Missing image info for descriptor update\n";
                return;
            }
            image_infos[i].resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                image_infos[i][j] = src.pImageInfo[j];
                if (image_infos[i][j].imageView != VK_NULL_HANDLE) {
                    image_infos[i][j].imageView =
                        g_resource_state.get_remote_image_view(src.pImageInfo[j].imageView);
                    if (image_infos[i][j].imageView == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Image view not tracked for descriptor update\n";
                        return;
                    }
                }
                if (image_infos[i][j].sampler != VK_NULL_HANDLE) {
                    image_infos[i][j].sampler =
                        g_resource_state.get_remote_sampler(src.pImageInfo[j].sampler);
                    if (image_infos[i][j].sampler == VK_NULL_HANDLE) {
                        ICD_LOG_ERROR() << "[Client ICD] Sampler not tracked for descriptor update\n";
                        return;
                    }
                }
            }
            dst.pBufferInfo = nullptr;
            dst.pImageInfo = image_infos[i].data();
            dst.pTexelBufferView = nullptr;
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            if (!src.pTexelBufferView) {
                ICD_LOG_ERROR() << "[Client ICD] Missing texel buffer info for descriptor update\n";
                return;
            }
            texel_views[i].resize(src.descriptorCount);
            for (uint32_t j = 0; j < src.descriptorCount; ++j) {
                if (src.pTexelBufferView[j] == VK_NULL_HANDLE) {
                    texel_views[i][j] = VK_NULL_HANDLE;
                    continue;
                }
                texel_views[i][j] =
                    g_resource_state.get_remote_buffer_view(src.pTexelBufferView[j]);
                if (texel_views[i][j] == VK_NULL_HANDLE) {
                    ICD_LOG_ERROR() << "[Client ICD] Buffer view not tracked for descriptor update\n";
                    return;
                }
            }
            dst.pBufferInfo = nullptr;
            dst.pImageInfo = nullptr;
            dst.pTexelBufferView = texel_views[i].data();
            break;
        default:
            if (src.descriptorCount > 0) {
                ICD_LOG_ERROR() << "[Client ICD] Unsupported descriptor type in vkUpdateDescriptorSets\n";
                return;
            }
            dst.pBufferInfo = nullptr;
            dst.pImageInfo = nullptr;
            dst.pTexelBufferView = nullptr;
            break;
        }
    }

    std::vector<VkCopyDescriptorSet> remote_copies(descriptorCopyCount);
    for (uint32_t i = 0; i < descriptorCopyCount; ++i) {
        remote_copies[i] = pDescriptorCopies[i];
        remote_copies[i].srcSet =
            g_pipeline_state.get_remote_descriptor_set(pDescriptorCopies[i].srcSet);
        remote_copies[i].dstSet =
            g_pipeline_state.get_remote_descriptor_set(pDescriptorCopies[i].dstSet);
        if (remote_copies[i].srcSet == VK_NULL_HANDLE ||
            remote_copies[i].dstSet == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Descriptor set not tracked for copy update\n";
            return;
        }
    }

    vn_async_vkUpdateDescriptorSets(&g_ring,
                                    icd_device->remote_handle,
                                    descriptorWriteCount,
                                    remote_writes.data(),
                                    descriptorCopyCount,
                                    remote_copies.data());
    ICD_LOG_INFO() << "[Client ICD] Descriptor sets updated\n";
}

// vkCreatePipelineLayout - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice device,
    const VkPipelineLayoutCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkPipelineLayout* pPipelineLayout) {

    ICD_LOG_INFO() << "[Client ICD] vkCreatePipelineLayout called\n";

    if (!pCreateInfo || !pPipelineLayout) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreatePipelineLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreatePipelineLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkDescriptorSetLayout> remote_layouts;
    if (pCreateInfo->setLayoutCount > 0) {
        remote_layouts.resize(pCreateInfo->setLayoutCount);
        for (uint32_t i = 0; i < pCreateInfo->setLayoutCount; ++i) {
            remote_layouts[i] =
                g_pipeline_state.get_remote_descriptor_set_layout(pCreateInfo->pSetLayouts[i]);
            if (remote_layouts[i] == VK_NULL_HANDLE) {
                ICD_LOG_ERROR() << "[Client ICD] Descriptor set layout not tracked for pipeline layout\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
    }

    VkPipelineLayoutCreateInfo remote_info = *pCreateInfo;
    if (!remote_layouts.empty()) {
        remote_info.pSetLayouts = remote_layouts.data();
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkPipelineLayout remote_layout = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreatePipelineLayout(&g_ring,
                                                     icd_device->remote_handle,
                                                     &remote_info,
                                                     pAllocator,
                                                     &remote_layout);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreatePipelineLayout failed: " << result << "\n";
        return result;
    }

    VkPipelineLayout local = g_handle_allocator.allocate<VkPipelineLayout>();
    g_pipeline_state.add_pipeline_layout(device, local, remote_layout, pCreateInfo);
    *pPipelineLayout = local;
    ICD_LOG_INFO() << "[Client ICD] Pipeline layout created (local=" << local << ")\n";
    return VK_SUCCESS;
}

// vkDestroyPipelineLayout - Phase 9
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(
    VkDevice device,
    VkPipelineLayout pipelineLayout,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyPipelineLayout called\n";

    if (pipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    VkPipelineLayout remote_layout =
        g_pipeline_state.get_remote_pipeline_layout(pipelineLayout);
    g_pipeline_state.remove_pipeline_layout(pipelineLayout);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyPipelineLayout\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyPipelineLayout\n";
        return;
    }

    if (remote_layout == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote pipeline layout handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyPipelineLayout(&g_ring,
                                     icd_device->remote_handle,
                                     remote_layout,
                                     pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Pipeline layout destroyed (local=" << pipelineLayout << ")\n";
}

// vkCreatePipelineCache - Phase 9_3
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineCache(
    VkDevice device,
    const VkPipelineCacheCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkPipelineCache* pPipelineCache) {

    ICD_LOG_INFO() << "[Client ICD] vkCreatePipelineCache called\n";

    if (!pCreateInfo || !pPipelineCache) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreatePipelineCache\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreatePipelineCache\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkPipelineCache remote_cache = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreatePipelineCache(&g_ring,
                                                    icd_device->remote_handle,
                                                    pCreateInfo,
                                                    pAllocator,
                                                    &remote_cache);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreatePipelineCache failed: " << result << "\n";
        return result;
    }

    VkPipelineCache local_cache = g_handle_allocator.allocate<VkPipelineCache>();
    g_pipeline_state.add_pipeline_cache(device, local_cache, remote_cache);
    *pPipelineCache = local_cache;
    ICD_LOG_INFO() << "[Client ICD] Pipeline cache created (local=" << local_cache << ")\n";
    return VK_SUCCESS;
}

// vkDestroyPipelineCache - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineCache(
    VkDevice device,
    VkPipelineCache pipelineCache,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyPipelineCache called\n";

    if (pipelineCache == VK_NULL_HANDLE) {
        return;
    }

    VkPipelineCache remote_cache = g_pipeline_state.get_remote_pipeline_cache(pipelineCache);
    g_pipeline_state.remove_pipeline_cache(pipelineCache);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyPipelineCache\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyPipelineCache\n";
        return;
    }

    if (remote_cache == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline cache not tracked in vkDestroyPipelineCache\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyPipelineCache(&g_ring, icd_device->remote_handle, remote_cache, pAllocator);
}

// vkGetPipelineCacheData - Phase 9_3
VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineCacheData(
    VkDevice device,
    VkPipelineCache pipelineCache,
    size_t* pDataSize,
    void* pData) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPipelineCacheData called\n";

    if (!pDataSize) {
        ICD_LOG_ERROR() << "[Client ICD] pDataSize is NULL in vkGetPipelineCacheData\n";
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetPipelineCacheData\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPipelineCache remote_cache = g_pipeline_state.get_remote_pipeline_cache(pipelineCache);
    if (remote_cache == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline cache not tracked in vkGetPipelineCacheData\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    return vn_call_vkGetPipelineCacheData(&g_ring,
                                          icd_device->remote_handle,
                                          remote_cache,
                                          pDataSize,
                                          pData);
}

// vkMergePipelineCaches - Phase 9_3
VKAPI_ATTR VkResult VKAPI_CALL vkMergePipelineCaches(
    VkDevice device,
    VkPipelineCache dstCache,
    uint32_t srcCacheCount,
    const VkPipelineCache* pSrcCaches) {

    ICD_LOG_INFO() << "[Client ICD] vkMergePipelineCaches called\n";

    if (dstCache == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (srcCacheCount == 0) {
        return VK_SUCCESS;
    }

    if (!pSrcCaches) {
        ICD_LOG_ERROR() << "[Client ICD] pSrcCaches is NULL in vkMergePipelineCaches\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkMergePipelineCaches\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPipelineCache remote_dst = g_pipeline_state.get_remote_pipeline_cache(dstCache);
    if (remote_dst == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Destination cache not tracked in vkMergePipelineCaches\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (g_pipeline_state.get_pipeline_cache_device(dstCache) != device) {
        ICD_LOG_ERROR() << "[Client ICD] Destination cache belongs to different device\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkPipelineCache> remote_src(srcCacheCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < srcCacheCount; ++i) {
        remote_src[i] = g_pipeline_state.get_remote_pipeline_cache(pSrcCaches[i]);
        if (remote_src[i] == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Source cache not tracked in vkMergePipelineCaches\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        if (g_pipeline_state.get_pipeline_cache_device(pSrcCaches[i]) != device) {
            ICD_LOG_ERROR() << "[Client ICD] Source cache belongs to different device\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    return vn_call_vkMergePipelineCaches(&g_ring,
                                         icd_device->remote_handle,
                                         remote_dst,
                                         srcCacheCount,
                                         remote_src.data());
}

// vkCreateQueryPool - Phase 9_3
VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(
    VkDevice device,
    const VkQueryPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkQueryPool* pQueryPool) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateQueryPool called\n";

    if (!pCreateInfo || !pQueryPool) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateQueryPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateQueryPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkQueryPool remote_pool = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateQueryPool(&g_ring,
                                                icd_device->remote_handle,
                                                pCreateInfo,
                                                pAllocator,
                                                &remote_pool);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateQueryPool failed: " << result << "\n";
        return result;
    }

    VkQueryPool local_pool = g_handle_allocator.allocate<VkQueryPool>();
    g_query_state.add_query_pool(device,
                                 local_pool,
                                 remote_pool,
                                 pCreateInfo->queryType,
                                 pCreateInfo->queryCount,
                                 pCreateInfo->pipelineStatistics);
    *pQueryPool = local_pool;
    ICD_LOG_INFO() << "[Client ICD] Query pool created (local=" << local_pool << ")\n";
    return VK_SUCCESS;
}

// vkDestroyQueryPool - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(
    VkDevice device,
    VkQueryPool queryPool,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyQueryPool called\n";

    if (queryPool == VK_NULL_HANDLE) {
        return;
    }

    VkQueryPool remote_pool = g_query_state.get_remote_query_pool(queryPool);
    g_query_state.remove_query_pool(queryPool);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyQueryPool\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyQueryPool\n";
        return;
    }

    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Query pool not tracked in vkDestroyQueryPool\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyQueryPool(&g_ring, icd_device->remote_handle, remote_pool, pAllocator);
}

// vkResetQueryPool - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkResetQueryPool(
    VkDevice device,
    VkQueryPool queryPool,
    uint32_t firstQuery,
    uint32_t queryCount) {

    ICD_LOG_INFO() << "[Client ICD] vkResetQueryPool called\n";

    if (queryCount == 0) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkResetQueryPool\n";
        return;
    }

    if (!g_query_state.validate_query_range(queryPool, firstQuery, queryCount)) {
        ICD_LOG_ERROR() << "[Client ICD] Query range invalid in vkResetQueryPool\n";
        return;
    }

    VkQueryPool remote_pool = g_query_state.get_remote_query_pool(queryPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Query pool not tracked in vkResetQueryPool\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkResetQueryPool(&g_ring,
                              icd_device->remote_handle,
                              remote_pool,
                              firstQuery,
                              queryCount);
}

// vkGetQueryPoolResults - Phase 9_3
VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(
    VkDevice device,
    VkQueryPool queryPool,
    uint32_t firstQuery,
    uint32_t queryCount,
    size_t dataSize,
    void* pData,
    VkDeviceSize stride,
    VkQueryResultFlags flags) {

    ICD_LOG_INFO() << "[Client ICD] vkGetQueryPoolResults called\n";

    if (queryCount == 0) {
        return VK_SUCCESS;
    }

    if (dataSize == 0 || !pData) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid data buffer in vkGetQueryPoolResults\n";
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetQueryPoolResults\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_query_state.validate_query_range(queryPool, firstQuery, queryCount)) {
        ICD_LOG_ERROR() << "[Client ICD] Query range invalid in vkGetQueryPoolResults\n";
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    VkQueryPool remote_pool = g_query_state.get_remote_query_pool(queryPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Query pool not tracked in vkGetQueryPoolResults\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    return vn_call_vkGetQueryPoolResults(&g_ring,
                                         icd_device->remote_handle,
                                         remote_pool,
                                         firstQuery,
                                         queryCount,
                                         dataSize,
                                         pData,
                                         stride,
                                         flags);
}

// vkCreateSwapchainKHR - Phase 10
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(
    VkDevice device,
    const VkSwapchainCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSwapchainKHR* pSwapchain) {

    (void)pAllocator;
    ICD_LOG_INFO() << "[Client ICD] vkCreateSwapchainKHR called\n";

    if (!pCreateInfo || !pSwapchain) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pCreateInfo->imageExtent.width == 0 || pCreateInfo->imageExtent.height == 0) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid swapchain extent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateSwapchainKHR\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    uint32_t swapchain_id = g_swapchain_state.allocate_swapchain_id();
    VkDevice remote_device = g_device_state.get_remote_device(device);
    if (remote_device == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to resolve remote device for swapchain\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VenusSwapchainCreateRequest request = {};
    request.command = VENUS_PLUS_CMD_CREATE_SWAPCHAIN;
    request.create_info.swapchain_id = swapchain_id;
    request.create_info.width = pCreateInfo->imageExtent.width;
    request.create_info.height = pCreateInfo->imageExtent.height;
    request.create_info.format = static_cast<uint32_t>(pCreateInfo->imageFormat);
    request.create_info.image_count = std::max(pCreateInfo->minImageCount, 1u);
    request.create_info.usage = pCreateInfo->imageUsage;
    request.create_info.present_mode = pCreateInfo->presentMode;
    request.create_info.device_handle = reinterpret_cast<uint64_t>(remote_device);

    std::vector<uint8_t> reply_buffer;
    if (!send_swapchain_command(&request, sizeof(request), &reply_buffer)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (reply_buffer.size() < sizeof(VenusSwapchainCreateReply)) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid swapchain reply size\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const auto* reply = reinterpret_cast<const VenusSwapchainCreateReply*>(reply_buffer.data());
    if (reply->result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateSwapchainKHR failed on server: " << reply->result << "\n";
        return reply->result;
    }

    uint32_t image_count = reply->actual_image_count;
    if (image_count == 0) {
        image_count = request.create_info.image_count;
    }
    if (image_count > kVenusMaxSwapchainImages) {
        ICD_LOG_ERROR() << "[Client ICD] Server reported too many swapchain images\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkImage> images(image_count);
    for (uint32_t i = 0; i < image_count; ++i) {
        images[i] = g_handle_allocator.allocate<VkImage>();
    }

    VkImageCreateInfo swapchain_image_info = {};
    swapchain_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    swapchain_image_info.imageType = VK_IMAGE_TYPE_2D;
    swapchain_image_info.format = pCreateInfo->imageFormat;
    swapchain_image_info.extent = {pCreateInfo->imageExtent.width,
                                   pCreateInfo->imageExtent.height,
                                   1};
    swapchain_image_info.mipLevels = 1;
    swapchain_image_info.arrayLayers = 1;
    swapchain_image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    swapchain_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    swapchain_image_info.usage = pCreateInfo->imageUsage |
                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapchain_image_info.flags = 0;

    for (uint32_t i = 0; i < image_count; ++i) {
        VkImage remote_image = reinterpret_cast<VkImage>(reply->images[i].image_handle);
        if (remote_image == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Missing remote swapchain image handle\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        g_resource_state.add_image(device, images[i], remote_image, swapchain_image_info);
    }

    auto wsi = create_platform_wsi(pCreateInfo->surface);
    if (!wsi || !wsi->init(*pCreateInfo, image_count)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to initialize Platform WSI\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkSwapchainKHR handle = g_swapchain_state.add_swapchain(device,
                                                           swapchain_id,
                                                           *pCreateInfo,
                                                           image_count,
                                                           std::move(images),
                                                           wsi);
    *pSwapchain = handle;
    ICD_LOG_INFO() << "[Client ICD] Swapchain created (id=" << swapchain_id << ")\n";
    return VK_SUCCESS;
}

// vkDestroySwapchainKHR - Phase 10
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    const VkAllocationCallbacks* pAllocator) {

    (void)pAllocator;
    ICD_LOG_INFO() << "[Client ICD] vkDestroySwapchainKHR called\n";

    if (swapchain == VK_NULL_HANDLE) {
        return;
    }

    SwapchainInfo info = {};
    if (!g_swapchain_state.remove_swapchain(swapchain, &info)) {
        ICD_LOG_WARN() << "[Client ICD] Swapchain not tracked locally\n";
        return;
    }

    for (VkImage image : info.images) {
        g_resource_state.remove_image(image);
    }

    if (info.wsi) {
        info.wsi->shutdown();
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during swapchain destroy\n";
        return;
    }

    VenusSwapchainDestroyRequest request = {};
    request.command = VENUS_PLUS_CMD_DESTROY_SWAPCHAIN;
    request.swapchain_id = info.swapchain_id;

    std::vector<uint8_t> reply_buffer;
    if (!send_swapchain_command(&request, sizeof(request), &reply_buffer)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to send destroy swapchain command\n";
        return;
    }

    if (reply_buffer.size() < sizeof(VkResult)) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid destroy reply size\n";
        return;
    }

    VkResult result = *reinterpret_cast<VkResult*>(reply_buffer.data());
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] Server failed to destroy swapchain: " << result << "\n";
    }
}

// vkGetSwapchainImagesKHR - Phase 10
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint32_t* pSwapchainImageCount,
    VkImage* pSwapchainImages) {

    (void)device;
    ICD_LOG_INFO() << "[Client ICD] vkGetSwapchainImagesKHR called\n";

    if (!pSwapchainImageCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkImage> images;
    if (!g_swapchain_state.get_images(swapchain, &images)) {
        ICD_LOG_ERROR() << "[Client ICD] Swapchain not tracked for images\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!pSwapchainImages) {
        *pSwapchainImageCount = static_cast<uint32_t>(images.size());
        return VK_SUCCESS;
    }

    uint32_t count = std::min(*pSwapchainImageCount, static_cast<uint32_t>(images.size()));
    for (uint32_t i = 0; i < count; ++i) {
        pSwapchainImages[i] = images[i];
    }
    *pSwapchainImageCount = count;
    return (count < images.size()) ? VK_INCOMPLETE : VK_SUCCESS;
}

// vkAcquireNextImageKHR - Phase 10
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImageKHR(
    VkDevice device,
    VkSwapchainKHR swapchain,
    uint64_t timeout,
    VkSemaphore semaphore,
    VkFence fence,
    uint32_t* pImageIndex) {

    (void)device;
    (void)timeout;
    (void)semaphore;
    (void)fence;

    ICD_LOG_INFO() << "[Client ICD] vkAcquireNextImageKHR called\n";

    if (!pImageIndex) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    uint32_t remote_id = g_swapchain_state.get_remote_id(swapchain);
    if (remote_id == 0) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown swapchain in acquire\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VenusSwapchainAcquireRequest request = {};
    request.command = VENUS_PLUS_CMD_ACQUIRE_IMAGE;
    request.swapchain_id = remote_id;
    request.timeout = timeout;

    std::vector<uint8_t> reply_buffer;
    if (!send_swapchain_command(&request, sizeof(request), &reply_buffer)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (reply_buffer.size() < sizeof(VenusSwapchainAcquireReply)) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid acquire reply size\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const auto* reply = reinterpret_cast<const VenusSwapchainAcquireReply*>(reply_buffer.data());
    if (reply->result == VK_SUCCESS) {
        *pImageIndex = reply->image_index;
    }
    return reply->result;
}

// vkAcquireNextImage2KHR - Phase 10
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireNextImage2KHR(
    VkDevice device,
    const VkAcquireNextImageInfoKHR* pAcquireInfo,
    uint32_t* pImageIndex) {

    if (!pAcquireInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return vkAcquireNextImageKHR(device,
                                 pAcquireInfo->swapchain,
                                 pAcquireInfo->timeout,
                                 pAcquireInfo->semaphore,
                                 pAcquireInfo->fence,
                                 pImageIndex);
}
// vkCreateRenderPass - Phase 10
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(
    VkDevice device,
    const VkRenderPassCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkRenderPass* pRenderPass) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateRenderPass called\n";

    if (!pCreateInfo || !pRenderPass) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateRenderPass\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateRenderPass\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkRenderPass remote_render_pass = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateRenderPass(&g_ring,
                                                 icd_device->remote_handle,
                                                 pCreateInfo,
                                                 pAllocator,
                                                 &remote_render_pass);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateRenderPass failed: " << result << "\n";
        return result;
    }

    VkRenderPass local = g_handle_allocator.allocate<VkRenderPass>();
    *pRenderPass = local;
    g_resource_state.add_render_pass(device, local, remote_render_pass);
    ICD_LOG_INFO() << "[Client ICD] Render pass created (local=" << local << ")\n";
    return VK_SUCCESS;
}

// vkCreateRenderPass2 - Phase 10
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass2(
    VkDevice device,
    const VkRenderPassCreateInfo2* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkRenderPass* pRenderPass) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateRenderPass2 called\n";

    if (!pCreateInfo || !pRenderPass) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateRenderPass2\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateRenderPass2\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkRenderPass remote_render_pass = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateRenderPass2(&g_ring,
                                                  icd_device->remote_handle,
                                                  pCreateInfo,
                                                  pAllocator,
                                                  &remote_render_pass);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateRenderPass2 failed: " << result << "\n";
        return result;
    }

    VkRenderPass local = g_handle_allocator.allocate<VkRenderPass>();
    *pRenderPass = local;
    g_resource_state.add_render_pass(device, local, remote_render_pass);
    ICD_LOG_INFO() << "[Client ICD] Render pass (v2) created (local=" << local << ")\n";
    return VK_SUCCESS;
}

// vkDestroyRenderPass - Phase 10
VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(
    VkDevice device,
    VkRenderPass renderPass,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyRenderPass called\n";

    if (renderPass == VK_NULL_HANDLE) {
        return;
    }

    VkRenderPass remote_render_pass = g_resource_state.get_remote_render_pass(renderPass);
    g_resource_state.remove_render_pass(renderPass);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyRenderPass\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyRenderPass\n";
        return;
    }

    if (remote_render_pass == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote render pass handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyRenderPass(&g_ring,
                                 icd_device->remote_handle,
                                 remote_render_pass,
                                 pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Render pass destroyed (local=" << renderPass << ")\n";
}

// vkCreateFramebuffer - Phase 10
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(
    VkDevice device,
    const VkFramebufferCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkFramebuffer* pFramebuffer) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateFramebuffer called\n";

    if (!pCreateInfo || !pFramebuffer) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateFramebuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateFramebuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkRenderPass remote_render_pass =
        g_resource_state.get_remote_render_pass(pCreateInfo->renderPass);
    if (remote_render_pass == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Render pass not tracked for framebuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkImageView> remote_attachments;
    if (pCreateInfo->attachmentCount > 0) {
        remote_attachments.resize(pCreateInfo->attachmentCount);
        for (uint32_t i = 0; i < pCreateInfo->attachmentCount; ++i) {
            remote_attachments[i] =
                g_resource_state.get_remote_image_view(pCreateInfo->pAttachments[i]);
            if (remote_attachments[i] == VK_NULL_HANDLE) {
                ICD_LOG_ERROR() << "[Client ICD] Attachment image view not tracked for framebuffer\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
    }

    VkFramebufferCreateInfo remote_info = *pCreateInfo;
    remote_info.renderPass = remote_render_pass;
    if (!remote_attachments.empty()) {
        remote_info.pAttachments = remote_attachments.data();
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkFramebuffer remote_framebuffer = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateFramebuffer(&g_ring,
                                                  icd_device->remote_handle,
                                                  &remote_info,
                                                  pAllocator,
                                                  &remote_framebuffer);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateFramebuffer failed: " << result << "\n";
        return result;
    }

    VkFramebuffer local = g_handle_allocator.allocate<VkFramebuffer>();
    *pFramebuffer = local;
    g_resource_state.add_framebuffer(device, local, remote_framebuffer, pCreateInfo->renderPass, *pCreateInfo);
    ICD_LOG_INFO() << "[Client ICD] Framebuffer created (local=" << local << ")\n";
    return VK_SUCCESS;
}

// vkDestroyFramebuffer - Phase 10
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(
    VkDevice device,
    VkFramebuffer framebuffer,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyFramebuffer called\n";

    if (framebuffer == VK_NULL_HANDLE) {
        return;
    }

    VkFramebuffer remote_framebuffer = g_resource_state.get_remote_framebuffer(framebuffer);
    g_resource_state.remove_framebuffer(framebuffer);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyFramebuffer\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyFramebuffer\n";
        return;
    }

    if (remote_framebuffer == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote framebuffer handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyFramebuffer(&g_ring,
                                  icd_device->remote_handle,
                                  remote_framebuffer,
                                  pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Framebuffer destroyed (local=" << framebuffer << ")\n";
}

// vkCreateGraphicsPipelines - Phase 10
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(
    VkDevice device,
    VkPipelineCache pipelineCache,
    uint32_t createInfoCount,
    const VkGraphicsPipelineCreateInfo* pCreateInfos,
    const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateGraphicsPipelines called (count=" << createInfoCount << ")\n";

    if (!pCreateInfos || (!pPipelines && createInfoCount > 0)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (createInfoCount == 0) {
        return VK_SUCCESS;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateGraphicsPipelines\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkGraphicsPipelineCreateInfo> remote_infos(createInfoCount);
    std::vector<std::vector<VkPipelineShaderStageCreateInfo>> stage_infos(createInfoCount);
    for (uint32_t i = 0; i < createInfoCount; ++i) {
        remote_infos[i] = pCreateInfos[i];

        stage_infos[i].resize(remote_infos[i].stageCount);
        for (uint32_t j = 0; j < remote_infos[i].stageCount; ++j) {
            stage_infos[i][j] = pCreateInfos[i].pStages[j];
            VkShaderModule remote_module =
                g_pipeline_state.get_remote_shader_module(pCreateInfos[i].pStages[j].module);
            if (remote_module == VK_NULL_HANDLE) {
                ICD_LOG_ERROR() << "[Client ICD] Shader module not tracked for graphics pipeline\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            stage_infos[i][j].module = remote_module;
        }
        if (!stage_infos[i].empty()) {
            remote_infos[i].pStages = stage_infos[i].data();
        }

        VkPipelineLayout remote_layout =
            g_pipeline_state.get_remote_pipeline_layout(pCreateInfos[i].layout);
        if (remote_layout == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Pipeline layout not tracked for graphics pipeline\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_infos[i].layout = remote_layout;

        if (pCreateInfos[i].renderPass != VK_NULL_HANDLE) {
            VkRenderPass remote_render_pass =
                g_resource_state.get_remote_render_pass(pCreateInfos[i].renderPass);
            if (remote_render_pass == VK_NULL_HANDLE) {
                ICD_LOG_ERROR() << "[Client ICD] Render pass not tracked for graphics pipeline\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            remote_infos[i].renderPass = remote_render_pass;
        }

        if (pCreateInfos[i].basePipelineHandle != VK_NULL_HANDLE) {
            VkPipeline remote_base =
                g_pipeline_state.get_remote_pipeline(pCreateInfos[i].basePipelineHandle);
            if (remote_base == VK_NULL_HANDLE) {
                ICD_LOG_ERROR() << "[Client ICD] Base pipeline not tracked for graphics pipeline\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            remote_infos[i].basePipelineHandle = remote_base;
        }
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    std::vector<VkPipeline> remote_pipelines(createInfoCount, VK_NULL_HANDLE);
    VkResult result = vn_call_vkCreateGraphicsPipelines(&g_ring,
                                                       icd_device->remote_handle,
                                                       pipelineCache,
                                                       createInfoCount,
                                                       remote_infos.data(),
                                                       pAllocator,
                                                       remote_pipelines.data());
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateGraphicsPipelines failed: " << result << "\n";
        return result;
    }

    for (uint32_t i = 0; i < createInfoCount; ++i) {
        VkPipeline local = g_handle_allocator.allocate<VkPipeline>();
        g_pipeline_state.add_pipeline(device,
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      local,
                                      remote_pipelines[i]);
        pPipelines[i] = local;
    }

    ICD_LOG_INFO() << "[Client ICD] Graphics pipeline(s) created\n";
    return VK_SUCCESS;
}

// vkCreateComputePipelines - Phase 9
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(
    VkDevice device,
    VkPipelineCache pipelineCache,
    uint32_t createInfoCount,
    const VkComputePipelineCreateInfo* pCreateInfos,
    const VkAllocationCallbacks* pAllocator,
    VkPipeline* pPipelines) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateComputePipelines called (count=" << createInfoCount << ")\n";

    if (!pCreateInfos || (!pPipelines && createInfoCount > 0)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (createInfoCount == 0) {
        return VK_SUCCESS;
    }

    if (pipelineCache != VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline cache not supported in Phase 9\n";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateComputePipelines\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkComputePipelineCreateInfo> remote_infos(createInfoCount);
    for (uint32_t i = 0; i < createInfoCount; ++i) {
        remote_infos[i] = pCreateInfos[i];
        VkShaderModule remote_module =
            g_pipeline_state.get_remote_shader_module(pCreateInfos[i].stage.module);
        if (remote_module == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Shader module not tracked for compute pipeline\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_infos[i].stage.module = remote_module;

        VkPipelineLayout remote_layout =
            g_pipeline_state.get_remote_pipeline_layout(pCreateInfos[i].layout);
        if (remote_layout == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Pipeline layout not tracked for compute pipeline\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_infos[i].layout = remote_layout;

        if (pCreateInfos[i].basePipelineHandle != VK_NULL_HANDLE) {
            VkPipeline remote_base =
                g_pipeline_state.get_remote_pipeline(pCreateInfos[i].basePipelineHandle);
            if (remote_base == VK_NULL_HANDLE) {
                ICD_LOG_ERROR() << "[Client ICD] Base pipeline not tracked for compute pipeline\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            remote_infos[i].basePipelineHandle = remote_base;
        }
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    std::vector<VkPipeline> remote_pipelines(createInfoCount, VK_NULL_HANDLE);
    VkResult result = vn_call_vkCreateComputePipelines(&g_ring,
                                                       icd_device->remote_handle,
                                                       pipelineCache,
                                                       createInfoCount,
                                                       remote_infos.data(),
                                                       pAllocator,
                                                       remote_pipelines.data());
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateComputePipelines failed: " << result << "\n";
        return result;
    }

    for (uint32_t i = 0; i < createInfoCount; ++i) {
        VkPipeline local = g_handle_allocator.allocate<VkPipeline>();
        g_pipeline_state.add_pipeline(device,
                                      VK_PIPELINE_BIND_POINT_COMPUTE,
                                      local,
                                      remote_pipelines[i]);
        pPipelines[i] = local;
    }

    ICD_LOG_INFO() << "[Client ICD] Compute pipeline(s) created\n";
    return VK_SUCCESS;
}

// vkDestroyPipeline - Phase 9
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(
    VkDevice device,
    VkPipeline pipeline,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyPipeline called\n";

    if (pipeline == VK_NULL_HANDLE) {
        return;
    }

    VkPipeline remote_pipeline = g_pipeline_state.get_remote_pipeline(pipeline);
    g_pipeline_state.remove_pipeline(pipeline);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyPipeline\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyPipeline\n";
        return;
    }

    if (remote_pipeline == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote pipeline handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyPipeline(&g_ring,
                               icd_device->remote_handle,
                               remote_pipeline,
                               pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Pipeline destroyed (local=" << pipeline << ")\n";
}

// vkCreateCommandPool - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
    VkDevice device,
    const VkCommandPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkCommandPool* pCommandPool) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateCommandPool called\n";

    if (!pCreateInfo || !pCommandPool) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateCommandPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateCommandPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkCommandPool remote_pool = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateCommandPool(&g_ring, icd_device->remote_handle, pCreateInfo, pAllocator, &remote_pool);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateCommandPool failed: " << result << "\n";
        return result;
    }

    VkCommandPool local_pool = g_handle_allocator.allocate<VkCommandPool>();
    *pCommandPool = local_pool;
    g_command_buffer_state.add_pool(device, local_pool, remote_pool, *pCreateInfo);

    ICD_LOG_INFO() << "[Client ICD] Command pool created (local=" << local_pool
              << ", family=" << pCreateInfo->queueFamilyIndex << ")\n";
    return VK_SUCCESS;
}

// vkDestroyCommandPool - Phase 5
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyCommandPool called\n";

    if (commandPool == VK_NULL_HANDLE) {
        return;
    }

    VkCommandPool remote_pool = g_command_buffer_state.get_remote_pool(commandPool);
    std::vector<VkCommandBuffer> buffers_to_free;
    g_command_buffer_state.remove_pool(commandPool, &buffers_to_free);

    for (VkCommandBuffer buffer : buffers_to_free) {
        IcdCommandBuffer* icd_cb = icd_command_buffer_from_handle(buffer);
        delete icd_cb;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyCommandPool\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyCommandPool\n";
        return;
    }

    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command pool handle missing\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyCommandPool(&g_ring, icd_device->remote_handle, remote_pool, pAllocator);
    ICD_LOG_INFO() << "[Client ICD] Command pool destroyed (local=" << commandPool << ")\n";
}

// vkResetCommandPool - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandPool(
    VkDevice device,
    VkCommandPool commandPool,
    VkCommandPoolResetFlags flags) {

    ICD_LOG_INFO() << "[Client ICD] vkResetCommandPool called\n";

    if (!g_command_buffer_state.has_pool(commandPool)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown command pool in vkResetCommandPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkResetCommandPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandPool remote_pool = g_command_buffer_state.get_remote_pool(commandPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote pool missing in vkResetCommandPool\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkResetCommandPool(&g_ring, icd_device->remote_handle, remote_pool, flags);
    if (result == VK_SUCCESS) {
        g_command_buffer_state.reset_pool(commandPool);
        ICD_LOG_INFO() << "[Client ICD] Command pool reset\n";
    } else {
        ICD_LOG_ERROR() << "[Client ICD] vkResetCommandPool failed: " << result << "\n";
    }
    return result;
}

// vkAllocateCommandBuffers - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
    VkDevice device,
    const VkCommandBufferAllocateInfo* pAllocateInfo,
    VkCommandBuffer* pCommandBuffers) {

    ICD_LOG_INFO() << "[Client ICD] vkAllocateCommandBuffers called\n";

    if (!pAllocateInfo || !pCommandBuffers || pAllocateInfo->commandBufferCount == 0) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkAllocateCommandBuffers\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkAllocateCommandBuffers\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandPool command_pool = pAllocateInfo->commandPool;
    if (!g_command_buffer_state.has_pool(command_pool)) {
        ICD_LOG_ERROR() << "[Client ICD] Command pool not tracked in vkAllocateCommandBuffers\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (g_command_buffer_state.get_pool_device(command_pool) != device) {
        ICD_LOG_ERROR() << "[Client ICD] Command pool not owned by device\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandPool remote_pool = g_command_buffer_state.get_remote_pool(command_pool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command pool missing in vkAllocateCommandBuffers\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    uint32_t count = pAllocateInfo->commandBufferCount;
    std::vector<VkCommandBuffer> remote_buffers(count, VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo remote_info = *pAllocateInfo;
    remote_info.commandPool = remote_pool;
    VkResult result = vn_call_vkAllocateCommandBuffers(&g_ring, icd_device->remote_handle, &remote_info, remote_buffers.data());
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkAllocateCommandBuffers failed: " << result << "\n";
        return result;
    }

    uint32_t allocated = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (remote_buffers[i] == VK_NULL_HANDLE) {
            result = VK_ERROR_INITIALIZATION_FAILED;
            break;
        }

        IcdCommandBuffer* icd_cb = new (std::nothrow) IcdCommandBuffer();
        if (!icd_cb) {
            result = VK_ERROR_OUT_OF_HOST_MEMORY;
            break;
        }

        icd_cb->loader_data = nullptr;
        icd_cb->remote_handle = remote_buffers[i];
        icd_cb->parent_device = device;
        icd_cb->parent_pool = command_pool;
        icd_cb->level = pAllocateInfo->level;

        VkCommandBuffer local_handle = icd_command_buffer_to_handle(icd_cb);
        pCommandBuffers[i] = local_handle;
        g_command_buffer_state.add_command_buffer(command_pool, local_handle, remote_buffers[i], pAllocateInfo->level);
        allocated++;
    }

    if (result != VK_SUCCESS) {
        for (uint32_t i = 0; i < allocated; ++i) {
            g_command_buffer_state.remove_command_buffer(pCommandBuffers[i]);
            IcdCommandBuffer* icd_cb = icd_command_buffer_from_handle(pCommandBuffers[i]);
            delete icd_cb;
            pCommandBuffers[i] = VK_NULL_HANDLE;
        }
        vn_async_vkFreeCommandBuffers(&g_ring, icd_device->remote_handle, remote_pool, count, remote_buffers.data());
        return result;
    }

    ICD_LOG_INFO() << "[Client ICD] Allocated " << count << " command buffer(s)\n";
    return VK_SUCCESS;
}

// vkFreeCommandBuffers - Phase 5
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
    VkDevice device,
    VkCommandPool commandPool,
    uint32_t commandBufferCount,
    const VkCommandBuffer* pCommandBuffers) {

    ICD_LOG_INFO() << "[Client ICD] vkFreeCommandBuffers called\n";

    if (commandBufferCount == 0 || !pCommandBuffers) {
        return;
    }

    if (!g_command_buffer_state.has_pool(commandPool)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown command pool in vkFreeCommandBuffers\n";
        return;
    }

    VkCommandPool remote_pool = g_command_buffer_state.get_remote_pool(commandPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command pool missing in vkFreeCommandBuffers\n";
        return;
    }
    std::vector<VkCommandBuffer> remote_handles;
    std::vector<VkCommandBuffer> local_handles;
    remote_handles.reserve(commandBufferCount);
    local_handles.reserve(commandBufferCount);

    for (uint32_t i = 0; i < commandBufferCount; ++i) {
        VkCommandBuffer handle = pCommandBuffers[i];
        if (handle == VK_NULL_HANDLE) {
            continue;
        }
        if (!g_command_buffer_state.has_command_buffer(handle)) {
            ICD_LOG_ERROR() << "[Client ICD] vkFreeCommandBuffers skipping unknown buffer " << handle << "\n";
            continue;
        }
        if (g_command_buffer_state.get_buffer_pool(handle) != commandPool) {
            ICD_LOG_ERROR() << "[Client ICD] vkFreeCommandBuffers: buffer " << handle << " not from pool\n";
            continue;
        }
        VkCommandBuffer remote_cb = get_remote_command_buffer_handle(handle);
        if (remote_cb != VK_NULL_HANDLE) {
            remote_handles.push_back(remote_cb);
        }
        g_command_buffer_state.remove_command_buffer(handle);
        local_handles.push_back(handle);
    }

    for (VkCommandBuffer handle : local_handles) {
        IcdCommandBuffer* icd_cb = icd_command_buffer_from_handle(handle);
        delete icd_cb;
    }

    if (remote_handles.empty()) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkFreeCommandBuffers\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkFreeCommandBuffers\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkFreeCommandBuffers(&g_ring,
                                  icd_device->remote_handle,
                                  remote_pool,
                                  static_cast<uint32_t>(remote_handles.size()),
                                  remote_handles.data());
    ICD_LOG_INFO() << "[Client ICD] Freed " << remote_handles.size() << " command buffer(s)\n";
}

// vkBeginCommandBuffer - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
    VkCommandBuffer commandBuffer,
    const VkCommandBufferBeginInfo* pBeginInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkBeginCommandBuffer called\n";

    if (!pBeginInfo) {
        ICD_LOG_ERROR() << "[Client ICD] pBeginInfo is NULL in vkBeginCommandBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_command_buffer_tracked(commandBuffer, "vkBeginCommandBuffer")) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    CommandBufferLifecycleState state = g_command_buffer_state.get_buffer_state(commandBuffer);
    if (state == CommandBufferLifecycleState::RECORDING) {
        ICD_LOG_ERROR() << "[Client ICD] Command buffer already recording\n";
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (state == CommandBufferLifecycleState::EXECUTABLE &&
        !(pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT)) {
        ICD_LOG_ERROR() << "[Client ICD] vkBeginCommandBuffer requires SIMULTANEOUS_USE when re-recording\n";
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }

    if (state == CommandBufferLifecycleState::INVALID) {
        ICD_LOG_ERROR() << "[Client ICD] Command buffer is invalid\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkBeginCommandBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkBeginCommandBuffer(&g_ring, remote_cb, pBeginInfo);
    if (result == VK_SUCCESS) {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::RECORDING);
        g_command_buffer_state.set_usage_flags(commandBuffer, pBeginInfo->flags);
        ICD_LOG_INFO() << "[Client ICD] Command buffer recording begun\n";
    } else {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::INVALID);
        ICD_LOG_ERROR() << "[Client ICD] vkBeginCommandBuffer failed: " << result << "\n";
    }
    return result;
}

// vkEndCommandBuffer - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    ICD_LOG_INFO() << "[Client ICD] vkEndCommandBuffer called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkEndCommandBuffer")) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkEndCommandBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkEndCommandBuffer(&g_ring, remote_cb);
    if (result == VK_SUCCESS) {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::EXECUTABLE);
        ICD_LOG_INFO() << "[Client ICD] Command buffer recording ended\n";
    } else {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::INVALID);
        ICD_LOG_ERROR() << "[Client ICD] vkEndCommandBuffer failed: " << result << "\n";
    }
    return result;
}

// vkResetCommandBuffer - Phase 5
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(
    VkCommandBuffer commandBuffer,
    VkCommandBufferResetFlags flags) {

    ICD_LOG_INFO() << "[Client ICD] vkResetCommandBuffer called\n";

    if (!ensure_command_buffer_tracked(commandBuffer, "vkResetCommandBuffer")) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandPool pool = g_command_buffer_state.get_buffer_pool(commandBuffer);
    if (pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Unable to determine parent pool in vkResetCommandBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandPoolCreateFlags pool_flags = g_command_buffer_state.get_pool_flags(pool);
    if (!(pool_flags & VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)) {
        ICD_LOG_ERROR() << "[Client ICD] Command pool does not support individual reset\n";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkResetCommandBuffer\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkResetCommandBuffer(&g_ring, remote_cb, flags);
    if (result == VK_SUCCESS) {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::INITIAL);
        g_command_buffer_state.set_usage_flags(commandBuffer, 0);
        ICD_LOG_INFO() << "[Client ICD] Command buffer reset\n";
    } else {
        g_command_buffer_state.set_buffer_state(commandBuffer, CommandBufferLifecycleState::INVALID);
        ICD_LOG_ERROR() << "[Client ICD] vkResetCommandBuffer failed: " << result << "\n";
    }
    return result;
}

static bool validate_buffer_regions(uint32_t count, const void* regions, const char* func_name) {
    if (count == 0 || !regions) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " requires valid regions\n";
        return false;
    }
    return true;
}

static bool ensure_remote_buffer(VkBuffer buffer, VkBuffer* remote, const char* func_name) {
    *remote = g_resource_state.get_remote_buffer(buffer);
    if (*remote == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " buffer not tracked\n";
        return false;
    }
    return true;
}

static bool ensure_remote_image(VkImage image, VkImage* remote, const char* func_name) {
    *remote = g_resource_state.get_remote_image(image);
    if (*remote == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] " << func_name << " image not tracked\n";
        return false;
    }
    return true;
}

// vkCmdCopyBuffer - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkBuffer dstBuffer,
    uint32_t regionCount,
    const VkBufferCopy* pRegions) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyBuffer called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyBuffer") ||
        !validate_buffer_regions(regionCount, pRegions, "vkCmdCopyBuffer")) {
        return;
    }

    VkBuffer remote_src = VK_NULL_HANDLE;
    VkBuffer remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_buffer(srcBuffer, &remote_src, "vkCmdCopyBuffer") ||
        !ensure_remote_buffer(dstBuffer, &remote_dst, "vkCmdCopyBuffer")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdCopyBuffer\n";
        return;
    }
    vn_async_vkCmdCopyBuffer(&g_ring, remote_cb, remote_src, remote_dst, regionCount, pRegions);
    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyBuffer recorded (" << regionCount << " regions)\n";
}

// vkCmdCopyImage - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkImageCopy* pRegions) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyImage called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyImage") ||
        !validate_buffer_regions(regionCount, pRegions, "vkCmdCopyImage")) {
        return;
    }

    VkImage remote_src = VK_NULL_HANDLE;
    VkImage remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_image(srcImage, &remote_src, "vkCmdCopyImage") ||
        !ensure_remote_image(dstImage, &remote_dst, "vkCmdCopyImage")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdCopyImage\n";
        return;
    }
    vn_async_vkCmdCopyImage(&g_ring,
                            remote_cb,
                            remote_src,
                            srcImageLayout,
                            remote_dst,
                            dstImageLayout,
                            regionCount,
                            pRegions);
    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyImage recorded\n";
}

// vkCmdBlitImage - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkImageBlit* pRegions,
    VkFilter filter) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdBlitImage called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdBlitImage") ||
        !validate_buffer_regions(regionCount, pRegions, "vkCmdBlitImage")) {
        return;
    }

    VkImage remote_src = VK_NULL_HANDLE;
    VkImage remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_image(srcImage, &remote_src, "vkCmdBlitImage") ||
        !ensure_remote_image(dstImage, &remote_dst, "vkCmdBlitImage")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdBlitImage\n";
        return;
    }
    vn_async_vkCmdBlitImage(&g_ring,
                            remote_cb,
                            remote_src,
                            srcImageLayout,
                            remote_dst,
                            dstImageLayout,
                            regionCount,
                            pRegions,
                            filter);
    ICD_LOG_INFO() << "[Client ICD] vkCmdBlitImage recorded\n";
}

// vkCmdCopyBufferToImage - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(
    VkCommandBuffer commandBuffer,
    VkBuffer srcBuffer,
    VkImage dstImage,
    VkImageLayout dstImageLayout,
    uint32_t regionCount,
    const VkBufferImageCopy* pRegions) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyBufferToImage called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyBufferToImage") ||
        !validate_buffer_regions(regionCount, pRegions, "vkCmdCopyBufferToImage")) {
        return;
    }

    VkBuffer remote_src = VK_NULL_HANDLE;
    VkImage remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_buffer(srcBuffer, &remote_src, "vkCmdCopyBufferToImage") ||
        !ensure_remote_image(dstImage, &remote_dst, "vkCmdCopyBufferToImage")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdCopyBufferToImage\n";
        return;
    }
    vn_async_vkCmdCopyBufferToImage(&g_ring,
                                    remote_cb,
                                    remote_src,
                                    remote_dst,
                                    dstImageLayout,
                                    regionCount,
                                    pRegions);
    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyBufferToImage recorded\n";
}

// vkCmdCopyImageToBuffer - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(
    VkCommandBuffer commandBuffer,
    VkImage srcImage,
    VkImageLayout srcImageLayout,
    VkBuffer dstBuffer,
    uint32_t regionCount,
    const VkBufferImageCopy* pRegions) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyImageToBuffer called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyImageToBuffer") ||
        !validate_buffer_regions(regionCount, pRegions, "vkCmdCopyImageToBuffer")) {
        return;
    }

    VkImage remote_src = VK_NULL_HANDLE;
    VkBuffer remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_image(srcImage, &remote_src, "vkCmdCopyImageToBuffer") ||
        !ensure_remote_buffer(dstBuffer, &remote_dst, "vkCmdCopyImageToBuffer")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdCopyImageToBuffer\n";
        return;
    }
    vn_async_vkCmdCopyImageToBuffer(&g_ring,
                                    remote_cb,
                                    remote_src,
                                    srcImageLayout,
                                    remote_dst,
                                    regionCount,
                                    pRegions);
    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyImageToBuffer recorded\n";
}

// vkCmdFillBuffer - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdFillBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer dstBuffer,
    VkDeviceSize dstOffset,
    VkDeviceSize size,
    uint32_t data) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdFillBuffer called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdFillBuffer")) {
        return;
    }

    VkBuffer remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_buffer(dstBuffer, &remote_dst, "vkCmdFillBuffer")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdFillBuffer\n";
        return;
    }
    vn_async_vkCmdFillBuffer(&g_ring, remote_cb, remote_dst, dstOffset, size, data);
    ICD_LOG_INFO() << "[Client ICD] vkCmdFillBuffer recorded\n";
}

// vkCmdUpdateBuffer - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdUpdateBuffer(
    VkCommandBuffer commandBuffer,
    VkBuffer dstBuffer,
    VkDeviceSize dstOffset,
    VkDeviceSize dataSize,
    const void* pData) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdUpdateBuffer called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdUpdateBuffer")) {
        return;
    }

    if (!pData || dataSize == 0 || (dataSize % 4) != 0) {
        ICD_LOG_ERROR() << "[Client ICD] vkCmdUpdateBuffer requires 4-byte aligned data\n";
        return;
    }

    VkBuffer remote_dst = VK_NULL_HANDLE;
    if (!ensure_remote_buffer(dstBuffer, &remote_dst, "vkCmdUpdateBuffer")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdUpdateBuffer\n";
        return;
    }
    vn_async_vkCmdUpdateBuffer(&g_ring, remote_cb, remote_dst, dstOffset, dataSize, pData);
    ICD_LOG_INFO() << "[Client ICD] vkCmdUpdateBuffer recorded\n";
}

// vkCmdClearColorImage - Phase 5
VKAPI_ATTR void VKAPI_CALL vkCmdClearColorImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout imageLayout,
    const VkClearColorValue* pColor,
    uint32_t rangeCount,
    const VkImageSubresourceRange* pRanges) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdClearColorImage called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdClearColorImage") ||
        !pColor ||
        !validate_buffer_regions(rangeCount, pRanges, "vkCmdClearColorImage")) {
        return;
    }

    VkImage remote_image = VK_NULL_HANDLE;
    if (!ensure_remote_image(image, &remote_image, "vkCmdClearColorImage")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdClearColorImage\n";
        return;
    }
    vn_async_vkCmdClearColorImage(&g_ring,
                                  remote_cb,
                                  remote_image,
                                  imageLayout,
                                  pColor,
                                  rangeCount,
                                  pRanges);
    ICD_LOG_INFO() << "[Client ICD] vkCmdClearColorImage recorded\n";
}

// vkCmdBeginRenderPass - Phase 10
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(
    VkCommandBuffer commandBuffer,
    const VkRenderPassBeginInfo* pRenderPassBegin,
    VkSubpassContents contents) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdBeginRenderPass called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdBeginRenderPass")) {
        return;
    }

    if (!pRenderPassBegin) {
        ICD_LOG_ERROR() << "[Client ICD] pRenderPassBegin is NULL in vkCmdBeginRenderPass\n";
        return;
    }

    VkRenderPass remote_render_pass =
        g_resource_state.get_remote_render_pass(pRenderPassBegin->renderPass);
    if (remote_render_pass == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Render pass not tracked for vkCmdBeginRenderPass\n";
        return;
    }

    VkFramebuffer remote_framebuffer =
        g_resource_state.get_remote_framebuffer(pRenderPassBegin->framebuffer);
    if (remote_framebuffer == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Framebuffer not tracked for vkCmdBeginRenderPass\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdBeginRenderPass\n";
        return;
    }

    VkRenderPassBeginInfo remote_begin = *pRenderPassBegin;
    remote_begin.renderPass = remote_render_pass;
    remote_begin.framebuffer = remote_framebuffer;

    vn_async_vkCmdBeginRenderPass(&g_ring, remote_cb, &remote_begin, contents);
    ICD_LOG_INFO() << "[Client ICD] vkCmdBeginRenderPass recorded\n";
}

// vkCmdEndRenderPass - Phase 10
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(
    VkCommandBuffer commandBuffer) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdEndRenderPass called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdEndRenderPass")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdEndRenderPass\n";
        return;
    }

    vn_async_vkCmdEndRenderPass(&g_ring, remote_cb);
    ICD_LOG_INFO() << "[Client ICD] vkCmdEndRenderPass recorded\n";
}

// vkCmdBindPipeline - Phase 9
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(
    VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint,
    VkPipeline pipeline) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdBindPipeline called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdBindPipeline")) {
        return;
    }

    if (pipelineBindPoint != VK_PIPELINE_BIND_POINT_COMPUTE &&
        pipelineBindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) {
        ICD_LOG_ERROR() << "[Client ICD] Unsupported bind point in vkCmdBindPipeline\n";
        return;
    }

    VkPipeline remote_pipeline = g_pipeline_state.get_remote_pipeline(pipeline);
    if (remote_pipeline == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline not tracked in vkCmdBindPipeline\n";
        return;
    }

    VkPipelineBindPoint stored_bind_point = g_pipeline_state.get_pipeline_bind_point(pipeline);
    if (stored_bind_point != pipelineBindPoint) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline bind point mismatch\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdBindPipeline\n";
        return;
    }

    vn_async_vkCmdBindPipeline(&g_ring, remote_cb, pipelineBindPoint, remote_pipeline);
    ICD_LOG_INFO() << "[Client ICD] Pipeline bound (bindPoint=" << pipelineBindPoint << ")\n";
}

// vkCmdPushConstants - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout layout,
    VkShaderStageFlags stageFlags,
    uint32_t offset,
    uint32_t size,
    const void* pValues) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdPushConstants called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdPushConstants")) {
        return;
    }

    if (size > 0 && !pValues) {
        ICD_LOG_ERROR() << "[Client ICD] pValues is NULL for non-zero size in vkCmdPushConstants\n";
        return;
    }

    VkPipelineLayout remote_layout = g_pipeline_state.get_remote_pipeline_layout(layout);
    if (remote_layout == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline layout not tracked in vkCmdPushConstants\n";
        return;
    }

    if (!g_pipeline_state.validate_push_constant_range(layout, offset, size, stageFlags)) {
        ICD_LOG_ERROR() << "[Client ICD] Push constant range invalid for pipeline layout\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdPushConstants\n";
        return;
    }

    vn_async_vkCmdPushConstants(&g_ring,
                                remote_cb,
                                remote_layout,
                                stageFlags,
                                offset,
                                size,
                                pValues);
}

// vkCmdDispatchIndirect - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchIndirect(
    VkCommandBuffer commandBuffer,
    VkBuffer buffer,
    VkDeviceSize offset) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdDispatchIndirect called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdDispatchIndirect")) {
        return;
    }

    VkBuffer remote_buffer = g_resource_state.get_remote_buffer(buffer);
    if (remote_buffer == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked in vkCmdDispatchIndirect\n";
        return;
    }

    if (!g_resource_state.buffer_is_bound(buffer)) {
        ICD_LOG_ERROR() << "[Client ICD] Buffer not bound for vkCmdDispatchIndirect\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdDispatchIndirect\n";
        return;
    }

    vn_async_vkCmdDispatchIndirect(&g_ring, remote_cb, remote_buffer, offset);
}

// vkCmdDispatchBase - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBase(
    VkCommandBuffer commandBuffer,
    uint32_t baseGroupX,
    uint32_t baseGroupY,
    uint32_t baseGroupZ,
    uint32_t groupCountX,
    uint32_t groupCountY,
    uint32_t groupCountZ) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdDispatchBase called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdDispatchBase")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdDispatchBase\n";
        return;
    }

    vn_async_vkCmdDispatchBase(&g_ring,
                               remote_cb,
                               baseGroupX,
                               baseGroupY,
                               baseGroupZ,
                               groupCountX,
                               groupCountY,
                               groupCountZ);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDispatchBaseKHR(
    VkCommandBuffer commandBuffer,
    uint32_t baseGroupX,
    uint32_t baseGroupY,
    uint32_t baseGroupZ,
    uint32_t groupCountX,
    uint32_t groupCountY,
    uint32_t groupCountZ) {
    vkCmdDispatchBase(commandBuffer,
                      baseGroupX,
                      baseGroupY,
                      baseGroupZ,
                      groupCountX,
                      groupCountY,
                      groupCountZ);
}

// vkCmdResetQueryPool - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkCmdResetQueryPool(
    VkCommandBuffer commandBuffer,
    VkQueryPool queryPool,
    uint32_t firstQuery,
    uint32_t queryCount) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdResetQueryPool called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdResetQueryPool")) {
        return;
    }

    if (!g_query_state.validate_query_range(queryPool, firstQuery, queryCount)) {
        ICD_LOG_ERROR() << "[Client ICD] Query range invalid in vkCmdResetQueryPool\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkQueryPool remote_pool = g_query_state.get_remote_query_pool(queryPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Query pool not tracked in vkCmdResetQueryPool\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdResetQueryPool\n";
        return;
    }

    vn_async_vkCmdResetQueryPool(&g_ring, remote_cb, remote_pool, firstQuery, queryCount);
}

// vkCmdBeginQuery - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkCmdBeginQuery(
    VkCommandBuffer commandBuffer,
    VkQueryPool queryPool,
    uint32_t query,
    VkQueryControlFlags flags) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdBeginQuery called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdBeginQuery")) {
        return;
    }

    if (!g_query_state.validate_query_range(queryPool, query, 1)) {
        ICD_LOG_ERROR() << "[Client ICD] Query out of range in vkCmdBeginQuery\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkQueryPool remote_pool = g_query_state.get_remote_query_pool(queryPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Query pool not tracked in vkCmdBeginQuery\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdBeginQuery\n";
        return;
    }

    vn_async_vkCmdBeginQuery(&g_ring, remote_cb, remote_pool, query, flags);
}

// vkCmdEndQuery - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkCmdEndQuery(
    VkCommandBuffer commandBuffer,
    VkQueryPool queryPool,
    uint32_t query) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdEndQuery called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdEndQuery")) {
        return;
    }

    if (!g_query_state.validate_query_range(queryPool, query, 1)) {
        ICD_LOG_ERROR() << "[Client ICD] Query out of range in vkCmdEndQuery\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkQueryPool remote_pool = g_query_state.get_remote_query_pool(queryPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Query pool not tracked in vkCmdEndQuery\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdEndQuery\n";
        return;
    }

    vn_async_vkCmdEndQuery(&g_ring, remote_cb, remote_pool, query);
}

// vkCmdWriteTimestamp - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp(
    VkCommandBuffer commandBuffer,
    VkPipelineStageFlagBits pipelineStage,
    VkQueryPool queryPool,
    uint32_t query) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdWriteTimestamp called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdWriteTimestamp")) {
        return;
    }

    if (!g_query_state.validate_query_range(queryPool, query, 1)) {
        ICD_LOG_ERROR() << "[Client ICD] Query out of range in vkCmdWriteTimestamp\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkQueryPool remote_pool = g_query_state.get_remote_query_pool(queryPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Query pool not tracked in vkCmdWriteTimestamp\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdWriteTimestamp\n";
        return;
    }

    vn_async_vkCmdWriteTimestamp(&g_ring, remote_cb, pipelineStage, remote_pool, query);
}

// vkCmdCopyQueryPoolResults - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkCmdCopyQueryPoolResults(
    VkCommandBuffer commandBuffer,
    VkQueryPool queryPool,
    uint32_t firstQuery,
    uint32_t queryCount,
    VkBuffer dstBuffer,
    VkDeviceSize dstOffset,
    VkDeviceSize stride,
    VkQueryResultFlags flags) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyQueryPoolResults called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyQueryPoolResults")) {
        return;
    }

    if (!g_query_state.validate_query_range(queryPool, firstQuery, queryCount)) {
        ICD_LOG_ERROR() << "[Client ICD] Query range invalid in vkCmdCopyQueryPoolResults\n";
        return;
    }

    VkBuffer remote_buffer = g_resource_state.get_remote_buffer(dstBuffer);
    if (remote_buffer == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Destination buffer not tracked in vkCmdCopyQueryPoolResults\n";
        return;
    }

    if (!g_resource_state.buffer_is_bound(dstBuffer)) {
        ICD_LOG_ERROR() << "[Client ICD] Destination buffer not bound in vkCmdCopyQueryPoolResults\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkQueryPool remote_pool = g_query_state.get_remote_query_pool(queryPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Query pool not tracked in vkCmdCopyQueryPoolResults\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdCopyQueryPoolResults\n";
        return;
    }

    vn_async_vkCmdCopyQueryPoolResults(&g_ring,
                                       remote_cb,
                                       remote_pool,
                                       firstQuery,
                                       queryCount,
                                       remote_buffer,
                                       dstOffset,
                                       stride,
                                       flags);
}

// vkCmdSetEvent - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent(
    VkCommandBuffer commandBuffer,
    VkEvent event,
    VkPipelineStageFlags stageMask) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetEvent called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetEvent")) {
        return;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkCmdSetEvent\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetEvent\n";
        return;
    }

    vn_async_vkCmdSetEvent(&g_ring, remote_cb, remote_event, stageMask);
}

// vkCmdResetEvent - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent(
    VkCommandBuffer commandBuffer,
    VkEvent event,
    VkPipelineStageFlags stageMask) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdResetEvent called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdResetEvent")) {
        return;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkCmdResetEvent\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdResetEvent\n";
        return;
    }

    vn_async_vkCmdResetEvent(&g_ring, remote_cb, remote_event, stageMask);
}

// vkCmdWaitEvents - Phase 9_3
VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents(
    VkCommandBuffer commandBuffer,
    uint32_t eventCount,
    const VkEvent* pEvents,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    uint32_t memoryBarrierCount,
    const VkMemoryBarrier* pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount,
    const VkImageMemoryBarrier* pImageMemoryBarriers) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdWaitEvents called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdWaitEvents")) {
        return;
    }

    if (eventCount == 0 || !pEvents) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid event list in vkCmdWaitEvents\n";
        return;
    }

    if ((memoryBarrierCount > 0 && !pMemoryBarriers) ||
        (bufferMemoryBarrierCount > 0 && !pBufferMemoryBarriers) ||
        (imageMemoryBarrierCount > 0 && !pImageMemoryBarriers)) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid barrier arrays in vkCmdWaitEvents\n";
        return;
    }

    std::vector<VkEvent> remote_events(eventCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < eventCount; ++i) {
        remote_events[i] = g_sync_state.get_remote_event(pEvents[i]);
        if (remote_events[i] == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkCmdWaitEvents\n";
            return;
        }
    }

    std::vector<VkBufferMemoryBarrier> buffer_barriers(bufferMemoryBarrierCount);
    for (uint32_t i = 0; i < bufferMemoryBarrierCount; ++i) {
        buffer_barriers[i] = pBufferMemoryBarriers[i];
        buffer_barriers[i].buffer =
            g_resource_state.get_remote_buffer(pBufferMemoryBarriers[i].buffer);
        if (buffer_barriers[i].buffer == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked in vkCmdWaitEvents\n";
            return;
        }
    }

    std::vector<VkImageMemoryBarrier> image_barriers(imageMemoryBarrierCount);
    for (uint32_t i = 0; i < imageMemoryBarrierCount; ++i) {
        image_barriers[i] = pImageMemoryBarriers[i];
        image_barriers[i].image =
            g_resource_state.get_remote_image(pImageMemoryBarriers[i].image);
        if (image_barriers[i].image == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Image not tracked in vkCmdWaitEvents\n";
            return;
        }
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdWaitEvents\n";
        return;
    }

    vn_async_vkCmdWaitEvents(&g_ring,
                             remote_cb,
                             eventCount,
                             remote_events.data(),
                             srcStageMask,
                             dstStageMask,
                             memoryBarrierCount,
                             pMemoryBarriers,
                             bufferMemoryBarrierCount,
                             buffer_barriers.empty() ? nullptr : buffer_barriers.data(),
                             imageMemoryBarrierCount,
                             image_barriers.empty() ? nullptr : image_barriers.data());
}

// vkCmdBindVertexBuffers - Phase 10
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(
    VkCommandBuffer commandBuffer,
    uint32_t firstBinding,
    uint32_t bindingCount,
    const VkBuffer* pBuffers,
    const VkDeviceSize* pOffsets) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdBindVertexBuffers called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdBindVertexBuffers")) {
        return;
    }

    if (bindingCount == 0) {
        return;
    }

    if (!pBuffers || !pOffsets) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid buffers or offsets for vkCmdBindVertexBuffers\n";
        return;
    }

    std::vector<VkBuffer> remote_buffers(bindingCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < bindingCount; ++i) {
        remote_buffers[i] = g_resource_state.get_remote_buffer(pBuffers[i]);
        if (remote_buffers[i] == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked for vkCmdBindVertexBuffers\n";
            return;
        }
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdBindVertexBuffers\n";
        return;
    }

    vn_async_vkCmdBindVertexBuffers(&g_ring,
                                    remote_cb,
                                    firstBinding,
                                    bindingCount,
                                    remote_buffers.data(),
                                    pOffsets);
    ICD_LOG_INFO() << "[Client ICD] vkCmdBindVertexBuffers recorded\n";
}

// vkCmdSetViewport - Phase 10
VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(
    VkCommandBuffer commandBuffer,
    uint32_t firstViewport,
    uint32_t viewportCount,
    const VkViewport* pViewports) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetViewport called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetViewport")) {
        return;
    }

    if (viewportCount == 0 || !pViewports) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid viewport parameters in vkCmdSetViewport\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetViewport\n";
        return;
    }

    vn_async_vkCmdSetViewport(&g_ring, remote_cb, firstViewport, viewportCount, pViewports);
    ICD_LOG_INFO() << "[Client ICD] vkCmdSetViewport recorded\n";
}

// vkCmdSetScissor - Phase 10
VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(
    VkCommandBuffer commandBuffer,
    uint32_t firstScissor,
    uint32_t scissorCount,
    const VkRect2D* pScissors) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetScissor called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetScissor")) {
        return;
    }

    if (scissorCount == 0 || !pScissors) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid scissor parameters in vkCmdSetScissor\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetScissor\n";
        return;
    }

    vn_async_vkCmdSetScissor(&g_ring, remote_cb, firstScissor, scissorCount, pScissors);
    ICD_LOG_INFO() << "[Client ICD] vkCmdSetScissor recorded\n";
}

// vkCmdDraw - Phase 10
VKAPI_ATTR void VKAPI_CALL vkCmdDraw(
    VkCommandBuffer commandBuffer,
    uint32_t vertexCount,
    uint32_t instanceCount,
    uint32_t firstVertex,
    uint32_t firstInstance) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdDraw called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdDraw")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdDraw\n";
        return;
    }

    vn_async_vkCmdDraw(&g_ring,
                       remote_cb,
                       vertexCount,
                       instanceCount,
                       firstVertex,
                       firstInstance);
    ICD_LOG_INFO() << "[Client ICD] vkCmdDraw recorded\n";
}

// vkCmdBindDescriptorSets - Phase 9
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer,
    VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout layout,
    uint32_t firstSet,
    uint32_t descriptorSetCount,
    const VkDescriptorSet* pDescriptorSets,
    uint32_t dynamicOffsetCount,
    const uint32_t* pDynamicOffsets) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdBindDescriptorSets called (count=" << descriptorSetCount << ")\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdBindDescriptorSets")) {
        return;
    }

    if (pipelineBindPoint != VK_PIPELINE_BIND_POINT_COMPUTE) {
        ICD_LOG_ERROR() << "[Client ICD] Only compute bind point supported in vkCmdBindDescriptorSets\n";
        return;
    }

    if (descriptorSetCount > 0 && !pDescriptorSets) {
        ICD_LOG_ERROR() << "[Client ICD] Descriptor set array missing in vkCmdBindDescriptorSets\n";
        return;
    }

    VkPipelineLayout remote_layout = g_pipeline_state.get_remote_pipeline_layout(layout);
    if (remote_layout == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline layout not tracked in vkCmdBindDescriptorSets\n";
        return;
    }

    std::vector<VkDescriptorSet> remote_sets(descriptorSetCount);
    for (uint32_t i = 0; i < descriptorSetCount; ++i) {
        remote_sets[i] = g_pipeline_state.get_remote_descriptor_set(pDescriptorSets[i]);
        if (remote_sets[i] == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Descriptor set not tracked in vkCmdBindDescriptorSets\n";
            return;
        }
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdBindDescriptorSets\n";
        return;
    }

    vn_async_vkCmdBindDescriptorSets(&g_ring,
                                     remote_cb,
                                     pipelineBindPoint,
                                     remote_layout,
                                     firstSet,
                                     descriptorSetCount,
                                     remote_sets.empty() ? nullptr : remote_sets.data(),
                                     dynamicOffsetCount,
                                     pDynamicOffsets);
    ICD_LOG_INFO() << "[Client ICD] Descriptor sets bound\n";
}

// vkCmdDispatch - Phase 9
VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(
    VkCommandBuffer commandBuffer,
    uint32_t groupCountX,
    uint32_t groupCountY,
    uint32_t groupCountZ) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdDispatch called ("
              << groupCountX << ", " << groupCountY << ", " << groupCountZ << ")\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdDispatch")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdDispatch\n";
        return;
    }

    vn_async_vkCmdDispatch(&g_ring, remote_cb, groupCountX, groupCountY, groupCountZ);
    ICD_LOG_INFO() << "[Client ICD] Dispatch recorded\n";
}

// vkCmdPipelineBarrier - Phase 9
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    VkDependencyFlags dependencyFlags,
    uint32_t memoryBarrierCount,
    const VkMemoryBarrier* pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount,
    const VkImageMemoryBarrier* pImageMemoryBarriers) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdPipelineBarrier called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdPipelineBarrier")) {
        return;
    }

    if ((memoryBarrierCount > 0 && !pMemoryBarriers) ||
        (bufferMemoryBarrierCount > 0 && !pBufferMemoryBarriers) ||
        (imageMemoryBarrierCount > 0 && !pImageMemoryBarriers)) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid barrier arrays\n";
        return;
    }

    std::vector<VkBufferMemoryBarrier> buffer_barriers(bufferMemoryBarrierCount);
    for (uint32_t i = 0; i < bufferMemoryBarrierCount; ++i) {
        buffer_barriers[i] = pBufferMemoryBarriers[i];
        buffer_barriers[i].buffer =
            g_resource_state.get_remote_buffer(pBufferMemoryBarriers[i].buffer);
        if (buffer_barriers[i].buffer == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked in vkCmdPipelineBarrier\n";
            return;
        }
    }

    std::vector<VkImageMemoryBarrier> image_barriers(imageMemoryBarrierCount);
    for (uint32_t i = 0; i < imageMemoryBarrierCount; ++i) {
        image_barriers[i] = pImageMemoryBarriers[i];
        image_barriers[i].image =
            g_resource_state.get_remote_image(pImageMemoryBarriers[i].image);
        if (image_barriers[i].image == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Image not tracked in vkCmdPipelineBarrier\n";
            return;
        }
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdPipelineBarrier\n";
        return;
    }

    vn_async_vkCmdPipelineBarrier(&g_ring,
                                  remote_cb,
                                  srcStageMask,
                                  dstStageMask,
                                  dependencyFlags,
                                  memoryBarrierCount,
                                  pMemoryBarriers,
                                  bufferMemoryBarrierCount,
                                  buffer_barriers.empty() ? nullptr : buffer_barriers.data(),
                                  imageMemoryBarrierCount,
                                  image_barriers.empty() ? nullptr : image_barriers.data());
    ICD_LOG_INFO() << "[Client ICD] Pipeline barrier recorded\n";
}

// Event synchronization - Phase 9_3
VKAPI_ATTR VkResult VKAPI_CALL vkCreateEvent(
    VkDevice device,
    const VkEventCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkEvent* pEvent) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateEvent called\n";

    if (!pCreateInfo || !pEvent) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkEvent remote_event = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateEvent(&g_ring,
                                            icd_device->remote_handle,
                                            pCreateInfo,
                                            pAllocator,
                                            &remote_event);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateEvent failed: " << result << "\n";
        return result;
    }

    VkEvent local_event = g_handle_allocator.allocate<VkEvent>();
    g_sync_state.add_event(device, local_event, remote_event, false);
    *pEvent = local_event;
    ICD_LOG_INFO() << "[Client ICD] Event created (local=" << local_event << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyEvent(
    VkDevice device,
    VkEvent event,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyEvent called\n";

    if (event == VK_NULL_HANDLE) {
        return;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    g_sync_state.remove_event(event);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyEvent\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyEvent\n";
        return;
    }

    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkDestroyEvent\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyEvent(&g_ring, icd_device->remote_handle, remote_event, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetEventStatus(VkDevice device, VkEvent event) {
    ICD_LOG_INFO() << "[Client ICD] vkGetEventStatus called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetEventStatus\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkGetEventStatus\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkGetEventStatus(&g_ring, icd_device->remote_handle, remote_event);
    if (result == VK_EVENT_SET) {
        g_sync_state.set_event_signaled(event, true);
    } else if (result == VK_EVENT_RESET) {
        g_sync_state.set_event_signaled(event, false);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkSetEvent(VkDevice device, VkEvent event) {
    ICD_LOG_INFO() << "[Client ICD] vkSetEvent called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkSetEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkSetEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkSetEvent(&g_ring, icd_device->remote_handle, remote_event);
    if (result == VK_SUCCESS) {
        g_sync_state.set_event_signaled(event, true);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetEvent(VkDevice device, VkEvent event) {
    ICD_LOG_INFO() << "[Client ICD] vkResetEvent called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkResetEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkResetEvent\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkResetEvent(&g_ring, icd_device->remote_handle, remote_event);
    if (result == VK_SUCCESS) {
        g_sync_state.set_event_signaled(event, false);
    }
    return result;
}

// Synchronization objects - Phase 6
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(
    VkDevice device,
    const VkFenceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkFence* pFence) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateFence called\n";

    if (!pCreateInfo || !pFence) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateFence\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateFence\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkFence remote_fence = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateFence(&g_ring, icd_device->remote_handle, pCreateInfo, pAllocator, &remote_fence);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateFence failed: " << result << "\n";
        return result;
    }

    VkFence local_fence = g_handle_allocator.allocate<VkFence>();
    g_sync_state.add_fence(device, local_fence, remote_fence, (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0);
    *pFence = local_fence;
    ICD_LOG_INFO() << "[Client ICD] Fence created (local=" << *pFence << ", remote=" << remote_fence << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyFence(
    VkDevice device,
    VkFence fence,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroyFence called\n";
    if (fence == VK_NULL_HANDLE) {
        return;
    }

    VkFence remote = g_sync_state.get_remote_fence(fence);
    g_sync_state.remove_fence(fence);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroyFence\n";
        return;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroyFence\n";
        return;
    }
    if (remote == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote fence missing in vkDestroyFence\n";
        return;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroyFence(&g_ring, icd_device->remote_handle, remote, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice device, VkFence fence) {
    ICD_LOG_INFO() << "[Client ICD] vkGetFenceStatus called\n";
    if (!g_sync_state.has_fence(fence)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown fence in vkGetFenceStatus\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetFenceStatus\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkFence remote = g_sync_state.get_remote_fence(fence);
    if (remote == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkGetFenceStatus(&g_ring, icd_device->remote_handle, remote);
    if (result == VK_SUCCESS) {
        g_sync_state.set_fence_signaled(fence, true);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence* pFences) {

    ICD_LOG_INFO() << "[Client ICD] vkResetFences called\n";

    if (!fenceCount || !pFences) {
        return VK_SUCCESS;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkResetFences\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkFence> remote_fences(fenceCount);
    for (uint32_t i = 0; i < fenceCount; ++i) {
        VkFence remote = g_sync_state.get_remote_fence(pFences[i]);
        if (remote == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] vkResetFences: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_fences[i] = remote;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkResetFences(&g_ring,
                                            icd_device->remote_handle,
                                            fenceCount,
                                            remote_fences.data());
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < fenceCount; ++i) {
            g_sync_state.set_fence_signaled(pFences[i], false);
        }
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(
    VkDevice device,
    uint32_t fenceCount,
    const VkFence* pFences,
    VkBool32 waitAll,
    uint64_t timeout) {

    ICD_LOG_INFO() << "[Client ICD] vkWaitForFences called\n";

    if (!fenceCount || !pFences) {
        return VK_SUCCESS;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkWaitForFences\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkFence> remote_fences(fenceCount);
    for (uint32_t i = 0; i < fenceCount; ++i) {
        VkFence remote = g_sync_state.get_remote_fence(pFences[i]);
        if (remote == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] vkWaitForFences: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_fences[i] = remote;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkWaitForFences(&g_ring,
                                              icd_device->remote_handle,
                                              fenceCount,
                                              remote_fences.data(),
                                              waitAll,
                                              timeout);
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < fenceCount; ++i) {
            g_sync_state.set_fence_signaled(pFences[i], true);
        }
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(
    VkDevice device,
    const VkSemaphoreCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkSemaphore* pSemaphore) {

    ICD_LOG_INFO() << "[Client ICD] vkCreateSemaphore called\n";

    if (!pCreateInfo || !pSemaphore) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCreateSemaphore\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCreateSemaphore\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkSemaphore remote_semaphore = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateSemaphore(&g_ring,
                                                icd_device->remote_handle,
                                                pCreateInfo,
                                                pAllocator,
                                                &remote_semaphore);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateSemaphore failed: " << result << "\n";
        return result;
    }

    const VkSemaphoreTypeCreateInfo* type_info = find_semaphore_type_info(pCreateInfo);
    VkSemaphoreType type = type_info ? type_info->semaphoreType : VK_SEMAPHORE_TYPE_BINARY;
    uint64_t initial_value = type_info ? type_info->initialValue : 0;

    VkSemaphore local_semaphore = g_handle_allocator.allocate<VkSemaphore>();
    g_sync_state.add_semaphore(device,
                               local_semaphore,
                               remote_semaphore,
                               type,
                               false,
                               initial_value);
    *pSemaphore = local_semaphore;
    ICD_LOG_INFO() << "[Client ICD] Semaphore created (local=" << *pSemaphore
              << ", remote=" << remote_semaphore
              << ", type=" << (type == VK_SEMAPHORE_TYPE_TIMELINE ? "timeline" : "binary") << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(
    VkDevice device,
    VkSemaphore semaphore,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkDestroySemaphore called\n";
    if (semaphore == VK_NULL_HANDLE) {
        return;
    }

    VkSemaphore remote = g_sync_state.get_remote_semaphore(semaphore);
    g_sync_state.remove_semaphore(semaphore);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkDestroySemaphore\n";
        return;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDestroySemaphore\n";
        return;
    }
    if (remote == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote semaphore missing in vkDestroySemaphore\n";
        return;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_async_vkDestroySemaphore(&g_ring, icd_device->remote_handle, remote, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetSemaphoreCounterValue(
    VkDevice device,
    VkSemaphore semaphore,
    uint64_t* pValue) {

    ICD_LOG_INFO() << "[Client ICD] vkGetSemaphoreCounterValue called\n";

    if (!pValue) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_sync_state.has_semaphore(semaphore)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown semaphore in vkGetSemaphoreCounterValue\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (g_sync_state.get_semaphore_type(semaphore) != VK_SEMAPHORE_TYPE_TIMELINE) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetSemaphoreCounterValue\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkSemaphore remote = g_sync_state.get_remote_semaphore(semaphore);
    if (remote == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkGetSemaphoreCounterValue(&g_ring,
                                                         icd_device->remote_handle,
                                                         remote,
                                                         pValue);
    if (result == VK_SUCCESS) {
        g_sync_state.set_timeline_value(semaphore, *pValue);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkSignalSemaphore(
    VkDevice device,
    const VkSemaphoreSignalInfo* pSignalInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkSignalSemaphore called\n";

    if (!pSignalInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkSemaphore semaphore = pSignalInfo->semaphore;
    if (!g_sync_state.has_semaphore(semaphore)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown semaphore in vkSignalSemaphore\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (g_sync_state.get_semaphore_type(semaphore) != VK_SEMAPHORE_TYPE_TIMELINE) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkSignalSemaphore\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkSemaphore remote = g_sync_state.get_remote_semaphore(semaphore);
    if (remote == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkSemaphoreSignalInfo remote_info = *pSignalInfo;
    remote_info.semaphore = remote;
    VkResult result = vn_call_vkSignalSemaphore(&g_ring, icd_device->remote_handle, &remote_info);
    if (result == VK_SUCCESS) {
        g_sync_state.set_timeline_value(semaphore, pSignalInfo->value);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkWaitSemaphores(
    VkDevice device,
    const VkSemaphoreWaitInfo* pWaitInfo,
    uint64_t timeout) {

    ICD_LOG_INFO() << "[Client ICD] vkWaitSemaphores called\n";

    if (!pWaitInfo || pWaitInfo->semaphoreCount == 0 ||
        !pWaitInfo->pSemaphores || !pWaitInfo->pValues) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkWaitSemaphores\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkSemaphore> remote_handles(pWaitInfo->semaphoreCount);
    for (uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
        VkSemaphore sem = pWaitInfo->pSemaphores[i];
        if (!g_sync_state.has_semaphore(sem) ||
            g_sync_state.get_semaphore_type(sem) != VK_SEMAPHORE_TYPE_TIMELINE) {
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        VkSemaphore remote = g_sync_state.get_remote_semaphore(sem);
        if (remote == VK_NULL_HANDLE) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        remote_handles[i] = remote;
    }

    VkSemaphoreWaitInfo remote_info = *pWaitInfo;
    remote_info.pSemaphores = remote_handles.data();

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkWaitSemaphores(&g_ring, icd_device->remote_handle, &remote_info, timeout);
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
            g_sync_state.set_timeline_value(pWaitInfo->pSemaphores[i], pWaitInfo->pValues[i]);
        }
    }
    return result;
}

// Queue submission - Phase 6
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
    VkQueue queue,
    uint32_t submitCount,
    const VkSubmitInfo* pSubmits,
    VkFence fence) {

    ICD_LOG_INFO() << "[Client ICD] vkQueueSubmit called (submitCount=" << submitCount << ")\n";

    if (submitCount > 0 && !pSubmits) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkQueue remote_queue = VK_NULL_HANDLE;
    if (!ensure_queue_tracked(queue, &remote_queue)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkFence remote_fence = VK_NULL_HANDLE;
    if (fence != VK_NULL_HANDLE) {
        remote_fence = g_sync_state.get_remote_fence(fence);
        if (remote_fence == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: fence not tracked\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    struct SubmitStorage {
        std::vector<VkSemaphore> wait_local;
        std::vector<VkSemaphore> signal_local;
        std::vector<VkSemaphore> wait_remote;
        std::vector<VkPipelineStageFlags> wait_stages;
        std::vector<VkCommandBuffer> remote_cbs;
        std::vector<VkSemaphore> signal_remote;
        std::vector<uint64_t> wait_values;
        std::vector<uint64_t> signal_values;
        VkTimelineSemaphoreSubmitInfo timeline_info{};
        bool has_timeline = false;
    };

    std::vector<VkSubmitInfo> remote_submits;
    std::vector<SubmitStorage> storage;
    if (submitCount > 0) {
        remote_submits.resize(submitCount);
        storage.resize(submitCount);
    }

    for (uint32_t i = 0; i < submitCount; ++i) {
        const VkSubmitInfo& src = pSubmits[i];
        VkSubmitInfo& dst = remote_submits[i];
        SubmitStorage& slot = storage[i];
        dst = src;

        if (src.waitSemaphoreCount > 0) {
            if (!src.pWaitSemaphores || !src.pWaitDstStageMask) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: invalid wait semaphore arrays\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.wait_local.assign(src.pWaitSemaphores, src.pWaitSemaphores + src.waitSemaphoreCount);
            slot.wait_remote.resize(src.waitSemaphoreCount);
            slot.wait_stages.assign(src.pWaitDstStageMask, src.pWaitDstStageMask + src.waitSemaphoreCount);
            for (uint32_t j = 0; j < src.waitSemaphoreCount; ++j) {
                VkSemaphore wait_sem = src.pWaitSemaphores[j];
                if (!g_sync_state.has_semaphore(wait_sem)) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: wait semaphore not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.wait_remote[j] = g_sync_state.get_remote_semaphore(wait_sem);
                if (slot.wait_remote[j] == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
            }
            dst.pWaitSemaphores = slot.wait_remote.data();
            dst.pWaitDstStageMask = slot.wait_stages.data();
        } else {
            dst.pWaitSemaphores = nullptr;
            dst.pWaitDstStageMask = nullptr;
        }

        if (src.commandBufferCount > 0) {
            if (!src.pCommandBuffers) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: command buffers missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.remote_cbs.resize(src.commandBufferCount);
            for (uint32_t j = 0; j < src.commandBufferCount; ++j) {
                VkCommandBuffer local_cb = src.pCommandBuffers[j];
                if (!g_command_buffer_state.has_command_buffer(local_cb)) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: command buffer not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                if (g_command_buffer_state.get_buffer_state(local_cb) != CommandBufferLifecycleState::EXECUTABLE) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: command buffer not executable\n";
                    return VK_ERROR_VALIDATION_FAILED_EXT;
                }
                VkCommandBuffer remote_cb = get_remote_command_buffer_handle(local_cb);
                if (remote_cb == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.remote_cbs[j] = remote_cb;
            }
            dst.pCommandBuffers = slot.remote_cbs.data();
        } else {
            dst.pCommandBuffers = nullptr;
        }

        if (src.signalSemaphoreCount > 0) {
            if (!src.pSignalSemaphores) {
                ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: signal semaphores missing\n";
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            slot.signal_local.assign(src.pSignalSemaphores, src.pSignalSemaphores + src.signalSemaphoreCount);
            slot.signal_remote.resize(src.signalSemaphoreCount);
            for (uint32_t j = 0; j < src.signalSemaphoreCount; ++j) {
                VkSemaphore signal_sem = src.pSignalSemaphores[j];
                if (!g_sync_state.has_semaphore(signal_sem)) {
                    ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit: signal semaphore not tracked\n";
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                slot.signal_remote[j] = g_sync_state.get_remote_semaphore(signal_sem);
                if (slot.signal_remote[j] == VK_NULL_HANDLE) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
            }
            dst.pSignalSemaphores = slot.signal_remote.data();
        } else {
            dst.pSignalSemaphores = nullptr;
        }

        const VkTimelineSemaphoreSubmitInfo* timeline = find_timeline_submit_info(src.pNext);
        if (timeline) {
            slot.timeline_info = *timeline;
            if (timeline->waitSemaphoreValueCount) {
                slot.wait_values.assign(timeline->pWaitSemaphoreValues,
                                        timeline->pWaitSemaphoreValues + timeline->waitSemaphoreValueCount);
                slot.timeline_info.pWaitSemaphoreValues = slot.wait_values.data();
            }
            if (timeline->signalSemaphoreValueCount) {
                slot.signal_values.assign(timeline->pSignalSemaphoreValues,
                                          timeline->pSignalSemaphoreValues + timeline->signalSemaphoreValueCount);
                slot.timeline_info.pSignalSemaphoreValues = slot.signal_values.data();
            }
            dst.pNext = &slot.timeline_info;
            slot.has_timeline = true;
        } else {
            dst.pNext = nullptr;
            slot.has_timeline = false;
        }
    }

    const VkSubmitInfo* submit_ptr = submitCount > 0 ? remote_submits.data() : nullptr;
    VkResult result = vn_call_vkQueueSubmit(&g_ring, remote_queue, submitCount, submit_ptr, remote_fence);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkQueueSubmit failed: " << result << "\n";
        return result;
    }

    if (fence != VK_NULL_HANDLE) {
        g_sync_state.set_fence_signaled(fence, true);
    }
    for (uint32_t i = 0; i < submitCount; ++i) {
        const SubmitStorage& slot = storage[i];
        for (VkSemaphore wait_sem : slot.wait_local) {
            if (g_sync_state.get_semaphore_type(wait_sem) == VK_SEMAPHORE_TYPE_BINARY) {
                g_sync_state.set_binary_semaphore_signaled(wait_sem, false);
            }
        }
        if (slot.has_timeline && !slot.wait_values.empty()) {
            for (size_t j = 0; j < slot.wait_local.size() && j < slot.wait_values.size(); ++j) {
                if (g_sync_state.get_semaphore_type(slot.wait_local[j]) == VK_SEMAPHORE_TYPE_TIMELINE) {
                    g_sync_state.set_timeline_value(slot.wait_local[j], slot.wait_values[j]);
                }
            }
        }
        for (size_t j = 0; j < slot.signal_local.size(); ++j) {
            VkSemaphore signal_sem = slot.signal_local[j];
            if (g_sync_state.get_semaphore_type(signal_sem) == VK_SEMAPHORE_TYPE_BINARY) {
                g_sync_state.set_binary_semaphore_signaled(signal_sem, true);
            } else if (slot.has_timeline && j < slot.signal_values.size()) {
                g_sync_state.set_timeline_value(signal_sem, slot.signal_values[j]);
            }
        }
    }

    ICD_LOG_INFO() << "[Client ICD] vkQueueSubmit completed\n";
    return VK_SUCCESS;
}

// vkQueuePresentKHR - Phase 10
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(
    VkQueue queue,
    const VkPresentInfoKHR* pPresentInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkQueuePresentKHR called\n";

    if (!pPresentInfo || pPresentInfo->swapchainCount == 0 || !pPresentInfo->pSwapchains) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkQueue remote_queue = VK_NULL_HANDLE;
    if (!ensure_queue_tracked(queue, &remote_queue)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    (void)remote_queue;

    VkResult final_result = VK_SUCCESS;
    for (uint32_t i = 0; i < pPresentInfo->swapchainCount; ++i) {
        VkSwapchainKHR swapchain = pPresentInfo->pSwapchains[i];
        uint32_t image_index = pPresentInfo->pImageIndices ? pPresentInfo->pImageIndices[i] : 0;
        uint32_t remote_id = g_swapchain_state.get_remote_id(swapchain);
        if (remote_id == 0) {
            ICD_LOG_ERROR() << "[Client ICD] Unknown swapchain in queue present\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        VenusSwapchainPresentRequest request = {};
        request.command = VENUS_PLUS_CMD_PRESENT;
        request.swapchain_id = remote_id;
        request.image_index = image_index;

        std::vector<uint8_t> reply_buffer;
        if (!send_swapchain_command(&request, sizeof(request), &reply_buffer)) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        if (reply_buffer.size() < sizeof(VenusSwapchainPresentReply)) {
            ICD_LOG_ERROR() << "[Client ICD] Invalid present reply size\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        const auto* reply = reinterpret_cast<const VenusSwapchainPresentReply*>(reply_buffer.data());
        if (reply->result != VK_SUCCESS) {
            final_result = reply->result;
            continue;
        }

        size_t payload_size = reply_buffer.size() - sizeof(VenusSwapchainPresentReply);
        if (payload_size < reply->frame.payload_size) {
            ICD_LOG_ERROR() << "[Client ICD] Present payload truncated\n";
            final_result = VK_ERROR_INITIALIZATION_FAILED;
            continue;
        }

        const uint8_t* payload = reply_buffer.data() + sizeof(VenusSwapchainPresentReply);
        auto wsi = g_swapchain_state.get_wsi(swapchain);
        if (wsi) {
            wsi->handle_frame(reply->frame, payload);
        }
    }

    return final_result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue) {
    ICD_LOG_INFO() << "[Client ICD] vkQueueWaitIdle called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkQueue remote_queue = VK_NULL_HANDLE;
    if (!ensure_queue_tracked(queue, &remote_queue)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = vn_call_vkQueueWaitIdle(&g_ring, remote_queue);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkQueueWaitIdle failed: " << result << "\n";
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) {
    ICD_LOG_INFO() << "[Client ICD] vkDeviceWaitIdle called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDeviceWaitIdle\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkDeviceWaitIdle(&g_ring, icd_device->remote_handle);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkDeviceWaitIdle failed: " << result << "\n";
    }
    return result;
}

} // extern "C"
