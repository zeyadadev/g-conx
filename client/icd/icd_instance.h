#ifndef VENUS_PLUS_ICD_INSTANCE_H
#define VENUS_PLUS_ICD_INSTANCE_H

#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

namespace venus_plus {

// ICD instance structure
// For ICD interface version 5, the instance must start with a dispatch table pointer
// The loader will write to this after vkCreateInstance returns
struct IcdInstance {
    // Loader magic - must be first!
    // This will be filled by the loader with a pointer to its dispatch table
    void *loader_data;

    // Our instance data
    VkInstance client_handle;  // The handle we allocated
    // Future: Add more instance-specific state here
};

// Helper to convert between VkInstance and IcdInstance*
inline IcdInstance* icd_instance_from_handle(VkInstance instance) {
    return reinterpret_cast<IcdInstance*>(instance);
}

inline VkInstance icd_instance_to_handle(IcdInstance* instance) {
    return reinterpret_cast<VkInstance>(instance);
}

} // namespace venus_plus

#endif // VENUS_PLUS_ICD_INSTANCE_H
