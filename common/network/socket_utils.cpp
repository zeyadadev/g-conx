#include "socket_utils.h"
#include <unistd.h>
#include <iostream>

namespace venus_plus {

bool read_all(int fd, void* buffer, size_t size) {
    uint8_t* ptr = static_cast<uint8_t*>(buffer);
    size_t remaining = size;

    while (remaining > 0) {
        ssize_t n = read(fd, ptr, remaining);

        if (n < 0) {
            std::cerr << "read() error\n";
            return false;
        }
        if (n == 0) {
            std::cerr << "Connection closed by peer\n";
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
            std::cerr << "write() error\n";
            return false;
        }

        ptr += n;
        remaining -= n;
    }

    return true;
}

} // namespace venus_plus
