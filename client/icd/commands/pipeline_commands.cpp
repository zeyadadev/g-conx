// Pipeline Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"

extern "C" {

// Vulkan function implementations

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

} // extern "C"
