#include "sync_state.h"

namespace venus_plus {

SyncState g_sync_state;

void SyncState::add_fence(VkDevice device, VkFence local, VkFence remote, bool signaled) {
    std::lock_guard<std::mutex> lock(mutex_);
    FenceState state;
    state.device = device;
    state.remote_handle = remote;
    state.signaled = signaled;
    fences_[handle_key(local)] = state;
}

void SyncState::remove_fence(VkFence fence) {
    std::lock_guard<std::mutex> lock(mutex_);
    fences_.erase(handle_key(fence));
}

bool SyncState::has_fence(VkFence fence) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fences_.find(handle_key(fence)) != fences_.end();
}

VkFence SyncState::get_remote_fence(VkFence fence) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fences_.find(handle_key(fence));
    if (it == fences_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

void SyncState::set_fence_signaled(VkFence fence, bool signaled) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fences_.find(handle_key(fence));
    if (it != fences_.end()) {
        it->second.signaled = signaled;
    }
}

bool SyncState::is_fence_signaled(VkFence fence) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fences_.find(handle_key(fence));
    if (it == fences_.end()) {
        return false;
    }
    return it->second.signaled;
}

void SyncState::remove_device(VkDevice device) {
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

void SyncState::add_semaphore(VkDevice device,
                              VkSemaphore local,
                              VkSemaphore remote,
                              VkSemaphoreType type,
                              bool binary_signaled,
                              uint64_t timeline_value) {
    std::lock_guard<std::mutex> lock(mutex_);
    SemaphoreState state;
    state.device = device;
    state.remote_handle = remote;
    state.type = type;
    state.binary_signaled = binary_signaled;
    state.timeline_value = timeline_value;
    semaphores_[handle_key(local)] = state;
}

void SyncState::remove_semaphore(VkSemaphore semaphore) {
    std::lock_guard<std::mutex> lock(mutex_);
    semaphores_.erase(handle_key(semaphore));
}

bool SyncState::has_semaphore(VkSemaphore semaphore) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return semaphores_.find(handle_key(semaphore)) != semaphores_.end();
}

VkSemaphore SyncState::get_remote_semaphore(VkSemaphore semaphore) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it == semaphores_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

VkSemaphoreType SyncState::get_semaphore_type(VkSemaphore semaphore) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it == semaphores_.end()) {
        return VK_SEMAPHORE_TYPE_BINARY;
    }
    return it->second.type;
}

bool SyncState::is_binary_semaphore_signaled(VkSemaphore semaphore) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it == semaphores_.end()) {
        return false;
    }
    return it->second.binary_signaled;
}

void SyncState::set_binary_semaphore_signaled(VkSemaphore semaphore, bool signaled) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it != semaphores_.end()) {
        it->second.binary_signaled = signaled;
    }
}

uint64_t SyncState::get_timeline_value(VkSemaphore semaphore) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it == semaphores_.end()) {
        return 0;
    }
    return it->second.timeline_value;
}

void SyncState::set_timeline_value(VkSemaphore semaphore, uint64_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it != semaphores_.end()) {
        if (value > it->second.timeline_value) {
            it->second.timeline_value = value;
        }
    }
}

} // namespace venus_plus
