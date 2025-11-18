#include "fake_gpu_data_bridge.h"
#include "fake_gpu_data.h"

extern "C" {

void fake_gpu_data_bridge_get_properties(VkPhysicalDeviceProperties* props) {
    venus_plus::generate_fake_physical_device_properties(props);
}

void fake_gpu_data_bridge_get_features(VkPhysicalDeviceFeatures* features) {
    venus_plus::generate_fake_physical_device_features(features);
}

void fake_gpu_data_bridge_get_queue_families(uint32_t* pCount, VkQueueFamilyProperties* pProps) {
    venus_plus::generate_fake_queue_family_properties(pCount, pProps);
}

void fake_gpu_data_bridge_get_memory_properties(VkPhysicalDeviceMemoryProperties* memProps) {
    venus_plus::generate_fake_memory_properties(memProps);
}

} // extern "C"
