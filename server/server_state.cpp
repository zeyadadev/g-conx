#include "server_state.h"
#include "server_state_bridge.h"
#include <algorithm>
#include <string>

namespace venus_plus {

VkInstance server_state_alloc_instance(ServerState* state) {
    VkInstance handle = reinterpret_cast<VkInstance>(state->next_instance_handle++);
    state->instance_map.insert(handle, handle);
    return handle;
}

void server_state_remove_instance(ServerState* state, VkInstance instance) {
    state->instance_map.remove(instance);
}

bool server_state_instance_exists(const ServerState* state, VkInstance instance) {
    return state->instance_map.exists(instance);
}

VkPhysicalDevice server_state_get_fake_device(ServerState* state) {
    if (state->fake_device_handle == VK_NULL_HANDLE) {
        state->fake_device_handle = reinterpret_cast<VkPhysicalDevice>(state->next_physical_device_handle++);
        state->physical_device_map.insert(state->fake_device_handle, state->fake_device_handle);
    }
    return state->fake_device_handle;
}

// Phase 3: Device management
VkDevice server_state_alloc_device(ServerState* state, VkPhysicalDevice physical_device) {
    VkDevice handle = reinterpret_cast<VkDevice>(state->next_device_handle++);
    state->device_map.insert(handle, handle);

    DeviceInfo info;
    info.handle = handle;
    info.physical_device = physical_device;
    state->device_info_map[handle] = info;

    return handle;
}

void server_state_remove_device(ServerState* state, VkDevice device) {
    // Remove all queues associated with this device
    auto it = state->device_info_map.find(device);
    if (it != state->device_info_map.end()) {
        for (const auto& queue_info : it->second.queues) {
            state->queue_map.remove(queue_info.handle);
        }
        state->device_info_map.erase(it);
    }
    state->device_map.remove(device);
}

bool server_state_device_exists(const ServerState* state, VkDevice device) {
    return state->device_map.exists(device);
}

VkPhysicalDevice server_state_get_device_physical_device(const ServerState* state, VkDevice device) {
    auto it = state->device_info_map.find(device);
    if (it != state->device_info_map.end()) {
        return it->second.physical_device;
    }
    return VK_NULL_HANDLE;
}

// Phase 3: Queue management
VkQueue server_state_alloc_queue(ServerState* state, VkDevice device, uint32_t family_index, uint32_t queue_index) {
    VkQueue handle = reinterpret_cast<VkQueue>(state->next_queue_handle++);
    state->queue_map.insert(handle, handle);

    auto it = state->device_info_map.find(device);
    if (it != state->device_info_map.end()) {
        QueueInfo queue_info;
        queue_info.handle = handle;
        queue_info.family_index = family_index;
        queue_info.queue_index = queue_index;
        it->second.queues.push_back(queue_info);
    }

    return handle;
}

VkQueue server_state_find_queue(const ServerState* state, VkDevice device, uint32_t family_index, uint32_t queue_index) {
    auto it = state->device_info_map.find(device);
    if (it != state->device_info_map.end()) {
        for (const auto& queue_info : it->second.queues) {
            if (queue_info.family_index == family_index && queue_info.queue_index == queue_index) {
                return queue_info.handle;
            }
        }
    }
    return VK_NULL_HANDLE;
}

VkDeviceMemory server_state_alloc_memory(ServerState* state, VkDevice device, const VkMemoryAllocateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    return state->resource_tracker.allocate_memory(device, *info);
}

bool server_state_free_memory(ServerState* state, VkDeviceMemory memory) {
    return state->resource_tracker.free_memory(memory);
}

VkBuffer server_state_create_buffer(ServerState* state, VkDevice device, const VkBufferCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    return state->resource_tracker.create_buffer(device, *info);
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
            printf("[Venus Server]   -> %s\n", error.c_str());
        }
        return VK_ERROR_VALIDATION_FAILED_EXT;
    }
    return VK_SUCCESS;
}

VkImage server_state_create_image(ServerState* state, VkDevice device, const VkImageCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    return state->resource_tracker.create_image(device, *info);
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
            printf("[Venus Server]   -> %s\n", error.c_str());
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

// Phase 3: C bridge functions for device management
VkDevice server_state_bridge_alloc_device(struct ServerState* state, VkPhysicalDevice physical_device) {
    return venus_plus::server_state_alloc_device(state, physical_device);
}

void server_state_bridge_remove_device(struct ServerState* state, VkDevice device) {
    venus_plus::server_state_remove_device(state, device);
}

bool server_state_bridge_device_exists(const struct ServerState* state, VkDevice device) {
    return venus_plus::server_state_device_exists(state, device);
}

VkQueue server_state_bridge_alloc_queue(struct ServerState* state, VkDevice device, uint32_t family_index, uint32_t queue_index) {
    return venus_plus::server_state_alloc_queue(state, device, family_index, queue_index);
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

} // extern "C"
