#ifndef VENUS_PLUS_INSTANCE_STATE_H
#define VENUS_PLUS_INSTANCE_STATE_H

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace venus_plus {

// Client-side instance state
struct PhysicalDeviceEntry {
    VkPhysicalDevice local_handle;
    VkPhysicalDevice remote_handle;

    PhysicalDeviceEntry()
        : local_handle(VK_NULL_HANDLE), remote_handle(VK_NULL_HANDLE) {}

    PhysicalDeviceEntry(VkPhysicalDevice local, VkPhysicalDevice remote)
        : local_handle(local), remote_handle(remote) {}
};

struct InstanceState {
    VkInstance local_handle;
    VkInstance remote_handle;
    std::vector<PhysicalDeviceEntry> physical_devices;

    InstanceState()
        : local_handle(VK_NULL_HANDLE), remote_handle(VK_NULL_HANDLE) {}

    InstanceState(VkInstance local, VkInstance remote)
        : local_handle(local), remote_handle(remote) {}
};

// Global instance state manager
class InstanceStateManager {
public:
    // Add new instance
    void add_instance(VkInstance local_handle, VkInstance remote_handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        instances_[reinterpret_cast<uint64_t>(local_handle)] = InstanceState(local_handle, remote_handle);
    }

    // Remove instance
    void remove_instance(VkInstance instance) {
        std::lock_guard<std::mutex> lock(mutex_);
        instances_.erase(reinterpret_cast<uint64_t>(instance));
    }

    // Check if instance exists
    bool has_instance(VkInstance instance) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return instances_.find(reinterpret_cast<uint64_t>(instance)) != instances_.end();
    }

    // Get instance state
    InstanceState* get_instance(VkInstance instance) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = instances_.find(reinterpret_cast<uint64_t>(instance));
        if (it == instances_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    // Store physical devices for instance
    void set_physical_devices(VkInstance instance, const std::vector<PhysicalDeviceEntry>& devices) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = instances_.find(reinterpret_cast<uint64_t>(instance));
        if (it != instances_.end()) {
            it->second.physical_devices = devices;
        }
    }

    VkInstance get_remote_handle(VkInstance instance) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = instances_.find(reinterpret_cast<uint64_t>(instance));
        if (it == instances_.end()) {
            return VK_NULL_HANDLE;
        }
        return it->second.remote_handle;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, InstanceState> instances_;
};

// Global instance state manager
extern InstanceStateManager g_instance_state;

} // namespace venus_plus

#endif // VENUS_PLUS_INSTANCE_STATE_H
