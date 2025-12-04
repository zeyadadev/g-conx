#include "wsi/swapchain_manager.h"

#include "utils/logging.h"
#include <algorithm>
#include <cstring>

#define SERVER_LOG_ERROR() VP_LOG_STREAM_ERROR(SERVER)
#define SERVER_LOG_INFO() VP_LOG_STREAM_INFO(SERVER)

namespace {

VkExtent3D make_extent(uint32_t width, uint32_t height) {
    VkExtent3D extent = {};
    extent.width = width;
    extent.height = height;
    extent.depth = 1;
    return extent;
}

}

namespace venus_plus {

ServerSwapchainManager::ServerSwapchainManager(ServerState* state)
    : state_(state) {}

VkResult ServerSwapchainManager::create_swapchain(const VenusSwapchainCreateInfo& info,
                                                  VenusSwapchainCreateReply* reply) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDevice client_device = reinterpret_cast<VkDevice>(info.device_handle);
    VkDevice device = server_state_get_real_device(state_, client_device);
    if (device == VK_NULL_HANDLE) {
        SERVER_LOG_ERROR() << "[Swapchain] Unknown device handle for swapchain creation";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ServerSwapchain swapchain = {};
    swapchain.id = info.swapchain_id;
    swapchain.width = info.width;
    swapchain.height = info.height;
    swapchain.format = static_cast<VkFormat>(info.format);
    swapchain.image_count = std::max(info.image_count, 1u);
    swapchain.device = device;
    swapchain.client_device = client_device;
    swapchain.queue_family_index = 0;

    vkGetDeviceQueue(device, swapchain.queue_family_index, 0, &swapchain.queue);
    if (swapchain.queue == VK_NULL_HANDLE) {
        SERVER_LOG_ERROR() << "[Swapchain] Failed to acquire device queue";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = swapchain.queue_family_index;
    if (vkCreateCommandPool(device, &pool_info, nullptr, &swapchain.command_pool) != VK_SUCCESS) {
        SERVER_LOG_ERROR() << "[Swapchain] Failed to create command pool";
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    VkCommandBufferAllocateInfo cmd_info = {};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_info.commandPool = swapchain.command_pool;
    cmd_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_info.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &cmd_info, &swapchain.command_buffer) != VK_SUCCESS) {
        SERVER_LOG_ERROR() << "[Swapchain] Failed to allocate command buffer";
        vkDestroyCommandPool(device, swapchain.command_pool, nullptr);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &fence_info, nullptr, &swapchain.copy_fence) != VK_SUCCESS) {
        SERVER_LOG_ERROR() << "[Swapchain] Failed to create fence";
        vkDestroyCommandPool(device, swapchain.command_pool, nullptr);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    swapchain.images.resize(swapchain.image_count);

    if (!reply) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    reply->result = VK_SUCCESS;
    reply->actual_image_count = swapchain.image_count;

    if (!allocate_resources(swapchain, info, reply)) {
        free_resources(swapchain);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    swapchains_[info.swapchain_id] = std::move(swapchain);
    SERVER_LOG_INFO() << "[Swapchain] Created swapchain #" << info.swapchain_id
                      << " (" << info.width << "x" << info.height
                      << ", images=" << info.image_count << ")";

    // The caller expects to have the reply serialized and sent immediately.
    // However, main.cpp already sends reply after calling this method, so nothing else needed here.
    return VK_SUCCESS;
}

void ServerSwapchainManager::destroy_swapchain(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = swapchains_.find(id);
    if (it != swapchains_.end()) {
        free_resources(it->second);
        swapchains_.erase(it);
        SERVER_LOG_INFO() << "[Swapchain] Destroyed swapchain #" << id;
    }
}

void ServerSwapchainManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : swapchains_) {
        free_resources(entry.second);
    }
    swapchains_.clear();
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

    auto& swapchain = it->second;
    auto& image = swapchain.images[image_index];

    if (!image.image || !image.staging_buffer) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    vkDeviceWaitIdle(swapchain.device);

    vkResetCommandPool(swapchain.device, swapchain.command_pool, 0);

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(swapchain.command_buffer, &begin_info);

    VkImageSubresourceRange range = {};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    VkImageMemoryBarrier pre_copy = {};
    pre_copy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    pre_copy.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    pre_copy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    pre_copy.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    pre_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    pre_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    pre_copy.image = image.image;
    pre_copy.subresourceRange = range;

    vkCmdPipelineBarrier(swapchain.command_buffer,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &pre_copy);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = make_extent(swapchain.width, swapchain.height);

    vkCmdCopyImageToBuffer(swapchain.command_buffer,
                           image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image.staging_buffer,
                           1,
                           &region);

    VkImageMemoryBarrier post_copy = {};
    post_copy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    post_copy.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    post_copy.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    post_copy.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    post_copy.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    post_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    post_copy.image = image.image;
    post_copy.subresourceRange = range;

    vkCmdPipelineBarrier(swapchain.command_buffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &post_copy);

    vkEndCommandBuffer(swapchain.command_buffer);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &swapchain.command_buffer;

    vkQueueSubmit(swapchain.queue, 1, &submit_info, swapchain.copy_fence);
    vkWaitForFences(swapchain.device, 1, &swapchain.copy_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(swapchain.device, 1, &swapchain.copy_fence);

    std::vector<uint8_t> frame(static_cast<size_t>(image.staging_size));
    std::memcpy(frame.data(), image.staging_ptr, static_cast<size_t>(image.staging_size));

    header->magic = kVenusFrameMagic;
    header->swapchain_id = id;
    header->image_index = image_index;
    header->width = swapchain.width;
    header->height = swapchain.height;
    header->format = static_cast<uint32_t>(swapchain.format);
    header->stride = swapchain.width * 4u;

    FrameCompressionType compression = FrameCompressionType::NONE;
    std::vector<uint8_t> compressed;
    compress_frame(frame, &compressed, &compression);

    const std::vector<uint8_t>* send_buffer = &frame;
    if (compression != FrameCompressionType::NONE && !compressed.empty()) {
        send_buffer = &compressed;
    } else {
        compression = FrameCompressionType::NONE;
    }

    header->compression = compression;
    header->payload_size = static_cast<uint32_t>(send_buffer->size());
    header->uncompressed_size = static_cast<uint32_t>(frame.size());

    payload->assign(send_buffer->begin(), send_buffer->end());
    SERVER_LOG_INFO() << "[Swapchain] Present swapchain #" << id << " image " << image_index;
    return VK_SUCCESS;
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

bool ServerSwapchainManager::allocate_resources(ServerSwapchain& swapchain,
                                                const VenusSwapchainCreateInfo& info,
                                                VenusSwapchainCreateReply* reply) {
    if (!state_) {
        return false;
    }

    for (uint32_t i = 0; i < swapchain.image_count; ++i) {
        auto& image = swapchain.images[i];

        VkImageCreateInfo image_info = {};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = static_cast<VkFormat>(info.format);
        image_info.extent = make_extent(info.width, info.height);
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = info.usage |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(swapchain.device, &image_info, nullptr, &image.image) != VK_SUCCESS) {
            SERVER_LOG_ERROR() << "[Swapchain] Failed to create swapchain image";
            return false;
        }

        VkMemoryRequirements image_reqs = {};
        vkGetImageMemoryRequirements(swapchain.device, image.image, &image_reqs);
        VkMemoryAllocateInfo image_alloc = {};
        image_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        image_alloc.allocationSize = image_reqs.size;
        image_alloc.memoryTypeIndex = find_memory_type(image_reqs.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(swapchain.device, &image_alloc, nullptr, &image.memory) != VK_SUCCESS) {
            SERVER_LOG_ERROR() << "[Swapchain] Failed to allocate image memory";
            return false;
        }
        vkBindImageMemory(swapchain.device, image.image, image.memory, 0);

        image.staging_size = static_cast<VkDeviceSize>(info.width) *
                             static_cast<VkDeviceSize>(info.height) * 4u;
        VkBufferCreateInfo buffer_info = {};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = image.staging_size;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(swapchain.device, &buffer_info, nullptr, &image.staging_buffer) != VK_SUCCESS) {
            SERVER_LOG_ERROR() << "[Swapchain] Failed to create staging buffer";
            return false;
        }

        VkMemoryRequirements buffer_reqs = {};
        vkGetBufferMemoryRequirements(swapchain.device, image.staging_buffer, &buffer_reqs);
        VkMemoryAllocateInfo buffer_alloc = {};
        buffer_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        buffer_alloc.allocationSize = buffer_reqs.size;
        buffer_alloc.memoryTypeIndex = find_memory_type(buffer_reqs.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(swapchain.device, &buffer_alloc, nullptr, &image.staging_memory) != VK_SUCCESS) {
            SERVER_LOG_ERROR() << "[Swapchain] Failed to allocate staging memory";
            return false;
        }
        vkBindBufferMemory(swapchain.device, image.staging_buffer, image.staging_memory, 0);
        vkMapMemory(swapchain.device, image.staging_memory, 0, buffer_alloc.allocationSize, 0, &image.staging_ptr);

        if (reply && i < kVenusMaxSwapchainImages) {
            reply->images[i].image_handle = reinterpret_cast<uint64_t>(image.image);
        }

        if (state_) {
            state_->resource_tracker.register_external_image(swapchain.client_device,
                                                             swapchain.device,
                                                             image.image,
                                                             image.image,
                                                             image_info);
        }
    }

    return true;
}

void ServerSwapchainManager::free_resources(ServerSwapchain& swapchain) {
    if (swapchain.device == VK_NULL_HANDLE) {
        return;
    }

    for (auto& image : swapchain.images) {
        if (state_) {
            state_->resource_tracker.unregister_external_image(image.image);
        }
        if (image.staging_ptr) {
            vkUnmapMemory(swapchain.device, image.staging_memory);
            image.staging_ptr = nullptr;
        }
        if (image.staging_memory) {
            vkFreeMemory(swapchain.device, image.staging_memory, nullptr);
        }
        if (image.staging_buffer) {
            vkDestroyBuffer(swapchain.device, image.staging_buffer, nullptr);
        }
        if (image.memory) {
            vkFreeMemory(swapchain.device, image.memory, nullptr);
        }
        if (image.image) {
            vkDestroyImage(swapchain.device, image.image, nullptr);
        }
    }
    swapchain.images.clear();

    if (swapchain.copy_fence) {
        vkDestroyFence(swapchain.device, swapchain.copy_fence, nullptr);
    }
    if (swapchain.command_pool) {
        vkDestroyCommandPool(swapchain.device, swapchain.command_pool, nullptr);
    }
}

uint32_t ServerSwapchainManager::find_memory_type(uint32_t type_bits,
                                                  VkMemoryPropertyFlags flags) const {
    if (!state_) {
        return 0;
    }
    const auto& props = state_->physical_device_memory_properties;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return 0;
}

} // namespace venus_plus
