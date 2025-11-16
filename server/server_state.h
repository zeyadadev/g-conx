#ifndef VENUS_PLUS_SERVER_STATE_H
#define VENUS_PLUS_SERVER_STATE_H

#include "state/handle_map.h"
#include <vulkan/vulkan.h>
#include <cstdint>

struct ServerState {
    venus_plus::HandleMap<VkInstance> instance_map;
    venus_plus::HandleMap<VkPhysicalDevice> physical_device_map;
    uint64_t next_instance_handle = 1;
    uint64_t next_physical_device_handle = 0x1000;
    VkPhysicalDevice fake_device_handle = VK_NULL_HANDLE;
};

namespace venus_plus {

VkInstance server_state_alloc_instance(ServerState* state);
void server_state_remove_instance(ServerState* state, VkInstance instance);
bool server_state_instance_exists(const ServerState* state, VkInstance instance);
VkPhysicalDevice server_state_get_fake_device(ServerState* state);

} // namespace venus_plus

#endif // VENUS_PLUS_SERVER_STATE_H
