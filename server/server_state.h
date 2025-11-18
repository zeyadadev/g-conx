#ifndef VENUS_PLUS_SERVER_STATE_H
#define VENUS_PLUS_SERVER_STATE_H

#include "state/handle_map.h"
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

} // namespace venus_plus

#endif // VENUS_PLUS_SERVER_STATE_H
