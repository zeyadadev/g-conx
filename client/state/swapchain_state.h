#ifndef VENUS_PLUS_SWAPCHAIN_STATE_H
#define VENUS_PLUS_SWAPCHAIN_STATE_H

#include <vulkan/vulkan.h>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <memory>

#include "wsi/platform_wsi.h"

namespace venus_plus {

struct SwapchainInfo {
    VkDevice device = VK_NULL_HANDLE;
    uint32_t swapchain_id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t image_count = 0;
    std::vector<VkImage> images;
    uint32_t last_acquired = 0;
    std::shared_ptr<PlatformWSI> wsi;
};

class SwapchainState {
public:
    SwapchainState();

    uint32_t allocate_swapchain_id();

    VkSwapchainKHR add_swapchain(VkDevice device,
                                 uint32_t swapchain_id,
                                 const VkSwapchainCreateInfoKHR& create_info,
                                 uint32_t image_count,
                                 std::vector<VkImage>&& images,
                                 std::shared_ptr<PlatformWSI> wsi);

    bool remove_swapchain(VkSwapchainKHR swapchain, SwapchainInfo* out_info);
    bool get_images(VkSwapchainKHR swapchain, std::vector<VkImage>* out_images) const;
    bool acquire_image(VkSwapchainKHR swapchain, uint32_t* image_index);
    bool get_info(VkSwapchainKHR swapchain, SwapchainInfo* out_info) const;
    std::shared_ptr<PlatformWSI> get_wsi(VkSwapchainKHR swapchain) const;
    uint32_t get_remote_id(VkSwapchainKHR swapchain) const;
    void remove_device_swapchains(VkDevice device, std::vector<SwapchainInfo>* removed);

private:
    template <typename T>
    static uint64_t handle_key(T handle) {
        return reinterpret_cast<uint64_t>(handle);
    }

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, SwapchainInfo> swapchains_;
    uint32_t next_id_;
};

extern SwapchainState g_swapchain_state;

} // namespace venus_plus

#endif // VENUS_PLUS_SWAPCHAIN_STATE_H
