# Venus Plus Testing Strategy

**Comprehensive testing approach for incremental development**

## Table of Contents

1. [Overview](#overview)
2. [Testing Layers](#testing-layers)
3. [Test Types](#test-types)
4. [Testing Per Phase](#testing-per-phase)
5. [Test Execution](#test-execution)
6. [Continuous Integration](#continuous-integration)
7. [Debugging Strategy](#debugging-strategy)

## Overview

Venus Plus employs a **multi-layered testing approach** to ensure correctness at every development phase:

1. **Unit Tests**: Test individual components in isolation
2. **Integration Tests**: Test client-server communication
3. **End-to-End Tests**: Test complete Vulkan workflows
4. **Regression Tests**: Ensure previous phases still work

**Key Principle**: **"Test as you build, not after"**

Every phase includes comprehensive tests before moving to the next phase.

## Testing Layers

```
┌─────────────────────────────────────────────────────────┐
│         Layer 4: End-to-End Application Tests           │
│  ┌───────────────────────────────────────────────────┐  │
│  │  venus-test-app (C++)                             │  │
│  │  - Phase 1: Network test                          │  │
│  │  - Phase 2: Instance creation                     │  │
│  │  - Phase 3: Device creation                       │  │
│  │  - ...                                            │  │
│  │  - Phase 10: Triangle rendering                   │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                          ▲
                          │
┌─────────────────────────────────────────────────────────┐
│         Layer 3: Integration Tests                      │
│  ┌───────────────────────────────────────────────────┐  │
│  │  Client + Server Together                         │  │
│  │  - Message round-trip tests                       │  │
│  │  - Handle mapping tests                           │  │
│  │  - Timeout/error handling                         │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                          ▲
                          │
┌─────────────────────────────────────────────────────────┐
│         Layer 2: Component Unit Tests                   │
│  ┌──────────────────┐        ┌──────────────────────┐  │
│  │  Client Tests    │        │  Server Tests        │  │
│  │  - Encoding      │        │  - Decoding          │  │
│  │  - Handle alloc  │        │  - Handle mapping    │  │
│  │  - State mgmt    │        │  - Executor          │  │
│  └──────────────────┘        └──────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                          ▲
                          │
┌─────────────────────────────────────────────────────────┐
│         Layer 1: Foundation Unit Tests                  │
│  ┌───────────────────────────────────────────────────┐  │
│  │  Common Library Tests                             │  │
│  │  - Venus encoder/decoder                          │  │
│  │  - Network client/server                          │  │
│  │  - Message protocol                               │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

## Test Types

### 1. Unit Tests

**Purpose**: Test individual components in isolation

**Framework**: Google Test (gtest)

**Location**: `client/tests/`, `server/tests/`, `common/tests/`

**Example**:
```cpp
// common/tests/test_venus_encoder.cpp
TEST(VenusEncoder, EncodeSimpleCommand) {
    VenusEncoder enc;

    VkCommandTypeEXT cmd_type = VK_COMMAND_TYPE_vkEnumerateInstanceVersion_EXT;
    uint32_t version = VK_API_VERSION_1_3;

    enc.encode(cmd_type, version);

    EXPECT_GT(enc.size(), 0);
    EXPECT_NE(enc.data(), nullptr);
}

TEST(VenusEncoder, EncodeDecodeRoundTrip) {
    VenusEncoder enc;
    enc.encode_vkCreateBuffer(device, &create_info, buffer);

    VenusDecoder dec(enc.data(), enc.size());

    VkDevice decoded_device;
    VkBufferCreateInfo decoded_info;
    VkBuffer decoded_buffer;

    dec.decode_vkCreateBuffer(decoded_device, decoded_info, decoded_buffer);

    EXPECT_EQ(decoded_device, device);
    EXPECT_EQ(decoded_info.size, create_info.size);
    EXPECT_EQ(decoded_buffer, buffer);
}
```

**Run**:
```bash
cd build
ctest --test-dir client/tests
ctest --test-dir server/tests
ctest --test-dir common/tests
```

### 2. Integration Tests

**Purpose**: Test client and server together

**Framework**: Custom test harness + gtest

**Location**: `test-app/integration/`

**Example**:
```cpp
// test-app/integration/test_round_trip.cpp
TEST_F(IntegrationTest, CreateBufferRoundTrip) {
    // Start mock server
    TestServer server(5556);
    server.start_async();

    // Connect client
    NetworkClient client;
    ASSERT_TRUE(client.connect("127.0.0.1", 5556));

    // Send vkCreateBuffer command
    VenusEncoder enc;
    enc.encode_vkCreateBuffer(device, &create_info, buffer);

    ASSERT_TRUE(client.send(enc.data(), enc.size()));

    // Receive reply
    std::vector<uint8_t> reply;
    ASSERT_TRUE(client.receive(reply));

    // Decode reply
    VenusDecoder dec(reply.data(), reply.size());
    VkResult result;
    dec.decode_vkCreateBuffer_reply(result);

    EXPECT_EQ(result, VK_SUCCESS);

    // Verify server created handle mapping
    EXPECT_TRUE(server.has_handle(buffer));
}
```

### 3. End-to-End Tests (venus-test-app)

**Purpose**: Test real Vulkan workflows through the ICD

**Framework**: Custom C++ application with test phases

**Location**: `test-app/`

**Structure**:
```cpp
// test-app/main.cpp
int main(int argc, char** argv) {
    int phase = parse_args(argc, argv);

    switch (phase) {
    case 1:  return phase01::run_test();
    case 2:  return phase02::run_test();
    case 3:  return phase03::run_test();
    // ... all phases
    case 10: return phase10::run_test();
    case ALL: return run_all_phases();
    }
}
```

**Example Phase Test**:
```cpp
// test-app/phase01/phase01_test.cpp
namespace phase01 {

int run_test() {
    std::cout << "Phase 1: Network Communication\n";

    // Test vkEnumerateInstanceVersion
    uint32_t version;
    VkResult result = vkEnumerateInstanceVersion(&version);

    if (result != VK_SUCCESS) {
        std::cerr << "❌ FAILED: vkEnumerateInstanceVersion returned "
                  << result << "\n";
        return 1;
    }

    std::cout << "✅ vkEnumerateInstanceVersion returned: "
              << VK_VERSION_MAJOR(version) << "."
              << VK_VERSION_MINOR(version) << "."
              << VK_VERSION_PATCH(version) << "\n";

    std::cout << "✅ PASSED\n";
    return 0;
}

} // namespace phase01
```

**Run**:
```bash
# Terminal 1: Start server
./build/server/venus-server --port 5556

# Terminal 2: Run test
VK_DRIVER_FILES=./build/client/venus_icd.x86_64.json \
./build/test-app/venus-test-app --phase 1
```

### 4. Regression Tests

**Purpose**: Ensure previous phases still work after changes

**Approach**: Run all phase tests up to current phase

**Run**:
```bash
# Run all tests up to phase 5
./build/test-app/venus-test-app --up-to 5

# Expected output:
# Phase 1: Network Communication      ✅ PASSED
# Phase 2: Fake Instance              ✅ PASSED
# Phase 3: Fake Device                ✅ PASSED
# Phase 4: Fake Resources             ✅ PASSED
# Phase 5: Fake Commands              ✅ PASSED
#
# All 5 phases passed!
```

## Testing Per Phase

### Phase 1: Network Communication

**Unit Tests**:
- ✅ `test_venus_encoder`: Encoding primitives
- ✅ `test_venus_decoder`: Decoding primitives
- ✅ `test_network_client`: TCP client
- ✅ `test_network_server`: TCP server
- ✅ `test_message_protocol`: Message header/payload

**Integration Tests**:
- ✅ `test_client_server_connection`: Connection establishment
- ✅ `test_message_round_trip`: Send/receive messages

**End-to-End Test**:
- ✅ `phase01_test`: Call `vkEnumerateInstanceVersion`

### Phase 2: Fake Instance

**Unit Tests**:
- ✅ `test_handle_allocator`: Client handle allocation
- ✅ `test_handle_map`: Server handle mapping

**Integration Tests**:
- ✅ `test_create_instance_round_trip`: Create and destroy instance

**End-to-End Test**:
- ✅ `phase02_test`: Create instance, enumerate GPUs, destroy

### Phase 3: Fake Device

**Unit Tests**:
- ✅ `test_device_state`: Device state tracking

**Integration Tests**:
- ✅ `test_create_device_round_trip`: Create and destroy device

**End-to-End Test**:
- ✅ `phase03_test`: Query GPU, create device, get queue, destroy

### Phase 4: Fake Resources

**Unit Tests**:
- ✅ `test_resource_tracker`: Server resource tracking

**Integration Tests**:
- ✅ `test_buffer_lifecycle`: Create, query, bind, destroy buffer
- ✅ `test_image_lifecycle`: Create, query, bind, destroy image

**End-to-End Test**:
- ✅ `phase04_test`: Create buffer, allocate memory, bind, destroy

### Phase 5: Fake Commands

**Unit Tests**:
- ✅ `test_command_encoding`: Encode vkCmd* commands

**Integration Tests**:
- ✅ `test_command_recording`: Record and validate commands

**End-to-End Test**:
- ✅ `phase05_test`: Create command pool, record commands, destroy

### Phase 6: Fake Submission

**Unit Tests**:
- ✅ `test_sync_objects`: Fence and semaphore handling

**Integration Tests**:
- ✅ `test_queue_submit`: Submit commands and wait

**End-to-End Test**:
- ✅ `phase06_test`: Record, submit, wait for fence

### Phase 7: Real Execution

**Unit Tests**:
- ✅ `test_executor`: Vulkan executor unit tests

**Integration Tests**:
- ✅ `test_real_execution`: Execute real Vulkan commands

**End-to-End Test**:
- ✅ `phase07_test`: Submit real work, verify execution
- ✅ **Validation**: Run with Vulkan validation layers enabled

### Phase 8: Memory Transfer

**Unit Tests**:
- ✅ `test_memory_transfer`: Memory transfer protocol

**Integration Tests**:
- ✅ `test_map_unmap_transfer`: Map, write, unmap, transfer

**End-to-End Test**:
- ✅ `phase08_test`: Write data, execute copy, read back, verify

### Phase 9: Compute Shaders

**Unit Tests**:
- ✅ `test_shader_module`: Shader module creation
- ✅ `test_descriptor_sets`: Descriptor set management

**Integration Tests**:
- ✅ `test_compute_pipeline`: Compute pipeline creation

**End-to-End Test**:
- ✅ `phase09_test`: Run "add arrays" shader, verify results

### Phase 10: Graphics Rendering

**Unit Tests**:
- ✅ `test_render_pass`: Render pass creation
- ✅ `test_graphics_pipeline`: Graphics pipeline creation

**Integration Tests**:
- ✅ `test_rendering`: Render and readback

**End-to-End Test**:
- ✅ `phase10_test`: Render triangle, save image, verify

## Test Execution

### Manual Testing

**Run all unit tests**:
```bash
cd build
ctest --output-on-failure
```

**Run specific test**:
```bash
./build/client/tests/test_venus_encoder
```

**Run phase test**:
```bash
# Terminal 1
./build/server/venus-server --port 5556

# Terminal 2
VK_DRIVER_FILES=./build/client/venus_icd.x86_64.json \
./build/test-app/venus-test-app --phase 3
```

**Run all phases**:
```bash
# Terminal 1
./build/server/venus-server --port 5556 &

# Terminal 2
VK_DRIVER_FILES=./build/client/venus_icd.x86_64.json \
./build/test-app/venus-test-app --all
```

### Automated Testing Script

```bash
#!/bin/bash
# scripts/run_tests.sh

set -e

echo "======================================"
echo "Venus Plus Test Suite"
echo "======================================"

# Build
echo "Building project..."
cd build
cmake ..
make -j$(nproc)

# Unit tests
echo ""
echo "Running unit tests..."
ctest --output-on-failure

# Start server in background
echo ""
echo "Starting test server..."
./server/venus-server --port 5556 &
SERVER_PID=$!
sleep 1  # Wait for server to start

# End-to-end tests
echo ""
echo "Running phase tests..."
VK_DRIVER_FILES=$(pwd)/client/venus_icd.x86_64.json \
./test-app/venus-test-app --all

# Kill server
kill $SERVER_PID

echo ""
echo "======================================"
echo "All tests passed!"
echo "======================================"
```

## Continuous Integration

### CI Pipeline (GitHub Actions / GitLab CI)

```yaml
# .github/workflows/test.yml
name: Test

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v3

    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y cmake g++ libvulkan-dev

    - name: Build
      run: |
        mkdir build
        cd build
        cmake ..
        make -j$(nproc)

    - name: Run unit tests
      run: |
        cd build
        ctest --output-on-failure

    - name: Run phase tests
      run: |
        cd build
        ./server/venus-server --port 5556 &
        sleep 1
        VK_DRIVER_FILES=$(pwd)/client/venus_icd.x86_64.json \
        ./test-app/venus-test-app --all
```

## Debugging Strategy

### Logging

**Log Levels**:
```cpp
enum LogLevel {
    DEBUG,    // Verbose details
    INFO,     // Normal operations
    WARNING,  // Unexpected but handled
    ERROR     // Failures
};
```

**Enable Debug Logging**:
```bash
# Client
export VENUS_CLIENT_LOG_LEVEL=DEBUG

# Server
./venus-server --log-level DEBUG
```

**Example Output**:
```
[DEBUG] [Client] Encoding vkCreateBuffer: size=1024, usage=0x80
[DEBUG] [Client] Sending message: 128 bytes
[DEBUG] [Server] Received message: 128 bytes
[DEBUG] [Server] Decoded command: VK_COMMAND_TYPE_vkCreateBuffer_EXT
[DEBUG] [Server] Creating real buffer on GPU
[DEBUG] [Server] Handle mapping: client=0x1234 → real=0x7f8a3c
[DEBUG] [Server] Sending reply: VK_SUCCESS
[DEBUG] [Client] Received reply: VK_SUCCESS
```

### Debugging Tools

**Valgrind** (memory leaks):
```bash
valgrind --leak-check=full \
./build/test-app/venus-test-app --phase 3
```

**GDB** (debugging crashes):
```bash
gdb --args ./build/server/venus-server --port 5556

(gdb) run
(gdb) bt  # Backtrace on crash
```

**Vulkan Validation Layers** (server-side):
```bash
# Enable validation on server
./build/server/venus-server --validation --port 5556

# Server will print validation errors:
# [VALIDATION ERROR] vkCreateBuffer: size must be > 0
```

**Network Debugging** (tcpdump):
```bash
# Capture traffic on port 5556
sudo tcpdump -i lo -w venus.pcap port 5556

# Analyze with Wireshark
wireshark venus.pcap
```

### Common Issues and Solutions

| Issue | Symptom | Solution |
|-------|---------|----------|
| Connection refused | Client can't connect | Check server is running on correct port |
| Decode error | Server crashes on receive | Verify client encoding is correct |
| Handle not found | Server error "unknown handle" | Check handle mapping logic |
| Validation error | Validation layer reports error | Fix server Vulkan usage |
| Memory leak | Valgrind reports leaks | Check resource cleanup on client/server |

---

**Next**: See [BUILD_AND_RUN.md](BUILD_AND_RUN.md) for complete build and run instructions.
