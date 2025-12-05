// Resource Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"
#include "host_image_copy_utils.h"

#include <vector>

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

    VkBufferCreateInfo local_info = *pCreateInfo;
    // Fold VkBufferUsageFlags2CreateInfo into legacy usage bits for compatibility.
    for (const VkBaseInStructure* header = reinterpret_cast<const VkBaseInStructure*>(pCreateInfo->pNext);
         header;
         header = header->pNext) {
        if (header->sType == VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO ||
            header->sType == VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO_KHR) {
            const VkBufferUsageFlags2CreateInfo* usage2 =
                reinterpret_cast<const VkBufferUsageFlags2CreateInfo*>(header);
            VkBufferUsageFlags legacy =
                static_cast<VkBufferUsageFlags>(usage2->usage & 0xffffffffu);
            local_info.usage |= legacy;
            if (usage2->usage >> 32) {
                ICD_LOG_WARN() << "[Client ICD] vkCreateBuffer ignoring upper 32 bits of usage2";
            }
        }
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkDevice remote_device = icd_device->remote_handle;

    VkBuffer remote_buffer = VK_NULL_HANDLE;
    VkResult result = vn_call_vkCreateBuffer(&g_ring, remote_device, &local_info, pAllocator, &remote_buffer);
    if (result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCreateBuffer failed: " << result << "\n";
        return result;
    }

    VkBuffer local_buffer = g_handle_allocator.allocate<VkBuffer>();
    g_resource_state.add_buffer(device, local_buffer, remote_buffer, local_info);
    *pBuffer = local_buffer;

    ICD_LOG_INFO() << "[Client ICD] Buffer created (local=" << *pBuffer
              << ", remote=" << remote_buffer
              << ", size=" << local_info.size << ")\n";
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
              << ", alignment=" << pMemoryRequirements->alignment
              << ", memoryTypeBits=0x" << std::hex << pMemoryRequirements->memoryTypeBits << std::dec << "\n";
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

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSubresourceLayout(
    VkDevice device,
    const VkDeviceImageSubresourceInfo* pInfo,
    VkSubresourceLayout2* pLayout) {

    ICD_LOG_INFO() << "[Client ICD] vkGetDeviceImageSubresourceLayout called\n";

    if (!pInfo || !pLayout || !pInfo->pCreateInfo || !pInfo->pSubresource) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters in vkGetDeviceImageSubresourceLayout\n";
        if (pLayout) {
            memset(pLayout, 0, sizeof(VkSubresourceLayout2));
        }
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pLayout, 0, sizeof(VkSubresourceLayout2));
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetDeviceImageSubresourceLayout\n";
        memset(pLayout, 0, sizeof(VkSubresourceLayout2));
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetDeviceImageSubresourceLayout(&g_ring, icd_device->remote_handle, pInfo, pLayout);
}

VKAPI_ATTR void VKAPI_CALL vkGetDeviceImageSubresourceLayoutKHR(
    VkDevice device,
    const VkDeviceImageSubresourceInfo* pInfo,
    VkSubresourceLayout2* pLayout) {
    vkGetDeviceImageSubresourceLayout(device, pInfo, pLayout);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2(
    VkDevice device,
    VkImage image,
    const VkImageSubresource2* pSubresource,
    VkSubresourceLayout2* pLayout) {

    ICD_LOG_INFO() << "[Client ICD] vkGetImageSubresourceLayout2 called\n";

    if (!pSubresource || !pLayout) {
        ICD_LOG_ERROR() << "[Client ICD] Missing parameters in vkGetImageSubresourceLayout2\n";
        if (pLayout) {
            memset(pLayout, 0, sizeof(VkSubresourceLayout2));
        }
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pLayout, 0, sizeof(VkSubresourceLayout2));
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetImageSubresourceLayout2\n";
        memset(pLayout, 0, sizeof(VkSubresourceLayout2));
        return;
    }

    VkImage remote_image = g_resource_state.get_remote_image(image);
    if (remote_image == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Image not tracked in vkGetImageSubresourceLayout2\n";
        memset(pLayout, 0, sizeof(VkSubresourceLayout2));
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetImageSubresourceLayout2(&g_ring,
                                         icd_device->remote_handle,
                                         remote_image,
                                         pSubresource,
                                         pLayout);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2KHR(
    VkDevice device,
    VkImage image,
    const VkImageSubresource2* pSubresource,
    VkSubresourceLayout2* pLayout) {
    vkGetImageSubresourceLayout2(device, image, pSubresource, pLayout);
}

VKAPI_ATTR void VKAPI_CALL vkGetImageSubresourceLayout2EXT(
    VkDevice device,
    VkImage image,
    const VkImageSubresource2* pSubresource,
    VkSubresourceLayout2* pLayout) {
    vkGetImageSubresourceLayout2(device, image, pSubresource, pLayout);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyMemoryToImage(
    VkDevice device,
    const VkCopyMemoryToImageInfo* pCopyMemoryToImageInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCopyMemoryToImage called\n";

    if (!pCopyMemoryToImageInfo || !pCopyMemoryToImageInfo->pRegions ||
        pCopyMemoryToImageInfo->regionCount == 0) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCopyMemoryToImage\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_DEVICE_LOST;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCopyMemoryToImage\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const VkPhysicalDeviceVulkan14Features* vk14 = g_device_state.get_vk14_features(device);
    if (!vk14 || !vk14->hostImageCopy) {
        ICD_LOG_ERROR() << "[Client ICD] hostImageCopy feature not enabled\n";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    venus_plus::ImageState image_state = {};
    if (!g_resource_state.get_image_info(pCopyMemoryToImageInfo->dstImage, &image_state)) {
        ICD_LOG_ERROR() << "[Client ICD] Destination image not tracked\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImage remote_image = image_state.remote_handle;
    if (remote_image == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Destination image missing remote handle\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice remote_physical = g_device_state.get_device_physical_device(device);
    VkFormatProperties3 props3 = {};
    props3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
    VkFormatProperties2 props2 = {};
    props2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    props2.pNext = &props3;
    vn_call_vkGetPhysicalDeviceFormatProperties2(&g_ring, remote_physical, image_state.format, &props2);
    VkFormatFeatureFlags2 fmt_features = image_state.tiling == VK_IMAGE_TILING_LINEAR
                                             ? props3.linearTilingFeatures
                                             : props3.optimalTilingFeatures;
    if ((fmt_features & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT) == 0) {
        ICD_LOG_ERROR() << "[Client ICD] Image format lacks HOST_IMAGE_TRANSFER support\n";
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    std::vector<VkMemoryToImageCopyMESA> mesa_regions(pCopyMemoryToImageInfo->regionCount);
    std::vector<std::vector<uint8_t>> region_blobs(pCopyMemoryToImageInfo->regionCount);

    for (uint32_t i = 0; i < pCopyMemoryToImageInfo->regionCount; ++i) {
        const VkMemoryToImageCopy& region = pCopyMemoryToImageInfo->pRegions[i];
        if (!region.pHostPointer) {
            ICD_LOG_ERROR() << "[Client ICD] Region " << i << " missing host pointer\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        HostImageCopyLayout layout = {};
        VkDeviceSize region_size = 0;
        if (!compute_host_image_copy_size(image_state.format,
                                          region.imageExtent,
                                          region.memoryRowLength,
                                          region.memoryImageHeight,
                                          region.imageSubresource.layerCount,
                                          &layout,
                                          &region_size)) {
            ICD_LOG_ERROR() << "[Client ICD] Failed to compute copy size for region " << i << "\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        region_blobs[i].resize(static_cast<size_t>(region_size));
        if (region_size > 0) {
            memcpy(region_blobs[i].data(), region.pHostPointer, static_cast<size_t>(region_size));
        }

        VkMemoryToImageCopyMESA mesa_region = {};
        mesa_region.sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_MESA;
        mesa_region.pNext = region.pNext;
        mesa_region.dataSize = region_size;
        mesa_region.pData = region_blobs[i].data();
        mesa_region.memoryRowLength = region.memoryRowLength;
        mesa_region.memoryImageHeight = region.memoryImageHeight;
        mesa_region.imageSubresource = region.imageSubresource;
        mesa_region.imageOffset = region.imageOffset;
        mesa_region.imageExtent = region.imageExtent;
        mesa_regions[i] = mesa_region;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkCopyMemoryToImageInfoMESA mesa_info = {};
    mesa_info.sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_MESA;
    mesa_info.pNext = pCopyMemoryToImageInfo->pNext;
    mesa_info.flags = pCopyMemoryToImageInfo->flags;
    mesa_info.dstImage = remote_image;
    mesa_info.dstImageLayout = pCopyMemoryToImageInfo->dstImageLayout;
    mesa_info.regionCount = pCopyMemoryToImageInfo->regionCount;
    mesa_info.pRegions = mesa_regions.data();

    VkResult ret = vn_call_vkCopyMemoryToImageMESA(&g_ring, icd_device->remote_handle, &mesa_info);
    if (ret != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] vkCopyMemoryToImageMESA failed: " << ret << "\n";
    }
    return ret;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToMemory(
    VkDevice device,
    const VkCopyImageToMemoryInfo* pCopyImageToMemoryInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCopyImageToMemory called\n";

    if (!pCopyImageToMemoryInfo || !pCopyImageToMemoryInfo->pRegions ||
        pCopyImageToMemoryInfo->regionCount == 0) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCopyImageToMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_DEVICE_LOST;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCopyImageToMemory\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const VkPhysicalDeviceVulkan14Features* vk14 = g_device_state.get_vk14_features(device);
    if (!vk14 || !vk14->hostImageCopy) {
        ICD_LOG_ERROR() << "[Client ICD] hostImageCopy feature not enabled\n";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    venus_plus::ImageState image_state = {};
    if (!g_resource_state.get_image_info(pCopyImageToMemoryInfo->srcImage, &image_state)) {
        ICD_LOG_ERROR() << "[Client ICD] Source image not tracked\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImage remote_image = image_state.remote_handle;
    if (remote_image == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Source image missing remote handle\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice remote_physical = g_device_state.get_device_physical_device(device);
    VkFormatProperties3 props3 = {};
    props3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
    VkFormatProperties2 props2 = {};
    props2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    props2.pNext = &props3;
    vn_call_vkGetPhysicalDeviceFormatProperties2(&g_ring, remote_physical, image_state.format, &props2);
    VkFormatFeatureFlags2 fmt_features = image_state.tiling == VK_IMAGE_TILING_LINEAR
                                             ? props3.linearTilingFeatures
                                             : props3.optimalTilingFeatures;
    if ((fmt_features & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT) == 0) {
        ICD_LOG_ERROR() << "[Client ICD] Image format lacks HOST_IMAGE_TRANSFER support\n";
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    VkResult ret = VK_SUCCESS;

    for (uint32_t i = 0; i < pCopyImageToMemoryInfo->regionCount; ++i) {
        const VkImageToMemoryCopy& region = pCopyImageToMemoryInfo->pRegions[i];
        if (!region.pHostPointer) {
            ICD_LOG_ERROR() << "[Client ICD] Region " << i << " missing host pointer\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        HostImageCopyLayout layout = {};
        VkDeviceSize region_size = 0;
        if (!compute_host_image_copy_size(image_state.format,
                                          region.imageExtent,
                                          region.memoryRowLength,
                                          region.memoryImageHeight,
                                          region.imageSubresource.layerCount,
                                          &layout,
                                          &region_size)) {
            ICD_LOG_ERROR() << "[Client ICD] Failed to compute copy size for region " << i << "\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        std::vector<uint8_t> region_data(static_cast<size_t>(region_size));

        VkCopyImageToMemoryInfoMESA mesa_info = {};
        mesa_info.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_MEMORY_INFO_MESA;
        mesa_info.pNext = pCopyImageToMemoryInfo->pNext;
        mesa_info.flags = pCopyImageToMemoryInfo->flags;
        mesa_info.srcImage = remote_image;
        mesa_info.srcImageLayout = pCopyImageToMemoryInfo->srcImageLayout;
        mesa_info.memoryRowLength = region.memoryRowLength;
        mesa_info.memoryImageHeight = region.memoryImageHeight;
        mesa_info.imageSubresource = region.imageSubresource;
        mesa_info.imageOffset = region.imageOffset;
        mesa_info.imageExtent = region.imageExtent;

        ret = vn_call_vkCopyImageToMemoryMESA(&g_ring,
                                              icd_device->remote_handle,
                                              &mesa_info,
                                              static_cast<size_t>(region_size),
                                              region_data.data());
        if (ret != VK_SUCCESS) {
            ICD_LOG_ERROR() << "[Client ICD] vkCopyImageToMemoryMESA failed for region " << i
                            << ": " << ret << "\n";
            return ret;
        }

        if (region_size > 0) {
            memcpy(region.pHostPointer, region_data.data(), static_cast<size_t>(region_size));
        }
    }

    return ret;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCopyImageToImage(
    VkDevice device,
    const VkCopyImageToImageInfo* pCopyImageToImageInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkCopyImageToImage called\n";

    if (!pCopyImageToImageInfo || pCopyImageToImageInfo->regionCount == 0 ||
        !pCopyImageToImageInfo->pRegions) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid parameters for vkCopyImageToImage\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const VkPhysicalDeviceVulkan14Features* vk14 = g_device_state.get_vk14_features(device);
    if (!vk14 || !vk14->hostImageCopy) {
        ICD_LOG_ERROR() << "[Client ICD] hostImageCopy feature not enabled\n";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCopyImageToImage\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_DEVICE_LOST;
    }

    VkImage src_remote = g_resource_state.get_remote_image(pCopyImageToImageInfo->srcImage);
    VkImage dst_remote = g_resource_state.get_remote_image(pCopyImageToImageInfo->dstImage);
    if (src_remote == VK_NULL_HANDLE || dst_remote == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Images not tracked in vkCopyImageToImage\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    venus_plus::ImageState src_info = {};
    venus_plus::ImageState dst_info = {};
    if (!g_resource_state.get_image_info(pCopyImageToImageInfo->srcImage, &src_info) ||
        !g_resource_state.get_image_info(pCopyImageToImageInfo->dstImage, &dst_info)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to fetch image info for host image copy\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice remote_physical = g_device_state.get_device_physical_device(device);
    auto format_supported = [&](const venus_plus::ImageState& info) {
        VkFormatProperties3 props3 = {};
        props3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
        VkFormatProperties2 props2 = {};
        props2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
        props2.pNext = &props3;
        vn_call_vkGetPhysicalDeviceFormatProperties2(&g_ring, remote_physical, info.format, &props2);
        VkFormatFeatureFlags2 feats = info.tiling == VK_IMAGE_TILING_LINEAR
                                          ? props3.linearTilingFeatures
                                          : props3.optimalTilingFeatures;
        return (feats & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT) != 0;
    };

    if (!format_supported(src_info) || !format_supported(dst_info)) {
        ICD_LOG_ERROR() << "[Client ICD] Source or destination format lacks HOST_IMAGE_TRANSFER support\n";
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    if (!icd_device) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkCopyImageToImage\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCopyImageToImageInfo remote_info = *pCopyImageToImageInfo;
    remote_info.srcImage = src_remote;
    remote_info.dstImage = dst_remote;

    return vn_call_vkCopyImageToImage(&g_ring, icd_device->remote_handle, &remote_info);
}

VKAPI_ATTR VkResult VKAPI_CALL vkTransitionImageLayout(
    VkDevice device,
    uint32_t transitionCount,
    const VkHostImageLayoutTransitionInfo* pTransitions) {

    ICD_LOG_INFO() << "[Client ICD] vkTransitionImageLayout called (count=" << transitionCount << ")\n";

    if (transitionCount == 0 || !pTransitions) {
        ICD_LOG_ERROR() << "[Client ICD] Missing transitions for vkTransitionImageLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const VkPhysicalDeviceVulkan14Features* vk14 = g_device_state.get_vk14_features(device);
    if (!vk14 || !vk14->hostImageCopy) {
        ICD_LOG_ERROR() << "[Client ICD] hostImageCopy feature not enabled\n";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_DEVICE_LOST;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkTransitionImageLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    if (!icd_device) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkTransitionImageLayout\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkHostImageLayoutTransitionInfo> transitions(transitionCount);
    VkPhysicalDevice remote_physical = g_device_state.get_device_physical_device(device);

    for (uint32_t i = 0; i < transitionCount; ++i) {
        transitions[i] = pTransitions[i];
        venus_plus::ImageState image_state = {};
        if (!g_resource_state.get_image_info(pTransitions[i].image, &image_state)) {
            ICD_LOG_ERROR() << "[Client ICD] Image not tracked in vkTransitionImageLayout\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        transitions[i].image = image_state.remote_handle;
        if (transitions[i].image == VK_NULL_HANDLE) {
            ICD_LOG_ERROR() << "[Client ICD] Remote image handle missing in vkTransitionImageLayout\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        VkFormatProperties3 props3 = {};
        props3.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3;
        VkFormatProperties2 props2 = {};
        props2.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
        props2.pNext = &props3;
        vn_call_vkGetPhysicalDeviceFormatProperties2(&g_ring, remote_physical, image_state.format, &props2);
        VkFormatFeatureFlags2 fmt_features = image_state.tiling == VK_IMAGE_TILING_LINEAR
                                                 ? props3.linearTilingFeatures
                                                 : props3.optimalTilingFeatures;
        if ((fmt_features & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT) == 0) {
            ICD_LOG_ERROR() << "[Client ICD] Image format lacks HOST_IMAGE_TRANSFER support\n";
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }
    }

    return vn_call_vkTransitionImageLayout(&g_ring,
                                           icd_device->remote_handle,
                                           transitionCount,
                                           transitions.data());
}

} // extern "C"
