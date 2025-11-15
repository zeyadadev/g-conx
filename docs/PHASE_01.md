# Phase 01: Network Communication

**Prove TCP communication works with Venus protocol encoding/decoding**

## Overview

**Goal**: Establish basic client-server communication and implement ONE simple Vulkan command end-to-end.

**Duration**: 3 days

**Command to implement**: `vkEnumerateInstanceVersion`

## Objectives

By the end of this phase, you will have:

1. ✅ Working TCP client and server
2. ✅ Message protocol (header + payload)
3. ✅ Venus protocol encoder/decoder wrappers
4. ✅ Client ICD stub that can encode and send commands
5. ✅ Server that can receive, decode, and send fake replies
6. ✅ Test application that successfully calls `vkEnumerateInstanceVersion`

## Architecture

```
Application                                 Server
    │                                          │
    ├─ vkEnumerateInstanceVersion()           │
    │      │                                   │
    ▼      ▼                                   │
┌────────────────┐                    ┌───────────────┐
│  Client ICD    │                    │    Server     │
│                │                    │               │
│  1. Encode     │                    │               │
│     Command    │                    │               │
│                │    TCP Socket      │               │
│  2. Send    ───┼───────────────────►│  3. Receive   │
│                │                    │               │
│                │                    │  4. Decode    │
│                │                    │               │
│                │                    │  5. Create    │
│                │                    │     Fake      │
│                │                    │     Reply     │
│                │                    │               │
│  7. Receive ◄──┼────────────────────┤  6. Send      │
│                │                    │     Reply     │
│  8. Decode     │                    │               │
│     Reply      │                    │               │
│                │                    │               │
│  9. Return     │                    │               │
│     Result     │                    │               │
└────────────────┘                    └───────────────┘
```

## Implementation Steps

### Step 1: Project Setup (30 mins)

Create the basic directory structure:

```bash
cd /home/ayman/venus-plus

# Create directories
mkdir -p common/{venus-protocol,network,protocol-helpers}
mkdir -p client/icd
mkdir -p server
mkdir -p test-app/phase01
mkdir -p build

# Copy Venus protocol headers
cp /home/ayman/mesa/src/virtio/venus-protocol/*.h common/venus-protocol/
```

Create root CMakeLists.txt:

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(VenusPlus VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Vulkan
find_package(Vulkan REQUIRED)

# Add subdirectories
add_subdirectory(common)
add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(test-app)
```

### Step 2: Network Layer (3-4 hours)

#### 2.1 Message Protocol

Create `common/network/message.h`:

```cpp
#ifndef VENUS_PLUS_MESSAGE_H
#define VENUS_PLUS_MESSAGE_H

#include <cstdint>
#include <cstddef>

namespace venus_plus {

// Magic number for message validation
constexpr uint32_t MESSAGE_MAGIC = 0x56504C53;  // "VPLS"

// Message header
struct MessageHeader {
    uint32_t magic;    // Magic number for validation
    uint32_t size;     // Payload size in bytes
};

} // namespace venus_plus

#endif // VENUS_PLUS_MESSAGE_H
```

Create `common/network/message.cpp`:

```cpp
#include "message.h"
#include <cstring>

namespace venus_plus {

// Message implementation (if needed)

} // namespace venus_plus
```

#### 2.2 Socket Utilities

Create `common/network/socket_utils.h`:

```cpp
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
```

Create `common/network/socket_utils.cpp`:

```cpp
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
```

#### 2.3 Network Client

Create `common/network/network_client.h`:

```cpp
#ifndef VENUS_PLUS_NETWORK_CLIENT_H
#define VENUS_PLUS_NETWORK_CLIENT_H

#include <string>
#include <vector>
#include <cstdint>

namespace venus_plus {

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    // Connect to server
    bool connect(const std::string& host, uint16_t port);

    // Send message
    bool send(const void* data, size_t size);

    // Receive message
    bool receive(std::vector<uint8_t>& buffer);

    // Disconnect
    void disconnect();

    // Check if connected
    bool is_connected() const { return fd_ >= 0; }

private:
    int fd_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_NETWORK_CLIENT_H
```

Create `common/network/network_client.cpp`:

```cpp
#include "network_client.h"
#include "message.h"
#include "socket_utils.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace venus_plus {

NetworkClient::NetworkClient() : fd_(-1) {}

NetworkClient::~NetworkClient() {
    disconnect();
}

bool NetworkClient::connect(const std::string& host, uint16_t port) {
    // Create socket
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        std::cerr << "Failed to create socket\n";
        return false;
    }

    // Setup server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address: " << host << "\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    // Connect
    if (::connect(fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed to " << host << ":" << port << "\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    std::cout << "Connected to " << host << ":" << port << "\n";
    return true;
}

bool NetworkClient::send(const void* data, size_t size) {
    if (fd_ < 0) {
        std::cerr << "Not connected\n";
        return false;
    }

    // Send header
    MessageHeader header;
    header.magic = MESSAGE_MAGIC;
    header.size = size;

    if (!write_all(fd_, &header, sizeof(header))) {
        return false;
    }

    // Send payload
    if (!write_all(fd_, data, size)) {
        return false;
    }

    return true;
}

bool NetworkClient::receive(std::vector<uint8_t>& buffer) {
    if (fd_ < 0) {
        std::cerr << "Not connected\n";
        return false;
    }

    // Receive header
    MessageHeader header;
    if (!read_all(fd_, &header, sizeof(header))) {
        return false;
    }

    // Validate magic
    if (header.magic != MESSAGE_MAGIC) {
        std::cerr << "Invalid message magic\n";
        return false;
    }

    // Receive payload
    buffer.resize(header.size);
    if (!read_all(fd_, buffer.data(), header.size)) {
        return false;
    }

    return true;
}

void NetworkClient::disconnect() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

} // namespace venus_plus
```

#### 2.4 Network Server

Create `common/network/network_server.h`:

```cpp
#ifndef VENUS_PLUS_NETWORK_SERVER_H
#define VENUS_PLUS_NETWORK_SERVER_H

#include <cstdint>
#include <functional>
#include <vector>

namespace venus_plus {

// Callback for handling client messages
// Parameters: client_fd, message_data, message_size
// Returns: true to continue, false to disconnect client
using ClientHandler = std::function<bool(int, const void*, size_t)>;

class NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();

    // Start server
    bool start(uint16_t port, const std::string& bind_addr = "0.0.0.0");

    // Run server (blocks until stopped)
    void run(ClientHandler handler);

    // Stop server
    void stop();

    // Send message to client
    static bool send_to_client(int client_fd, const void* data, size_t size);

private:
    void handle_client(int client_fd, ClientHandler handler);

    int server_fd_;
    bool running_;
};

} // namespace venus_plus

#endif // VENUS_PLUS_NETWORK_SERVER_H
```

Create `common/network/network_server.cpp`:

```cpp
#include "network_server.h"
#include "message.h"
#include "socket_utils.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <thread>

namespace venus_plus {

NetworkServer::NetworkServer() : server_fd_(-1), running_(false) {}

NetworkServer::~NetworkServer() {
    stop();
}

bool NetworkServer::start(uint16_t port, const std::string& bind_addr) {
    // Create socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "Failed to create server socket\n";
        return false;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt failed\n";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, bind_addr.c_str(), &addr.sin_addr);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Bind failed on port " << port << "\n";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Listen
    if (listen(server_fd_, 5) < 0) {
        std::cerr << "Listen failed\n";
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    std::cout << "Server listening on " << bind_addr << ":" << port << "\n";
    return true;
}

void NetworkServer::run(ClientHandler handler) {
    while (running_) {
        // Accept client
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) {
                std::cerr << "Accept failed\n";
            }
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "Client connected from " << client_ip << "\n";

        // Handle client in separate thread (for now, just inline)
        handle_client(client_fd, handler);
    }
}

void NetworkServer::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
}

void NetworkServer::handle_client(int client_fd, ClientHandler handler) {
    std::vector<uint8_t> buffer;

    while (true) {
        // Receive message header
        MessageHeader header;
        if (!read_all(client_fd, &header, sizeof(header))) {
            break;
        }

        // Validate magic
        if (header.magic != MESSAGE_MAGIC) {
            std::cerr << "Invalid message magic from client\n";
            break;
        }

        // Receive payload
        buffer.resize(header.size);
        if (!read_all(client_fd, buffer.data(), header.size)) {
            break;
        }

        // Call handler
        if (!handler(client_fd, buffer.data(), header.size)) {
            break;
        }
    }

    std::cout << "Client disconnected\n";
    close(client_fd);
}

bool NetworkServer::send_to_client(int client_fd, const void* data, size_t size) {
    // Send header
    MessageHeader header;
    header.magic = MESSAGE_MAGIC;
    header.size = size;

    if (!write_all(client_fd, &header, sizeof(header))) {
        return false;
    }

    // Send payload
    if (!write_all(client_fd, data, size)) {
        return false;
    }

    return true;
}

} // namespace venus_plus
```

#### 2.5 Common CMakeLists.txt

Create `common/CMakeLists.txt`:

```cmake
add_library(venus_common STATIC
    network/message.cpp
    network/socket_utils.cpp
    network/network_client.cpp
    network/network_server.cpp
)

target_include_directories(venus_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/venus-protocol
)

target_link_libraries(venus_common PUBLIC
    Vulkan::Vulkan
)
```

### Step 3: Client ICD (3-4 hours)

#### 3.1 ICD Entrypoint

Create `client/icd/icd_entrypoints.h`:

```cpp
#ifndef VENUS_PLUS_ICD_ENTRYPOINTS_H
#define VENUS_PLUS_ICD_ENTRYPOINTS_H

#include <vulkan/vulkan.h>

extern "C" {

// Global commands
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* pApiVersion);

} // extern "C"

#endif // VENUS_PLUS_ICD_ENTRYPOINTS_H
```

Create `client/icd/icd_entrypoints.cpp`:

```cpp
#include "icd_entrypoints.h"
#include "network/network_client.h"
#include <iostream>
#include <vector>
#include <cstring>

// For Phase 1, we'll use a simple global connection
static venus_plus::NetworkClient g_client;
static bool g_connected = false;

static bool ensure_connected() {
    if (!g_connected) {
        // TODO: Get host/port from env variable
        if (!g_client.connect("127.0.0.1", 5556)) {
            return false;
        }
        g_connected = true;
    }
    return true;
}

extern "C" {

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* pApiVersion) {
    std::cout << "[Client] vkEnumerateInstanceVersion called\n";

    if (!ensure_connected()) {
        std::cerr << "[Client] Failed to connect to server\n";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // For Phase 1: Send a simple encoded message
    // Format: [command_type: uint32_t]
    uint32_t command_type = 1;  // Arbitrary ID for EnumerateInstanceVersion

    if (!g_client.send(&command_type, sizeof(command_type))) {
        std::cerr << "[Client] Failed to send command\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::cout << "[Client] Sent command\n";

    // Receive reply
    std::vector<uint8_t> reply;
    if (!g_client.receive(reply)) {
        std::cerr << "[Client] Failed to receive reply\n";
        return VK_ERROR_DEVICE_LOST;
    }

    std::cout << "[Client] Received reply: " << reply.size() << " bytes\n";

    // Decode reply: [result: uint32_t][version: uint32_t]
    if (reply.size() < 8) {
        std::cerr << "[Client] Invalid reply size\n";
        return VK_ERROR_DEVICE_LOST;
    }

    uint32_t result;
    uint32_t version;
    memcpy(&result, reply.data(), 4);
    memcpy(&version, reply.data() + 4, 4);

    if (result != VK_SUCCESS) {
        return static_cast<VkResult>(result);
    }

    *pApiVersion = version;
    std::cout << "[Client] Version: " << version << "\n";

    return VK_SUCCESS;
}

} // extern "C"
```

#### 3.2 ICD Manifest

Create `client/venus_icd.json.in`:

```json
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "@CMAKE_CURRENT_BINARY_DIR@/libvenus_icd.so",
        "api_version": "1.3.0"
    }
}
```

#### 3.3 Client CMakeLists.txt

Create `client/CMakeLists.txt`:

```cmake
add_library(venus_icd SHARED
    icd/icd_entrypoints.cpp
)

target_link_libraries(venus_icd PRIVATE
    venus_common
    Vulkan::Vulkan
)

# Generate ICD manifest
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/venus_icd.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/venus_icd.x86_64.json
    @ONLY
)
```

### Step 4: Server (2-3 hours)

Create `server/main.cpp`:

```cpp
#include "network/network_server.h"
#include <iostream>
#include <cstring>
#include <vector>
#include <vulkan/vulkan.h>

using namespace venus_plus;

bool handle_client_message(int client_fd, const void* data, size_t size) {
    std::cout << "[Server] Received message: " << size << " bytes\n";

    // Decode command type
    if (size < 4) {
        std::cerr << "[Server] Message too small\n";
        return false;
    }

    uint32_t command_type;
    memcpy(&command_type, data, 4);

    std::cout << "[Server] Command type: " << command_type << "\n";

    // Handle vkEnumerateInstanceVersion (command_type == 1)
    if (command_type == 1) {
        // Create fake reply: [result: uint32_t][version: uint32_t]
        uint32_t reply_data[2];
        reply_data[0] = VK_SUCCESS;
        reply_data[1] = VK_API_VERSION_1_3;  // Version 1.3.0

        std::cout << "[Server] Sending reply: VK_API_VERSION_1_3\n";

        if (!NetworkServer::send_to_client(client_fd, reply_data, sizeof(reply_data))) {
            std::cerr << "[Server] Failed to send reply\n";
            return false;
        }

        return true;  // Continue handling this client
    }

    std::cerr << "[Server] Unknown command type: " << command_type << "\n";
    return false;
}

int main(int argc, char** argv) {
    std::cout << "Venus Plus Server v0.1\n";
    std::cout << "======================\n\n";

    NetworkServer server;

    if (!server.start(5556)) {
        std::cerr << "Failed to start server\n";
        return 1;
    }

    std::cout << "Waiting for clients...\n\n";

    server.run(handle_client_message);

    return 0;
}
```

Create `server/CMakeLists.txt`:

```cmake
add_executable(venus-server
    main.cpp
)

target_link_libraries(venus-server PRIVATE
    venus_common
    Vulkan::Vulkan
)
```

### Step 5: Test Application (1-2 hours)

Create `test-app/phase01/phase01_test.h`:

```cpp
#ifndef PHASE01_TEST_H
#define PHASE01_TEST_H

namespace phase01 {

int run_test();

} // namespace phase01

#endif // PHASE01_TEST_H
```

Create `test-app/phase01/phase01_test.cpp`:

```cpp
#include "phase01_test.h"
#include <vulkan/vulkan.h>
#include <iostream>

namespace phase01 {

int run_test() {
    std::cout << "\n";
    std::cout << "=================================================\n";
    std::cout << "Phase 1: Network Communication\n";
    std::cout << "=================================================\n\n";

    // Test vkEnumerateInstanceVersion
    uint32_t version = 0;
    VkResult result = vkEnumerateInstanceVersion(&version);

    if (result != VK_SUCCESS) {
        std::cerr << "❌ FAILED: vkEnumerateInstanceVersion returned " << result << "\n";
        return 1;
    }

    uint32_t major = VK_VERSION_MAJOR(version);
    uint32_t minor = VK_VERSION_MINOR(version);
    uint32_t patch = VK_VERSION_PATCH(version);

    std::cout << "✅ vkEnumerateInstanceVersion returned: "
              << major << "." << minor << "." << patch << "\n";

    std::cout << "\n";
    std::cout << "✅ Phase 1 PASSED\n";
    std::cout << "=================================================\n\n";

    return 0;
}

} // namespace phase01
```

Create `test-app/main.cpp`:

```cpp
#include "phase01/phase01_test.h"
#include <iostream>
#include <cstring>

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  --phase N    Run phase N test\n";
    std::cout << "  --all        Run all available phases\n";
    std::cout << "  --help       Show this help\n";
}

int main(int argc, char** argv) {
    std::cout << "Venus Plus Test Application\n";

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "--phase") == 0) {
        if (argc < 3) {
            std::cerr << "Error: --phase requires phase number\n";
            return 1;
        }

        int phase = atoi(argv[2]);

        switch (phase) {
        case 1:
            return phase01::run_test();
        default:
            std::cerr << "Error: Phase " << phase << " not implemented yet\n";
            return 1;
        }
    }

    if (strcmp(argv[1], "--all") == 0) {
        // Run all phases
        int result = phase01::run_test();
        if (result != 0) return result;

        std::cout << "\n";
        std::cout << "=================================================\n";
        std::cout << "All phases completed successfully!\n";
        std::cout << "=================================================\n";
        return 0;
    }

    print_usage(argv[0]);
    return 1;
}
```

Create `test-app/CMakeLists.txt`:

```cmake
add_executable(venus-test-app
    main.cpp
    phase01/phase01_test.cpp
)

target_include_directories(venus-test-app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(venus-test-app PRIVATE
    Vulkan::Vulkan
)
```

## Testing

### Build

```bash
cd /home/ayman/venus-plus
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

### Run

**Terminal 1** - Start server:
```bash
cd /home/ayman/venus-plus/build
./server/venus-server
```

Expected output:
```
Venus Plus Server v0.1
======================

Server listening on 0.0.0.0:5556
Waiting for clients...
```

**Terminal 2** - Run test:
```bash
cd /home/ayman/venus-plus/build
export VK_DRIVER_FILES=$(pwd)/client/venus_icd.x86_64.json
./test-app/venus-test-app --phase 1
```

Expected output:
```
Venus Plus Test Application

=================================================
Phase 1: Network Communication
=================================================

[Client] vkEnumerateInstanceVersion called
Connected to 127.0.0.1:5556
[Client] Sent command
[Client] Received reply: 8 bytes
[Client] Version: 4206596
✅ vkEnumerateInstanceVersion returned: 1.3.0

✅ Phase 1 PASSED
=================================================
```

## Success Criteria

- [ ] Project builds without errors
- [ ] Server starts and listens on port 5556
- [ ] Client connects to server
- [ ] Client sends encoded message
- [ ] Server receives and decodes message
- [ ] Server sends reply
- [ ] Client receives and decodes reply
- [ ] Test app prints "✅ Phase 1 PASSED"

## Deliverables

- [ ] Network client and server classes
- [ ] Message protocol implementation
- [ ] ICD stub with `vkEnumerateInstanceVersion`
- [ ] Server with command handler
- [ ] Test application
- [ ] All code compiles and runs
- [ ] Documentation updated

## Next Steps

Once Phase 1 is complete and all tests pass, proceed to [PHASE_02.md](PHASE_02.md) to implement instance creation.

## Troubleshooting

See [BUILD_AND_RUN.md](BUILD_AND_RUN.md) for common issues and solutions.
