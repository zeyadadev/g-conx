// Memory Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"
#include "profiling.h"
#include <atomic>

VkResult send_transfer_memory_data(VkDeviceMemory memory,
                                   VkDeviceSize offset,
                                   VkDeviceSize size,
                                   const void* data);

namespace {

VkResult unmap_memory_internal(VkDevice device, VkDeviceMemory memory) {
    if (memory == VK_NULL_HANDLE) {
        return VK_SUCCESS;
    }

    ShadowBufferMapping mapping = {};
    if (!g_shadow_buffer_manager.remove_mapping(memory, &mapping)) {
        ICD_LOG_ERROR() << "[Client ICD] vkUnmapMemory: memory was not mapped\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkResult result = VK_SUCCESS;
    if (mapping.device != device) {
        ICD_LOG_ERROR() << "[Client ICD] vkUnmapMemory: device mismatch\n";
        result = VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Lost connection before flushing vkUnmapMemory\n";
        g_shadow_buffer_manager.free_mapping_resources(&mapping);
        return VK_ERROR_DEVICE_LOST;
    }

    if (mapping.size > 0 && mapping.data) {
        VkResult transfer = send_transfer_memory_data(memory, mapping.offset, mapping.size, mapping.data);
        if (transfer != VK_SUCCESS) {
            ICD_LOG_ERROR() << "[Client ICD] Failed to transfer memory on unmap: " << transfer << "\n";
            result = transfer;
        } else {
            ICD_LOG_INFO() << "[Client ICD] Transferred " << mapping.size << " bytes on unmap\n";
        }
    }

    g_shadow_buffer_manager.free_mapping_resources(&mapping);
    return result;
}

} // namespace

VkResult send_transfer_memory_data(VkDeviceMemory memory,
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

VkResult read_memory_data(VkDeviceMemory memory,
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

extern "C" {

// Vulkan function implementations

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

VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice device,
    VkDeviceMemory memory,
    const VkAllocationCallbacks* pAllocator) {

    ICD_LOG_INFO() << "[Client ICD] vkFreeMemory called\n";

    if (memory == VK_NULL_HANDLE) {
        return;
    }

    ShadowBufferMapping mapping = {};
    bool had_shadow_mapping = g_shadow_buffer_manager.remove_mapping(memory, &mapping);
    if (had_shadow_mapping) {
        ICD_LOG_ERROR() << "[Client ICD] Warning: Memory freed while still mapped, flushing shadow buffer before release\n";
    }

    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server during vkFreeMemory\n";
        if (had_shadow_mapping) {
            g_shadow_buffer_manager.free_mapping_resources(&mapping);
        }
        g_resource_state.remove_memory(memory);
        return;
    }

    if (had_shadow_mapping) {
        if (mapping.device != device) {
            ICD_LOG_ERROR() << "[Client ICD] vkFreeMemory: device mismatch for mapped memory\n";
        }

        if (mapping.size > 0 && mapping.data) {
            VkResult flush_result =
                send_transfer_memory_data(memory, mapping.offset, mapping.size, mapping.data);
            if (flush_result != VK_SUCCESS) {
                ICD_LOG_ERROR() << "[Client ICD] Failed to flush mapped memory before free: "
                                << flush_result << "\n";
            } else {
                ICD_LOG_INFO() << "[Client ICD] Flushed " << mapping.size
                               << " bytes before vkFreeMemory\n";
            }
        }

        g_shadow_buffer_manager.free_mapping_resources(&mapping);
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

VKAPI_ATTR void VKAPI_CALL vkGetDeviceMemoryCommitment(
    VkDevice device,
    VkDeviceMemory memory,
    VkDeviceSize* pCommittedMemoryInBytes) {

    ICD_LOG_INFO() << "[Client ICD] vkGetDeviceMemoryCommitment called\n";

    if (!pCommittedMemoryInBytes) {
        ICD_LOG_ERROR() << "[Client ICD] pCommittedMemoryInBytes is NULL\n";
        return;
    }
    *pCommittedMemoryInBytes = 0;

    if (!g_resource_state.has_memory(memory)) {
        ICD_LOG_ERROR() << "[Client ICD] Memory not tracked in vkGetDeviceMemoryCommitment\n";
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return;
    }

    if (!g_device_state.has_device(device)) {
        ICD_LOG_ERROR() << "[Client ICD] Unknown device in vkGetDeviceMemoryCommitment\n";
        return;
    }

    VkDeviceMemory remote_memory = g_resource_state.get_remote_memory(memory);
    if (remote_memory == VK_NULL_HANDLE) {
        ICD_LOG_ERROR() << "[Client ICD] Remote memory handle missing in vkGetDeviceMemoryCommitment\n";
        return;
    }

    IcdDevice* icd_device = icd_device_from_handle(device);
    vn_call_vkGetDeviceMemoryCommitment(&g_ring,
                                        icd_device->remote_handle,
                                        remote_memory,
                                        pCommittedMemoryInBytes);
    ICD_LOG_INFO() << "[Client ICD] Committed size: " << *pCommittedMemoryInBytes << "\n";
}

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
    if (!host_coherent) {
        static std::atomic<bool> warned_non_coherent{false};
        if (!warned_non_coherent.exchange(true)) {
            ICD_LOG_ERROR() << "[Client ICD] Device memory type "
                            << type_index
                            << " lacks HOST_COHERENT; applications must flush/invalidate mapped ranges for visibility\n";
        }
    }

    const VkDeviceSize kInvalidateWaitThreshold = invalidate_max_bytes();
    bool invalidate_on_wait = g_resource_state.should_invalidate_on_wait(memory);
    // If the mapped slice is small, allow automatic invalidate-on-wait even
    // when the backing allocation is large (e.g., a shared arena).
    if (size != VK_WHOLE_SIZE && size <= kInvalidateWaitThreshold) {
        invalidate_on_wait = true;
    }

    void* shadow_ptr = nullptr;
    if (!g_shadow_buffer_manager.create_mapping(device,
                                                memory,
                                                offset,
                                                size,
                                                host_coherent,
                                                invalidate_on_wait,
                                                &shadow_ptr)) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to allocate shadow buffer for mapping\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    VkResult read_result = read_memory_data(memory, offset, size, shadow_ptr);
    if (read_result != VK_SUCCESS) {
        ShadowBufferMapping mapping = {};
        g_shadow_buffer_manager.remove_mapping(memory, &mapping);
        g_shadow_buffer_manager.free_mapping_resources(&mapping);
        return read_result;
    }

    g_shadow_buffer_manager.reset_host_coherent_mapping(memory);

    *ppData = shadow_ptr;
    ICD_LOG_INFO() << "[Client ICD] Memory mapped (size=" << size << ", offset=" << offset << ")\n";
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2(
    VkDevice device,
    const VkMemoryMapInfo* pMemoryMapInfo,
    void** ppData) {

    ICD_LOG_INFO() << "[Client ICD] vkMapMemory2 called\n";

    if (!pMemoryMapInfo || !ppData) {
        ICD_LOG_ERROR() << "[Client ICD] vkMapMemory2: missing map info or ppData\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (pMemoryMapInfo->pNext) {
        ICD_LOG_ERROR() << "[Client ICD] vkMapMemory2: unsupported pNext chain\n";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    return vkMapMemory(device,
                       pMemoryMapInfo->memory,
                       pMemoryMapInfo->offset,
                       pMemoryMapInfo->size,
                       pMemoryMapInfo->flags,
                       ppData);
}

VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory2KHR(
    VkDevice device,
    const VkMemoryMapInfo* pMemoryMapInfo,
    void** ppData) {
    return vkMapMemory2(device, pMemoryMapInfo, ppData);
}

VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(
    VkDevice device,
    VkDeviceMemory memory) {

    ICD_LOG_INFO() << "[Client ICD] vkUnmapMemory called\n";

    (void)unmap_memory_internal(device, memory);
}

VKAPI_ATTR VkResult VKAPI_CALL vkUnmapMemory2(
    VkDevice device,
    const VkMemoryUnmapInfo* pMemoryUnmapInfo) {

    ICD_LOG_INFO() << "[Client ICD] vkUnmapMemory2 called\n";

    if (!pMemoryUnmapInfo) {
        ICD_LOG_ERROR() << "[Client ICD] vkUnmapMemory2: pMemoryUnmapInfo is NULL\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (pMemoryUnmapInfo->pNext) {
        ICD_LOG_ERROR() << "[Client ICD] vkUnmapMemory2: unsupported pNext chain\n";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    if (pMemoryUnmapInfo->flags != 0) {
        ICD_LOG_ERROR() << "[Client ICD] vkUnmapMemory2: flags must be zero\n";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    return unmap_memory_internal(device, pMemoryUnmapInfo->memory);
}

VKAPI_ATTR VkResult VKAPI_CALL vkUnmapMemory2KHR(
    VkDevice device,
    const VkMemoryUnmapInfo* pMemoryUnmapInfo) {
    return vkUnmapMemory2(device, pMemoryUnmapInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const VkMappedMemoryRange* pMemoryRanges) {

    VENUS_PROFILE_MEMORY_OP();

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

VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(
    VkDevice device,
    uint32_t memoryRangeCount,
    const VkMappedMemoryRange* pMemoryRanges) {

    VENUS_PROFILE_MEMORY_OP();

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

} // extern "C"
