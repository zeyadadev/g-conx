# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Venus Plus is a network-based Vulkan ICD (Installable Client Driver) that enables remote GPU rendering over TCP/IP. It consists of:

- **Client (ICD)**: Intercepts Vulkan API calls, encodes them using the Venus protocol, and transmits over TCP/IP
- **Server**: Receives encoded commands, executes them on a real GPU, and returns results

**Current Status**: Documentation phase complete. Implementation not yet started. Ready to begin Phase 1.

## Build Commands

### Initial Build
```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Build Options
```bash
# Debug build (default)
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release build
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build with tests
cmake -DBUILD_TESTING=ON ..

# Build specific targets
make venus_icd        # Client ICD only
make venus_server     # Server only
make venus_test_app   # Test app only
```

### Running Tests
```bash
# Unit tests
ctest --output-on-failure

# Specific unit tests
./client/tests/test_venus_encoder
./server/tests/test_decoder

# Phase tests (end-to-end)
./test-app/venus-test-app --phase 1
./test-app/venus-test-app --all
```

### Running the System

**Terminal 1 - Server:**
```bash
./server/venus-server --port 5556
```

**Terminal 2 - Client Application:**
```bash
export VK_DRIVER_FILES=$(pwd)/client/venus_icd.x86_64.json
./test-app/venus-test-app --phase 1
```

## Architecture

### High-Level Design

```
Application → Venus Plus ICD (Client) ↔ TCP/IP ↔ Venus Plus Server → Real GPU
```

### Component Layers

**Client (ICD):**
1. Vulkan Entrypoints Layer - Standard Vulkan API functions
2. Command Encoding Layer - Venus protocol encoding, handle translation
3. Network Layer - TCP socket management, message protocol

**Server:**
1. Network Layer - TCP server, client connection management
2. Command Decoding Layer - Venus protocol decoding, handle mapping
3. Execution Layer - Real Vulkan API calls, resource tracking

### Directory Structure

```
venus-plus/
├── common/              # Shared code between client and server
│   ├── venus-protocol/  # Venus protocol headers from Mesa (auto-generated)
│   ├── protocol-helpers/# C++ wrappers: VenusEncoder, VenusDecoder classes
│   ├── network/         # TCP networking: NetworkClient, NetworkServer classes
│   └── utils/           # Logging, handle mapping utilities
│
├── client/              # Vulkan ICD implementation
│   ├── icd/             # Core ICD: vkGetInstanceProcAddr, dispatch tables
│   ├── commands/        # Vulkan command implementations by category:
│   │                    # instance.cpp, device.cpp, queue.cpp, resources.cpp, etc.
│   ├── state/           # Client-side state: handle allocator, instance/device state
│   └── tests/           # Unit tests for encoding, handle allocation
│
├── server/              # Server implementation
│   ├── decoder/         # Venus protocol decoder and command dispatcher
│   ├── executor/        # Vulkan execution: instance, device, queue, resource executors
│   ├── state/           # Server-side state: handle mapping (client ↔ real GPU)
│   └── tests/           # Unit tests for decoding, handle mapping
│
├── test-app/            # Progressive test application (C++)
│   ├── phase01/ ... phase10/  # Phase-specific test implementations
│   ├── utils/           # Test helpers, shader loaders
│   └── shaders/         # GLSL shaders for graphics phases
│
└── docs/                # Comprehensive documentation
```

### Venus Protocol Integration

**Why Venus?**
- Complete coverage of all Vulkan commands
- Auto-generated from Vulkan XML specs
- Battle-tested (Chrome OS, Android virtualization)
- Efficient binary format

**Key Files:**
- `common/venus-protocol/vn_protocol_driver.h` - Main protocol header
- `common/venus-protocol/vn_protocol_driver_cs.h` - Command stream primitives
- `common/venus-protocol/vn_protocol_driver_types.h` - Type definitions

**Source:** Copied from Mesa 3D (`src/virtio/venus-protocol/`)

**Wrappers:**
- `VenusEncoder` class - Simplifies encoding Vulkan commands
- `VenusDecoder` class - Simplifies decoding and extracting parameters

### Message Protocol

Every network message follows this format:
```
┌─────────────────────────────────────┐
│  Header (8 bytes)                   │
│  ├─ Magic: 0x56504C53 (4 bytes)    │
│  └─ Size: Payload size (4 bytes)   │
├─────────────────────────────────────┤
│  Venus Encoded Payload              │
│  ├─ Command Type (VkCommandTypeEXT) │
│  ├─ Command Flags                   │
│  └─ Command Parameters              │
└─────────────────────────────────────┘
```

### Handle Mapping

**Problem:** Client and server have different handle address spaces.

**Solution:**
- **Client**: Allocates sequential handles (simple counter)
- **Server**: Maintains bidirectional map: `client_handle ↔ real_gpu_handle`
- **Translation**: Server translates client handles to real handles before GPU calls

Example:
```cpp
// Client allocates: VkBuffer client_buffer = 0x123
// Server creates real buffer and maps:
//   handle_map[0x123] = real_buffer_from_gpu
```

## Development Approach

### 10-Phase Incremental Plan

Venus Plus is developed in **10 phases over ~90 days**. Each phase builds on the previous and maintains end-to-end functionality.

| Phase | Focus | Duration | Key Goal |
|-------|-------|----------|----------|
| 1 | Network + Venus basics | Days 1-3 | TCP communication with Venus encoding/decoding |
| 2 | Fake instance | Days 4-7 | Instance creation with fake data returned |
| 3 | Fake device | Days 8-14 | Logical device + queues (fake) |
| 4 | Fake resources | Days 15-21 | Buffer/Image creation (no real GPU) |
| 5 | Fake commands | Days 22-28 | Command buffer recording (fake) |
| 6 | Fake submission | Days 29-35 | Queue submission (immediately signaled) |
| 7 | Real execution | Days 36-49 | Real GPU execution for simple commands |
| 8 | Memory transfer | Days 50-60 | Network transfer of buffer/image data |
| 9 | Compute shaders | Days 61-75 | Execute compute shaders on remote GPU |
| 10 | Graphics rendering | Days 76-90 | Full graphics pipeline rendering |

**Key Principle:** "Always have something working" - every phase delivers a complete, testable system.

### Phase Documentation

Each phase has detailed documentation in `docs/PHASE_XX.md`:
- Objectives and deliverables
- Technical specifications
- Implementation steps
- Testing requirements
- Example code

### Testing Strategy

**4-Layer Testing:**

1. **Foundation Unit Tests** (`common/tests/`)
   - Venus encoder/decoder
   - Network message protocol
   - Handle mapping utilities

2. **Component Unit Tests** (`client/tests/`, `server/tests/`)
   - Client: encoding, handle allocation, state management
   - Server: decoding, handle mapping, execution

3. **Integration Tests** (`test-app/integration/`)
   - Client + server together
   - Message round-trips, error handling

4. **End-to-End Phase Tests** (`test-app/phaseXX/`)
   - Complete Vulkan workflows
   - Growing complexity across phases
   - Regression testing of all previous phases

## Implementation Guidelines

### When Implementing Client Commands

1. **Allocate client handles** (for create functions like vkCreateBuffer)
2. **Encode command** using VenusEncoder with all parameters
3. **Send over network** using NetworkClient
4. **Receive reply** and decode VkResult
5. **Return result** to application

Example pattern:
```cpp
VkResult vkCreateBuffer(VkDevice device,
                        const VkBufferCreateInfo* pCreateInfo,
                        const VkAllocationCallbacks* pAllocator,
                        VkBuffer* pBuffer) {
    // 1. Allocate client handle
    *pBuffer = handle_allocator.allocate<VkBuffer>();

    // 2. Encode
    VenusEncoder enc;
    enc.encode(VK_COMMAND_TYPE_vkCreateBuffer_EXT, device, pCreateInfo, *pBuffer);

    // 3. Send
    network_client.send(enc.data(), enc.size());

    // 4. Receive reply
    auto reply = network_client.receive();
    VenusDecoder dec(reply.data(), reply.size());
    VkResult result = dec.decode_result();

    // 5. Return
    return result;
}
```

### When Implementing Server Handlers

1. **Decode parameters** using VenusDecoder
2. **Translate client handles** to real GPU handles using handle_map
3. **Call real Vulkan API** with real handles
4. **Store mapping** (for create functions)
5. **Encode reply** with VkResult
6. **Send reply** back to client

Example pattern:
```cpp
void handle_vkCreateBuffer(VenusDecoder& dec) {
    // 1. Decode
    VkDevice client_device;
    VkBufferCreateInfo create_info;
    VkBuffer client_buffer;
    dec.decode(client_device, create_info, client_buffer);

    // 2. Translate
    VkDevice real_device = handle_map.get_real(client_device);

    // 3. Call real Vulkan
    VkBuffer real_buffer;
    VkResult result = vkCreateBuffer(real_device, &create_info, nullptr, &real_buffer);

    // 4. Store mapping
    if (result == VK_SUCCESS) {
        handle_map.insert(client_buffer, real_buffer);
    }

    // 5. Encode reply
    VenusEncoder enc;
    enc.encode_reply(VK_COMMAND_TYPE_vkCreateBuffer_EXT, result);

    // 6. Send
    network_server.send(enc.data(), enc.size());
}
```

### Error Handling

**Network Errors** → Return `VK_ERROR_DEVICE_LOST`
**Protocol Errors** (invalid message) → Disconnect client, return `VK_ERROR_DEVICE_LOST`
**Vulkan Errors** → Pass through to client unchanged

### Memory Management

**Early Phases (1-7):**
- No memory transfer
- Fake/empty memory operations

**Phase 8+:**
- Client sends memory data on vkUnmapMemory
- Server receives and writes to real GPU memory
- Custom command: `VENUS_PLUS_CMD_TRANSFER_MEMORY_DATA`

### Synchronization

**Early Phases (1-6):**
- All syncs immediately signaled
- Fences always signaled
- Semaphores never wait

**Phase 7+:**
- Client polls server for sync status
- Server tracks real fence/semaphore states
- vkWaitForFences sends `QUERY_FENCE_STATUS` command

## Prerequisites and Dependencies

**System:**
- Linux (Ubuntu 20.04+, Fedora 34+)
- CMake 3.20+
- C++17 compiler (GCC 9+ or Clang 10+)

**Libraries:**
- Vulkan SDK 1.3+ (`libvulkan-dev`)
- Google Test (for unit tests)

**Setup:**
```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake git pkg-config libvulkan-dev vulkan-validationlayers vulkan-tools libgtest-dev

# Copy Venus protocol headers from Mesa
mkdir -p common/venus-protocol
cp /path/to/mesa/src/virtio/venus-protocol/*.h common/venus-protocol/
```

## Common Development Tasks

### Adding a New Vulkan Command

1. **Add client implementation** in `client/commands/<category>.cpp`
2. **Add server handler** in `server/executor/<category>.cpp`
3. **Wire up in command dispatcher** (`server/decoder/dispatcher.cpp`)
4. **Add unit tests** for encoding/decoding
5. **Add phase test** that uses the command

### Updating Venus Protocol

```bash
# Pull latest Mesa
cd /path/to/mesa
git pull

# Copy updated headers
cp src/virtio/venus-protocol/*.h /home/ayman/venus-plus/common/venus-protocol/

# Update version info in common/venus-protocol/README.md
# Rebuild
cd /home/ayman/venus-plus/build
cmake .. && make -j$(nproc)
```

### Debugging Network Issues

```bash
# Server side - verbose logging
./server/venus-server --log-level DEBUG

# Client side - enable debug logging
export VENUS_CLIENT_LOG_LEVEL=DEBUG

# Network analysis
tcpdump -i lo port 5556 -X

# Check connections
netstat -tuln | grep 5556
lsof -i :5556
```

### Running with GDB

```bash
# Debug server
gdb --args ./server/venus-server --port 5556
(gdb) run

# In another terminal, run client to trigger issue
export VK_DRIVER_FILES=$(pwd)/client/venus_icd.x86_64.json
./test-app/venus-test-app --phase 2
```

### Memory/Leak Debugging

```bash
# Valgrind on test app
valgrind --leak-check=full --show-leak-kinds=all \
  --suppressions=vulkan.supp \
  ./test-app/venus-test-app --phase 3

# Valgrind on server
valgrind --leak-check=full ./server/venus-server
```

## Key Design Constraints

1. **No code generation**: Venus protocol headers are copied from Mesa, not generated
2. **No modifications to Venus protocol**: Use as-is to maintain compatibility
3. **Thread safety**: Network layer must be thread-safe (multiple Vulkan commands may be concurrent)
4. **Error resilience**: Network errors must not crash, return VK_ERROR_DEVICE_LOST
5. **Handle lifetime**: Client handles persist until vkDestroy*, server must clean up mappings

## Performance Considerations

**Early Phases:** Ignore performance, focus on correctness

**Later Optimizations:**
- Command batching (send multiple commands in one message)
- Async submission (don't wait for reply on submit)
- Lazy memory transfer (transfer only when needed)
- Resource caching (cache immutable resources server-side)

## Security Notes

**Current Implementation:**
- No authentication
- No encryption
- Assumes trusted network
- Server runs with full GPU access

**Future Considerations:**
- Add client authentication
- TLS for encryption
- Resource limits per client
- Sandboxing server process

## Documentation Structure

All documentation is in `docs/`:

- `ARCHITECTURE.md` - System design (660 lines)
- `PROJECT_STRUCTURE.md` - Directory organization (520 lines)
- `DEVELOPMENT_ROADMAP.md` - Phase-by-phase plan with milestones
- `TESTING_STRATEGY.md` - Multi-layered testing approach
- `BUILD_AND_RUN.md` - Build instructions and troubleshooting
- `PHASE_01.md` through `PHASE_10.md` - Detailed phase specifications
- `INDEX.md` - Documentation index

Always refer to phase-specific docs when implementing features for that phase.

## Code Style

- C++17 standard features preferred
- RAII for resource management
- Use standard library containers (std::vector, std::unordered_map)
- Clear error handling with explicit checks
- Descriptive variable names (client_device, real_buffer, etc.)
- Comments for non-obvious handle translations

## Contact and Resources

**Project Location:** `/home/ayman/venus-plus/`

**External Resources:**
- [Venus Protocol](https://gitlab.freedesktop.org/virgl/venus-protocol)
- [Mesa Venus Driver](https://docs.mesa3d.org/drivers/venus.html)
- [Vulkan Specification](https://www.khronos.org/vulkan/)
