#ifndef VENUS_PLUS_SERVER_SYNC_MANAGER_H
#define VENUS_PLUS_SERVER_SYNC_MANAGER_H

#include <vulkan/vulkan.h>
#include <mutex>
#include <unordered_map>

namespace venus_plus {

class SyncManager {
public:
    SyncManager();

    VkFence create_fence(VkDevice device,
                         VkDevice real_device,
                         const VkFenceCreateInfo& info);
    bool destroy_fence(VkFence fence);
    VkResult get_fence_status(VkFence fence);
    VkResult reset_fences(VkDevice real_device, const VkFence* fences, uint32_t count);
    VkResult wait_for_fences(VkDevice real_device,
                             const VkFence* fences,
                             uint32_t count,
                             VkBool32 waitAll,
                             uint64_t timeout);
    bool fence_exists(VkFence fence) const;
    VkFence get_real_fence(VkFence fence) const;
    VkDevice get_fence_real_device(VkFence fence) const;
    void remove_device(VkDevice device);

    VkSemaphore create_semaphore(VkDevice device,
                                 VkDevice real_device,
                                 VkSemaphoreType type,
                                 uint64_t initial_value);
    bool destroy_semaphore(VkSemaphore semaphore);
    bool semaphore_exists(VkSemaphore semaphore) const;
    VkSemaphoreType get_semaphore_type(VkSemaphore semaphore) const;
    VkSemaphore get_real_semaphore(VkSemaphore semaphore) const;
    void consume_binary_semaphore(VkSemaphore semaphore);
    void signal_binary_semaphore(VkSemaphore semaphore);
    VkResult get_timeline_value(VkSemaphore semaphore, uint64_t* out_value) const;
    VkResult wait_timeline_value(VkSemaphore semaphore, uint64_t value);
    VkResult signal_timeline_value(VkSemaphore semaphore, uint64_t value);

private:
    template <typename T>
    static uint64_t handle_key(T handle) {
        return reinterpret_cast<uint64_t>(handle);
    }

    mutable std::mutex mutex_;

    struct FenceEntry {
        VkDevice device = VK_NULL_HANDLE;
        VkDevice real_device = VK_NULL_HANDLE;
        VkFence real_fence = VK_NULL_HANDLE;
        bool signaled = false;
    };

    struct SemaphoreEntry {
        VkDevice device = VK_NULL_HANDLE;
        VkDevice real_device = VK_NULL_HANDLE;
        VkSemaphore real_semaphore = VK_NULL_HANDLE;
        VkSemaphoreType type = VK_SEMAPHORE_TYPE_BINARY;
        bool binary_signaled = false;
        uint64_t timeline_value = 0;
    };

    std::unordered_map<uint64_t, FenceEntry> fences_;
    std::unordered_map<uint64_t, SemaphoreEntry> semaphores_;
    uint64_t next_fence_handle_;
    uint64_t next_semaphore_handle_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_SERVER_SYNC_MANAGER_H
