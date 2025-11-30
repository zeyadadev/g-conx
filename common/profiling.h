#ifndef VENUS_PLUS_PROFILING_H
#define VENUS_PLUS_PROFILING_H

#include <atomic>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <cstdint>

namespace venus_plus {

/**
 * Performance profiling system for Venus Plus
 * Tracks Vulkan operations and network activity to diagnose performance issues
 */
class VenusProfiler {
public:
    static VenusProfiler& instance() {
        static VenusProfiler inst;
        return inst;
    }

    // Vulkan operation tracking
    void record_queue_submit() {
        queue_submit_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void record_wait_fences() {
        wait_fences_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void record_map_memory() {
        map_memory_count_.fetch_add(1, std::memory_order_relaxed);
    }

    // Network operation tracking
    void record_send(size_t bytes) {
        send_count_.fetch_add(1, std::memory_order_relaxed);
        send_bytes_.fetch_add(bytes, std::memory_order_relaxed);
    }

    void record_receive(size_t bytes) {
        recv_count_.fetch_add(1, std::memory_order_relaxed);
        recv_bytes_.fetch_add(bytes, std::memory_order_relaxed);
    }

    // Time tracking for round-trips
    void record_rtt_us(uint64_t microseconds) {
        rtt_count_.fetch_add(1, std::memory_order_relaxed);
        total_rtt_us_.fetch_add(microseconds, std::memory_order_relaxed);

        // Track max RTT
        uint64_t current_max = max_rtt_us_.load(std::memory_order_relaxed);
        while (microseconds > current_max &&
               !max_rtt_us_.compare_exchange_weak(current_max, microseconds,
                                                   std::memory_order_relaxed)) {
            // Retry if another thread updated max
        }
    }

    // Token generation tracking (for inference workloads)
    void start_inference() {
        if (!inference_started_) {
            inference_start_ = std::chrono::steady_clock::now();
            inference_started_ = true;
        }
    }

    void record_token_generated() {
        tokens_generated_.fetch_add(1, std::memory_order_relaxed);
    }

    // Print comprehensive summary
    void print_summary() const {
        if (!inference_started_) {
            return;  // No data collected
        }

        auto now = std::chrono::steady_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - inference_start_).count();
        double duration_sec = duration_ms / 1000.0;

        // Load all counters
        uint64_t submits = queue_submit_count_.load(std::memory_order_relaxed);
        uint64_t waits = wait_fences_count_.load(std::memory_order_relaxed);
        uint64_t maps = map_memory_count_.load(std::memory_order_relaxed);
        uint64_t sends = send_count_.load(std::memory_order_relaxed);
        uint64_t recvs = recv_count_.load(std::memory_order_relaxed);
        uint64_t send_bytes = send_bytes_.load(std::memory_order_relaxed);
        uint64_t recv_bytes = recv_bytes_.load(std::memory_order_relaxed);
        uint64_t tokens = tokens_generated_.load(std::memory_order_relaxed);
        uint64_t rtt_count = rtt_count_.load(std::memory_order_relaxed);
        uint64_t total_rtt = total_rtt_us_.load(std::memory_order_relaxed);
        uint64_t max_rtt = max_rtt_us_.load(std::memory_order_relaxed);

        // Calculate derived metrics
        double tokens_per_sec = (tokens > 0 && duration_sec > 0) ? tokens / duration_sec : 0.0;
        double ms_per_token = (tokens > 0) ? duration_ms / (double)tokens : 0.0;

        double submits_per_token = (tokens > 0) ? (double)submits / tokens : 0.0;
        double waits_per_token = (tokens > 0) ? (double)waits / tokens : 0.0;
        double maps_per_token = (tokens > 0) ? (double)maps / tokens : 0.0;
        double sends_per_token = (tokens > 0) ? (double)sends / tokens : 0.0;
        double recvs_per_token = (tokens > 0) ? (double)recvs / tokens : 0.0;

        double avg_rtt_ms = (rtt_count > 0) ? (total_rtt / (double)rtt_count) / 1000.0 : 0.0;
        double max_rtt_ms = max_rtt / 1000.0;

        double send_mb = send_bytes / (1024.0 * 1024.0);
        double recv_mb = recv_bytes / (1024.0 * 1024.0);
        double send_mb_per_token = (tokens > 0) ? send_mb / tokens : 0.0;
        double recv_mb_per_token = (tokens > 0) ? recv_mb / tokens : 0.0;

        // Calculate estimated network overhead
        double network_overhead_ms = 0.0;
        if (tokens > 0 && avg_rtt_ms > 0) {
            network_overhead_ms = sends_per_token * avg_rtt_ms;
        }

        // Print header
        std::cout << "\n";
        std::cout << "================================================================\n";
        std::cout << "               Venus Plus Performance Summary\n";
        std::cout << "================================================================\n";
        std::cout << std::fixed << std::setprecision(2);

        // Overall performance
        std::cout << "\nOverall Performance:\n";
        std::cout << "  Duration:             " << std::setw(10) << duration_sec << " s\n";
        std::cout << "  Tokens generated:     " << std::setw(10) << tokens << " tokens\n";
        std::cout << "  Throughput:           " << std::setw(10) << tokens_per_sec << " tokens/sec\n";
        std::cout << "  Time per token:       " << std::setw(10) << ms_per_token << " ms\n";

        // Vulkan operations
        std::cout << "\nVulkan Operations:\n";
        std::cout << "  vkQueueSubmit:        " << std::setw(10) << submits
                  << " calls  (" << std::setw(6) << submits_per_token << " per token)\n";
        std::cout << "  vkWaitForFences:      " << std::setw(10) << waits
                  << " calls  (" << std::setw(6) << waits_per_token << " per token)\n";
        std::cout << "  vkMapMemory:          " << std::setw(10) << maps
                  << " calls  (" << std::setw(6) << maps_per_token << " per token)\n";

        // Network operations
        std::cout << "\nNetwork Operations:\n";
        std::cout << "  Sends:                " << std::setw(10) << sends
                  << " calls  (" << std::setw(6) << sends_per_token << " per token)\n";
        std::cout << "  Receives:             " << std::setw(10) << recvs
                  << " calls  (" << std::setw(6) << recvs_per_token << " per token)\n";
        std::cout << "  Data sent:            " << std::setw(10) << send_mb << " MB"
                  << "    (" << std::setw(6) << send_mb_per_token << " MB per token)\n";
        std::cout << "  Data received:        " << std::setw(10) << recv_mb << " MB"
                  << "    (" << std::setw(6) << recv_mb_per_token << " MB per token)\n";

        // Network latency
        if (rtt_count > 0) {
            std::cout << "\nNetwork Latency:\n";
            std::cout << "  Round-trips:          " << std::setw(10) << rtt_count << " measured\n";
            std::cout << "  Average RTT:          " << std::setw(10) << avg_rtt_ms << " ms\n";
            std::cout << "  Max RTT:              " << std::setw(10) << max_rtt_ms << " ms\n";
            std::cout << "  Est. network overhead:" << std::setw(10) << network_overhead_ms
                      << " ms per token\n";
        }

        // Analysis and recommendations
        std::cout << "\nAnalysis:\n";

        bool has_issues = false;

        if (submits_per_token > 50) {
            std::cout << "  ⚠ HIGH SUBMIT COUNT (" << submits_per_token << " per token)\n";
            std::cout << "     → Operations are not batched efficiently\n";
            std::cout << "     → Recommendation: Batch multiple operations into single vkQueueSubmit\n";
            has_issues = true;
        } else if (submits_per_token > 10) {
            std::cout << "  ⚠ MODERATE SUBMIT COUNT (" << submits_per_token << " per token)\n";
            std::cout << "     → Some batching opportunity exists\n";
            has_issues = true;
        }

        if (waits_per_token > 50) {
            std::cout << "  ⚠ HIGH WAIT COUNT (" << waits_per_token << " per token)\n";
            std::cout << "     → Poor pipelining, waiting after every submit\n";
            std::cout << "     → Recommendation: Pipeline operations, reduce synchronous waits\n";
            has_issues = true;
        }

        if (std::abs(waits_per_token - submits_per_token) < 1.0 && submits_per_token > 5) {
            std::cout << "  ⚠ SYNCHRONOUS EXECUTION (waits ≈ submits)\n";
            std::cout << "     → Every submit immediately followed by wait\n";
            std::cout << "     → Recommendation: Use async execution with fences\n";
            has_issues = true;
        }

        if (avg_rtt_ms > 5.0) {
            std::cout << "  ⚠ HIGH NETWORK LATENCY (" << avg_rtt_ms << " ms average RTT)\n";
            std::cout << "     → Network is slow (WiFi, WAN, or congested)\n";
            std::cout << "     → Recommendation: Use wired connection or optimize for high latency\n";
            has_issues = true;
        }

        if (max_rtt_ms > avg_rtt_ms * 5 && avg_rtt_ms > 0) {
            std::cout << "  ⚠ HIGH LATENCY SPIKES (max " << max_rtt_ms << " ms)\n";
            std::cout << "     → Network has jitter/packet loss\n";
            std::cout << "     → Recommendation: Check WiFi interference or switch to QUIC\n";
            has_issues = true;
        }

        if (network_overhead_ms > ms_per_token * 0.5 && ms_per_token > 0) {
            std::cout << "  ⚠ NETWORK IS DOMINANT BOTTLENECK\n";
            std::cout << "     → Network overhead (" << network_overhead_ms
                      << " ms) is " << (int)((network_overhead_ms / ms_per_token) * 100)
                      << "% of total time\n";
            std::cout << "     → Recommendation: Reduce round-trips via batching\n";
            has_issues = true;
        }

        if (!has_issues) {
            std::cout << "  ✓ No major issues detected\n";
            std::cout << "     → Performance looks reasonable\n";
            if (submits_per_token < 5 && avg_rtt_ms < 2.0 && tokens_per_sec < 50) {
                std::cout << "     → Bottleneck is likely GPU compute or protocol overhead\n";
            }
        }

        std::cout << "================================================================\n\n";
    }

    // Print periodic updates (call every N seconds)
    void maybe_print_periodic(int interval_seconds = 10) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_periodic_print_).count();

        if (elapsed >= interval_seconds) {
            print_summary();
            last_periodic_print_ = now;
        }
    }

    void reset() {
        queue_submit_count_.store(0, std::memory_order_relaxed);
        wait_fences_count_.store(0, std::memory_order_relaxed);
        map_memory_count_.store(0, std::memory_order_relaxed);
        send_count_.store(0, std::memory_order_relaxed);
        recv_count_.store(0, std::memory_order_relaxed);
        send_bytes_.store(0, std::memory_order_relaxed);
        recv_bytes_.store(0, std::memory_order_relaxed);
        tokens_generated_.store(0, std::memory_order_relaxed);
        rtt_count_.store(0, std::memory_order_relaxed);
        total_rtt_us_.store(0, std::memory_order_relaxed);
        max_rtt_us_.store(0, std::memory_order_relaxed);
        inference_start_ = std::chrono::steady_clock::now();
        last_periodic_print_ = inference_start_;
    }

private:
    VenusProfiler() {
        inference_start_ = std::chrono::steady_clock::now();
        last_periodic_print_ = inference_start_;
    }

    // Vulkan operation counters
    std::atomic<uint64_t> queue_submit_count_{0};
    std::atomic<uint64_t> wait_fences_count_{0};
    std::atomic<uint64_t> map_memory_count_{0};

    // Network operation counters
    std::atomic<uint64_t> send_count_{0};
    std::atomic<uint64_t> recv_count_{0};
    std::atomic<uint64_t> send_bytes_{0};
    std::atomic<uint64_t> recv_bytes_{0};

    // Network latency tracking
    std::atomic<uint64_t> rtt_count_{0};
    std::atomic<uint64_t> total_rtt_us_{0};
    std::atomic<uint64_t> max_rtt_us_{0};

    // Token/inference tracking
    std::atomic<uint64_t> tokens_generated_{0};

    // Timing
    std::chrono::steady_clock::time_point inference_start_;
    std::chrono::steady_clock::time_point last_periodic_print_;
    bool inference_started_{false};
};

// Convenience macros (can be compiled out if needed)
#ifndef VENUS_PROFILING_ENABLED
#define VENUS_PROFILING_ENABLED 1
#endif

#if VENUS_PROFILING_ENABLED

#define VENUS_PROFILE_QUEUE_SUBMIT() \
    venus_plus::VenusProfiler::instance().record_queue_submit()

#define VENUS_PROFILE_WAIT_FENCES() \
    venus_plus::VenusProfiler::instance().record_wait_fences()

#define VENUS_PROFILE_MAP_MEMORY() \
    venus_plus::VenusProfiler::instance().record_map_memory()

#define VENUS_PROFILE_SEND(bytes) \
    venus_plus::VenusProfiler::instance().record_send(bytes)

#define VENUS_PROFILE_RECEIVE(bytes) \
    venus_plus::VenusProfiler::instance().record_receive(bytes)

#define VENUS_PROFILE_RTT_US(microseconds) \
    venus_plus::VenusProfiler::instance().record_rtt_us(microseconds)

#define VENUS_PROFILE_TOKEN() \
    venus_plus::VenusProfiler::instance().record_token_generated()

#define VENUS_PROFILE_START() \
    venus_plus::VenusProfiler::instance().start_inference()

#define VENUS_PROFILE_PRINT() \
    venus_plus::VenusProfiler::instance().print_summary()

#define VENUS_PROFILE_PERIODIC(interval_sec) \
    venus_plus::VenusProfiler::instance().maybe_print_periodic(interval_sec)

#define VENUS_PROFILE_RESET() \
    venus_plus::VenusProfiler::instance().reset()

#else

// No-op macros when profiling disabled
#define VENUS_PROFILE_QUEUE_SUBMIT()
#define VENUS_PROFILE_WAIT_FENCES()
#define VENUS_PROFILE_MAP_MEMORY()
#define VENUS_PROFILE_SEND(bytes)
#define VENUS_PROFILE_RECEIVE(bytes)
#define VENUS_PROFILE_RTT_US(microseconds)
#define VENUS_PROFILE_TOKEN()
#define VENUS_PROFILE_START()
#define VENUS_PROFILE_PRINT()
#define VENUS_PROFILE_PERIODIC(interval_sec)
#define VENUS_PROFILE_RESET()

#endif // VENUS_PROFILING_ENABLED

} // namespace venus_plus

#endif // VENUS_PLUS_PROFILING_H
