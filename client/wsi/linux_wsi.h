#ifndef VENUS_PLUS_LINUX_WSI_H
#define VENUS_PLUS_LINUX_WSI_H

#if defined(__linux__) && !defined(__ANDROID__)

#include <memory>
#include <vulkan/vulkan.h>

namespace venus_plus {

class PlatformWSI;

std::shared_ptr<PlatformWSI> create_linux_platform_wsi(VkSurfaceKHR surface);

} // namespace venus_plus

#endif // defined(__linux__) && !defined(__ANDROID__)

#endif // VENUS_PLUS_LINUX_WSI_H
