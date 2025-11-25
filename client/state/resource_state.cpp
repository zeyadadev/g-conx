#include "resource_state.h"

#include <algorithm>

namespace venus_plus {

ResourceState g_resource_state;

namespace {
constexpr VkDeviceSize kAutoInvalidateOnWaitThreshold = 16 * 1024 * 1024; // 16 MiB
}

void ResourceState::add_buffer(VkDevice device, VkBuffer local, VkBuffer remote, const VkBufferCreateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    BufferState state = {};
    state.device = device;
    state.remote_handle = remote;
    state.size = info.size;
    state.usage = info.usage;
    state.sharing_mode = info.sharingMode;
    state.bound_memory = VK_NULL_HANDLE;
    state.bound_offset = 0;
    state.requirements = {};
    state.requirements_cached = false;
    buffers_[handle_key(local)] = state;
}

void ResourceState::remove_buffer(VkBuffer buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    if (it == buffers_.end()) {
        return;
    }
    if (it->second.bound_memory != VK_NULL_HANDLE) {
        remove_buffer_binding_locked(buffer, it->second.bound_memory);
    }
    buffers_.erase(it);
}

bool ResourceState::has_buffer(VkBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffers_.find(handle_key(buffer)) != buffers_.end();
}

VkBuffer ResourceState::get_remote_buffer(VkBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    if (it == buffers_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

bool ResourceState::cache_buffer_requirements(VkBuffer buffer, const VkMemoryRequirements& requirements) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    if (it == buffers_.end()) {
        return false;
    }
    it->second.requirements = requirements;
    it->second.requirements_cached = true;
    return true;
}

bool ResourceState::get_cached_buffer_requirements(VkBuffer buffer, VkMemoryRequirements* out) const {
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    if (it == buffers_.end() || !it->second.requirements_cached) {
        return false;
    }
    *out = it->second.requirements;
    return true;
}

bool ResourceState::bind_buffer(VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto bit = buffers_.find(handle_key(buffer));
    auto mit = memories_.find(handle_key(memory));
    if (bit == buffers_.end() || mit == memories_.end()) {
        return false;
    }
    bit->second.bound_memory = memory;
    bit->second.bound_offset = offset;
    auto& vec = mit->second.bound_buffers;
    if (std::find(vec.begin(), vec.end(), buffer) == vec.end()) {
        vec.push_back(buffer);
    }
    if (bit->second.size <= kAutoInvalidateOnWaitThreshold) {
        mit->second.invalidate_on_wait = true;
    }
    return true;
}

void ResourceState::add_image(VkDevice device, VkImage local, VkImage remote, const VkImageCreateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    ImageState state = {};
    state.device = device;
    state.remote_handle = remote;
    state.type = info.imageType;
    state.format = info.format;
    state.extent = info.extent;
    state.mip_levels = info.mipLevels;
    state.array_layers = info.arrayLayers;
    state.samples = info.samples;
    state.tiling = info.tiling;
    state.usage = info.usage;
    state.flags = info.flags;
    state.bound_memory = VK_NULL_HANDLE;
    state.bound_offset = 0;
    state.requirements = {};
    state.requirements_cached = false;
    images_[handle_key(local)] = state;
}

void ResourceState::remove_image(VkImage image) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end()) {
        return;
    }
    if (it->second.bound_memory != VK_NULL_HANDLE) {
        remove_image_binding_locked(image, it->second.bound_memory);
    }
    images_.erase(it);
}

bool ResourceState::has_image(VkImage image) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return images_.find(handle_key(image)) != images_.end();
}

VkImage ResourceState::get_remote_image(VkImage image) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

bool ResourceState::cache_image_requirements(VkImage image, const VkMemoryRequirements& requirements) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end()) {
        return false;
    }
    it->second.requirements = requirements;
    it->second.requirements_cached = true;
    return true;
}

bool ResourceState::get_cached_image_requirements(VkImage image, VkMemoryRequirements* out) const {
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    if (it == images_.end() || !it->second.requirements_cached) {
        return false;
    }
    *out = it->second.requirements;
    return true;
}

bool ResourceState::bind_image(VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto iit = images_.find(handle_key(image));
    auto mit = memories_.find(handle_key(memory));
    if (iit == images_.end() || mit == memories_.end()) {
        return false;
    }
    iit->second.bound_memory = memory;
    iit->second.bound_offset = offset;
    auto& vec = mit->second.bound_images;
    if (std::find(vec.begin(), vec.end(), image) == vec.end()) {
        vec.push_back(image);
    }
    return true;
}

void ResourceState::add_image_view(VkDevice device, VkImageView local, VkImageView remote, VkImage image) {
    std::lock_guard<std::mutex> lock(mutex_);
    ImageViewState state = {};
    state.device = device;
    state.remote_handle = remote;
    state.image = image;
    image_views_[handle_key(local)] = state;
}

void ResourceState::remove_image_view(VkImageView view) {
    std::lock_guard<std::mutex> lock(mutex_);
    image_views_.erase(handle_key(view));
}

bool ResourceState::has_image_view(VkImageView view) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return image_views_.find(handle_key(view)) != image_views_.end();
}

VkImageView ResourceState::get_remote_image_view(VkImageView view) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = image_views_.find(handle_key(view));
    if (it == image_views_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

VkImage ResourceState::get_image_from_view(VkImageView view) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = image_views_.find(handle_key(view));
    if (it == image_views_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.image;
}

void ResourceState::add_buffer_view(VkDevice device,
                                    VkBufferView local,
                                    VkBufferView remote,
                                    VkBuffer buffer,
                                    VkFormat format,
                                    VkDeviceSize offset,
                                    VkDeviceSize range) {
    std::lock_guard<std::mutex> lock(mutex_);
    BufferViewState state = {};
    state.device = device;
    state.remote_handle = remote;
    state.buffer = buffer;
    state.format = format;
    state.offset = offset;
    state.range = range;
    buffer_views_[handle_key(local)] = state;
}

void ResourceState::remove_buffer_view(VkBufferView view) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_views_.erase(handle_key(view));
}

bool ResourceState::has_buffer_view(VkBufferView view) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_views_.find(handle_key(view)) != buffer_views_.end();
}

VkBufferView ResourceState::get_remote_buffer_view(VkBufferView view) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffer_views_.find(handle_key(view));
    if (it == buffer_views_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

VkBuffer ResourceState::get_buffer_from_view(VkBufferView view) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffer_views_.find(handle_key(view));
    if (it == buffer_views_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.buffer;
}

void ResourceState::add_sampler(VkDevice device, VkSampler local, VkSampler remote) {
    std::lock_guard<std::mutex> lock(mutex_);
    SamplerState state = {};
    state.device = device;
    state.remote_handle = remote;
    samplers_[handle_key(local)] = state;
}

void ResourceState::remove_sampler(VkSampler sampler) {
    std::lock_guard<std::mutex> lock(mutex_);
    samplers_.erase(handle_key(sampler));
}

bool ResourceState::has_sampler(VkSampler sampler) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samplers_.find(handle_key(sampler)) != samplers_.end();
}

VkSampler ResourceState::get_remote_sampler(VkSampler sampler) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = samplers_.find(handle_key(sampler));
    if (it == samplers_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

void ResourceState::add_render_pass(VkDevice device, VkRenderPass local, VkRenderPass remote) {
    std::lock_guard<std::mutex> lock(mutex_);
    RenderPassState state = {};
    state.device = device;
    state.remote_handle = remote;
    render_passes_[handle_key(local)] = state;
}

void ResourceState::remove_render_pass(VkRenderPass render_pass) {
    std::lock_guard<std::mutex> lock(mutex_);
    render_passes_.erase(handle_key(render_pass));
}

bool ResourceState::has_render_pass(VkRenderPass render_pass) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return render_passes_.find(handle_key(render_pass)) != render_passes_.end();
}

VkRenderPass ResourceState::get_remote_render_pass(VkRenderPass render_pass) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = render_passes_.find(handle_key(render_pass));
    if (it == render_passes_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

void ResourceState::add_framebuffer(VkDevice device,
                                    VkFramebuffer local,
                                    VkFramebuffer remote,
                                    VkRenderPass render_pass,
                                    const VkFramebufferCreateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    FramebufferState state = {};
    state.device = device;
    state.remote_handle = remote;
    state.render_pass = render_pass;
    if (info.attachmentCount > 0 && info.pAttachments) {
        state.attachments.assign(info.pAttachments, info.pAttachments + info.attachmentCount);
    }
    framebuffers_[handle_key(local)] = state;
}

void ResourceState::remove_framebuffer(VkFramebuffer framebuffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    framebuffers_.erase(handle_key(framebuffer));
}

bool ResourceState::has_framebuffer(VkFramebuffer framebuffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return framebuffers_.find(handle_key(framebuffer)) != framebuffers_.end();
}

VkFramebuffer ResourceState::get_remote_framebuffer(VkFramebuffer framebuffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = framebuffers_.find(handle_key(framebuffer));
    if (it == framebuffers_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

VkRenderPass ResourceState::get_framebuffer_render_pass(VkFramebuffer framebuffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = framebuffers_.find(handle_key(framebuffer));
    if (it == framebuffers_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.render_pass;
}

void ResourceState::add_memory(VkDevice device, VkDeviceMemory local, VkDeviceMemory remote, const VkMemoryAllocateInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    MemoryState state = {};
    state.device = device;
    state.remote_handle = remote;
    state.size = info.allocationSize;
    state.memory_type_index = info.memoryTypeIndex;
    memories_[handle_key(local)] = state;
}

void ResourceState::remove_memory(VkDeviceMemory memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return;
    }
    for (VkBuffer buffer : it->second.bound_buffers) {
        auto bit = buffers_.find(handle_key(buffer));
        if (bit != buffers_.end() && bit->second.bound_memory == memory) {
            bit->second.bound_memory = VK_NULL_HANDLE;
            bit->second.bound_offset = 0;
        }
    }
    for (VkImage image : it->second.bound_images) {
        auto iit = images_.find(handle_key(image));
        if (iit != images_.end() && iit->second.bound_memory == memory) {
            iit->second.bound_memory = VK_NULL_HANDLE;
            iit->second.bound_offset = 0;
        }
    }
    memories_.erase(it);
}

bool ResourceState::has_memory(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return memories_.find(handle_key(memory)) != memories_.end();
}

VkDeviceMemory ResourceState::get_remote_memory(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.remote_handle;
}

VkDeviceSize ResourceState::get_memory_size(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return 0;
    }
    return it->second.size;
}

VkDevice ResourceState::get_memory_device(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return VK_NULL_HANDLE;
    }
    return it->second.device;
}

uint32_t ResourceState::get_memory_type_index(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return UINT32_MAX;
    }
    return it->second.memory_type_index;
}

bool ResourceState::should_invalidate_on_wait(VkDeviceMemory memory) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = memories_.find(handle_key(memory));
    if (it == memories_.end()) {
        return false;
    }
    return it->second.invalidate_on_wait;
}

bool ResourceState::buffer_is_bound(VkBuffer buffer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle_key(buffer));
    return it != buffers_.end() && it->second.bound_memory != VK_NULL_HANDLE;
}

bool ResourceState::image_is_bound(VkImage image) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = images_.find(handle_key(image));
    return it != images_.end() && it->second.bound_memory != VK_NULL_HANDLE;
}

void ResourceState::remove_device_resources(VkDevice device) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint64_t> buffer_keys;
    for (const auto& pair : buffers_) {
        if (pair.second.device == device) {
            buffer_keys.push_back(pair.first);
        }
    }
    for (uint64_t key : buffer_keys) {
        VkBuffer buffer = reinterpret_cast<VkBuffer>(key);
        auto it = buffers_.find(key);
        if (it != buffers_.end()) {
            if (it->second.bound_memory != VK_NULL_HANDLE) {
                remove_buffer_binding_locked(buffer, it->second.bound_memory);
            }
            buffers_.erase(it);
        }
    }

    std::vector<uint64_t> image_keys;
    for (const auto& pair : images_) {
        if (pair.second.device == device) {
            image_keys.push_back(pair.first);
        }
    }
    for (uint64_t key : image_keys) {
        VkImage image = reinterpret_cast<VkImage>(key);
        auto it = images_.find(key);
        if (it != images_.end()) {
            if (it->second.bound_memory != VK_NULL_HANDLE) {
                remove_image_binding_locked(image, it->second.bound_memory);
            }
            images_.erase(it);
        }
    }

    std::vector<uint64_t> memory_keys;
    for (const auto& pair : memories_) {
        if (pair.second.device == device) {
            memory_keys.push_back(pair.first);
        }
    }
    for (uint64_t key : memory_keys) {
        auto it = memories_.find(key);
        if (it != memories_.end()) {
            for (VkBuffer buffer : it->second.bound_buffers) {
                auto bit = buffers_.find(handle_key(buffer));
                if (bit != buffers_.end()) {
                    bit->second.bound_memory = VK_NULL_HANDLE;
                    bit->second.bound_offset = 0;
                }
            }
            for (VkImage image : it->second.bound_images) {
                auto iit = images_.find(handle_key(image));
                if (iit != images_.end()) {
                    iit->second.bound_memory = VK_NULL_HANDLE;
                    iit->second.bound_offset = 0;
                }
            }
            memories_.erase(it);
        }
    }

    std::vector<uint64_t> image_view_keys;
    for (const auto& pair : image_views_) {
        if (pair.second.device == device) {
            image_view_keys.push_back(pair.first);
        }
    }
    for (uint64_t key : image_view_keys) {
        image_views_.erase(key);
    }

    std::vector<uint64_t> buffer_view_keys;
    for (const auto& pair : buffer_views_) {
        if (pair.second.device == device) {
            buffer_view_keys.push_back(pair.first);
        }
    }
    for (uint64_t key : buffer_view_keys) {
        buffer_views_.erase(key);
    }

    std::vector<uint64_t> sampler_keys;
    for (const auto& pair : samplers_) {
        if (pair.second.device == device) {
            sampler_keys.push_back(pair.first);
        }
    }
    for (uint64_t key : sampler_keys) {
        samplers_.erase(key);
    }

    std::vector<uint64_t> render_pass_keys;
    for (const auto& pair : render_passes_) {
        if (pair.second.device == device) {
            render_pass_keys.push_back(pair.first);
        }
    }
    for (uint64_t key : render_pass_keys) {
        render_passes_.erase(key);
    }

    std::vector<uint64_t> framebuffer_keys;
    for (const auto& pair : framebuffers_) {
        if (pair.second.device == device) {
            framebuffer_keys.push_back(pair.first);
        }
    }
    for (uint64_t key : framebuffer_keys) {
        framebuffers_.erase(key);
    }
}

void ResourceState::remove_buffer_binding_locked(VkBuffer buffer, VkDeviceMemory memory) {
    auto mit = memories_.find(handle_key(memory));
    if (mit == memories_.end()) {
        return;
    }
    auto& vec = mit->second.bound_buffers;
    vec.erase(std::remove(vec.begin(), vec.end(), buffer), vec.end());
}

void ResourceState::remove_image_binding_locked(VkImage image, VkDeviceMemory memory) {
    auto mit = memories_.find(handle_key(memory));
    if (mit == memories_.end()) {
        return;
    }
    auto& vec = mit->second.bound_images;
    vec.erase(std::remove(vec.begin(), vec.end(), image), vec.end());
}

} // namespace venus_plus
