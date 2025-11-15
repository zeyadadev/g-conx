# Phase 02: Fake Instance Creation

**Create Vulkan instance with fake data**

## Overview

**Goal**: Implement instance creation, destruction, and physical device enumeration with fake/stub implementations.

**Duration**: 4 days (Days 4-7)

**Prerequisites**: Phase 01 complete and passing

## Objectives

By the end of this phase:

- ✅ Client can create and destroy Vulkan instances
- ✅ Server maintains handle mappings (client handle ↔ fake server handle)
- ✅ Client can enumerate physical devices (server returns 1 fake GPU)
- ✅ Handle allocator implemented on client side
- ✅ Handle map implemented on server side
- ✅ Test app successfully creates instance, enumerates GPU, destroys instance

## Commands to Implement

### Client Side

- [ ] `vkCreateInstance`
- [ ] `vkDestroyInstance`
- [ ] `vkEnumeratePhysicalDevices`

### Server Side

- [ ] Handle `vkCreateInstance` command
- [ ] Handle `vkDestroyInstance` command
- [ ] Handle `vkEnumeratePhysicalDevices` command

## New Components Required

### Client Components

**1. Handle Allocator** (`client/state/handle_allocator.h/cpp`)
- Allocate unique client-side handles
- Use monotonically increasing counter
- Template-based for all Vulkan handle types
- Thread-safe allocation

**2. Instance State** (`client/state/instance_state.h/cpp`)
- Track created instances
- Store instance creation info
- Track which instances are active

### Server Components

**1. Handle Map** (`server/state/handle_map.h/cpp`)
- Map client handles to server handles
- Support insert, lookup, remove operations
- Separate maps for each handle type
- Thread-safe access

**2. Instance State** (`server/state/instance_state.h/cpp`)
- Track instances per client
- Store fake instance data
- Manage fake physical device list

### Common Components

**1. Protocol Extensions** (`common/protocol-helpers/`)
- Add support for encoding/decoding instance commands
- Handle `VkInstanceCreateInfo` with pNext chains
- Handle arrays of physical devices

## Architecture

```
Application
    │
    ├─ vkCreateInstance(&createInfo, &instance)
    │
    ▼
Client ICD
    ├─ 1. Allocate client handle: instance_0x1234
    ├─ 2. Encode: VkInstanceCreateInfo
    ├─ 3. Send: vkCreateInstance command
    │
    ▼
Network
    │
    ▼
Server
    ├─ 4. Decode command
    ├─ 5. Store mapping: instance_0x1234 → instance_0x1234 (fake for now)
    ├─ 6. Encode reply: VK_SUCCESS
    │
    ▼
Network
    │
    ▼
Client ICD
    ├─ 7. Receive reply
    ├─ 8. Store instance state
    └─ 9. Return instance_0x1234 to app
```

## Detailed Requirements

### vkCreateInstance

**Client Requirements**:
- [ ] Allocate unique VkInstance handle using handle allocator
- [ ] Encode VkInstanceCreateInfo structure
- [ ] Handle pNext chain (can be NULL for this phase)
- [ ] Handle ppEnabledLayerNames array
- [ ] Handle ppEnabledExtensionNames array
- [ ] Send command to server
- [ ] Receive and decode reply
- [ ] Store instance in client state
- [ ] Return result to application

**Server Requirements**:
- [ ] Decode VkInstanceCreateInfo
- [ ] Validate basic parameters (non-null pointers)
- [ ] Store client instance handle in handle map
- [ ] For now, map to same value (fake): map[client_instance] = client_instance
- [ ] Track instance in server state
- [ ] Send success reply

**What NOT to do** (Phase 7+):
- Don't call real vkCreateInstance yet
- Don't validate extension names
- Don't validate layer names

### vkDestroyInstance

**Client Requirements**:
- [ ] Verify instance exists in client state
- [ ] Encode instance handle
- [ ] Send command to server
- [ ] Receive reply
- [ ] Remove instance from client state
- [ ] Free any associated resources

**Server Requirements**:
- [ ] Decode instance handle
- [ ] Verify instance exists in handle map
- [ ] Remove from handle map
- [ ] Remove from server state
- [ ] Send success reply

### vkEnumeratePhysicalDevices

**Client Requirements**:
- [ ] Verify instance is valid
- [ ] Encode instance handle
- [ ] Handle two-call pattern:
  - First call: pPhysicalDevices = NULL, get count
  - Second call: pPhysicalDevices != NULL, get devices
- [ ] Receive array of physical devices from server
- [ ] Allocate client handles for physical devices
- [ ] Store physical device handles
- [ ] Return count and/or devices to application

**Server Requirements**:
- [ ] Decode instance handle
- [ ] Verify instance exists
- [ ] Return fake physical device count: 1
- [ ] If pPhysicalDevices != NULL:
  - Generate 1 fake physical device handle
  - Store mapping: client_physical_device → fake_physical_device
  - Return physical device handle array
- [ ] Send reply with count and devices

## Handle Allocator Specification

### Requirements

- [ ] Thread-safe handle allocation
- [ ] Unique handles across all types
- [ ] Handles must be non-zero
- [ ] Support all Vulkan handle types
- [ ] Minimal memory overhead

### Interface

```
Template class: HandleAllocator<T>

Methods:
- T allocate() - Allocate new handle
- void free(T handle) - Mark handle as free (optional for now)
- bool is_valid(T handle) - Check if handle was allocated

Implementation:
- Use atomic counter for thread safety
- Cast counter value to handle type
- Start from 1 (VK_NULL_HANDLE is 0)
```

## Handle Map Specification

### Requirements

- [ ] Thread-safe insertion, lookup, removal
- [ ] Support all Vulkan handle types
- [ ] Fast lookup (O(1) average)
- [ ] Minimal memory overhead
- [ ] Handle cleanup on client disconnect

### Interface

```
Template class: HandleMap<T>

Methods:
- void insert(T client_handle, T server_handle)
- T lookup(T client_handle) const
- bool exists(T client_handle) const
- void remove(T client_handle)
- void clear() - Remove all mappings

Implementation:
- Use std::unordered_map internally
- Use std::mutex for thread safety
- Convert handles to uint64_t for storage
```

## Message Flow Examples

### Creating Instance

```
CLIENT → SERVER:
  Header: [MAGIC][SIZE]
  Payload:
    [VkCommandTypeEXT: vkCreateInstance]
    [VkInstanceCreateInfo: encoded]
    [VkInstance: client_handle_0x1234]

SERVER → CLIENT:
  Header: [MAGIC][SIZE]
  Payload:
    [VkCommandTypeEXT: vkCreateInstance]
    [VkResult: VK_SUCCESS]
```

### Enumerating Physical Devices (First Call)

```
CLIENT → SERVER:
  Header: [MAGIC][SIZE]
  Payload:
    [VkCommandTypeEXT: vkEnumeratePhysicalDevices]
    [VkInstance: client_handle_0x1234]
    [pPhysicalDeviceCount: pointer (encode as flag)]
    [pPhysicalDevices: NULL (encode as flag)]

SERVER → CLIENT:
  Header: [MAGIC][SIZE]
  Payload:
    [VkCommandTypeEXT: vkEnumeratePhysicalDevices]
    [VkResult: VK_SUCCESS]
    [physicalDeviceCount: 1]
```

### Enumerating Physical Devices (Second Call)

```
CLIENT → SERVER:
  Header: [MAGIC][SIZE]
  Payload:
    [VkCommandTypeEXT: vkEnumeratePhysicalDevices]
    [VkInstance: client_handle_0x1234]
    [pPhysicalDeviceCount: pointer (encode as flag)]
    [pPhysicalDevices: non-NULL (encode as flag)]

SERVER → CLIENT:
  Header: [MAGIC][SIZE]
  Payload:
    [VkCommandTypeEXT: vkEnumeratePhysicalDevices]
    [VkResult: VK_SUCCESS]
    [physicalDeviceCount: 1]
    [physicalDevices[0]: 0x5678]
```

## Test Application Requirements

### Test Scenario

**File**: `test-app/phase02/phase02_test.cpp`

**Test Steps**:
1. [ ] Call vkCreateInstance with minimal create info
2. [ ] Verify result is VK_SUCCESS
3. [ ] Verify instance handle is non-NULL
4. [ ] Call vkEnumeratePhysicalDevices (first call, get count)
5. [ ] Verify count is 1
6. [ ] Allocate array for 1 physical device
7. [ ] Call vkEnumeratePhysicalDevices (second call, get devices)
8. [ ] Verify physical device handle is non-NULL
9. [ ] Call vkDestroyInstance
10. [ ] Verify cleanup succeeded

**Expected Output**:
```
Phase 2: Fake Instance Creation
================================
✅ vkCreateInstance succeeded
✅ Instance handle: 0x1234
✅ vkEnumeratePhysicalDevices (count): 1 device
✅ vkEnumeratePhysicalDevices (devices): got device 0x5678
✅ vkDestroyInstance succeeded
✅ Phase 2 PASSED
```

## Implementation Checklist

### Day 1: Design & Client Handle Allocator
- [ ] Review Phase 1 code
- [ ] Design handle allocator interface
- [ ] Implement client handle allocator
- [ ] Write unit tests for handle allocator
- [ ] Update CMakeLists.txt

### Day 2: Client Instance Commands
- [ ] Implement vkCreateInstance (client)
- [ ] Implement vkDestroyInstance (client)
- [ ] Implement vkEnumeratePhysicalDevices (client)
- [ ] Add instance state tracking
- [ ] Test compilation

### Day 3: Server Handle Map & Commands
- [ ] Implement server handle map
- [ ] Implement vkCreateInstance handler (server)
- [ ] Implement vkDestroyInstance handler (server)
- [ ] Implement vkEnumeratePhysicalDevices handler (server)
- [ ] Add server state tracking

### Day 4: Testing & Integration
- [ ] Implement phase02 test app
- [ ] Test end-to-end
- [ ] Fix bugs
- [ ] Run regression tests (Phase 1 still works)
- [ ] Update documentation

## Success Criteria

- [ ] All code compiles without warnings
- [ ] Unit tests pass:
  - Handle allocator tests
  - Handle map tests
- [ ] Integration test passes:
  - Phase 2 test app runs successfully
- [ ] Regression test passes:
  - Phase 1 test still works
- [ ] Server handles multiple create/destroy cycles
- [ ] No memory leaks (verify with valgrind)
- [ ] Clean shutdown (server and client)

## Deliverables

- [ ] Client handle allocator (header + implementation)
- [ ] Client instance state management
- [ ] Client vkCreateInstance implementation
- [ ] Client vkDestroyInstance implementation
- [ ] Client vkEnumeratePhysicalDevices implementation
- [ ] Server handle map (header + implementation)
- [ ] Server instance state management
- [ ] Server command handlers for all 3 commands
- [ ] Phase 2 test application
- [ ] Unit tests for new components
- [ ] Updated CMakeLists.txt files
- [ ] Documentation updates

## Common Issues & Solutions

**Issue**: Handle allocator not thread-safe
**Solution**: Use std::atomic for counter

**Issue**: Handle map lookup fails
**Solution**: Verify handle type casting is correct (uint64_t conversion)

**Issue**: Physical device enumeration returns wrong count
**Solution**: Implement two-call pattern correctly (first for count, second for data)

**Issue**: Server state not cleaned up on instance destroy
**Solution**: Implement proper cleanup in vkDestroyInstance handler

**Issue**: Memory leak on instance creation
**Solution**: Verify RAII or manual cleanup of VkInstanceCreateInfo deep copies

## Next Steps

Once Phase 2 is complete and all tests pass, proceed to **[PHASE_03.md](PHASE_03.md)** for device creation.

## Notes

- This phase uses **fake handles** - server doesn't call real Vulkan yet
- Focus on correct protocol encoding/decoding
- Handle mapping is crucial - bugs here will affect all future phases
- Test thoroughly before moving to Phase 3
