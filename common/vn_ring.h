#ifndef VENUS_PLUS_VN_RING_H
#define VENUS_PLUS_VN_RING_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "vn_cs.h"

namespace venus_plus {
class NetworkClient;
}

#define VN_TRACE_FUNC()

struct vn_ring_submit_command {
    void* cmd_data;
    size_t cmd_size;
    size_t reply_size;
    vn_cs_encoder encoder;
    vn_cs_decoder decoder;
    std::vector<uint8_t> reply_buffer;
};

struct vn_ring {
    venus_plus::NetworkClient* client;
    std::vector<uint8_t> pending_buffer;
};

vn_cs_encoder* vn_ring_submit_command_init(struct vn_ring* ring,
                                           struct vn_ring_submit_command* submit,
                                           void* cmd_data,
                                           size_t cmd_size,
                                           size_t reply_size);

void vn_ring_submit_command(struct vn_ring* ring, struct vn_ring_submit_command* submit);
void vn_ring_flush_pending(struct vn_ring* ring);
vn_cs_decoder* vn_ring_get_command_reply(struct vn_ring* ring, struct vn_ring_submit_command* submit);
void vn_ring_free_command_reply(struct vn_ring* ring, struct vn_ring_submit_command* submit);

#endif // VENUS_PLUS_VN_RING_H
