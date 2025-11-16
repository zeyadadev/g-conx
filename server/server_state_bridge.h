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

#ifdef __cplusplus
}
#endif

#endif // VENUS_PLUS_SERVER_STATE_BRIDGE_H
