#include "network_client.h"
#include "message.h"
#include "socket_utils.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace venus_plus {

NetworkClient::NetworkClient() : fd_(-1) {}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const std::string& host, uint16_t port) {
    // Create socket
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        std::cerr << "Failed to create socket\n";
        return false;
    }

    // Setup server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address: " << host << "\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    // Connect
    if (::connect(fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed to " << host << ":" << port << "\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    std::cout << "Connected to " << host << ":" << port << "\n";
    return true;
}

bool NetworkClient::send(const void* data, size_t size) {
    if (fd_ < 0) {
        std::cerr << "Not connected\n";
        return false;
    }

    // Send header
    MessageHeader header;
    header.magic = MESSAGE_MAGIC;
    header.size = size;

    if (!write_all(fd_, &header, sizeof(header))) {
        return false;
    }

    // Send payload
    if (!write_all(fd_, data, size)) {
        return false;
    }

    return true;
}

bool NetworkClient::receive(std::vector<uint8_t>& buffer) {
    if (fd_ < 0) {
        std::cerr << "Not connected\n";
        return false;
    }

    // Receive header
    MessageHeader header;
    if (!read_all(fd_, &header, sizeof(header))) {
        return false;
    }

    // Validate magic
    if (header.magic != MESSAGE_MAGIC) {
        std::cerr << "Invalid message magic\n";
        return false;
    }

    // Receive payload
    buffer.resize(header.size);
    if (!read_all(fd_, buffer.data(), header.size)) {
        return false;
    }

    return true;
}

void NetworkClient::disconnect() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

} // namespace venus_plus
