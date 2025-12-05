#include "resource_tracker.h"

#include "binding_validator.h"
#include "utils/logging.h"
#include <algorithm>

#define RESOURCE_LOG_ERROR() VP_LOG_STREAM_ERROR(SERVER)

namespace venus_plus {

ResourceTracker::ResourceTracker()
    : next_buffer_handle_(0x40000000ull),
      next_image_handle_(0x50000000ull),
      next_memory_handle_(0x60000000ull),
      next_image_view_handle_(0x76000000ull),
      next_buffer_view_handle_(0x77000000ull),
      next_sampler_handle_(0x78000000ull),
      next_shader_module_handle_(0x70000000ull),
      next_descriptor_set_layout_handle_(0x71000000ull),
      next_descriptor_update_template_handle_(0x79000000ull),
      next_descriptor_pool_handle_(0x72000000ull),
      next_descriptor_set_handle_(0x73000000ull),
      next_pipeline_layout_handle_(0x74000000ull),
      next_pipeline_handle_(0x75000000ull),
      next_render_pass_handle_(0x7a000000ull),
      next_framebuffer_handle_(0x7b000000ull),
      next_pipeline_cache_handle_(0x7c000000ull),
      next_query_pool_handle_(0x7d000000ull) {}

void ResourceTracker::reset() {
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkRenderPass> render_passes;
    std::vector<VkPipeline> pipelines;
    std::vector<VkPipelineLayout> pipeline_layouts;
    std::vector<VkPipelineCache> pipeline_caches;
    std::vector<VkQueryPool> query_pools;
    std::vector<VkDescriptorPool> descriptor_pools;
    std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
    std::vector<VkDescriptorUpdateTemplate> descriptor_templates;
    std::vector<VkBufferView> buffer_views;
    std::vector<VkImageView> image_views;
    std::vector<VkSampler> samplers;
    std::vector<VkImage> images;
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> memories;
    std::vector<VkShaderModule> shader_modules;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        framebuffers.reserve(framebuffers_.size());
        for (const auto& kv : framebuffers_) {
            framebuffers.push_back(kv.second.handle);
        }
        render_passes.reserve(render_passes_.size());
        for (const auto& kv : render_passes_) {
            render_passes.push_back(kv.second.handle);
        }
        pipelines.reserve(pipelines_.size());
        for (const auto& kv : pipelines_) {
            pipelines.push_back(kv.second.handle);
        }
        pipeline_layouts.reserve(pipeline_layouts_.size());
        for (const auto& kv : pipeline_layouts_) {
            pipeline_layouts.push_back(kv.second.handle);
        }
        pipeline_caches.reserve(pipeline_caches_.size());
        for (const auto& kv : pipeline_caches_) {
            pipeline_caches.push_back(kv.second.handle);
        }
        query_pools.reserve(query_pools_.size());
        for (const auto& kv : query_pools_) {
            query_pools.push_back(kv.second.handle);
        }
        descriptor_pools.reserve(descriptor_pools_.size());
        for (const auto& kv : descriptor_pools_) {
            descriptor_pools.push_back(kv.second.handle);
        }
        descriptor_set_layouts.reserve(descriptor_set_layouts_.size());
        for (const auto& kv : descriptor_set_layouts_) {
            descriptor_set_layouts.push_back(kv.second.handle);
        }
        descriptor_templates.reserve(descriptor_update_templates_.size());
        for (const auto& kv : descriptor_update_templates_) {
            descriptor_templates.push_back(kv.second.handle);
        }
        buffer_views.reserve(buffer_views_.size());
        for (const auto& kv : buffer_views_) {
            buffer_views.push_back(kv.second.handle);
        }
        image_views.reserve(image_views_.size());
        for (const auto& kv : image_views_) {
            image_views.push_back(kv.second.handle);
        }
        samplers.reserve(samplers_.size());
        for (const auto& kv : samplers_) {
            samplers.push_back(kv.second.handle);
        }
        images.reserve(images_.size());
        for (const auto& kv : images_) {
            images.push_back(kv.second.handle);
        }
        buffers.reserve(buffers_.size());
        for (const auto& kv : buffers_) {
            buffers.push_back(kv.second.handle);
        }
        memories.reserve(memories_.size());
        for (const auto& kv : memories_) {
            memories.push_back(kv.second.handle);
        }
        shader_modules.reserve(shader_modules_.size());
        for (const auto& kv : shader_modules_) {
            shader_modules.push_back(kv.second.handle);
        }
    }

    // Tear down resources in rough dependency order.
    for (VkFramebuffer fb : framebuffers) {
        destroy_framebuffer(fb);
    }
    for (VkRenderPass rp : render_passes) {
        destroy_render_pass(rp);
    }
    for (VkPipeline pipeline : pipelines) {
        destroy_pipeline(pipeline);
    }
    for (VkPipelineLayout layout : pipeline_layouts) {
        destroy_pipeline_layout(layout);
    }
    for (VkPipelineCache cache : pipeline_caches) {
        destroy_pipeline_cache(cache);
    }
    for (VkQueryPool pool : query_pools) {
        destroy_query_pool(pool);
    }
    for (VkDescriptorPool pool : descriptor_pools) {
        destroy_descriptor_pool(pool);
    }
    for (VkDescriptorSetLayout layout : descriptor_set_layouts) {
        destroy_descriptor_set_layout(layout);
    }
    for (VkDescriptorUpdateTemplate tmpl : descriptor_templates) {
        destroy_descriptor_update_template(tmpl);
    }
    for (VkBufferView view : buffer_views) {
        destroy_buffer_view(view);
    }
    for (VkImageView view : image_views) {
        destroy_image_view(view);
    }
    for (VkSampler sampler : samplers) {
        destroy_sampler(sampler);
    }
    for (VkImage image : images) {
        destroy_image(image);
    }
    for (VkBuffer buffer : buffers) {
        destroy_buffer(buffer);
    }
    for (VkDeviceMemory mem : memories) {
        free_memory(mem);
    }
    for (VkShaderModule module : shader_modules) {
        destroy_shader_module(module);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    descriptor_sets_.clear();
    buffers_.clear();
    images_.clear();
    image_views_.clear();
    buffer_views_.clear();
    samplers_.clear();
    render_passes_.clear();
    framebuffers_.clear();
    memories_.clear();
    shader_modules_.clear();
    descriptor_set_layouts_.clear();
    descriptor_update_templates_.clear();
    descriptor_pools_.clear();
    pipeline_layouts_.clear();
    pipelines_.clear();
    pipeline_caches_.clear();
    query_pools_.clear();
    next_buffer_handle_ = 0x40000000ull;
    next_image_handle_ = 0x50000000ull;
    next_memory_handle_ = 0x60000000ull;
    next_image_view_handle_ = 0x76000000ull;
    next_buffer_view_handle_ = 0x77000000ull;
    next_sampler_handle_ = 0x78000000ull;
    next_shader_module_handle_ = 0x70000000ull;
    next_descriptor_set_layout_handle_ = 0x71000000ull;
    next_descriptor_update_template_handle_ = 0x79000000ull;
    next_descriptor_pool_handle_ = 0x72000000ull;
    next_descriptor_set_handle_ = 0x73000000ull;
    next_pipeline_layout_handle_ = 0x74000000ull;
    next_pipeline_handle_ = 0x75000000ull;
    next_render_pass_handle_ = 0x7a000000ull;
    next_framebuffer_handle_ = 0x7b000000ull;
    next_pipeline_cache_handle_ = 0x7c000000ull;
    next_query_pool_handle_ = 0x7d000000ull;
}

VkBuffer ResourceTracker::create_buffer(VkDevice device,
                                        VkDevice real_device,
                                        const VkBufferCreateInfo& info) {
    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkBuffer real_handle = VK_NULL_HANDLE;
    VkResult result = vkCreateBuffer(real_device, &info, nullptr, &real_handle);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateBuffer failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkBuffer handle = reinterpret_cast<VkBuffer>(next_buffer_handle_++);
    BufferResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_handle;
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
    VkBuffer real_handle = it->second.real_handle;
    VkDevice real_device = it->second.real_device;
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
    if (real_handle != VK_NULL_HANDLE) {
        vkDestroyBuffer(real_device, real_handle, nullptr);
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
    vkGetBufferMemoryRequirements(it->second.real_device, it->second.real_handle, requirements);
    it->second.requirements = *requirements;
    it->second.requirements_valid = true;
    return true;
}

VkImage ResourceTracker::create_image(VkDevice device,
                                      VkDevice real_device,
                                      const VkImageCreateInfo& info) {
    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkImage real_handle = VK_NULL_HANDLE;
    VkResult result = vkCreateImage(real_device, &info, nullptr, &real_handle);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateImage failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkImage handle = reinterpret_cast<VkImage>(next_image_handle_++);
    ImageResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_handle;
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

void ResourceTracker::register_external_image(VkDevice device,
                                              VkDevice real_device,
                                              VkImage client_handle,
                                              VkImage real_handle,
                                              const VkImageCreateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    ImageResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = client_handle;
    resource.real_handle = real_handle;
    resource.type = info.imageType;
    resource.format = info.format;
    resource.extent = info.extent;
    resource.mip_levels = std::max(1u, info.mipLevels);
    resource.array_layers = std::max(1u, info.arrayLayers);
    resource.samples = info.samples;
    resource.tiling = info.tiling;
    resource.usage = info.usage;
    resource.external = true;
    images_[handle_key(client_handle)] = resource;
}

void ResourceTracker::unregister_external_image(VkImage image) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it != images_.end()) {
        it->second.external = false;
        images_.erase(it);
    }
}

bool ResourceTracker::destroy_image(VkImage image) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end()) {
        return false;
    }
    VkImage real_handle = it->second.real_handle;
    VkDevice real_device = it->second.real_device;
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
    if (real_handle != VK_NULL_HANDLE) {
        if (!it->second.external) {
            vkDestroyImage(real_device, real_handle, nullptr);
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
    vkGetImageMemoryRequirements(it->second.real_device, it->second.real_handle, requirements);
    it->second.requirements = *requirements;
    it->second.requirements_valid = true;
    return true;
}

VkImageView ResourceTracker::create_image_view(VkDevice device,
                                               VkDevice real_device,
                                               const VkImageViewCreateInfo& info,
                                               VkImage client_image,
                                               VkImage real_image) {
    if (real_device == VK_NULL_HANDLE || real_image == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkImageViewCreateInfo real_info = info;
    real_info.image = real_image;
    VkImageView real_handle = VK_NULL_HANDLE;
    VkResult result = vkCreateImageView(real_device, &real_info, nullptr, &real_handle);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateImageView failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkImageView handle = reinterpret_cast<VkImageView>(next_image_view_handle_++);
    ImageViewResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_handle;
    resource.image = client_image;
    resource.real_image = real_image;
    image_views_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_image_view(VkImageView view) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = image_views_.find(handle_key(view));
    if (it == image_views_.end()) {
        return false;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyImageView(it->second.real_device, it->second.real_handle, nullptr);
    }
    image_views_.erase(it);
    return true;
}

VkImageView ResourceTracker::get_real_image_view(VkImageView view) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = image_views_.find(handle_key(view));
    if (it == image_views_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkBufferView ResourceTracker::create_buffer_view(VkDevice device,
                                                 VkDevice real_device,
                                                 const VkBufferViewCreateInfo& info,
                                                 VkBuffer client_buffer,
                                                 VkBuffer real_buffer) {
    if (real_device == VK_NULL_HANDLE || real_buffer == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkBufferViewCreateInfo real_info = info;
    real_info.buffer = real_buffer;
    VkBufferView real_handle = VK_NULL_HANDLE;
    VkResult result = vkCreateBufferView(real_device, &real_info, nullptr, &real_handle);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateBufferView failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkBufferView handle = reinterpret_cast<VkBufferView>(next_buffer_view_handle_++);
    BufferViewResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_handle;
    resource.buffer = client_buffer;
    resource.real_buffer = real_buffer;
    resource.format = info.format;
    resource.offset = info.offset;
    resource.range = info.range;
    buffer_views_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_buffer_view(VkBufferView view) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffer_views_.find(handle_key(view));
    if (it == buffer_views_.end()) {
        return false;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyBufferView(it->second.real_device, it->second.real_handle, nullptr);
    }
    buffer_views_.erase(it);
    return true;
}

VkBufferView ResourceTracker::get_real_buffer_view(VkBufferView view) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffer_views_.find(handle_key(view));
    if (it == buffer_views_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkSampler ResourceTracker::create_sampler(VkDevice device,
                                          VkDevice real_device,
                                          const VkSamplerCreateInfo& info) {
    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkSampler real_handle = VK_NULL_HANDLE;
    VkResult result = vkCreateSampler(real_device, &info, nullptr, &real_handle);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateSampler failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkSampler handle = reinterpret_cast<VkSampler>(next_sampler_handle_++);
    SamplerResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_handle;
    samplers_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_sampler(VkSampler sampler) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = samplers_.find(handle_key(sampler));
    if (it == samplers_.end()) {
        return false;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroySampler(it->second.real_device, it->second.real_handle, nullptr);
    }
    samplers_.erase(it);
    return true;
}

VkSampler ResourceTracker::get_real_sampler(VkSampler sampler) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = samplers_.find(handle_key(sampler));
    if (it == samplers_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkRenderPass ResourceTracker::create_render_pass(VkDevice device,
                                                 VkDevice real_device,
                                                 const VkRenderPassCreateInfo& info) {
    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkRenderPass real_handle = VK_NULL_HANDLE;
    VkResult result = vkCreateRenderPass(real_device, &info, nullptr, &real_handle);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateRenderPass failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkRenderPass handle = reinterpret_cast<VkRenderPass>(next_render_pass_handle_++);
    RenderPassResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_handle;
    render_passes_[handle_key(handle)] = resource;
    return handle;
}

VkRenderPass ResourceTracker::create_render_pass2(VkDevice device,
                                                  VkDevice real_device,
                                                  const VkRenderPassCreateInfo2* info) {
    if (real_device == VK_NULL_HANDLE || !info) {
        return VK_NULL_HANDLE;
    }
    VkRenderPass real_handle = VK_NULL_HANDLE;
    VkResult result = vkCreateRenderPass2(real_device, info, nullptr, &real_handle);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateRenderPass2 failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkRenderPass handle = reinterpret_cast<VkRenderPass>(next_render_pass_handle_++);
    RenderPassResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_handle;
    render_passes_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_render_pass(VkRenderPass render_pass) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = render_passes_.find(handle_key(render_pass));
    if (it == render_passes_.end()) {
        return false;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyRenderPass(it->second.real_device, it->second.real_handle, nullptr);
    }
    render_passes_.erase(it);
    return true;
}

VkRenderPass ResourceTracker::get_real_render_pass(VkRenderPass render_pass) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = render_passes_.find(handle_key(render_pass));
    if (it == render_passes_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkDevice ResourceTracker::get_render_pass_real_device(VkRenderPass render_pass) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = render_passes_.find(handle_key(render_pass));
    if (it == render_passes_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_device;
}

VkFramebuffer ResourceTracker::create_framebuffer(VkDevice device,
                                                  VkDevice real_device,
                                                  const VkFramebufferCreateInfo& info) {
    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkFramebuffer real_handle = VK_NULL_HANDLE;
    VkResult result = vkCreateFramebuffer(real_device, &info, nullptr, &real_handle);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateFramebuffer failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkFramebuffer handle = reinterpret_cast<VkFramebuffer>(next_framebuffer_handle_++);
    FramebufferResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_handle;
    resource.render_pass = info.renderPass;
    if (info.attachmentCount > 0 && info.pAttachments) {
        resource.attachments.assign(info.pAttachments, info.pAttachments + info.attachmentCount);
    }
    framebuffers_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_framebuffer(VkFramebuffer framebuffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = framebuffers_.find(handle_key(framebuffer));
    if (it == framebuffers_.end()) {
        return false;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(it->second.real_device, it->second.real_handle, nullptr);
    }
    framebuffers_.erase(it);
    return true;
}

VkFramebuffer ResourceTracker::get_real_framebuffer(VkFramebuffer framebuffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = framebuffers_.find(handle_key(framebuffer));
    if (it == framebuffers_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkDeviceMemory ResourceTracker::allocate_memory(VkDevice device,
                                                VkDevice real_device,
                                                const VkMemoryAllocateInfo& info) {
    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkDeviceMemory real_handle = VK_NULL_HANDLE;
    VkResult result = vkAllocateMemory(real_device, &info, nullptr, &real_handle);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkAllocateMemory failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkDeviceMemory handle = reinterpret_cast<VkDeviceMemory>(next_memory_handle_++);
    MemoryResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_handle;
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
    VkDeviceMemory real_handle = it->second.real_handle;
    VkDevice real_device = it->second.real_device;

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

    if (it->second.mapped_ptr) {
        vkUnmapMemory(real_device, real_handle);
        it->second.mapped_ptr = nullptr;
        it->second.mapped_size = 0;
    }

    if (real_handle != VK_NULL_HANDLE) {
        vkFreeMemory(real_device, real_handle, nullptr);
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

    VkResult bind_result =
        vkBindBufferMemory(buf.real_device, buf.real_handle, mem.real_handle, offset);
    if (bind_result != VK_SUCCESS) {
        if (error_message) {
            *error_message = "vkBindBufferMemory failed: " + std::to_string(bind_result);
        }
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

    VkResult bind_result =
        vkBindImageMemory(img.real_device, img.real_handle, mem.real_handle, offset);
    if (bind_result != VK_SUCCESS) {
        if (error_message) {
            *error_message = "vkBindImageMemory failed: " + std::to_string(bind_result);
        }
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
    vkGetImageSubresourceLayout(img.real_device, img.real_handle, &subresource, layout);
    return true;
}

bool ResourceTracker::buffer_exists(VkBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffers_.find(handle_key(buffer)) != buffers_.end();
}

VkBuffer ResourceTracker::get_real_buffer(VkBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    if (it == buffers_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

bool ResourceTracker::image_exists(VkImage image) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return images_.find(handle_key(image)) != images_.end();
}

VkImage ResourceTracker::get_real_image(VkImage image) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkDeviceMemory ResourceTracker::get_real_memory(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

bool ResourceTracker::get_memory_info(VkDeviceMemory memory,
                                      VkDeviceMemory* real_memory,
                                      VkDevice* real_device,
                                      VkDeviceSize* size,
                                      uint32_t* type_index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return false;
    }
    if (real_memory) {
        *real_memory = it->second.real_handle;
    }
    if (real_device) {
        *real_device = it->second.real_device;
    }
    if (size) {
        *size = it->second.size;
    }
    if (type_index) {
        *type_index = it->second.type_index;
    }
    return true;
}

VkResult ResourceTracker::get_memory_mapping(VkDeviceMemory memory,
                                             void** mapped_ptr,
                                             VkDeviceSize* size) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }
    MemoryResource& mem = it->second;
    if (!mem.mapped_ptr) {
        VkResult map_result = vkMapMemory(mem.real_device,
                                          mem.real_handle,
                                          0,
                                          mem.size,
                                          0,
                                          &mem.mapped_ptr);
        if (map_result != VK_SUCCESS) {
            mem.mapped_ptr = nullptr;
            mem.mapped_size = 0;
            return map_result;
        }
        mem.mapped_size = mem.size;
    }
    if (mapped_ptr) {
        *mapped_ptr = mem.mapped_ptr;
    }
    if (size) {
        *size = mem.mapped_size;
    }
    return VK_SUCCESS;
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

VkShaderModule ResourceTracker::create_shader_module(VkDevice device,
                                                     VkDevice real_device,
                                                     const VkShaderModuleCreateInfo& info) {
    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkShaderModule real_module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(real_device, &info, nullptr, &real_module);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateShaderModule failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkShaderModule handle = reinterpret_cast<VkShaderModule>(next_shader_module_handle_++);
    ShaderModuleResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_module;
    resource.code_size = info.codeSize;
    shader_modules_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_shader_module(VkShaderModule module) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = shader_modules_.find(handle_key(module));
    if (it == shader_modules_.end()) {
        return false;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyShaderModule(it->second.real_device, it->second.real_handle, nullptr);
    }
    shader_modules_.erase(it);
    return true;
}

VkShaderModule ResourceTracker::get_real_shader_module(VkShaderModule module) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = shader_modules_.find(handle_key(module));
    if (it == shader_modules_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkDescriptorSetLayout ResourceTracker::create_descriptor_set_layout(
    VkDevice device,
    VkDevice real_device,
    const VkDescriptorSetLayoutCreateInfo& info) {

    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetLayout real_layout = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorSetLayout(real_device, &info, nullptr, &real_layout);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateDescriptorSetLayout failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkDescriptorSetLayout handle =
        reinterpret_cast<VkDescriptorSetLayout>(next_descriptor_set_layout_handle_++);
    DescriptorSetLayoutResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_layout;
    descriptor_set_layouts_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_descriptor_set_layout(VkDescriptorSetLayout layout) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_set_layouts_.find(handle_key(layout));
    if (it == descriptor_set_layouts_.end()) {
        return false;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(it->second.real_device, it->second.real_handle, nullptr);
    }
    descriptor_set_layouts_.erase(it);
    return true;
}

VkDescriptorSetLayout
ResourceTracker::get_real_descriptor_set_layout(VkDescriptorSetLayout layout) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_set_layouts_.find(handle_key(layout));
    if (it == descriptor_set_layouts_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkDescriptorUpdateTemplate ResourceTracker::create_descriptor_update_template(
    VkDevice device,
    VkDevice real_device,
    const VkDescriptorUpdateTemplateCreateInfo& info) {

    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorUpdateTemplateCreateInfo real_info = info;
    if (info.descriptorSetLayout != VK_NULL_HANDLE) {
        real_info.descriptorSetLayout = get_real_descriptor_set_layout(info.descriptorSetLayout);
    }
    if (info.pipelineLayout != VK_NULL_HANDLE) {
        real_info.pipelineLayout = get_real_pipeline_layout(info.pipelineLayout);
    }

    VkDescriptorUpdateTemplate real_template = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorUpdateTemplate(real_device, &real_info, nullptr, &real_template);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateDescriptorUpdateTemplate failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkDescriptorUpdateTemplate handle =
        reinterpret_cast<VkDescriptorUpdateTemplate>(next_descriptor_update_template_handle_++);
    DescriptorUpdateTemplateResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_template;
    resource.template_type = info.templateType;
    resource.bind_point = info.pipelineBindPoint;
    resource.set_layout = info.descriptorSetLayout;
    resource.pipeline_layout = info.pipelineLayout;
    resource.set_number = info.set;
    if (info.descriptorUpdateEntryCount > 0 && info.pDescriptorUpdateEntries) {
        resource.entries.assign(info.pDescriptorUpdateEntries,
                                info.pDescriptorUpdateEntries + info.descriptorUpdateEntryCount);
    }
    descriptor_update_templates_[handle_key(handle)] = resource;
    return handle;
}

void ResourceTracker::destroy_descriptor_update_template(VkDescriptorUpdateTemplate tmpl) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_update_templates_.find(handle_key(tmpl));
    if (it == descriptor_update_templates_.end()) {
        return;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyDescriptorUpdateTemplate(it->second.real_device, it->second.real_handle, nullptr);
    }
    descriptor_update_templates_.erase(it);
}

VkDescriptorUpdateTemplate
ResourceTracker::get_real_descriptor_update_template(VkDescriptorUpdateTemplate tmpl) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_update_templates_.find(handle_key(tmpl));
    if (it == descriptor_update_templates_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

bool ResourceTracker::get_descriptor_update_template_info(VkDescriptorUpdateTemplate tmpl,
                                                          DescriptorUpdateTemplateResource* out_info) const {
    if (!out_info) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_update_templates_.find(handle_key(tmpl));
    if (it == descriptor_update_templates_.end()) {
        return false;
    }
    *out_info = it->second;
    return true;
}

VkDescriptorPool ResourceTracker::create_descriptor_pool(
    VkDevice device,
    VkDevice real_device,
    const VkDescriptorPoolCreateInfo& info) {

    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorPool real_pool = VK_NULL_HANDLE;
    VkResult result = vkCreateDescriptorPool(real_device, &info, nullptr, &real_pool);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateDescriptorPool failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkDescriptorPool handle =
        reinterpret_cast<VkDescriptorPool>(next_descriptor_pool_handle_++);
    DescriptorPoolResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_pool;
    resource.flags = info.flags;
    descriptor_pools_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_descriptor_pool(VkDescriptorPool pool) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_pools_.find(handle_key(pool));
    if (it == descriptor_pools_.end()) {
        return false;
    }
    for (VkDescriptorSet set : it->second.descriptor_sets) {
        descriptor_sets_.erase(handle_key(set));
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(it->second.real_device, it->second.real_handle, nullptr);
    }
    descriptor_pools_.erase(it);
    return true;
}

VkResult ResourceTracker::reset_descriptor_pool(VkDescriptorPool pool,
                                                VkDescriptorPoolResetFlags flags) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_pools_.find(handle_key(pool));
    if (it == descriptor_pools_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = vkResetDescriptorPool(it->second.real_device, it->second.real_handle, flags);
    if (result == VK_SUCCESS) {
        for (VkDescriptorSet set : it->second.descriptor_sets) {
            descriptor_sets_.erase(handle_key(set));
        }
        it->second.descriptor_sets.clear();
    }
    return result;
}

VkDescriptorPool ResourceTracker::get_real_descriptor_pool(VkDescriptorPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_pools_.find(handle_key(pool));
    if (it == descriptor_pools_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkResult ResourceTracker::allocate_descriptor_sets(
    VkDevice device,
    VkDevice real_device,
    const VkDescriptorSetAllocateInfo& info,
    std::vector<VkDescriptorSet>* out_sets) {

    if (!out_sets || info.descriptorSetCount == 0) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDescriptorSetAllocateInfo real_info = info;
    std::vector<VkDescriptorSetLayout> real_layouts(info.descriptorSetCount);
    VkDescriptorPool pool_handle = VK_NULL_HANDLE;
    VkDevice pool_real_device = VK_NULL_HANDLE;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto pool_it = descriptor_pools_.find(handle_key(info.descriptorPool));
        if (pool_it == descriptor_pools_.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        pool_handle = pool_it->second.real_handle;
        pool_real_device = pool_it->second.real_device;
        for (uint32_t i = 0; i < info.descriptorSetCount; ++i) {
            auto layout_it = descriptor_set_layouts_.find(handle_key(info.pSetLayouts[i]));
            if (layout_it == descriptor_set_layouts_.end()) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            real_layouts[i] = layout_it->second.real_handle;
        }
    }

    real_info.descriptorPool = pool_handle;
    real_info.pSetLayouts = real_layouts.data();

    std::vector<VkDescriptorSet> real_sets(info.descriptorSetCount);
    VkResult result = vkAllocateDescriptorSets(pool_real_device, &real_info, real_sets.data());
    if (result != VK_SUCCESS) {
        return result;
    }

    out_sets->resize(info.descriptorSetCount);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto pool_it = descriptor_pools_.find(handle_key(info.descriptorPool));
        if (pool_it == descriptor_pools_.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        for (uint32_t i = 0; i < info.descriptorSetCount; ++i) {
            VkDescriptorSet handle =
                reinterpret_cast<VkDescriptorSet>(next_descriptor_set_handle_++);
            DescriptorSetResource resource = {};
            resource.handle_device = device;
            resource.real_device = real_device;
            resource.handle = handle;
            resource.real_handle = real_sets[i];
            resource.pool = info.descriptorPool;
            resource.layout = info.pSetLayouts[i];
            descriptor_sets_[handle_key(handle)] = resource;
            pool_it->second.descriptor_sets.push_back(handle);
            (*out_sets)[i] = handle;
        }
    }
    return VK_SUCCESS;
}

VkResult ResourceTracker::free_descriptor_sets(
    VkDescriptorPool pool,
    const std::vector<VkDescriptorSet>& sets) {

    if (sets.empty()) {
        return VK_SUCCESS;
    }

    VkDescriptorPool real_pool = VK_NULL_HANDLE;
    VkDevice pool_real_device = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> real_sets(sets.size());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto pool_it = descriptor_pools_.find(handle_key(pool));
        if (pool_it == descriptor_pools_.end()) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        real_pool = pool_it->second.real_handle;
        pool_real_device = pool_it->second.real_device;
        for (size_t i = 0; i < sets.size(); ++i) {
            auto set_it = descriptor_sets_.find(handle_key(sets[i]));
            if (set_it == descriptor_sets_.end()) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            real_sets[i] = set_it->second.real_handle;
        }
    }

    VkResult result = vkFreeDescriptorSets(pool_real_device,
                                           real_pool,
                                           static_cast<uint32_t>(real_sets.size()),
                                           real_sets.data());
    if (result != VK_SUCCESS) {
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto pool_it = descriptor_pools_.find(handle_key(pool));
    if (pool_it == descriptor_pools_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (VkDescriptorSet set : sets) {
        descriptor_sets_.erase(handle_key(set));
        auto& vec = pool_it->second.descriptor_sets;
        vec.erase(std::remove(vec.begin(), vec.end(), set), vec.end());
    }
    return VK_SUCCESS;
}

VkDescriptorSet ResourceTracker::get_real_descriptor_set(VkDescriptorSet set) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = descriptor_sets_.find(handle_key(set));
    if (it == descriptor_sets_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkPipelineLayout ResourceTracker::create_pipeline_layout(
    VkDevice device,
    VkDevice real_device,
    const VkPipelineLayoutCreateInfo& info) {

    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    std::vector<VkDescriptorSetLayout> real_layouts(info.setLayoutCount);
    VkPipelineLayoutCreateInfo real_info = info;
    if (info.setLayoutCount > 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t i = 0; i < info.setLayoutCount; ++i) {
            auto it = descriptor_set_layouts_.find(handle_key(info.pSetLayouts[i]));
            if (it == descriptor_set_layouts_.end()) {
                return VK_NULL_HANDLE;
            }
            real_layouts[i] = it->second.real_handle;
        }
        real_info.pSetLayouts = real_layouts.data();
    }

    VkPipelineLayout real_layout = VK_NULL_HANDLE;
    VkResult result = vkCreatePipelineLayout(real_device, &real_info, nullptr, &real_layout);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreatePipelineLayout failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkPipelineLayout handle =
        reinterpret_cast<VkPipelineLayout>(next_pipeline_layout_handle_++);
    PipelineLayoutResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_layout;
    pipeline_layouts_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_pipeline_layout(VkPipelineLayout layout) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipeline_layouts_.find(handle_key(layout));
    if (it == pipeline_layouts_.end()) {
        return false;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(it->second.real_device, it->second.real_handle, nullptr);
    }
    pipeline_layouts_.erase(it);
    return true;
}

VkPipelineLayout ResourceTracker::get_real_pipeline_layout(VkPipelineLayout layout) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipeline_layouts_.find(handle_key(layout));
    if (it == pipeline_layouts_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkResult ResourceTracker::create_compute_pipelines(
    VkDevice device,
    VkDevice real_device,
    VkPipelineCache cache,
    uint32_t count,
    const VkComputePipelineCreateInfo* infos,
    std::vector<VkPipeline>* out_pipelines) {

    if (!infos || !out_pipelines || count == 0 || real_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkComputePipelineCreateInfo> real_infos(count);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t i = 0; i < count; ++i) {
            real_infos[i] = infos[i];
            auto module_it = shader_modules_.find(handle_key(infos[i].stage.module));
            if (module_it == shader_modules_.end()) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            real_infos[i].stage.module = module_it->second.real_handle;

            auto layout_it = pipeline_layouts_.find(handle_key(infos[i].layout));
            if (layout_it == pipeline_layouts_.end()) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            real_infos[i].layout = layout_it->second.real_handle;

            if (infos[i].basePipelineHandle != VK_NULL_HANDLE) {
                auto base_it = pipelines_.find(handle_key(infos[i].basePipelineHandle));
                if (base_it == pipelines_.end()) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                real_infos[i].basePipelineHandle = base_it->second.real_handle;
            }
        }
    }

    std::vector<VkPipeline> real_handles(count, VK_NULL_HANDLE);
    VkResult result = vkCreateComputePipelines(real_device,
                                               cache,
                                               count,
                                               real_infos.data(),
                                               nullptr,
                                               real_handles.data());
    if (result != VK_SUCCESS) {
        for (VkPipeline pipe : real_handles) {
            if (pipe != VK_NULL_HANDLE) {
                vkDestroyPipeline(real_device, pipe, nullptr);
            }
        }
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    out_pipelines->resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkPipeline handle = reinterpret_cast<VkPipeline>(next_pipeline_handle_++);
        PipelineResource resource = {};
        resource.handle_device = device;
        resource.real_device = real_device;
        resource.handle = handle;
        resource.real_handle = real_handles[i];
        resource.bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
        pipelines_[handle_key(handle)] = resource;
        (*out_pipelines)[i] = handle;
    }
    return VK_SUCCESS;
}

VkResult ResourceTracker::create_graphics_pipelines(
    VkDevice device,
    VkDevice real_device,
    VkPipelineCache cache,
    uint32_t count,
    const VkGraphicsPipelineCreateInfo* infos,
    std::vector<VkPipeline>* out_pipelines) {

    if (!infos || !out_pipelines || count == 0 || real_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    std::vector<VkGraphicsPipelineCreateInfo> real_infos(count);
    std::vector<std::vector<VkPipelineShaderStageCreateInfo>> stage_infos(count);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (uint32_t i = 0; i < count; ++i) {
            real_infos[i] = infos[i];

            stage_infos[i].resize(infos[i].stageCount);
            for (uint32_t j = 0; j < infos[i].stageCount; ++j) {
                stage_infos[i][j] = infos[i].pStages[j];
                auto module_it = shader_modules_.find(handle_key(infos[i].pStages[j].module));
                if (module_it == shader_modules_.end()) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                stage_infos[i][j].module = module_it->second.real_handle;
            }
            if (!stage_infos[i].empty()) {
                real_infos[i].pStages = stage_infos[i].data();
            }

            if (infos[i].layout != VK_NULL_HANDLE) {
                auto layout_it = pipeline_layouts_.find(handle_key(infos[i].layout));
                if (layout_it == pipeline_layouts_.end()) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                real_infos[i].layout = layout_it->second.real_handle;
            }

            if (infos[i].renderPass != VK_NULL_HANDLE) {
                auto rp_it = render_passes_.find(handle_key(infos[i].renderPass));
                if (rp_it == render_passes_.end()) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                real_infos[i].renderPass = rp_it->second.real_handle;
            }

            if (infos[i].basePipelineHandle != VK_NULL_HANDLE) {
                auto base_it = pipelines_.find(handle_key(infos[i].basePipelineHandle));
                if (base_it == pipelines_.end()) {
                    return VK_ERROR_INITIALIZATION_FAILED;
                }
                real_infos[i].basePipelineHandle = base_it->second.real_handle;
            }
        }
    }

    std::vector<VkPipeline> real_handles(count, VK_NULL_HANDLE);
    VkResult result = vkCreateGraphicsPipelines(real_device,
                                                cache,
                                                count,
                                                real_infos.data(),
                                                nullptr,
                                                real_handles.data());
    if (result != VK_SUCCESS) {
        for (VkPipeline pipe : real_handles) {
            if (pipe != VK_NULL_HANDLE) {
                vkDestroyPipeline(real_device, pipe, nullptr);
            }
        }
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    out_pipelines->resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkPipeline handle = reinterpret_cast<VkPipeline>(next_pipeline_handle_++);
        PipelineResource resource = {};
        resource.handle_device = device;
        resource.real_device = real_device;
        resource.handle = handle;
        resource.real_handle = real_handles[i];
        resource.bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
        pipelines_[handle_key(handle)] = resource;
        (*out_pipelines)[i] = handle;
    }
    return VK_SUCCESS;
}

bool ResourceTracker::destroy_pipeline(VkPipeline pipeline) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipelines_.find(handle_key(pipeline));
    if (it == pipelines_.end()) {
        return false;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyPipeline(it->second.real_device, it->second.real_handle, nullptr);
    }
    pipelines_.erase(it);
    return true;
}

VkPipeline ResourceTracker::get_real_pipeline(VkPipeline pipeline) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipelines_.find(handle_key(pipeline));
    if (it == pipelines_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkPipelineCache ResourceTracker::create_pipeline_cache(VkDevice device,
                                                       VkDevice real_device,
                                                       const VkPipelineCacheCreateInfo* info) {
    if (!info || real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkPipelineCache real_cache = VK_NULL_HANDLE;
    VkResult result = vkCreatePipelineCache(real_device, info, nullptr, &real_cache);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreatePipelineCache failed: " << result;
        return VK_NULL_HANDLE;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    VkPipelineCache handle = reinterpret_cast<VkPipelineCache>(next_pipeline_cache_handle_++);
    PipelineCacheResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_cache;
    pipeline_caches_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_pipeline_cache(VkPipelineCache cache) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipeline_caches_.find(handle_key(cache));
    if (it == pipeline_caches_.end()) {
        return false;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(it->second.real_device, it->second.real_handle, nullptr);
    }
    pipeline_caches_.erase(it);
    return true;
}

VkPipelineCache ResourceTracker::get_real_pipeline_cache(VkPipelineCache cache) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipeline_caches_.find(handle_key(cache));
    if (it == pipeline_caches_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkDevice ResourceTracker::get_pipeline_cache_real_device(VkPipelineCache cache) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pipeline_caches_.find(handle_key(cache));
    if (it == pipeline_caches_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_device;
}

VkQueryPool ResourceTracker::create_query_pool(VkDevice device,
                                               VkDevice real_device,
                                               const VkQueryPoolCreateInfo* info) {
    if (!info || real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkQueryPool real_pool = VK_NULL_HANDLE;
    VkResult result = vkCreateQueryPool(real_device, info, nullptr, &real_pool);
    if (result != VK_SUCCESS) {
        RESOURCE_LOG_ERROR() << "vkCreateQueryPool failed: " << result;
        return VK_NULL_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    VkQueryPool handle = reinterpret_cast<VkQueryPool>(next_query_pool_handle_++);
    QueryPoolResource resource = {};
    resource.handle_device = device;
    resource.real_device = real_device;
    resource.handle = handle;
    resource.real_handle = real_pool;
    resource.type = info->queryType;
    resource.query_count = info->queryCount;
    resource.statistics = info->pipelineStatistics;
    query_pools_[handle_key(handle)] = resource;
    return handle;
}

bool ResourceTracker::destroy_query_pool(VkQueryPool pool) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = query_pools_.find(handle_key(pool));
    if (it == query_pools_.end()) {
        return false;
    }
    if (it->second.real_handle != VK_NULL_HANDLE) {
        vkDestroyQueryPool(it->second.real_device, it->second.real_handle, nullptr);
    }
    query_pools_.erase(it);
    return true;
}

VkQueryPool ResourceTracker::get_real_query_pool(VkQueryPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = query_pools_.find(handle_key(pool));
    if (it == query_pools_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_handle;
}

VkDevice ResourceTracker::get_query_pool_real_device(VkQueryPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = query_pools_.find(handle_key(pool));
    if (it == query_pools_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.real_device;
}

VkQueryType ResourceTracker::get_query_pool_type(VkQueryPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = query_pools_.find(handle_key(pool));
    if (it == query_pools_.end()) {
        return VK_QUERY_TYPE_MAX_ENUM;
    }
    return it->second.type;
}

uint32_t ResourceTracker::get_query_pool_count(VkQueryPool pool) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = query_pools_.find(handle_key(pool));
    if (it == query_pools_.end()) {
        return 0;
    }
    return it->second.query_count;
}

} // namespace venus_plus
