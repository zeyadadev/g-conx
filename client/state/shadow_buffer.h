#ifndef VENUS_PLUS_SHADOW_BUFFER_H
#define VENUS_PLUS_SHADOW_BUFFER_H

#include <atomic>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace venus_plus {

struct ShadowBufferMapping {
    VkDevice device = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* data = nullptr;
    size_t alloc_size = 0;
    bool host_coherent = false;
    bool invalidate_on_wait = false;
    struct HostCoherentTracking* tracking = nullptr;
};

struct HostCoherentTracking {
    void* base = nullptr;
    size_t size = 0;
    size_t alloc_size = 0;
    size_t page_size = 0;
    size_t page_count = 0;
    std::unique_ptr<std::atomic<uint8_t>[]> dirty;
    std::unique_ptr<std::atomic<uint8_t>[]> writable;
    void* fault_node = nullptr;
};

struct ShadowCoherentRange {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* data = nullptr;
    struct HostCoherentTracking* tracking = nullptr;
    size_t first_page = 0;
    size_t page_count = 0;
};

class ShadowBufferManager {
public:
    ShadowBufferManager();
    ~ShadowBufferManager();

    bool create_mapping(VkDevice device,
                        VkDeviceMemory memory,
                        VkDeviceSize offset,
                        VkDeviceSize size,
                        bool host_coherent,
                        bool invalidate_on_wait,
                        void** out_ptr);

    bool remove_mapping(VkDeviceMemory memory, ShadowBufferMapping* out_mapping);
    bool get_mapping(VkDeviceMemory memory, ShadowBufferMapping* out_mapping) const;
    bool is_mapped(VkDeviceMemory memory) const;
    void remove_device(VkDevice device);
    void collect_dirty_coherent_ranges(VkDevice device, std::vector<ShadowCoherentRange>* out_ranges) const;
    void collect_host_coherent_ranges(VkDevice device, std::vector<ShadowCoherentRange>* out_ranges) const;
    void prepare_coherent_range_flush(const ShadowCoherentRange& range) const;
    void finalize_coherent_range_flush(const ShadowCoherentRange& range) const;
    void reset_host_coherent_mapping(VkDeviceMemory memory);
    bool range_has_dirty_pages(const ShadowCoherentRange& range) const;
    void prepare_coherent_range_invalidate(const ShadowCoherentRange& range) const;
    void finalize_coherent_range_invalidate(const ShadowCoherentRange& range) const;
    void free_mapping_resources(ShadowBufferMapping* mapping) const;

private:
    template <typename T>
    static uint64_t handle_key(T handle) {
        return reinterpret_cast<uint64_t>(handle);
    }

    void free_all_locked();

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, ShadowBufferMapping> mappings_;
};

extern ShadowBufferManager g_shadow_buffer_manager;

} // namespace venus_plus

#endif // VENUS_PLUS_SHADOW_BUFFER_H
