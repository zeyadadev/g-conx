#ifndef VENUS_PLUS_SERVER_COMMAND_BUFFER_STATE_H
#define VENUS_PLUS_SERVER_COMMAND_BUFFER_STATE_H

#include <vulkan/vulkan.h>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace venus_plus {

enum class ServerCommandBufferState {
    INITIAL,
    RECORDING,
    EXECUTABLE,
    INVALID,
};

class CommandBufferState {
public:
    CommandBufferState();

    VkCommandPool create_pool(VkDevice device, const VkCommandPoolCreateInfo& info);
    bool destroy_pool(VkCommandPool pool);
    VkResult reset_pool(VkCommandPool pool, VkCommandPoolResetFlags flags);

    VkResult allocate_command_buffers(VkDevice device,
                                      const VkCommandBufferAllocateInfo& info,
                                      std::vector<VkCommandBuffer>* out_buffers);
    void free_command_buffers(VkCommandPool pool, const std::vector<VkCommandBuffer>& buffers);

    VkResult begin(VkCommandBuffer buffer, const VkCommandBufferBeginInfo* info);
    VkResult end(VkCommandBuffer buffer);
    VkResult reset_buffer(VkCommandBuffer buffer, VkCommandBufferResetFlags flags);

    bool is_recording(VkCommandBuffer buffer) const;
    bool buffer_exists(VkCommandBuffer buffer) const;
    void invalidate(VkCommandBuffer buffer);
    ServerCommandBufferState get_state(VkCommandBuffer buffer) const;

private:
    struct PoolEntry {
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPoolCreateFlags flags = 0;
        uint32_t queue_family_index = 0;
        std::vector<VkCommandBuffer> buffers;
    };

    struct BufferEntry {
        VkDevice device = VK_NULL_HANDLE;
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ServerCommandBufferState state = ServerCommandBufferState::INITIAL;
    };

    template <typename T>
    static uint64_t handle_key(T handle) {
        return reinterpret_cast<uint64_t>(handle);
    }

    VkCommandPool new_command_pool_handle();
    VkCommandBuffer new_command_buffer_handle();

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, PoolEntry> pools_;
    std::unordered_map<uint64_t, BufferEntry> buffers_;
    uint64_t next_pool_handle_;
    uint64_t next_buffer_handle_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_SERVER_COMMAND_BUFFER_STATE_H
