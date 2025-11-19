#ifndef VENUS_PLUS_FRAME_TRANSFER_PROTOCOL_H
#define VENUS_PLUS_FRAME_TRANSFER_PROTOCOL_H

#include <cstdint>
#include <vulkan/vulkan.h>

namespace venus_plus {

enum VenusPlusCommandType : uint32_t {
    VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA = 0x10000000u,
    VENUS_PLUS_CMD_READ_MEMORY_DATA     = 0x10000001u,

    VENUS_PLUS_CMD_CREATE_SWAPCHAIN     = 0x10000010u,
    VENUS_PLUS_CMD_DESTROY_SWAPCHAIN    = 0x10000011u,
    VENUS_PLUS_CMD_ACQUIRE_IMAGE        = 0x10000012u,
    VENUS_PLUS_CMD_PRESENT              = 0x10000013u,
};

struct VenusSwapchainCreateInfo {
    uint32_t swapchain_id;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t image_count;
    uint32_t usage;
    uint32_t present_mode;
};

struct VenusSwapchainCreateRequest {
    uint32_t command; // VenusPlusCommandType
    VenusSwapchainCreateInfo create_info;
};

struct VenusSwapchainCreateReply {
    VkResult result;
    uint32_t actual_image_count;
};

struct VenusSwapchainDestroyRequest {
    uint32_t command;      // VenusPlusCommandType
    uint32_t swapchain_id;
};

struct VenusSwapchainAcquireRequest {
    uint32_t command;      // VenusPlusCommandType
    uint32_t swapchain_id;
    uint64_t timeout;
};

struct VenusSwapchainAcquireReply {
    VkResult result;
    uint32_t image_index;
};

struct VenusSwapchainPresentRequest {
    uint32_t command;      // VenusPlusCommandType
    uint32_t swapchain_id;
    uint32_t image_index;
};

static constexpr uint32_t kVenusFrameMagic = 0x56504652u; // "VPFR"

enum class FrameCompressionType : uint32_t {
    NONE = 0,
    RLE = 3, // simple run-length encoding placeholder
};

struct VenusFrameHeader {
    uint32_t magic;
    uint32_t swapchain_id;
    uint32_t image_index;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    FrameCompressionType compression;
    uint32_t payload_size;
    uint32_t uncompressed_size;
    uint32_t stride;
};

struct VenusSwapchainPresentReply {
    VkResult result;
    VenusFrameHeader frame;
    // Followed by |payload_size| bytes if result == VK_SUCCESS.
};

} // namespace venus_plus

#endif // VENUS_PLUS_FRAME_TRANSFER_PROTOCOL_H
