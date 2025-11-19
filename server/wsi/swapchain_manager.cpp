#include "wsi/swapchain_manager.h"

#include "utils/logging.h"
#include <algorithm>

namespace venus_plus {

VkResult ServerSwapchainManager::create_swapchain(const VenusSwapchainCreateInfo& info,
                                                  uint32_t* actual_image_count) {
    std::lock_guard<std::mutex> lock(mutex_);
    ServerSwapchain swapchain = {};
    swapchain.id = info.swapchain_id;
    swapchain.width = info.width;
    swapchain.height = info.height;
    swapchain.format = static_cast<VkFormat>(info.format);
    swapchain.image_count = std::max(info.image_count, 1u);
    swapchain.image_storage.resize(swapchain.image_count);

    swapchains_[info.swapchain_id] = std::move(swapchain);

    if (actual_image_count) {
        *actual_image_count = info.image_count;
    }
    VP_LOG_STREAM_INFO(SERVER) << "[Swapchain] Created swapchain #" << info.swapchain_id
                               << " (" << info.width << "x" << info.height
                               << ", images=" << info.image_count << ")";
    return VK_SUCCESS;
}

void ServerSwapchainManager::destroy_swapchain(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    swapchains_.erase(id);
    VP_LOG_STREAM_INFO(SERVER) << "[Swapchain] Destroyed swapchain #" << id;
}

VkResult ServerSwapchainManager::acquire_image(uint32_t id, uint32_t* image_index) {
    if (!image_index) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = swapchains_.find(id);
    if (it == swapchains_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    uint32_t index = it->second.next_image % it->second.image_count;
    it->second.next_image = (index + 1) % it->second.image_count;
    *image_index = index;
    return VK_SUCCESS;
}

VkResult ServerSwapchainManager::present(uint32_t id,
                                         uint32_t image_index,
                                         VenusFrameHeader* header,
                                         std::vector<uint8_t>* payload) {
    if (!header || !payload) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = swapchains_.find(id);
    if (it == swapchains_.end()) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (image_index >= it->second.image_count) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    const size_t expected_bytes = static_cast<size_t>(it->second.width) *
                                  static_cast<size_t>(it->second.height) * 4u;
    if (it->second.image_storage[image_index].size() != expected_bytes) {
        it->second.image_storage[image_index] = generate_frame(it->second, image_index);
    }

    header->magic = kVenusFrameMagic;
    header->swapchain_id = id;
    header->image_index = image_index;
    header->width = it->second.width;
    header->height = it->second.height;
    header->format = static_cast<uint32_t>(it->second.format);
    std::vector<uint8_t> compressed;
    FrameCompressionType compression = FrameCompressionType::NONE;
    compress_frame(it->second.image_storage[image_index], &compressed, &compression);
    const std::vector<uint8_t>* send_buffer = &it->second.image_storage[image_index];
    if (compression != FrameCompressionType::NONE && !compressed.empty()) {
        send_buffer = &compressed;
    } else {
        compression = FrameCompressionType::NONE;
    }

    header->compression = compression;
    header->payload_size = static_cast<uint32_t>(send_buffer->size());
    header->uncompressed_size = static_cast<uint32_t>(it->second.image_storage[image_index].size());
    header->stride = it->second.width * 4u;

    *payload = *send_buffer;
    VP_LOG_STREAM_INFO(SERVER) << "[Swapchain] Present swapchain #" << id
                               << " image " << image_index;
    return VK_SUCCESS;
}

std::vector<uint8_t> ServerSwapchainManager::generate_frame(const ServerSwapchain& swapchain,
                                                            uint32_t image_index) const {
    const size_t bytes = static_cast<size_t>(swapchain.width) *
                         static_cast<size_t>(swapchain.height) * 4u;
    std::vector<uint8_t> frame(bytes);
    for (uint32_t y = 0; y < swapchain.height; ++y) {
        for (uint32_t x = 0; x < swapchain.width; ++x) {
            size_t offset = (static_cast<size_t>(y) * swapchain.width + x) * 4u;
            frame[offset + 0] = static_cast<uint8_t>((x * 255) / std::max(1u, swapchain.width));
            frame[offset + 1] = static_cast<uint8_t>((y * 255) / std::max(1u, swapchain.height));
            frame[offset + 2] = static_cast<uint8_t>((image_index * 50) % 255);
            frame[offset + 3] = 255;
        }
    }
    return frame;
}

void ServerSwapchainManager::compress_frame(const std::vector<uint8_t>& input,
                                            std::vector<uint8_t>* output,
                                            FrameCompressionType* mode) const {
    if (!output || !mode) {
        return;
    }
    output->clear();
    *mode = FrameCompressionType::NONE;
    if (input.empty()) {
        return;
    }

    std::vector<uint8_t> encoded;
    encoded.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        size_t run = 1;
        while ((i + run) < input.size() &&
               input[i + run] == input[i] &&
               run < 255) {
            ++run;
        }

        if (run >= 4) {
            encoded.push_back(1);
            encoded.push_back(static_cast<uint8_t>(run));
            encoded.push_back(input[i]);
            i += run;
        } else {
            size_t literal_len = std::min<size_t>(255, input.size() - i);
            encoded.push_back(0);
            encoded.push_back(static_cast<uint8_t>(literal_len));
            encoded.insert(encoded.end(), input.begin() + i, input.begin() + i + literal_len);
            i += literal_len;
        }
    }

    if (encoded.size() + 4 < input.size()) {
        *output = std::move(encoded);
        *mode = FrameCompressionType::RLE;
    }
}

} // namespace venus_plus
