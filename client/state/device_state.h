#ifndef VENUS_PLUS_DEVICE_STATE_H
#define VENUS_PLUS_DEVICE_STATE_H

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <unordered_set>

namespace venus_plus {

struct QueueEntry {
    VkQueue local_handle;
    VkQueue remote_handle;
    uint32_t family_index;
    uint32_t queue_index;
};

struct DeviceEntry {
    VkDevice local_handle;
    VkDevice remote_handle;
    VkPhysicalDevice physical_device;  // The local physical device handle
    std::vector<QueueEntry> queues;
    std::unordered_set<std::string> enabled_extensions;
    uint32_t api_version = VK_API_VERSION_1_0;
};

class DeviceState {
public:
    DeviceState() = default;
    ~DeviceState() = default;

    // Add a new device
    void add_device(VkDevice local,
                    VkDevice remote,
                    VkPhysicalDevice phys_dev,
                    uint32_t api_version = VK_API_VERSION_1_0);

    // Remove a device and all its queues
    void remove_device(VkDevice local);

    // Check if device exists
    bool has_device(VkDevice local) const;

    // Get remote device handle
    VkDevice get_remote_device(VkDevice local) const;

    // Get device entry
    DeviceEntry* get_device(VkDevice local);

    // Extension helpers
    void set_device_extensions(VkDevice device, const char* const* names, uint32_t count);
    bool is_extension_enabled(VkDevice device, const char* name) const;
    uint32_t get_device_api_version(VkDevice device) const;
    VkPhysicalDevice get_device_physical_device(VkDevice device) const;

    // Add a queue to a device
    void add_queue(VkDevice device, VkQueue local, VkQueue remote, uint32_t family, uint32_t index);

    // Get remote queue handle
    VkQueue get_remote_queue(VkQueue local) const;

private:
    std::unordered_map<VkDevice, DeviceEntry> devices_;
    std::unordered_map<VkQueue, VkQueue> queue_to_remote_;  // local -> remote mapping for quick lookup
};

// Global device state
extern DeviceState g_device_state;

} // namespace venus_plus

#endif // VENUS_PLUS_DEVICE_STATE_H
