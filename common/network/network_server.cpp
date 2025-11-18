#include "network_server.h"
#include "message.h"
#include "socket_utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

#include "utils/logging.h"

#define NETWORK_LOG_ERROR() VP_LOG_STREAM_ERROR(NETWORK)
#define NETWORK_LOG_INFO() VP_LOG_STREAM_INFO(NETWORK)

namespace venus_plus {

NetworkServer::NetworkServer() : server_fd_(-1), running_(false) {}

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::start(uint16_t port, const std::string& bind_addr) {
    // Create socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        NETWORK_LOG_ERROR() << "Failed to create server socket";
        return false;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        NETWORK_LOG_ERROR() << "setsockopt failed";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, bind_addr.c_str(), &addr.sin_addr);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        NETWORK_LOG_ERROR() << "Bind failed on port " << port;
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Listen
    if (listen(server_fd_, 5) < 0) {
        NETWORK_LOG_ERROR() << "Listen failed";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    NETWORK_LOG_INFO() << "Server listening on " << bind_addr << ":" << port;
    return true;
}

void NetworkServer::run(ClientHandler handler) {
    while (running_) {
        // Accept client
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) {
                NETWORK_LOG_ERROR() << "Accept failed";
            }
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        NETWORK_LOG_INFO() << "Client connected from " << client_ip;

        // Handle client (inline for Phase 1, will thread later)
        handle_client(client_fd, handler);
    }
}

void NetworkServer::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
}

void NetworkServer::handle_client(int client_fd, ClientHandler handler) {
    std::vector<uint8_t> buffer;

    while (true) {
        // Receive message header
        MessageHeader header;
        if (!read_all(client_fd, &header, sizeof(header))) {
            break;
        }

        // Validate magic
        if (header.magic != MESSAGE_MAGIC) {
            NETWORK_LOG_ERROR() << "Invalid message magic from client";
            break;
        }

        // Receive payload
        buffer.resize(header.size);
        if (!read_all(client_fd, buffer.data(), header.size)) {
            break;
        }

        // Call handler
        if (!handler(client_fd, buffer.data(), header.size)) {
            break;
        }
    }

    NETWORK_LOG_INFO() << "Client disconnected";
    close(client_fd);
}

bool NetworkServer::send_to_client(int client_fd, const void* data, size_t size) {
    // Send header
    MessageHeader header;
    header.magic = MESSAGE_MAGIC;
    header.size = size;

    if (!write_all(client_fd, &header, sizeof(header))) {
        return false;
    }

    // Send payload
    if (!write_all(client_fd, data, size)) {
        return false;
    }

    return true;
}

} // namespace venus_plus
