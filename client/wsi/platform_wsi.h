#ifndef VENUS_PLUS_PLATFORM_WSI_H
#define VENUS_PLUS_PLATFORM_WSI_H

#include <memory>
#include <vulkan/vulkan.h>

#include "protocol/frame_transfer.h"

namespace venus_plus {

class PlatformWSI {
public:
    virtual ~PlatformWSI() = default;

    virtual bool init(const VkSwapchainCreateInfoKHR& info, uint32_t image_count) = 0;
    virtual void handle_frame(const VenusFrameHeader& frame, const uint8_t* data) = 0;
    virtual void shutdown() = 0;
};

std::shared_ptr<PlatformWSI> create_platform_wsi(VkSurfaceKHR surface);

} // namespace venus_plus

#endif // VENUS_PLUS_PLATFORM_WSI_H
