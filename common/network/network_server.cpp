#include "network_server.h"
#include "message.h"
#include "socket_utils.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace venus_plus {

NetworkServer::NetworkServer() : server_fd_(-1), running_(false) {}

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::start(uint16_t port, const std::string& bind_addr) {
    // Create socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "Failed to create server socket\n";
        return false;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt failed\n";
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
        std::cerr << "Bind failed on port " << port << "\n";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Listen
    if (listen(server_fd_, 5) < 0) {
        std::cerr << "Listen failed\n";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    std::cout << "Server listening on " << bind_addr << ":" << port << "\n";
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
                std::cerr << "Accept failed\n";
            }
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Client connected from " << client_ip << "\n";

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
            std::cerr << "Invalid message magic from client\n";
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

    std::cout << "Client disconnected\n";
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
