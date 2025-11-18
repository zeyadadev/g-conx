#include "shadow_buffer.h"

#include <cstdlib>
#include <cstring>

namespace venus_plus {

ShadowBufferManager g_shadow_buffer_manager;

ShadowBufferManager::ShadowBufferManager() = default;

ShadowBufferManager::~ShadowBufferManager() {
    free_all_locked();
}

bool ShadowBufferManager::create_mapping(VkDevice device,
                                         VkDeviceMemory memory,
                                         VkDeviceSize offset,
                                         VkDeviceSize size,
                                         bool host_coherent,
                                         void** out_ptr) {
    if (!out_ptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (mappings_.count(handle_key(memory)) != 0) {
        return false;
    }

    void* data = nullptr;
    if (size) {
        data = std::malloc(static_cast<size_t>(size));
        if (!data) {
            return false;
        }
        std::memset(data, 0, static_cast<size_t>(size));
    }

    ShadowBufferMapping mapping = {};
    mapping.device = device;
    mapping.memory = memory;
    mapping.offset = offset;
    mapping.size = size;
    mapping.data = data;
    mapping.host_coherent = host_coherent;

    mappings_[handle_key(memory)] = mapping;
    *out_ptr = data;
    return true;
}

bool ShadowBufferManager::remove_mapping(VkDeviceMemory memory, ShadowBufferMapping* out_mapping) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mappings_.find(handle_key(memory));
    if (it == mappings_.end()) {
        return false;
    }
    if (out_mapping) {
        *out_mapping = it->second;
    } else if (it->second.data) {
        std::free(it->second.data);
    }
    mappings_.erase(it);
    return true;
}

bool ShadowBufferManager::get_mapping(VkDeviceMemory memory, ShadowBufferMapping* out_mapping) const {
    if (!out_mapping) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mappings_.find(handle_key(memory));
    if (it == mappings_.end()) {
        return false;
    }
    *out_mapping = it->second;
    return true;
}

bool ShadowBufferManager::is_mapped(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return mappings_.find(handle_key(memory)) != mappings_.end();
}

void ShadowBufferManager::remove_device(VkDevice device) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = mappings_.begin(); it != mappings_.end();) {
        if (it->second.device == device) {
            if (it->second.data) {
                std::free(it->second.data);
            }
            it = mappings_.erase(it);
        } else {
            ++it;
        }
    }
}

void ShadowBufferManager::free_all_locked() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : mappings_) {
        if (pair.second.data) {
            std::free(pair.second.data);
        }
    }
    mappings_.clear();
}

} // namespace venus_plus
