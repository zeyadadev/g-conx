#include "socket_utils.h"

#include <unistd.h>

#include "utils/logging.h"

#define NETWORK_LOG_ERROR() VP_LOG_STREAM_ERROR(NETWORK)

namespace venus_plus {

bool read_all(int fd, void* buffer, size_t size) {
    uint8_t* ptr = static_cast<uint8_t*>(buffer);
    size_t remaining = size;

    while (remaining > 0) {
        ssize_t n = read(fd, ptr, remaining);

        if (n < 0) {
            NETWORK_LOG_ERROR() << "read() error";
            return false;
        }
        if (n == 0) {
            NETWORK_LOG_ERROR() << "Connection closed by peer";
            return false;
        }

        ptr += n;
        remaining -= n;
    }

    return true;
}

bool write_all(int fd, const void* buffer, size_t size) {
    const uint8_t* ptr = static_cast<const uint8_t*>(buffer);
    size_t remaining = size;

    while (remaining > 0) {
        ssize_t n = write(fd, ptr, remaining);

        if (n < 0) {
            NETWORK_LOG_ERROR() << "write() error";
            return false;
        }

        ptr += n;
        remaining -= n;
    }

    return true;
}

} // namespace venus_plus
