#include "device_state.h"

namespace venus_plus {

DeviceState g_device_state;

void DeviceState::add_device(VkDevice local, VkDevice remote, VkPhysicalDevice phys_dev) {
    DeviceEntry entry;
    entry.local_handle = local;
    entry.remote_handle = remote;
    entry.physical_device = phys_dev;
    devices_[local] = entry;
}

void DeviceState::remove_device(VkDevice local) {
    auto it = devices_.find(local);
    if (it != devices_.end()) {
        // Remove all queue mappings for this device
        for (const auto& queue : it->second.queues) {
            queue_to_remote_.erase(queue.local_handle);
        }
        devices_.erase(it);
    }
}

bool DeviceState::has_device(VkDevice local) const {
    return devices_.find(local) != devices_.end();
}

VkDevice DeviceState::get_remote_device(VkDevice local) const {
    auto it = devices_.find(local);
    if (it != devices_.end()) {
        return it->second.remote_handle;
    }
    return VK_NULL_HANDLE;
}

DeviceEntry* DeviceState::get_device(VkDevice local) {
    auto it = devices_.find(local);
    if (it != devices_.end()) {
        return &it->second;
    }
    return nullptr;
}

void DeviceState::add_queue(VkDevice device, VkQueue local, VkQueue remote, uint32_t family, uint32_t index) {
    auto it = devices_.find(device);
    if (it != devices_.end()) {
        QueueEntry entry;
        entry.local_handle = local;
        entry.remote_handle = remote;
        entry.family_index = family;
        entry.queue_index = index;
        it->second.queues.push_back(entry);
        queue_to_remote_[local] = remote;
    }
}

VkQueue DeviceState::get_remote_queue(VkQueue local) const {
    auto it = queue_to_remote_.find(local);
    if (it != queue_to_remote_.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

} // namespace venus_plus
