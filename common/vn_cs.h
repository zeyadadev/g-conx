#ifndef VENUS_PLUS_VN_CS_H
#define VENUS_PLUS_VN_CS_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#else
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#endif

#include <vulkan/vulkan.h>
#ifdef __cplusplus
struct vn_cs_encoder {
    uint8_t* data;
    size_t capacity;
    size_t offset;
    bool fatal;
    bool owns_storage;
    bool busy;
    std::vector<uint8_t> storage;
};

struct vn_cs_decoder {
    const uint8_t* data;
    size_t size;
    size_t offset;
    bool fatal;
    std::vector<std::unique_ptr<uint8_t[]>> temp_buffers;
};
#else
struct vn_cs_encoder;
struct vn_cs_decoder;
typedef struct vn_cs_encoder vn_cs_encoder;
typedef struct vn_cs_decoder vn_cs_decoder;
#endif

#ifdef __cplusplus
using vn_object_id = uint64_t;
#else
typedef uint64_t vn_object_id;
#endif

// Protocol capability helpers
#ifdef __cplusplus
extern "C" {
#endif
bool vn_cs_renderer_protocol_has_api_version(uint32_t api_version);
bool vn_cs_renderer_protocol_has_extension(uint32_t ext_number);

// Initialization helpers
struct vn_cs_encoder* vn_cs_encoder_create(void);
void vn_cs_encoder_destroy(struct vn_cs_encoder* enc);
struct vn_cs_decoder* vn_cs_decoder_create(void);
void vn_cs_decoder_destroy(struct vn_cs_decoder* dec);
void vn_cs_encoder_init_external(struct vn_cs_encoder* enc, void* data, size_t capacity);
void vn_cs_encoder_init_dynamic(struct vn_cs_encoder* enc);
void vn_cs_decoder_init(struct vn_cs_decoder* dec, const void* data, size_t size);

// Driver-facing CS helpers required by the generated venus protocol headers
size_t vn_cs_encoder_get_len(const struct vn_cs_encoder* enc);
bool vn_cs_encoder_reserve(struct vn_cs_encoder* enc, size_t size);
size_t vn_cs_decoder_bytes_remaining(const struct vn_cs_decoder* dec);

#ifndef VN_RENDERER_STATIC_DISPATCH
void vn_cs_encoder_write(struct vn_cs_encoder* enc, size_t size, const void* value, size_t value_size);
void vn_cs_decoder_set_fatal(struct vn_cs_decoder* dec);
void vn_cs_decoder_read(struct vn_cs_decoder* dec, size_t size, void* value, size_t value_size);
void vn_cs_decoder_peek(struct vn_cs_decoder* dec, size_t size, void* value, size_t value_size);
bool vn_cs_decoder_get_fatal(const struct vn_cs_decoder* dec);
void* vn_cs_decoder_alloc_temp(struct vn_cs_decoder* dec, size_t size);
void* vn_cs_decoder_alloc_temp_array(struct vn_cs_decoder* dec, size_t size, size_t count);
vn_object_id vn_cs_handle_load_id(const void** handle, VkObjectType type);
void vn_cs_handle_store_id(void** handle, vn_object_id id, VkObjectType type);
uint64_t vn_cs_get_object_handle(const void** handle, VkObjectType type);
#endif

// Convenience helpers
void vn_cs_decoder_reset_temp_storage(struct vn_cs_decoder* dec);
const uint8_t* vn_cs_encoder_get_data(const struct vn_cs_encoder* enc);

#ifdef __cplusplus
}
#endif

#endif // VENUS_PLUS_VN_CS_H
