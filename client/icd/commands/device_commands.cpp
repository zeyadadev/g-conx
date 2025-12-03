// Device Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"

extern "C" {

// Vulkan function implementations

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

    // Fetch device properties to cache API version for capability checks.
    VkPhysicalDeviceProperties phys_props = {};
    vkGetPhysicalDeviceProperties(physicalDevice, &phys_props);

    // Call server to create device
    VkResult result = vn_call_vkCreateDevice(&g_ring, remote_physical_device, pCreateInfo, pAllocator, &icd_device->remote_handle);

    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateDevice failed: " << result << "\n";
        delete icd_device;
        return result;
    }

    // Return the ICD device as VkDevice handle
    *pDevice = icd_device_to_handle(icd_device);

    // Store device mapping and enabled extensions
    g_device_state.add_device(*pDevice, icd_device->remote_handle, physicalDevice, phys_props.apiVersion);
    if (pCreateInfo->enabledExtensionCount > 0) {
        g_device_state.set_device_extensions(*pDevice,
                                             pCreateInfo->ppEnabledExtensionNames,
                                             pCreateInfo->enabledExtensionCount);
    }

    ICD_LOG_INFO() << "[Client ICD] Device created successfully (local=" << *pDevice
              << ", remote=" << icd_device->remote_handle << ")\n";
    return VK_SUCCESS;
}

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

    VkResult flush_result = venus_flush_submit_accumulator();
    if (flush_result != VK_SUCCESS) {
        ICD_LOG_WARN() << "[Client ICD] Pending submit batch flush failed during device destroy: "
                  << flush_result << "\n";
    }

    // Call server to destroy device
    vn_async_vkDestroyDevice(&g_ring, icd_device->remote_handle, pAllocator);
    vn_ring_flush_pending(&g_ring); // ensure batched async commands are delivered before teardown

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

VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) {
    ICD_LOG_INFO() << "[Client ICD] vkDeviceWaitIdle called\n";

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult flush_result = venus_flush_submit_accumulator();
    if (flush_result != VK_SUCCESS) {
        return flush_result;
    }
    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkDeviceWaitIdle\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkDeviceWaitIdle(&g_ring, icd_device->remote_handle);
    if (result == VK_SUCCESS) {
        VkResult invalidate_result = invalidate_host_coherent_mappings(device);
        if (invalidate_result != VK_SUCCESS) {
            return invalidate_result;
        }
    } else {
        ICD_LOG_ERROR() << "[Client ICD] vkDeviceWaitIdle failed: " << result << "\n";
    }
    return result;
}

} // extern "C"
