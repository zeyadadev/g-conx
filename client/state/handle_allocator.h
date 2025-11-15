#ifndef VENUS_PLUS_HANDLE_ALLOCATOR_H
#define VENUS_PLUS_HANDLE_ALLOCATOR_H

#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>

namespace venus_plus {

// Thread-safe handle allocator for client-side Vulkan handles
// Uses a monotonically increasing atomic counter
class HandleAllocator {
public:
    HandleAllocator() : counter_(1) {}  // Start from 1 (VK_NULL_HANDLE is 0)

    // Allocate a new handle of type T
    template<typename T>
    T allocate() {
        uint64_t value = counter_.fetch_add(1, std::memory_order_relaxed);
        return reinterpret_cast<T>(value);
    }

    // Check if a handle is valid (non-zero)
    template<typename T>
    static bool is_valid(T handle) {
        return handle != VK_NULL_HANDLE;
    }

private:
    std::atomic<uint64_t> counter_;
};

// Global handle allocator instance
extern HandleAllocator g_handle_allocator;

} // namespace venus_plus

#endif // VENUS_PLUS_HANDLE_ALLOCATOR_H
