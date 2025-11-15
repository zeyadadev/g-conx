#ifndef VENUS_PLUS_SOCKET_UTILS_H
#define VENUS_PLUS_SOCKET_UTILS_H

#include <cstddef>

namespace venus_plus {

// Read exactly 'size' bytes from socket
// Returns true on success, false on error
bool read_all(int fd, void* buffer, size_t size);

// Write exactly 'size' bytes to socket
// Returns true on success, false on error
bool write_all(int fd, const void* buffer, size_t size);

} // namespace venus_plus

#endif // VENUS_PLUS_SOCKET_UTILS_H
