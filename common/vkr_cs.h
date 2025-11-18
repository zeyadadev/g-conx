#ifndef VENUS_PLUS_VKR_CS_H
#define VENUS_PLUS_VKR_CS_H

#include "vn_cs.h"

#ifdef __cplusplus
using vkr_object_id = vn_object_id;
#else
#define vkr_object_id vn_object_id
#endif

struct vkr_object {
    union {
        uint64_t u64;
    } handle;
};

#ifdef __cplusplus
#define vkr_cs_encoder vn_cs_encoder
#define vkr_cs_decoder vn_cs_decoder
#else
#define vkr_cs_encoder vn_cs_encoder
#define vkr_cs_decoder vn_cs_decoder
#endif

#ifdef __cplusplus
extern "C" {
#endif

bool vkr_cs_encoder_acquire(vkr_cs_encoder* enc);
void vkr_cs_encoder_release(vkr_cs_encoder* enc);
void vkr_cs_encoder_write(vkr_cs_encoder* enc, size_t size, const void* value, size_t value_size);

void vkr_cs_decoder_set_fatal(const vkr_cs_decoder* dec);
bool vkr_cs_decoder_get_fatal(const vkr_cs_decoder* dec);
void* vkr_cs_decoder_lookup_object(const vkr_cs_decoder* dec, vkr_object_id id, VkObjectType type);
void vkr_cs_decoder_reset_temp_pool(vkr_cs_decoder* dec);
void* vkr_cs_decoder_alloc_temp(vkr_cs_decoder* dec, size_t size);
void* vkr_cs_decoder_alloc_temp_array(vkr_cs_decoder* dec, size_t size, size_t count);
void* vkr_cs_decoder_get_blob_storage(vkr_cs_decoder* dec, size_t size);
void* vkr_cs_encoder_get_blob_storage(vkr_cs_encoder* enc, size_t offset, size_t size);
void vkr_cs_decoder_read(vkr_cs_decoder* dec, size_t size, void* value, size_t value_size);
void vkr_cs_decoder_peek(const vkr_cs_decoder* dec, size_t size, void* value, size_t value_size);

bool vkr_cs_handle_indirect_id(VkObjectType type);
vkr_object_id vkr_cs_handle_load_id(const void** handle, VkObjectType type);
void vkr_cs_handle_store_id(void** handle, vkr_object_id id, VkObjectType type);
uint64_t vkr_cs_get_object_handle(const void** handle, VkObjectType type);

#ifdef __cplusplus
}
#endif

#endif // VENUS_PLUS_VKR_CS_H
