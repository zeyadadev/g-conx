/*
 * Minimal linux-dmabuf-unstable-v1 protocol definitions for Wayland clients.
 * Based on the upstream Wayland protocol (MIT licensed).
 */

#ifndef VENUS_PLUS_WAYLAND_DMABUF_PROTOCOL_H
#define VENUS_PLUS_WAYLAND_DMABUF_PROTOCOL_H

#include <wayland-client.h>

#define WL_TYPES(arr) const_cast<const struct wl_interface **>(arr)

#ifdef __cplusplus
extern "C" {
#endif

struct zwp_linux_dmabuf_v1;
struct zwp_linux_buffer_params_v1;

extern const struct wl_interface zwp_linux_dmabuf_v1_interface;
extern const struct wl_interface zwp_linux_buffer_params_v1_interface;

#ifndef ZWP_LINUX_DMABUF_V1_ERROR_ENUM
#define ZWP_LINUX_DMABUF_V1_ERROR_ENUM
enum zwp_linux_dmabuf_v1_error {
    ZWP_LINUX_DMABUF_V1_ERROR_INVALID_FORMAT = 0,
    ZWP_LINUX_DMABUF_V1_ERROR_INVALID_DIMENSIONS = 1,
    ZWP_LINUX_DMABUF_V1_ERROR_INVALID_WL_BUFFER = 2,
};
#endif

#ifndef ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ENUM
#define ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ENUM
enum zwp_linux_buffer_params_v1_error {
    ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED = 0,
    ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX = 1,
    ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET = 2,
    ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE = 3,
    ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT = 4,
    ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS = 5,
    ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS = 6,
};
#endif

#ifndef ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_ENUM
#define ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_ENUM
enum zwp_linux_buffer_params_v1_flags {
    ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_Y_INVERT = 1,
    ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_INTERLACED = 2,
    ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_BOTTOM_FIRST = 4,
    ZWP_LINUX_BUFFER_PARAMS_V1_FLAGS_SCANOUT = 8,
};
#endif

struct zwp_linux_buffer_params_v1_listener {
    void (*created)(void *data,
                    struct zwp_linux_buffer_params_v1 *zwp_linux_buffer_params_v1,
                    struct wl_buffer *buffer);
    void (*failed)(void *data,
                   struct zwp_linux_buffer_params_v1 *zwp_linux_buffer_params_v1);
};

static inline int zwp_linux_buffer_params_v1_add_listener(
    struct zwp_linux_buffer_params_v1 *zwp_linux_buffer_params_v1,
    const struct zwp_linux_buffer_params_v1_listener *listener,
    void *data) {
    return wl_proxy_add_listener((struct wl_proxy *)zwp_linux_buffer_params_v1,
                                 (void (**)(void))listener, data);
}

#define ZWP_LINUX_DMABUF_V1_DESTROY 0
#define ZWP_LINUX_DMABUF_V1_CREATE_PARAMS 1

static inline void zwp_linux_dmabuf_v1_destroy(
    struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf_v1) {
    wl_proxy_marshal((struct wl_proxy *)zwp_linux_dmabuf_v1,
                     ZWP_LINUX_DMABUF_V1_DESTROY);
    wl_proxy_destroy((struct wl_proxy *)zwp_linux_dmabuf_v1);
}

static inline struct zwp_linux_buffer_params_v1 *zwp_linux_dmabuf_v1_create_params(
    struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf_v1) {
    struct wl_proxy *id = wl_proxy_marshal_constructor(
        (struct wl_proxy *)zwp_linux_dmabuf_v1, ZWP_LINUX_DMABUF_V1_CREATE_PARAMS,
        &zwp_linux_buffer_params_v1_interface, NULL);
    return (struct zwp_linux_buffer_params_v1 *)id;
}

#define ZWP_LINUX_BUFFER_PARAMS_V1_DESTROY 0
#define ZWP_LINUX_BUFFER_PARAMS_V1_ADD 1
#define ZWP_LINUX_BUFFER_PARAMS_V1_CREATE 2
#define ZWP_LINUX_BUFFER_PARAMS_V1_CREATE_IMMED 3

static inline void zwp_linux_buffer_params_v1_destroy(
    struct zwp_linux_buffer_params_v1 *zwp_linux_buffer_params_v1) {
    wl_proxy_marshal((struct wl_proxy *)zwp_linux_buffer_params_v1,
                     ZWP_LINUX_BUFFER_PARAMS_V1_DESTROY);
    wl_proxy_destroy((struct wl_proxy *)zwp_linux_buffer_params_v1);
}

static inline void zwp_linux_buffer_params_v1_add(
    struct zwp_linux_buffer_params_v1 *zwp_linux_buffer_params_v1,
    int32_t fd, uint32_t plane_idx, uint32_t offset, uint32_t stride,
    uint32_t modifier_hi, uint32_t modifier_lo) {
    wl_proxy_marshal((struct wl_proxy *)zwp_linux_buffer_params_v1,
                     ZWP_LINUX_BUFFER_PARAMS_V1_ADD, fd, plane_idx, offset,
                     stride, modifier_hi, modifier_lo);
}

static inline void zwp_linux_buffer_params_v1_create(
    struct zwp_linux_buffer_params_v1 *zwp_linux_buffer_params_v1,
    int32_t width, int32_t height, uint32_t format, uint32_t flags) {
    wl_proxy_marshal((struct wl_proxy *)zwp_linux_buffer_params_v1,
                     ZWP_LINUX_BUFFER_PARAMS_V1_CREATE, NULL, width, height,
                     format, flags);
}

static inline struct wl_buffer *zwp_linux_buffer_params_v1_create_immed(
    struct zwp_linux_buffer_params_v1 *zwp_linux_buffer_params_v1,
    int32_t width, int32_t height, uint32_t format, uint32_t flags) {
    struct wl_proxy *id = wl_proxy_marshal_constructor(
        (struct wl_proxy *)zwp_linux_buffer_params_v1,
        ZWP_LINUX_BUFFER_PARAMS_V1_CREATE_IMMED, &wl_buffer_interface, NULL,
        width, height, format, flags);
    return (struct wl_buffer *)id;
}

static const struct wl_interface * const zwp_linux_dmabuf_v1_create_params_types[] = {
    &zwp_linux_buffer_params_v1_interface
};

static const struct wl_message zwp_linux_dmabuf_v1_requests[] = {
    { "destroy", "", NULL },
    { "create_params", "n", WL_TYPES(zwp_linux_dmabuf_v1_create_params_types) },
};

static const struct wl_message zwp_linux_dmabuf_v1_events[] = {
    { "format", "u", NULL },
    { "modifier", "uuu", NULL },
};

static const struct wl_interface * const zwp_linux_buffer_params_v1_create_types[] = {
    &wl_buffer_interface
};

static const struct wl_interface * const zwp_linux_buffer_params_v1_create_immed_types[] = {
    &wl_buffer_interface
};

static const struct wl_interface * const zwp_linux_buffer_params_v1_created_types[] = {
    &wl_buffer_interface
};

static const struct wl_message zwp_linux_buffer_params_v1_requests[] = {
    { "destroy", "", NULL },
    { "add", "huuuuu", NULL },
    { "create", "niiuu", WL_TYPES(zwp_linux_buffer_params_v1_create_types) },
    { "create_immed", "niiuu", WL_TYPES(zwp_linux_buffer_params_v1_create_immed_types) },
};

static const struct wl_message zwp_linux_buffer_params_v1_events[] = {
    { "created", "o", WL_TYPES(zwp_linux_buffer_params_v1_created_types) },
    { "failed", "", NULL },
};

const struct wl_interface zwp_linux_dmabuf_v1_interface = {
    "zwp_linux_dmabuf_v1", 4,
    2, zwp_linux_dmabuf_v1_requests,
    2, zwp_linux_dmabuf_v1_events,
};

const struct wl_interface zwp_linux_buffer_params_v1_interface = {
    "zwp_linux_buffer_params_v1", 4,
    4, zwp_linux_buffer_params_v1_requests,
    2, zwp_linux_buffer_params_v1_events,
};

#ifdef __cplusplus
}
#endif

#endif // VENUS_PLUS_WAYLAND_DMABUF_PROTOCOL_H
