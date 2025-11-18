#ifndef VENUS_PLUS_MEMORY_REQUIREMENTS_H
#define VENUS_PLUS_MEMORY_REQUIREMENTS_H

#include <vulkan/vulkan.h>

namespace venus_plus {

VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment);
VkMemoryRequirements make_buffer_memory_requirements(VkDeviceSize requested_size);
VkMemoryRequirements make_image_memory_requirements(VkFormat format,
                                                    const VkExtent3D& extent,
                                                    uint32_t mip_levels,
                                                    uint32_t array_layers,
                                                    VkSampleCountFlagBits samples);
uint32_t format_bytes_per_pixel(VkFormat format);
VkDeviceSize compute_mip_level_size(const VkExtent3D& base_extent,
                                    uint32_t mip_level,
                                    uint32_t bytes_per_pixel,
                                    VkSampleCountFlagBits samples);

} // namespace venus_plus

#endif // VENUS_PLUS_MEMORY_REQUIREMENTS_H
