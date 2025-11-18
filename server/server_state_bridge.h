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

// Phase 3: Device and queue management
VkDevice server_state_bridge_alloc_device(struct ServerState* state, VkPhysicalDevice physical_device);
void server_state_bridge_remove_device(struct ServerState* state, VkDevice device);
bool server_state_bridge_device_exists(const struct ServerState* state, VkDevice device);
VkQueue server_state_bridge_alloc_queue(struct ServerState* state, VkDevice device, uint32_t family_index, uint32_t queue_index);
VkQueue server_state_bridge_find_queue(const struct ServerState* state, VkDevice device, uint32_t family_index, uint32_t queue_index);

#ifdef __cplusplus
}
#endif

#endif // VENUS_PLUS_SERVER_STATE_BRIDGE_H
