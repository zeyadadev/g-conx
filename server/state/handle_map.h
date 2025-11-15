#ifndef VENUS_PLUS_HANDLE_MAP_H
#define VENUS_PLUS_HANDLE_MAP_H

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace venus_plus {

// Thread-safe bidirectional handle mapping
// Maps client handles to server handles (which are fake for Phase 2-6)
template<typename T>
class HandleMap {
public:
    // Insert a mapping
    void insert(T client_handle, T server_handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t client_key = handle_to_uint64(client_handle);
        uint64_t server_value = handle_to_uint64(server_handle);
        map_[client_key] = server_value;
    }

    // Lookup server handle from client handle
    T lookup(T client_handle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t client_key = handle_to_uint64(client_handle);
        auto it = map_.find(client_key);
        if (it == map_.end()) {
            return VK_NULL_HANDLE;
        }
        return uint64_to_handle<T>(it->second);
    }

    // Check if mapping exists
    bool exists(T client_handle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t client_key = handle_to_uint64(client_handle);
        return map_.find(client_key) != map_.end();
    }

    // Remove mapping
    void remove(T client_handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t client_key = handle_to_uint64(client_handle);
        map_.erase(client_key);
    }

    // Clear all mappings
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        map_.clear();
    }

    // Get count of mappings
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return map_.size();
    }

private:
    // Convert handle to uint64_t for storage
    template<typename U>
    static uint64_t handle_to_uint64(U handle) {
        return reinterpret_cast<uint64_t>(handle);
    }

    // Convert uint64_t back to handle
    template<typename U>
    static U uint64_to_handle(uint64_t value) {
        return reinterpret_cast<U>(value);
    }

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, uint64_t> map_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_HANDLE_MAP_H
