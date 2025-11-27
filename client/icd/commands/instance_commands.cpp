// Instance Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"

extern "C" {

// Vulkan function implementations

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
        vn_ring_flush_pending(&g_ring); // flush any batched commands before shutdown
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

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirements(
    VkDevice device,
    const VkDeviceBufferMemoryRequirements* pInfo,
    VkMemoryRequirements2* pMemoryRequirements) {

    ICD_LOG_INFO() << "[Client ICD] vkGetDeviceBufferMemoryRequirements called\n";

    if (!pInfo || !pMemoryRequirements || !pInfo->pCreateInfo) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkGetDeviceBufferMemoryRequirements\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetDeviceBufferMemoryRequirements\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetDeviceBufferMemoryRequirements(&g_ring,
                                                icd_device->remote_handle,
                                                pInfo,
                                                pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceBufferMemoryRequirementsKHR(
    VkDevice device,
    const VkDeviceBufferMemoryRequirements* pInfo,
    VkMemoryRequirements2* pMemoryRequirements) {
    vkGetDeviceBufferMemoryRequirements(device, pInfo, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirements(
    VkDevice device,
    const VkDeviceImageMemoryRequirements* pInfo,
    VkMemoryRequirements2* pMemoryRequirements) {

    ICD_LOG_INFO() << "[Client ICD] vkGetDeviceImageMemoryRequirements called\n";

    if (!pInfo || !pMemoryRequirements || !pInfo->pCreateInfo) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkGetDeviceImageMemoryRequirements\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetDeviceImageMemoryRequirements\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetDeviceImageMemoryRequirements(&g_ring,
                                               icd_device->remote_handle,
                                               pInfo,
                                               pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageMemoryRequirementsKHR(
    VkDevice device,
    const VkDeviceImageMemoryRequirements* pInfo,
    VkMemoryRequirements2* pMemoryRequirements) {
    vkGetDeviceImageMemoryRequirements(device, pInfo, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirements(
    VkDevice device,
    const VkDeviceImageMemoryRequirements* pInfo,
    uint32_t* pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) {

    ICD_LOG_INFO() << "[Client ICD] vkGetDeviceImageSparseMemoryRequirements called\n";

    if (!pInfo || !pInfo->pCreateInfo || !pSparseMemoryRequirementCount) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkGetDeviceImageSparseMemoryRequirements\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetDeviceImageSparseMemoryRequirements\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetDeviceImageSparseMemoryRequirements(&g_ring,
                                                     icd_device->remote_handle,
                                                     pInfo,
                                                     pSparseMemoryRequirementCount,
                                                     pSparseMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSparseMemoryRequirementsKHR(
    VkDevice device,
    const VkDeviceImageMemoryRequirements* pInfo,
    uint32_t* pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2* pSparseMemoryRequirements) {
    vkGetDeviceImageSparseMemoryRequirements(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}

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

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutablePropertiesKHR(
    VkDevice device,
    const VkPipelineInfoKHR* pPipelineInfo,
    uint32_t* pExecutableCount,
    VkPipelineExecutablePropertiesKHR* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPipelineExecutablePropertiesKHR called\n";

    if (!pPipelineInfo || !pExecutableCount) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters in vkGetPipelineExecutablePropertiesKHR\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetPipelineExecutablePropertiesKHR\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pPipelineInfo->pipeline != VK_NULL_HANDLE &&
        g_pipeline_state.get_remote_pipeline(pPipelineInfo->pipeline) == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline not tracked in vkGetPipelineExecutablePropertiesKHR\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const uint32_t capacity = (pProperties && pExecutableCount) ? *pExecutableCount : 0;
    *pExecutableCount = 0;

    if (pProperties && capacity > 0) {
        for (uint32_t i = 0; i < capacity; ++i) {
            pProperties[i] = {};
            pProperties[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
        }
    }

    log_pipeline_exec_stub_once();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutableStatisticsKHR(
    VkDevice device,
    const VkPipelineExecutableInfoKHR* pExecutableInfo,
    uint32_t* pStatisticCount,
    VkPipelineExecutableStatisticKHR* pStatistics) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPipelineExecutableStatisticsKHR called\n";

    if (!pExecutableInfo || !pStatisticCount) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters in vkGetPipelineExecutableStatisticsKHR\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetPipelineExecutableStatisticsKHR\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pExecutableInfo->pipeline != VK_NULL_HANDLE &&
        g_pipeline_state.get_remote_pipeline(pExecutableInfo->pipeline) == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline not tracked in vkGetPipelineExecutableStatisticsKHR\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const uint32_t capacity = (pStatistics && pStatisticCount) ? *pStatisticCount : 0;
    *pStatisticCount = 0;

    if (pStatistics && capacity > 0) {
        for (uint32_t i = 0; i < capacity; ++i) {
            pStatistics[i] = {};
            pStatistics[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
        }
    }

    log_pipeline_exec_stub_once();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPipelineExecutableInternalRepresentationsKHR(
    VkDevice device,
    const VkPipelineExecutableInfoKHR* pExecutableInfo,
    uint32_t* pInternalRepresentationCount,
    VkPipelineExecutableInternalRepresentationKHR* pInternalRepresentations) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPipelineExecutableInternalRepresentationsKHR called\n";

    if (!pExecutableInfo || !pInternalRepresentationCount) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters in vkGetPipelineExecutableInternalRepresentationsKHR\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetPipelineExecutableInternalRepresentationsKHR\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (pExecutableInfo->pipeline != VK_NULL_HANDLE &&
        g_pipeline_state.get_remote_pipeline(pExecutableInfo->pipeline) == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Pipeline not tracked in vkGetPipelineExecutableInternalRepresentationsKHR\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const uint32_t capacity =
        (pInternalRepresentations && pInternalRepresentationCount) ? *pInternalRepresentationCount : 0;
    *pInternalRepresentationCount = 0;

    if (pInternalRepresentations && capacity > 0) {
        for (uint32_t i = 0; i < capacity; ++i) {
            void* data_ptr = pInternalRepresentations[i].pData;
            size_t data_size = pInternalRepresentations[i].dataSize;
            pInternalRepresentations[i] = {};
            pInternalRepresentations[i].sType =
                VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR;
            if (data_ptr && data_size > 0) {
                std::memset(data_ptr, 0, data_size);
            }
        }
    }

    log_pipeline_exec_stub_once();
    return VK_SUCCESS;
}

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
        VkResult invalidate_result = invalidate_host_coherent_mappings(device);
        if (invalidate_result != VK_SUCCESS) {
            return invalidate_result;
        }
    }
    return result;
}

} // extern "C"
