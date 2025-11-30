#include "pipeline_state.h"

#include <algorithm>

namespace venus_plus {

PipelineState g_pipeline_state;

void PipelineState::add_shader_module(VkDevice device,
                                      VkShaderModule local,
                                      VkShaderModule remote,
                                      size_t code_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    ShaderModuleInfo info = {};
    info.device = device;
    info.remote_handle = remote;
    info.code_size = code_size;
    shader_modules_[handle_key(local)] = info;
}

void PipelineState::remove_shader_module(VkShaderModule module) {
    std::lock_guard<std::mutex> lock(mutex_);
    shader_modules_.erase(handle_key(module));
}

VkShaderModule PipelineState::get_remote_shader_module(VkShaderModule module) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = shader_modules_.find(handle_key(module));
    if (it == shader_modules_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

void PipelineState::add_descriptor_set_layout(VkDevice device,
                                              VkDescriptorSetLayout local,
                                              VkDescriptorSetLayout remote) {
    std::lock_guard<std::mutex> lock(mutex_);
    DescriptorSetLayoutInfo info = {};
    info.device = device;
    info.remote_handle = remote;
    descriptor_set_layouts_[handle_key(local)] = info;
}

void PipelineState::remove_descriptor_set_layout(VkDescriptorSetLayout layout) {
    std::lock_guard<std::mutex> lock(mutex_);
    descriptor_set_layouts_.erase(handle_key(layout));
}

VkDescriptorSetLayout PipelineState::get_remote_descriptor_set_layout(VkDescriptorSetLayout layout) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_set_layouts_.find(handle_key(layout));
    if (it == descriptor_set_layouts_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

void PipelineState::add_descriptor_pool(VkDevice device,
                                        VkDescriptorPool local,
                                        VkDescriptorPool remote,
                                        VkDescriptorPoolCreateFlags flags) {
    std::lock_guard<std::mutex> lock(mutex_);
    DescriptorPoolInfo info = {};
    info.device = device;
    info.remote_handle = remote;
    info.flags = flags;
    descriptor_pools_[handle_key(local)] = info;
}

void PipelineState::remove_descriptor_pool(VkDescriptorPool pool) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_pools_.find(handle_key(pool));
    if (it == descriptor_pools_.end()) {
        return;
    }
    for (VkDescriptorSet set : it->second.descriptor_sets) {
        descriptor_sets_.erase(handle_key(set));
        descriptor_write_cache_.erase(handle_key(set));
    }
    descriptor_pools_.erase(it);
}

void PipelineState::reset_descriptor_pool(VkDescriptorPool pool) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_pools_.find(handle_key(pool));
    if (it == descriptor_pools_.end()) {
        return;
    }
    for (VkDescriptorSet set : it->second.descriptor_sets) {
        descriptor_sets_.erase(handle_key(set));
        descriptor_write_cache_.erase(handle_key(set));
    }
    it->second.descriptor_sets.clear();
}

VkDescriptorPool PipelineState::get_remote_descriptor_pool(VkDescriptorPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_pools_.find(handle_key(pool));
    if (it == descriptor_pools_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

void PipelineState::add_descriptor_set(VkDevice device,
                                       VkDescriptorPool pool,
                                       VkDescriptorSetLayout layout,
                                       VkDescriptorSet local,
                                       VkDescriptorSet remote) {
    std::lock_guard<std::mutex> lock(mutex_);
    DescriptorSetInfo info = {};
    info.device = device;
    info.remote_handle = remote;
    info.parent_pool = pool;
    info.layout = layout;
    descriptor_sets_[handle_key(local)] = info;

    auto pit = descriptor_pools_.find(handle_key(pool));
    if (pit != descriptor_pools_.end()) {
        pit->second.descriptor_sets.push_back(local);
    }
}

void PipelineState::remove_descriptor_set(VkDescriptorSet set) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_sets_.find(handle_key(set));
    if (it == descriptor_sets_.end()) {
        return;
    }
    VkDescriptorPool pool = it->second.parent_pool;
    auto pit = descriptor_pools_.find(handle_key(pool));
    if (pit != descriptor_pools_.end()) {
        auto& vec = pit->second.descriptor_sets;
        vec.erase(std::remove(vec.begin(), vec.end(), set), vec.end());
    }
    descriptor_sets_.erase(it);
    descriptor_write_cache_.erase(handle_key(set));
}

VkDescriptorSet PipelineState::get_remote_descriptor_set(VkDescriptorSet set) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_sets_.find(handle_key(set));
    if (it == descriptor_sets_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

VkDescriptorPool PipelineState::get_descriptor_set_pool(VkDescriptorSet set) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_sets_.find(handle_key(set));
    if (it == descriptor_sets_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.parent_pool;
}

bool PipelineState::update_descriptor_write_cache(VkDescriptorSet set,
                                                  const VkWriteDescriptorSet& write,
                                                  const VkDescriptorBufferInfo* buffer_infos,
                                                  const VkDescriptorImageInfo* image_infos,
                                                  const VkBufferView* texel_views) {
    std::lock_guard<std::mutex> lock(mutex_);

    // If the set is not tracked, fall back to sending the write.
    if (descriptor_sets_.find(handle_key(set)) == descriptor_sets_.end()) {
        return true;
    }

    if (write.descriptorCount == 0) {
        return false;
    }

    auto& binding_map = descriptor_write_cache_[handle_key(set)];
    DescriptorBindingSnapshot& snapshot = binding_map[write.dstBinding];

    if (snapshot.type != write.descriptorType) {
        snapshot.type = write.descriptorType;
        snapshot.items.clear();
    }

    const uint32_t required_size = write.dstArrayElement + write.descriptorCount;
    if (snapshot.items.size() < required_size) {
        snapshot.items.resize(required_size);
    }

    bool changed = false;
    for (uint32_t j = 0; j < write.descriptorCount; ++j) {
        DescriptorWriteItemSnapshot& cached = snapshot.items[write.dstArrayElement + j];
        DescriptorWriteItemSnapshot latest = {};

        switch (write.descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            if (!buffer_infos) {
                changed = true;
                break;
            }
            latest.buffer = buffer_infos[j].buffer;
            latest.offset = buffer_infos[j].offset;
            latest.range = buffer_infos[j].range;
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            if (!image_infos) {
                changed = true;
                break;
            }
            latest.image_view = image_infos[j].imageView;
            latest.image_layout = image_infos[j].imageLayout;
            latest.sampler = image_infos[j].sampler;
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            if (!texel_views) {
                changed = true;
                break;
            }
            latest.texel_view = texel_views[j];
            break;
        default:
            // Unknown type; conservatively treat as changed.
            changed = true;
            break;
        }

        if (!changed &&
            cached.buffer == latest.buffer &&
            cached.offset == latest.offset &&
            cached.range == latest.range &&
            cached.image_view == latest.image_view &&
            cached.image_layout == latest.image_layout &&
            cached.sampler == latest.sampler &&
            cached.texel_view == latest.texel_view) {
            continue;
        }

        cached = latest;
        changed = true;
    }

    return changed;
}

void PipelineState::clear_descriptor_write_cache(VkDescriptorSet set) {
    std::lock_guard<std::mutex> lock(mutex_);
    descriptor_write_cache_.erase(handle_key(set));
}

void PipelineState::add_pipeline_layout(VkDevice device,
                                        VkPipelineLayout local,
                                        VkPipelineLayout remote,
                                        const VkPipelineLayoutCreateInfo* create_info) {
    std::lock_guard<std::mutex> lock(mutex_);
    PipelineLayoutInfo layout_info = {};
    layout_info.device = device;
    layout_info.remote_handle = remote;
    if (create_info && create_info->pPushConstantRanges && create_info->pushConstantRangeCount > 0) {
        layout_info.push_constant_ranges.assign(create_info->pPushConstantRanges,
                                                create_info->pPushConstantRanges +
                                                    create_info->pushConstantRangeCount);
    }
    pipeline_layouts_[handle_key(local)] = layout_info;
}

void PipelineState::remove_pipeline_layout(VkPipelineLayout layout) {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_layouts_.erase(handle_key(layout));
}

VkPipelineLayout PipelineState::get_remote_pipeline_layout(VkPipelineLayout layout) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipeline_layouts_.find(handle_key(layout));
    if (it == pipeline_layouts_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

bool PipelineState::validate_push_constant_range(VkPipelineLayout layout,
                                                 uint32_t offset,
                                                 uint32_t size,
                                                 VkShaderStageFlags stages) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipeline_layouts_.find(handle_key(layout));
    if (it == pipeline_layouts_.end()) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    uint64_t end = static_cast<uint64_t>(offset) + size;
    for (const auto& range : it->second.push_constant_ranges) {
        uint64_t range_end = static_cast<uint64_t>(range.offset) + range.size;
        if (offset >= range.offset && end <= range_end) {
            if ((stages & ~range.stageFlags) == 0) {
                return true;
            }
        }
    }
    return false;
}

void PipelineState::add_pipeline(VkDevice device,
                                 VkPipelineBindPoint bind_point,
                                 VkPipeline local,
                                 VkPipeline remote) {
    std::lock_guard<std::mutex> lock(mutex_);
    PipelineInfo info = {};
    info.device = device;
    info.remote_handle = remote;
    info.bind_point = bind_point;
    pipelines_[handle_key(local)] = info;
}

void PipelineState::remove_pipeline(VkPipeline pipeline) {
    std::lock_guard<std::mutex> lock(mutex_);
    pipelines_.erase(handle_key(pipeline));
}

VkPipeline PipelineState::get_remote_pipeline(VkPipeline pipeline) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipelines_.find(handle_key(pipeline));
    if (it == pipelines_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

VkPipelineBindPoint PipelineState::get_pipeline_bind_point(VkPipeline pipeline) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipelines_.find(handle_key(pipeline));
    if (it == pipelines_.end()) {
        return VK_PIPELINE_BIND_POINT_MAX_ENUM;
    }
    return it->second.bind_point;
}

void PipelineState::add_pipeline_cache(VkDevice device,
                                       VkPipelineCache local,
                                       VkPipelineCache remote) {
    std::lock_guard<std::mutex> lock(mutex_);
    PipelineCacheInfo info = {};
    info.device = device;
    info.remote_handle = remote;
    pipeline_caches_[handle_key(local)] = info;
}

void PipelineState::remove_pipeline_cache(VkPipelineCache cache) {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_caches_.erase(handle_key(cache));
}

VkPipelineCache PipelineState::get_remote_pipeline_cache(VkPipelineCache cache) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipeline_caches_.find(handle_key(cache));
    if (it == pipeline_caches_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

VkDevice PipelineState::get_pipeline_cache_device(VkPipelineCache cache) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipeline_caches_.find(handle_key(cache));
    if (it == pipeline_caches_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.device;
}

void PipelineState::remove_device_resources(VkDevice device) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = shader_modules_.begin(); it != shader_modules_.end();) {
        if (it->second.device == device) {
            it = shader_modules_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = descriptor_sets_.begin(); it != descriptor_sets_.end();) {
        if (it->second.device == device) {
            VkDescriptorPool pool = it->second.parent_pool;
            auto pit = descriptor_pools_.find(handle_key(pool));
            if (pit != descriptor_pools_.end()) {
                auto& vec = pit->second.descriptor_sets;
                vec.erase(std::remove(vec.begin(), vec.end(),
                                      reinterpret_cast<VkDescriptorSet>(it->first)),
                          vec.end());
            }
            descriptor_write_cache_.erase(it->first);
            it = descriptor_sets_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = descriptor_pools_.begin(); it != descriptor_pools_.end();) {
        if (it->second.device == device) {
            for (VkDescriptorSet set : it->second.descriptor_sets) {
                descriptor_sets_.erase(handle_key(set));
                descriptor_write_cache_.erase(handle_key(set));
            }
            it = descriptor_pools_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = descriptor_set_layouts_.begin(); it != descriptor_set_layouts_.end();) {
        if (it->second.device == device) {
            it = descriptor_set_layouts_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = pipeline_layouts_.begin(); it != pipeline_layouts_.end();) {
        if (it->second.device == device) {
            it = pipeline_layouts_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = pipelines_.begin(); it != pipelines_.end();) {
        if (it->second.device == device) {
            it = pipelines_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = pipeline_caches_.begin(); it != pipeline_caches_.end();) {
        if (it->second.device == device) {
            it = pipeline_caches_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace venus_plus
