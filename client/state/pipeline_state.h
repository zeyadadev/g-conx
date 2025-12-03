#ifndef VENUS_PLUS_PIPELINE_STATE_H
#define VENUS_PLUS_PIPELINE_STATE_H

#include <vulkan/vulkan.h>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace venus_plus {

struct ShaderModuleInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkShaderModule remote_handle = VK_NULL_HANDLE;
    size_t code_size = 0;
};

struct DescriptorSetLayoutInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout remote_handle = VK_NULL_HANDLE;
    bool is_push_descriptor = false;
};

struct DescriptorPoolInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorPool remote_handle = VK_NULL_HANDLE;
    VkDescriptorPoolCreateFlags flags = 0;
    std::vector<VkDescriptorSet> descriptor_sets;
};

struct DescriptorSetInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSet remote_handle = VK_NULL_HANDLE;
    VkDescriptorPool parent_pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
};

struct DescriptorWriteItemSnapshot {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize range = 0;
    VkImageView image_view = VK_NULL_HANDLE;
    VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkSampler sampler = VK_NULL_HANDLE;
    VkBufferView texel_view = VK_NULL_HANDLE;
};

struct DescriptorBindingSnapshot {
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    std::vector<DescriptorWriteItemSnapshot> items;
};

struct PipelineLayoutInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineLayout remote_handle = VK_NULL_HANDLE;
    std::vector<VkPushConstantRange> push_constant_ranges;
};

struct PipelineCacheInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineCache remote_handle = VK_NULL_HANDLE;
};

struct PipelineInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkPipeline remote_handle = VK_NULL_HANDLE;
    VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_MAX_ENUM;
};

struct DescriptorUpdateTemplateInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorUpdateTemplate remote_handle = VK_NULL_HANDLE;
    VkDescriptorUpdateTemplateType template_type = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
    VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_MAX_ENUM;
    std::vector<VkDescriptorUpdateTemplateEntry> entries;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    uint32_t set_number = 0;
};

class PipelineState {
public:
    void add_shader_module(VkDevice device, VkShaderModule local, VkShaderModule remote, size_t code_size);
    void remove_shader_module(VkShaderModule module);
    VkShaderModule get_remote_shader_module(VkShaderModule module) const;

    void add_descriptor_set_layout(VkDevice device,
                                   VkDescriptorSetLayout local,
                                   VkDescriptorSetLayout remote,
                                   const VkDescriptorSetLayoutCreateInfo* info);
    void remove_descriptor_set_layout(VkDescriptorSetLayout layout);
    VkDescriptorSetLayout get_remote_descriptor_set_layout(VkDescriptorSetLayout layout) const;
    bool is_push_descriptor_layout(VkDescriptorSetLayout layout) const;

    void add_descriptor_pool(VkDevice device,
                             VkDescriptorPool local,
                             VkDescriptorPool remote,
                             VkDescriptorPoolCreateFlags flags);
    void remove_descriptor_pool(VkDescriptorPool pool);
    void reset_descriptor_pool(VkDescriptorPool pool);
    VkDescriptorPool get_remote_descriptor_pool(VkDescriptorPool pool) const;

    void add_descriptor_set(VkDevice device,
                            VkDescriptorPool pool,
                            VkDescriptorSetLayout layout,
                            VkDescriptorSet local,
                            VkDescriptorSet remote);
    void remove_descriptor_set(VkDescriptorSet set);
    VkDescriptorSet get_remote_descriptor_set(VkDescriptorSet set) const;
    VkDescriptorPool get_descriptor_set_pool(VkDescriptorSet set) const;

    // Returns true if the write differs from the cached snapshot and updates the cache.
    bool update_descriptor_write_cache(VkDescriptorSet set,
                                       const VkWriteDescriptorSet& write,
                                       const VkDescriptorBufferInfo* buffer_infos,
                                       const VkDescriptorImageInfo* image_infos,
                                       const VkBufferView* texel_views);
    void clear_descriptor_write_cache(VkDescriptorSet set);

    void add_pipeline_layout(VkDevice device,
                             VkPipelineLayout local,
                             VkPipelineLayout remote,
                             const VkPipelineLayoutCreateInfo* info);
    void remove_pipeline_layout(VkPipelineLayout layout);
    VkPipelineLayout get_remote_pipeline_layout(VkPipelineLayout layout) const;
    bool validate_push_constant_range(VkPipelineLayout layout,
                                      uint32_t offset,
                                      uint32_t size,
                                      VkShaderStageFlags stages) const;

    void add_pipeline(VkDevice device,
                      VkPipelineBindPoint bind_point,
                      VkPipeline local,
                      VkPipeline remote);
    void remove_pipeline(VkPipeline pipeline);
    VkPipeline get_remote_pipeline(VkPipeline pipeline) const;
    VkPipelineBindPoint get_pipeline_bind_point(VkPipeline pipeline) const;

    void add_pipeline_cache(VkDevice device, VkPipelineCache local, VkPipelineCache remote);
    void remove_pipeline_cache(VkPipelineCache cache);
    VkPipelineCache get_remote_pipeline_cache(VkPipelineCache cache) const;
    VkDevice get_pipeline_cache_device(VkPipelineCache cache) const;

    void add_descriptor_update_template(VkDevice device,
                                        VkDescriptorUpdateTemplate local,
                                        VkDescriptorUpdateTemplate remote,
                                        const VkDescriptorUpdateTemplateCreateInfo* info);
    void remove_descriptor_update_template(VkDevice device, VkDescriptorUpdateTemplate tmpl);
    VkDescriptorUpdateTemplate get_remote_descriptor_update_template(VkDescriptorUpdateTemplate tmpl) const;
    bool get_descriptor_update_template_info(VkDescriptorUpdateTemplate tmpl,
                                             DescriptorUpdateTemplateInfo* out_info) const;

    void remove_device_resources(VkDevice device);

private:
    template <typename T>
    static uint64_t handle_key(T handle) {
        return reinterpret_cast<uint64_t>(handle);
    }

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, ShaderModuleInfo> shader_modules_;
    std::unordered_map<uint64_t, DescriptorSetLayoutInfo> descriptor_set_layouts_;
    std::unordered_map<uint64_t, DescriptorPoolInfo> descriptor_pools_;
    std::unordered_map<uint64_t, DescriptorSetInfo> descriptor_sets_;
    std::unordered_map<uint64_t, std::unordered_map<uint32_t, DescriptorBindingSnapshot>>
        descriptor_write_cache_;
    std::unordered_map<uint64_t, PipelineLayoutInfo> pipeline_layouts_;
    std::unordered_map<uint64_t, PipelineInfo> pipelines_;
    std::unordered_map<uint64_t, PipelineCacheInfo> pipeline_caches_;
    std::unordered_map<uint64_t, DescriptorUpdateTemplateInfo> descriptor_update_templates_;
};

extern PipelineState g_pipeline_state;

} // namespace venus_plus

#endif // VENUS_PLUS_PIPELINE_STATE_H
