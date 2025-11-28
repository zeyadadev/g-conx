#ifndef VENUS_PLUS_NETWORK_CLIENT_H
#define VENUS_PLUS_NETWORK_CLIENT_H

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

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
    bool receive_one(std::vector<uint8_t>& buffer);
    void start_receive_thread();
    void stop_receive_thread();

    int fd_;
    bool pipeline_enabled_ = false;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::thread recv_thread_;
    std::mutex recv_mutex_;
    std::condition_variable recv_cv_;
    std::deque<std::vector<uint8_t>> recv_queue_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_NETWORK_CLIENT_H
