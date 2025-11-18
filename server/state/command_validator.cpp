#include "command_validator.h"
#include "resource_tracker.h"

namespace venus_plus {

CommandValidator::CommandValidator(ResourceTracker* tracker)
    : tracker_(tracker) {}

void CommandValidator::set_resource_tracker(ResourceTracker* tracker) {
    tracker_ = tracker;
}

bool CommandValidator::validate_copy_buffer(VkBuffer srcBuffer,
                                            VkBuffer dstBuffer,
                                            uint32_t regionCount,
                                            const VkBufferCopy* regions,
                                            std::string* error) const {
    if (!tracker_ || !regions || regionCount == 0) {
        if (error) *error = "Invalid regions for vkCmdCopyBuffer";
        return false;
    }
    if (!tracker_->buffer_exists(srcBuffer) || !tracker_->buffer_exists(dstBuffer)) {
        if (error) *error = "Source or destination buffer not found";
        return false;
    }
    return true;
}

bool CommandValidator::validate_copy_image(VkImage srcImage,
                                           VkImage dstImage,
                                           uint32_t regionCount,
                                           const VkImageCopy* regions,
                                           std::string* error) const {
    if (!tracker_ || !regions || regionCount == 0) {
        if (error) *error = "Invalid regions for vkCmdCopyImage";
        return false;
    }
    if (!tracker_->image_exists(srcImage) || !tracker_->image_exists(dstImage)) {
        if (error) *error = "Source or destination image not found";
        return false;
    }
    return true;
}

bool CommandValidator::validate_blit_image(VkImage srcImage,
                                           VkImage dstImage,
                                           uint32_t regionCount,
                                           const VkImageBlit* regions,
                                           std::string* error) const {
    if (!tracker_ || !regions || regionCount == 0) {
        if (error) *error = "Invalid regions for vkCmdBlitImage";
        return false;
    }
    if (!tracker_->image_exists(srcImage) || !tracker_->image_exists(dstImage)) {
        if (error) *error = "Source or destination image not found";
        return false;
    }
    return true;
}

bool CommandValidator::validate_copy_buffer_to_image(VkBuffer srcBuffer,
                                                     VkImage dstImage,
                                                     uint32_t regionCount,
                                                     const VkBufferImageCopy* regions,
                                                     std::string* error) const {
    if (!tracker_ || !regions || regionCount == 0) {
        if (error) *error = "Invalid regions for vkCmdCopyBufferToImage";
        return false;
    }
    if (!tracker_->buffer_exists(srcBuffer) || !tracker_->image_exists(dstImage)) {
        if (error) *error = "Buffer or image not found";
        return false;
    }
    return true;
}

bool CommandValidator::validate_copy_image_to_buffer(VkImage srcImage,
                                                     VkBuffer dstBuffer,
                                                     uint32_t regionCount,
                                                     const VkBufferImageCopy* regions,
                                                     std::string* error) const {
    if (!tracker_ || !regions || regionCount == 0) {
        if (error) *error = "Invalid regions for vkCmdCopyImageToBuffer";
        return false;
    }
    if (!tracker_->image_exists(srcImage) || !tracker_->buffer_exists(dstBuffer)) {
        if (error) *error = "Image or buffer not found";
        return false;
    }
    return true;
}

bool CommandValidator::validate_fill_buffer(VkBuffer buffer,
                                            VkDeviceSize /*offset*/,
                                            VkDeviceSize /*size*/,
                                            std::string* error) const {
    if (!tracker_ || !tracker_->buffer_exists(buffer)) {
        if (error) *error = "Buffer not found for vkCmdFillBuffer";
        return false;
    }
    return true;
}

bool CommandValidator::validate_update_buffer(VkBuffer buffer,
                                              VkDeviceSize /*offset*/,
                                              VkDeviceSize dataSize,
                                              const void* data,
                                              std::string* error) const {
    if (!tracker_ || !tracker_->buffer_exists(buffer)) {
        if (error) *error = "Buffer not found for vkCmdUpdateBuffer";
        return false;
    }
    if (!data || dataSize == 0 || (dataSize % 4) != 0) {
        if (error) *error = "vkCmdUpdateBuffer requires data aligned to 4 bytes";
        return false;
    }
    return true;
}

bool CommandValidator::validate_clear_color_image(VkImage image,
                                                  uint32_t rangeCount,
                                                  const VkImageSubresourceRange* ranges,
                                                  std::string* error) const {
    if (!tracker_ || !ranges || rangeCount == 0) {
        if (error) *error = "Invalid ranges for vkCmdClearColorImage";
        return false;
    }
    if (!tracker_->image_exists(image)) {
        if (error) *error = "Image not found for vkCmdClearColorImage";
        return false;
    }
    return true;
}

} // namespace venus_plus
