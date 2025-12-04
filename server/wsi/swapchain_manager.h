#ifndef VENUS_PLUS_SERVER_SWAPCHAIN_MANAGER_H
#define VENUS_PLUS_SERVER_SWAPCHAIN_MANAGER_H

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "protocol/frame_transfer.h"
#include "server_state.h"

namespace venus_plus {

struct ServerSwapchain {
    uint32_t id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t image_count = 0;
    uint32_t next_image = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkDevice client_device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queue_family_index = 0;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence copy_fence = VK_NULL_HANDLE;

    struct ImageResources {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkBuffer staging_buffer = VK_NULL_HANDLE;
        VkDeviceMemory staging_memory = VK_NULL_HANDLE;
        void* staging_ptr = nullptr;
        VkDeviceSize staging_size = 0;
    };

    std::vector<ImageResources> images;
};

class ServerSwapchainManager {
public:
    explicit ServerSwapchainManager(ServerState* state);
    VkResult create_swapchain(const VenusSwapchainCreateInfo& info,
                              VenusSwapchainCreateReply* reply);
    void destroy_swapchain(uint32_t id);
    void reset();
    VkResult acquire_image(uint32_t id, uint32_t* image_index);
    VkResult present(uint32_t id,
                     uint32_t image_index,
                     VenusFrameHeader* header,
                     std::vector<uint8_t>* payload);

private:
    void compress_frame(const std::vector<uint8_t>& input,
                        std::vector<uint8_t>* output,
                        FrameCompressionType* mode) const;

    bool allocate_resources(ServerSwapchain& swapchain,
                            const VenusSwapchainCreateInfo& info,
                            VenusSwapchainCreateReply* reply);
    void free_resources(ServerSwapchain& swapchain);
    uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags flags) const;

    ServerState* state_ = nullptr;
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, ServerSwapchain> swapchains_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_SERVER_SWAPCHAIN_MANAGER_H
