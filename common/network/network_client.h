#ifndef VENUS_PLUS_NETWORK_CLIENT_H
#define VENUS_PLUS_NETWORK_CLIENT_H

#include <string>
#include <vector>
#include <cstdint>

namespace venus_plus {

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    // Connect to server
    bool connect(const std::string& host, uint16_t port);

    // Send message
    bool send(const void* data, size_t size);

    // Receive message
    bool receive(std::vector<uint8_t>& buffer);

    // Disconnect
    void disconnect();

    // Check if connected
    bool is_connected() const { return fd_ >= 0; }

private:
    int fd_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_NETWORK_CLIENT_H
