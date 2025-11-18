#ifndef VENUS_PLUS_SERVER_STATE_H
#define VENUS_PLUS_SERVER_STATE_H

#include "state/handle_map.h"
#include "state/resource_tracker.h"
#include "state/command_buffer_state.h"
#include "state/command_validator.h"
#include "state/sync_manager.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

struct QueueInfo {
    VkQueue handle;
    uint32_t family_index;
    uint32_t queue_index;
};

struct DeviceInfo {
    VkDevice handle;
    VkPhysicalDevice physical_device;
    std::vector<QueueInfo> queues;
};

struct ServerState {
    ServerState();

    venus_plus::HandleMap<VkInstance> instance_map;
    venus_plus::HandleMap<VkPhysicalDevice> physical_device_map;
    venus_plus::HandleMap<VkDevice> device_map;
    venus_plus::HandleMap<VkQueue> queue_map;

    std::unordered_map<VkDevice, DeviceInfo> device_info_map;

    uint64_t next_instance_handle = 1;
    uint64_t next_physical_device_handle = 0x1000;
    uint64_t next_device_handle = 0x2000;
    uint64_t next_queue_handle = 0x3000;
    VkPhysicalDevice fake_device_handle = VK_NULL_HANDLE;
    venus_plus::ResourceTracker resource_tracker;
    venus_plus::CommandBufferState command_buffer_state;
    venus_plus::CommandValidator command_validator;
    venus_plus::SyncManager sync_manager;
};

namespace venus_plus {

VkInstance server_state_alloc_instance(ServerState* state);
void server_state_remove_instance(ServerState* state, VkInstance instance);
bool server_state_instance_exists(const ServerState* state, VkInstance instance);
VkPhysicalDevice server_state_get_fake_device(ServerState* state);

// Phase 3: Device management
VkDevice server_state_alloc_device(ServerState* state, VkPhysicalDevice physical_device);
void server_state_remove_device(ServerState* state, VkDevice device);
bool server_state_device_exists(const ServerState* state, VkDevice device);
VkPhysicalDevice server_state_get_device_physical_device(const ServerState* state, VkDevice device);

// Phase 3: Queue management
VkQueue server_state_alloc_queue(ServerState* state, VkDevice device, uint32_t family_index, uint32_t queue_index);
VkQueue server_state_find_queue(const ServerState* state, VkDevice device, uint32_t family_index, uint32_t queue_index);

// Phase 4: Resource management helpers
VkDeviceMemory server_state_alloc_memory(ServerState* state, VkDevice device, const VkMemoryAllocateInfo* info);
bool server_state_free_memory(ServerState* state, VkDeviceMemory memory);
VkBuffer server_state_create_buffer(ServerState* state, VkDevice device, const VkBufferCreateInfo* info);
bool server_state_destroy_buffer(ServerState* state, VkBuffer buffer);
bool server_state_get_buffer_memory_requirements(ServerState* state, VkBuffer buffer, VkMemoryRequirements* requirements);
VkResult server_state_bind_buffer_memory(ServerState* state, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset);
VkImage server_state_create_image(ServerState* state, VkDevice device, const VkImageCreateInfo* info);
bool server_state_destroy_image(ServerState* state, VkImage image);
bool server_state_get_image_memory_requirements(ServerState* state, VkImage image, VkMemoryRequirements* requirements);
VkResult server_state_bind_image_memory(ServerState* state, VkImage image, VkDeviceMemory memory, VkDeviceSize offset);
bool server_state_get_image_subresource_layout(ServerState* state, VkImage image, const VkImageSubresource* subresource, VkSubresourceLayout* layout);

VkCommandPool server_state_create_command_pool(ServerState* state, VkDevice device, const VkCommandPoolCreateInfo* info);
bool server_state_destroy_command_pool(ServerState* state, VkCommandPool pool);
VkResult server_state_reset_command_pool(ServerState* state, VkCommandPool pool, VkCommandPoolResetFlags flags);
VkResult server_state_allocate_command_buffers(ServerState* state, VkDevice device, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* buffers);
void server_state_free_command_buffers(ServerState* state, VkCommandPool pool, uint32_t commandBufferCount, const VkCommandBuffer* buffers);
VkResult server_state_begin_command_buffer(ServerState* state, VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* info);
VkResult server_state_end_command_buffer(ServerState* state, VkCommandBuffer commandBuffer);
VkResult server_state_reset_command_buffer(ServerState* state, VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags);
bool server_state_command_buffer_is_recording(const ServerState* state, VkCommandBuffer commandBuffer);
void server_state_mark_command_buffer_invalid(ServerState* state, VkCommandBuffer commandBuffer);
bool server_state_validate_cmd_copy_buffer(ServerState* state, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* regions);
bool server_state_validate_cmd_copy_image(ServerState* state, VkImage srcImage, VkImage dstImage, uint32_t regionCount, const VkImageCopy* regions);
bool server_state_validate_cmd_blit_image(ServerState* state, VkImage srcImage, VkImage dstImage, uint32_t regionCount, const VkImageBlit* regions);
bool server_state_validate_cmd_copy_buffer_to_image(ServerState* state, VkBuffer srcBuffer, VkImage dstImage, uint32_t regionCount, const VkBufferImageCopy* regions);
bool server_state_validate_cmd_copy_image_to_buffer(ServerState* state, VkImage srcImage, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* regions);
bool server_state_validate_cmd_fill_buffer(ServerState* state, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);
bool server_state_validate_cmd_update_buffer(ServerState* state, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize dataSize, const void* data);
bool server_state_validate_cmd_clear_color_image(ServerState* state, VkImage image, uint32_t rangeCount, const VkImageSubresourceRange* ranges);

// Phase 6: Sync and submission
VkFence server_state_create_fence(ServerState* state, VkDevice device, const VkFenceCreateInfo* info);
bool server_state_destroy_fence(ServerState* state, VkFence fence);
VkResult server_state_get_fence_status(ServerState* state, VkFence fence);
VkResult server_state_reset_fences(ServerState* state, uint32_t fenceCount, const VkFence* pFences);
VkResult server_state_wait_for_fences(ServerState* state,
                                      uint32_t fenceCount,
                                      const VkFence* pFences,
                                      VkBool32 waitAll,
                                      uint64_t timeout);
VkSemaphore server_state_create_semaphore(ServerState* state, VkDevice device, const VkSemaphoreCreateInfo* info);
bool server_state_destroy_semaphore(ServerState* state, VkSemaphore semaphore);
VkResult server_state_get_semaphore_counter_value(ServerState* state, VkSemaphore semaphore, uint64_t* pValue);
VkResult server_state_signal_semaphore(ServerState* state, const VkSemaphoreSignalInfo* info);
VkResult server_state_wait_semaphores(ServerState* state, const VkSemaphoreWaitInfo* info, uint64_t timeout);
VkResult server_state_queue_submit(ServerState* state, VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);
VkResult server_state_queue_wait_idle(ServerState* state, VkQueue queue);
VkResult server_state_device_wait_idle(ServerState* state, VkDevice device);

} // namespace venus_plus

#endif // VENUS_PLUS_SERVER_STATE_H
