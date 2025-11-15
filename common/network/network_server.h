#ifndef VENUS_PLUS_NETWORK_SERVER_H
#define VENUS_PLUS_NETWORK_SERVER_H

#include <cstdint>
#include <string>
#include <functional>
#include <vector>

namespace venus_plus {

// Callback for handling client messages
// Parameters: client_fd, message_data, message_size
// Returns: true to continue, false to disconnect client
using ClientHandler = std::function<bool(int, const void*, size_t)>;

class NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();

    // Start server
    bool start(uint16_t port, const std::string& bind_addr = "0.0.0.0");

    // Run server (blocks until stopped)
    void run(ClientHandler handler);

    // Stop server
    void stop();

    // Send message to client
    static bool send_to_client(int client_fd, const void* data, size_t size);

private:
    void handle_client(int client_fd, ClientHandler handler);

    int server_fd_;
    bool running_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_NETWORK_SERVER_H
