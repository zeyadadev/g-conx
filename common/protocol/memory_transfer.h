#ifndef VENUS_PLUS_MEMORY_TRANSFER_PROTOCOL_H
#define VENUS_PLUS_MEMORY_TRANSFER_PROTOCOL_H

#include <cstdint>
#include <vulkan/vulkan.h>

namespace venus_plus {

// Custom Venus Plus command identifiers for host memory transfers.
enum VenusPlusCommandType : uint32_t {
    VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA = 0x10000000u,
    VENUS_PLUS_CMD_READ_MEMORY_DATA     = 0x10000001u,
};

struct TransferMemoryDataHeader {
    uint32_t command;       // VenusPlusCommandType
    uint64_t memory_handle; // Client-side VkDeviceMemory
    uint64_t offset;        // Offset within memory allocation
    uint64_t size;          // Number of bytes that follow
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
