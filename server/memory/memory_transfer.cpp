#include "memory_transfer.h"

#include <cstring>
#include <limits>

#include "server_state.h"
#include "utils/logging.h"

#define MEMORY_LOG_ERROR() VP_LOG_STREAM_ERROR(MEMORY)

namespace venus_plus {

MemoryTransferHandler::MemoryTransferHandler(ServerState* state)
    : state_(state) {}

VkResult MemoryTransferHandler::handle_transfer_command(const void* data, size_t size) {
    if (!state_ || !data || size < sizeof(TransferMemoryDataHeader)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    TransferMemoryDataHeader header = {};
    std::memcpy(&header, data, sizeof(header));
    if (header.command != VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const size_t payload_size = size - sizeof(header);
    if (header.size != static_cast<uint64_t>(payload_size)) {
        MEMORY_LOG_ERROR() << "Transfer payload size mismatch";
        return VK_ERROR_UNKNOWN;
    }

    if (payload_size > std::numeric_limits<size_t>::max()) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    const uint8_t* payload = reinterpret_cast<const uint8_t*>(data) + sizeof(header);
    return write_memory(header, payload, payload_size);
}

VkResult MemoryTransferHandler::handle_read_command(const void* data,
                                                    size_t size,
                                                    std::vector<uint8_t>* out_payload) {
    if (!state_ || !out_payload || !data || size < sizeof(ReadMemoryDataRequest)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ReadMemoryDataRequest request = {};
    std::memcpy(&request, data, sizeof(request));
    if (request.command != VENUS_PLUS_CMD_READ_MEMORY_DATA) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    return read_memory(request, out_payload);
}

VkResult MemoryTransferHandler::write_memory(const TransferMemoryDataHeader& header,
                                             const uint8_t* payload,
                                             size_t payload_size) {
    VkDeviceMemory real_memory = VK_NULL_HANDLE;
    VkDevice real_device = VK_NULL_HANDLE;
    VkDeviceSize allocation_size = 0;
    uint32_t type_index = 0;

    if (!state_->resource_tracker.get_memory_info(reinterpret_cast<VkDeviceMemory>(header.memory_handle),
                                                  &real_memory,
                                                  &real_device,
                                                  &allocation_size,
                                                  &type_index)) {
        MEMORY_LOG_ERROR() << "Unknown memory handle in transfer";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    const uint64_t end = header.offset + header.size;
    if (end > allocation_size) {
        MEMORY_LOG_ERROR() << "Transfer range exceeds allocation";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (header.size == 0 || payload_size == 0) {
        return VK_SUCCESS;
    }

    void* mapped = nullptr;
    VkResult result = vkMapMemory(real_device,
                                  real_memory,
                                  header.offset,
                                  header.size,
                                  0,
                                  &mapped);
    if (result != VK_SUCCESS) {
        MEMORY_LOG_ERROR() << "vkMapMemory failed for transfer: " << result;
        return result;
    }

    std::memcpy(mapped, payload, payload_size);

    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = real_memory;
    range.offset = header.offset;
    range.size = header.size;
    vkFlushMappedMemoryRanges(real_device, 1, &range);

    vkUnmapMemory(real_device, real_memory);
    return VK_SUCCESS;
}

VkResult MemoryTransferHandler::read_memory(const ReadMemoryDataRequest& request,
                                            std::vector<uint8_t>* out_payload) {
    out_payload->clear();

    VkDeviceMemory real_memory = VK_NULL_HANDLE;
    VkDevice real_device = VK_NULL_HANDLE;
    VkDeviceSize allocation_size = 0;
    uint32_t type_index = 0;

    if (!state_->resource_tracker.get_memory_info(reinterpret_cast<VkDeviceMemory>(request.memory_handle),
                                                  &real_memory,
                                                  &real_device,
                                                  &allocation_size,
                                                  &type_index)) {
        MEMORY_LOG_ERROR() << "Unknown memory handle in read";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    const uint64_t end = request.offset + request.size;
    if (end > allocation_size) {
        MEMORY_LOG_ERROR() << "Read range exceeds allocation";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (request.size == 0) {
        return VK_SUCCESS;
    }

    if (request.size > std::numeric_limits<size_t>::max()) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    out_payload->resize(static_cast<size_t>(request.size));

    void* mapped = nullptr;
    VkResult result = vkMapMemory(real_device,
                                  real_memory,
                                  request.offset,
                                  request.size,
                                  0,
                                  &mapped);
    if (result != VK_SUCCESS) {
        MEMORY_LOG_ERROR() << "vkMapMemory failed for read: " << result;
        out_payload->clear();
        return result;
    }

    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = real_memory;
    range.offset = request.offset;
    range.size = request.size;
    vkInvalidateMappedMemoryRanges(real_device, 1, &range);

    std::memcpy(out_payload->data(), mapped, out_payload->size());

    vkUnmapMemory(real_device, real_memory);
    return VK_SUCCESS;
}

} // namespace venus_plus
