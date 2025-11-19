#include "swapchain_state.h"

#include "state/handle_allocator.h"

namespace venus_plus {

SwapchainState g_swapchain_state;

SwapchainState::SwapchainState()
    : next_id_(1) {}

uint32_t SwapchainState::allocate_swapchain_id() {
    std::lock_guard<std::mutex> lock(mutex_);
    return next_id_++;
}

VkSwapchainKHR SwapchainState::add_swapchain(VkDevice device,
                                             uint32_t swapchain_id,
                                             const VkSwapchainCreateInfoKHR& create_info,
                                             uint32_t image_count,
                                             std::vector<VkImage>&& images,
                                             std::shared_ptr<PlatformWSI> wsi) {
    std::lock_guard<std::mutex> lock(mutex_);
    SwapchainInfo info = {};
    info.device = device;
    info.swapchain_id = swapchain_id;
    info.width = create_info.imageExtent.width;
    info.height = create_info.imageExtent.height;
    info.format = create_info.imageFormat;
    info.image_count = image_count;
    info.images = std::move(images);
    info.last_acquired = 0;
    info.wsi = std::move(wsi);

    VkSwapchainKHR handle = g_handle_allocator.allocate<VkSwapchainKHR>();
    swapchains_[handle_key(handle)] = std::move(info);
    return handle;
}

bool SwapchainState::remove_swapchain(VkSwapchainKHR swapchain, SwapchainInfo* out_info) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = swapchains_.find(handle_key(swapchain));
    if (it == swapchains_.end()) {
        return false;
    }
    if (out_info) {
        *out_info = it->second;
    }
    swapchains_.erase(it);
    return true;
}

bool SwapchainState::get_images(VkSwapchainKHR swapchain, std::vector<VkImage>* out_images) const {
    if (!out_images) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = swapchains_.find(handle_key(swapchain));
    if (it == swapchains_.end()) {
        return false;
    }
    *out_images = it->second.images;
    return true;
}

bool SwapchainState::acquire_image(VkSwapchainKHR swapchain, uint32_t* image_index) {
    if (!image_index) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = swapchains_.find(handle_key(swapchain));
    if (it == swapchains_.end()) {
        return false;
    }
    uint32_t index = it->second.last_acquired % it->second.image_count;
    it->second.last_acquired = index + 1;
    *image_index = index;
    return true;
}

bool SwapchainState::get_info(VkSwapchainKHR swapchain, SwapchainInfo* out_info) const {
    if (!out_info) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = swapchains_.find(handle_key(swapchain));
    if (it == swapchains_.end()) {
        return false;
    }
    *out_info = it->second;
    return true;
}

std::shared_ptr<PlatformWSI> SwapchainState::get_wsi(VkSwapchainKHR swapchain) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = swapchains_.find(handle_key(swapchain));
    if (it == swapchains_.end()) {
        return nullptr;
    }
    return it->second.wsi;
}

uint32_t SwapchainState::get_remote_id(VkSwapchainKHR swapchain) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = swapchains_.find(handle_key(swapchain));
    if (it == swapchains_.end()) {
        return 0;
    }
    return it->second.swapchain_id;
}

void SwapchainState::remove_device_swapchains(VkDevice device, std::vector<SwapchainInfo>* removed) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = swapchains_.begin(); it != swapchains_.end();) {
        if (it->second.device == device) {
            if (removed) {
                removed->push_back(it->second);
            }
            it = swapchains_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace venus_plus
