// Wsi Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"

// Helper functions (must be outside extern "C" to match header declarations)

bool send_swapchain_command(const void* request,
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

extern "C" {

// Vulkan function implementations

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

} // extern "C"
