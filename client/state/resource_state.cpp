#include "resource_state.h"

#include <algorithm>

namespace venus_plus {

ResourceState g_resource_state;

void ResourceState::add_buffer(VkDevice device, VkBuffer local, VkBuffer remote, const VkBufferCreateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    BufferState state = {};
    state.device = device;
    state.remote_handle = remote;
    state.size = info.size;
    state.usage = info.usage;
    state.sharing_mode = info.sharingMode;
    state.bound_memory = VK_NULL_HANDLE;
    state.bound_offset = 0;
    state.requirements = {};
    state.requirements_cached = false;
    buffers_[handle_key(local)] = state;
}

void ResourceState::remove_buffer(VkBuffer buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    if (it == buffers_.end()) {
        return;
    }
    if (it->second.bound_memory != VK_NULL_HANDLE) {
        remove_buffer_binding_locked(buffer, it->second.bound_memory);
    }
    buffers_.erase(it);
}

bool ResourceState::has_buffer(VkBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffers_.find(handle_key(buffer)) != buffers_.end();
}

VkBuffer ResourceState::get_remote_buffer(VkBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    if (it == buffers_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

bool ResourceState::cache_buffer_requirements(VkBuffer buffer, const VkMemoryRequirements& requirements) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    if (it == buffers_.end()) {
        return false;
    }
    it->second.requirements = requirements;
    it->second.requirements_cached = true;
    return true;
}

bool ResourceState::get_cached_buffer_requirements(VkBuffer buffer, VkMemoryRequirements* out) const {
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    if (it == buffers_.end() || !it->second.requirements_cached) {
        return false;
    }
    *out = it->second.requirements;
    return true;
}

bool ResourceState::bind_buffer(VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    auto mit = memories_.find(handle_key(memory));
    if (bit == buffers_.end() || mit == memories_.end()) {
        return false;
    }
    bit->second.bound_memory = memory;
    bit->second.bound_offset = offset;
    auto& vec = mit->second.bound_buffers;
    if (std::find(vec.begin(), vec.end(), buffer) == vec.end()) {
        vec.push_back(buffer);
    }
    return true;
}

void ResourceState::add_image(VkDevice device, VkImage local, VkImage remote, const VkImageCreateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    ImageState state = {};
    state.device = device;
    state.remote_handle = remote;
    state.type = info.imageType;
    state.format = info.format;
    state.extent = info.extent;
    state.mip_levels = info.mipLevels;
    state.array_layers = info.arrayLayers;
    state.samples = info.samples;
    state.tiling = info.tiling;
    state.usage = info.usage;
    state.flags = info.flags;
    state.bound_memory = VK_NULL_HANDLE;
    state.bound_offset = 0;
    state.requirements = {};
    state.requirements_cached = false;
    images_[handle_key(local)] = state;
}

void ResourceState::remove_image(VkImage image) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end()) {
        return;
    }
    if (it->second.bound_memory != VK_NULL_HANDLE) {
        remove_image_binding_locked(image, it->second.bound_memory);
    }
    images_.erase(it);
}

bool ResourceState::has_image(VkImage image) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return images_.find(handle_key(image)) != images_.end();
}

VkImage ResourceState::get_remote_image(VkImage image) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

bool ResourceState::cache_image_requirements(VkImage image, const VkMemoryRequirements& requirements) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end()) {
        return false;
    }
    it->second.requirements = requirements;
    it->second.requirements_cached = true;
    return true;
}

bool ResourceState::get_cached_image_requirements(VkImage image, VkMemoryRequirements* out) const {
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end() || !it->second.requirements_cached) {
        return false;
    }
    *out = it->second.requirements;
    return true;
}

bool ResourceState::bind_image(VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto iit = images_.find(handle_key(image));
    auto mit = memories_.find(handle_key(memory));
    if (iit == images_.end() || mit == memories_.end()) {
        return false;
    }
    iit->second.bound_memory = memory;
    iit->second.bound_offset = offset;
    auto& vec = mit->second.bound_images;
    if (std::find(vec.begin(), vec.end(), image) == vec.end()) {
        vec.push_back(image);
    }
    return true;
}

void ResourceState::add_memory(VkDevice device, VkDeviceMemory local, VkDeviceMemory remote, const VkMemoryAllocateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    MemoryState state = {};
    state.device = device;
    state.remote_handle = remote;
    state.size = info.allocationSize;
    state.memory_type_index = info.memoryTypeIndex;
    memories_[handle_key(local)] = state;
}

void ResourceState::remove_memory(VkDeviceMemory memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return;
    }
    for (VkBuffer buffer : it->second.bound_buffers) {
        auto bit = buffers_.find(handle_key(buffer));
        if (bit != buffers_.end() && bit->second.bound_memory == memory) {
            bit->second.bound_memory = VK_NULL_HANDLE;
            bit->second.bound_offset = 0;
        }
    }
    for (VkImage image : it->second.bound_images) {
        auto iit = images_.find(handle_key(image));
        if (iit != images_.end() && iit->second.bound_memory == memory) {
            iit->second.bound_memory = VK_NULL_HANDLE;
            iit->second.bound_offset = 0;
        }
    }
    memories_.erase(it);
}

bool ResourceState::has_memory(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return memories_.find(handle_key(memory)) != memories_.end();
}

VkDeviceMemory ResourceState::get_remote_memory(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

VkDeviceSize ResourceState::get_memory_size(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return 0;
    }
    return it->second.size;
}

bool ResourceState::buffer_is_bound(VkBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    return it != buffers_.end() && it->second.bound_memory != VK_NULL_HANDLE;
}

bool ResourceState::image_is_bound(VkImage image) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    return it != images_.end() && it->second.bound_memory != VK_NULL_HANDLE;
}

void ResourceState::remove_device_resources(VkDevice device) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint64_t> buffer_keys;
    for (const auto& pair : buffers_) {
        if (pair.second.device == device) {
            buffer_keys.push_back(pair.first);
        }
    }
    for (uint64_t key : buffer_keys) {
        VkBuffer buffer = reinterpret_cast<VkBuffer>(key);
        auto it = buffers_.find(key);
        if (it != buffers_.end()) {
            if (it->second.bound_memory != VK_NULL_HANDLE) {
                remove_buffer_binding_locked(buffer, it->second.bound_memory);
            }
            buffers_.erase(it);
        }
    }

    std::vector<uint64_t> image_keys;
    for (const auto& pair : images_) {
        if (pair.second.device == device) {
            image_keys.push_back(pair.first);
        }
    }
    for (uint64_t key : image_keys) {
        VkImage image = reinterpret_cast<VkImage>(key);
        auto it = images_.find(key);
        if (it != images_.end()) {
            if (it->second.bound_memory != VK_NULL_HANDLE) {
                remove_image_binding_locked(image, it->second.bound_memory);
            }
            images_.erase(it);
        }
    }

    std::vector<uint64_t> memory_keys;
    for (const auto& pair : memories_) {
        if (pair.second.device == device) {
            memory_keys.push_back(pair.first);
        }
    }
    for (uint64_t key : memory_keys) {
        auto it = memories_.find(key);
        if (it != memories_.end()) {
            for (VkBuffer buffer : it->second.bound_buffers) {
                auto bit = buffers_.find(handle_key(buffer));
                if (bit != buffers_.end()) {
                    bit->second.bound_memory = VK_NULL_HANDLE;
                    bit->second.bound_offset = 0;
                }
            }
            for (VkImage image : it->second.bound_images) {
                auto iit = images_.find(handle_key(image));
                if (iit != images_.end()) {
                    iit->second.bound_memory = VK_NULL_HANDLE;
                    iit->second.bound_offset = 0;
                }
            }
            memories_.erase(it);
        }
    }
}

void ResourceState::remove_buffer_binding_locked(VkBuffer buffer, VkDeviceMemory memory) {
    auto mit = memories_.find(handle_key(memory));
    if (mit == memories_.end()) {
        return;
    }
    auto& vec = mit->second.bound_buffers;
    vec.erase(std::remove(vec.begin(), vec.end(), buffer), vec.end());
}

void ResourceState::remove_image_binding_locked(VkImage image, VkDeviceMemory memory) {
    auto mit = memories_.find(handle_key(memory));
    if (mit == memories_.end()) {
        return;
    }
    auto& vec = mit->second.bound_images;
    vec.erase(std::remove(vec.begin(), vec.end(), image), vec.end());
}

} // namespace venus_plus
