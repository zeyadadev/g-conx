#ifndef VENUS_PLUS_RENDERER_DECODER_H
#define VENUS_PLUS_RENDERER_DECODER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ServerState;
struct VenusRenderer;

struct VenusRenderer* venus_renderer_create(struct ServerState* state);
void venus_renderer_destroy(struct VenusRenderer* renderer);
bool venus_renderer_handle(struct VenusRenderer* renderer,
                           const void* data,
                           size_t size,
                           uint8_t** reply_data,
                           size_t* reply_size);

#ifdef __cplusplus
}
#endif

#endif // VENUS_PLUS_RENDERER_DECODER_H
