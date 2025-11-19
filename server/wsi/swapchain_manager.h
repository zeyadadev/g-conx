#ifndef VENUS_PLUS_SERVER_SWAPCHAIN_MANAGER_H
#define VENUS_PLUS_SERVER_SWAPCHAIN_MANAGER_H

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "protocol/frame_transfer.h"

namespace venus_plus {

struct ServerSwapchain {
    uint32_t id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t image_count = 0;
    uint32_t next_image = 0;
    std::vector<std::vector<uint8_t>> image_storage;
};

class ServerSwapchainManager {
public:
    VkResult create_swapchain(const VenusSwapchainCreateInfo& info, uint32_t* actual_image_count);
    void destroy_swapchain(uint32_t id);
    VkResult acquire_image(uint32_t id, uint32_t* image_index);
    VkResult present(uint32_t id,
                     uint32_t image_index,
                     VenusFrameHeader* header,
                     std::vector<uint8_t>* payload);

private:
    std::vector<uint8_t> generate_frame(const ServerSwapchain& swapchain, uint32_t image_index) const;
    void compress_frame(const std::vector<uint8_t>& input,
                        std::vector<uint8_t>* output,
                        FrameCompressionType* mode) const;

    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, ServerSwapchain> swapchains_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_SERVER_SWAPCHAIN_MANAGER_H
