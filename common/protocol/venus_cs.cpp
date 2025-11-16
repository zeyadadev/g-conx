#include "vn_cs.h"
#include "vkr_cs.h"

#include <algorithm>
#include <cstring>
#include <new>

extern "C" {

struct vn_cs_encoder* vn_cs_encoder_create(void) {
    return new (std::nothrow) vn_cs_encoder();
}

void vn_cs_encoder_destroy(struct vn_cs_encoder* enc) {
    delete enc;
}

struct vn_cs_decoder* vn_cs_decoder_create(void) {
    return new (std::nothrow) vn_cs_decoder();
}

void vn_cs_decoder_destroy(struct vn_cs_decoder* dec) {
    delete dec;
}

bool vn_cs_renderer_protocol_has_api_version(uint32_t api_version) {
    (void)api_version;
    return true;
}

bool vn_cs_renderer_protocol_has_extension(uint32_t ext_number) {
    (void)ext_number;
    return true;
}

void vn_cs_encoder_init_external(struct vn_cs_encoder* enc, void* data, size_t capacity) {
    if (!enc) return;
    enc->data = static_cast<uint8_t*>(data);
    enc->capacity = capacity;
    enc->offset = 0;
    enc->fatal = false;
    enc->owns_storage = false;
    enc->busy = false;
    enc->storage.clear();
}

void vn_cs_encoder_init_dynamic(struct vn_cs_encoder* enc) {
    if (!enc) return;
    enc->data = nullptr;
    enc->capacity = 0;
    enc->offset = 0;
    enc->fatal = false;
    enc->owns_storage = true;
    enc->busy = false;
    enc->storage.clear();
}

void vn_cs_decoder_init(struct vn_cs_decoder* dec, const void* data, size_t size) {
    if (!dec) return;
    dec->data = static_cast<const uint8_t*>(data);
    dec->size = size;
    dec->offset = 0;
    dec->fatal = false;
    dec->temp_buffers.clear();
}

size_t vn_cs_encoder_get_len(const struct vn_cs_encoder* enc) {
    return enc ? enc->offset : 0;
}

static bool vn_cs_encoder_ensure_capacity(struct vn_cs_encoder* enc, size_t required) {
    if (!enc) return false;
    if (enc->owns_storage) {
        if (enc->storage.size() < required) {
            try {
                enc->storage.resize(required);
            } catch (...) {
                enc->fatal = true;
                return false;
            }
        }
        return true;
    }

    if (required > enc->capacity) {
        enc->fatal = true;
        return false;
    }
    return true;
}

bool vn_cs_encoder_reserve(struct vn_cs_encoder* enc, size_t size) {
    if (!enc) return false;
    const size_t required = enc->offset + size;
    return vn_cs_encoder_ensure_capacity(enc, required);
}

void vn_cs_encoder_write(struct vn_cs_encoder* enc, size_t size, const void* value, size_t value_size) {
    if (!enc || !size) return;
    if (!vn_cs_encoder_reserve(enc, size)) return;

    uint8_t* dst = nullptr;
    if (enc->owns_storage) {
        dst = enc->storage.data() + enc->offset;
    } else {
        dst = enc->data + enc->offset;
    }

    const size_t copy_bytes = std::min(size, value_size);
    if (value && copy_bytes) {
        std::memcpy(dst, value, copy_bytes);
    }

    if (size > copy_bytes) {
        std::memset(dst + copy_bytes, 0, size - copy_bytes);
    }

    enc->offset += size;
    if (enc->owns_storage && enc->storage.size() < enc->offset) {
        enc->storage.resize(enc->offset);
    }
}

void vn_cs_decoder_set_fatal(struct vn_cs_decoder* dec) {
    if (dec) dec->fatal = true;
}

static bool vn_cs_decoder_has_bytes(const struct vn_cs_decoder* dec, size_t size) {
    if (!dec || dec->fatal) return false;
    return dec->offset + size <= dec->size;
}

void vn_cs_decoder_read(struct vn_cs_decoder* dec, size_t size, void* value, size_t value_size) {
    if (!vn_cs_decoder_has_bytes(dec, size)) {
        if (dec) dec->fatal = true;
        return;
    }

    const size_t copy_bytes = std::min(size, value_size);
    if (value && copy_bytes) {
        std::memcpy(value, dec->data + dec->offset, copy_bytes);
    }
    if (value && value_size > copy_bytes) {
        std::memset(static_cast<uint8_t*>(value) + copy_bytes, 0, value_size - copy_bytes);
    }

    dec->offset += size;
}

void vn_cs_decoder_peek(struct vn_cs_decoder* dec, size_t size, void* value, size_t value_size) {
    if (!vn_cs_decoder_has_bytes(dec, size)) {
        if (dec) dec->fatal = true;
        return;
    }

    const size_t copy_bytes = std::min(size, value_size);
    if (value && copy_bytes) {
        std::memcpy(value, dec->data + dec->offset, copy_bytes);
    }
    if (value && value_size > copy_bytes) {
        std::memset(static_cast<uint8_t*>(value) + copy_bytes, 0, value_size - copy_bytes);
    }
}

bool vn_cs_decoder_get_fatal(const struct vn_cs_decoder* dec) {
    return dec ? dec->fatal : true;
}

void vn_cs_decoder_reset_temp_storage(struct vn_cs_decoder* dec) {
    if (!dec) return;
    dec->temp_buffers.clear();
}

void* vn_cs_decoder_alloc_temp(struct vn_cs_decoder* dec, size_t size) {
    if (!dec) return nullptr;
    if (!size) return nullptr;

    auto buffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[size]);
    if (!buffer) {
        dec->fatal = true;
        return nullptr;
    }

    uint8_t* ptr = buffer.get();
    dec->temp_buffers.push_back(std::move(buffer));
    return ptr;
}

void* vn_cs_decoder_alloc_temp_array(struct vn_cs_decoder* dec, size_t size, size_t count) {
    if (!count || !size)
        return nullptr;
    size_t total = size * count;
    if (size != 0 && total / size != count) {
        vn_cs_decoder_set_fatal(dec);
        return nullptr;
    }
    return vn_cs_decoder_alloc_temp(dec, total);
}

const uint8_t* vn_cs_encoder_get_data(const struct vn_cs_encoder* enc) {
    if (!enc) return nullptr;
    if (enc->owns_storage) {
        return enc->storage.empty() ? nullptr : enc->storage.data();
    }
    return enc->data;
}

vn_object_id vn_cs_handle_load_id(const void** handle, VkObjectType) {
    if (!handle) return 0;
    return reinterpret_cast<vn_object_id>(*handle);
}

void vn_cs_handle_store_id(void** handle, vn_object_id id, VkObjectType) {
    if (!handle) return;
    *handle = reinterpret_cast<void*>(id);
}

uint64_t vn_cs_get_object_handle(const void** handle, VkObjectType type) {
    (void)type;
    return vn_cs_handle_load_id(handle, type);
}

bool vkr_cs_encoder_acquire(vkr_cs_encoder* enc) {
    if (!enc || enc->busy)
        return false;
    enc->offset = 0;
    enc->fatal = false;
    if (enc->owns_storage)
        enc->storage.clear();
    enc->busy = true;
    return true;
}

void vkr_cs_encoder_release(vkr_cs_encoder* enc) {
    if (!enc) return;
    enc->busy = false;
}

void vkr_cs_encoder_write(vkr_cs_encoder* enc, size_t size, const void* value, size_t value_size) {
    vn_cs_encoder_write(enc, size, value, value_size);
}

void vkr_cs_decoder_set_fatal(const vkr_cs_decoder* dec) {
    vn_cs_decoder_set_fatal(const_cast<vn_cs_decoder*>(dec));
}

bool vkr_cs_decoder_get_fatal(const vkr_cs_decoder* dec) {
    return vn_cs_decoder_get_fatal(dec);
}

void* vkr_cs_decoder_lookup_object(const vkr_cs_decoder*, vkr_object_id id, VkObjectType) {
    return reinterpret_cast<void*>(id);
}

void vkr_cs_decoder_reset_temp_pool(vkr_cs_decoder* dec) {
    vn_cs_decoder_reset_temp_storage(dec);
}

void* vkr_cs_decoder_alloc_temp(vkr_cs_decoder* dec, size_t size) {
    return vn_cs_decoder_alloc_temp(dec, size);
}

void* vkr_cs_decoder_alloc_temp_array(vkr_cs_decoder* dec, size_t size, size_t count) {
    return vn_cs_decoder_alloc_temp_array(dec, size, count);
}

void* vkr_cs_decoder_get_blob_storage(vkr_cs_decoder* dec, size_t size) {
    return vn_cs_decoder_alloc_temp(dec, size);
}

void* vkr_cs_encoder_get_blob_storage(vkr_cs_encoder* enc, size_t offset, size_t size) {
    if (!enc || !enc->owns_storage)
        return nullptr;
    const size_t required = offset + size;
    if (!vn_cs_encoder_ensure_capacity(enc, required))
        return nullptr;
    return enc->storage.data() + offset;
}

void vkr_cs_decoder_read(vkr_cs_decoder* dec, size_t size, void* value, size_t value_size) {
    vn_cs_decoder_read(dec, size, value, value_size);
}

void vkr_cs_decoder_peek(const vkr_cs_decoder* dec, size_t size, void* value, size_t value_size) {
    vn_cs_decoder_peek(const_cast<vn_cs_decoder*>(dec), size, value, value_size);
}

bool vkr_cs_handle_indirect_id(VkObjectType) {
    return false;
}

vkr_object_id vkr_cs_handle_load_id(const void** handle, VkObjectType type) {
    return vn_cs_handle_load_id(handle, type);
}

void vkr_cs_handle_store_id(void** handle, vkr_object_id id, VkObjectType type) {
    vn_cs_handle_store_id(handle, id, type);
}

uint64_t vkr_cs_get_object_handle(const void** handle, VkObjectType type) {
    return vn_cs_get_object_handle(handle, type);
}

} // extern "C"
