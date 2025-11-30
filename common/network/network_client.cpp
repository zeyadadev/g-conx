#include "network_client.h"
#include "message.h"
#include "socket_utils.h"
#include "profiling.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <chrono>

#include "utils/logging.h"

#define NETWORK_LOG_ERROR() VP_LOG_STREAM_ERROR(NETWORK)
#define NETWORK_LOG_INFO() VP_LOG_STREAM_INFO(NETWORK)

namespace venus_plus {

namespace {

inline bool trace_net() {
    static const bool enabled = []() {
        const char* env = std::getenv("VENUS_TRACE_NET");
        return env && env[0] != '0';
    }();
    return enabled;
}

inline bool pipeline_enabled_env() {
    static const bool enabled = []() {
        const char* env = std::getenv("VENUS_PIPELINED_RECV");
        if (env && env[0] != '\0' && env[0] != '0') {
            return true;
        }
        env = std::getenv("VENUS_LATENCY_MODE");
        return env && env[0] != '0';
    }();
    return enabled;
}

inline int socket_buffer_bytes() {
    static const int buf = []() {
        const char* env = std::getenv("VENUS_SOCKET_BUFFER_BYTES");
        if (!env || env[0] == '\0') {
            return 4 * 1024 * 1024;
        }
        long parsed = std::strtol(env, nullptr, 10);
        if (parsed <= 0) {
            return 4 * 1024 * 1024;
        }
        return static_cast<int>(parsed);
    }();
    return buf;
}

struct NetStats {
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> total_us{0};
    std::atomic<uint64_t> total_bytes{0};
    std::atomic<uint64_t> max_us{0};
};

void record_net(NetStats& stats, uint64_t elapsed_us, uint64_t bytes, const char* tag) {
    const uint64_t count = stats.calls.fetch_add(1, std::memory_order_relaxed) + 1;
    stats.total_us.fetch_add(elapsed_us, std::memory_order_relaxed);
    stats.total_bytes.fetch_add(bytes, std::memory_order_relaxed);
    uint64_t prev_max = stats.max_us.load(std::memory_order_relaxed);
    while (elapsed_us > prev_max &&
           !stats.max_us.compare_exchange_weak(prev_max, elapsed_us, std::memory_order_relaxed)) {
    }
    if (count % 100 == 0) {
        const uint64_t total_us = stats.total_us.load(std::memory_order_relaxed);
        const uint64_t total_b = stats.total_bytes.load(std::memory_order_relaxed);
        const uint64_t max_seen = stats.max_us.load(std::memory_order_relaxed);
        const double avg_us = count ? static_cast<double>(total_us) / static_cast<double>(count) : 0.0;
        const double avg_b = count ? static_cast<double>(total_b) / static_cast<double>(count) : 0.0;
        NETWORK_LOG_INFO() << "[Net] " << tag << " summary: calls=" << count
                           << " avg_us=" << avg_us
                           << " avg_bytes=" << avg_b
                           << " max_us=" << max_seen;
    }
}

NetStats g_send_stats;
NetStats g_recv_stats;

inline void set_tcp_opts(int fd) {
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
#ifdef TCP_QUICKACK
    setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag));
#endif
    const int buf_size = socket_buffer_bytes();
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
}

} // namespace

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

    // Disable Nagle's algorithm for low latency and tune buffers/ACKs
    set_tcp_opts(fd_);

    NETWORK_LOG_INFO() << "Connected to " << host << ":" << port;
    pipeline_enabled_ = pipeline_enabled_env();
    stop_requested_.store(false, std::memory_order_relaxed);
    running_.store(true, std::memory_order_relaxed);
    recv_queue_.clear();
    if (pipeline_enabled_) {
        start_receive_thread();
    }
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

    const bool do_trace = trace_net();
    auto start = std::chrono::steady_clock::now();  // Always measure for profiling

    // Send header + payload with a single writev to minimize syscalls.
    struct iovec iov[2];
    iov[0].iov_base = &header;
    iov[0].iov_len = sizeof(header);
    iov[1].iov_base = const_cast<void*>(data);
    iov[1].iov_len = size;

    size_t remaining = sizeof(header) + size;
    while (remaining > 0) {
        ssize_t n = writev(fd_, iov, 2);
        if (n < 0) {
            NETWORK_LOG_ERROR() << "writev() error";
            return false;
        }
        remaining -= static_cast<size_t>(n);
        if (remaining == 0) {
            break;
        }
        size_t written = static_cast<size_t>(n);
        if (iov[0].iov_len > 0) {
            if (written >= iov[0].iov_len) {
                written -= iov[0].iov_len;
                iov[0].iov_len = 0;
                iov[0].iov_base = nullptr;
            } else {
                iov[0].iov_base = static_cast<uint8_t*>(iov[0].iov_base) + written;
                iov[0].iov_len -= written;
                written = 0;
            }
        }
        if (written > 0) {
            iov[1].iov_base = static_cast<uint8_t*>(iov[1].iov_base) + written;
            iov[1].iov_len -= written;
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const uint64_t elapsed_us =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());

    // Profile: track send bytes and RTT
    VENUS_PROFILE_SEND(sizeof(header) + size);
    VENUS_PROFILE_RTT_US(elapsed_us);

    if (do_trace) {
        record_net(g_send_stats, elapsed_us, sizeof(header) + size, "send");
    }

    return true;
}

bool NetworkClient::receive(std::vector<uint8_t>& buffer) {
    if (!pipeline_enabled_) {
        return receive_one(buffer);
    }

    std::unique_lock<std::mutex> lock(recv_mutex_);
    recv_cv_.wait(lock, [this]() {
        return !recv_queue_.empty() || !running_.load(std::memory_order_relaxed);
    });

    if (!recv_queue_.empty()) {
        buffer = std::move(recv_queue_.front());
        recv_queue_.pop_front();
        return true;
    }
    return false;
}

void NetworkClient::disconnect() {
    stop_receive_thread();
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    running_.store(false, std::memory_order_relaxed);
}

bool NetworkClient::receive_one(std::vector<uint8_t>& buffer) {
    if (fd_ < 0) {
        NETWORK_LOG_ERROR() << "Not connected";
        return false;
    }

    const bool do_trace = trace_net();
    auto start = do_trace ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};

    MessageHeader header;
    if (!read_all(fd_, &header, sizeof(header))) {
        return false;
    }

    if (header.magic != MESSAGE_MAGIC) {
        NETWORK_LOG_ERROR() << "Invalid message magic";
        return false;
    }

    buffer.resize(header.size);
    if (!read_all(fd_, buffer.data(), header.size)) {
        return false;
    }

    // Profile: track receive bytes
    VENUS_PROFILE_RECEIVE(sizeof(header) + header.size);

    if (do_trace) {
        const auto end = std::chrono::steady_clock::now();
        const uint64_t elapsed_us =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());
        record_net(g_recv_stats, elapsed_us, sizeof(header) + header.size, "recv");
    }

    return true;
}

void NetworkClient::start_receive_thread() {
    if (recv_thread_.joinable()) {
        return;
    }
    recv_thread_ = std::thread([this]() {
        while (!stop_requested_.load(std::memory_order_relaxed)) {
            std::vector<uint8_t> buffer;
            if (!receive_one(buffer)) {
                running_.store(false, std::memory_order_relaxed);
                break;
            }
            {
                std::lock_guard<std::mutex> lock(recv_mutex_);
                recv_queue_.push_back(std::move(buffer));
            }
            recv_cv_.notify_one();
        }
        recv_cv_.notify_all();
    });
}

void NetworkClient::stop_receive_thread() {
    stop_requested_.store(true, std::memory_order_relaxed);
    running_.store(false, std::memory_order_relaxed);
    recv_cv_.notify_all();
    if (fd_ >= 0) {
        shutdown(fd_, SHUT_RDWR);
    }
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(recv_mutex_);
        recv_queue_.clear();
    }
}

} // namespace venus_plus
