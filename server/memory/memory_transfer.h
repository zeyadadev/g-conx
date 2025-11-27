#ifndef VENUS_PLUS_MEMORY_TRANSFER_HANDLER_H
#define VENUS_PLUS_MEMORY_TRANSFER_HANDLER_H

#include <cstddef>
#include <vector>
#include <vulkan/vulkan.h>

#include "protocol/memory_transfer.h"

struct ServerState;

namespace venus_plus {

class MemoryTransferHandler {
public:
    explicit MemoryTransferHandler(ServerState* state);

    VkResult handle_transfer_command(const void* data, size_t size);
    VkResult handle_transfer_batch_command(const void* data, size_t size);
    VkResult handle_read_command(const void* data, size_t size, std::vector<uint8_t>* out_payload);
    VkResult handle_read_batch_command(const void* data,
                                       size_t size,
                                       std::vector<uint8_t>* out_payload);

private:
    VkResult write_memory(const TransferMemoryDataHeader& header, const uint8_t* payload, size_t payload_size);
    VkResult read_memory(const ReadMemoryDataRequest& request, std::vector<uint8_t>* out_payload);

    ServerState* state_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_MEMORY_TRANSFER_HANDLER_H
