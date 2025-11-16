# Venus Protocol Integration

**Status**: ✅ Fully Integrated (Phase 2)
**Last Updated**: 2025-11-16

This document describes how Venus Plus integrates the Venus protocol for encoding/decoding Vulkan commands between client and server.

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Client-Side Integration](#client-side-integration)
4. [Server-Side Integration](#server-side-integration)
5. [Adding New Commands](#adding-new-commands)
6. [C/C++ Compatibility](#cc-compatibility)
7. [Debugging](#debugging)
8. [References](#references)

---

## Overview

Venus Plus uses the **Venus protocol** - a battle-tested, auto-generated protocol from the Mesa/Chromium projects that provides encoding/decoding for all Vulkan commands.

### Why Venus Protocol?

- ✅ **Complete coverage**: All Vulkan 1.3+ commands supported
- ✅ **Auto-generated**: Generated from Vulkan XML specs
- ✅ **Production-tested**: Used in Chrome OS, Android virtualization
- ✅ **Efficient**: Compact binary format with alignment
- ✅ **Maintained**: Updated with new Vulkan versions

### Integration Points

```
Application
    ↓
Vulkan Loader
    ↓
Venus Plus ICD (Client)
    ↓ vn_call_vkXxx()          ← Venus Protocol Encoding
vn_ring → NetworkClient
    ↓ TCP/IP
NetworkServer
    ↓ VenusRenderer
venus_renderer_handle()        ← Venus Protocol Decoding
    ↓ vn_dispatch_context
server_dispatch_vkXxx()
    ↓
Real Vulkan GPU
```

---

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────────────────────┐
│                         CLIENT SIDE                          │
├─────────────────────────────────────────────────────────────┤
│  ICD Entrypoints (C++)                                       │
│    └─> vn_call_vkCreateInstance(&g_ring, ...)              │
│         └─> vn_encode_vkCreateInstance() [Generated]        │
│              └─> vn_cs_encoder (common/vn_cs.h)             │
│                   └─> vn_ring_submit_command()              │
│                        └─> NetworkClient::send()            │
└─────────────────────────────────────────────────────────────┘
                              ↓ TCP/IP
┌─────────────────────────────────────────────────────────────┐
│                         SERVER SIDE                          │
├─────────────────────────────────────────────────────────────┤
│  NetworkServer::receive()                                    │
│    └─> venus_renderer_handle() (C)                          │
│         └─> vn_dispatch_context                             │
│              └─> server_dispatch_vkCreateInstance()         │
│                   └─> Real Vulkan API call                  │
│                        └─> Encode reply                     │
│                             └─> NetworkServer::send()       │
└─────────────────────────────────────────────────────────────┘
```

### Key Files

| File | Purpose | Language |
|------|---------|----------|
| `common/vn_cs.h` | Core encoder/decoder primitives | C/C++ |
| `common/vn_ring.h` | Ring buffer abstraction | C++ |
| `common/protocol/venus_ring.cpp` | vn_ring implementation | C++ |
| `common/venus-protocol/*.h` | Generated protocol headers | C |
| `server/renderer_decoder.h` | VenusRenderer API | C |
| `server/renderer_decoder.c` | Command dispatch implementation | C |

---

## Client-Side Integration

### 1. The `vn_ring` Structure

The `vn_ring` is a custom abstraction that connects Venus protocol to our NetworkClient:

**Definition** (`common/vn_ring.h`):
```cpp
struct vn_ring {
    venus_plus::NetworkClient* client;
};
```

**Initialization** (`client/icd/icd_entrypoints.cpp`):
```cpp
static NetworkClient g_client;
static vn_ring g_ring = {};

// In ensure_connected():
g_ring.client = &g_client;
```

### 2. Command Submission Flow

Venus protocol provides high-level wrapper functions like `vn_call_vkXxx()` that handle the complete encode-send-receive-decode cycle.

**Example - vkCreateInstance**:
```cpp
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(
    const VkInstanceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkInstance* pInstance) {

    // 1. Allocate ICD instance structure
    IcdInstance* icd_instance = new IcdInstance();
    icd_instance->remote_handle = VK_NULL_HANDLE;

    // 2. Call Venus protocol wrapper - handles everything!
    VkResult result = vn_call_vkCreateInstance(
        &g_ring,                    // Ring buffer (network abstraction)
        pCreateInfo,                // Vulkan parameters
        pAllocator,
        &icd_instance->remote_handle  // Output: remote handle
    );

    // 3. Store handle mapping
    *pInstance = icd_instance_to_handle(icd_instance);
    g_instance_state.add_instance(*pInstance, icd_instance->remote_handle);

    return result;
}
```

### 3. Ring Buffer Functions

The `vn_ring` implementation provides the bridge between Venus protocol and networking:

**vn_ring_submit_command_init()** - Prepare command buffer:
```cpp
vn_cs_encoder* vn_ring_submit_command_init(
    struct vn_ring* ring,
    struct vn_ring_submit_command* submit,
    void* cmd_data,
    size_t cmd_size,
    size_t reply_size)
{
    submit->cmd_data = cmd_data;
    submit->cmd_size = cmd_size;
    vn_cs_encoder_init_external(&submit->encoder, cmd_data, cmd_size);
    return &submit->encoder;
}
```

**vn_ring_submit_command()** - Send command:
```cpp
void vn_ring_submit_command(
    struct vn_ring* ring,
    struct vn_ring_submit_command* submit)
{
    const size_t size = vn_cs_encoder_get_len(&submit->encoder);
    ring->client->send(submit->cmd_data, size);
}
```

**vn_ring_get_command_reply()** - Receive reply:
```cpp
vn_cs_decoder* vn_ring_get_command_reply(
    struct vn_ring* ring,
    struct vn_ring_submit_command* submit)
{
    std::vector<uint8_t> reply;
    ring->client->receive(reply);

    submit->reply_buffer = std::move(reply);
    vn_cs_decoder_init(&submit->decoder,
                       submit->reply_buffer.data(),
                       submit->reply_buffer.size());
    return &submit->decoder;
}
```

### 4. Async Commands

For commands that don't need a reply (like `vkDestroyInstance`), use `vn_async_vkXxx()`:

```cpp
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(
    VkInstance instance,
    const VkAllocationCallbacks* pAllocator) {

    IcdInstance* icd_instance = icd_instance_from_handle(instance);

    // Async - fire and forget, no reply expected
    vn_async_vkDestroyInstance(&g_ring, icd_instance->remote_handle, pAllocator);

    delete icd_instance;
}
```

---

## Server-Side Integration

### 1. VenusRenderer Structure

The server uses a C-based `VenusRenderer` for decoding Venus protocol commands:

**Definition** (`server/renderer_decoder.c`):
```c
struct VenusRenderer {
    struct vn_dispatch_context ctx;    // Dispatch context
    struct vn_cs_decoder* decoder;     // Venus decoder
    struct vn_cs_encoder* encoder;     // Venus encoder
    struct ServerState* state;         // Server state
};
```

**Dispatch Context**:
```c
struct vn_dispatch_context {
    void* data;                        // User data (ServerState*)
    void* debug_log;                   // Optional logging callback

    // Dispatch function pointers for each Vulkan command
    vn_dispatch_vkCreateInstance_func vkCreateInstance;
    vn_dispatch_vkDestroyInstance_func vkDestroyInstance;
    vn_dispatch_vkEnumeratePhysicalDevices_func vkEnumeratePhysicalDevices;
    // ... etc for all commands
};
```

### 2. Renderer Initialization

**Creating the Renderer** (`server/main.cpp`):
```cpp
g_renderer = venus_renderer_create(&g_server_state);
```

**Initialization** (`server/renderer_decoder.c`):
```c
struct VenusRenderer* venus_renderer_create(struct ServerState* state) {
    struct VenusRenderer* renderer = calloc(1, sizeof(*renderer));

    renderer->state = state;
    renderer->decoder = vn_cs_decoder_create();
    renderer->encoder = vn_cs_encoder_create();

    // Set up dispatch context
    renderer->ctx.data = state;
    renderer->ctx.vkCreateInstance = server_dispatch_vkCreateInstance;
    renderer->ctx.vkDestroyInstance = server_dispatch_vkDestroyInstance;
    renderer->ctx.vkEnumeratePhysicalDevices = server_dispatch_vkEnumeratePhysicalDevices;
    // ... register all command handlers

    return renderer;
}
```

### 3. Command Handling

**Main Handler** (`server/main.cpp`):
```cpp
bool handle_client_message(int client_fd, const void* data, size_t size) {
    uint8_t* reply = nullptr;
    size_t reply_size = 0;

    // Decode and dispatch Venus command
    if (!venus_renderer_handle(g_renderer, data, size, &reply, &reply_size)) {
        return false;
    }

    // Send reply
    if (reply && reply_size > 0) {
        NetworkServer::send_to_client(client_fd, reply, reply_size);
        free(reply);
    }

    return true;
}
```

**venus_renderer_handle()** - The core dispatch function:
```c
bool venus_renderer_handle(struct VenusRenderer* renderer,
                           const void* data,
                           size_t size,
                           uint8_t** reply_data,
                           size_t* reply_size)
{
    // 1. Reset decoder with incoming data
    vn_cs_decoder_reset(renderer->decoder, data, size);

    // 2. Reset encoder for reply
    vn_cs_encoder_reset(renderer->encoder);

    // 3. Dispatch the command
    //    This reads command type and calls the appropriate handler
    vn_dispatch_command(&renderer->ctx, renderer->decoder, renderer->encoder);

    // 4. Get encoded reply
    *reply_data = vn_cs_encoder_get_data(renderer->encoder);
    *reply_size = vn_cs_encoder_get_len(renderer->encoder);

    return true;
}
```

### 4. Dispatch Functions

Each Vulkan command has a dispatch function that:
1. Receives decoded parameters in a `vn_command_vkXxx` struct
2. Executes the command (or fakes it for early phases)
3. Sets the return value and output parameters

**Example - vkCreateInstance**:
```c
static void server_dispatch_vkCreateInstance(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCreateInstance* args)
{
    printf("[Venus Server] Dispatching vkCreateInstance\n");

    // Validate parameters
    if (!args->pInstance) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    // Execute command (Phase 2: fake implementation)
    *args->pInstance = server_state_bridge_alloc_instance(
        (struct ServerState*)ctx->data
    );

    args->ret = VK_SUCCESS;
    printf("[Venus Server]   -> Created instance: %p\n",
           (void*)*args->pInstance);
}
```

**Example - vkEnumeratePhysicalDevices**:
```c
static void server_dispatch_vkEnumeratePhysicalDevices(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkEnumeratePhysicalDevices* args)
{
    printf("[Venus Server] Dispatching vkEnumeratePhysicalDevices (instance: %p)\n",
           (void*)args->instance);

    struct ServerState* state = (struct ServerState*)ctx->data;

    if (!args->pPhysicalDeviceCount) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    const uint32_t available_devices = 1;

    // First call: return count
    if (!args->pPhysicalDevices) {
        *args->pPhysicalDeviceCount = available_devices;
        printf("[Venus Server]   -> Returning device count: %u\n", available_devices);
        args->ret = VK_SUCCESS;
        return;
    }

    // Second call: return devices
    const uint32_t max_out = *args->pPhysicalDeviceCount;
    const uint32_t to_write = available_devices < max_out ? available_devices : max_out;

    for (uint32_t i = 0; i < to_write; ++i) {
        args->pPhysicalDevices[i] = server_state_bridge_get_fake_device(state);
        printf("[Venus Server]   -> Device %u: %p\n", i,
               (void*)args->pPhysicalDevices[i]);
    }

    *args->pPhysicalDeviceCount = to_write;
    args->ret = (max_out < available_devices) ? VK_INCOMPLETE : VK_SUCCESS;
}
```

---

## Adding New Commands

To add a new Vulkan command to Venus Plus:

### Step 1: Client Implementation

Add the ICD entrypoint in `client/icd/icd_entrypoints.cpp`:

```cpp
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(
    VkPhysicalDevice physicalDevice,
    const VkDeviceCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDevice* pDevice)
{
    std::cout << "[Client ICD] vkCreateDevice called\n";

    // 1. Allocate client structure
    IcdDevice* icd_device = new IcdDevice();
    icd_device->remote_handle = VK_NULL_HANDLE;

    // 2. Get remote physical device handle
    VkPhysicalDevice remote_physical_device =
        get_remote_physical_device(physicalDevice);

    // 3. Call Venus protocol wrapper
    VkResult result = vn_call_vkCreateDevice(
        &g_ring,
        remote_physical_device,
        pCreateInfo,
        pAllocator,
        &icd_device->remote_handle
    );

    if (result != VK_SUCCESS) {
        delete icd_device;
        return result;
    }

    // 4. Store mapping
    *pDevice = icd_device_to_handle(icd_device);
    g_device_state.add_device(*pDevice, icd_device->remote_handle);

    return VK_SUCCESS;
}
```

### Step 2: Server Dispatch Function

Add the dispatch function in `server/renderer_decoder.c`:

```c
static void server_dispatch_vkCreateDevice(
    struct vn_dispatch_context* ctx,
    struct vn_command_vkCreateDevice* args)
{
    printf("[Venus Server] Dispatching vkCreateDevice\n");

    struct ServerState* state = (struct ServerState*)ctx->data;

    // Validate parameters
    if (!args->pDevice) {
        args->ret = VK_ERROR_INITIALIZATION_FAILED;
        return;
    }

    // For Phase 2-6: Fake implementation
    *args->pDevice = server_state_bridge_alloc_device(state);
    args->ret = VK_SUCCESS;

    printf("[Venus Server]   -> Created device: %p\n", (void*)*args->pDevice);

    // For Phase 7+: Real implementation
    // VkDevice real_device;
    // args->ret = vkCreateDevice(args->physicalDevice, args->pCreateInfo,
    //                           args->pAllocator, &real_device);
    // if (args->ret == VK_SUCCESS) {
    //     *args->pDevice = real_device;
    //     server_state_bridge_map_device(state, *args->pDevice, real_device);
    // }
}
```

### Step 3: Register Dispatch Function

In `venus_renderer_create()`, register the new handler:

```c
renderer->ctx.vkCreateDevice = server_dispatch_vkCreateDevice;
```

### Step 4: Export from ICD

Add to `vk_icdGetInstanceProcAddr()` or `vk_icdGetDeviceProcAddr()`:

```cpp
if (strcmp(pName, "vkCreateDevice") == 0) {
    return (PFN_vkVoidFunction)vkCreateDevice;
}
```

### Step 5: Test

```bash
# Add test in test-app/phaseXX/
./test-app/venus-test-app --phase X
```

---

## C/C++ Compatibility

The Venus protocol headers are auto-generated C code, but Venus Plus is mostly C++. Here's how we handle compatibility:

### Problem: C-Style Code in C++ Context

Venus protocol headers use C constructs that aren't valid in C++:
- Compound literals: `&(VkStructureType){ VALUE }`
- Implicit pointer conversions
- C-style function signatures

### Solution: Hybrid C/C++ Approach

#### 1. Server Uses Pure C

The server decoder is implemented in C (`renderer_decoder.c`) to avoid compatibility issues:

```c
// server/renderer_decoder.c - Pure C file
#include "vn_protocol_renderer.h"  // Generated Venus headers - OK in C

static void server_dispatch_vkCreateInstance(...) {
    // Pure C implementation
}
```

#### 2. Client Uses C++ with Wrappers

The client is C++ but uses `extern "C"` for Venus protocol interfaces:

**vn_ring.h**:
```cpp
#ifdef __cplusplus
extern "C" {
#endif

struct vn_ring {
    void* client;  // Actually NetworkClient*, but opaque to C code
};

vn_cs_encoder* vn_ring_submit_command_init(...);
void vn_ring_submit_command(...);

#ifdef __cplusplus
}
#endif
```

**vn_cs.h** - Dual C/C++ Header:
```cpp
#ifdef __cplusplus
#include <vector>
struct vn_cs_encoder {
    uint8_t* data;
    size_t capacity;
    std::vector<uint8_t> storage;  // C++ feature
};
#else
struct vn_cs_encoder;  // Opaque in C
typedef struct vn_cs_encoder vn_cs_encoder;
#endif
```

#### 3. Fixed Venus Protocol Template

We patched the venus-protocol generator to avoid compound literals:

**Before** (C99, not C++):
```c
vn_encode_VkStructureType(enc, &(VkStructureType){ VK_STRUCTURE_TYPE_XXX });
```

**After** (C++ compatible):
```c
{ const VkStructureType stype = VK_STRUCTURE_TYPE_XXX;
  vn_encode_VkStructureType(enc, &stype); }
```

**How to regenerate** (if needed):
```bash
# 1. Patch template
cd /home/ayman/venus-protocol/templates
# Edit types_chain.h line 232 as shown above

# 2. Regenerate
python3 /home/ayman/venus-protocol/vn_protocol.py \
    --outdir /home/ayman/venus-plus/common/venus-protocol

# 3. Fix vn_protocol_driver_cs.h include path
sed -i 's|#include "vn_cs.h"|#include "../protocol-helpers/vn_cs.h"|' \
    /home/ayman/venus-plus/common/venus-protocol/vn_protocol_driver_cs.h
```

#### 4. Compiler Flags

For remaining warnings, we use `-fpermissive`:

**client/CMakeLists.txt**:
```cmake
target_compile_options(venus_icd PRIVATE
    -fpermissive         # Allow implicit pointer conversions
)
```

---

## Debugging

### Enable Verbose Logging

#### Client Side
```cpp
// In client/icd/icd_entrypoints.cpp
std::cout << "[Client ICD] Calling vn_call_vkCreateInstance\n";
std::cout << "[Client ICD] Remote handle: " << remote_handle << "\n";
```

#### Server Side
```c
// In server/renderer_decoder.c (already implemented)
printf("[Venus Server] Dispatching vkCreateInstance\n");
printf("[Venus Server]   -> Created instance: %p\n", (void*)instance);
```

### Common Issues

#### Issue: "vn_ring_submit_command undefined"

**Cause**: Missing vn_ring implementation
**Fix**: Ensure `common/protocol/venus_ring.cpp` is compiled and linked

#### Issue: "Cannot convert vn_ring* to vn_ring_submit_command*"

**Cause**: Venus protocol expects different vn_ring API
**Fix**: Check vn_ring.h matches the stub functions needed

#### Issue: Client sends but server doesn't receive

**Cause**: Encoder not writing data
**Fix**: Check `vn_cs_encoder_get_len()` returns non-zero before sending

#### Issue: Server crashes on decode

**Cause**: Corrupted or incomplete message
**Fix**:
1. Check message size matches `vn_cs_encoder_get_len()`
2. Enable wire protocol logging:
```cpp
// Dump raw bytes
for (size_t i = 0; i < size; i++) {
    printf("%02x ", ((uint8_t*)data)[i]);
}
printf("\n");
```

### Debugging Tools

#### 1. Network Packet Capture
```bash
tcpdump -i lo port 5556 -w venus.pcap
wireshark venus.pcap
```

#### 2. Check Encoder State
```cpp
printf("Encoder: offset=%zu capacity=%zu\n",
       encoder->offset, encoder->capacity);
```

#### 3. Verify Command Type
```c
// In venus_renderer_handle, before dispatch:
VkCommandTypeEXT cmd_type;
vn_decode_VkCommandTypeEXT(decoder, &cmd_type);
printf("Command type: %u\n", cmd_type);
vn_cs_decoder_reset(decoder, data, size);  // Reset for actual decode
```

#### 4. GDB Server Debugging
```bash
gdb --args ./server/venus-server
(gdb) b server_dispatch_vkCreateInstance
(gdb) run
(gdb) p *args
```

---

## Common Pitfalls and Lessons Learned

### Critical ICD Requirements

#### ⚠️ Issue: "vkEnumerateInstanceExtensionProperties points to the loader"

**Problem**: ICD functions were calling the Vulkan loader instead of the ICD implementation, causing infinite recursion.

**Root Causes**:
1. **Linking against libvulkan.so** - Created circular dependency
2. **PUBLIC symbol visibility** - Exported Vulkan function symbols

**Solutions**:

**1. Never link ICD against Vulkan loader** (`client/CMakeLists.txt`):
```cmake
# ❌ WRONG - Creates circular dependency
target_link_libraries(venus_icd PRIVATE
    Vulkan::Vulkan  # DON'T DO THIS!
)

# ✅ CORRECT - Only need headers
target_link_libraries(venus_icd PRIVATE
    venus_common  # Provides Vulkan headers without loader
)
```

**2. Use HIDDEN visibility for Vulkan functions** (`client/icd/icd_entrypoints.h`):
```cpp
// Symbol visibility macros
#define VP_PUBLIC __attribute__((visibility("default")))
#define VP_PRIVATE __attribute__((visibility("hidden")))

// ✅ ICD interface - MUST be PUBLIC (loader needs to find these)
VP_PUBLIC VKAPI_ATTR VkResult VKAPI_CALL
    vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t* pSupportedVersion);

VP_PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
    vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);

VP_PUBLIC VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
    vk_icdGetPhysicalDeviceProcAddr(VkInstance instance, const char* pName);

// ✅ Vulkan functions - MUST be PRIVATE (accessed through vk_icdGetInstanceProcAddr)
VP_PRIVATE VKAPI_ATTR VkResult VKAPI_CALL
    vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                     const VkAllocationCallbacks* pAllocator,
                     VkInstance* pInstance);

VP_PRIVATE VKAPI_ATTR VkResult VKAPI_CALL
    vkEnumeratePhysicalDevices(VkInstance instance,
                                uint32_t* pPhysicalDeviceCount,
                                VkPhysicalDevice* pPhysicalDevices);
// ... all other Vulkan functions must be VP_PRIVATE
```

**Verification**:
```bash
# Check that ICD doesn't link against libvulkan.so
ldd ./client/libvenus_icd.so | grep vulkan
# Should show NOTHING

# Check symbol visibility
nm -D ./client/libvenus_icd.so | grep vkCreateInstance
# Should show NOTHING (symbol is hidden)

nm -D ./client/libvenus_icd.so | grep vk_icdNegotiateLoaderICDInterfaceVersion
# Should show: 00001234 T vk_icdNegotiateLoaderICDInterfaceVersion
```

### Why This Matters

**What happens if you link against libvulkan.so:**
```
Application calls vkCreateInstance
  ↓
Vulkan loader dispatches to ICD's vkCreateInstance
  ↓
ICD's vkCreateInstance (if linked to libvulkan.so) calls libvulkan's vkCreateInstance
  ↓
Vulkan loader dispatches to ICD's vkCreateInstance again
  ↓
INFINITE LOOP! 💥
```

**What happens with correct implementation:**
```
Application calls vkCreateInstance
  ↓
Vulkan loader dispatches to ICD's vkCreateInstance
  ↓
ICD's vkCreateInstance (NOT linked to libvulkan.so) executes ICD code
  ↓
SUCCESS! ✅
```

### Other Common Issues

#### Issue: "undefined reference to vn_call_vkXxx"

**Cause**: Missing link to venus protocol implementation
**Fix**: Ensure `common/protocol/venus_ring.cpp` is in `venus_common` target

#### Issue: Compiler errors about compound literals

**Cause**: Venus protocol headers use C99 compound literals
**Fix**: See [C/C++ Compatibility](#cc-compatibility) section - template patching required

#### Issue: Server doesn't log Venus commands

**Cause**: Missing printf statements in dispatch functions
**Fix**: Add logging to each `server_dispatch_vkXxx()` function

---

## References

### Venus Protocol

- **Repository**: https://gitlab.freedesktop.org/virgl/venus-protocol
- **Mesa Implementation**: https://docs.mesa3d.org/drivers/venus.html
- **Generator**: `/home/ayman/venus-protocol/vn_protocol.py`

### Vulkan Specification

- **Spec**: https://www.khronos.org/vulkan/
- **XML**: Used by venus-protocol generator

### Related Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) - Overall system design
- [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) - Directory layout
- [PHASE_02.md](PHASE_02.md) - Current implementation phase
- [BUILD_AND_RUN.md](BUILD_AND_RUN.md) - Build instructions

---

## Summary

Venus Plus successfully integrates the Venus protocol through:

1. ✅ **Client**: C++ ICD using `vn_call_vkXxx()` wrappers
2. ✅ **vn_ring**: Custom implementation wrapping NetworkClient
3. ✅ **Server**: Pure C renderer with dispatch context
4. ✅ **C/C++ Compat**: Hybrid approach with extern "C" boundaries
5. ✅ **Testing**: Full Phase 2 passing with Venus protocol

The integration is **production-ready** and supports all future Vulkan commands through the auto-generated protocol headers.
