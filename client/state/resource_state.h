#ifndef VENUS_PLUS_RESOURCE_STATE_H
#define VENUS_PLUS_RESOURCE_STATE_H

#include <vulkan/vulkan.h>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace venus_plus {

struct BufferState {
    VkDevice device;
    VkBuffer remote_handle;
    VkDeviceSize size;
    VkBufferUsageFlags usage;
    VkSharingMode sharing_mode;
    VkDeviceMemory bound_memory;
    VkDeviceSize bound_offset;
    VkMemoryRequirements requirements;
    bool requirements_cached;
};

struct ImageState {
    VkDevice device;
    VkImage remote_handle;
    VkImageType type;
    VkFormat format;
    VkExtent3D extent;
    uint32_t mip_levels;
    uint32_t array_layers;
    VkSampleCountFlagBits samples;
    VkImageTiling tiling;
    VkImageUsageFlags usage;
    VkImageCreateFlags flags;
    VkDeviceMemory bound_memory;
    VkDeviceSize bound_offset;
    VkMemoryRequirements requirements;
    bool requirements_cached;
};

struct MemoryState {
    VkDevice device;
    VkDeviceMemory remote_handle;
    VkDeviceSize size;
    uint32_t memory_type_index;
    std::vector<VkBuffer> bound_buffers;
    std::vector<VkImage> bound_images;
};

class ResourceState {
public:
    void add_buffer(VkDevice device, VkBuffer local, VkBuffer remote, const VkBufferCreateInfo& info);
    void remove_buffer(VkBuffer buffer);
    bool has_buffer(VkBuffer buffer) const;
    VkBuffer get_remote_buffer(VkBuffer buffer) const;
    bool cache_buffer_requirements(VkBuffer buffer, const VkMemoryRequirements& requirements);
    bool get_cached_buffer_requirements(VkBuffer buffer, VkMemoryRequirements* out) const;
    bool bind_buffer(VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset);

    void add_image(VkDevice device, VkImage local, VkImage remote, const VkImageCreateInfo& info);
    void remove_image(VkImage image);
    bool has_image(VkImage image) const;
    VkImage get_remote_image(VkImage image) const;
    bool cache_image_requirements(VkImage image, const VkMemoryRequirements& requirements);
    bool get_cached_image_requirements(VkImage image, VkMemoryRequirements* out) const;
    bool bind_image(VkImage image, VkDeviceMemory memory, VkDeviceSize offset);

    void add_memory(VkDevice device, VkDeviceMemory local, VkDeviceMemory remote, const VkMemoryAllocateInfo& info);
    void remove_memory(VkDeviceMemory memory);
    bool has_memory(VkDeviceMemory memory) const;
    VkDeviceMemory get_remote_memory(VkDeviceMemory memory) const;
    VkDeviceSize get_memory_size(VkDeviceMemory memory) const;

    bool buffer_is_bound(VkBuffer buffer) const;
    bool image_is_bound(VkImage image) const;

    void remove_device_resources(VkDevice device);

private:
    template <typename T>
    static uint64_t handle_key(T handle) {
        return reinterpret_cast<uint64_t>(handle);
    }

    void remove_buffer_binding_locked(VkBuffer buffer, VkDeviceMemory memory);
    void remove_image_binding_locked(VkImage image, VkDeviceMemory memory);

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, BufferState> buffers_;
    std::unordered_map<uint64_t, ImageState> images_;
    std::unordered_map<uint64_t, MemoryState> memories_;
};

extern ResourceState g_resource_state;

} // namespace venus_plus

#endif // VENUS_PLUS_RESOURCE_STATE_H
