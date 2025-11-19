#include "wsi/linux_surface.h"

#if defined(__linux__) && !defined(__ANDROID__)

#include <xcb/xcb.h>
#include <algorithm>
#include <cstdlib>

namespace venus_plus {

bool is_linux_surface(VkSurfaceKHR surface) {
    if (surface == VK_NULL_HANDLE) {
        return false;
    }
    const auto* info = reinterpret_cast<const LinuxSurface*>(surface);
    return info && info->magic == LinuxSurface::kMagic;
}

LinuxSurface* get_linux_surface(VkSurfaceKHR surface) {
    if (!is_linux_surface(surface)) {
        return nullptr;
    }
    return reinterpret_cast<LinuxSurface*>(surface);
}

VkExtent2D query_linux_surface_extent(const LinuxSurface& surface) {
    VkExtent2D extent = {800, 600};
    if (surface.type == LinuxSurfaceType::kXcb && surface.xcb.connection && surface.xcb.window) {
        xcb_get_geometry_cookie_t cookie =
            xcb_get_geometry(surface.xcb.connection, surface.xcb.window);
        xcb_get_geometry_reply_t* reply = xcb_get_geometry_reply(surface.xcb.connection, cookie, nullptr);
        if (reply) {
            extent.width = std::max<uint32_t>(1u, reply->width);
            extent.height = std::max<uint32_t>(1u, reply->height);
            extent.width = std::min<uint32_t>(extent.width, 4096u);
            extent.height = std::min<uint32_t>(extent.height, 4096u);
            const_cast<LinuxSurface&>(surface).xcb.depth = reply->depth;
            free(reply);
        }
    }
    return extent;
}

} // namespace venus_plus

#endif // defined(__linux__) && !defined(__ANDROID__)
