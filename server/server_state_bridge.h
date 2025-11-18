#ifndef VENUS_PLUS_SERVER_STATE_BRIDGE_H
#define VENUS_PLUS_SERVER_STATE_BRIDGE_H

#include <stdbool.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ServerState;

VkInstance server_state_bridge_alloc_instance(struct ServerState* state);
void server_state_bridge_remove_instance(struct ServerState* state, VkInstance instance);
bool server_state_bridge_instance_exists(const struct ServerState* state, VkInstance instance);
VkPhysicalDevice server_state_bridge_get_fake_device(struct ServerState* state);
VkInstance server_state_bridge_get_real_instance(const struct ServerState* state, VkInstance instance);
VkPhysicalDevice server_state_bridge_get_real_physical_device(const struct ServerState* state,
                                                              VkPhysicalDevice physical_device);
VkDevice server_state_bridge_get_real_device(const struct ServerState* state, VkDevice device);
VkQueue server_state_bridge_get_real_queue(const struct ServerState* state, VkQueue queue);
VkBuffer server_state_bridge_get_real_buffer(const struct ServerState* state, VkBuffer buffer);
VkImage server_state_bridge_get_real_image(const struct ServerState* state, VkImage image);
VkDeviceMemory server_state_bridge_get_real_memory(const struct ServerState* state, VkDeviceMemory memory);
VkCommandBuffer server_state_bridge_get_real_command_buffer(const struct ServerState* state,
                                                            VkCommandBuffer commandBuffer);

// Phase 3: Device and queue management
VkDevice server_state_bridge_alloc_device(struct ServerState* state,
                                          VkPhysicalDevice physical_device,
                                          VkDevice real_device);
void server_state_bridge_remove_device(struct ServerState* state, VkDevice device);
bool server_state_bridge_device_exists(const struct ServerState* state, VkDevice device);
VkQueue server_state_bridge_alloc_queue(struct ServerState* state,
                                        VkDevice device,
                                        uint32_t family_index,
                                        uint32_t queue_index,
                                        VkQueue real_queue);
VkQueue server_state_bridge_find_queue(const struct ServerState* state, VkDevice device, uint32_t family_index, uint32_t queue_index);

// Phase 4: Resource management bridge
VkDeviceMemory server_state_bridge_alloc_memory(struct ServerState* state, VkDevice device, const VkMemoryAllocateInfo* info);
bool server_state_bridge_free_memory(struct ServerState* state, VkDeviceMemory memory);
VkBuffer server_state_bridge_create_buffer(struct ServerState* state, VkDevice device, const VkBufferCreateInfo* info);
bool server_state_bridge_destroy_buffer(struct ServerState* state, VkBuffer buffer);
bool server_state_bridge_get_buffer_memory_requirements(struct ServerState* state, VkBuffer buffer, VkMemoryRequirements* requirements);
VkResult server_state_bridge_bind_buffer_memory(struct ServerState* state, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset);
VkImage server_state_bridge_create_image(struct ServerState* state, VkDevice device, const VkImageCreateInfo* info);
bool server_state_bridge_destroy_image(struct ServerState* state, VkImage image);
bool server_state_bridge_get_image_memory_requirements(struct ServerState* state, VkImage image, VkMemoryRequirements* requirements);
VkResult server_state_bridge_bind_image_memory(struct ServerState* state, VkImage image, VkDeviceMemory memory, VkDeviceSize offset);
bool server_state_bridge_get_image_subresource_layout(struct ServerState* state, VkImage image, const VkImageSubresource* subresource, VkSubresourceLayout* layout);

VkCommandPool server_state_bridge_create_command_pool(struct ServerState* state, VkDevice device, const VkCommandPoolCreateInfo* info);
bool server_state_bridge_destroy_command_pool(struct ServerState* state, VkCommandPool commandPool);
VkResult server_state_bridge_reset_command_pool(struct ServerState* state, VkCommandPool commandPool, VkCommandPoolResetFlags flags);
VkResult server_state_bridge_allocate_command_buffers(struct ServerState* state, VkDevice device, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* pCommandBuffers);
void server_state_bridge_free_command_buffers(struct ServerState* state, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers);
VkResult server_state_bridge_begin_command_buffer(struct ServerState* state, VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* info);
VkResult server_state_bridge_end_command_buffer(struct ServerState* state, VkCommandBuffer commandBuffer);
VkResult server_state_bridge_reset_command_buffer(struct ServerState* state, VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags);
bool server_state_bridge_command_buffer_is_recording(const struct ServerState* state, VkCommandBuffer commandBuffer);
void server_state_bridge_mark_command_buffer_invalid(struct ServerState* state, VkCommandBuffer commandBuffer);
bool server_state_bridge_validate_cmd_copy_buffer(struct ServerState* state, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy* pRegions);
bool server_state_bridge_validate_cmd_copy_image(struct ServerState* state, VkImage srcImage, VkImage dstImage, uint32_t regionCount, const VkImageCopy* pRegions);
bool server_state_bridge_validate_cmd_blit_image(struct ServerState* state, VkImage srcImage, VkImage dstImage, uint32_t regionCount, const VkImageBlit* pRegions);
bool server_state_bridge_validate_cmd_copy_buffer_to_image(struct ServerState* state, VkBuffer srcBuffer, VkImage dstImage, uint32_t regionCount, const VkBufferImageCopy* pRegions);
bool server_state_bridge_validate_cmd_copy_image_to_buffer(struct ServerState* state, VkImage srcImage, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions);
bool server_state_bridge_validate_cmd_fill_buffer(struct ServerState* state, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);
bool server_state_bridge_validate_cmd_update_buffer(struct ServerState* state, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize dataSize, const void* data);
bool server_state_bridge_validate_cmd_clear_color_image(struct ServerState* state, VkImage image, uint32_t rangeCount, const VkImageSubresourceRange* pRanges);

VkFence server_state_bridge_create_fence(struct ServerState* state,
                                         VkDevice device,
                                         const VkFenceCreateInfo* info);
bool server_state_bridge_destroy_fence(struct ServerState* state, VkFence fence);
VkResult server_state_bridge_get_fence_status(struct ServerState* state, VkFence fence);
VkResult server_state_bridge_reset_fences(struct ServerState* state,
                                          uint32_t fenceCount,
                                          const VkFence* pFences);
VkResult server_state_bridge_wait_for_fences(struct ServerState* state,
                                             uint32_t fenceCount,
                                             const VkFence* pFences,
                                             VkBool32 waitAll,
                                             uint64_t timeout);
VkSemaphore server_state_bridge_create_semaphore(struct ServerState* state,
                                                 VkDevice device,
                                                 const VkSemaphoreCreateInfo* info);
bool server_state_bridge_destroy_semaphore(struct ServerState* state, VkSemaphore semaphore);
VkResult server_state_bridge_get_semaphore_counter_value(struct ServerState* state,
                                                         VkSemaphore semaphore,
                                                         uint64_t* pValue);
VkResult server_state_bridge_signal_semaphore(struct ServerState* state, const VkSemaphoreSignalInfo* info);
VkResult server_state_bridge_wait_semaphores(struct ServerState* state,
                                             const VkSemaphoreWaitInfo* info,
                                             uint64_t timeout);
VkResult server_state_bridge_queue_submit(struct ServerState* state,
                                          VkQueue queue,
                                          uint32_t submitCount,
                                          const VkSubmitInfo* pSubmits,
                                          VkFence fence);
VkResult server_state_bridge_queue_wait_idle(struct ServerState* state, VkQueue queue);
VkResult server_state_bridge_device_wait_idle(struct ServerState* state, VkDevice device);

#ifdef __cplusplus
}
#endif

#endif // VENUS_PLUS_SERVER_STATE_BRIDGE_H
