#ifndef VENUS_PLUS_MESSAGE_H
#define VENUS_PLUS_MESSAGE_H

#include <cstdint>
#include <cstddef>

namespace venus_plus {

// Magic number for message validation: "VPLS" (Venus PLuS)
constexpr uint32_t MESSAGE_MAGIC = 0x56504C53;

// Message header
struct MessageHeader {
    uint32_t magic;    // Magic number for validation
    uint32_t size;     // Payload size in bytes
};

} // namespace venus_plus

#endif // VENUS_PLUS_MESSAGE_H
