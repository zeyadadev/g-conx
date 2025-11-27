#ifndef VENUS_PLUS_MEMORY_TRANSFER_PROTOCOL_H
#define VENUS_PLUS_MEMORY_TRANSFER_PROTOCOL_H

#include <cstdint>
#include <vulkan/vulkan.h>

#include "frame_transfer.h"

namespace venus_plus {

struct TransferMemoryDataHeader {
    uint32_t command;       // VenusPlusCommandType
    uint64_t memory_handle; // Client-side VkDeviceMemory
    uint64_t offset;        // Offset within memory allocation
    uint64_t size;          // Number of bytes that follow
};

struct TransferMemoryBatchHeader {
    uint32_t command;       // VenusPlusCommandType
    uint32_t range_count;   // Number of ranges in this batch
    uint32_t reserved0;
    uint32_t reserved1;
};

struct TransferMemoryRange {
    uint64_t memory_handle; // Client-side VkDeviceMemory
    uint64_t offset;        // Offset within memory allocation
    uint64_t size;          // Number of bytes for this range
};

struct ReadMemoryDataRequest {
    uint32_t command;       // VenusPlusCommandType
    uint64_t memory_handle; // Client-side VkDeviceMemory
    uint64_t offset;        // Offset to read from
    uint64_t size;          // Bytes requested
};

struct ReadMemoryDataResponse {
    VkResult result;
    // Followed by |size| bytes of payload if result == VK_SUCCESS.
};

} // namespace venus_plus

#endif // VENUS_PLUS_MEMORY_TRANSFER_PROTOCOL_H
