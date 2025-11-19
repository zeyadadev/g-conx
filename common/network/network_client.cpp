#include "network_client.h"
#include "message.h"
#include "socket_utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

#include "utils/logging.h"

#define NETWORK_LOG_ERROR() VP_LOG_STREAM_ERROR(NETWORK)
#define NETWORK_LOG_INFO() VP_LOG_STREAM_INFO(NETWORK)

namespace venus_plus {

NetworkClient::NetworkClient() : fd_(-1) {}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const std::string& host, uint16_t port) {
    // Create socket
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        NETWORK_LOG_ERROR() << "Failed to create socket";
        return false;
    }

    // Setup server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        NETWORK_LOG_ERROR() << "Invalid address: " << host;
        close(fd_);
        fd_ = -1;
        return false;
    }

    // Connect
    if (::connect(fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        NETWORK_LOG_ERROR() << "Connection failed to " << host << ":" << port;
        close(fd_);
        fd_ = -1;
        return false;
    }

    // Disable Nagle's algorithm for low latency
    int flag = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    NETWORK_LOG_INFO() << "Connected to " << host << ":" << port;
    return true;
}

bool NetworkClient::send(const void* data, size_t size) {
    if (fd_ < 0) {
        NETWORK_LOG_ERROR() << "Not connected";
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
        NETWORK_LOG_ERROR() << "Not connected";
        return false;
    }

    // Receive header
    MessageHeader header;
    if (!read_all(fd_, &header, sizeof(header))) {
        return false;
    }

    // Validate magic
    if (header.magic != MESSAGE_MAGIC) {
        NETWORK_LOG_ERROR() << "Invalid message magic";
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
