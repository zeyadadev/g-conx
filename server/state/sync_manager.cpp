#include "sync_manager.h"

#include <algorithm>

namespace venus_plus {

SyncManager::SyncManager()
    : next_fence_handle_(0x80000000ull),
      next_semaphore_handle_(0x90000000ull) {}

VkFence SyncManager::create_fence(VkDevice device, const VkFenceCreateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    VkFence handle = reinterpret_cast<VkFence>(next_fence_handle_++);
    FenceEntry entry;
    entry.device = device;
    entry.signaled = (info.flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0;
    fences_[handle_key(handle)] = entry;
    return handle;
}

bool SyncManager::destroy_fence(VkFence fence) {
    std::lock_guard<std::mutex> lock(mutex_);
    return fences_.erase(handle_key(fence)) > 0;
}

VkResult SyncManager::get_fence_status(VkFence fence) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fences_.find(handle_key(fence));
    if (it == fences_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return it->second.signaled ? VK_SUCCESS : VK_NOT_READY;
}

VkResult SyncManager::reset_fences(const VkFence* fences, uint32_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint32_t i = 0; i < count; ++i) {
        auto it = fences_.find(handle_key(fences[i]));
        if (it == fences_.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        it->second.signaled = false;
    }
    return VK_SUCCESS;
}

VkResult SyncManager::wait_for_fences(const VkFence* fences,
                                      uint32_t count,
                                      VkBool32 waitAll,
                                      uint64_t timeout) {
    (void)waitAll;
    (void)timeout;
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint32_t i = 0; i < count; ++i) {
        auto it = fences_.find(handle_key(fences[i]));
        if (it == fences_.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        it->second.signaled = true;
    }
    return VK_SUCCESS;
}

void SyncManager::signal_fence(VkFence fence) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fences_.find(handle_key(fence));
    if (it != fences_.end()) {
        it->second.signaled = true;
    }
}

bool SyncManager::fence_exists(VkFence fence) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fences_.find(handle_key(fence)) != fences_.end();
}

void SyncManager::remove_device(VkDevice device) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = fences_.begin(); it != fences_.end();) {
        if (it->second.device == device) {
            it = fences_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = semaphores_.begin(); it != semaphores_.end();) {
        if (it->second.device == device) {
            it = semaphores_.erase(it);
        } else {
            ++it;
        }
    }
}

VkSemaphore SyncManager::create_semaphore(VkDevice device,
                                          VkSemaphoreType type,
                                          uint64_t initial_value) {
    std::lock_guard<std::mutex> lock(mutex_);
    VkSemaphore handle = reinterpret_cast<VkSemaphore>(next_semaphore_handle_++);
    SemaphoreEntry entry;
    entry.device = device;
    entry.type = type;
    entry.binary_signaled = false;
    entry.timeline_value = initial_value;
    semaphores_[handle_key(handle)] = entry;
    return handle;
}

bool SyncManager::destroy_semaphore(VkSemaphore semaphore) {
    std::lock_guard<std::mutex> lock(mutex_);
    return semaphores_.erase(handle_key(semaphore)) > 0;
}

bool SyncManager::semaphore_exists(VkSemaphore semaphore) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return semaphores_.find(handle_key(semaphore)) != semaphores_.end();
}

VkSemaphoreType SyncManager::get_semaphore_type(VkSemaphore semaphore) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it == semaphores_.end()) {
        return VK_SEMAPHORE_TYPE_BINARY;
    }
    return it->second.type;
}

void SyncManager::consume_binary_semaphore(VkSemaphore semaphore) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it != semaphores_.end() && it->second.type == VK_SEMAPHORE_TYPE_BINARY) {
        it->second.binary_signaled = false;
    }
}

void SyncManager::signal_binary_semaphore(VkSemaphore semaphore) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it != semaphores_.end() && it->second.type == VK_SEMAPHORE_TYPE_BINARY) {
        it->second.binary_signaled = true;
    }
}

VkResult SyncManager::get_timeline_value(VkSemaphore semaphore, uint64_t* out_value) const {
    if (!out_value) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it == semaphores_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (it->second.type != VK_SEMAPHORE_TYPE_TIMELINE) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    *out_value = it->second.timeline_value;
    return VK_SUCCESS;
}

VkResult SyncManager::wait_timeline_value(VkSemaphore semaphore, uint64_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it == semaphores_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (it->second.type != VK_SEMAPHORE_TYPE_TIMELINE) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    it->second.timeline_value = std::max(it->second.timeline_value, value);
    return VK_SUCCESS;
}

VkResult SyncManager::signal_timeline_value(VkSemaphore semaphore, uint64_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it == semaphores_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (it->second.type != VK_SEMAPHORE_TYPE_TIMELINE) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    it->second.timeline_value = std::max(it->second.timeline_value, value);
    return VK_SUCCESS;
}

} // namespace venus_plus
