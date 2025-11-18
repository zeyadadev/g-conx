#ifndef VENUS_PLUS_RESOURCE_TRACKER_H
#define VENUS_PLUS_RESOURCE_TRACKER_H

#include "memory_requirements.h"
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace venus_plus {

class ResourceTracker {
public:
    ResourceTracker();

    VkBuffer create_buffer(VkDevice device, const VkBufferCreateInfo& info);
    bool destroy_buffer(VkBuffer buffer);
    bool get_buffer_requirements(VkBuffer buffer, VkMemoryRequirements* requirements);

    VkImage create_image(VkDevice device, const VkImageCreateInfo& info);
    bool destroy_image(VkImage image);
    bool get_image_requirements(VkImage image, VkMemoryRequirements* requirements);
    bool get_image_subresource_layout(VkImage image, const VkImageSubresource& subresource, VkSubresourceLayout* layout) const;

    VkDeviceMemory allocate_memory(VkDevice device, const VkMemoryAllocateInfo& info);
    bool free_memory(VkDeviceMemory memory);

    bool bind_buffer_memory(VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset, std::string* error_message);
    bool bind_image_memory(VkImage image, VkDeviceMemory memory, VkDeviceSize offset, std::string* error_message);

private:
    struct BufferResource {
        VkDevice handle_device;
        VkBuffer handle;
        VkDeviceSize size;
        VkBufferUsageFlags usage;
        bool bound = false;
        VkDeviceMemory bound_memory = VK_NULL_HANDLE;
        VkDeviceSize bound_offset = 0;
        VkMemoryRequirements requirements = {};
        bool requirements_valid = false;
    };

    struct ImageResource {
        VkDevice handle_device;
        VkImage handle;
        VkImageType type;
        VkFormat format;
        VkExtent3D extent;
        uint32_t mip_levels;
        uint32_t array_layers;
        VkSampleCountFlagBits samples;
        VkImageTiling tiling;
        VkImageUsageFlags usage;
        bool bound = false;
        VkDeviceMemory bound_memory = VK_NULL_HANDLE;
        VkDeviceSize bound_offset = 0;
        VkMemoryRequirements requirements = {};
        bool requirements_valid = false;
    };

    struct BufferBinding {
        VkBuffer buffer;
        VkDeviceSize offset;
        VkDeviceSize size;
    };

    struct ImageBinding {
        VkImage image;
        VkDeviceSize offset;
        VkDeviceSize size;
    };

    struct MemoryResource {
        VkDevice handle_device;
        VkDeviceMemory handle;
        VkDeviceSize size;
        uint32_t type_index;
        std::vector<BufferBinding> buffer_bindings;
        std::vector<ImageBinding> image_bindings;
    };

    template <typename T>
    static uint64_t handle_key(T handle) {
        return reinterpret_cast<uint64_t>(handle);
    }

    static bool ranges_overlap(VkDeviceSize offset_a, VkDeviceSize size_a, VkDeviceSize offset_b, VkDeviceSize size_b);

    bool check_memory_overlap_locked(const MemoryResource& memory,
                                     VkDeviceSize offset,
                                     VkDeviceSize size,
                                     std::string* error_message) const;

    VkDeviceSize compute_layer_pitch_locked(const ImageResource& image) const;

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, BufferResource> buffers_;
    std::unordered_map<uint64_t, ImageResource> images_;
    std::unordered_map<uint64_t, MemoryResource> memories_;
    uint64_t next_buffer_handle_;
    uint64_t next_image_handle_;
    uint64_t next_memory_handle_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_RESOURCE_TRACKER_H
