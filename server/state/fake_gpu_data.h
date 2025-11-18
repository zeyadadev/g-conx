#ifndef VENUS_PLUS_FAKE_GPU_DATA_H
#define VENUS_PLUS_FAKE_GPU_DATA_H

#include <vulkan/vulkan.h>

namespace venus_plus {

// Generate fake but valid GPU properties
void generate_fake_physical_device_properties(VkPhysicalDeviceProperties* props);

// Generate fake but valid GPU features
void generate_fake_physical_device_features(VkPhysicalDeviceFeatures* features);

// Generate fake queue family properties
void generate_fake_queue_family_properties(uint32_t* pCount, VkQueueFamilyProperties* pProps);

// Generate fake memory properties
void generate_fake_memory_properties(VkPhysicalDeviceMemoryProperties* memProps);

} // namespace venus_plus

#endif // VENUS_PLUS_FAKE_GPU_DATA_H
