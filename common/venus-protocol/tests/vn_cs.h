/*
 * Copyright 2020 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VN_CS_H
#define VN_CS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>

typedef uint64_t vn_object_id;

struct vn_cs_encoder;
struct vn_cs_decoder;

static inline bool
vn_cs_renderer_protocol_has_api_version(uint32_t api_version)
{
    return true;
}

static inline bool
vn_cs_renderer_protocol_has_extension(uint32_t ext_number)
{
    return true;
}

static inline size_t
vn_cs_encoder_get_len(const struct vn_cs_encoder *enc)
{
    return 0;
}

static inline bool
vn_cs_encoder_reserve(struct vn_cs_encoder *cs, size_t size)
{
    return true;
}

static inline void
vn_cs_encoder_write(struct vn_cs_encoder *cs, size_t size, const void *val, size_t val_size)
{
}

static inline void
vn_cs_decoder_set_fatal(struct vn_cs_decoder *dec)
{
}

static inline void
vn_cs_decoder_read(struct vn_cs_decoder *dec, size_t size, void *val, size_t val_size)
{
}

static inline void
vn_cs_decoder_peek(struct vn_cs_decoder *dec, size_t size, void *val, size_t val_size)
{
}

static inline vn_object_id
vn_cs_handle_load_id(const void **handle, VkObjectType type)
{
    return 0;
}

static inline void
vn_cs_handle_store_id(void **handle, vn_object_id id, VkObjectType type)
{
}

#endif /* VN_CS_H */
