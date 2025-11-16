#include "server_state.h"
#include "server_state_bridge.h"

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

} // extern "C"
