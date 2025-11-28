#ifndef VENUS_PLUS_REMOTE_PERF_PROTOCOL_H
#define VENUS_PLUS_REMOTE_PERF_PROTOCOL_H

#include <cstdint>
#include <vulkan/vulkan.h>

namespace venus_plus {

// Flags for coalesced submit/wait envelopes
static constexpr uint32_t kVenusCoalesceFlagTransfer = 0x1;
static constexpr uint32_t kVenusCoalesceFlagCommand  = 0x2;
static constexpr uint32_t kVenusCoalesceFlagInvalidate = 0x4;

struct SubmitCoalesceHeader {
    uint32_t command;       // VenusPlusCommandType
    uint32_t flags;         // bitmask of kVenusCoalesceFlag*
    uint32_t transfer_size; // bytes of TransferMemoryBatch payload (may be 0)
    uint32_t command_size;  // bytes of Venus command stream
};

struct SubmitCoalesceReplyHeader {
    VkResult transfer_result;
    uint32_t command_reply_size;
};

struct WaitInvalidateHeader {
    uint32_t command;           // VenusPlusCommandType
    uint32_t flags;             // bitmask of kVenusCoalesceFlag*
    uint32_t wait_command_size; // bytes of Venus wait command stream
    uint32_t invalidate_size;   // bytes of ReadMemoryBatch request (may be 0)
};

struct WaitInvalidateReplyHeader {
    uint32_t wait_reply_size;      // bytes of Venus reply payload
    uint32_t invalidate_reply_size; // bytes of ReadMemoryBatch reply payload
};

} // namespace venus_plus

#endif // VENUS_PLUS_REMOTE_PERF_PROTOCOL_H
