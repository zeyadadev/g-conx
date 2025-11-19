#ifndef VENUS_PLUS_SYNC_STATE_H
#define VENUS_PLUS_SYNC_STATE_H

#include <vulkan/vulkan.h>
#include <mutex>
#include <unordered_map>

namespace venus_plus {

struct FenceState {
    VkDevice device = VK_NULL_HANDLE;
    VkFence remote_handle = VK_NULL_HANDLE;
    bool signaled = false;
};

struct SemaphoreState {
    VkDevice device = VK_NULL_HANDLE;
    VkSemaphore remote_handle = VK_NULL_HANDLE;
    VkSemaphoreType type = VK_SEMAPHORE_TYPE_BINARY;
    bool binary_signaled = false;
    uint64_t timeline_value = 0;
};

struct EventState {
    VkDevice device = VK_NULL_HANDLE;
    VkEvent remote_handle = VK_NULL_HANDLE;
    bool signaled = false;
};

class SyncState {
public:
    void add_fence(VkDevice device, VkFence local, VkFence remote, bool signaled);
    void remove_fence(VkFence fence);
    bool has_fence(VkFence fence) const;
    VkFence get_remote_fence(VkFence fence) const;
    void set_fence_signaled(VkFence fence, bool signaled);
    bool is_fence_signaled(VkFence fence) const;
    void remove_device(VkDevice device);

    void add_event(VkDevice device, VkEvent local, VkEvent remote, bool signaled);
    void remove_event(VkEvent event);
    bool has_event(VkEvent event) const;
    VkEvent get_remote_event(VkEvent event) const;
    void set_event_signaled(VkEvent event, bool signaled);
    bool is_event_signaled(VkEvent event) const;

    void add_semaphore(VkDevice device,
                       VkSemaphore local,
                       VkSemaphore remote,
                       VkSemaphoreType type,
                       bool binary_signaled,
                       uint64_t timeline_value);
    void remove_semaphore(VkSemaphore semaphore);
    bool has_semaphore(VkSemaphore semaphore) const;
    VkSemaphore get_remote_semaphore(VkSemaphore semaphore) const;
    VkSemaphoreType get_semaphore_type(VkSemaphore semaphore) const;
    bool is_binary_semaphore_signaled(VkSemaphore semaphore) const;
    void set_binary_semaphore_signaled(VkSemaphore semaphore, bool signaled);
    uint64_t get_timeline_value(VkSemaphore semaphore) const;
    void set_timeline_value(VkSemaphore semaphore, uint64_t value);

private:
    template <typename T>
    static uint64_t handle_key(T handle) {
        return reinterpret_cast<uint64_t>(handle);
    }

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, FenceState> fences_;
    std::unordered_map<uint64_t, SemaphoreState> semaphores_;
    std::unordered_map<uint64_t, EventState> events_;
};

extern SyncState g_sync_state;

} // namespace venus_plus

#endif // VENUS_PLUS_SYNC_STATE_H
