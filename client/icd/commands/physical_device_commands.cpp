// Physical Device Command Implementations
// Auto-generated from icd_entrypoints.cpp refactoring

#include "icd/icd_entrypoints.h"
#include "icd/commands/commands_common.h"

extern "C" {

// Vulkan function implementations

VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(
    VkInstance instance,
    uint32_t* pPhysicalDeviceCount,
    VkPhysicalDevice* pPhysicalDevices) {

    ICD_LOG_INFO() << "[Client ICD] vkEnumeratePhysicalDevices called\n";

    if (!pPhysicalDeviceCount) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    IcdInstance* icd_instance = icd_instance_from_handle(instance);
    InstanceState* state = g_instance_state.get_instance(instance);
    if (!icd_instance || !state) {
        ICD_LOG_ERROR() << "[Client ICD] Invalid instance state\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkInstance remote_instance = icd_instance->remote_handle;
    uint32_t requested_count = (pPhysicalDevices && *pPhysicalDeviceCount) ? *pPhysicalDeviceCount : 0;
    std::vector<VkPhysicalDevice> remote_devices;
    if (pPhysicalDevices && requested_count > 0) {
        remote_devices.resize(requested_count);
    }

    VkResult wire_result = vn_call_vkEnumeratePhysicalDevices(
        &g_ring,
        remote_instance,
        pPhysicalDeviceCount,
        pPhysicalDevices && requested_count > 0 ? remote_devices.data() : nullptr);

    if (wire_result != VK_SUCCESS) {
        return wire_result;
    }

    ICD_LOG_INFO() << "[Client ICD] Server reported " << *pPhysicalDeviceCount << " device(s)\n";

    if (!pPhysicalDevices) {
        return VK_SUCCESS;
    }

    const uint32_t returned = std::min<uint32_t>(remote_devices.size(), *pPhysicalDeviceCount);
    remote_devices.resize(returned);

    std::vector<PhysicalDeviceEntry> new_entries;
    new_entries.reserve(remote_devices.size());
    std::vector<VkPhysicalDevice> local_devices;
    local_devices.reserve(remote_devices.size());

    for (VkPhysicalDevice remote : remote_devices) {
        auto existing = std::find_if(
            state->physical_devices.begin(),
            state->physical_devices.end(),
            [remote](const PhysicalDeviceEntry& entry) {
                return entry.remote_handle == remote;
            });

        VkPhysicalDevice local = VK_NULL_HANDLE;
        if (existing != state->physical_devices.end()) {
            local = existing->local_handle;
        } else {
            local = g_handle_allocator.allocate<VkPhysicalDevice>();
        }

        new_entries.emplace_back(local, remote);
        local_devices.push_back(local);
    }

    state->physical_devices = std::move(new_entries);

    for (uint32_t i = 0; i < local_devices.size(); ++i) {
        pPhysicalDevices[i] = local_devices[i];
        ICD_LOG_INFO() << "[Client ICD] Physical device " << i << " local=" << local_devices[i]
                  << " remote=" << remote_devices[i] << "\n";
    }

    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures* pFeatures) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceFeatures called\n";

    if (!pFeatures) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pFeatures, 0, sizeof(VkPhysicalDeviceFeatures));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceFeatures(&g_ring, remote_device, pFeatures);
    ICD_LOG_INFO() << "[Client ICD] Returned features from server\n";
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2* pFeatures) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceFeatures2 called\n";

    if (!pFeatures) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pFeatures, 0, sizeof(VkPhysicalDeviceFeatures2));
        return;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceFeatures2");
    if (remote_device == VK_NULL_HANDLE) {
        memset(pFeatures, 0, sizeof(VkPhysicalDeviceFeatures2));
        return;
    }

    vn_call_vkGetPhysicalDeviceFeatures2(&g_ring, remote_device, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceFeatures2* pFeatures) {
    vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkFormatProperties* pFormatProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceFormatProperties called\n";

    if (!pFormatProperties) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pFormatProperties, 0, sizeof(VkFormatProperties));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceFormatProperties(&g_ring, remote_device, format, pFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    VkImageFormatProperties* pImageFormatProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceImageFormatProperties called\n";

    if (!pImageFormatProperties) {
        ICD_LOG_ERROR() << "[Client ICD] pImageFormatProperties is NULL\n";
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceImageFormatProperties");
    if (remote_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkGetPhysicalDeviceImageFormatProperties(
        &g_ring, remote_device, format, type, tiling, usage, flags, pImageFormatProperties);
    if (result != VK_SUCCESS) {
        ICD_LOG_WARN() << "[Client ICD] vkGetPhysicalDeviceImageFormatProperties returned " << result << "\n";
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceImageFormatProperties2 called\n";

    if (!pImageFormatInfo || !pImageFormatProperties) {
        ICD_LOG_ERROR() << "[Client ICD] pImageFormatInfo/pImageFormatProperties is NULL\n";
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceImageFormatProperties2");
    if (remote_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkGetPhysicalDeviceImageFormatProperties2(
        &g_ring, remote_device, pImageFormatInfo, pImageFormatProperties);
    if (result != VK_SUCCESS) {
        ICD_LOG_WARN() << "[Client ICD] vkGetPhysicalDeviceImageFormatProperties2 returned " << result << "\n";
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2* pImageFormatInfo,
    VkImageFormatProperties2* pImageFormatProperties) {
    return vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, pImageFormatInfo, pImageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceProperties called\n";

    if (!pProperties) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pProperties, 0, sizeof(VkPhysicalDeviceProperties));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceProperties(&g_ring, remote_device, pProperties);
    ICD_LOG_INFO() << "[Client ICD] Returned device properties from server: " << pProperties->deviceName << "\n";
    vp_branding_apply_properties(pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceProperties2 called\n";

    if (!pProperties) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pProperties, 0, sizeof(VkPhysicalDeviceProperties2));
        return;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceProperties2");
    if (remote_device == VK_NULL_HANDLE) {
        memset(pProperties, 0, sizeof(VkPhysicalDeviceProperties2));
        return;
    }

    vn_call_vkGetPhysicalDeviceProperties2(&g_ring, remote_device, pProperties);
    vp_branding_apply_properties2(pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceProperties2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2* pProperties) {
    vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties* pQueueFamilyProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceQueueFamilyProperties called\n";

    if (!pQueueFamilyPropertyCount) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        *pQueueFamilyPropertyCount = 0;
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceQueueFamilyProperties(&g_ring, remote_device, pQueueFamilyPropertyCount, pQueueFamilyProperties);

    if (pQueueFamilyProperties) {
        ICD_LOG_INFO() << "[Client ICD] Returned " << *pQueueFamilyPropertyCount << " queue families from server\n";
    } else {
        ICD_LOG_INFO() << "[Client ICD] Returning queue family count: " << *pQueueFamilyPropertyCount << "\n";
    }
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2* pQueueFamilyProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceQueueFamilyProperties2 called\n";

    if (!pQueueFamilyPropertyCount) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        *pQueueFamilyPropertyCount = 0;
        return;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceQueueFamilyProperties2");
    if (remote_device == VK_NULL_HANDLE) {
        *pQueueFamilyPropertyCount = 0;
        return;
    }

    vn_call_vkGetPhysicalDeviceQueueFamilyProperties2(
        &g_ring, remote_device, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2KHR(
    VkPhysicalDevice physicalDevice,
    uint32_t* pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2* pQueueFamilyProperties) {
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties* pMemoryProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceMemoryProperties called\n";

    if (!pMemoryProperties) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pMemoryProperties, 0, sizeof(VkPhysicalDeviceMemoryProperties));
        return;
    }

    // Get remote physical device handle
    InstanceState* state = g_instance_state.get_instance_by_physical_device(physicalDevice);
    VkPhysicalDevice remote_device = VK_NULL_HANDLE;
    if (state) {
        for (const auto& entry : state->physical_devices) {
            if (entry.local_handle == physicalDevice) {
                remote_device = entry.remote_handle;
                break;
            }
        }
    }

    vn_call_vkGetPhysicalDeviceMemoryProperties(&g_ring, remote_device, pMemoryProperties);
    ICD_LOG_INFO() << "[Client ICD] Returned memory properties from server: "
              << pMemoryProperties->memoryTypeCount << " types, "
              << pMemoryProperties->memoryHeapCount << " heaps\n";
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2* pMemoryProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceMemoryProperties2 called\n";

    if (!pMemoryProperties) {
        return;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        memset(pMemoryProperties, 0, sizeof(VkPhysicalDeviceMemoryProperties2));
        return;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkGetPhysicalDeviceMemoryProperties2");
    if (remote_device == VK_NULL_HANDLE) {
        memset(pMemoryProperties, 0, sizeof(VkPhysicalDeviceMemoryProperties2));
        return;
    }

    vn_call_vkGetPhysicalDeviceMemoryProperties2(&g_ring, remote_device, pMemoryProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2* pMemoryProperties) {
    vkGetPhysicalDeviceMemoryProperties2(physicalDevice, pMemoryProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(
    VkPhysicalDevice physicalDevice,
    const char* pLayerName,
    uint32_t* pPropertyCount,
    VkExtensionProperties* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkEnumerateDeviceExtensionProperties called\n";

    if (!pPropertyCount) {
        ICD_LOG_ERROR() << "[Client ICD] pPropertyCount is NULL\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Device layers are not supported per spec
    if (pLayerName != nullptr) {
        ICD_LOG_ERROR() << "[Client ICD] Layer requested: " << pLayerName << " -> VK_ERROR_LAYER_NOT_PRESENT\n";
        return VK_ERROR_LAYER_NOT_PRESENT;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkEnumerateDeviceExtensionProperties");
    if (remote_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    uint32_t remote_count = 0;
    VkResult count_result =
        vn_call_vkEnumerateDeviceExtensionProperties(&g_ring, remote_device, pLayerName, &remote_count, nullptr);
    if (count_result != VK_SUCCESS) {
        ICD_LOG_ERROR() << "[Client ICD] Failed to query device extension count: " << count_result << "\n";
        return count_result;
    }

    std::vector<VkExtensionProperties> remote_props;
    if (remote_count > 0) {
        remote_props.resize(remote_count);
        uint32_t write_count = remote_count;
        VkResult list_result = vn_call_vkEnumerateDeviceExtensionProperties(
            &g_ring, remote_device, pLayerName, &write_count, remote_props.data());
        if (list_result != VK_SUCCESS && list_result != VK_INCOMPLETE) {
            ICD_LOG_ERROR() << "[Client ICD] Failed to fetch device extensions: " << list_result << "\n";
            return list_result;
        }
        remote_props.resize(write_count);
        if (list_result == VK_INCOMPLETE) {
            ICD_LOG_WARN() << "[Client ICD] Server reported VK_INCOMPLETE while fetching extensions (extensions may have changed)\n";
        }
    }

    std::vector<VkExtensionProperties> filtered;
    filtered.reserve(remote_props.size());
    for (const auto& prop : remote_props) {
        if (!should_filter_device_extension(prop)) {
            filtered.push_back(prop);
        } else {
            ICD_LOG_WARN() << "[Client ICD] Filtering unsupported device extension: " << prop.extensionName << "\n";
        }
    }

    const uint32_t filtered_count = static_cast<uint32_t>(filtered.size());
    if (!pProperties) {
        *pPropertyCount = filtered_count;
        ICD_LOG_INFO() << "[Client ICD] Returning device extension count: " << filtered_count << "\n";
        return VK_SUCCESS;
    }

    const uint32_t requested = *pPropertyCount;
    const uint32_t copy_count = std::min(filtered_count, requested);
    for (uint32_t i = 0; i < copy_count; ++i) {
        pProperties[i] = filtered[i];
    }

    *pPropertyCount = filtered_count;
    if (copy_count < filtered_count) {
        ICD_LOG_INFO() << "[Client ICD] Provided " << copy_count << " extensions (need " << filtered_count << "), returning VK_INCOMPLETE\n";
        return VK_INCOMPLETE;
    }

    ICD_LOG_INFO() << "[Client ICD] Returning " << copy_count << " device extensions\n";
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(
    VkPhysicalDevice physicalDevice,
    uint32_t* pPropertyCount,
    VkLayerProperties* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkEnumerateDeviceLayerProperties called\n";

    if (!pPropertyCount) {
        ICD_LOG_ERROR() << "[Client ICD] pPropertyCount is NULL\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    if (!ensure_connected()) {
        ICD_LOG_ERROR() << "[Client ICD] Not connected to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice remote_device =
        get_remote_physical_device_handle(physicalDevice, "vkEnumerateDeviceLayerProperties");
    if (remote_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = vn_call_vkEnumerateDeviceLayerProperties(
        &g_ring, remote_device, pPropertyCount, pProperties);

    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pPropertyCount) {
        ICD_LOG_INFO() << "[Client ICD] Returning " << *pPropertyCount << " layer properties"
                       << (result == VK_INCOMPLETE ? " (VK_INCOMPLETE)" : "") << "\n";
    } else if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        ICD_LOG_ERROR() << "[Client ICD] vkEnumerateDeviceLayerProperties failed: " << result << "\n";
    }

    return result;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice,
    VkFormat format,
    VkImageType type,
    VkSampleCountFlagBits samples,
    VkImageUsageFlags usage,
    VkImageTiling tiling,
    uint32_t* pPropertyCount,
    VkSparseImageFormatProperties* pProperties) {

    ICD_LOG_INFO() << "[Client ICD] vkGetPhysicalDeviceSparseImageFormatProperties called\n";

    if (!pPropertyCount) {
        return;
    }

    // For Phase 2: Return 0 sparse properties (we don't support sparse)
    *pPropertyCount = 0;
}

} // extern "C"
