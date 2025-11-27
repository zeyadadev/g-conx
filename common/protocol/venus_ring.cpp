#include "vn_ring.h"

#include "network/network_client.h"
#include "utils/logging.h"

#define CLIENT_LOG_ERROR() VP_LOG_STREAM_ERROR(CLIENT)

using namespace venus_plus;

// These functions must have C linkage to match the header declarations
extern "C" {

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
        CLIENT_LOG_ERROR() << "Attempted to send empty Venus command";
        return;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(submit->cmd_data);
    ring->pending_buffer.insert(ring->pending_buffer.end(), bytes, bytes + payload_size);
}

void vn_ring_flush_pending(struct vn_ring* ring) {
    if (!ring || !ring->client)
        return;

    if (ring->pending_buffer.empty())
        return;

    if (!ring->client->send(ring->pending_buffer.data(), ring->pending_buffer.size())) {
        CLIENT_LOG_ERROR() << "Failed to send pending Venus commands";
        return;
    }

    ring->pending_buffer.clear();
}

vn_cs_decoder* vn_ring_get_command_reply(struct vn_ring* ring, struct vn_ring_submit_command* submit) {
    if (!ring || !submit || !ring->client)
        return nullptr;

    if (!submit->reply_size)
        return nullptr;

    if (ring->pending_buffer.empty()) {
        CLIENT_LOG_ERROR() << "No pending Venus commands to flush for reply";
        return nullptr;
    }

    vn_ring_flush_pending(ring);
    if (!ring->pending_buffer.empty()) {
        CLIENT_LOG_ERROR() << "Pending buffer not cleared after flush";
        return nullptr;
    }

    std::vector<uint8_t> reply;
    if (!ring->client->receive(reply)) {
        CLIENT_LOG_ERROR() << "Failed to receive Venus reply";
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

} // extern "C"
