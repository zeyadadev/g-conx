#ifndef VENUS_PLUS_COMMAND_BUFFER_STATE_H
#define VENUS_PLUS_COMMAND_BUFFER_STATE_H

#include <vulkan/vulkan.h>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace venus_plus {

enum class CommandBufferLifecycleState {
    INITIAL,
    RECORDING,
    EXECUTABLE,
    INVALID,
};

struct CommandPoolInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool remote_handle = VK_NULL_HANDLE;
    VkCommandPoolCreateFlags flags = 0;
    uint32_t queue_family_index = 0;
    std::vector<VkCommandBuffer> command_buffers;
};

struct CommandBufferInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer remote_handle = VK_NULL_HANDLE;
    VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CommandBufferLifecycleState state = CommandBufferLifecycleState::INITIAL;
    VkCommandBufferUsageFlags usage_flags = 0;
    struct BoundDescriptorState {
        bool valid = false;
        VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_MAX_ENUM;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        uint32_t first_set = 0;
        std::vector<VkDescriptorSet> sets;
        std::vector<uint32_t> dynamic_offsets;
    } bound_descriptors;
};

class CommandBufferState {
public:
    void add_pool(VkDevice device, VkCommandPool local, VkCommandPool remote, const VkCommandPoolCreateInfo& info);
    bool remove_pool(VkCommandPool pool, std::vector<VkCommandBuffer>* buffers_to_free);
    bool has_pool(VkCommandPool pool) const;
    VkCommandPool get_remote_pool(VkCommandPool pool) const;
    VkDevice get_pool_device(VkCommandPool pool) const;
    VkCommandPoolCreateFlags get_pool_flags(VkCommandPool pool) const;
    void reset_pool(VkCommandPool pool);

    void add_command_buffer(VkCommandPool pool, VkCommandBuffer local, VkCommandBuffer remote, VkCommandBufferLevel level);
    bool remove_command_buffer(VkCommandBuffer buffer);
    bool has_command_buffer(VkCommandBuffer buffer) const;
    VkCommandPool get_buffer_pool(VkCommandBuffer buffer) const;
    VkCommandBuffer get_remote_command_buffer(VkCommandBuffer buffer) const;
    VkDevice get_buffer_device(VkCommandBuffer buffer) const;
    CommandBufferLifecycleState get_buffer_state(VkCommandBuffer buffer) const;
    void set_buffer_state(VkCommandBuffer buffer, CommandBufferLifecycleState state);
    VkCommandBufferUsageFlags get_usage_flags(VkCommandBuffer buffer) const;
    void set_usage_flags(VkCommandBuffer buffer, VkCommandBufferUsageFlags flags);

    // Returns true if the incoming bind differs from the cached state and updates the cache.
    bool update_descriptor_bind_state(VkCommandBuffer buffer,
                                      VkPipelineBindPoint bind_point,
                                      VkPipelineLayout layout,
                                      uint32_t first_set,
                                      uint32_t descriptor_set_count,
                                      const VkDescriptorSet* sets,
                                      uint32_t dynamic_offset_count,
                                      const uint32_t* dynamic_offsets);
    void clear_descriptor_bind_state(VkCommandBuffer buffer);

    void remove_device(VkDevice device,
                       std::vector<VkCommandBuffer>* buffers_to_free,
                       std::vector<VkCommandPool>* pools_removed);

private:
    template <typename T>
    static uint64_t handle_key(T handle) {
        return reinterpret_cast<uint64_t>(handle);
    }

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, CommandPoolInfo> pools_;
    std::unordered_map<uint64_t, CommandBufferInfo> buffers_;
};

extern CommandBufferState g_command_buffer_state;

} // namespace venus_plus

#endif // VENUS_PLUS_COMMAND_BUFFER_STATE_H
