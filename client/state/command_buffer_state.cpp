#include "command_buffer_state.h"

#include <algorithm>

namespace venus_plus {

CommandBufferState g_command_buffer_state;

void CommandBufferState::add_pool(VkDevice device,
                                  VkCommandPool local,
                                  VkCommandPool remote,
                                  const VkCommandPoolCreateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandPoolInfo state = {};
    state.device = device;
    state.remote_handle = remote;
    state.flags = info.flags;
    state.queue_family_index = info.queueFamilyIndex;
    pools_[handle_key(local)] = state;
}

bool CommandBufferState::remove_pool(VkCommandPool pool, std::vector<VkCommandBuffer>* buffers_to_free) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(handle_key(pool));
    if (it == pools_.end()) {
        return false;
    }
    if (buffers_to_free) {
        *buffers_to_free = it->second.command_buffers;
    }
    for (VkCommandBuffer buffer : it->second.command_buffers) {
        buffers_.erase(handle_key(buffer));
    }
    pools_.erase(it);
    return true;
}

bool CommandBufferState::has_pool(VkCommandPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pools_.find(handle_key(pool)) != pools_.end();
}

VkCommandPool CommandBufferState::get_remote_pool(VkCommandPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(handle_key(pool));
    if (it == pools_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

VkDevice CommandBufferState::get_pool_device(VkCommandPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(handle_key(pool));
    if (it == pools_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.device;
}

VkCommandPoolCreateFlags CommandBufferState::get_pool_flags(VkCommandPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(handle_key(pool));
    if (it == pools_.end()) {
        return 0;
    }
    return it->second.flags;
}

void CommandBufferState::reset_pool(VkCommandPool pool) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pools_.find(handle_key(pool));
    if (it == pools_.end()) {
        return;
    }
    for (VkCommandBuffer buffer : it->second.command_buffers) {
        auto bit = buffers_.find(handle_key(buffer));
        if (bit != buffers_.end()) {
            bit->second.state = CommandBufferLifecycleState::INITIAL;
            bit->second.usage_flags = 0;
            bit->second.bound_descriptors = {};
        }
    }
}

void CommandBufferState::add_command_buffer(VkCommandPool pool,
                                            VkCommandBuffer local,
                                            VkCommandBuffer remote,
                                            VkCommandBufferLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandBufferInfo info = {};
    info.pool = pool;
    info.remote_handle = remote;
    info.level = level;
    info.state = CommandBufferLifecycleState::INITIAL;
    info.usage_flags = 0;

    auto pit = pools_.find(handle_key(pool));
    if (pit != pools_.end()) {
        info.device = pit->second.device;
        pit->second.command_buffers.push_back(local);
    }

    buffers_[handle_key(local)] = info;
}

bool CommandBufferState::remove_command_buffer(VkCommandBuffer buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return false;
    }
    auto pit = pools_.find(handle_key(bit->second.pool));
    if (pit != pools_.end()) {
        auto& list = pit->second.command_buffers;
        list.erase(std::remove(list.begin(), list.end(), buffer), list.end());
    }
    buffers_.erase(bit);
    return true;
}

bool CommandBufferState::has_command_buffer(VkCommandBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffers_.find(handle_key(buffer)) != buffers_.end();
}

VkCommandPool CommandBufferState::get_buffer_pool(VkCommandBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return VK_NULL_HANDLE;
    }
    return bit->second.pool;
}

VkCommandBuffer CommandBufferState::get_remote_command_buffer(VkCommandBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return VK_NULL_HANDLE;
    }
    return bit->second.remote_handle;
}

CommandBufferLifecycleState CommandBufferState::get_buffer_state(VkCommandBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return CommandBufferLifecycleState::INVALID;
    }
    return bit->second.state;
}

void CommandBufferState::set_buffer_state(VkCommandBuffer buffer, CommandBufferLifecycleState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return;
    }
    bit->second.state = state;
    if (state == CommandBufferLifecycleState::INVALID) {
        bit->second.usage_flags = 0;
        bit->second.bound_descriptors = {};
    }
}

VkCommandBufferUsageFlags CommandBufferState::get_usage_flags(VkCommandBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return 0;
    }
    return bit->second.usage_flags;
}

void CommandBufferState::set_usage_flags(VkCommandBuffer buffer, VkCommandBufferUsageFlags flags) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return;
    }
    bit->second.usage_flags = flags;
}

bool CommandBufferState::update_descriptor_bind_state(VkCommandBuffer buffer,
                                                      VkPipelineBindPoint bind_point,
                                                      VkPipelineLayout layout,
                                                      uint32_t first_set,
                                                      uint32_t descriptor_set_count,
                                                      const VkDescriptorSet* sets,
                                                      uint32_t dynamic_offset_count,
                                                      const uint32_t* dynamic_offsets) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return true;
    }

    if ((descriptor_set_count > 0 && !sets) ||
        (dynamic_offset_count > 0 && !dynamic_offsets)) {
        return true;
    }

    auto& cache = bit->second.bound_descriptors;
    bool changed = !cache.valid || cache.bind_point != bind_point || cache.layout != layout ||
                   cache.first_set != first_set || cache.sets.size() != descriptor_set_count ||
                   cache.dynamic_offsets.size() != dynamic_offset_count;

    if (!changed) {
        for (uint32_t i = 0; i < descriptor_set_count; ++i) {
            if (cache.sets[i] != sets[i]) {
                changed = true;
                break;
            }
        }
    }

    if (!changed) {
        for (uint32_t i = 0; i < dynamic_offset_count; ++i) {
            if (cache.dynamic_offsets[i] != dynamic_offsets[i]) {
                changed = true;
                break;
            }
        }
    }

    if (!changed) {
        return false;
    }

    cache.valid = true;
    cache.bind_point = bind_point;
    cache.layout = layout;
    cache.first_set = first_set;
    cache.sets.clear();
    if (descriptor_set_count > 0 && sets) {
        cache.sets.assign(sets, sets + descriptor_set_count);
    }

    cache.dynamic_offsets.clear();
    if (dynamic_offset_count > 0 && dynamic_offsets) {
        cache.dynamic_offsets.assign(dynamic_offsets, dynamic_offsets + dynamic_offset_count);
    }
    return true;
}

void CommandBufferState::clear_descriptor_bind_state(VkCommandBuffer buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return;
    }
    bit->second.bound_descriptors = {};
}

void CommandBufferState::remove_device(VkDevice device,
                                       std::vector<VkCommandBuffer>* buffers_to_free,
                                       std::vector<VkCommandPool>* pools_removed) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pools_.begin(); it != pools_.end();) {
        if (it->second.device != device) {
            ++it;
            continue;
        }
        VkCommandPool pool_handle = reinterpret_cast<VkCommandPool>(it->first);
        if (pools_removed) {
            pools_removed->push_back(pool_handle);
        }
        if (buffers_to_free) {
            buffers_to_free->insert(buffers_to_free->end(),
                                    it->second.command_buffers.begin(),
                                    it->second.command_buffers.end());
        }
        for (VkCommandBuffer buffer : it->second.command_buffers) {
            buffers_.erase(handle_key(buffer));
        }
        it = pools_.erase(it);
    }
}

} // namespace venus_plus
