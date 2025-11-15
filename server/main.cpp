#include "network/network_server.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <vulkan/vulkan.h>

using namespace venus_plus;

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
