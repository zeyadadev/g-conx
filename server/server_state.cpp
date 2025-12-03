#include "server_state.h"
#include "server_state_bridge.h"
#include "utils/logging.h"
#include <algorithm>
#include <string>
#include <vector>
#include <cstdlib>

#define SERVER_LOG_ERROR() VP_LOG_STREAM_ERROR(SERVER)
#define SERVER_LOG_INFO() VP_LOG_STREAM_INFO(SERVER)

ServerState::ServerState()
    : resource_tracker(),
      command_buffer_state(),
      command_validator(&resource_tracker) {}

bool ServerState::initialize_vulkan(bool enable_validation) {
    venus_plus::VulkanContextCreateInfo info = {};
    info.enable_validation = enable_validation;
    if (!vulkan_context.initialize(info)) {
        SERVER_LOG_ERROR() << "Failed to initialize Vulkan context";
        return false;
    }

    real_instance = vulkan_context.instance();

    uint32_t physical_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(real_instance, &physical_count, nullptr);
    if (result != VK_SUCCESS || physical_count == 0) {
        SERVER_LOG_ERROR() << "No physical devices available (result=" << result << ")";
        return false;
    }

    std::vector<VkPhysicalDevice> physical_devices(physical_count);
    result = vkEnumeratePhysicalDevices(real_instance, &physical_count, physical_devices.data());
    if (result != VK_SUCCESS) {
        SERVER_LOG_ERROR() << "Failed to enumerate physical devices (result=" << result << ")";
        return false;
    }

    VkPhysicalDeviceProperties chosen_props = {};
    VkPhysicalDevice chosen_device = VK_NULL_HANDLE;
    for (VkPhysicalDevice device : physical_devices) {
        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(device, &props);
        if (chosen_device == VK_NULL_HANDLE) {
            chosen_device = device;
            chosen_props = props;
        }
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            chosen_device = device;
            chosen_props = props;
            break;
        }
    }

    if (chosen_device == VK_NULL_HANDLE) {
        SERVER_LOG_ERROR() << "Failed to pick a physical device";
        return false;
    }

    real_physical_device = chosen_device;
    physical_device_properties = chosen_props;
    vkGetPhysicalDeviceMemoryProperties(real_physical_device, &physical_device_memory_properties);

    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(real_physical_device, &queue_count, nullptr);
    queue_family_properties.resize(queue_count);
    if (queue_count > 0) {
        vkGetPhysicalDeviceQueueFamilyProperties(real_physical_device,
                                                 &queue_count,
                                                 queue_family_properties.data());
    }

    SERVER_LOG_INFO() << "Selected GPU: " << physical_device_properties.deviceName;
    return true;
}

void ServerState::shutdown_vulkan() {
    queue_family_properties.clear();
    physical_device_info_map.clear();
    instance_info_map.clear();
    real_physical_device = VK_NULL_HANDLE;
    real_instance = VK_NULL_HANDLE;
    fake_device_handle = VK_NULL_HANDLE;
    vulkan_context.shutdown();
}

namespace venus_plus {

static const VkSemaphoreTypeCreateInfo* find_semaphore_type_info(const void* pNext) {
    const VkBaseInStructure* header = reinterpret_cast<const VkBaseInStructure*>(pNext);
    while (header) {
        if (header->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) {
            return reinterpret_cast<const VkSemaphoreTypeCreateInfo*>(header);
        }
        header = header->pNext;
    }
    return nullptr;
}

static const VkTimelineSemaphoreSubmitInfo* find_timeline_submit_info(const void* pNext) {
    const VkBaseInStructure* header = reinterpret_cast<const VkBaseInStructure*>(pNext);
    while (header) {
        if (header->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
            return reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(header);
        }
        header = header->pNext;
    }
    return nullptr;
}

VkInstance server_state_alloc_instance(ServerState* state) {
    if (state->real_instance == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkInstance handle = reinterpret_cast<VkInstance>(state->next_instance_handle++);
    state->instance_map.insert(handle, state->real_instance);
    InstanceInfo info = {};
    info.client_handle = handle;
    info.real_handle = state->real_instance;
    state->instance_info_map[handle] = info;
    return handle;
}

void server_state_remove_instance(ServerState* state, VkInstance instance) {
    state->instance_map.remove(instance);
    state->instance_info_map.erase(instance);
}

bool server_state_instance_exists(const ServerState* state, VkInstance instance) {
    return state->instance_map.exists(instance);
}

VkInstance server_state_get_real_instance(const ServerState* state, VkInstance instance) {
    return state->instance_map.lookup(instance);
}

VkPhysicalDevice server_state_get_fake_device(ServerState* state) {
    if (state->fake_device_handle == VK_NULL_HANDLE) {
        if (state->real_physical_device == VK_NULL_HANDLE) {
            return VK_NULL_HANDLE;
        }
        state->fake_device_handle =
            reinterpret_cast<VkPhysicalDevice>(state->next_physical_device_handle++);
        state->physical_device_map.insert(state->fake_device_handle, state->real_physical_device);

        PhysicalDeviceInfo info = {};
        info.client_handle = state->fake_device_handle;
        info.real_handle = state->real_physical_device;
        info.properties = state->physical_device_properties;
        info.memory_properties = state->physical_device_memory_properties;
        info.queue_families = state->queue_family_properties;
        state->physical_device_info_map[info.client_handle] = info;
    }
    return state->fake_device_handle;
}

VkPhysicalDevice server_state_get_real_physical_device(const ServerState* state,
                                                       VkPhysicalDevice physical_device) {
    return state->physical_device_map.lookup(physical_device);
}

// Phase 3: Device management
VkDevice server_state_alloc_device(ServerState* state,
                                   VkPhysicalDevice physical_device,
                                   VkDevice real_device) {
    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkDevice handle = reinterpret_cast<VkDevice>(state->next_device_handle++);
    state->device_map.insert(handle, real_device);

    DeviceInfo info = {};
    info.client_handle = handle;
    info.real_handle = real_device;
    info.client_physical_device = physical_device;
    info.real_physical_device = server_state_get_real_physical_device(state, physical_device);
    state->device_info_map[handle] = info;

    return handle;
}

void server_state_remove_device(ServerState* state, VkDevice device) {
    // Remove all queues associated with this device
    auto it = state->device_info_map.find(device);
    if (it != state->device_info_map.end()) {
        for (const auto& queue_info : it->second.queues) {
            state->queue_map.remove(queue_info.client_handle);
            state->queue_info_map.erase(queue_info.client_handle);
        }
        state->device_info_map.erase(it);
    }
    state->device_map.remove(device);
    state->sync_manager.remove_device(device);
}

bool server_state_device_exists(const ServerState* state, VkDevice device) {
    return state->device_map.exists(device);
}

VkPhysicalDevice server_state_get_device_physical_device(const ServerState* state, VkDevice device) {
    auto it = state->device_info_map.find(device);
    if (it != state->device_info_map.end()) {
        return it->second.client_physical_device;
    }
    return VK_NULL_HANDLE;
}

VkDevice server_state_get_real_device(const ServerState* state, VkDevice device) {
    return state->device_map.lookup(device);
}

// Phase 3: Queue management
VkQueue server_state_alloc_queue(ServerState* state,
                                 VkDevice device,
                                 uint32_t family_index,
                                 uint32_t queue_index,
                                 VkQueue real_queue) {
    if (real_queue == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkQueue handle = reinterpret_cast<VkQueue>(state->next_queue_handle++);
    state->queue_map.insert(handle, real_queue);

    QueueInfo queue_info = {};
    queue_info.client_handle = handle;
    queue_info.real_handle = real_queue;
    queue_info.family_index = family_index;
    queue_info.queue_index = queue_index;
    state->queue_info_map[handle] = queue_info;

    auto it = state->device_info_map.find(device);
    if (it != state->device_info_map.end()) {
        it->second.queues.push_back(queue_info);
    }

    return handle;
}

VkQueue server_state_find_queue(const ServerState* state, VkDevice device, uint32_t family_index, uint32_t queue_index) {
    auto it = state->device_info_map.find(device);
    if (it != state->device_info_map.end()) {
        for (const auto& queue_info : it->second.queues) {
            if (queue_info.family_index == family_index && queue_info.queue_index == queue_index) {
                return queue_info.client_handle;
            }
        }
    }
    return VK_NULL_HANDLE;
}

VkQueue server_state_get_real_queue(const ServerState* state, VkQueue queue) {
    return state->queue_map.lookup(queue);
}

VkDeviceMemory server_state_alloc_memory(ServerState* state, VkDevice device, const VkMemoryAllocateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.allocate_memory(device, real_device, *info);
}

bool server_state_free_memory(ServerState* state, VkDeviceMemory memory) {
    return state->resource_tracker.free_memory(memory);
}

VkBuffer server_state_create_buffer(ServerState* state, VkDevice device, const VkBufferCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_buffer(device, real_device, *info);
}

bool server_state_destroy_buffer(ServerState* state, VkBuffer buffer) {
    return state->resource_tracker.destroy_buffer(buffer);
}

bool server_state_get_buffer_memory_requirements(ServerState* state, VkBuffer buffer, VkMemoryRequirements* requirements) {
    return state->resource_tracker.get_buffer_requirements(buffer, requirements);
}

VkResult server_state_bind_buffer_memory(ServerState* state,
                                         VkBuffer buffer,
                                         VkDeviceMemory memory,
                                         VkDeviceSize offset) {
    std::string error;
    if (!state->resource_tracker.bind_buffer_memory(buffer, memory, offset, &error)) {
        if (!error.empty()) {
            SERVER_LOG_ERROR() << error;
        }
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    return VK_SUCCESS;
}

VkImage server_state_create_image(ServerState* state, VkDevice device, const VkImageCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_image(device, real_device, *info);
}

bool server_state_destroy_image(ServerState* state, VkImage image) {
    return state->resource_tracker.destroy_image(image);
}

bool server_state_get_image_memory_requirements(ServerState* state, VkImage image, VkMemoryRequirements* requirements) {
    return state->resource_tracker.get_image_requirements(image, requirements);
}

VkResult server_state_bind_image_memory(ServerState* state,
                                        VkImage image,
                                        VkDeviceMemory memory,
                                        VkDeviceSize offset) {
    std::string error;
    if (!state->resource_tracker.bind_image_memory(image, memory, offset, &error)) {
        if (!error.empty()) {
            SERVER_LOG_ERROR() << error;
        }
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    return VK_SUCCESS;
}

bool server_state_get_image_subresource_layout(ServerState* state,
                                               VkImage image,
                                               const VkImageSubresource* subresource,
                                               VkSubresourceLayout* layout) {
    return state->resource_tracker.get_image_subresource_layout(image, *subresource, layout);
}

VkImageView server_state_create_image_view(ServerState* state,
                                           VkDevice device,
                                           const VkImageViewCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    VkImage real_image = server_state_get_real_image(state, info->image);
    if (real_device == VK_NULL_HANDLE || real_image == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkImageViewCreateInfo real_info = *info;
    real_info.image = real_image;
    return state->resource_tracker.create_image_view(device, real_device, real_info, info->image, real_image);
}

bool server_state_destroy_image_view(ServerState* state, VkImageView view) {
    return state->resource_tracker.destroy_image_view(view);
}

VkImageView server_state_get_real_image_view(const ServerState* state, VkImageView view) {
    return state->resource_tracker.get_real_image_view(view);
}

VkBufferView server_state_create_buffer_view(ServerState* state,
                                             VkDevice device,
                                             const VkBufferViewCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    VkBuffer real_buffer = server_state_get_real_buffer(state, info->buffer);
    if (real_device == VK_NULL_HANDLE || real_buffer == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    VkBufferViewCreateInfo real_info = *info;
    real_info.buffer = real_buffer;
    return state->resource_tracker.create_buffer_view(device, real_device, real_info, info->buffer, real_buffer);
}

bool server_state_destroy_buffer_view(ServerState* state, VkBufferView view) {
    return state->resource_tracker.destroy_buffer_view(view);
}

VkBufferView server_state_get_real_buffer_view(const ServerState* state, VkBufferView view) {
    return state->resource_tracker.get_real_buffer_view(view);
}

VkSampler server_state_create_sampler(ServerState* state,
                                      VkDevice device,
                                      const VkSamplerCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    return state->resource_tracker.create_sampler(device, real_device, *info);
}

bool server_state_destroy_sampler(ServerState* state, VkSampler sampler) {
    return state->resource_tracker.destroy_sampler(sampler);
}

VkSampler server_state_get_real_sampler(const ServerState* state, VkSampler sampler) {
    return state->resource_tracker.get_real_sampler(sampler);
}

VkBuffer server_state_get_real_buffer(const ServerState* state, VkBuffer buffer) {
    return state->resource_tracker.get_real_buffer(buffer);
}

VkImage server_state_get_real_image(const ServerState* state, VkImage image) {
    return state->resource_tracker.get_real_image(image);
}

VkDeviceMemory server_state_get_real_memory(const ServerState* state, VkDeviceMemory memory) {
    return state->resource_tracker.get_real_memory(memory);
}

VkShaderModule server_state_create_shader_module(ServerState* state,
                                                 VkDevice device,
                                                 const VkShaderModuleCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_shader_module(device, real_device, *info);
}

bool server_state_destroy_shader_module(ServerState* state, VkShaderModule module) {
    return state->resource_tracker.destroy_shader_module(module);
}

VkShaderModule server_state_get_real_shader_module(const ServerState* state,
                                                   VkShaderModule module) {
    return state->resource_tracker.get_real_shader_module(module);
}

VkDescriptorSetLayout server_state_create_descriptor_set_layout(
    ServerState* state,
    VkDevice device,
    const VkDescriptorSetLayoutCreateInfo* info) {

    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_descriptor_set_layout(device, real_device, *info);
}

bool server_state_destroy_descriptor_set_layout(ServerState* state,
                                                VkDescriptorSetLayout layout) {
    return state->resource_tracker.destroy_descriptor_set_layout(layout);
}

VkDescriptorSetLayout server_state_get_real_descriptor_set_layout(
    const ServerState* state,
    VkDescriptorSetLayout layout) {
    return state->resource_tracker.get_real_descriptor_set_layout(layout);
}

VkDescriptorUpdateTemplate server_state_create_descriptor_update_template(
    ServerState* state,
    VkDevice device,
    const VkDescriptorUpdateTemplateCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_descriptor_update_template(device, real_device, *info);
}

void server_state_destroy_descriptor_update_template(ServerState* state,
                                                     VkDescriptorUpdateTemplate tmpl) {
    state->resource_tracker.destroy_descriptor_update_template(tmpl);
}

VkDescriptorUpdateTemplate server_state_get_real_descriptor_update_template(
    const ServerState* state,
    VkDescriptorUpdateTemplate tmpl) {
    return state->resource_tracker.get_real_descriptor_update_template(tmpl);
}

bool server_state_get_descriptor_update_template_info(const ServerState* state,
                                                      VkDescriptorUpdateTemplate tmpl,
                                                      DescriptorUpdateTemplateInfoBridge* out_info) {
    if (!state || !out_info) {
        return false;
    }
    venus_plus::ResourceTracker::DescriptorUpdateTemplateResource info = {};
    if (!state->resource_tracker.get_descriptor_update_template_info(tmpl, &info)) {
        return false;
    }

    out_info->template_type = info.template_type;
    out_info->bind_point = info.bind_point;
    out_info->set_layout = info.set_layout;
    out_info->pipeline_layout = info.pipeline_layout;
    out_info->set_number = info.set_number;
    out_info->entry_count = static_cast<uint32_t>(info.entries.size());
    if (out_info->entry_count > 0) {
        out_info->entries = static_cast<VkDescriptorUpdateTemplateEntry*>(
            std::calloc(out_info->entry_count, sizeof(VkDescriptorUpdateTemplateEntry)));
        if (!out_info->entries) {
            out_info->entry_count = 0;
            out_info->entries = nullptr;
            return false;
        }
        for (uint32_t i = 0; i < out_info->entry_count; ++i) {
            out_info->entries[i] = info.entries[i];
        }
    } else {
        out_info->entries = nullptr;
    }
    return true;
}

VkDescriptorPool server_state_create_descriptor_pool(ServerState* state,
                                                     VkDevice device,
                                                     const VkDescriptorPoolCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_descriptor_pool(device, real_device, *info);
}

bool server_state_destroy_descriptor_pool(ServerState* state, VkDescriptorPool pool) {
    return state->resource_tracker.destroy_descriptor_pool(pool);
}

VkResult server_state_reset_descriptor_pool(ServerState* state,
                                            VkDescriptorPool pool,
                                            VkDescriptorPoolResetFlags flags) {
    return state->resource_tracker.reset_descriptor_pool(pool, flags);
}

VkDescriptorPool server_state_get_real_descriptor_pool(const ServerState* state,
                                                       VkDescriptorPool pool) {
    return state->resource_tracker.get_real_descriptor_pool(pool);
}

VkResult server_state_allocate_descriptor_sets(ServerState* state,
                                               VkDevice device,
                                               const VkDescriptorSetAllocateInfo* info,
                                               std::vector<VkDescriptorSet>* out_sets) {
    if (!info || !out_sets) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.allocate_descriptor_sets(device, real_device, *info, out_sets);
}

VkResult server_state_free_descriptor_sets(ServerState* state,
                                           VkDevice device,
                                           VkDescriptorPool pool,
                                           uint32_t descriptorSetCount,
                                           const VkDescriptorSet* pDescriptorSets) {
    if (descriptorSetCount > 0 && !pDescriptorSets) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::vector<VkDescriptorSet> sets(descriptorSetCount);
    for (uint32_t i = 0; i < descriptorSetCount; ++i) {
        sets[i] = pDescriptorSets[i];
    }
    return state->resource_tracker.free_descriptor_sets(pool, sets);
}

VkDescriptorSet server_state_get_real_descriptor_set(const ServerState* state,
                                                     VkDescriptorSet set) {
    return state->resource_tracker.get_real_descriptor_set(set);
}

VkPipelineLayout server_state_create_pipeline_layout(ServerState* state,
                                                     VkDevice device,
                                                     const VkPipelineLayoutCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_pipeline_layout(device, real_device, *info);
}

bool server_state_destroy_pipeline_layout(ServerState* state, VkPipelineLayout layout) {
    return state->resource_tracker.destroy_pipeline_layout(layout);
}

VkPipelineLayout server_state_get_real_pipeline_layout(const ServerState* state,
                                                       VkPipelineLayout layout) {
    return state->resource_tracker.get_real_pipeline_layout(layout);
}

VkPipelineCache server_state_create_pipeline_cache(ServerState* state,
                                                   VkDevice device,
                                                   const VkPipelineCacheCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_pipeline_cache(device, real_device, info);
}

bool server_state_destroy_pipeline_cache(ServerState* state, VkPipelineCache cache) {
    return state->resource_tracker.destroy_pipeline_cache(cache);
}

VkPipelineCache server_state_get_real_pipeline_cache(const ServerState* state,
                                                     VkPipelineCache cache) {
    return state->resource_tracker.get_real_pipeline_cache(cache);
}

VkResult server_state_get_pipeline_cache_data(ServerState* state,
                                              VkDevice device,
                                              VkPipelineCache cache,
                                              size_t* pDataSize,
                                              void* pData) {
    VkDevice real_device = server_state_get_real_device(state, device);
    VkPipelineCache real_cache = state->resource_tracker.get_real_pipeline_cache(cache);
    if (real_device == VK_NULL_HANDLE || real_cache == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return vkGetPipelineCacheData(real_device, real_cache, pDataSize, pData);
}

VkResult server_state_merge_pipeline_caches(ServerState* state,
                                            VkDevice device,
                                            VkPipelineCache dst_cache,
                                            uint32_t src_count,
                                            const VkPipelineCache* src_caches) {
    if (!src_caches || !src_count) {
        return VK_SUCCESS;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    VkPipelineCache real_dst = state->resource_tracker.get_real_pipeline_cache(dst_cache);
    if (real_device == VK_NULL_HANDLE || real_dst == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::vector<VkPipelineCache> real_src(src_count);
    for (uint32_t i = 0; i < src_count; ++i) {
        real_src[i] = state->resource_tracker.get_real_pipeline_cache(src_caches[i]);
        if (real_src[i] == VK_NULL_HANDLE) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    return vkMergePipelineCaches(real_device, real_dst, src_count, real_src.data());
}

VkQueryPool server_state_create_query_pool(ServerState* state,
                                           VkDevice device,
                                           const VkQueryPoolCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_query_pool(device, real_device, info);
}

bool server_state_destroy_query_pool(ServerState* state, VkQueryPool pool) {
    return state->resource_tracker.destroy_query_pool(pool);
}

VkQueryPool server_state_get_real_query_pool(const ServerState* state, VkQueryPool pool) {
    return state->resource_tracker.get_real_query_pool(pool);
}

VkDevice server_state_get_query_pool_real_device(const ServerState* state, VkQueryPool pool) {
    return state->resource_tracker.get_query_pool_real_device(pool);
}

uint32_t server_state_get_query_pool_count(const ServerState* state, VkQueryPool pool) {
    return state->resource_tracker.get_query_pool_count(pool);
}

VkResult server_state_get_query_pool_results(ServerState* state,
                                             VkDevice device,
                                             VkQueryPool pool,
                                             uint32_t first_query,
                                             uint32_t query_count,
                                             size_t dataSize,
                                             void* pData,
                                             VkDeviceSize stride,
                                             VkQueryResultFlags flags) {
    VkDevice real_device = server_state_get_real_device(state, device);
    VkQueryPool real_pool = state->resource_tracker.get_real_query_pool(pool);
    if (real_device == VK_NULL_HANDLE || real_pool == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return vkGetQueryPoolResults(real_device,
                                 real_pool,
                                 first_query,
                                 query_count,
                                 dataSize,
                                 pData,
                                 stride,
                                 flags);
}

VkRenderPass server_state_create_render_pass(ServerState* state,
                                             VkDevice device,
                                             const VkRenderPassCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_render_pass(device, real_device, *info);
}

VkRenderPass server_state_create_render_pass2(ServerState* state,
                                              VkDevice device,
                                              const VkRenderPassCreateInfo2* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_render_pass2(device, real_device, info);
}

bool server_state_destroy_render_pass(ServerState* state, VkRenderPass render_pass) {
    return state->resource_tracker.destroy_render_pass(render_pass);
}

VkRenderPass server_state_get_real_render_pass(const ServerState* state,
                                               VkRenderPass render_pass) {
    return state->resource_tracker.get_real_render_pass(render_pass);
}

VkFramebuffer server_state_create_framebuffer(ServerState* state,
                                              VkDevice device,
                                              const VkFramebufferCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    if (real_device == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkFramebufferCreateInfo real_info = *info;
    VkRenderPass real_render_pass = server_state_get_real_render_pass(state, info->renderPass);
    if (real_render_pass == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    real_info.renderPass = real_render_pass;

    std::vector<VkImageView> real_attachments(info->attachmentCount);
    if (info->attachmentCount > 0 && info->pAttachments) {
        for (uint32_t i = 0; i < info->attachmentCount; ++i) {
            VkImageView real_view = server_state_get_real_image_view(state, info->pAttachments[i]);
            if (real_view == VK_NULL_HANDLE) {
                return VK_NULL_HANDLE;
            }
            real_attachments[i] = real_view;
        }
        real_info.pAttachments = real_attachments.data();
    }

    return state->resource_tracker.create_framebuffer(device, real_device, real_info);
}

bool server_state_destroy_framebuffer(ServerState* state, VkFramebuffer framebuffer) {
    return state->resource_tracker.destroy_framebuffer(framebuffer);
}

VkFramebuffer server_state_get_real_framebuffer(const ServerState* state,
                                                VkFramebuffer framebuffer) {
    return state->resource_tracker.get_real_framebuffer(framebuffer);
}

VkResult server_state_create_compute_pipelines(ServerState* state,
                                               VkDevice device,
                                               VkPipelineCache cache,
                                               uint32_t count,
                                               const VkComputePipelineCreateInfo* infos,
                                               std::vector<VkPipeline>* out_pipelines) {
    if (!infos || !out_pipelines) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_compute_pipelines(device, real_device, cache, count, infos, out_pipelines);
}

bool server_state_destroy_pipeline(ServerState* state, VkPipeline pipeline) {
    return state->resource_tracker.destroy_pipeline(pipeline);
}

VkPipeline server_state_get_real_pipeline(const ServerState* state, VkPipeline pipeline) {
    return state->resource_tracker.get_real_pipeline(pipeline);
}

VkResult server_state_create_graphics_pipelines(ServerState* state,
                                                VkDevice device,
                                                VkPipelineCache cache,
                                                uint32_t count,
                                                const VkGraphicsPipelineCreateInfo* infos,
                                                std::vector<VkPipeline>* out_pipelines) {
    if (!infos || !out_pipelines) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->resource_tracker.create_graphics_pipelines(device,
                                                             real_device,
                                                             cache,
                                                             count,
                                                             infos,
                                                             out_pipelines);
}

VkCommandPool server_state_create_command_pool(ServerState* state,
                                               VkDevice device,
                                               const VkCommandPoolCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->command_buffer_state.create_pool(device, real_device, *info);
}

bool server_state_destroy_command_pool(ServerState* state, VkCommandPool pool) {
    return state->command_buffer_state.destroy_pool(pool);
}

VkResult server_state_reset_command_pool(ServerState* state,
                                         VkCommandPool pool,
                                         VkCommandPoolResetFlags flags) {
    return state->command_buffer_state.reset_pool(pool, flags);
}

VkResult server_state_allocate_command_buffers(ServerState* state,
                                               VkDevice device,
                                               const VkCommandBufferAllocateInfo* info,
                                               VkCommandBuffer* buffers) {
    if (!info || !buffers) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::vector<VkCommandBuffer> temp;
    VkDevice real_device = server_state_get_real_device(state, device);
    VkResult result = state->command_buffer_state.allocate_command_buffers(device, real_device, *info, &temp);
    if (result != VK_SUCCESS) {
        return result;
    }
    for (size_t i = 0; i < temp.size(); ++i) {
        buffers[i] = temp[i];
    }
    return VK_SUCCESS;
}

void server_state_free_command_buffers(ServerState* state,
                                       VkCommandPool pool,
                                       uint32_t commandBufferCount,
                                       const VkCommandBuffer* buffers) {
    if (!buffers || commandBufferCount == 0) {
        return;
    }
    std::vector<VkCommandBuffer> temp(buffers, buffers + commandBufferCount);
    state->command_buffer_state.free_command_buffers(pool, temp);
}

VkResult server_state_begin_command_buffer(ServerState* state,
                                           VkCommandBuffer commandBuffer,
                                           const VkCommandBufferBeginInfo* info) {
    return state->command_buffer_state.begin(commandBuffer, info);
}

VkResult server_state_end_command_buffer(ServerState* state, VkCommandBuffer commandBuffer) {
    return state->command_buffer_state.end(commandBuffer);
}

VkResult server_state_reset_command_buffer(ServerState* state,
                                           VkCommandBuffer commandBuffer,
                                           VkCommandBufferResetFlags flags) {
    return state->command_buffer_state.reset_buffer(commandBuffer, flags);
}

bool server_state_command_buffer_is_recording(const ServerState* state, VkCommandBuffer commandBuffer) {
    return state->command_buffer_state.is_recording(commandBuffer);
}

void server_state_mark_command_buffer_invalid(ServerState* state, VkCommandBuffer commandBuffer) {
    state->command_buffer_state.invalidate(commandBuffer);
}

VkCommandBuffer server_state_get_real_command_buffer(const ServerState* state, VkCommandBuffer commandBuffer) {
    return state->command_buffer_state.get_real_buffer(commandBuffer);
}

static bool log_validation_result(bool result, const std::string& error_message) {
    if (!result && !error_message.empty()) {
        SERVER_LOG_ERROR() << "Validation error: " << error_message;
    }
    return result;
}

bool server_state_validate_cmd_copy_buffer(ServerState* state,
                                           VkBuffer srcBuffer,
                                           VkBuffer dstBuffer,
                                           uint32_t regionCount,
                                           const VkBufferCopy* regions) {
    std::string error;
    bool ok = state->command_validator.validate_copy_buffer(srcBuffer, dstBuffer, regionCount, regions, &error);
    return log_validation_result(ok, error);
}

bool server_state_validate_cmd_copy_image(ServerState* state,
                                          VkImage srcImage,
                                          VkImage dstImage,
                                          uint32_t regionCount,
                                          const VkImageCopy* regions) {
    std::string error;
    bool ok = state->command_validator.validate_copy_image(srcImage, dstImage, regionCount, regions, &error);
    return log_validation_result(ok, error);
}

bool server_state_validate_cmd_blit_image(ServerState* state,
                                          VkImage srcImage,
                                          VkImage dstImage,
                                          uint32_t regionCount,
                                          const VkImageBlit* regions) {
    std::string error;
    bool ok = state->command_validator.validate_blit_image(srcImage, dstImage, regionCount, regions, &error);
    return log_validation_result(ok, error);
}

bool server_state_validate_cmd_copy_buffer_to_image(ServerState* state,
                                                    VkBuffer srcBuffer,
                                                    VkImage dstImage,
                                                    uint32_t regionCount,
                                                    const VkBufferImageCopy* regions) {
    std::string error;
    bool ok = state->command_validator.validate_copy_buffer_to_image(srcBuffer, dstImage, regionCount, regions, &error);
    return log_validation_result(ok, error);
}

bool server_state_validate_cmd_copy_image_to_buffer(ServerState* state,
                                                    VkImage srcImage,
                                                    VkBuffer dstBuffer,
                                                    uint32_t regionCount,
                                                    const VkBufferImageCopy* regions) {
    std::string error;
    bool ok = state->command_validator.validate_copy_image_to_buffer(srcImage, dstBuffer, regionCount, regions, &error);
    return log_validation_result(ok, error);
}

bool server_state_validate_cmd_fill_buffer(ServerState* state,
                                           VkBuffer buffer,
                                           VkDeviceSize offset,
                                           VkDeviceSize size) {
    std::string error;
    bool ok = state->command_validator.validate_fill_buffer(buffer, offset, size, &error);
    return log_validation_result(ok, error);
}

bool server_state_validate_cmd_update_buffer(ServerState* state,
                                             VkBuffer buffer,
                                             VkDeviceSize offset,
                                             VkDeviceSize dataSize,
                                             const void* data) {
    std::string error;
    bool ok = state->command_validator.validate_update_buffer(buffer, offset, dataSize, data, &error);
    return log_validation_result(ok, error);
}

bool server_state_validate_cmd_clear_color_image(ServerState* state,
                                                 VkImage image,
                                                 uint32_t rangeCount,
                                                 const VkImageSubresourceRange* ranges) {
    std::string error;
    bool ok = state->command_validator.validate_clear_color_image(image, rangeCount, ranges, &error);
    return log_validation_result(ok, error);
}

VkFence server_state_create_fence(ServerState* state, VkDevice device, const VkFenceCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->sync_manager.create_fence(device, real_device, *info);
}

bool server_state_destroy_fence(ServerState* state, VkFence fence) {
    return state->sync_manager.destroy_fence(fence);
}

VkResult server_state_get_fence_status(ServerState* state, VkFence fence) {
    return state->sync_manager.get_fence_status(fence);
}

VkResult server_state_reset_fences(ServerState* state, uint32_t fenceCount, const VkFence* pFences) {
    if (!fenceCount || !pFences) {
        return VK_SUCCESS;
    }
    VkDevice real_device = state->sync_manager.get_fence_real_device(pFences[0]);
    if (real_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return state->sync_manager.reset_fences(real_device, pFences, fenceCount);
}

VkResult server_state_wait_for_fences(ServerState* state,
                                      uint32_t fenceCount,
                                      const VkFence* pFences,
                                      VkBool32 waitAll,
                                      uint64_t timeout) {
    if (!fenceCount || !pFences) {
        return VK_SUCCESS;
    }
    VkDevice real_device = state->sync_manager.get_fence_real_device(pFences[0]);
    if (real_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return state->sync_manager.wait_for_fences(real_device, pFences, fenceCount, waitAll, timeout);
}

VkSemaphore server_state_create_semaphore(ServerState* state,
                                          VkDevice device,
                                          const VkSemaphoreCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    VkSemaphoreType type = VK_SEMAPHORE_TYPE_BINARY;
    uint64_t initial_value = 0;
    const VkSemaphoreTypeCreateInfo* type_info = find_semaphore_type_info(info->pNext);
    if (type_info) {
        type = type_info->semaphoreType;
        initial_value = type_info->initialValue;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    return state->sync_manager.create_semaphore(device, real_device, type, initial_value);
}

bool server_state_destroy_semaphore(ServerState* state, VkSemaphore semaphore) {
    return state->sync_manager.destroy_semaphore(semaphore);
}

VkResult server_state_get_semaphore_counter_value(ServerState* state,
                                                  VkSemaphore semaphore,
                                                  uint64_t* pValue) {
    return state->sync_manager.get_timeline_value(semaphore, pValue);
}

VkResult server_state_signal_semaphore(ServerState* state, const VkSemaphoreSignalInfo* info) {
    if (!info) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkSemaphoreType type = state->sync_manager.get_semaphore_type(info->semaphore);
    if (type != VK_SEMAPHORE_TYPE_TIMELINE) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    return state->sync_manager.signal_timeline_value(info->semaphore, info->value);
}

VkResult server_state_wait_semaphores(ServerState* state,
                                      const VkSemaphoreWaitInfo* info,
                                      uint64_t timeout) {
    (void)timeout;
    if (!info || info->semaphoreCount == 0 || !info->pSemaphores || !info->pValues) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    for (uint32_t i = 0; i < info->semaphoreCount; ++i) {
        VkResult result = state->sync_manager.wait_timeline_value(info->pSemaphores[i], info->pValues[i]);
        if (result != VK_SUCCESS) {
            return result;
        }
    }
    return VK_SUCCESS;
}

VkEvent server_state_create_event(ServerState* state,
                                  VkDevice device,
                                  const VkEventCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    return state->sync_manager.create_event(device,
                                            server_state_get_real_device(state, device),
                                            *info);
}

bool server_state_destroy_event(ServerState* state, VkEvent event) {
    return state->sync_manager.destroy_event(event);
}

VkEvent server_state_get_real_event(const ServerState* state, VkEvent event) {
    return state->sync_manager.get_real_event(event);
}

VkResult server_state_get_event_status(ServerState* state, VkEvent event) {
    return state->sync_manager.get_event_status(event);
}

VkResult server_state_set_event(ServerState* state, VkEvent event) {
    return state->sync_manager.set_event(event);
}

VkResult server_state_reset_event(ServerState* state, VkEvent event) {
    return state->sync_manager.reset_event(event);
}

VkResult server_state_queue_submit(ServerState* state,
                                   VkQueue queue,
                                   uint32_t submitCount,
                                   const VkSubmitInfo* pSubmits,
                                   VkFence fence) {
    if (submitCount > 0 && !pSubmits) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (queue != VK_NULL_HANDLE && !state->queue_map.exists(queue)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    for (uint32_t i = 0; i < submitCount; ++i) {
        const VkSubmitInfo& submit = pSubmits[i];
        if (submit.commandBufferCount > 0 && !submit.pCommandBuffers) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        for (uint32_t j = 0; j < submit.commandBufferCount; ++j) {
            VkCommandBuffer buffer = submit.pCommandBuffers[j];
            if (!state->command_buffer_state.buffer_exists(buffer)) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            if (state->command_buffer_state.get_state(buffer) != ServerCommandBufferState::EXECUTABLE) {
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
        }
        if (submit.waitSemaphoreCount > 0 &&
            (!submit.pWaitSemaphores || !submit.pWaitDstStageMask)) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        if (submit.signalSemaphoreCount > 0 && !submit.pSignalSemaphores) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        for (uint32_t j = 0; j < submit.waitSemaphoreCount; ++j) {
            if (!state->sync_manager.semaphore_exists(submit.pWaitSemaphores[j])) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
        for (uint32_t j = 0; j < submit.signalSemaphoreCount; ++j) {
            if (!state->sync_manager.semaphore_exists(submit.pSignalSemaphores[j])) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
    }

    VkQueue real_queue = server_state_get_real_queue(state, queue);
    if (queue != VK_NULL_HANDLE && real_queue == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkFence real_fence = state->sync_manager.get_real_fence(fence);

    std::vector<VkSubmitInfo> real_submits(submitCount);
    std::vector<std::vector<VkSemaphore>> wait_semaphores(submitCount);
    std::vector<std::vector<VkSemaphore>> signal_semaphores(submitCount);
    std::vector<std::vector<VkCommandBuffer>> command_buffers(submitCount);
    std::vector<std::vector<VkPipelineStageFlags>> wait_stages(submitCount);

    for (uint32_t i = 0; i < submitCount; ++i) {
        const VkSubmitInfo& submit = pSubmits[i];
        VkSubmitInfo& real_submit = real_submits[i];
        real_submit = submit;

        if (submit.waitSemaphoreCount > 0) {
            wait_semaphores[i].resize(submit.waitSemaphoreCount);
            wait_stages[i].assign(submit.pWaitDstStageMask,
                                  submit.pWaitDstStageMask + submit.waitSemaphoreCount);
            for (uint32_t j = 0; j < submit.waitSemaphoreCount; ++j) {
                wait_semaphores[i][j] =
                    state->sync_manager.get_real_semaphore(submit.pWaitSemaphores[j]);
            }
            real_submit.pWaitSemaphores = wait_semaphores[i].data();
            real_submit.pWaitDstStageMask = wait_stages[i].data();
        }

        if (submit.commandBufferCount > 0) {
            command_buffers[i].resize(submit.commandBufferCount);
            for (uint32_t j = 0; j < submit.commandBufferCount; ++j) {
                command_buffers[i][j] =
                    server_state_get_real_command_buffer(state, submit.pCommandBuffers[j]);
            }
            real_submit.pCommandBuffers = command_buffers[i].data();
        }

        if (submit.signalSemaphoreCount > 0) {
            signal_semaphores[i].resize(submit.signalSemaphoreCount);
            for (uint32_t j = 0; j < submit.signalSemaphoreCount; ++j) {
                signal_semaphores[i][j] =
                    state->sync_manager.get_real_semaphore(submit.pSignalSemaphores[j]);
            }
            real_submit.pSignalSemaphores = signal_semaphores[i].data();
        }
    }

    return vkQueueSubmit(real_queue, submitCount, real_submits.data(), real_fence);
}

VkResult server_state_queue_submit2(ServerState* state,
                                    VkQueue queue,
                                    uint32_t submitCount,
                                    const VkSubmitInfo2* pSubmits,
                                    VkFence fence) {
    if (submitCount > 0 && !pSubmits) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (queue != VK_NULL_HANDLE && !state->queue_map.exists(queue)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    for (uint32_t i = 0; i < submitCount; ++i) {
        const VkSubmitInfo2& submit = pSubmits[i];
        if ((submit.waitSemaphoreInfoCount > 0 && !submit.pWaitSemaphoreInfos) ||
            (submit.commandBufferInfoCount > 0 && !submit.pCommandBufferInfos) ||
            (submit.signalSemaphoreInfoCount > 0 && !submit.pSignalSemaphoreInfos)) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        for (uint32_t j = 0; j < submit.commandBufferInfoCount; ++j) {
            VkCommandBuffer buffer = submit.pCommandBufferInfos[j].commandBuffer;
            if (!state->command_buffer_state.buffer_exists(buffer)) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            if (state->command_buffer_state.get_state(buffer) != ServerCommandBufferState::EXECUTABLE) {
                return VK_ERROR_VALIDATION_FAILED_EXT;
            }
        }
        for (uint32_t j = 0; j < submit.waitSemaphoreInfoCount; ++j) {
            VkSemaphore semaphore = submit.pWaitSemaphoreInfos[j].semaphore;
            if (!state->sync_manager.semaphore_exists(semaphore)) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
        for (uint32_t j = 0; j < submit.signalSemaphoreInfoCount; ++j) {
            VkSemaphore semaphore = submit.pSignalSemaphoreInfos[j].semaphore;
            if (!state->sync_manager.semaphore_exists(semaphore)) {
                return VK_ERROR_INITIALIZATION_FAILED;
            }
        }
    }

    VkQueue real_queue = server_state_get_real_queue(state, queue);
    if (queue != VK_NULL_HANDLE && real_queue == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkFence real_fence = state->sync_manager.get_real_fence(fence);

    std::vector<VkSubmitInfo2> real_submits(submitCount);
    std::vector<std::vector<VkSemaphoreSubmitInfo>> wait_infos(submitCount);
    std::vector<std::vector<VkCommandBufferSubmitInfo>> cmd_infos(submitCount);
    std::vector<std::vector<VkSemaphoreSubmitInfo>> signal_infos(submitCount);

    for (uint32_t i = 0; i < submitCount; ++i) {
        const VkSubmitInfo2& submit = pSubmits[i];
        VkSubmitInfo2& real_submit = real_submits[i];
        real_submit = submit;

        if (submit.waitSemaphoreInfoCount > 0) {
            wait_infos[i].resize(submit.waitSemaphoreInfoCount);
            for (uint32_t j = 0; j < submit.waitSemaphoreInfoCount; ++j) {
                wait_infos[i][j] = submit.pWaitSemaphoreInfos[j];
                wait_infos[i][j].semaphore =
                    state->sync_manager.get_real_semaphore(wait_infos[i][j].semaphore);
            }
            real_submit.pWaitSemaphoreInfos = wait_infos[i].data();
        } else {
            real_submit.pWaitSemaphoreInfos = NULL;
        }

        if (submit.commandBufferInfoCount > 0) {
            cmd_infos[i].resize(submit.commandBufferInfoCount);
            for (uint32_t j = 0; j < submit.commandBufferInfoCount; ++j) {
                cmd_infos[i][j] = submit.pCommandBufferInfos[j];
                cmd_infos[i][j].commandBuffer =
                    server_state_get_real_command_buffer(state, cmd_infos[i][j].commandBuffer);
            }
            real_submit.pCommandBufferInfos = cmd_infos[i].data();
        } else {
            real_submit.pCommandBufferInfos = NULL;
        }

        if (submit.signalSemaphoreInfoCount > 0) {
            signal_infos[i].resize(submit.signalSemaphoreInfoCount);
            for (uint32_t j = 0; j < submit.signalSemaphoreInfoCount; ++j) {
                signal_infos[i][j] = submit.pSignalSemaphoreInfos[j];
                signal_infos[i][j].semaphore =
                    state->sync_manager.get_real_semaphore(signal_infos[i][j].semaphore);
            }
            real_submit.pSignalSemaphoreInfos = signal_infos[i].data();
        } else {
            real_submit.pSignalSemaphoreInfos = NULL;
        }
    }

    return vkQueueSubmit2(real_queue, submitCount, real_submits.data(), real_fence);
}

VkResult server_state_queue_wait_idle(ServerState* state, VkQueue queue) {
    if (queue == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (!state->queue_map.exists(queue)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkQueue real_queue = server_state_get_real_queue(state, queue);
    if (real_queue == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return vkQueueWaitIdle(real_queue);
}

VkResult server_state_device_wait_idle(ServerState* state, VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkDevice real_device = server_state_get_real_device(state, device);
    if (real_device == VK_NULL_HANDLE) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return vkDeviceWaitIdle(real_device);
}

} // namespace venus_plus

extern "C" {

VkInstance server_state_bridge_alloc_instance(struct ServerState* state) {
    return venus_plus::server_state_alloc_instance(state);
}

void server_state_bridge_remove_instance(struct ServerState* state, VkInstance instance) {
    venus_plus::server_state_remove_instance(state, instance);
}

bool server_state_bridge_instance_exists(const struct ServerState* state, VkInstance instance) {
    return venus_plus::server_state_instance_exists(state, instance);
}

VkPhysicalDevice server_state_bridge_get_fake_device(struct ServerState* state) {
    return venus_plus::server_state_get_fake_device(state);
}

VkInstance server_state_bridge_get_real_instance(const struct ServerState* state, VkInstance instance) {
    return venus_plus::server_state_get_real_instance(state, instance);
}

VkPhysicalDevice server_state_bridge_get_real_physical_device(const struct ServerState* state,
                                                              VkPhysicalDevice physical_device) {
    return venus_plus::server_state_get_real_physical_device(state, physical_device);
}

VkDevice server_state_bridge_get_real_device(const struct ServerState* state, VkDevice device) {
    return venus_plus::server_state_get_real_device(state, device);
}

VkQueue server_state_bridge_get_real_queue(const struct ServerState* state, VkQueue queue) {
    return venus_plus::server_state_get_real_queue(state, queue);
}

VkBuffer server_state_bridge_get_real_buffer(const struct ServerState* state, VkBuffer buffer) {
    return venus_plus::server_state_get_real_buffer(state, buffer);
}

VkImage server_state_bridge_get_real_image(const struct ServerState* state, VkImage image) {
    return venus_plus::server_state_get_real_image(state, image);
}

VkDeviceMemory server_state_bridge_get_real_memory(const struct ServerState* state, VkDeviceMemory memory) {
    return venus_plus::server_state_get_real_memory(state, memory);
}

VkCommandBuffer server_state_bridge_get_real_command_buffer(const struct ServerState* state,
                                                            VkCommandBuffer commandBuffer) {
    return venus_plus::server_state_get_real_command_buffer(state, commandBuffer);
}

VkShaderModule server_state_bridge_create_shader_module(struct ServerState* state,
                                                        VkDevice device,
                                                        const VkShaderModuleCreateInfo* info) {
    return venus_plus::server_state_create_shader_module(state, device, info);
}

void server_state_bridge_destroy_shader_module(struct ServerState* state, VkShaderModule module) {
    venus_plus::server_state_destroy_shader_module(state, module);
}

VkShaderModule server_state_bridge_get_real_shader_module(const struct ServerState* state,
                                                          VkShaderModule module) {
    return venus_plus::server_state_get_real_shader_module(state, module);
}

VkDescriptorSetLayout server_state_bridge_create_descriptor_set_layout(struct ServerState* state,
                                                                       VkDevice device,
                                                                       const VkDescriptorSetLayoutCreateInfo* info) {
    return venus_plus::server_state_create_descriptor_set_layout(state, device, info);
}

void server_state_bridge_destroy_descriptor_set_layout(struct ServerState* state,
                                                       VkDescriptorSetLayout layout) {
    venus_plus::server_state_destroy_descriptor_set_layout(state, layout);
}

VkDescriptorSetLayout server_state_bridge_get_real_descriptor_set_layout(const struct ServerState* state,
                                                                         VkDescriptorSetLayout layout) {
    return venus_plus::server_state_get_real_descriptor_set_layout(state, layout);
}

VkDescriptorUpdateTemplate server_state_bridge_create_descriptor_update_template(
    struct ServerState* state,
    VkDevice device,
    const VkDescriptorUpdateTemplateCreateInfo* info) {
    return venus_plus::server_state_create_descriptor_update_template(state, device, info);
}

void server_state_bridge_destroy_descriptor_update_template(struct ServerState* state,
                                                            VkDescriptorUpdateTemplate tmpl) {
    venus_plus::server_state_destroy_descriptor_update_template(state, tmpl);
}

VkDescriptorUpdateTemplate server_state_bridge_get_real_descriptor_update_template(
    const struct ServerState* state,
    VkDescriptorUpdateTemplate tmpl) {
    return venus_plus::server_state_get_real_descriptor_update_template(state, tmpl);
}

bool server_state_bridge_get_descriptor_update_template_info(
    const struct ServerState* state,
    VkDescriptorUpdateTemplate tmpl,
    struct DescriptorUpdateTemplateInfoBridge* out_info) {
    return venus_plus::server_state_get_descriptor_update_template_info(state, tmpl, out_info);
}

VkDescriptorPool server_state_bridge_create_descriptor_pool(struct ServerState* state,
                                                            VkDevice device,
                                                            const VkDescriptorPoolCreateInfo* info) {
    return venus_plus::server_state_create_descriptor_pool(state, device, info);
}

void server_state_bridge_destroy_descriptor_pool(struct ServerState* state, VkDescriptorPool pool) {
    venus_plus::server_state_destroy_descriptor_pool(state, pool);
}

VkResult server_state_bridge_reset_descriptor_pool(struct ServerState* state,
                                                   VkDescriptorPool pool,
                                                   VkDescriptorPoolResetFlags flags) {
    return venus_plus::server_state_reset_descriptor_pool(state, pool, flags);
}

VkDescriptorPool server_state_bridge_get_real_descriptor_pool(const struct ServerState* state,
                                                              VkDescriptorPool pool) {
    return venus_plus::server_state_get_real_descriptor_pool(state, pool);
}

VkResult server_state_bridge_allocate_descriptor_sets(struct ServerState* state,
                                                      VkDevice device,
                                                      const VkDescriptorSetAllocateInfo* info,
                                                      VkDescriptorSet* pDescriptorSets) {
    if (!info || !pDescriptorSets) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::vector<VkDescriptorSet> sets;
    VkResult result = venus_plus::server_state_allocate_descriptor_sets(state, device, info, &sets);
    if (result != VK_SUCCESS) {
        return result;
    }
    for (size_t i = 0; i < sets.size(); ++i) {
        pDescriptorSets[i] = sets[i];
    }
    return VK_SUCCESS;
}

VkResult server_state_bridge_free_descriptor_sets(struct ServerState* state,
                                                  VkDevice device,
                                                  VkDescriptorPool pool,
                                                  uint32_t descriptorSetCount,
                                                  const VkDescriptorSet* pDescriptorSets) {
    return venus_plus::server_state_free_descriptor_sets(state, device, pool, descriptorSetCount, pDescriptorSets);
}

VkDescriptorSet server_state_bridge_get_real_descriptor_set(const struct ServerState* state,
                                                            VkDescriptorSet set) {
    return venus_plus::server_state_get_real_descriptor_set(state, set);
}

VkPipelineLayout server_state_bridge_create_pipeline_layout(struct ServerState* state,
                                                            VkDevice device,
                                                            const VkPipelineLayoutCreateInfo* info) {
    return venus_plus::server_state_create_pipeline_layout(state, device, info);
}

void server_state_bridge_destroy_pipeline_layout(struct ServerState* state, VkPipelineLayout layout) {
    venus_plus::server_state_destroy_pipeline_layout(state, layout);
}

VkPipelineLayout server_state_bridge_get_real_pipeline_layout(const struct ServerState* state,
                                                              VkPipelineLayout layout) {
    return venus_plus::server_state_get_real_pipeline_layout(state, layout);
}

VkPipelineCache server_state_bridge_create_pipeline_cache(struct ServerState* state,
                                                          VkDevice device,
                                                          const VkPipelineCacheCreateInfo* info) {
    return venus_plus::server_state_create_pipeline_cache(state, device, info);
}

bool server_state_bridge_destroy_pipeline_cache(struct ServerState* state, VkPipelineCache cache) {
    return venus_plus::server_state_destroy_pipeline_cache(state, cache);
}

VkPipelineCache server_state_bridge_get_real_pipeline_cache(const struct ServerState* state,
                                                            VkPipelineCache cache) {
    return venus_plus::server_state_get_real_pipeline_cache(state, cache);
}

VkResult server_state_bridge_get_pipeline_cache_data(struct ServerState* state,
                                                     VkDevice device,
                                                     VkPipelineCache cache,
                                                     size_t* pDataSize,
                                                     void* pData) {
    return venus_plus::server_state_get_pipeline_cache_data(state, device, cache, pDataSize, pData);
}

VkResult server_state_bridge_merge_pipeline_caches(struct ServerState* state,
                                                   VkDevice device,
                                                   VkPipelineCache dstCache,
                                                   uint32_t srcCacheCount,
                                                   const VkPipelineCache* pSrcCaches) {
    return venus_plus::server_state_merge_pipeline_caches(state, device, dstCache, srcCacheCount, pSrcCaches);
}

VkRenderPass server_state_bridge_create_render_pass(struct ServerState* state,
                                                    VkDevice device,
                                                    const VkRenderPassCreateInfo* info) {
    return venus_plus::server_state_create_render_pass(state, device, info);
}

VkRenderPass server_state_bridge_create_render_pass2(struct ServerState* state,
                                                     VkDevice device,
                                                     const VkRenderPassCreateInfo2* info) {
    return venus_plus::server_state_create_render_pass2(state, device, info);
}

void server_state_bridge_destroy_render_pass(struct ServerState* state, VkRenderPass render_pass) {
    venus_plus::server_state_destroy_render_pass(state, render_pass);
}

VkRenderPass server_state_bridge_get_real_render_pass(const struct ServerState* state,
                                                      VkRenderPass render_pass) {
    return venus_plus::server_state_get_real_render_pass(state, render_pass);
}

VkFramebuffer server_state_bridge_create_framebuffer(struct ServerState* state,
                                                     VkDevice device,
                                                     const VkFramebufferCreateInfo* info) {
    return venus_plus::server_state_create_framebuffer(state, device, info);
}

void server_state_bridge_destroy_framebuffer(struct ServerState* state, VkFramebuffer framebuffer) {
    venus_plus::server_state_destroy_framebuffer(state, framebuffer);
}

VkFramebuffer server_state_bridge_get_real_framebuffer(const struct ServerState* state,
                                                       VkFramebuffer framebuffer) {
    return venus_plus::server_state_get_real_framebuffer(state, framebuffer);
}

VkResult server_state_bridge_create_compute_pipelines(struct ServerState* state,
                                                      VkDevice device,
                                                      VkPipelineCache cache,
                                                      uint32_t createInfoCount,
                                                      const VkComputePipelineCreateInfo* pCreateInfos,
                                                      VkPipeline* pPipelines) {
    if (!pCreateInfos || !pPipelines) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::vector<VkPipeline> pipelines;
    VkResult result = venus_plus::server_state_create_compute_pipelines(state, device, cache, createInfoCount, pCreateInfos, &pipelines);
    if (result != VK_SUCCESS) {
        return result;
    }
    for (size_t i = 0; i < pipelines.size(); ++i) {
        pPipelines[i] = pipelines[i];
    }
    return VK_SUCCESS;
}

void server_state_bridge_destroy_pipeline(struct ServerState* state, VkPipeline pipeline) {
    venus_plus::server_state_destroy_pipeline(state, pipeline);
}

VkPipeline server_state_bridge_get_real_pipeline(const struct ServerState* state, VkPipeline pipeline) {
    return venus_plus::server_state_get_real_pipeline(state, pipeline);
}

VkResult server_state_bridge_create_graphics_pipelines(struct ServerState* state,
                                                       VkDevice device,
                                                       VkPipelineCache cache,
                                                       uint32_t createInfoCount,
                                                       const VkGraphicsPipelineCreateInfo* pCreateInfos,
                                                       VkPipeline* pPipelines) {
    if (!pCreateInfos || !pPipelines) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::vector<VkPipeline> pipelines;
    VkResult result = venus_plus::server_state_create_graphics_pipelines(state,
                                                                         device,
                                                                         cache,
                                                                         createInfoCount,
                                                                         pCreateInfos,
                                                                         &pipelines);
    if (result != VK_SUCCESS) {
        return result;
    }
    for (size_t i = 0; i < pipelines.size(); ++i) {
        pPipelines[i] = pipelines[i];
    }
    return VK_SUCCESS;
}

// Phase 3: C bridge functions for device management
VkDevice server_state_bridge_alloc_device(struct ServerState* state,
                                          VkPhysicalDevice physical_device,
                                          VkDevice real_device) {
    return venus_plus::server_state_alloc_device(state, physical_device, real_device);
}

void server_state_bridge_remove_device(struct ServerState* state, VkDevice device) {
    venus_plus::server_state_remove_device(state, device);
}

bool server_state_bridge_device_exists(const struct ServerState* state, VkDevice device) {
    return venus_plus::server_state_device_exists(state, device);
}

VkQueue server_state_bridge_alloc_queue(struct ServerState* state,
                                        VkDevice device,
                                        uint32_t family_index,
                                        uint32_t queue_index,
                                        VkQueue real_queue) {
    return venus_plus::server_state_alloc_queue(state, device, family_index, queue_index, real_queue);
}

VkQueue server_state_bridge_find_queue(const struct ServerState* state, VkDevice device, uint32_t family_index, uint32_t queue_index) {
    return venus_plus::server_state_find_queue(state, device, family_index, queue_index);
}

VkDeviceMemory server_state_bridge_alloc_memory(struct ServerState* state, VkDevice device, const VkMemoryAllocateInfo* info) {
    return venus_plus::server_state_alloc_memory(state, device, info);
}

bool server_state_bridge_free_memory(struct ServerState* state, VkDeviceMemory memory) {
    return venus_plus::server_state_free_memory(state, memory);
}

VkBuffer server_state_bridge_create_buffer(struct ServerState* state, VkDevice device, const VkBufferCreateInfo* info) {
    return venus_plus::server_state_create_buffer(state, device, info);
}

bool server_state_bridge_destroy_buffer(struct ServerState* state, VkBuffer buffer) {
    return venus_plus::server_state_destroy_buffer(state, buffer);
}

bool server_state_bridge_get_buffer_memory_requirements(struct ServerState* state, VkBuffer buffer, VkMemoryRequirements* requirements) {
    return venus_plus::server_state_get_buffer_memory_requirements(state, buffer, requirements);
}

VkResult server_state_bridge_bind_buffer_memory(struct ServerState* state, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    return venus_plus::server_state_bind_buffer_memory(state, buffer, memory, offset);
}

VkImage server_state_bridge_create_image(struct ServerState* state, VkDevice device, const VkImageCreateInfo* info) {
    return venus_plus::server_state_create_image(state, device, info);
}

bool server_state_bridge_destroy_image(struct ServerState* state, VkImage image) {
    return venus_plus::server_state_destroy_image(state, image);
}

bool server_state_bridge_get_image_memory_requirements(struct ServerState* state, VkImage image, VkMemoryRequirements* requirements) {
    return venus_plus::server_state_get_image_memory_requirements(state, image, requirements);
}

VkResult server_state_bridge_bind_image_memory(struct ServerState* state, VkImage image, VkDeviceMemory memory, VkDeviceSize offset) {
    return venus_plus::server_state_bind_image_memory(state, image, memory, offset);
}

bool server_state_bridge_get_image_subresource_layout(struct ServerState* state,
                                                      VkImage image,
                                                      const VkImageSubresource* subresource,
                                                      VkSubresourceLayout* layout) {
    return venus_plus::server_state_get_image_subresource_layout(state, image, subresource, layout);
}

VkImageView server_state_bridge_create_image_view(struct ServerState* state,
                                                  VkDevice device,
                                                  const VkImageViewCreateInfo* info) {
    return venus_plus::server_state_create_image_view(state, device, info);
}

bool server_state_bridge_destroy_image_view(struct ServerState* state, VkImageView view) {
    return venus_plus::server_state_destroy_image_view(state, view);
}

VkImageView server_state_bridge_get_real_image_view(const struct ServerState* state, VkImageView view) {
    return venus_plus::server_state_get_real_image_view(state, view);
}

VkBufferView server_state_bridge_create_buffer_view(struct ServerState* state,
                                                    VkDevice device,
                                                    const VkBufferViewCreateInfo* info) {
    return venus_plus::server_state_create_buffer_view(state, device, info);
}

bool server_state_bridge_destroy_buffer_view(struct ServerState* state, VkBufferView view) {
    return venus_plus::server_state_destroy_buffer_view(state, view);
}

VkBufferView server_state_bridge_get_real_buffer_view(const struct ServerState* state, VkBufferView view) {
    return venus_plus::server_state_get_real_buffer_view(state, view);
}

VkCommandPool server_state_bridge_create_command_pool(struct ServerState* state,
                                                      VkDevice device,
                                                      const VkCommandPoolCreateInfo* info) {
    return venus_plus::server_state_create_command_pool(state, device, info);
}

bool server_state_bridge_destroy_command_pool(struct ServerState* state, VkCommandPool commandPool) {
    return venus_plus::server_state_destroy_command_pool(state, commandPool);
}

VkResult server_state_bridge_reset_command_pool(struct ServerState* state,
                                                VkCommandPool commandPool,
                                                VkCommandPoolResetFlags flags) {
    return venus_plus::server_state_reset_command_pool(state, commandPool, flags);
}

VkResult server_state_bridge_allocate_command_buffers(struct ServerState* state,
                                                      VkDevice device,
                                                      const VkCommandBufferAllocateInfo* info,
                                                      VkCommandBuffer* pCommandBuffers) {
    return venus_plus::server_state_allocate_command_buffers(state, device, info, pCommandBuffers);
}

void server_state_bridge_free_command_buffers(struct ServerState* state,
                                              VkCommandPool commandPool,
                                              uint32_t commandBufferCount,
                                              const VkCommandBuffer* pCommandBuffers) {
    venus_plus::server_state_free_command_buffers(state, commandPool, commandBufferCount, pCommandBuffers);
}

VkResult server_state_bridge_begin_command_buffer(struct ServerState* state,
                                                  VkCommandBuffer commandBuffer,
                                                  const VkCommandBufferBeginInfo* info) {
    return venus_plus::server_state_begin_command_buffer(state, commandBuffer, info);
}

VkResult server_state_bridge_end_command_buffer(struct ServerState* state, VkCommandBuffer commandBuffer) {
    return venus_plus::server_state_end_command_buffer(state, commandBuffer);
}

VkResult server_state_bridge_reset_command_buffer(struct ServerState* state,
                                                  VkCommandBuffer commandBuffer,
                                                  VkCommandBufferResetFlags flags) {
    return venus_plus::server_state_reset_command_buffer(state, commandBuffer, flags);
}

bool server_state_bridge_command_buffer_is_recording(const struct ServerState* state, VkCommandBuffer commandBuffer) {
    return venus_plus::server_state_command_buffer_is_recording(state, commandBuffer);
}

void server_state_bridge_mark_command_buffer_invalid(struct ServerState* state, VkCommandBuffer commandBuffer) {
    venus_plus::server_state_mark_command_buffer_invalid(state, commandBuffer);
}

bool server_state_bridge_validate_cmd_copy_buffer(struct ServerState* state,
                                                  VkBuffer srcBuffer,
                                                  VkBuffer dstBuffer,
                                                  uint32_t regionCount,
                                                  const VkBufferCopy* pRegions) {
    return venus_plus::server_state_validate_cmd_copy_buffer(state, srcBuffer, dstBuffer, regionCount, pRegions);
}

bool server_state_bridge_validate_cmd_copy_image(struct ServerState* state,
                                                 VkImage srcImage,
                                                 VkImage dstImage,
                                                 uint32_t regionCount,
                                                 const VkImageCopy* pRegions) {
    return venus_plus::server_state_validate_cmd_copy_image(state, srcImage, dstImage, regionCount, pRegions);
}

bool server_state_bridge_validate_cmd_blit_image(struct ServerState* state,
                                                 VkImage srcImage,
                                                 VkImage dstImage,
                                                 uint32_t regionCount,
                                                 const VkImageBlit* pRegions) {
    return venus_plus::server_state_validate_cmd_blit_image(state, srcImage, dstImage, regionCount, pRegions);
}

bool server_state_bridge_validate_cmd_copy_buffer_to_image(struct ServerState* state,
                                                           VkBuffer srcBuffer,
                                                           VkImage dstImage,
                                                           uint32_t regionCount,
                                                           const VkBufferImageCopy* pRegions) {
    return venus_plus::server_state_validate_cmd_copy_buffer_to_image(state, srcBuffer, dstImage, regionCount, pRegions);
}

bool server_state_bridge_validate_cmd_copy_image_to_buffer(struct ServerState* state,
                                                           VkImage srcImage,
                                                           VkBuffer dstBuffer,
                                                           uint32_t regionCount,
                                                           const VkBufferImageCopy* pRegions) {
    return venus_plus::server_state_validate_cmd_copy_image_to_buffer(state, srcImage, dstBuffer, regionCount, pRegions);
}

bool server_state_bridge_validate_cmd_fill_buffer(struct ServerState* state,
                                                  VkBuffer buffer,
                                                  VkDeviceSize offset,
                                                  VkDeviceSize size) {
    return venus_plus::server_state_validate_cmd_fill_buffer(state, buffer, offset, size);
}

bool server_state_bridge_validate_cmd_update_buffer(struct ServerState* state,
                                                    VkBuffer buffer,
                                                    VkDeviceSize offset,
                                                    VkDeviceSize dataSize,
                                                    const void* data) {
    return venus_plus::server_state_validate_cmd_update_buffer(state, buffer, offset, dataSize, data);
}

bool server_state_bridge_validate_cmd_clear_color_image(struct ServerState* state,
                                                        VkImage image,
                                                        uint32_t rangeCount,
                                                        const VkImageSubresourceRange* pRanges) {
    return venus_plus::server_state_validate_cmd_clear_color_image(state, image, rangeCount, pRanges);
}

VkFence server_state_bridge_create_fence(struct ServerState* state,
                                         VkDevice device,
                                         const VkFenceCreateInfo* info) {
    return venus_plus::server_state_create_fence(state, device, info);
}

bool server_state_bridge_destroy_fence(struct ServerState* state, VkFence fence) {
    return venus_plus::server_state_destroy_fence(state, fence);
}

VkResult server_state_bridge_get_fence_status(struct ServerState* state, VkFence fence) {
    return venus_plus::server_state_get_fence_status(state, fence);
}

VkResult server_state_bridge_reset_fences(struct ServerState* state,
                                          uint32_t fenceCount,
                                          const VkFence* pFences) {
    return venus_plus::server_state_reset_fences(state, fenceCount, pFences);
}

VkResult server_state_bridge_wait_for_fences(struct ServerState* state,
                                             uint32_t fenceCount,
                                             const VkFence* pFences,
                                             VkBool32 waitAll,
                                             uint64_t timeout) {
    return venus_plus::server_state_wait_for_fences(state, fenceCount, pFences, waitAll, timeout);
}

VkSampler server_state_bridge_create_sampler(struct ServerState* state,
                                             VkDevice device,
                                             const VkSamplerCreateInfo* info) {
    return venus_plus::server_state_create_sampler(state, device, info);
}

bool server_state_bridge_destroy_sampler(struct ServerState* state, VkSampler sampler) {
    return venus_plus::server_state_destroy_sampler(state, sampler);
}

VkSampler server_state_bridge_get_real_sampler(const struct ServerState* state, VkSampler sampler) {
    return venus_plus::server_state_get_real_sampler(state, sampler);
}

VkSemaphore server_state_bridge_create_semaphore(struct ServerState* state,
                                                 VkDevice device,
                                                 const VkSemaphoreCreateInfo* info) {
    return venus_plus::server_state_create_semaphore(state, device, info);
}

bool server_state_bridge_destroy_semaphore(struct ServerState* state, VkSemaphore semaphore) {
    return venus_plus::server_state_destroy_semaphore(state, semaphore);
}

VkResult server_state_bridge_get_semaphore_counter_value(struct ServerState* state,
                                                         VkSemaphore semaphore,
                                                         uint64_t* pValue) {
    return venus_plus::server_state_get_semaphore_counter_value(state, semaphore, pValue);
}

VkResult server_state_bridge_signal_semaphore(struct ServerState* state,
                                              const VkSemaphoreSignalInfo* info) {
    return venus_plus::server_state_signal_semaphore(state, info);
}

VkResult server_state_bridge_wait_semaphores(struct ServerState* state,
                                             const VkSemaphoreWaitInfo* info,
                                             uint64_t timeout) {
    return venus_plus::server_state_wait_semaphores(state, info, timeout);
}

VkEvent server_state_bridge_create_event(struct ServerState* state,
                                         VkDevice device,
                                         const VkEventCreateInfo* info) {
    return venus_plus::server_state_create_event(state, device, info);
}

bool server_state_bridge_destroy_event(struct ServerState* state, VkEvent event) {
    return venus_plus::server_state_destroy_event(state, event);
}

VkEvent server_state_bridge_get_real_event(const struct ServerState* state, VkEvent event) {
    return venus_plus::server_state_get_real_event(state, event);
}

VkResult server_state_bridge_get_event_status(struct ServerState* state, VkEvent event) {
    return venus_plus::server_state_get_event_status(state, event);
}

VkResult server_state_bridge_set_event(struct ServerState* state, VkEvent event) {
    return venus_plus::server_state_set_event(state, event);
}

VkResult server_state_bridge_reset_event(struct ServerState* state, VkEvent event) {
    return venus_plus::server_state_reset_event(state, event);
}

VkResult server_state_bridge_queue_submit(struct ServerState* state,
                                          VkQueue queue,
                                          uint32_t submitCount,
                                          const VkSubmitInfo* pSubmits,
                                          VkFence fence) {
    return venus_plus::server_state_queue_submit(state, queue, submitCount, pSubmits, fence);
}

VkResult server_state_bridge_queue_submit2(struct ServerState* state,
                                           VkQueue queue,
                                           uint32_t submitCount,
                                           const VkSubmitInfo2* pSubmits,
                                           VkFence fence) {
    return venus_plus::server_state_queue_submit2(state, queue, submitCount, pSubmits, fence);
}

VkResult server_state_bridge_queue_wait_idle(struct ServerState* state, VkQueue queue) {
    return venus_plus::server_state_queue_wait_idle(state, queue);
}

VkResult server_state_bridge_device_wait_idle(struct ServerState* state, VkDevice device) {
    return venus_plus::server_state_device_wait_idle(state, device);
}

VkQueryPool server_state_bridge_create_query_pool(struct ServerState* state,
                                                  VkDevice device,
                                                  const VkQueryPoolCreateInfo* info) {
    return venus_plus::server_state_create_query_pool(state, device, info);
}

bool server_state_bridge_destroy_query_pool(struct ServerState* state, VkQueryPool queryPool) {
    return venus_plus::server_state_destroy_query_pool(state, queryPool);
}

VkQueryPool server_state_bridge_get_real_query_pool(const struct ServerState* state, VkQueryPool pool) {
    return venus_plus::server_state_get_real_query_pool(state, pool);
}

VkDevice server_state_bridge_get_query_pool_real_device(const struct ServerState* state, VkQueryPool pool) {
    return venus_plus::server_state_get_query_pool_real_device(state, pool);
}

VkResult server_state_bridge_get_query_pool_results(struct ServerState* state,
                                                    VkDevice device,
                                                    VkQueryPool queryPool,
                                                    uint32_t firstQuery,
                                                    uint32_t queryCount,
                                                    size_t dataSize,
                                                    void* pData,
                                                    VkDeviceSize stride,
                                                    VkQueryResultFlags flags) {
    return venus_plus::server_state_get_query_pool_results(state,
                                                           device,
                                                           queryPool,
                                                           firstQuery,
                                                           queryCount,
                                                           dataSize,
                                                           pData,
                                                           stride,
                                                           flags);
}

} // extern "C"
