#include "memory_requirements.h"

#include <algorithm>

namespace venus_plus {

namespace {

uint32_t sample_count_value(VkSampleCountFlagBits samples) {
    switch (samples) {
    case VK_SAMPLE_COUNT_1_BIT: return 1u;
    case VK_SAMPLE_COUNT_2_BIT: return 2u;
    case VK_SAMPLE_COUNT_4_BIT: return 4u;
    case VK_SAMPLE_COUNT_8_BIT: return 8u;
    case VK_SAMPLE_COUNT_16_BIT: return 16u;
    case VK_SAMPLE_COUNT_32_BIT: return 32u;
    case VK_SAMPLE_COUNT_64_BIT: return 64u;
    default: return 1u;
    }
}

uint32_t clamp_dimension(uint32_t value, uint32_t mip_level) {
    return std::max(1u, value >> mip_level);
}

} // namespace

VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment == 0) {
        return value;
    }
    VkDeviceSize mask = alignment - 1;
    return (value + mask) & ~mask;
}

uint32_t format_bytes_per_pixel(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM:
    case VK_FORMAT_R8_UINT:
    case VK_FORMAT_R8_SINT:
    case VK_FORMAT_R8_SNORM:
        return 1;
    case VK_FORMAT_R8G8_UNORM:
    case VK_FORMAT_R8G8_UINT:
    case VK_FORMAT_R8G8_SINT:
    case VK_FORMAT_R8G8_SNORM:
    case VK_FORMAT_R16_UNORM:
    case VK_FORMAT_R16_UINT:
    case VK_FORMAT_R16_SINT:
    case VK_FORMAT_R16_SFLOAT:
        return 2;
    case VK_FORMAT_R8G8B8_UNORM:
    case VK_FORMAT_B8G8R8_UNORM:
    case VK_FORMAT_R8G8B8_SRGB:
    case VK_FORMAT_B8G8R8_SRGB:
        return 3;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SINT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R16G16_UNORM:
    case VK_FORMAT_R16G16_UINT:
    case VK_FORMAT_R16G16_SINT:
    case VK_FORMAT_R16G16_SFLOAT:
        return 4;
    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32_UINT:
    case VK_FORMAT_R32G32_SFLOAT:
        return 8;
    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        return 4;
    }
}

VkDeviceSize compute_mip_level_size(const VkExtent3D& base_extent,
                                    uint32_t mip_level,
                                    uint32_t bytes_per_pixel,
                                    VkSampleCountFlagBits samples) {
    uint32_t width = clamp_dimension(base_extent.width, mip_level);
    uint32_t height = clamp_dimension(base_extent.height, mip_level);
    uint32_t depth = clamp_dimension(base_extent.depth, mip_level);
    VkDeviceSize texels = static_cast<VkDeviceSize>(width) *
                          static_cast<VkDeviceSize>(height) *
                          static_cast<VkDeviceSize>(depth);
    VkDeviceSize size = texels * bytes_per_pixel;
    size *= sample_count_value(samples);
    return size;
}

VkMemoryRequirements make_buffer_memory_requirements(VkDeviceSize requested_size) {
    VkMemoryRequirements requirements = {};
    requirements.alignment = 256;
    requirements.size = align_up(requested_size, requirements.alignment);
    requirements.memoryTypeBits = 0x3; // support both memory types
    return requirements;
}

VkMemoryRequirements make_image_memory_requirements(VkFormat format,
                                                    const VkExtent3D& extent,
                                                    uint32_t mip_levels,
                                                    uint32_t array_layers,
                                                    VkSampleCountFlagBits samples) {
    VkMemoryRequirements requirements = {};
    const uint32_t bpp = format_bytes_per_pixel(format);
    const uint32_t levels = std::max(1u, mip_levels);
    const uint32_t layers = std::max(1u, array_layers);
    VkDeviceSize total = 0;

    for (uint32_t layer = 0; layer < layers; ++layer) {
        (void)layer;
        for (uint32_t level = 0; level < levels; ++level) {
            VkDeviceSize mip_size = compute_mip_level_size(extent, level, bpp, samples);
            total += align_up(mip_size, 4096);
        }
    }

    requirements.alignment = 4096;
    requirements.size = align_up(total, requirements.alignment);
    requirements.memoryTypeBits = 0x1; // device local only
    return requirements;
}

} // namespace venus_plus
