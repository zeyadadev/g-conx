#include "network/network_server.h"
#include "memory/memory_transfer.h"
#include "renderer_decoder.h"
#include "server_state.h"
#include "protocol/memory_transfer.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <vulkan/vulkan.h>

using namespace venus_plus;

static ServerState g_server_state;
static VenusRenderer* g_renderer = nullptr;
static MemoryTransferHandler g_memory_transfer(&g_server_state);

bool handle_client_message(int client_fd, const void* data, size_t size) {
    if (size >= sizeof(uint32_t)) {
        uint32_t command = 0;
        std::memcpy(&command, data, sizeof(command));
        if (command == VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA) {
            VkResult result = g_memory_transfer.handle_transfer_command(data, size);
            if (!NetworkServer::send_to_client(client_fd, &result, sizeof(result))) {
                std::cerr << "[Server] Failed to send transfer ack\n";
                return false;
            }
            return true;
        }
        if (command == VENUS_PLUS_CMD_READ_MEMORY_DATA) {
            std::vector<uint8_t> payload;
            VkResult result = g_memory_transfer.handle_read_command(data, size, &payload);
            const size_t reply_size = sizeof(VkResult) +
                                      (result == VK_SUCCESS ? payload.size() : 0);
            std::vector<uint8_t> reply(reply_size);
            std::memcpy(reply.data(), &result, sizeof(VkResult));
            if (result == VK_SUCCESS && !payload.empty()) {
                std::memcpy(reply.data() + sizeof(VkResult), payload.data(), payload.size());
            }
            if (!NetworkServer::send_to_client(client_fd, reply.data(), reply.size())) {
                std::cerr << "[Server] Failed to send read reply\n";
                return false;
            }
            return true;
        }
    }

    uint8_t* reply = nullptr;
    size_t reply_size = 0;

    if (!venus_renderer_handle(g_renderer, data, size, &reply, &reply_size)) {
        std::cerr << "[Server] Failed to decode Venus command\n";
        if (reply) {
            std::free(reply);
        }
        return false;
    }

    if (reply && reply_size > 0) {
        if (!NetworkServer::send_to_client(client_fd, reply, reply_size)) {
            std::cerr << "[Server] Failed to send reply\n";
            std::free(reply);
            return false;
        }
        std::free(reply);
    }

    return true;
}

int main(int argc, char** argv) {
    std::cout << "Venus Plus Server v0.1\n";
    std::cout << "======================\n\n";

    bool enable_validation = false;
    int port = 5556;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--validation") == 0) {
            enable_validation = true;
        } else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        }
    }

    if (!g_server_state.initialize_vulkan(enable_validation)) {
        std::cerr << "Failed to initialize Vulkan on server\n";
        return 1;
    }

    NetworkServer server;

    g_renderer = venus_renderer_create(&g_server_state);
    if (!g_renderer) {
        std::cerr << "Failed to initialize renderer decoder\n";
        g_server_state.shutdown_vulkan();
        return 1;
    }

    if (!server.start(port)) {
        std::cerr << "Failed to start server on port " << port << "\n";
        venus_renderer_destroy(g_renderer);
        g_renderer = nullptr;
        g_server_state.shutdown_vulkan();
        return 1;
    }

    std::cout << "Listening on port " << port
              << (enable_validation ? " (validation enabled)\n\n" : "\n\n");

    server.run(handle_client_message);

    venus_renderer_destroy(g_renderer);
    g_renderer = nullptr;

    g_server_state.shutdown_vulkan();

    return 0;
}
