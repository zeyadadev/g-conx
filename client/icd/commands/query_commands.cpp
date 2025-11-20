// Query Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"

extern "C" {

// Vulkan function implementations

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

} // extern "C"
