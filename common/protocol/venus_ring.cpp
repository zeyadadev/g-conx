#include "vn_ring.h"

#include <iostream>

#include "network/network_client.h"

using namespace venus_plus;

vn_cs_encoder* vn_ring_submit_command_init(struct vn_ring* ring,
                                           struct vn_ring_submit_command* submit,
                                           void* cmd_data,
                                           size_t cmd_size,
                                           size_t reply_size) {
    if (!ring || !submit)
        return nullptr;

    submit->cmd_data = cmd_data;
    submit->cmd_size = cmd_size;
    submit->reply_size = reply_size;
    submit->reply_buffer.clear();
    vn_cs_encoder_init_external(&submit->encoder, cmd_data, cmd_size);
    vn_cs_decoder_init(&submit->decoder, nullptr, 0);

    return &submit->encoder;
}

void vn_ring_submit_command(struct vn_ring* ring, struct vn_ring_submit_command* submit) {
    if (!ring || !submit || !ring->client)
        return;

    const size_t payload_size = vn_cs_encoder_get_len(&submit->encoder);
    if (!payload_size) {
        std::cerr << "[Client] Attempted to send empty Venus command\n";
        return;
    }

    if (!ring->client->send(submit->cmd_data, payload_size)) {
        std::cerr << "[Client] Failed to send Venus command\n";
    }
}

vn_cs_decoder* vn_ring_get_command_reply(struct vn_ring* ring, struct vn_ring_submit_command* submit) {
    if (!ring || !submit || !ring->client)
        return nullptr;

    if (!submit->reply_size)
        return nullptr;

    std::vector<uint8_t> reply;
    if (!ring->client->receive(reply)) {
        std::cerr << "[Client] Failed to receive Venus reply\n";
        return nullptr;
    }

    submit->reply_buffer = std::move(reply);
    vn_cs_decoder_init(&submit->decoder,
                       submit->reply_buffer.empty() ? nullptr : submit->reply_buffer.data(),
                       submit->reply_buffer.size());
    return &submit->decoder;
}

void vn_ring_free_command_reply(struct vn_ring* ring, struct vn_ring_submit_command* submit) {
    (void)ring;
    if (!submit)
        return;

    submit->reply_buffer.clear();
    vn_cs_decoder_reset_temp_storage(&submit->decoder);
}
