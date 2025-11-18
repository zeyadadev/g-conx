#include "server_state.h"
#include "server_state_bridge.h"
#include <algorithm>
#include <string>
#include <vector>
#include <cstdio>

namespace venus_plus {

ServerState::ServerState()
    : resource_tracker(),
      command_buffer_state(),
      command_validator(&resource_tracker) {}

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

VkCommandPool server_state_create_command_pool(ServerState* state,
                                               VkDevice device,
                                               const VkCommandPoolCreateInfo* info) {
    if (!info) {
        return VK_NULL_HANDLE;
    }
    return state->command_buffer_state.create_pool(device, *info);
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
    VkResult result = state->command_buffer_state.allocate_command_buffers(device, *info, &temp);
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

static bool log_validation_result(bool result, const std::string& error_message) {
    if (!result && !error_message.empty()) {
        std::printf("[Venus Server]   -> Validation error: %s\n", error_message.c_str());
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

} // extern "C"
