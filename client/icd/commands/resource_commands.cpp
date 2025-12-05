// Resource Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"

extern "C" {

// Vulkan function implementations

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

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(
    VkDevice device,
    const VkBufferMemoryRequirementsInfo2* pInfo,
    VkMemoryRequirements2* pMemoryRequirements) {

    ICD_LOG_INFO() << "[Client ICD] vkGetBufferMemoryRequirements2 called\n";

    if (!pInfo || !pMemoryRequirements) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkGetBufferMemoryRequirements2\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetBufferMemoryRequirements2\n";
        return;
    }

    VkBuffer remote_buffer = g_resource_state.get_remote_buffer(pInfo->buffer);
    if (remote_buffer == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked in vkGetBufferMemoryRequirements2\n";
        return;
    }

    VkBufferMemoryRequirementsInfo2 remote_info = *pInfo;
    remote_info.buffer = remote_buffer;

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetBufferMemoryRequirements2(&g_ring,
                                           icd_device->remote_handle,
                                           &remote_info,
                                           pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2KHR(
    VkDevice device,
    const VkBufferMemoryRequirementsInfo2* pInfo,
    VkMemoryRequirements2* pMemoryRequirements) {
    vkGetBufferMemoryRequirements2(device, pInfo, pMemoryRequirements);
}

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

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(
    VkDevice device,
    uint32_t bindInfoCount,
    const VkBindBufferMemoryInfo* pBindInfos) {

    ICD_LOG_INFO() << "[Client ICD] vkBindBufferMemory2 called (count=" << bindInfoCount << ")\n";

    if (bindInfoCount == 0 || !pBindInfos) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkBindBufferMemory2\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkBindBufferMemory2\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkBindBufferMemoryInfo> remote_infos(bindInfoCount);
    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        const VkBindBufferMemoryInfo& info = pBindInfos[i];
        if (!g_resource_state.has_buffer(info.buffer) || !g_resource_state.has_memory(info.memory)) {
            ICD_LOG_ERROR() << "[Client ICD] Buffer or memory not tracked in vkBindBufferMemory2 (index=" << i << ")\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        if (g_resource_state.buffer_is_bound(info.buffer)) {
            ICD_LOG_ERROR() << "[Client ICD] Buffer already bound in vkBindBufferMemory2 (index=" << i << ")\n";
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }

        VkMemoryRequirements cached_requirements;
        if (g_resource_state.get_cached_buffer_requirements(info.buffer, &cached_requirements)) {
            VkDeviceSize memory_size = g_resource_state.get_memory_size(info.memory);
            if (!validate_memory_offset(cached_requirements, memory_size, info.memoryOffset)) {
                ICD_LOG_ERROR() << "[Client ICD] Buffer bind validation failed in vkBindBufferMemory2 (index=" << i
                            << ", offset=" << info.memoryOffset << ")\n";
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
        }

        remote_infos[i] = info;
        remote_infos[i].buffer = g_resource_state.get_remote_buffer(info.buffer);
        remote_infos[i].memory = g_resource_state.get_remote_memory(info.memory);
        if (remote_infos[i].buffer == VK_NULL_HANDLE || remote_infos[i].memory == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Remote handles missing in vkBindBufferMemory2 (index=" << i << ")\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkBindBufferMemory2(&g_ring,
                                                  icd_device->remote_handle,
                                                  bindInfoCount,
                                                  remote_infos.data());
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < bindInfoCount; ++i) {
            g_resource_state.bind_buffer(pBindInfos[i].buffer, pBindInfos[i].memory, pBindInfos[i].memoryOffset);
        }
        ICD_LOG_INFO() << "[Client ICD] vkBindBufferMemory2 bound " << bindInfoCount << " buffer(s)\n";
    } else {
        ICD_LOG_ERROR() << "[Client ICD] vkBindBufferMemory2 failed: " << result << "\n";
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2KHR(
    VkDevice device,
    uint32_t bindInfoCount,
    const VkBindBufferMemoryInfo* pBindInfos) {
    return vkBindBufferMemory2(device, bindInfoCount, pBindInfos);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddress(
    VkDevice device,
    const VkBufferDeviceAddressInfo* pInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkGetBufferDeviceAddress called\n";

    if (!pInfo) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkGetBufferDeviceAddress\n";
        return 0;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return 0;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetBufferDeviceAddress\n";
        return 0;
    }

    VkBuffer remote_buffer = g_resource_state.get_remote_buffer(pInfo->buffer);
    if (remote_buffer == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked in vkGetBufferDeviceAddress\n";
        return 0;
    }

    VkBufferDeviceAddressInfo remote_info = *pInfo;
    remote_info.buffer = remote_buffer;

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDeviceAddress address =
        vn_call_vkGetBufferDeviceAddress(&g_ring, icd_device->remote_handle, &remote_info);
    ICD_LOG_INFO() << "[Client ICD] Buffer device address: 0x" << std::hex << address << std::dec << "\n";
    return address;
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddressKHR(
    VkDevice device,
    const VkBufferDeviceAddressInfo* pInfo) {
    return vkGetBufferDeviceAddress(device, pInfo);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddressEXT(
    VkDevice device,
    const VkBufferDeviceAddressInfo* pInfo) {
    return vkGetBufferDeviceAddress(device, pInfo);
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddress(
    VkDevice device,
    const VkBufferDeviceAddressInfo* pInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkGetBufferOpaqueCaptureAddress called\n";

    if (!pInfo) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkGetBufferOpaqueCaptureAddress\n";
        return 0;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return 0;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetBufferOpaqueCaptureAddress\n";
        return 0;
    }

    VkBufferDeviceAddressInfo remote_info = *pInfo;
    remote_info.buffer = g_resource_state.get_remote_buffer(pInfo->buffer);
    if (remote_info.buffer == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Buffer not tracked in vkGetBufferOpaqueCaptureAddress\n";
        return 0;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    uint64_t address = vn_call_vkGetBufferOpaqueCaptureAddress(&g_ring, icd_device->remote_handle, &remote_info);
    ICD_LOG_INFO() << "[Client ICD] Buffer opaque capture address: 0x" << std::hex << address << std::dec << "\n";
    return address;
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetBufferOpaqueCaptureAddressKHR(
    VkDevice device,
    const VkBufferDeviceAddressInfo* pInfo) {
    return vkGetBufferOpaqueCaptureAddress(device, pInfo);
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddress(
    VkDevice device,
    const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkGetDeviceMemoryOpaqueCaptureAddress called\n";

    if (!pInfo) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkGetDeviceMemoryOpaqueCaptureAddress\n";
        return 0;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return 0;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetDeviceMemoryOpaqueCaptureAddress\n";
        return 0;
    }

    VkDeviceMemoryOpaqueCaptureAddressInfo remote_info = *pInfo;
    remote_info.memory = g_resource_state.get_remote_memory(pInfo->memory);
    if (remote_info.memory == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Memory not tracked in vkGetDeviceMemoryOpaqueCaptureAddress\n";
        return 0;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    uint64_t address = vn_call_vkGetDeviceMemoryOpaqueCaptureAddress(&g_ring, icd_device->remote_handle, &remote_info);
    ICD_LOG_INFO() << "[Client ICD] Memory opaque capture address: 0x" << std::hex << address << std::dec << "\n";
    return address;
}

VKAPI_ATTR uint64_t VKAPI_CALL vkGetDeviceMemoryOpaqueCaptureAddressKHR(
    VkDevice device,
    const VkDeviceMemoryOpaqueCaptureAddressInfo* pInfo) {
    return vkGetDeviceMemoryOpaqueCaptureAddress(device, pInfo);
}

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

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(
    VkDevice device,
    const VkImageMemoryRequirementsInfo2* pInfo,
    VkMemoryRequirements2* pMemoryRequirements) {

    ICD_LOG_INFO() << "[Client ICD] vkGetImageMemoryRequirements2 called\n";

    if (!pInfo || !pMemoryRequirements) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkGetImageMemoryRequirements2\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetImageMemoryRequirements2\n";
        return;
    }

    VkImage remote_image = g_resource_state.get_remote_image(pInfo->image);
    if (remote_image == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Image not tracked in vkGetImageMemoryRequirements2\n";
        return;
    }

    VkImageMemoryRequirementsInfo2 remote_info = *pInfo;
    remote_info.image = remote_image;

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetImageMemoryRequirements2(&g_ring,
                                          icd_device->remote_handle,
                                          &remote_info,
                                          pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2KHR(
    VkDevice device,
    const VkImageMemoryRequirementsInfo2* pInfo,
    VkMemoryRequirements2* pMemoryRequirements) {
    vkGetImageMemoryRequirements2(device, pInfo, pMemoryRequirements);
}

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

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(
    VkDevice device,
    uint32_t bindInfoCount,
    const VkBindImageMemoryInfo* pBindInfos) {

    ICD_LOG_INFO() << "[Client ICD] vkBindImageMemory2 called (count=" << bindInfoCount << ")\n";

    if (bindInfoCount == 0 || !pBindInfos) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkBindImageMemory2\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkBindImageMemory2\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkBindImageMemoryInfo> remote_infos(bindInfoCount);
    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        const VkBindImageMemoryInfo& info = pBindInfos[i];
        if (!g_resource_state.has_image(info.image) || !g_resource_state.has_memory(info.memory)) {
            ICD_LOG_ERROR() << "[Client ICD] Image or memory not tracked in vkBindImageMemory2 (index=" << i << ")\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        if (g_resource_state.image_is_bound(info.image)) {
            ICD_LOG_ERROR() << "[Client ICD] Image already bound in vkBindImageMemory2 (index=" << i << ")\n";
            return VK_ERROR_VALIDATION_FAILED_EXT;
        }

        VkMemoryRequirements cached_requirements = {};
        if (g_resource_state.get_cached_image_requirements(info.image, &cached_requirements)) {
            VkDeviceSize memory_size = g_resource_state.get_memory_size(info.memory);
            if (!validate_memory_offset(cached_requirements, memory_size, info.memoryOffset)) {
                ICD_LOG_ERROR() << "[Client ICD] Image bind validation failed in vkBindImageMemory2 (index=" << i
                            << ", offset=" << info.memoryOffset << ")\n";
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
        }

        remote_infos[i] = info;
        remote_infos[i].image = g_resource_state.get_remote_image(info.image);
        remote_infos[i].memory = g_resource_state.get_remote_memory(info.memory);
        if (remote_infos[i].image == VK_NULL_HANDLE || remote_infos[i].memory == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Remote handles missing in vkBindImageMemory2 (index=" << i << ")\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult result = vn_call_vkBindImageMemory2(&g_ring,
                                                 icd_device->remote_handle,
                                                 bindInfoCount,
                                                 remote_infos.data());
    if (result == VK_SUCCESS) {
        for (uint32_t i = 0; i < bindInfoCount; ++i) {
            g_resource_state.bind_image(pBindInfos[i].image, pBindInfos[i].memory, pBindInfos[i].memoryOffset);
        }
        ICD_LOG_INFO() << "[Client ICD] vkBindImageMemory2 bound " << bindInfoCount << " image(s)\n";
    } else {
        ICD_LOG_ERROR() << "[Client ICD] vkBindImageMemory2 failed: " << result << "\n";
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2KHR(
    VkDevice device,
    uint32_t bindInfoCount,
    const VkBindImageMemoryInfo* pBindInfos) {
    return vkBindImageMemory2(device, bindInfoCount, pBindInfos);
}

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

} // extern "C"
