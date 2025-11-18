#include "resource_tracker.h"

#include "binding_validator.h"
#include <algorithm>

namespace venus_plus {

ResourceTracker::ResourceTracker()
    : next_buffer_handle_(0x40000000ull),
      next_image_handle_(0x50000000ull),
      next_memory_handle_(0x60000000ull) {}

VkBuffer ResourceTracker::create_buffer(VkDevice device, const VkBufferCreateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    VkBuffer handle = reinterpret_cast<VkBuffer>(next_buffer_handle_++);
    BufferResource resource = {};
    resource.handle_device = device;
    resource.handle = handle;
    resource.size = info.size;
    resource.usage = info.usage;
    buffers_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_buffer(VkBuffer buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    if (it == buffers_.end()) {
        return false;
    }
    if (it->second.bound && it->second.bound_memory != VK_NULL_HANDLE) {
        auto mem_it = memories_.find(handle_key(it->second.bound_memory));
        if (mem_it != memories_.end()) {
            auto& bindings = mem_it->second.buffer_bindings;
            bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                          [buffer](const BufferBinding& binding) {
                                              return binding.buffer == buffer;
                                          }),
                           bindings.end());
        }
    }
    buffers_.erase(it);
    return true;
}

bool ResourceTracker::get_buffer_requirements(VkBuffer buffer, VkMemoryRequirements* requirements) {
    if (!requirements) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    if (it == buffers_.end()) {
        return false;
    }
    if (!it->second.requirements_valid) {
        it->second.requirements = make_buffer_memory_requirements(it->second.size);
        it->second.requirements_valid = true;
    }
    *requirements = it->second.requirements;
    return true;
}

VkImage ResourceTracker::create_image(VkDevice device, const VkImageCreateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    VkImage handle = reinterpret_cast<VkImage>(next_image_handle_++);
    ImageResource resource = {};
    resource.handle_device = device;
    resource.handle = handle;
    resource.type = info.imageType;
    resource.format = info.format;
    resource.extent = info.extent;
    resource.mip_levels = std::max(1u, info.mipLevels);
    resource.array_layers = std::max(1u, info.arrayLayers);
    resource.samples = info.samples;
    resource.tiling = info.tiling;
    resource.usage = info.usage;
    images_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_image(VkImage image) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end()) {
        return false;
    }
    if (it->second.bound && it->second.bound_memory != VK_NULL_HANDLE) {
        auto mem_it = memories_.find(handle_key(it->second.bound_memory));
        if (mem_it != memories_.end()) {
            auto& bindings = mem_it->second.image_bindings;
            bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                          [image](const ImageBinding& binding) {
                                              return binding.image == image;
                                          }),
                           bindings.end());
        }
    }
    images_.erase(it);
    return true;
}

bool ResourceTracker::get_image_requirements(VkImage image, VkMemoryRequirements* requirements) {
    if (!requirements) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end()) {
        return false;
    }
    if (!it->second.requirements_valid) {
        it->second.requirements = make_image_memory_requirements(
            it->second.format, it->second.extent, it->second.mip_levels, it->second.array_layers, it->second.samples);
        it->second.requirements_valid = true;
    }
    *requirements = it->second.requirements;
    return true;
}

VkDeviceMemory ResourceTracker::allocate_memory(VkDevice device, const VkMemoryAllocateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    VkDeviceMemory handle = reinterpret_cast<VkDeviceMemory>(next_memory_handle_++);
    MemoryResource resource = {};
    resource.handle_device = device;
    resource.handle = handle;
    resource.size = info.allocationSize;
    resource.type_index = info.memoryTypeIndex;
    memories_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::free_memory(VkDeviceMemory memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return false;
    }

    // Clear bindings referencing this allocation
    for (const auto& binding : it->second.buffer_bindings) {
        auto buf_it = buffers_.find(handle_key(binding.buffer));
        if (buf_it != buffers_.end()) {
            buf_it->second.bound = false;
            buf_it->second.bound_memory = VK_NULL_HANDLE;
            buf_it->second.bound_offset = 0;
        }
    }
    for (const auto& binding : it->second.image_bindings) {
        auto img_it = images_.find(handle_key(binding.image));
        if (img_it != images_.end()) {
            img_it->second.bound = false;
            img_it->second.bound_memory = VK_NULL_HANDLE;
            img_it->second.bound_offset = 0;
        }
    }

    memories_.erase(it);
    return true;
}

bool ResourceTracker::bind_buffer_memory(VkBuffer buffer,
                                         VkDeviceMemory memory,
                                         VkDeviceSize offset,
                                         std::string* error_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto buf_it = buffers_.find(handle_key(buffer));
    auto mem_it = memories_.find(handle_key(memory));
    if (buf_it == buffers_.end() || mem_it == memories_.end()) {
        if (error_message) {
            *error_message = "Buffer or memory not tracked";
        }
        return false;
    }

    BufferResource& buf = buf_it->second;
    MemoryResource& mem = mem_it->second;

    if (buf.handle_device != mem.handle_device) {
        if (error_message) {
            *error_message = "Buffer and memory belong to different devices";
        }
        return false;
    }
    if (buf.bound) {
        if (error_message) {
            *error_message = "Buffer already bound";
        }
        return false;
    }

    if (!buf.requirements_valid) {
        buf.requirements = make_buffer_memory_requirements(buf.size);
        buf.requirements_valid = true;
    }

    if (!validate_buffer_binding(buf.requirements, mem.size, offset, error_message)) {
        return false;
    }

    if (!check_memory_overlap_locked(mem, offset, buf.requirements.size, error_message)) {
        return false;
    }

    buf.bound = true;
    buf.bound_memory = memory;
    buf.bound_offset = offset;
    mem.buffer_bindings.push_back(BufferBinding{buffer, offset, buf.requirements.size});
    return true;
}

bool ResourceTracker::bind_image_memory(VkImage image,
                                        VkDeviceMemory memory,
                                        VkDeviceSize offset,
                                        std::string* error_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto img_it = images_.find(handle_key(image));
    auto mem_it = memories_.find(handle_key(memory));
    if (img_it == images_.end() || mem_it == memories_.end()) {
        if (error_message) {
            *error_message = "Image or memory not tracked";
        }
        return false;
    }

    ImageResource& img = img_it->second;
    MemoryResource& mem = mem_it->second;

    if (img.handle_device != mem.handle_device) {
        if (error_message) {
            *error_message = "Image and memory belong to different devices";
        }
        return false;
    }
    if (img.bound) {
        if (error_message) {
            *error_message = "Image already bound";
        }
        return false;
    }

    if (!img.requirements_valid) {
        img.requirements = make_image_memory_requirements(
            img.format, img.extent, img.mip_levels, img.array_layers, img.samples);
        img.requirements_valid = true;
    }

    if (!validate_image_binding(img.requirements, mem.size, offset, error_message)) {
        return false;
    }

    if (!check_memory_overlap_locked(mem, offset, img.requirements.size, error_message)) {
        return false;
    }

    img.bound = true;
    img.bound_memory = memory;
    img.bound_offset = offset;
    mem.image_bindings.push_back(ImageBinding{image, offset, img.requirements.size});
    return true;
}

bool ResourceTracker::get_image_subresource_layout(VkImage image,
                                                   const VkImageSubresource& subresource,
                                                   VkSubresourceLayout* layout) const {
    if (!layout) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end()) {
        return false;
    }

    const ImageResource& img = it->second;
    if (subresource.mipLevel >= img.mip_levels || subresource.arrayLayer >= img.array_layers) {
        return false;
    }

    const uint32_t bpp = format_bytes_per_pixel(img.format);
    const uint32_t mip_level = subresource.mipLevel;

    VkDeviceSize layer_pitch = compute_layer_pitch_locked(img);
    VkDeviceSize offset = subresource.arrayLayer * layer_pitch;
    for (uint32_t level = 0; level < mip_level; ++level) {
        VkDeviceSize mip_size = compute_mip_level_size(img.extent, level, bpp, img.samples);
        offset += align_up(mip_size, 4096);
    }
    offset = align_up(offset, 4096);

    uint32_t width = std::max(1u, img.extent.width >> mip_level);
    uint32_t height = std::max(1u, img.extent.height >> mip_level);
    uint32_t depth = std::max(1u, img.extent.depth >> mip_level);

    VkDeviceSize row_pitch = static_cast<VkDeviceSize>(width) * bpp;
    VkDeviceSize depth_pitch = row_pitch * height;
    VkDeviceSize size = depth_pitch * depth;

    layout->offset = offset;
    layout->size = size;
    layout->rowPitch = row_pitch;
    layout->depthPitch = depth_pitch;
    layout->arrayPitch = layer_pitch;
    return true;
}

bool ResourceTracker::buffer_exists(VkBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffers_.find(handle_key(buffer)) != buffers_.end();
}

bool ResourceTracker::image_exists(VkImage image) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return images_.find(handle_key(image)) != images_.end();
}

bool ResourceTracker::ranges_overlap(VkDeviceSize offset_a, VkDeviceSize size_a, VkDeviceSize offset_b, VkDeviceSize size_b) {
    if (size_a == 0 || size_b == 0) {
        return false;
    }
    VkDeviceSize end_a = offset_a + size_a;
    VkDeviceSize end_b = offset_b + size_b;
    return !(end_a <= offset_b || end_b <= offset_a);
}

bool ResourceTracker::check_memory_overlap_locked(const MemoryResource& memory,
                                                  VkDeviceSize offset,
                                                  VkDeviceSize size,
                                                  std::string* error_message) const {
    for (const auto& binding : memory.buffer_bindings) {
        if (ranges_overlap(offset, size, binding.offset, binding.size)) {
            if (error_message) {
                *error_message = "Buffer binding overlaps existing buffer binding";
            }
            return false;
        }
    }
    for (const auto& binding : memory.image_bindings) {
        if (ranges_overlap(offset, size, binding.offset, binding.size)) {
            if (error_message) {
                *error_message = "Binding overlaps existing image binding";
            }
            return false;
        }
    }
    return true;
}

VkDeviceSize ResourceTracker::compute_layer_pitch_locked(const ImageResource& image) const {
    const uint32_t bpp = format_bytes_per_pixel(image.format);
    VkDeviceSize pitch = 0;
    for (uint32_t level = 0; level < image.mip_levels; ++level) {
        VkDeviceSize mip_size = compute_mip_level_size(image.extent, level, bpp, image.samples);
        pitch += align_up(mip_size, 4096);
    }
    return pitch;
}

} // namespace venus_plus
