#ifndef VENUS_PLUS_LINUX_SURFACE_H
#define VENUS_PLUS_LINUX_SURFACE_H

#if defined(__linux__) && !defined(__ANDROID__)

#include <vulkan/vulkan.h>

#include <cstdint>

struct xcb_connection_t;
struct wl_display;
struct wl_surface;
struct _XDisplay;

namespace venus_plus {

enum class LinuxSurfaceType {
    kNone = 0,
    kXcb,
    kWayland,
};

struct LinuxSurface {
    static constexpr uint32_t kMagic = 0x4c535246u; // 'LSRF'

    uint32_t magic = kMagic;
    LinuxSurfaceType type = LinuxSurfaceType::kNone;

    struct {
        xcb_connection_t* connection = nullptr;
        uint32_t window = 0;
        uint8_t depth = 32;
    } xcb;

    struct {
        wl_display* display = nullptr;
        wl_surface* surface = nullptr;
    } wayland;
};

bool is_linux_surface(VkSurfaceKHR surface);
LinuxSurface* get_linux_surface(VkSurfaceKHR surface);

VkExtent2D query_linux_surface_extent(const LinuxSurface& surface);

} // namespace venus_plus

#endif // defined(__linux__) && !defined(__ANDROID__)

#endif // VENUS_PLUS_LINUX_SURFACE_H
