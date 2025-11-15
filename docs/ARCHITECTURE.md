# Venus Plus Architecture

**Comprehensive system design documentation**

## Table of Contents

1. [Overview](#overview)
2. [System Components](#system-components)
3. [Communication Flow](#communication-flow)
4. [Venus Protocol Integration](#venus-protocol-integration)
5. [Network Layer](#network-layer)
6. [Handle Mapping](#handle-mapping)
7. [Memory Management](#memory-management)
8. [Synchronization](#synchronization)
9. [Error Handling](#error-handling)
10. [Performance Considerations](#performance-considerations)

## Overview

Venus Plus implements a client-server architecture where Vulkan API calls are transparently forwarded from a client application to a remote GPU server over TCP/IP.

### Design Principles

1. **Transparency**: Applications use standard Vulkan API without modification
2. **Incremental Complexity**: Build from simple to complex, testing at each step
3. **Protocol Reuse**: Leverage Venus protocol for serialization (don't reinvent the wheel)
4. **Separation of Concerns**: Clear boundaries between ICD, network, and server layers
5. **Testability**: Every component is independently testable

## System Components

### High-Level Architecture

```
┌───────────────────────────────────────────────────────────────────┐
│                        Application                                │
│                     (Unmodified Vulkan App)                       │
└────────────────────────────┬──────────────────────────────────────┘
                             │ Vulkan API Calls
                             │ (vkCreateDevice, vkQueueSubmit, etc.)
                             ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Venus Plus ICD (Client)                       │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │                  Vulkan Entrypoints Layer                    │ │
│ │  vkCreateInstance, vkCreateDevice, vkQueueSubmit, etc.      │ │
│ └──────────────────────────┬──────────────────────────────────┘ │
│                            │                                     │
│ ┌──────────────────────────▼──────────────────────────────────┐ │
│ │              Command Encoding Layer                         │ │
│ │  - Venus protocol encoding                                  │ │
│ │  - Parameter validation                                     │ │
│ │  - Handle translation (client → server)                     │ │
│ └──────────────────────────┬──────────────────────────────────┘ │
│                            │                                     │
│ ┌──────────────────────────▼──────────────────────────────────┐ │
│ │                 Network Layer                               │ │
│ │  - TCP socket management                                    │ │
│ │  - Send/receive primitives                                  │ │
│ │  - Reconnection logic                                       │ │
│ └──────────────────────────┬──────────────────────────────────┘ │
└────────────────────────────┼──────────────────────────────────────┘
                             │
                             │ TCP/IP Network
                             │ (Encoded Venus Protocol Messages)
                             │
┌────────────────────────────▼──────────────────────────────────────┐
│                  Venus Plus Server                                │
│ ┌─────────────────────────────────────────────────────────────┐  │
│ │                 Network Layer                               │  │
│ │  - TCP server socket                                        │  │
│ │  - Client connection management                             │  │
│ │  - Receive/send primitives                                  │  │
│ └──────────────────────────┬──────────────────────────────────┘  │
│                            │                                      │
│ ┌──────────────────────────▼──────────────────────────────────┐  │
│ │              Command Decoding Layer                         │  │
│ │  - Venus protocol decoding                                  │  │
│ │  - Handle translation (client → real GPU handles)           │  │
│ │  - Command validation                                       │  │
│ └──────────────────────────┬──────────────────────────────────┘  │
│                            │                                      │
│ ┌──────────────────────────▼──────────────────────────────────┐  │
│ │              Execution Layer                                │  │
│ │  - Call real Vulkan API                                     │  │
│ │  - State management                                         │  │
│ │  - Resource tracking                                        │  │
│ └──────────────────────────┬──────────────────────────────────┘  │
└────────────────────────────┼───────────────────────────────────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │   Real GPU      │
                    │  (NVIDIA, AMD,  │
                    │   Intel, etc.)  │
                    └─────────────────┘
```

## Communication Flow

### Example: `vkCreateBuffer` Flow

```
CLIENT SIDE                           NETWORK                    SERVER SIDE
───────────                           ───────                    ───────────

App calls:
vkCreateBuffer(device, pCreateInfo, NULL, pBuffer)
    │
    ▼
ICD intercepts:
├─ Allocate client handle: buffer_123
├─ Encode using Venus:
│  ├─ Command type: VK_COMMAND_TYPE_vkCreateBuffer_EXT
│  ├─ Device handle: device (client)
│  ├─ VkBufferCreateInfo: size, usage, etc.
│  └─ Buffer handle: buffer_123
├─ Calculate message size
└─ Send over TCP ──────────────►  Receive TCP data
                                      │
                                      ▼
                                  Decode message:
                                  ├─ Extract command type
                                  ├─ Decode device handle
                                  │  └─ Map to real device
                                  ├─ Decode VkBufferCreateInfo
                                  └─ Extract client buffer handle
                                      │
                                      ▼
                                  Execute:
                                  VkBuffer real_buffer;
                                  result = vkCreateBuffer(
                                      real_device,
                                      pCreateInfo,
                                      NULL,
                                      &real_buffer
                                  );
                                      │
                                      ▼
                                  Store mapping:
                                  buffer_map[buffer_123] = real_buffer
                                      │
                                      ▼
                                  Encode reply:
                                  ├─ Command type
                                  ├─ VkResult: result
Receive TCP reply ◄────────────── └─ Send over TCP
    │
    ▼
Decode reply:
├─ Extract VkResult
└─ Return to application
    │
    ▼
*pBuffer = buffer_123
return result
```

### Message Format

Every message follows this structure:

```
┌─────────────────────────────────────────┐
│         Message Header (8 bytes)        │
│  ┌────────────┬────────────────────────┐│
│  │ Magic (4B) │ Message Size (4B)      ││
│  │ 0x56504C53 │ Total payload size     ││
│  └────────────┴────────────────────────┘│
└─────────────────────────────────────────┘
┌─────────────────────────────────────────┐
│    Venus Protocol Encoded Payload       │
│  ┌────────────────────────────────────┐ │
│  │ Command Type (VkCommandTypeEXT)    │ │
│  ├────────────────────────────────────┤ │
│  │ Command Flags                      │ │
│  ├────────────────────────────────────┤ │
│  │ Command Parameters                 │ │
│  │ (Venus protocol encoded)           │ │
│  │ - Handles                          │ │
│  │ - Structures                       │ │
│  │ - Arrays                           │ │
│  │ - pNext chains                     │ │
│  └────────────────────────────────────┘ │
└─────────────────────────────────────────┘
```

## Venus Protocol Integration

### Why Venus Protocol?

1. **Complete**: Covers all Vulkan commands and structures
2. **Maintained**: Auto-generated from Vulkan XML specs
3. **Proven**: Used in production (Chrome OS, Android virtualization)
4. **Efficient**: Binary format optimized for GPU workloads
5. **Versioned**: Handles Vulkan API evolution

### Integration Approach

**Venus Protocol Files** (copied from Mesa):
- Located in: `common/venus-protocol/`
- Generated from: `https://gitlab.freedesktop.org/virgl/venus-protocol`
- Files copied:
  - `vn_protocol_driver.h` - Main header
  - `vn_protocol_driver_*.h` - Command-specific headers
  - `vn_protocol_driver_types.h` - Type definitions
  - `vn_protocol_driver_cs.h` - Command stream primitives

**Our Wrapper** (`common/protocol-helpers/`):
```cpp
// Simplified encoding wrapper
class VenusEncoder {
public:
    VenusEncoder(size_t initial_size = 4096);

    // Encode any Vulkan command
    template<typename... Args>
    void encode(VkCommandTypeEXT cmd_type, Args&&... args);

    // Get encoded data
    const void* data() const;
    size_t size() const;

private:
    vn_cs_encoder encoder_;
    std::vector<uint8_t> buffer_;
};

// Simplified decoding wrapper
class VenusDecoder {
public:
    VenusDecoder(const void* data, size_t size);

    // Decode command type
    VkCommandTypeEXT decode_command_type();

    // Decode any Vulkan command
    template<typename... Args>
    void decode(Args&... args);

private:
    vn_cs_decoder decoder_;
};
```

### Example Usage

**Client encoding:**
```cpp
// In vkCreateBuffer implementation
VenusEncoder enc;
enc.encode(VK_COMMAND_TYPE_vkCreateBuffer_EXT,
           cmd_flags,
           device,
           pCreateInfo,
           pAllocator,
           pBuffer);

network_send(enc.data(), enc.size());
```

**Server decoding:**
```cpp
// Receive message
auto msg = network_receive();

VenusDecoder dec(msg.data(), msg.size());
VkCommandTypeEXT cmd_type = dec.decode_command_type();

switch (cmd_type) {
case VK_COMMAND_TYPE_vkCreateBuffer_EXT:
    handle_vkCreateBuffer(dec);
    break;
// ... other commands
}
```

## Network Layer

### TCP Socket Management

**Client Side:**
```cpp
class NetworkClient {
public:
    // Connect to server
    bool connect(const std::string& host, uint16_t port);

    // Send encoded command
    bool send(const void* data, size_t size);

    // Receive reply
    bool receive(std::vector<uint8_t>& buffer);

    // Disconnect
    void disconnect();

private:
    int socket_fd_;
    std::mutex socket_mutex_;  // Thread-safe socket access
};
```

**Server Side:**
```cpp
class NetworkServer {
public:
    // Start server
    bool start(uint16_t port);

    // Accept client connections
    void run();

    // Handle client (runs in thread)
    void handle_client(int client_fd);

private:
    int server_fd_;
    std::vector<std::thread> client_threads_;
};
```

### Message Protocol

**Send Operation:**
```cpp
bool send_message(int fd, const void* data, size_t size) {
    // 1. Send header
    MessageHeader header;
    header.magic = VENUS_PLUS_MAGIC;  // 0x56504C53 "VPLS"
    header.size = size;

    if (write_all(fd, &header, sizeof(header)) != sizeof(header))
        return false;

    // 2. Send payload
    if (write_all(fd, data, size) != size)
        return false;

    return true;
}
```

**Receive Operation:**
```cpp
bool receive_message(int fd, std::vector<uint8_t>& buffer) {
    // 1. Receive header
    MessageHeader header;
    if (read_all(fd, &header, sizeof(header)) != sizeof(header))
        return false;

    // 2. Validate magic
    if (header.magic != VENUS_PLUS_MAGIC)
        return false;

    // 3. Receive payload
    buffer.resize(header.size);
    if (read_all(fd, buffer.data(), header.size) != header.size)
        return false;

    return true;
}
```

## Handle Mapping

### The Handle Problem

Vulkan uses opaque handles (VkDevice, VkBuffer, etc.). Client and server have different handle spaces:
- Client allocates handles locally
- Server gets real handles from GPU driver

**We need bidirectional mapping:**
- Client → Server: When sending commands
- Server → Client: When creating objects

### Handle Mapping Strategy

**Client Side:**
```cpp
class ClientHandleAllocator {
public:
    // Allocate a new client-side handle
    template<typename T>
    T allocate() {
        static std::atomic<uint64_t> counter{1};
        return reinterpret_cast<T>(counter.fetch_add(1));
    }

    // No mapping needed on client - we only allocate
};
```

**Server Side:**
```cpp
class ServerHandleMap {
public:
    // Store mapping: client_handle → real_handle
    template<typename T>
    void insert(T client_handle, T real_handle) {
        auto key = handle_to_uint64(client_handle);
        map_[key] = handle_to_uint64(real_handle);
    }

    // Retrieve real handle from client handle
    template<typename T>
    T get_real(T client_handle) const {
        auto key = handle_to_uint64(client_handle);
        auto it = map_.find(key);
        if (it == map_.end())
            return VK_NULL_HANDLE;
        return uint64_to_handle<T>(it->second);
    }

    // Remove mapping (on destroy)
    template<typename T>
    void remove(T client_handle) {
        map_.erase(handle_to_uint64(client_handle));
    }

private:
    std::unordered_map<uint64_t, uint64_t> map_;
    std::mutex mutex_;  // Thread-safe access
};
```

### Handle Translation Example

**Server handling vkCreateBuffer:**
```cpp
void Server::handle_vkCreateBuffer(VenusDecoder& dec) {
    // 1. Decode parameters
    VkDevice client_device;
    VkBufferCreateInfo create_info;
    VkBuffer client_buffer;  // Allocated by client

    dec.decode(client_device, create_info, client_buffer);

    // 2. Translate device handle
    VkDevice real_device = handle_map_.get_real(client_device);

    // 3. Call real Vulkan
    VkBuffer real_buffer;
    VkResult result = vkCreateBuffer(real_device, &create_info,
                                     nullptr, &real_buffer);

    // 4. Store mapping
    if (result == VK_SUCCESS) {
        handle_map_.insert(client_buffer, real_buffer);
    }

    // 5. Send reply
    VenusEncoder enc;
    enc.encode_reply(VK_COMMAND_TYPE_vkCreateBuffer_EXT, result);
    network_send(enc.data(), enc.size());
}
```

## Memory Management

### Challenge: Memory Cannot Be Shared Over Network

Unlike local Vulkan where client and GPU share memory, network Vulkan requires explicit transfers.

### Memory Transfer Strategy

**Phase 1 (Early phases): Anonymous Memory**
- Client allocates anonymous memory (malloc/mmap)
- On vkMapMemory: Client prepares to send data
- On vkUnmapMemory: Client sends data to server
- Server maintains corresponding memory

**Phase 2 (Later phases): Optimized Transfer**
- Lazy transfer: Only send when needed (before GPU read)
- Incremental transfer: Send only modified regions
- Compression: Compress texture data
- Caching: Cache frequently used resources

### Memory Transfer Protocol

**Map/Unmap Sequence:**
```
Client                              Server
──────                              ──────

vkMapMemory(memory, offset, size, &ptr)
    │
    ├─ Encode vkMapMemory command
    ├─ Send to server ─────────────►  Decode vkMapMemory
    │                                  │
    │                                  ├─ Map real memory
    │                                  ├─ Return success
    │                              ◄───┤ Send reply
    │
    ├─ Receive reply
    ├─ Allocate local shadow buffer
    └─ Return local pointer to app

App writes data to local pointer
memcpy(ptr, data, size);

vkUnmapMemory(memory)
    │
    ├─ Encode TRANSFER_MEMORY_DATA
    │  ├─ Memory handle
    │  ├─ Offset
    │  ├─ Size
    │  └─ Data
    ├─ Send to server ─────────────►  Decode TRANSFER_MEMORY_DATA
    │                                  │
    │                                  ├─ Get real memory pointer
    │                                  ├─ memcpy data to real memory
    │                                  ├─ vkUnmapMemory(real_memory)
    │                              ◄───┤ Send reply
    │
    └─ Free local shadow buffer
```

### Resource Transfer Commands

**Custom commands (extension to Venus protocol):**
```cpp
enum VenusPlus_CommandTypeEXT {
    VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA = 0x10000000,
    VENUS_PLUS_CMD_TRANSFER_IMAGE_DATA,
    VENUS_PLUS_CMD_QUERY_MEMORY_DATA,
};

struct TransferMemoryDataCmd {
    VkDeviceMemory memory;
    VkDeviceSize offset;
    VkDeviceSize size;
    uint8_t data[];  // Variable length
};
```

## Synchronization

### Synchronization Challenges

1. **Timeline Semaphores**: Need value queries across network
2. **Fences**: Need signal status queries
3. **Events**: Host-set events need synchronization
4. **Query Pools**: Need result retrieval

### Synchronization Strategy

**Early Phases (Fake execution):**
- All syncs immediately signaled
- No real waiting

**Later Phases (Real execution):**
- Client polls server for sync status
- Server maintains sync object state
- Use async queries to avoid blocking

**Fence Example:**
```
Client                              Server
──────                              ──────

vkQueueSubmit(..., fence)
    │
    ├─ Send command ───────────────►  vkQueueSubmit(real_queue, real_fence)
    │                                  │
    │                              ◄───┤ Send reply (immediate)
    │
    └─ Return VK_SUCCESS

vkWaitForFences(fence, timeout)
    │
    ├─ Send QUERY_FENCE_STATUS ───►  vkGetFenceStatus(real_fence)
    │                                  │
    │                              ◄───┤ Return status
    │
    ├─ If not signaled, poll again
    └─ Return when signaled
```

## Error Handling

### Error Categories

1. **Network Errors**: Connection lost, timeout
2. **Protocol Errors**: Invalid message, decode failure
3. **Vulkan Errors**: VK_ERROR_OUT_OF_MEMORY, etc.

### Error Handling Strategy

**Network Errors:**
```cpp
// Client loses connection
if (network_send_failed) {
    // Log error
    // Return VK_ERROR_DEVICE_LOST to application
    // Attempt reconnection in background
}
```

**Protocol Errors:**
```cpp
// Invalid message received
if (invalid_message) {
    // Log error
    // Disconnect client
    // Return VK_ERROR_DEVICE_LOST
}
```

**Vulkan Errors:**
```cpp
// Server encounters Vulkan error
VkResult result = vkCreateBuffer(...);
if (result != VK_SUCCESS) {
    // Encode error in reply
    // Send back to client
    // Client returns same error to app
}
```

## Performance Considerations

### Latency Optimization

1. **Batching**: Batch multiple commands in single network message
2. **Async Submission**: Don't wait for server reply on async commands
3. **Command Buffering**: Buffer command recording, send on submit
4. **Predictive Transfer**: Transfer resources before they're needed

### Bandwidth Optimization

1. **Compression**: Compress image/buffer data
2. **Delta Encoding**: Send only changed data
3. **Resource Caching**: Cache immutable resources server-side
4. **Smart Culling**: Don't send invisible/unused resources

### Future Optimizations

- UDP for loss-tolerant data (query results)
- Multiple TCP connections for parallelism
- Zero-copy networking where possible
- GPU-direct RDMA for high-performance networks

## Security Considerations

### Authentication
- Client authentication (future)
- Server access control (future)

### Validation
- Server validates all commands
- Resource access checks
- Memory bound checks

### Sandboxing
- Server runs with limited privileges
- Resource limits per client

---

**Next Steps**: See [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md) for the phase-by-phase implementation plan.
