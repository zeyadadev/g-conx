#ifndef VENUS_PLUS_ICD_DEVICE_H
#define VENUS_PLUS_ICD_DEVICE_H

#include <vulkan/vulkan.h>

// ICD device structure - MUST have loader_data as first member
// The Vulkan loader writes its dispatch table pointer here
struct IcdDevice {
    // Loader dispatch - MUST BE FIRST
    void* loader_data;

    // Our device-specific data
    VkDevice remote_handle;           // Device handle on server
    VkPhysicalDevice physical_device; // Parent physical device (local handle)
};

// ICD queue structure - MUST have loader_data as first member
struct IcdQueue {
    // Loader dispatch - MUST BE FIRST
    void* loader_data;

    // Our queue-specific data
    VkQueue remote_handle;      // Queue handle on server
    VkDevice parent_device;     // Parent device (local handle)
    uint32_t family_index;
    uint32_t queue_index;
};

// Helper functions for handle conversion
inline IcdDevice* icd_device_from_handle(VkDevice device) {
    return reinterpret_cast<IcdDevice*>(device);
}

inline VkDevice icd_device_to_handle(IcdDevice* device) {
    return reinterpret_cast<VkDevice>(device);
}

inline IcdQueue* icd_queue_from_handle(VkQueue queue) {
    return reinterpret_cast<IcdQueue*>(queue);
}

inline VkQueue icd_queue_to_handle(IcdQueue* queue) {
    return reinterpret_cast<VkQueue>(queue);
}

#endif // VENUS_PLUS_ICD_DEVICE_H
