#ifndef VENUS_PLUS_FAKE_GPU_DATA_BRIDGE_H
#define VENUS_PLUS_FAKE_GPU_DATA_BRIDGE_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// C bridge functions for fake GPU data generation
void fake_gpu_data_bridge_get_properties(VkPhysicalDeviceProperties* props);
void fake_gpu_data_bridge_get_features(VkPhysicalDeviceFeatures* features);
void fake_gpu_data_bridge_get_queue_families(uint32_t* pCount, VkQueueFamilyProperties* pProps);
void fake_gpu_data_bridge_get_memory_properties(VkPhysicalDeviceMemoryProperties* memProps);

#ifdef __cplusplus
}
#endif

#endif // VENUS_PLUS_FAKE_GPU_DATA_BRIDGE_H
