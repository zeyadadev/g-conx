#pragma once

#include <cstdint>
#include <limits>

#include <vulkan/vulkan.h>

#include "host_image_copy_format_table.h"

namespace venus_plus {

struct HostImageCopyLayout {
    VkDeviceSize row_pitch;
    VkDeviceSize slice_pitch;
    VkDeviceSize depth_pitch;
    VkDeviceSize layer_size;
};

inline bool mul_overflow_u64(uint64_t a, uint64_t b, uint64_t& out) {
    if (a == 0 || b == 0) {
        out = 0;
        return false;
    }
    const uint64_t max_val = std::numeric_limits<uint64_t>::max();
    if (a > max_val / b) {
        return true;
    }
    out = a * b;
    return false;
}

inline bool compute_host_image_copy_layout(const HostImageCopyFormatInfo& info,
                                           const VkExtent3D& extent,
                                           uint32_t row_length,
                                           uint32_t image_height,
                                           uint32_t layer_count,
                                           HostImageCopyLayout* out_layout,
                                           VkDeviceSize* out_size) {
    if (!out_size || !out_layout) {
        return false;
    }
    if (info.block_extent.width == 0 || info.block_extent.height == 0 || info.block_extent.depth == 0) {
        return false;
    }
    if (layer_count == 0) {
        return false;
    }

    const uint64_t width = row_length ? row_length : extent.width;
    const uint64_t height = image_height ? image_height : extent.height;
    const uint64_t depth = extent.depth ? extent.depth : 1u;

    const uint64_t blocks_w = (width + info.block_extent.width - 1) / info.block_extent.width;
    const uint64_t blocks_h = (height + info.block_extent.height - 1) / info.block_extent.height;
    const uint64_t blocks_d = (depth + info.block_extent.depth - 1) / info.block_extent.depth;

    uint64_t row_pitch = 0;
    uint64_t slice_pitch = 0;
    uint64_t depth_pitch = 0;
    uint64_t layer_size = 0;
    uint64_t total_size = 0;

    if (mul_overflow_u64(blocks_w, info.block_size, row_pitch)) {
        return false;
    }
    if (mul_overflow_u64(blocks_h, row_pitch, slice_pitch)) {
        return false;
    }
    if (mul_overflow_u64(blocks_d, slice_pitch, depth_pitch)) {
        return false;
    }
    layer_size = depth_pitch;
    if (mul_overflow_u64(layer_size, layer_count, total_size)) {
        return false;
    }

    out_layout->row_pitch = row_pitch;
    out_layout->slice_pitch = slice_pitch;
    out_layout->depth_pitch = depth_pitch;
    out_layout->layer_size = layer_size;
    *out_size = total_size;
    return true;
}

inline bool compute_host_image_copy_size(VkFormat format,
                                         const VkExtent3D& extent,
                                         uint32_t row_length,
                                         uint32_t image_height,
                                         uint32_t layer_count,
                                         HostImageCopyLayout* out_layout,
                                         VkDeviceSize* out_size) {
    const HostImageCopyFormatInfo* info = lookup_host_image_copy_format(format);
    if (!info) {
        return false;
    }
    return compute_host_image_copy_layout(*info, extent, row_length, image_height, layer_count, out_layout, out_size);
}

} // namespace venus_plus
