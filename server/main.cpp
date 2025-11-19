#include "network/network_server.h"
#include "memory/memory_transfer.h"
#include "renderer_decoder.h"
#include "server_state.h"
#include "protocol/memory_transfer.h"
#include "protocol/frame_transfer.h"
#include "wsi/swapchain_manager.h"
#include "utils/logging.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <vulkan/vulkan.h>

using namespace venus_plus;

#define SERVER_LOG_ERROR() VP_LOG_STREAM_ERROR(SERVER)
#define SERVER_LOG_INFO() VP_LOG_STREAM_INFO(SERVER)

static ServerState g_server_state;
static VenusRenderer* g_renderer = nullptr;
static MemoryTransferHandler g_memory_transfer(&g_server_state);
static ServerSwapchainManager g_swapchain_manager(&g_server_state);

bool handle_client_message(int client_fd, const void* data, size_t size) {
    if (size >= sizeof(uint32_t)) {
        uint32_t command = 0;
        std::memcpy(&command, data, sizeof(command));
        if (command == VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA) {
            VkResult result = g_memory_transfer.handle_transfer_command(data, size);
            if (!NetworkServer::send_to_client(client_fd, &result, sizeof(result))) {
                SERVER_LOG_ERROR() << "Failed to send transfer ack";
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
                SERVER_LOG_ERROR() << "Failed to send read reply";
                return false;
            }
            return true;
        }
        if (command == VENUS_PLUS_CMD_CREATE_SWAPCHAIN) {
            if (size < sizeof(VenusSwapchainCreateRequest)) {
                return false;
            }
            auto* request = reinterpret_cast<const VenusSwapchainCreateRequest*>(data);
            VenusSwapchainCreateReply reply = {};
            VkResult create_result = g_swapchain_manager.create_swapchain(request->create_info, &reply);
            reply.result = create_result;
            NetworkServer::send_to_client(client_fd, &reply, sizeof(reply));
            return true;
        }
        if (command == VENUS_PLUS_CMD_DESTROY_SWAPCHAIN) {
            if (size < sizeof(VenusSwapchainDestroyRequest)) {
                return false;
            }
            auto* request = reinterpret_cast<const VenusSwapchainDestroyRequest*>(data);
            g_swapchain_manager.destroy_swapchain(request->swapchain_id);
            VkResult result = VK_SUCCESS;
            NetworkServer::send_to_client(client_fd, &result, sizeof(result));
            return true;
        }
        if (command == VENUS_PLUS_CMD_ACQUIRE_IMAGE) {
            if (size < sizeof(VenusSwapchainAcquireRequest)) {
                return false;
            }
            auto* request = reinterpret_cast<const VenusSwapchainAcquireRequest*>(data);
            VenusSwapchainAcquireReply reply = {};
            reply.result = g_swapchain_manager.acquire_image(request->swapchain_id,
                                                             &reply.image_index);
            NetworkServer::send_to_client(client_fd, &reply, sizeof(reply));
            return true;
        }
        if (command == VENUS_PLUS_CMD_PRESENT) {
            if (size < sizeof(VenusSwapchainPresentRequest)) {
                return false;
            }
            auto* request = reinterpret_cast<const VenusSwapchainPresentRequest*>(data);
            VenusSwapchainPresentReply reply = {};
            std::vector<uint8_t> payload;
            reply.result = g_swapchain_manager.present(request->swapchain_id,
                                                       request->image_index,
                                                       &reply.frame,
                                                       &payload);
            std::vector<uint8_t> buffer(sizeof(reply) + (reply.result == VK_SUCCESS ? payload.size() : 0));
            std::memcpy(buffer.data(), &reply, sizeof(reply));
            if (reply.result == VK_SUCCESS && !payload.empty()) {
                std::memcpy(buffer.data() + sizeof(reply), payload.data(), payload.size());
            }
            NetworkServer::send_to_client(client_fd, buffer.data(), buffer.size());
            return true;
        }
    }

    uint8_t* reply = nullptr;
    size_t reply_size = 0;

    if (!venus_renderer_handle(g_renderer, data, size, &reply, &reply_size)) {
        SERVER_LOG_ERROR() << "Failed to decode Venus command";
        if (reply) {
            std::free(reply);
        }
        return false;
    }

    if (reply && reply_size > 0) {
        if (!NetworkServer::send_to_client(client_fd, reply, reply_size)) {
            SERVER_LOG_ERROR() << "Failed to send reply";
            std::free(reply);
            return false;
        }
        std::free(reply);
    }

    return true;
}

int main(int argc, char** argv) {
    SERVER_LOG_INFO() << "Venus Plus Server v0.1";
    SERVER_LOG_INFO() << "======================";

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
        SERVER_LOG_ERROR() << "Failed to initialize Vulkan on server";
        return 1;
    }

    NetworkServer server;

    g_renderer = venus_renderer_create(&g_server_state);
    if (!g_renderer) {
        SERVER_LOG_ERROR() << "Failed to initialize renderer decoder";
        g_server_state.shutdown_vulkan();
        return 1;
    }

    if (!server.start(port)) {
        SERVER_LOG_ERROR() << "Failed to start server on port " << port;
        venus_renderer_destroy(g_renderer);
        g_renderer = nullptr;
        g_server_state.shutdown_vulkan();
        return 1;
    }

    SERVER_LOG_INFO() << "Listening on port " << port
                      << (enable_validation ? " (validation enabled)" : "");

    server.run(handle_client_message);

    venus_renderer_destroy(g_renderer);
    g_renderer = nullptr;

    g_server_state.shutdown_vulkan();

    return 0;
}
