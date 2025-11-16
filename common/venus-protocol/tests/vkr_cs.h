/*
 * Copyright 2021 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VKR_CS_H
#define VKR_CS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>

struct vkr_cs_encoder;
struct vkr_cs_decoder;

typedef uint64_t vkr_object_id;

struct vkr_object {
    union {
        uint64_t u64;
    } handle;
};

static inline bool
vkr_cs_encoder_acquire(struct vkr_cs_encoder *enc)
{
   return true;
}

static inline void
vkr_cs_encoder_release(struct vkr_cs_encoder *enc)
{
}

static inline void
vkr_cs_encoder_write(struct vkr_cs_encoder *enc,
                     size_t size,
                     const void *val,
                     size_t val_size)
{
}

static inline void
vkr_cs_decoder_set_fatal(const struct vkr_cs_decoder *dec)
{
}

static inline bool
vkr_cs_decoder_get_fatal(const struct vkr_cs_decoder *dec)
{
   return false;
}

static inline void
vkr_cs_decoder_read(struct vkr_cs_decoder *dec,
                    size_t size,
                    void *val,
                    size_t val_size)
{
}

static inline void
vkr_cs_decoder_peek(const struct vkr_cs_decoder *dec,
                    size_t size,
                    void *val,
                    size_t val_size)
{
}

static inline struct vkr_object *
vkr_cs_decoder_lookup_object(const struct vkr_cs_decoder *dec,
                             vkr_object_id id,
                             VkObjectType type)
{
    return NULL;
}

static inline void
vkr_cs_decoder_reset_temp_pool(struct vkr_cs_decoder *dec)
{
}

static inline void *
vkr_cs_decoder_alloc_temp(struct vkr_cs_decoder *dec, size_t size)
{
    return NULL;
}

static inline void *
vkr_cs_decoder_alloc_temp_array(struct vkr_cs_decoder *dec,
                                size_t size,
                                size_t count)
{
    return NULL;
}

static inline void *
vkr_cs_decoder_get_blob_storage(struct vkr_cs_decoder *dec, size_t size)
{
   return NULL;
}

static inline void *
vkr_cs_encoder_get_blob_storage(struct vkr_cs_encoder *enc, size_t offset, size_t size)
{
   return NULL;
}

static inline bool
vkr_cs_handle_indirect_id(VkObjectType type)
{
    return true;
}

static inline vkr_object_id
vkr_cs_handle_load_id(const void **handle, VkObjectType type)
{
    return 0;
}

static inline void
vkr_cs_handle_store_id(void **handle,
                       vkr_object_id id,
                       VkObjectType type)
{
}

#endif /* VKR_CS_H */
