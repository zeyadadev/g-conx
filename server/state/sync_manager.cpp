#include "sync_manager.h"

#include <algorithm>
#include <vector>

namespace venus_plus {

SyncManager::SyncManager()
    : next_fence_handle_(0x80000000ull),
      next_semaphore_handle_(0x90000000ull) {}

VkFence SyncManager::create_fence(VkDevice device,
                                  VkDevice real_device,
                                  const VkFenceCreateInfo& info) {
    VkFence real_fence = VK_NULL_HANDLE;
    VkResult result = vkCreateFence(real_device, &info, nullptr, &real_fence);
    if (result != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkFence handle = reinterpret_cast<VkFence>(next_fence_handle_++);
    FenceEntry entry;
    entry.device = device;
    entry.real_device = real_device;
    entry.real_fence = real_fence;
    entry.signaled = (info.flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0;
    fences_[handle_key(handle)] = entry;
    return handle;
}

bool SyncManager::destroy_fence(VkFence fence) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fences_.find(handle_key(fence));
    if (it == fences_.end()) {
        return false;
    }
    if (it->second.real_fence != VK_NULL_HANDLE) {
        vkDestroyFence(it->second.real_device, it->second.real_fence, nullptr);
    }
    fences_.erase(it);
    return true;
}

VkResult SyncManager::get_fence_status(VkFence fence) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fences_.find(handle_key(fence));
    if (it == fences_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = vkGetFenceStatus(it->second.real_device, it->second.real_fence);
    if (result == VK_SUCCESS) {
        it->second.signaled = true;
    }
    return result;
}

VkResult SyncManager::reset_fences(VkDevice real_device, const VkFence* fences, uint32_t count) {
    if (real_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::vector<VkFence> real_fences;
    real_fences.reserve(count);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t i = 0; i < count; ++i) {
            auto it = fences_.find(handle_key(fences[i]));
            if (it == fences_.end()) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            real_fences.push_back(it->second.real_fence);
        }
    }
    VkResult result =
        vkResetFences(real_device, static_cast<uint32_t>(real_fences.size()), real_fences.data());
    if (result == VK_SUCCESS) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t i = 0; i < count; ++i) {
            auto it = fences_.find(handle_key(fences[i]));
            if (it != fences_.end()) {
                it->second.signaled = false;
            }
        }
    }
    return result;
}

VkResult SyncManager::wait_for_fences(VkDevice real_device,
                                      const VkFence* fences,
                                      uint32_t count,
                                      VkBool32 waitAll,
                                      uint64_t timeout) {
    if (real_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::vector<VkFence> real_fences;
    real_fences.reserve(count);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t i = 0; i < count; ++i) {
            auto it = fences_.find(handle_key(fences[i]));
            if (it == fences_.end()) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            real_fences.push_back(it->second.real_fence);
        }
    }
    VkResult result = vkWaitForFences(real_device,
                                      static_cast<uint32_t>(real_fences.size()),
                                      real_fences.data(),
                                      waitAll,
                                      timeout);
    if (result == VK_SUCCESS) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t i = 0; i < count; ++i) {
            auto it = fences_.find(handle_key(fences[i]));
            if (it != fences_.end()) {
                it->second.signaled = true;
            }
        }
    }
    return result;
}

bool SyncManager::fence_exists(VkFence fence) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fences_.find(handle_key(fence)) != fences_.end();
}

VkFence SyncManager::get_real_fence(VkFence fence) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fences_.find(handle_key(fence));
    if (it == fences_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_fence;
}

VkDevice SyncManager::get_fence_real_device(VkFence fence) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fences_.find(handle_key(fence));
    if (it == fences_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_device;
}

void SyncManager::remove_device(VkDevice device) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = fences_.begin(); it != fences_.end();) {
        if (it->second.device == device) {
            if (it->second.real_fence != VK_NULL_HANDLE) {
                vkDestroyFence(it->second.real_device, it->second.real_fence, nullptr);
            }
            it = fences_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = semaphores_.begin(); it != semaphores_.end();) {
        if (it->second.device == device) {
            if (it->second.real_semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(it->second.real_device, it->second.real_semaphore, nullptr);
            }
            it = semaphores_.erase(it);
        } else {
            ++it;
        }
    }
}

VkSemaphore SyncManager::create_semaphore(VkDevice device,
                                          VkDevice real_device,
                                          VkSemaphoreType type,
                                          uint64_t initial_value) {
    VkSemaphoreCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphoreTypeCreateInfo type_info = {};
    if (type == VK_SEMAPHORE_TYPE_TIMELINE) {
        type_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        type_info.initialValue = initial_value;
        create_info.pNext = &type_info;
    }

    VkSemaphore real_semaphore = VK_NULL_HANDLE;
    VkResult result = vkCreateSemaphore(real_device, &create_info, nullptr, &real_semaphore);
    if (result != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkSemaphore handle = reinterpret_cast<VkSemaphore>(next_semaphore_handle_++);
    SemaphoreEntry entry;
    entry.device = device;
    entry.real_device = real_device;
    entry.real_semaphore = real_semaphore;
    entry.type = type;
    entry.binary_signaled = false;
    entry.timeline_value = initial_value;
    semaphores_[handle_key(handle)] = entry;
    return handle;
}

bool SyncManager::destroy_semaphore(VkSemaphore semaphore) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it == semaphores_.end()) {
        return false;
    }
    if (it->second.real_semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(it->second.real_device, it->second.real_semaphore, nullptr);
    }
    semaphores_.erase(it);
    return true;
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

VkSemaphore SyncManager::get_real_semaphore(VkSemaphore semaphore) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = semaphores_.find(handle_key(semaphore));
    if (it == semaphores_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_semaphore;
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
