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

    VkBuffer create_buffer(VkDevice client_device,
                           VkDevice real_device,
                           const VkBufferCreateInfo& info);
    bool destroy_buffer(VkBuffer buffer);
    bool get_buffer_requirements(VkBuffer buffer, VkMemoryRequirements* requirements);
    VkBuffer get_real_buffer(VkBuffer buffer) const;

    VkImage create_image(VkDevice client_device,
                         VkDevice real_device,
                         const VkImageCreateInfo& info);
    void register_external_image(VkDevice client_device,
                                 VkDevice real_device,
                                 VkImage client_handle,
                                 VkImage real_handle,
                                 const VkImageCreateInfo& info);
    void unregister_external_image(VkImage image);
    bool destroy_image(VkImage image);
    bool get_image_requirements(VkImage image, VkMemoryRequirements* requirements);
    bool get_image_subresource_layout(VkImage image, const VkImageSubresource& subresource, VkSubresourceLayout* layout) const;
    VkImage get_real_image(VkImage image) const;
    VkImageView create_image_view(VkDevice client_device,
                                  VkDevice real_device,
                                  const VkImageViewCreateInfo& info,
                                  VkImage client_image,
                                  VkImage real_image);
    bool destroy_image_view(VkImageView view);
    VkImageView get_real_image_view(VkImageView view) const;

    VkBufferView create_buffer_view(VkDevice client_device,
                                    VkDevice real_device,
                                    const VkBufferViewCreateInfo& info,
                                    VkBuffer client_buffer,
                                    VkBuffer real_buffer);
    bool destroy_buffer_view(VkBufferView view);
    VkBufferView get_real_buffer_view(VkBufferView view) const;

    VkSampler create_sampler(VkDevice client_device,
                             VkDevice real_device,
                             const VkSamplerCreateInfo& info);
    bool destroy_sampler(VkSampler sampler);
    VkSampler get_real_sampler(VkSampler sampler) const;
    VkRenderPass create_render_pass(VkDevice client_device,
                                    VkDevice real_device,
                                    const VkRenderPassCreateInfo& info);
    VkRenderPass create_render_pass2(VkDevice client_device,
                                     VkDevice real_device,
                                     const VkRenderPassCreateInfo2* info);
    bool destroy_render_pass(VkRenderPass render_pass);
    VkRenderPass get_real_render_pass(VkRenderPass render_pass) const;

    VkFramebuffer create_framebuffer(VkDevice client_device,
                                     VkDevice real_device,
                                     const VkFramebufferCreateInfo& info);
    bool destroy_framebuffer(VkFramebuffer framebuffer);
    VkFramebuffer get_real_framebuffer(VkFramebuffer framebuffer) const;

    VkDeviceMemory allocate_memory(VkDevice client_device,
                                   VkDevice real_device,
                                   const VkMemoryAllocateInfo& info);
    bool free_memory(VkDeviceMemory memory);
    VkDeviceMemory get_real_memory(VkDeviceMemory memory) const;
    bool get_memory_info(VkDeviceMemory memory,
                         VkDeviceMemory* real_memory,
                         VkDevice* real_device,
                         VkDeviceSize* size,
                         uint32_t* type_index) const;

    bool bind_buffer_memory(VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset, std::string* error_message);
    bool bind_image_memory(VkImage image, VkDeviceMemory memory, VkDeviceSize offset, std::string* error_message);

    bool buffer_exists(VkBuffer buffer) const;
    bool image_exists(VkImage image) const;

    VkShaderModule create_shader_module(VkDevice device,
                                        VkDevice real_device,
                                        const VkShaderModuleCreateInfo& info);
    bool destroy_shader_module(VkShaderModule module);
    VkShaderModule get_real_shader_module(VkShaderModule module) const;

    VkDescriptorSetLayout create_descriptor_set_layout(VkDevice device,
                                                       VkDevice real_device,
                                                       const VkDescriptorSetLayoutCreateInfo& info);
    bool destroy_descriptor_set_layout(VkDescriptorSetLayout layout);
    VkDescriptorSetLayout get_real_descriptor_set_layout(VkDescriptorSetLayout layout) const;

    VkDescriptorPool create_descriptor_pool(VkDevice device,
                                            VkDevice real_device,
                                            const VkDescriptorPoolCreateInfo& info);
    bool destroy_descriptor_pool(VkDescriptorPool pool);
    VkResult reset_descriptor_pool(VkDescriptorPool pool, VkDescriptorPoolResetFlags flags);
    VkDescriptorPool get_real_descriptor_pool(VkDescriptorPool pool) const;

    VkResult allocate_descriptor_sets(VkDevice device,
                                      VkDevice real_device,
                                      const VkDescriptorSetAllocateInfo& info,
                                      std::vector<VkDescriptorSet>* out_sets);
    VkResult free_descriptor_sets(VkDescriptorPool pool,
                                  const std::vector<VkDescriptorSet>& sets);
    VkDescriptorSet get_real_descriptor_set(VkDescriptorSet set) const;

    VkPipelineLayout create_pipeline_layout(VkDevice device,
                                            VkDevice real_device,
                                            const VkPipelineLayoutCreateInfo& info);
    bool destroy_pipeline_layout(VkPipelineLayout layout);
    VkPipelineLayout get_real_pipeline_layout(VkPipelineLayout layout) const;

    VkResult create_compute_pipelines(VkDevice device,
                                      VkDevice real_device,
                                      VkPipelineCache cache,
                                      uint32_t count,
                                      const VkComputePipelineCreateInfo* infos,
                                      std::vector<VkPipeline>* out_pipelines);
    VkResult create_graphics_pipelines(VkDevice device,
                                       VkDevice real_device,
                                       VkPipelineCache cache,
                                       uint32_t count,
                                       const VkGraphicsPipelineCreateInfo* infos,
                                       std::vector<VkPipeline>* out_pipelines);
    bool destroy_pipeline(VkPipeline pipeline);
    VkPipeline get_real_pipeline(VkPipeline pipeline) const;

private:
    struct BufferResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkBuffer handle;
        VkBuffer real_handle;
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
        VkDevice real_device;
        VkImage handle;
        VkImage real_handle;
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
        bool external = false;
    };

    struct ImageViewResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkImageView handle;
        VkImageView real_handle;
        VkImage image;
        VkImage real_image;
    };

    struct BufferViewResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkBufferView handle;
        VkBufferView real_handle;
        VkBuffer buffer;
        VkBuffer real_buffer;
        VkFormat format;
        VkDeviceSize offset;
        VkDeviceSize range;
    };

    struct SamplerResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkSampler handle;
        VkSampler real_handle;
    };

    struct RenderPassResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkRenderPass handle;
        VkRenderPass real_handle;
    };

    struct FramebufferResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkFramebuffer handle;
        VkFramebuffer real_handle;
        VkRenderPass render_pass;
        std::vector<VkImageView> attachments;
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
        VkDevice real_device;
        VkDeviceMemory handle;
        VkDeviceMemory real_handle;
        VkDeviceSize size;
        uint32_t type_index;
        std::vector<BufferBinding> buffer_bindings;
        std::vector<ImageBinding> image_bindings;
    };

    struct ShaderModuleResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkShaderModule handle;
        VkShaderModule real_handle;
        size_t code_size;
    };

    struct DescriptorSetLayoutResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkDescriptorSetLayout handle;
        VkDescriptorSetLayout real_handle;
    };

    struct DescriptorPoolResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkDescriptorPool handle;
        VkDescriptorPool real_handle;
        VkDescriptorPoolCreateFlags flags;
        std::vector<VkDescriptorSet> descriptor_sets;
    };

    struct DescriptorSetResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkDescriptorSet handle;
        VkDescriptorSet real_handle;
        VkDescriptorPool pool;
        VkDescriptorSetLayout layout;
    };

    struct PipelineLayoutResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkPipelineLayout handle;
        VkPipelineLayout real_handle;
    };

    struct PipelineResource {
        VkDevice handle_device;
        VkDevice real_device;
        VkPipeline handle;
        VkPipeline real_handle;
        VkPipelineBindPoint bind_point;
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
    std::unordered_map<uint64_t, ImageViewResource> image_views_;
    std::unordered_map<uint64_t, BufferViewResource> buffer_views_;
    std::unordered_map<uint64_t, SamplerResource> samplers_;
    std::unordered_map<uint64_t, RenderPassResource> render_passes_;
    std::unordered_map<uint64_t, FramebufferResource> framebuffers_;
    std::unordered_map<uint64_t, MemoryResource> memories_;
    std::unordered_map<uint64_t, ShaderModuleResource> shader_modules_;
    std::unordered_map<uint64_t, DescriptorSetLayoutResource> descriptor_set_layouts_;
    std::unordered_map<uint64_t, DescriptorPoolResource> descriptor_pools_;
    std::unordered_map<uint64_t, DescriptorSetResource> descriptor_sets_;
    std::unordered_map<uint64_t, PipelineLayoutResource> pipeline_layouts_;
    std::unordered_map<uint64_t, PipelineResource> pipelines_;
    uint64_t next_buffer_handle_;
    uint64_t next_image_handle_;
    uint64_t next_memory_handle_;
    uint64_t next_image_view_handle_;
    uint64_t next_buffer_view_handle_;
    uint64_t next_sampler_handle_;
    uint64_t next_shader_module_handle_;
    uint64_t next_descriptor_set_layout_handle_;
    uint64_t next_descriptor_pool_handle_;
    uint64_t next_descriptor_set_handle_;
    uint64_t next_pipeline_layout_handle_;
    uint64_t next_pipeline_handle_;
    uint64_t next_render_pass_handle_;
    uint64_t next_framebuffer_handle_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_RESOURCE_TRACKER_H
