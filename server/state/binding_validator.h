#ifndef VENUS_PLUS_BINDING_VALIDATOR_H
#define VENUS_PLUS_BINDING_VALIDATOR_H

#include <string>
#include <vulkan/vulkan.h>

namespace venus_plus {

bool validate_buffer_binding(const VkMemoryRequirements& requirements,
                             VkDeviceSize memory_size,
                             VkDeviceSize offset,
                             std::string* error_message);

bool validate_image_binding(const VkMemoryRequirements& requirements,
                            VkDeviceSize memory_size,
                            VkDeviceSize offset,
                            std::string* error_message);

} // namespace venus_plus

#endif // VENUS_PLUS_BINDING_VALIDATOR_H
