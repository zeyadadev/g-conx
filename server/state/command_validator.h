#ifndef VENUS_PLUS_COMMAND_VALIDATOR_H
#define VENUS_PLUS_COMMAND_VALIDATOR_H

#include <string>
#include <vulkan/vulkan.h>

namespace venus_plus {

class ResourceTracker;

class CommandValidator {
public:
    explicit CommandValidator(ResourceTracker* tracker = nullptr);

    void set_resource_tracker(ResourceTracker* tracker);

    bool validate_copy_buffer(VkBuffer srcBuffer,
                              VkBuffer dstBuffer,
                              uint32_t regionCount,
                              const VkBufferCopy* regions,
                              std::string* error) const;

    bool validate_copy_image(VkImage srcImage,
                             VkImage dstImage,
                             uint32_t regionCount,
                             const VkImageCopy* regions,
                             std::string* error) const;

    bool validate_blit_image(VkImage srcImage,
                             VkImage dstImage,
                             uint32_t regionCount,
                             const VkImageBlit* regions,
                             std::string* error) const;

    bool validate_copy_buffer_to_image(VkBuffer srcBuffer,
                                       VkImage dstImage,
                                       uint32_t regionCount,
                                       const VkBufferImageCopy* regions,
                                       std::string* error) const;

    bool validate_copy_image_to_buffer(VkImage srcImage,
                                       VkBuffer dstBuffer,
                                       uint32_t regionCount,
                                       const VkBufferImageCopy* regions,
                                       std::string* error) const;

    bool validate_fill_buffer(VkBuffer buffer,
                              VkDeviceSize offset,
                              VkDeviceSize size,
                              std::string* error) const;

    bool validate_update_buffer(VkBuffer buffer,
                                VkDeviceSize offset,
                                VkDeviceSize dataSize,
                                const void* data,
                                std::string* error) const;

    bool validate_clear_color_image(VkImage image,
                                    uint32_t rangeCount,
                                    const VkImageSubresourceRange* ranges,
                                    std::string* error) const;

private:
    ResourceTracker* tracker_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_COMMAND_VALIDATOR_H
