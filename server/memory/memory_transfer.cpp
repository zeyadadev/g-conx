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

VkResult MemoryTransferHandler::handle_transfer_batch_command(const void* data, size_t size) {
    if (!state_ || !data || size < sizeof(TransferMemoryBatchHeader)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    TransferMemoryBatchHeader header = {};
    std::memcpy(&header, data, sizeof(header));
    if (header.command != VENUS_PLUS_CMD_TRANSFER_MEMORY_BATCH) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const size_t range_bytes = static_cast<size_t>(header.range_count) * sizeof(TransferMemoryRange);
    const size_t min_size = sizeof(TransferMemoryBatchHeader) + range_bytes;
    if (size < min_size) {
        MEMORY_LOG_ERROR() << "Transfer batch payload too small";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const uint8_t* range_bytes_ptr =
        static_cast<const uint8_t*>(data) + sizeof(TransferMemoryBatchHeader);
    const uint8_t* payload = static_cast<const uint8_t*>(data) + min_size;
    size_t payload_size = size - min_size;

    size_t consumed = 0;
    for (uint32_t i = 0; i < header.range_count; ++i) {
        const auto* src = range_bytes_ptr + static_cast<size_t>(i) * sizeof(TransferMemoryRange);
        TransferMemoryRange range = {};
        std::memcpy(&range, src, sizeof(TransferMemoryRange));
        if (range.size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            MEMORY_LOG_ERROR() << "Transfer batch range too large";
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        if (consumed + static_cast<size_t>(range.size) > payload_size) {
            MEMORY_LOG_ERROR() << "Transfer batch payload truncated";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        TransferMemoryDataHeader single = {};
        single.command = VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA;
        single.memory_handle = range.memory_handle;
        single.offset = range.offset;
        single.size = range.size;
        VkResult result = write_memory(
            single,
            payload + consumed,
            static_cast<size_t>(range.size));
        if (result != VK_SUCCESS) {
            return result;
        }
        consumed += static_cast<size_t>(range.size);
    }

    return VK_SUCCESS;
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

VkResult MemoryTransferHandler::handle_read_batch_command(const void* data,
                                                          size_t size,
                                                          std::vector<uint8_t>* out_payload) {
    if (!state_ || !out_payload || !data || size < sizeof(ReadMemoryBatchHeader)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    ReadMemoryBatchHeader header = {};
    std::memcpy(&header, data, sizeof(header));
    if (header.command != VENUS_PLUS_CMD_READ_MEMORY_BATCH) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const size_t range_bytes = static_cast<size_t>(header.range_count) * sizeof(ReadMemoryRange);
    const size_t min_size = sizeof(ReadMemoryBatchHeader) + range_bytes;
    if (size < min_size) {
        MEMORY_LOG_ERROR() << "Read batch payload too small";
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const uint8_t* range_ptr =
        static_cast<const uint8_t*>(data) + sizeof(ReadMemoryBatchHeader);
    std::vector<ReadMemoryRange> ranges(header.range_count);
    std::memcpy(ranges.data(), range_ptr, range_bytes);

    size_t total_size = 0;
    for (const auto& range : ranges) {
        if (range.size > std::numeric_limits<size_t>::max()) {
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        total_size += static_cast<size_t>(range.size);
    }

    ReadMemoryBatchReplyHeader reply_header = {};
    reply_header.result = VK_SUCCESS;
    reply_header.range_count = header.range_count;
    out_payload->assign(reinterpret_cast<uint8_t*>(&reply_header),
                        reinterpret_cast<uint8_t*>(&reply_header) + sizeof(reply_header));
    out_payload->resize(sizeof(reply_header) + total_size);

    size_t offset = sizeof(reply_header);
    for (const auto& range : ranges) {
        ReadMemoryDataRequest req = {};
        req.command = VENUS_PLUS_CMD_READ_MEMORY_DATA;
        req.memory_handle = range.memory_handle;
        req.offset = range.offset;
        req.size = range.size;

        std::vector<uint8_t> tmp;
        VkResult result = read_memory(req, &tmp);
        if (result != VK_SUCCESS) {
            reply_header.result = result;
            std::memcpy(out_payload->data(), &reply_header, sizeof(reply_header));
            out_payload->resize(sizeof(reply_header));
            return result;
        }
        if (tmp.size() != static_cast<size_t>(range.size)) {
            reply_header.result = VK_ERROR_MEMORY_MAP_FAILED;
            std::memcpy(out_payload->data(), &reply_header, sizeof(reply_header));
            out_payload->resize(sizeof(reply_header));
            return reply_header.result;
        }
        std::memcpy(out_payload->data() + offset, tmp.data(), tmp.size());
        offset += tmp.size();
    }

    std::memcpy(out_payload->data(), &reply_header, sizeof(reply_header));
    return VK_SUCCESS;
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

    void* mapped_base = nullptr;
    VkDeviceSize mapped_size = 0;
    VkResult map_result = state_->resource_tracker.get_memory_mapping(
        reinterpret_cast<VkDeviceMemory>(header.memory_handle),
        &mapped_base,
        &mapped_size);
    if (map_result != VK_SUCCESS || !mapped_base) {
        MEMORY_LOG_ERROR() << "Failed to map memory for transfer: " << map_result;
        return map_result;
    }

    if (header.offset + header.size > mapped_size) {
        MEMORY_LOG_ERROR() << "Transfer range exceeds mapped size";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    uint8_t* dst = static_cast<uint8_t*>(mapped_base) + header.offset;
    std::memcpy(dst, payload, payload_size);

    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = real_memory;
    range.offset = header.offset;
    range.size = header.size;
    vkFlushMappedMemoryRanges(real_device, 1, &range);
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

    void* mapped_base = nullptr;
    VkDeviceSize mapped_size = 0;
    VkResult map_result = state_->resource_tracker.get_memory_mapping(
        reinterpret_cast<VkDeviceMemory>(request.memory_handle),
        &mapped_base,
        &mapped_size);
    if (map_result != VK_SUCCESS || !mapped_base) {
        MEMORY_LOG_ERROR() << "Failed to map memory for read: " << map_result;
        out_payload->clear();
        return map_result;
    }

    VkMappedMemoryRange range = {};
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = real_memory;
    range.offset = request.offset;
    range.size = request.size;
    vkInvalidateMappedMemoryRanges(real_device, 1, &range);

    if (request.offset + request.size > mapped_size) {
        MEMORY_LOG_ERROR() << "Read range exceeds mapped size";
        out_payload->clear();
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    const uint8_t* src = static_cast<const uint8_t*>(mapped_base) + request.offset;
    std::memcpy(out_payload->data(), src, out_payload->size());
    return VK_SUCCESS;
}

} // namespace venus_plus
