#ifndef VENUS_PLUS_SERVER_STATE_H
#define VENUS_PLUS_SERVER_STATE_H

#include "state/handle_map.h"
#include "state/resource_tracker.h"
#include "state/command_buffer_state.h"
#include "state/command_validator.h"
#include "state/sync_manager.h"
#include "vulkan/vulkan_context.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

struct QueueInfo {
    VkQueue client_handle = VK_NULL_HANDLE;
    VkQueue real_handle = VK_NULL_HANDLE;
    uint32_t family_index = 0;
    uint32_t queue_index = 0;
};

struct DeviceInfo {
    VkDevice client_handle = VK_NULL_HANDLE;
    VkDevice real_handle = VK_NULL_HANDLE;
    VkPhysicalDevice client_physical_device = VK_NULL_HANDLE;
    VkPhysicalDevice real_physical_device = VK_NULL_HANDLE;
    std::vector<QueueInfo> queues;
};

struct PhysicalDeviceInfo {
    VkPhysicalDevice client_handle = VK_NULL_HANDLE;
    VkPhysicalDevice real_handle = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties = {};
    VkPhysicalDeviceMemoryProperties memory_properties = {};
    std::vector<VkQueueFamilyProperties> queue_families;
};

struct InstanceInfo {
    VkInstance client_handle = VK_NULL_HANDLE;
    VkInstance real_handle = VK_NULL_HANDLE;
};

struct ServerState {
    ServerState();

    bool initialize_vulkan(bool enable_validation);
    void shutdown_vulkan();
    bool vulkan_ready() const { return real_physical_device != VK_NULL_HANDLE; }

    venus_plus::HandleMap<VkInstance> instance_map;
    venus_plus::HandleMap<VkPhysicalDevice> physical_device_map;
    venus_plus::HandleMap<VkDevice> device_map;
    venus_plus::HandleMap<VkQueue> queue_map;

    std::unordered_map<VkInstance, InstanceInfo> instance_info_map;
    std::unordered_map<VkPhysicalDevice, PhysicalDeviceInfo> physical_device_info_map;
    std::unordered_map<VkDevice, DeviceInfo> device_info_map;
    std::unordered_map<VkQueue, QueueInfo> queue_info_map;

    uint64_t next_instance_handle = 1;
    uint64_t next_physical_device_handle = 0x1000;
    uint64_t next_device_handle = 0x2000;
    uint64_t next_queue_handle = 0x3000;
    VkPhysicalDevice fake_device_handle = VK_NULL_HANDLE;
    VkPhysicalDevice real_physical_device = VK_NULL_HANDLE;
    VkInstance real_instance = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physical_device_properties = {};
    VkPhysicalDeviceMemoryProperties physical_device_memory_properties = {};
    std::vector<VkQueueFamilyProperties> queue_family_properties;

    venus_plus::VulkanContext vulkan_context;
    venus_plus::ResourceTracker resource_tracker;
    venus_plus::CommandBufferState command_buffer_state;
    venus_plus::CommandValidator command_validator;
    venus_plus::SyncManager sync_manager;
};

namespace venus_plus {

VkInstance server_state_alloc_instance(ServerState* state);
void server_state_remove_instance(ServerState* state, VkInstance instance);
bool server_state_instance_exists(const ServerState* state, VkInstance instance);
VkInstance server_state_get_real_instance(const ServerState* state, VkInstance instance);
VkPhysicalDevice server_state_get_fake_device(ServerState* state);
VkPhysicalDevice server_state_get_real_physical_device(const ServerState* state, VkPhysicalDevice physical_device);

// Phase 3: Device management
VkDevice server_state_alloc_device(ServerState* state,
                                   VkPhysicalDevice physical_device,
                                   VkDevice real_device);
void server_state_remove_device(ServerState* state, VkDevice device);
bool server_state_device_exists(const ServerState* state, VkDevice device);
VkPhysicalDevice server_state_get_device_physical_device(const ServerState* state, VkDevice device);
VkDevice server_state_get_real_device(const ServerState* state, VkDevice device);

// Phase 3: Queue management
VkQueue server_state_alloc_queue(ServerState* state,
                                 VkDevice device,
                                 uint32_t family_index,
                                 uint32_t queue_index,
                                 VkQueue real_queue);
VkQueue server_state_find_queue(const ServerState* state, VkDevice device, uint32_t family_index, uint32_t queue_index);
VkQueue server_state_get_real_queue(const ServerState* state, VkQueue queue);

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
VkBuffer server_state_get_real_buffer(const ServerState* state, VkBuffer buffer);
VkImage server_state_get_real_image(const ServerState* state, VkImage image);
VkDeviceMemory server_state_get_real_memory(const ServerState* state, VkDeviceMemory memory);
VkShaderModule server_state_create_shader_module(ServerState* state, VkDevice device, const VkShaderModuleCreateInfo* info);
bool server_state_destroy_shader_module(ServerState* state, VkShaderModule module);
VkShaderModule server_state_get_real_shader_module(const ServerState* state, VkShaderModule module);
VkDescriptorSetLayout server_state_create_descriptor_set_layout(ServerState* state, VkDevice device, const VkDescriptorSetLayoutCreateInfo* info);
bool server_state_destroy_descriptor_set_layout(ServerState* state, VkDescriptorSetLayout layout);
VkDescriptorSetLayout server_state_get_real_descriptor_set_layout(const ServerState* state, VkDescriptorSetLayout layout);
VkDescriptorPool server_state_create_descriptor_pool(ServerState* state, VkDevice device, const VkDescriptorPoolCreateInfo* info);
bool server_state_destroy_descriptor_pool(ServerState* state, VkDescriptorPool pool);
VkResult server_state_reset_descriptor_pool(ServerState* state, VkDescriptorPool pool, VkDescriptorPoolResetFlags flags);
VkDescriptorPool server_state_get_real_descriptor_pool(const ServerState* state, VkDescriptorPool pool);
VkResult server_state_allocate_descriptor_sets(ServerState* state,
                                               VkDevice device,
                                               const VkDescriptorSetAllocateInfo* info,
                                               std::vector<VkDescriptorSet>* out_sets);
VkResult server_state_free_descriptor_sets(ServerState* state,
                                           VkDevice device,
                                           VkDescriptorPool pool,
                                           uint32_t descriptorSetCount,
                                           const VkDescriptorSet* pDescriptorSets);
VkDescriptorSet server_state_get_real_descriptor_set(const ServerState* state, VkDescriptorSet set);
VkPipelineLayout server_state_create_pipeline_layout(ServerState* state, VkDevice device, const VkPipelineLayoutCreateInfo* info);
bool server_state_destroy_pipeline_layout(ServerState* state, VkPipelineLayout layout);
VkPipelineLayout server_state_get_real_pipeline_layout(const ServerState* state, VkPipelineLayout layout);
VkResult server_state_create_compute_pipelines(ServerState* state,
                                               VkDevice device,
                                               VkPipelineCache cache,
                                               uint32_t count,
                                               const VkComputePipelineCreateInfo* infos,
                                               std::vector<VkPipeline>* out_pipelines);
bool server_state_destroy_pipeline(ServerState* state, VkPipeline pipeline);
VkPipeline server_state_get_real_pipeline(const ServerState* state, VkPipeline pipeline);

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
VkCommandBuffer server_state_get_real_command_buffer(const ServerState* state, VkCommandBuffer commandBuffer);
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
