#include "command_buffer_state.h"

#include <algorithm>
#include <utility>

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

VkCommandPool CommandBufferState::create_pool(VkDevice device,
                                              VkDevice real_device,
                                              const VkCommandPoolCreateInfo& info) {
    VkCommandPool real_pool = VK_NULL_HANDLE;
    VkResult result = vkCreateCommandPool(real_device, &info, nullptr, &real_pool);
    if (result != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkCommandPool handle = new_command_pool_handle();
    PoolEntry entry;
    entry.device = device;
    entry.real_device = real_device;
    entry.real_pool = real_pool;
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
    if (pit->second.real_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(pit->second.real_device, pit->second.real_pool, nullptr);
    }
    for (VkCommandBuffer buffer : pit->second.buffers) {
        buffers_.erase(handle_key(buffer));
    }
    pools_.erase(pit);
    return true;
}

VkResult CommandBufferState::reset_pool(VkCommandPool pool, VkCommandPoolResetFlags flags) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto pit = pools_.find(handle_key(pool));
    if (pit == pools_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = vkResetCommandPool(pit->second.real_device, pit->second.real_pool, flags);
    if (result != VK_SUCCESS) {
        return result;
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
                                                      VkDevice real_device,
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

    VkCommandBufferAllocateInfo real_info = info;
    real_info.commandPool = pit->second.real_pool;
    std::vector<VkCommandBuffer> real_buffers(info.commandBufferCount, VK_NULL_HANDLE);
    VkResult result = vkAllocateCommandBuffers(pit->second.real_device,
                                               &real_info,
                                               real_buffers.data());
    if (result != VK_SUCCESS) {
        return result;
    }

    out_buffers->clear();
    out_buffers->reserve(info.commandBufferCount);
    for (uint32_t i = 0; i < info.commandBufferCount; ++i) {
        VkCommandBuffer handle = new_command_buffer_handle();
        BufferEntry entry;
        entry.device = device;
        entry.real_device = pit->second.real_device;
        entry.pool = info.commandPool;
        entry.real_buffer = real_buffers[i];
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
        std::vector<VkCommandBuffer> real_buffers;
        real_buffers.reserve(buffers.size());
        for (VkCommandBuffer buffer : buffers) {
            auto bit = buffers_.find(handle_key(buffer));
            if (bit != buffers_.end()) {
                real_buffers.push_back(bit->second.real_buffer);
            }
        }
        if (!real_buffers.empty()) {
            vkFreeCommandBuffers(pit->second.real_device,
                                 pit->second.real_pool,
                                 static_cast<uint32_t>(real_buffers.size()),
                                 real_buffers.data());
        }
        auto& vec = pit->second.buffers;
        for (VkCommandBuffer buffer : buffers) {
            vec.erase(std::remove(vec.begin(), vec.end(), buffer), vec.end());
        }
    }
    for (VkCommandBuffer buffer : buffers) {
        buffers_.erase(handle_key(buffer));
    }
}

void CommandBufferState::reset() {
    std::vector<std::pair<PoolEntry, std::vector<VkCommandBuffer>>> pool_destroy_list;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_destroy_list.reserve(pools_.size());
        for (const auto& kv : pools_) {
            std::vector<VkCommandBuffer> real_buffers;
            real_buffers.reserve(kv.second.buffers.size());
            for (VkCommandBuffer buffer : kv.second.buffers) {
                auto bit = buffers_.find(handle_key(buffer));
                if (bit != buffers_.end() && bit->second.real_buffer != VK_NULL_HANDLE) {
                    real_buffers.push_back(bit->second.real_buffer);
                }
            }
            pool_destroy_list.emplace_back(kv.second, std::move(real_buffers));
        }
        pools_.clear();
        buffers_.clear();
        next_pool_handle_ = 0x50000000ull;
        next_buffer_handle_ = 0x60000000ull;
    }

    for (const auto& entry : pool_destroy_list) {
        const PoolEntry& pool_entry = entry.first;
        const auto& real_buffers = entry.second;
        if (!real_buffers.empty()) {
            vkFreeCommandBuffers(pool_entry.real_device,
                                 pool_entry.real_pool,
                                 static_cast<uint32_t>(real_buffers.size()),
                                 real_buffers.data());
        }
        if (pool_entry.real_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(pool_entry.real_device, pool_entry.real_pool, nullptr);
        }
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

    VkCommandBufferBeginInfo real_info = *info;

    VkResult result = VK_SUCCESS;
    switch (bit->second.state) {
        case ServerCommandBufferState::INITIAL:
            result = vkBeginCommandBuffer(bit->second.real_buffer, &real_info);
            if (result == VK_SUCCESS) {
                bit->second.state = ServerCommandBufferState::RECORDING;
            }
            return result;
        case ServerCommandBufferState::EXECUTABLE:
            if (info->flags & VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT) {
                result = vkBeginCommandBuffer(bit->second.real_buffer, &real_info);
                if (result == VK_SUCCESS) {
                    bit->second.state = ServerCommandBufferState::RECORDING;
                }
                return result;
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
    VkResult result = vkEndCommandBuffer(bit->second.real_buffer);
    if (result == VK_SUCCESS) {
        bit->second.state = ServerCommandBufferState::EXECUTABLE;
    }
    return result;
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

    VkResult result = vkResetCommandBuffer(bit->second.real_buffer, 0);
    if (result == VK_SUCCESS) {
        bit->second.state = ServerCommandBufferState::INITIAL;
    }
    return result;
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

ServerCommandBufferState CommandBufferState::get_state(VkCommandBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return ServerCommandBufferState::INVALID;
    }
    return bit->second.state;
}

void CommandBufferState::invalidate(VkCommandBuffer buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit != buffers_.end()) {
        bit->second.state = ServerCommandBufferState::INVALID;
    }
}

VkCommandBuffer CommandBufferState::get_real_buffer(VkCommandBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return VK_NULL_HANDLE;
    }
    return bit->second.real_buffer;
}

VkCommandBufferLevel CommandBufferState::get_level(VkCommandBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    if (bit == buffers_.end()) {
        return VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    }
    return bit->second.level;
}

VkCommandPool CommandBufferState::get_real_pool(VkCommandPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto pit = pools_.find(handle_key(pool));
    if (pit == pools_.end()) {
        return VK_NULL_HANDLE;
    }
    return pit->second.real_pool;
}

VkDevice CommandBufferState::get_pool_device(VkCommandPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto pit = pools_.find(handle_key(pool));
    if (pit == pools_.end()) {
        return VK_NULL_HANDLE;
    }
    return pit->second.device;
}

VkDevice CommandBufferState::get_pool_real_device(VkCommandPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto pit = pools_.find(handle_key(pool));
    if (pit == pools_.end()) {
        return VK_NULL_HANDLE;
    }
    return pit->second.real_device;
}

} // namespace venus_plus
