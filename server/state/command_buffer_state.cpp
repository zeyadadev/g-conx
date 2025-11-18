#include "command_buffer_state.h"

#include <algorithm>

namespace venus_plus {

CommandBufferState::CommandBufferState()
    : next_pool_handle_(0x50000000ull),
      next_buffer_handle_(0x60000000ull) {}

VkCommandPool CommandBufferState::new_command_pool_handle() {
    return reinterpret_cast<VkCommandPool>(next_pool_handle_++);
}

VkCommandBuffer CommandBufferState::new_command_buffer_handle() {
    return reinterpret_cast<VkCommandBuffer>(next_buffer_handle_++);
}

VkCommandPool CommandBufferState::create_pool(VkDevice device, const VkCommandPoolCreateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    VkCommandPool handle = new_command_pool_handle();
    PoolEntry entry;
    entry.device = device;
    entry.flags = info.flags;
    entry.queue_family_index = info.queueFamilyIndex;
    pools_[handle_key(handle)] = entry;
    return handle;
}

bool CommandBufferState::destroy_pool(VkCommandPool pool) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto pit = pools_.find(handle_key(pool));
    if (pit == pools_.end()) {
        return false;
    }
    for (VkCommandBuffer buffer : pit->second.buffers) {
        buffers_.erase(handle_key(buffer));
    }
    pools_.erase(pit);
    return true;
}

VkResult CommandBufferState::reset_pool(VkCommandPool pool, VkCommandPoolResetFlags /*flags*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto pit = pools_.find(handle_key(pool));
    if (pit == pools_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (VkCommandBuffer buffer : pit->second.buffers) {
        auto bit = buffers_.find(handle_key(buffer));
        if (bit != buffers_.end()) {
            bit->second.state = ServerCommandBufferState::INITIAL;
        }
    }
    return VK_SUCCESS;
}

VkResult CommandBufferState::allocate_command_buffers(VkDevice device,
                                                      const VkCommandBufferAllocateInfo& info,
                                                      std::vector<VkCommandBuffer>* out_buffers) {
    if (!out_buffers) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto pit = pools_.find(handle_key(info.commandPool));
    if (pit == pools_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (pit->second.device != device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    out_buffers->clear();
    out_buffers->reserve(info.commandBufferCount);
    for (uint32_t i = 0; i < info.commandBufferCount; ++i) {
        VkCommandBuffer handle = new_command_buffer_handle();
        BufferEntry entry;
        entry.device = device;
        entry.pool = info.commandPool;
        entry.level = info.level;
        entry.state = ServerCommandBufferState::INITIAL;
        buffers_[handle_key(handle)] = entry;
        pit->second.buffers.push_back(handle);
        out_buffers->push_back(handle);
    }
    return VK_SUCCESS;
}

void CommandBufferState::free_command_buffers(VkCommandPool pool, const std::vector<VkCommandBuffer>& buffers) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto pit = pools_.find(handle_key(pool));
    if (pit != pools_.end()) {
        auto& vec = pit->second.buffers;
        for (VkCommandBuffer buffer : buffers) {
            vec.erase(std::remove(vec.begin(), vec.end(), buffer), vec.end());
        }
    }
    for (VkCommandBuffer buffer : buffers) {
        buffers_.erase(handle_key(buffer));
    }
}

VkResult CommandBufferState::begin(VkCommandBuffer buffer, const VkCommandBufferBeginInfo* info) {
    if (!info) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    switch (bit->second.state) {
        case ServerCommandBufferState::INITIAL:
            bit->second.state = ServerCommandBufferState::RECORDING;
            return VK_SUCCESS;
        case ServerCommandBufferState::EXECUTABLE:
            if (info->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) {
                bit->second.state = ServerCommandBufferState::RECORDING;
                return VK_SUCCESS;
            }
            return VK_ERROR_VALIDATION_FAILED_EXT;
        case ServerCommandBufferState::RECORDING:
            return VK_ERROR_VALIDATION_FAILED_EXT;
        case ServerCommandBufferState::INVALID:
        default:
            return VK_ERROR_INITIALIZATION_FAILED;
    }
}

VkResult CommandBufferState::end(VkCommandBuffer buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (bit->second.state != ServerCommandBufferState::RECORDING) {
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    bit->second.state = ServerCommandBufferState::EXECUTABLE;
    return VK_SUCCESS;
}

VkResult CommandBufferState::reset_buffer(VkCommandBuffer buffer, VkCommandBufferResetFlags /*flags*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto pit = pools_.find(handle_key(bit->second.pool));
    if (pit == pools_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!(pit->second.flags & VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    bit->second.state = ServerCommandBufferState::INITIAL;
    return VK_SUCCESS;
}

bool CommandBufferState::is_recording(VkCommandBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return false;
    }
    return bit->second.state == ServerCommandBufferState::RECORDING;
}

bool CommandBufferState::buffer_exists(VkCommandBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffers_.find(handle_key(buffer)) != buffers_.end();
}

void CommandBufferState::invalidate(VkCommandBuffer buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit != buffers_.end()) {
        bit->second.state = ServerCommandBufferState::INVALID;
    }
}

} // namespace venus_plus
