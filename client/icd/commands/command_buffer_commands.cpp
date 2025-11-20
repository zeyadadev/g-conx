// Command Buffer Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"

extern "C" {

// Vulkan function implementations

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

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2(
    VkCommandBuffer commandBuffer,
    const VkCopyBufferInfo2* pCopyBufferInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyBuffer2 called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyBuffer2") ||
        !validate_buffer_regions(pCopyBufferInfo ? pCopyBufferInfo->regionCount : 0,
                                 pCopyBufferInfo ? pCopyBufferInfo->pRegions : nullptr,
                                 "vkCmdCopyBuffer2")) {
        return;
    }

    VkBuffer remote_src = g_resource_state.get_remote_buffer(pCopyBufferInfo->srcBuffer);
    VkBuffer remote_dst = g_resource_state.get_remote_buffer(pCopyBufferInfo->dstBuffer);
    if (remote_src == VK_NULL_HANDLE || remote_dst == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] vkCmdCopyBuffer2 buffers not tracked\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdCopyBuffer2\n";
        return;
    }

    struct CopyBuffer2Storage {
        VkCopyBufferInfo2 info{};
        std::vector<VkBufferCopy2> regions;
    } storage;

    storage.info = *pCopyBufferInfo;
    storage.info.srcBuffer = remote_src;
    storage.info.dstBuffer = remote_dst;
    if (storage.info.regionCount > 0) {
        storage.regions.assign(pCopyBufferInfo->pRegions,
                               pCopyBufferInfo->pRegions + storage.info.regionCount);
        storage.info.pRegions = storage.regions.data();
    } else {
        storage.info.pRegions = nullptr;
    }

    vn_async_vkCmdCopyBuffer2(&g_ring, remote_cb, &storage.info);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer2KHR(
    VkCommandBuffer commandBuffer,
    const VkCopyBufferInfo2* pCopyBufferInfo) {
    vkCmdCopyBuffer2(commandBuffer, pCopyBufferInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2(
    VkCommandBuffer commandBuffer,
    const VkCopyImageInfo2* pCopyImageInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyImage2 called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyImage2") ||
        !validate_buffer_regions(pCopyImageInfo ? pCopyImageInfo->regionCount : 0,
                                 pCopyImageInfo ? pCopyImageInfo->pRegions : nullptr,
                                 "vkCmdCopyImage2")) {
        return;
    }

    VkImage remote_src = g_resource_state.get_remote_image(pCopyImageInfo->srcImage);
    VkImage remote_dst = g_resource_state.get_remote_image(pCopyImageInfo->dstImage);
    if (remote_src == VK_NULL_HANDLE || remote_dst == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] vkCmdCopyImage2 images not tracked\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdCopyImage2\n";
        return;
    }

    struct CopyImage2Storage {
        VkCopyImageInfo2 info{};
        std::vector<VkImageCopy2> regions;
    } storage;

    storage.info = *pCopyImageInfo;
    storage.info.srcImage = remote_src;
    storage.info.dstImage = remote_dst;
    if (storage.info.regionCount > 0) {
        storage.regions.assign(pCopyImageInfo->pRegions,
                               pCopyImageInfo->pRegions + storage.info.regionCount);
        storage.info.pRegions = storage.regions.data();
    } else {
        storage.info.pRegions = nullptr;
    }

    vn_async_vkCmdCopyImage2(&g_ring, remote_cb, &storage.info);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImage2KHR(
    VkCommandBuffer commandBuffer,
    const VkCopyImageInfo2* pCopyImageInfo) {
    vkCmdCopyImage2(commandBuffer, pCopyImageInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2(
    VkCommandBuffer commandBuffer,
    const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyBufferToImage2 called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyBufferToImage2") ||
        !validate_buffer_regions(pCopyBufferToImageInfo ? pCopyBufferToImageInfo->regionCount : 0,
                                 pCopyBufferToImageInfo ? pCopyBufferToImageInfo->pRegions : nullptr,
                                 "vkCmdCopyBufferToImage2")) {
        return;
    }

    VkBuffer remote_src = g_resource_state.get_remote_buffer(pCopyBufferToImageInfo->srcBuffer);
    VkImage remote_dst = g_resource_state.get_remote_image(pCopyBufferToImageInfo->dstImage);
    if (remote_src == VK_NULL_HANDLE || remote_dst == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] vkCmdCopyBufferToImage2 resources not tracked\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdCopyBufferToImage2\n";
        return;
    }

    struct CopyBufferToImage2Storage {
        VkCopyBufferToImageInfo2 info{};
        std::vector<VkBufferImageCopy2> regions;
    } storage;

    storage.info = *pCopyBufferToImageInfo;
    storage.info.srcBuffer = remote_src;
    storage.info.dstImage = remote_dst;
    if (storage.info.regionCount > 0) {
        storage.regions.assign(pCopyBufferToImageInfo->pRegions,
                               pCopyBufferToImageInfo->pRegions + storage.info.regionCount);
        storage.info.pRegions = storage.regions.data();
    } else {
        storage.info.pRegions = nullptr;
    }

    vn_async_vkCmdCopyBufferToImage2(&g_ring, remote_cb, &storage.info);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage2KHR(
    VkCommandBuffer commandBuffer,
    const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo) {
    vkCmdCopyBufferToImage2(commandBuffer, pCopyBufferToImageInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2(
    VkCommandBuffer commandBuffer,
    const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdCopyImageToBuffer2 called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdCopyImageToBuffer2") ||
        !validate_buffer_regions(pCopyImageToBufferInfo ? pCopyImageToBufferInfo->regionCount : 0,
                                 pCopyImageToBufferInfo ? pCopyImageToBufferInfo->pRegions : nullptr,
                                 "vkCmdCopyImageToBuffer2")) {
        return;
    }

    VkImage remote_src = g_resource_state.get_remote_image(pCopyImageToBufferInfo->srcImage);
    VkBuffer remote_dst = g_resource_state.get_remote_buffer(pCopyImageToBufferInfo->dstBuffer);
    if (remote_src == VK_NULL_HANDLE || remote_dst == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] vkCmdCopyImageToBuffer2 resources not tracked\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdCopyImageToBuffer2\n";
        return;
    }

    struct CopyImageToBuffer2Storage {
        VkCopyImageToBufferInfo2 info{};
        std::vector<VkBufferImageCopy2> regions;
    } storage;

    storage.info = *pCopyImageToBufferInfo;
    storage.info.srcImage = remote_src;
    storage.info.dstBuffer = remote_dst;
    if (storage.info.regionCount > 0) {
        storage.regions.assign(pCopyImageToBufferInfo->pRegions,
                               pCopyImageToBufferInfo->pRegions + storage.info.regionCount);
        storage.info.pRegions = storage.regions.data();
    } else {
        storage.info.pRegions = nullptr;
    }

    vn_async_vkCmdCopyImageToBuffer2(&g_ring, remote_cb, &storage.info);
}

VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer2KHR(
    VkCommandBuffer commandBuffer,
    const VkCopyImageToBufferInfo2* pCopyImageToBufferInfo) {
    vkCmdCopyImageToBuffer2(commandBuffer, pCopyImageToBufferInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2(
    VkCommandBuffer commandBuffer,
    const VkBlitImageInfo2* pBlitImageInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdBlitImage2 called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdBlitImage2") ||
        !validate_buffer_regions(pBlitImageInfo ? pBlitImageInfo->regionCount : 0,
                                 pBlitImageInfo ? pBlitImageInfo->pRegions : nullptr,
                                 "vkCmdBlitImage2")) {
        return;
    }

    VkImage remote_src = g_resource_state.get_remote_image(pBlitImageInfo->srcImage);
    VkImage remote_dst = g_resource_state.get_remote_image(pBlitImageInfo->dstImage);
    if (remote_src == VK_NULL_HANDLE || remote_dst == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] vkCmdBlitImage2 images not tracked\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdBlitImage2\n";
        return;
    }

    struct BlitImage2Storage {
        VkBlitImageInfo2 info{};
        std::vector<VkImageBlit2> regions;
    } storage;

    storage.info = *pBlitImageInfo;
    storage.info.srcImage = remote_src;
    storage.info.dstImage = remote_dst;
    if (storage.info.regionCount > 0) {
        storage.regions.assign(pBlitImageInfo->pRegions,
                               pBlitImageInfo->pRegions + storage.info.regionCount);
        storage.info.pRegions = storage.regions.data();
    } else {
        storage.info.pRegions = nullptr;
    }

    vn_async_vkCmdBlitImage2(&g_ring, remote_cb, &storage.info);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBlitImage2KHR(
    VkCommandBuffer commandBuffer,
    const VkBlitImageInfo2* pBlitImageInfo) {
    vkCmdBlitImage2(commandBuffer, pBlitImageInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage2(
    VkCommandBuffer commandBuffer,
    const VkResolveImageInfo2* pResolveImageInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdResolveImage2 called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdResolveImage2") ||
        !validate_buffer_regions(pResolveImageInfo ? pResolveImageInfo->regionCount : 0,
                                 pResolveImageInfo ? pResolveImageInfo->pRegions : nullptr,
                                 "vkCmdResolveImage2")) {
        return;
    }

    VkImage remote_src = g_resource_state.get_remote_image(pResolveImageInfo->srcImage);
    VkImage remote_dst = g_resource_state.get_remote_image(pResolveImageInfo->dstImage);
    if (remote_src == VK_NULL_HANDLE || remote_dst == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] vkCmdResolveImage2 images not tracked\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdResolveImage2\n";
        return;
    }

    struct ResolveImage2Storage {
        VkResolveImageInfo2 info{};
        std::vector<VkImageResolve2> regions;
    } storage;

    storage.info = *pResolveImageInfo;
    storage.info.srcImage = remote_src;
    storage.info.dstImage = remote_dst;
    if (storage.info.regionCount > 0) {
        storage.regions.assign(pResolveImageInfo->pRegions,
                               pResolveImageInfo->pRegions + storage.info.regionCount);
        storage.info.pRegions = storage.regions.data();
    } else {
        storage.info.pRegions = nullptr;
    }

    vn_async_vkCmdResolveImage2(&g_ring, remote_cb, &storage.info);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResolveImage2KHR(
    VkCommandBuffer commandBuffer,
    const VkResolveImageInfo2* pResolveImageInfo) {
    vkCmdResolveImage2(commandBuffer, pResolveImageInfo);
}

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

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRendering(
    VkCommandBuffer commandBuffer,
    const VkRenderingInfo* pRenderingInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdBeginRendering called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdBeginRendering")) {
        return;
    }
    if (!pRenderingInfo) {
        ICD_LOG_ERROR() << "[Client ICD] vkCmdBeginRendering requires VkRenderingInfo\n";
        return;
    }

    RenderingInfoStorage storage;
    if (!populate_rendering_info(pRenderingInfo, &storage, "vkCmdBeginRendering")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdBeginRendering\n";
        return;
    }

    vn_async_vkCmdBeginRendering(&g_ring, remote_cb, &storage.info);
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderingKHR(
    VkCommandBuffer commandBuffer,
    const VkRenderingInfo* pRenderingInfo) {
    vkCmdBeginRendering(commandBuffer, pRenderingInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRendering(
    VkCommandBuffer commandBuffer) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdEndRendering called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdEndRendering")) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdEndRendering\n";
        return;
    }

    vn_async_vkCmdEndRendering(&g_ring, remote_cb);
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderingKHR(
    VkCommandBuffer commandBuffer) {
    vkCmdEndRendering(commandBuffer);
}

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

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2(
    VkCommandBuffer commandBuffer,
    VkPipelineStageFlags2 stage,
    VkQueryPool queryPool,
    uint32_t query) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdWriteTimestamp2 called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdWriteTimestamp2")) {
        return;
    }

    if (!g_query_state.validate_query_range(queryPool, query, 1)) {
        ICD_LOG_ERROR() << "[Client ICD] Query out of range in vkCmdWriteTimestamp2\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkQueryPool remote_pool = g_query_state.get_remote_query_pool(queryPool);
    if (remote_pool == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Query pool not tracked in vkCmdWriteTimestamp2\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdWriteTimestamp2\n";
        return;
    }

    vn_async_vkCmdWriteTimestamp2(&g_ring, remote_cb, stage, remote_pool, query);
}

VKAPI_ATTR void VKAPI_CALL vkCmdWriteTimestamp2KHR(
    VkCommandBuffer commandBuffer,
    VkPipelineStageFlags2 stage,
    VkQueryPool queryPool,
    uint32_t query) {
    vkCmdWriteTimestamp2(commandBuffer, stage, queryPool, query);
}

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

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2(
    VkCommandBuffer commandBuffer,
    VkEvent event,
    const VkDependencyInfo* pDependencyInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetEvent2 called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetEvent2")) {
        return;
    }
    if (!pDependencyInfo) {
        ICD_LOG_ERROR() << "[Client ICD] vkCmdSetEvent2 missing dependency info\n";
        return;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkCmdSetEvent2\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetEvent2\n";
        return;
    }

    DependencyInfoStorage storage;
    if (!populate_dependency_info(pDependencyInfo, &storage, "vkCmdSetEvent2")) {
        return;
    }

    vn_async_vkCmdSetEvent2(&g_ring, remote_cb, remote_event, &storage.info);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetEvent2KHR(
    VkCommandBuffer commandBuffer,
    VkEvent event,
    const VkDependencyInfo* pDependencyInfo) {
    vkCmdSetEvent2(commandBuffer, event, pDependencyInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2(
    VkCommandBuffer commandBuffer,
    VkEvent event,
    VkPipelineStageFlags2 stageMask) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdResetEvent2 called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdResetEvent2")) {
        return;
    }

    VkEvent remote_event = g_sync_state.get_remote_event(event);
    if (remote_event == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkCmdResetEvent2\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdResetEvent2\n";
        return;
    }

    vn_async_vkCmdResetEvent2(&g_ring, remote_cb, remote_event, stageMask);
}

VKAPI_ATTR void VKAPI_CALL vkCmdResetEvent2KHR(
    VkCommandBuffer commandBuffer,
    VkEvent event,
    VkPipelineStageFlags2 stageMask) {
    vkCmdResetEvent2(commandBuffer, event, stageMask);
}

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

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2(
    VkCommandBuffer commandBuffer,
    uint32_t eventCount,
    const VkEvent* pEvents,
    const VkDependencyInfo* pDependencyInfos) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdWaitEvents2 called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdWaitEvents2")) {
        return;
    }

    if (eventCount == 0 || !pEvents || !pDependencyInfos) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCmdWaitEvents2\n";
        return;
    }

    std::vector<VkEvent> remote_events(eventCount, VK_NULL_HANDLE);
    std::vector<DependencyInfoStorage> dep_storage(eventCount);
    for (uint32_t i = 0; i < eventCount; ++i) {
        remote_events[i] = g_sync_state.get_remote_event(pEvents[i]);
        if (remote_events[i] == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Event not tracked in vkCmdWaitEvents2\n";
            return;
        }
        if (!populate_dependency_info(&pDependencyInfos[i], &dep_storage[i], "vkCmdWaitEvents2")) {
            return;
        }
    }

    std::vector<VkDependencyInfo> remote_infos(eventCount);
    for (uint32_t i = 0; i < eventCount; ++i) {
        remote_infos[i] = dep_storage[i].info;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdWaitEvents2\n";
        return;
    }

    vn_async_vkCmdWaitEvents2(&g_ring,
                              remote_cb,
                              eventCount,
                              remote_events.data(),
                              remote_infos.data());
}

VKAPI_ATTR void VKAPI_CALL vkCmdWaitEvents2KHR(
    VkCommandBuffer commandBuffer,
    uint32_t eventCount,
    const VkEvent* pEvents,
    const VkDependencyInfo* pDependencyInfos) {
    vkCmdWaitEvents2(commandBuffer, eventCount, pEvents, pDependencyInfos);
}

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

VKAPI_ATTR void VKAPI_CALL vkCmdSetCullMode(
    VkCommandBuffer commandBuffer,
    VkCullModeFlags cullMode) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetCullMode called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetCullMode")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetCullMode\n";
        return;
    }
    vn_async_vkCmdSetCullMode(&g_ring, remote_cb, cullMode);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetCullModeEXT(
    VkCommandBuffer commandBuffer,
    VkCullModeFlags cullMode) {
    vkCmdSetCullMode(commandBuffer, cullMode);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFace(
    VkCommandBuffer commandBuffer,
    VkFrontFace frontFace) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetFrontFace called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetFrontFace")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetFrontFace\n";
        return;
    }
    vn_async_vkCmdSetFrontFace(&g_ring, remote_cb, frontFace);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetFrontFaceEXT(
    VkCommandBuffer commandBuffer,
    VkFrontFace frontFace) {
    vkCmdSetFrontFace(commandBuffer, frontFace);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopology(
    VkCommandBuffer commandBuffer,
    VkPrimitiveTopology primitiveTopology) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetPrimitiveTopology called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetPrimitiveTopology")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetPrimitiveTopology\n";
        return;
    }
    vn_async_vkCmdSetPrimitiveTopology(&g_ring, remote_cb, primitiveTopology);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveTopologyEXT(
    VkCommandBuffer commandBuffer,
    VkPrimitiveTopology primitiveTopology) {
    vkCmdSetPrimitiveTopology(commandBuffer, primitiveTopology);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCount(
    VkCommandBuffer commandBuffer,
    uint32_t viewportCount,
    const VkViewport* pViewports) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetViewportWithCount called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetViewportWithCount")) {
        return;
    }
    if (viewportCount == 0 || !pViewports) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid viewport data in vkCmdSetViewportWithCount\n";
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetViewportWithCount\n";
        return;
    }
    vn_async_vkCmdSetViewportWithCount(&g_ring, remote_cb, viewportCount, pViewports);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetViewportWithCountEXT(
    VkCommandBuffer commandBuffer,
    uint32_t viewportCount,
    const VkViewport* pViewports) {
    vkCmdSetViewportWithCount(commandBuffer, viewportCount, pViewports);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissorWithCount(
    VkCommandBuffer commandBuffer,
    uint32_t scissorCount,
    const VkRect2D* pScissors) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetScissorWithCount called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetScissorWithCount")) {
        return;
    }
    if (scissorCount == 0 || !pScissors) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid scissor data in vkCmdSetScissorWithCount\n";
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetScissorWithCount\n";
        return;
    }
    vn_async_vkCmdSetScissorWithCount(&g_ring, remote_cb, scissorCount, pScissors);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetScissorWithCountEXT(
    VkCommandBuffer commandBuffer,
    uint32_t scissorCount,
    const VkRect2D* pScissors) {
    vkCmdSetScissorWithCount(commandBuffer, scissorCount, pScissors);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnable(
    VkCommandBuffer commandBuffer,
    VkBool32 depthTestEnable) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetDepthTestEnable called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetDepthTestEnable")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetDepthTestEnable\n";
        return;
    }
    vn_async_vkCmdSetDepthTestEnable(&g_ring, remote_cb, depthTestEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthTestEnableEXT(
    VkCommandBuffer commandBuffer,
    VkBool32 depthTestEnable) {
    vkCmdSetDepthTestEnable(commandBuffer, depthTestEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnable(
    VkCommandBuffer commandBuffer,
    VkBool32 depthWriteEnable) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetDepthWriteEnable called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetDepthWriteEnable")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetDepthWriteEnable\n";
        return;
    }
    vn_async_vkCmdSetDepthWriteEnable(&g_ring, remote_cb, depthWriteEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthWriteEnableEXT(
    VkCommandBuffer commandBuffer,
    VkBool32 depthWriteEnable) {
    vkCmdSetDepthWriteEnable(commandBuffer, depthWriteEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOp(
    VkCommandBuffer commandBuffer,
    VkCompareOp compareOp) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetDepthCompareOp called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetDepthCompareOp")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetDepthCompareOp\n";
        return;
    }
    vn_async_vkCmdSetDepthCompareOp(&g_ring, remote_cb, compareOp);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthCompareOpEXT(
    VkCommandBuffer commandBuffer,
    VkCompareOp compareOp) {
    vkCmdSetDepthCompareOp(commandBuffer, compareOp);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBoundsTestEnable(
    VkCommandBuffer commandBuffer,
    VkBool32 depthBoundsTestEnable) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetDepthBoundsTestEnable called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetDepthBoundsTestEnable")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetDepthBoundsTestEnable\n";
        return;
    }
    vn_async_vkCmdSetDepthBoundsTestEnable(&g_ring, remote_cb, depthBoundsTestEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBoundsTestEnableEXT(
    VkCommandBuffer commandBuffer,
    VkBool32 depthBoundsTestEnable) {
    vkCmdSetDepthBoundsTestEnable(commandBuffer, depthBoundsTestEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnable(
    VkCommandBuffer commandBuffer,
    VkBool32 stencilTestEnable) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetStencilTestEnable called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetStencilTestEnable")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetStencilTestEnable\n";
        return;
    }
    vn_async_vkCmdSetStencilTestEnable(&g_ring, remote_cb, stencilTestEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilTestEnableEXT(
    VkCommandBuffer commandBuffer,
    VkBool32 stencilTestEnable) {
    vkCmdSetStencilTestEnable(commandBuffer, stencilTestEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOp(
    VkCommandBuffer commandBuffer,
    VkStencilFaceFlags faceMask,
    VkStencilOp failOp,
    VkStencilOp passOp,
    VkStencilOp depthFailOp,
    VkCompareOp compareOp) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetStencilOp called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetStencilOp")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetStencilOp\n";
        return;
    }
    vn_async_vkCmdSetStencilOp(&g_ring, remote_cb, faceMask, failOp, passOp, depthFailOp, compareOp);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetStencilOpEXT(
    VkCommandBuffer commandBuffer,
    VkStencilFaceFlags faceMask,
    VkStencilOp failOp,
    VkStencilOp passOp,
    VkStencilOp depthFailOp,
    VkCompareOp compareOp) {
    vkCmdSetStencilOp(commandBuffer, faceMask, failOp, passOp, depthFailOp, compareOp);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizerDiscardEnable(
    VkCommandBuffer commandBuffer,
    VkBool32 rasterizerDiscardEnable) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetRasterizerDiscardEnable called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetRasterizerDiscardEnable")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetRasterizerDiscardEnable\n";
        return;
    }
    vn_async_vkCmdSetRasterizerDiscardEnable(&g_ring, remote_cb, rasterizerDiscardEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetRasterizerDiscardEnableEXT(
    VkCommandBuffer commandBuffer,
    VkBool32 rasterizerDiscardEnable) {
    vkCmdSetRasterizerDiscardEnable(commandBuffer, rasterizerDiscardEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBiasEnable(
    VkCommandBuffer commandBuffer,
    VkBool32 depthBiasEnable) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetDepthBiasEnable called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetDepthBiasEnable")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetDepthBiasEnable\n";
        return;
    }
    vn_async_vkCmdSetDepthBiasEnable(&g_ring, remote_cb, depthBiasEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetDepthBiasEnableEXT(
    VkCommandBuffer commandBuffer,
    VkBool32 depthBiasEnable) {
    vkCmdSetDepthBiasEnable(commandBuffer, depthBiasEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartEnable(
    VkCommandBuffer commandBuffer,
    VkBool32 primitiveRestartEnable) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdSetPrimitiveRestartEnable called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdSetPrimitiveRestartEnable")) {
        return;
    }
    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }
    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdSetPrimitiveRestartEnable\n";
        return;
    }
    vn_async_vkCmdSetPrimitiveRestartEnable(&g_ring, remote_cb, primitiveRestartEnable);
}

VKAPI_ATTR void VKAPI_CALL vkCmdSetPrimitiveRestartEnableEXT(
    VkCommandBuffer commandBuffer,
    VkBool32 primitiveRestartEnable) {
    vkCmdSetPrimitiveRestartEnable(commandBuffer, primitiveRestartEnable);
}

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

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2(
    VkCommandBuffer commandBuffer,
    const VkDependencyInfo* pDependencyInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCmdPipelineBarrier2 called\n";

    if (!ensure_command_buffer_recording(commandBuffer, "vkCmdPipelineBarrier2")) {
        return;
    }
    if (!pDependencyInfo) {
        ICD_LOG_ERROR() << "[Client ICD] vkCmdPipelineBarrier2 missing dependency info\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    VkCommandBuffer remote_cb = get_remote_command_buffer_handle(commandBuffer);
    if (remote_cb == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote command buffer missing in vkCmdPipelineBarrier2\n";
        return;
    }

    DependencyInfoStorage storage;
    if (!populate_dependency_info(pDependencyInfo, &storage, "vkCmdPipelineBarrier2")) {
        return;
    }

    vn_async_vkCmdPipelineBarrier2(&g_ring, remote_cb, &storage.info);
}

VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier2KHR(
    VkCommandBuffer commandBuffer,
    const VkDependencyInfo* pDependencyInfo) {
    vkCmdPipelineBarrier2(commandBuffer, pDependencyInfo);
}

} // extern "C"
