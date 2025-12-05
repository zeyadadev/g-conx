#include "device_state.h"

namespace venus_plus {

DeviceState g_device_state;

void DeviceState::add_device(VkDevice local,
                             VkDevice remote,
                             VkPhysicalDevice local_phys_dev,
                             VkPhysicalDevice remote_phys_dev,
                             uint32_t api_version) {
    DeviceEntry entry;
    entry.local_handle = local;
    entry.remote_handle = remote;
    entry.physical_device = local_phys_dev;
    entry.remote_physical_device = remote_phys_dev;
    entry.api_version = api_version;
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

void DeviceState::set_device_extensions(VkDevice device,
                                        const char* const* names,
                                        uint32_t count) {
    auto it = devices_.find(device);
    if (it == devices_.end()) {
        return;
    }
    it->second.enabled_extensions.clear();
    for (uint32_t i = 0; i < count; ++i) {
        if (names && names[i]) {
            it->second.enabled_extensions.emplace(names[i]);
        }
    }
}

bool DeviceState::is_extension_enabled(VkDevice device, const char* name) const {
    if (!name) {
        return false;
    }
    auto it = devices_.find(device);
    if (it == devices_.end()) {
        return false;
    }
    return it->second.enabled_extensions.find(name) != it->second.enabled_extensions.end();
}

uint32_t DeviceState::get_device_api_version(VkDevice device) const {
    auto it = devices_.find(device);
    if (it == devices_.end()) {
        return VK_API_VERSION_1_0;
    }
    return it->second.api_version;
}

VkPhysicalDevice DeviceState::get_device_physical_device(VkDevice device) const {
    auto it = devices_.find(device);
    if (it == devices_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_physical_device;
}

void DeviceState::set_vulkan14_info(
    VkDevice device,
    const VkPhysicalDeviceVulkan14Features& features,
    const VkPhysicalDeviceVulkan14Properties& properties,
    const VkPhysicalDeviceLineRasterizationFeaturesEXT& line_feats,
    const VkPhysicalDeviceLineRasterizationPropertiesEXT& line_props) {
    auto it = devices_.find(device);
    if (it == devices_.end()) {
        return;
    }
    it->second.vk14_features = features;
    it->second.vk14_properties = properties;
    it->second.line_features = line_feats;
    it->second.line_properties = line_props;
}

const VkPhysicalDeviceVulkan14Features* DeviceState::get_vk14_features(VkDevice device) const {
    auto it = devices_.find(device);
    if (it == devices_.end()) {
        return nullptr;
    }
    return &it->second.vk14_features;
}

const VkPhysicalDeviceLineRasterizationFeaturesEXT* DeviceState::get_line_features(VkDevice device) const {
    auto it = devices_.find(device);
    if (it == devices_.end()) {
        return nullptr;
    }
    return &it->second.line_features;
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
