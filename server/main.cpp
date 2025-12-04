#include "network/network_server.h"
#include "memory/memory_transfer.h"
#include "renderer_decoder.h"
#include "server_state.h"
#include "protocol/memory_transfer.h"
#include "protocol/remote_perf.h"
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
        if (command == VENUS_PLUS_CMD_COALESCE_SUBMIT) {
            if (size < sizeof(SubmitCoalesceHeader)) {
                return false;
            }
            auto* header = reinterpret_cast<const SubmitCoalesceHeader*>(data);
            const size_t expected_size = sizeof(SubmitCoalesceHeader) +
                                         static_cast<size_t>(header->transfer_size) +
                                         static_cast<size_t>(header->command_size);
            if (size != expected_size) {
                SERVER_LOG_ERROR() << "Coalesced submit size mismatch";
                return false;
            }

            const uint8_t* transfer_ptr = static_cast<const uint8_t*>(data) + sizeof(SubmitCoalesceHeader);
            const uint8_t* command_ptr = transfer_ptr + header->transfer_size;

            VkResult transfer_result = VK_SUCCESS;
            if ((header->flags & kVenusCoalesceFlagTransfer) && header->transfer_size > 0) {
                transfer_result = g_memory_transfer.handle_transfer_batch_command(transfer_ptr, header->transfer_size);
            }

            uint8_t* venus_reply = nullptr;
            size_t venus_reply_size = 0;
            if (transfer_result == VK_SUCCESS &&
                (header->flags & kVenusCoalesceFlagCommand) &&
                header->command_size > 0) {
                if (!venus_renderer_handle(g_renderer, command_ptr, header->command_size, &venus_reply, &venus_reply_size)) {
                    if (venus_reply) {
                        std::free(venus_reply);
                    }
                    return false;
                }
            }

            SubmitCoalesceReplyHeader reply_header = {};
            reply_header.transfer_result = transfer_result;
            reply_header.command_reply_size =
                transfer_result == VK_SUCCESS ? static_cast<uint32_t>(venus_reply_size) : 0;
            const size_t reply_size = sizeof(reply_header) + reply_header.command_reply_size;
            std::vector<uint8_t> reply(reply_size);
            std::memcpy(reply.data(), &reply_header, sizeof(reply_header));
            if (reply_header.command_reply_size && venus_reply) {
                std::memcpy(reply.data() + sizeof(reply_header), venus_reply, venus_reply_size);
            }
            if (venus_reply) {
                std::free(venus_reply);
            }
            if (!NetworkServer::send_to_client(client_fd, reply.data(), reply.size())) {
                SERVER_LOG_ERROR() << "Failed to send coalesced submit reply";
                return false;
            }
            return true;
        }
        if (command == VENUS_PLUS_CMD_COALESCE_WAIT) {
            if (size < sizeof(WaitInvalidateHeader)) {
                return false;
            }
            auto* header = reinterpret_cast<const WaitInvalidateHeader*>(data);
            const size_t expected_size = sizeof(WaitInvalidateHeader) +
                                         static_cast<size_t>(header->wait_command_size) +
                                         static_cast<size_t>(header->invalidate_size);
            if (size != expected_size) {
                SERVER_LOG_ERROR() << "Coalesced wait payload size mismatch";
                return false;
            }

            const uint8_t* wait_ptr = static_cast<const uint8_t*>(data) + sizeof(WaitInvalidateHeader);
            const uint8_t* invalidate_ptr = wait_ptr + header->wait_command_size;

            uint8_t* wait_reply = nullptr;
            size_t wait_reply_size = 0;
            if ((header->flags & kVenusCoalesceFlagCommand) && header->wait_command_size > 0) {
                if (!venus_renderer_handle(g_renderer, wait_ptr, header->wait_command_size, &wait_reply, &wait_reply_size)) {
                    if (wait_reply) {
                        std::free(wait_reply);
                    }
                    return false;
                }
            }

            std::vector<uint8_t> invalidate_reply;
            if ((header->flags & kVenusCoalesceFlagInvalidate) && header->invalidate_size > 0) {
                VkResult read_result =
                    g_memory_transfer.handle_read_batch_command(invalidate_ptr, header->invalidate_size, &invalidate_reply);
                if (read_result != VK_SUCCESS) {
                    ReadMemoryBatchReplyHeader failure = {};
                    failure.result = read_result;
                    failure.range_count = 0;
                    invalidate_reply.assign(reinterpret_cast<uint8_t*>(&failure),
                                            reinterpret_cast<uint8_t*>(&failure) + sizeof(failure));
                }
            }

            WaitInvalidateReplyHeader reply_header = {};
            reply_header.wait_reply_size = static_cast<uint32_t>(wait_reply_size);
            reply_header.invalidate_reply_size = static_cast<uint32_t>(invalidate_reply.size());

            const size_t reply_size = sizeof(reply_header) +
                                      reply_header.wait_reply_size +
                                      reply_header.invalidate_reply_size;
            std::vector<uint8_t> reply(reply_size);
            std::memcpy(reply.data(), &reply_header, sizeof(reply_header));
            size_t reply_offset = sizeof(reply_header);
            if (reply_header.wait_reply_size && wait_reply) {
                std::memcpy(reply.data() + reply_offset, wait_reply, wait_reply_size);
                reply_offset += wait_reply_size;
            }
            if (reply_header.invalidate_reply_size && !invalidate_reply.empty()) {
                std::memcpy(reply.data() + reply_offset, invalidate_reply.data(), invalidate_reply.size());
            }
            if (wait_reply) {
                std::free(wait_reply);
            }
            if (!NetworkServer::send_to_client(client_fd, reply.data(), reply.size())) {
                SERVER_LOG_ERROR() << "Failed to send coalesced wait reply";
                return false;
            }
            return true;
        }
        if (command == VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA) {
            VkResult result = g_memory_transfer.handle_transfer_command(data, size);
            if (!NetworkServer::send_to_client(client_fd, &result, sizeof(result))) {
                SERVER_LOG_ERROR() << "Failed to send transfer ack";
                return false;
            }
            return true;
        }
        if (command == VENUS_PLUS_CMD_TRANSFER_MEMORY_BATCH) {
            VkResult result = g_memory_transfer.handle_transfer_batch_command(data, size);
            if (!NetworkServer::send_to_client(client_fd, &result, sizeof(result))) {
                SERVER_LOG_ERROR() << "Failed to send transfer batch ack";
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
        if (command == VENUS_PLUS_CMD_READ_MEMORY_BATCH) {
            std::vector<uint8_t> payload;
            VkResult result = g_memory_transfer.handle_read_batch_command(data, size, &payload);
            if (!NetworkServer::send_to_client(client_fd, payload.data(), payload.size())) {
                SERVER_LOG_ERROR() << "Failed to send read batch reply";
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

void reset_after_disconnect() {
    SERVER_LOG_INFO() << "Resetting server state after client disconnect";
    g_swapchain_manager.reset();
    g_server_state.reset_session();
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

    server.run(handle_client_message, reset_after_disconnect);

    venus_renderer_destroy(g_renderer);
    g_renderer = nullptr;

    g_server_state.shutdown_vulkan();

    return 0;
}
