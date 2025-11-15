#include "network/network_server.h"
#include "state/handle_map.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <vulkan/vulkan.h>

using namespace venus_plus;

// Global handle maps for Phase 2
static HandleMap<VkInstance> g_instance_map;
static HandleMap<VkPhysicalDevice> g_physical_device_map;

bool handle_client_message(int client_fd, const void* data, size_t size) {
    std::cout << "[Server] Received message: " << size << " bytes\n";

    // Decode command type
    if (size < 4) {
        std::cerr << "[Server] Message too small\n";
        return false;
    }

    uint32_t command_type;
    memcpy(&command_type, data, 4);

    std::cout << "[Server] Command type: " << command_type << "\n";

    // Handle vkEnumerateInstanceVersion (command_type == 1)
    if (command_type == 1) {
        // Create fake reply: [result: uint32_t][version: uint32_t]
        uint32_t reply_data[2];
        reply_data[0] = VK_SUCCESS;
        reply_data[1] = VK_API_VERSION_1_3;  // Version 1.3.0

        std::cout << "[Server] Sending reply: VK_API_VERSION_1_3\n";

        if (!NetworkServer::send_to_client(client_fd, reply_data, sizeof(reply_data))) {
            std::cerr << "[Server] Failed to send reply\n";
            return false;
        }

        return true;  // Continue handling this client
    }

    // Handle vkEnumerateInstanceExtensionProperties (command_type == 2)
    if (command_type == 2) {
        // Create fake reply: [result: uint32_t][count: uint32_t]
        uint32_t reply_data[2];
        reply_data[0] = VK_SUCCESS;
        reply_data[1] = 0;  // 0 extensions for Phase 1

        std::cout << "[Server] Sending reply: 0 extensions\n";

        if (!NetworkServer::send_to_client(client_fd, reply_data, sizeof(reply_data))) {
            std::cerr << "[Server] Failed to send reply\n";
            return false;
        }

        return true;  // Continue handling this client
    }

    // Handle vkCreateInstance (command_type == 3) - Phase 2
    if (command_type == 3) {
        // Decode: [instance_handle: uint64_t]
        if (size < 12) {  // 4 (cmd_type) + 8 (handle)
            std::cerr << "[Server] vkCreateInstance message too small\n";
            return false;
        }

        uint64_t instance_handle;
        memcpy(&instance_handle, static_cast<const uint8_t*>(data) + 4, 8);

        std::cout << "[Server] vkCreateInstance for handle: 0x" << std::hex << instance_handle << std::dec << "\n";

        // For Phase 2: Store fake mapping (map to itself)
        VkInstance client_instance = reinterpret_cast<VkInstance>(instance_handle);
        VkInstance fake_instance = client_instance;  // Fake: use same handle
        g_instance_map.insert(client_instance, fake_instance);

        std::cout << "[Server] Instance created (fake mapping)\n";

        // Send reply: [result: uint32_t]
        uint32_t result = VK_SUCCESS;
        if (!NetworkServer::send_to_client(client_fd, &result, sizeof(result))) {
            std::cerr << "[Server] Failed to send reply\n";
            return false;
        }

        return true;
    }

    // Handle vkDestroyInstance (command_type == 4) - Phase 2
    if (command_type == 4) {
        // Decode: [instance_handle: uint64_t]
        if (size < 12) {
            std::cerr << "[Server] vkDestroyInstance message too small\n";
            return false;
        }

        uint64_t instance_handle;
        memcpy(&instance_handle, static_cast<const uint8_t*>(data) + 4, 8);

        std::cout << "[Server] vkDestroyInstance for handle: 0x" << std::hex << instance_handle << std::dec << "\n";

        // Remove from mapping
        VkInstance client_instance = reinterpret_cast<VkInstance>(instance_handle);
        if (g_instance_map.exists(client_instance)) {
            g_instance_map.remove(client_instance);
            std::cout << "[Server] Instance destroyed\n";
        } else {
            std::cerr << "[Server] Warning: Instance not found in map\n";
        }

        // Send reply: [result: uint32_t]
        uint32_t result = VK_SUCCESS;
        if (!NetworkServer::send_to_client(client_fd, &result, sizeof(result))) {
            std::cerr << "[Server] Failed to send reply\n";
            return false;
        }

        return true;
    }

    // Handle vkEnumeratePhysicalDevices (command_type == 5) - Phase 2
    if (command_type == 5) {
        // Decode: [instance_handle: uint64_t][query_devices: uint32_t]
        if (size < 16) {  // 4 (cmd) + 8 (instance) + 4 (query_devices)
            std::cerr << "[Server] vkEnumeratePhysicalDevices message too small\n";
            return false;
        }

        uint64_t instance_handle;
        uint32_t query_devices;
        memcpy(&instance_handle, static_cast<const uint8_t*>(data) + 4, 8);
        memcpy(&query_devices, static_cast<const uint8_t*>(data) + 12, 4);

        std::cout << "[Server] vkEnumeratePhysicalDevices for instance: 0x" << std::hex << instance_handle << std::dec;
        std::cout << ", query_devices=" << query_devices << "\n";

        VkInstance client_instance = reinterpret_cast<VkInstance>(instance_handle);

        // Verify instance exists
        if (!g_instance_map.exists(client_instance)) {
            std::cerr << "[Server] Instance not found\n";
            uint32_t reply_data[2] = {static_cast<uint32_t>(VK_ERROR_INITIALIZATION_FAILED), 0};
            NetworkServer::send_to_client(client_fd, reply_data, sizeof(reply_data));
            return false;
        }

        // For Phase 2: Return 1 fake physical device
        uint32_t device_count = 1;

        if (query_devices == 0) {
            // First call: just return count
            std::cout << "[Server] Returning device count: " << device_count << "\n";
            uint32_t reply_data[2] = {VK_SUCCESS, device_count};
            if (!NetworkServer::send_to_client(client_fd, reply_data, sizeof(reply_data))) {
                std::cerr << "[Server] Failed to send reply\n";
                return false;
            }
        } else {
            // Second call: return count + device handles
            std::cout << "[Server] Returning " << device_count << " device(s)\n";

            // Generate fake physical device handle
            uint64_t fake_device_handle = 0x1000;  // Fake device
            VkPhysicalDevice client_device = reinterpret_cast<VkPhysicalDevice>(fake_device_handle);
            VkPhysicalDevice fake_device = client_device;  // Fake: use same handle

            // Store mapping
            g_physical_device_map.insert(client_device, fake_device);

            // Send reply: [result: uint32_t][count: uint32_t][device0: uint64_t]
            std::vector<uint8_t> reply(8 + device_count * 8);
            uint32_t result = VK_SUCCESS;
            memcpy(reply.data(), &result, 4);
            memcpy(reply.data() + 4, &device_count, 4);
            memcpy(reply.data() + 8, &fake_device_handle, 8);

            if (!NetworkServer::send_to_client(client_fd, reply.data(), reply.size())) {
                std::cerr << "[Server] Failed to send reply\n";
                return false;
            }
        }

        return true;
    }

    std::cerr << "[Server] Unknown command type: " << command_type << "\n";
    return false;
}

int main(int argc, char** argv) {
    std::cout << "Venus Plus Server v0.1\n";
    std::cout << "======================\n\n";

    NetworkServer server;

    if (!server.start(5556)) {
        std::cerr << "Failed to start server\n";
        return 1;
    }

    std::cout << "Waiting for clients...\n\n";

    server.run(handle_client_message);

    return 0;
}
