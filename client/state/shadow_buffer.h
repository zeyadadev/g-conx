#ifndef VENUS_PLUS_SHADOW_BUFFER_H
#define VENUS_PLUS_SHADOW_BUFFER_H

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace venus_plus {

struct ShadowBufferMapping {
    VkDevice device = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* data = nullptr;
    bool host_coherent = false;
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
                        void** out_ptr);

    bool remove_mapping(VkDeviceMemory memory, ShadowBufferMapping* out_mapping);
    bool get_mapping(VkDeviceMemory memory, ShadowBufferMapping* out_mapping) const;
    bool is_mapped(VkDeviceMemory memory) const;
    void remove_device(VkDevice device);

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
