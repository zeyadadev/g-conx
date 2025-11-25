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

struct ImageViewState {
    VkDevice device;
    VkImageView remote_handle;
    VkImage image;
};

struct BufferViewState {
    VkDevice device;
    VkBufferView remote_handle;
    VkBuffer buffer;
    VkFormat format;
    VkDeviceSize offset;
    VkDeviceSize range;
};

struct SamplerState {
    VkDevice device;
    VkSampler remote_handle;
};

struct RenderPassState {
    VkDevice device;
    VkRenderPass remote_handle;
};

struct FramebufferState {
    VkDevice device;
    VkFramebuffer remote_handle;
    VkRenderPass render_pass;
    std::vector<VkImageView> attachments;
};

struct MemoryState {
    VkDevice device;
    VkDeviceMemory remote_handle;
    VkDeviceSize size;
    uint32_t memory_type_index;
    std::vector<VkBuffer> bound_buffers;
    std::vector<VkImage> bound_images;
    bool invalidate_on_wait = false;
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

    void add_image_view(VkDevice device, VkImageView local, VkImageView remote, VkImage image);
    void remove_image_view(VkImageView view);
    bool has_image_view(VkImageView view) const;
    VkImageView get_remote_image_view(VkImageView view) const;
    VkImage get_image_from_view(VkImageView view) const;

    void add_buffer_view(VkDevice device,
                         VkBufferView local,
                         VkBufferView remote,
                         VkBuffer buffer,
                         VkFormat format,
                         VkDeviceSize offset,
                         VkDeviceSize range);
    void remove_buffer_view(VkBufferView view);
    bool has_buffer_view(VkBufferView view) const;
    VkBufferView get_remote_buffer_view(VkBufferView view) const;
    VkBuffer get_buffer_from_view(VkBufferView view) const;

    void add_sampler(VkDevice device, VkSampler local, VkSampler remote);
    void remove_sampler(VkSampler sampler);
    bool has_sampler(VkSampler sampler) const;
    VkSampler get_remote_sampler(VkSampler sampler) const;

    void add_render_pass(VkDevice device, VkRenderPass local, VkRenderPass remote);
    void remove_render_pass(VkRenderPass render_pass);
    bool has_render_pass(VkRenderPass render_pass) const;
    VkRenderPass get_remote_render_pass(VkRenderPass render_pass) const;

    void add_framebuffer(VkDevice device,
                         VkFramebuffer local,
                         VkFramebuffer remote,
                         VkRenderPass render_pass,
                         const VkFramebufferCreateInfo& info);
    void remove_framebuffer(VkFramebuffer framebuffer);
    bool has_framebuffer(VkFramebuffer framebuffer) const;
    VkFramebuffer get_remote_framebuffer(VkFramebuffer framebuffer) const;
    VkRenderPass get_framebuffer_render_pass(VkFramebuffer framebuffer) const;

    void add_memory(VkDevice device, VkDeviceMemory local, VkDeviceMemory remote, const VkMemoryAllocateInfo& info);
    void remove_memory(VkDeviceMemory memory);
    bool has_memory(VkDeviceMemory memory) const;
    VkDeviceMemory get_remote_memory(VkDeviceMemory memory) const;
    VkDeviceSize get_memory_size(VkDeviceMemory memory) const;
    VkDevice get_memory_device(VkDeviceMemory memory) const;
    uint32_t get_memory_type_index(VkDeviceMemory memory) const;
    bool should_invalidate_on_wait(VkDeviceMemory memory) const;

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
    std::unordered_map<uint64_t, ImageViewState> image_views_;
    std::unordered_map<uint64_t, BufferViewState> buffer_views_;
    std::unordered_map<uint64_t, SamplerState> samplers_;
    std::unordered_map<uint64_t, RenderPassState> render_passes_;
    std::unordered_map<uint64_t, FramebufferState> framebuffers_;
    std::unordered_map<uint64_t, MemoryState> memories_;
};

extern ResourceState g_resource_state;

} // namespace venus_plus

#endif // VENUS_PLUS_RESOURCE_STATE_H
