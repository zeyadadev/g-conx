# Phase 03: Fake Device Creation

**Create logical device and queues with fake data**

## Overview

**Goal**: Implement device queries, device creation, queue retrieval, and device destruction with fake/stub implementations.

**Duration**: 7 days (Days 8-14)

**Prerequisites**: Phase 02 complete and passing

## Objectives

By the end of this phase:

- ✅ Client can query physical device properties, features, and queue families
- ✅ Client can create and destroy logical devices
- ✅ Client can retrieve queue handles
- ✅ Server returns fake but valid GPU properties
- ✅ Server returns fake queue family properties
- ✅ Test app successfully queries GPU, creates device, gets queue, destroys device

## Commands to Implement

### Physical Device Query Commands (Client & Server)

- [ ] `vkGetPhysicalDeviceProperties`
- [ ] `vkGetPhysicalDeviceProperties2` (optional)
- [ ] `vkGetPhysicalDeviceFeatures`
- [ ] `vkGetPhysicalDeviceFeatures2` (optional)
- [ ] `vkGetPhysicalDeviceQueueFamilyProperties`
- [ ] `vkGetPhysicalDeviceMemoryProperties`
- [ ] `vkGetPhysicalDeviceFormatProperties`

### Device Commands (Client & Server)

- [ ] `vkCreateDevice`
- [ ] `vkDestroyDevice`
- [ ] `vkGetDeviceQueue`

## New Components Required

### Server Components

**1. Fake GPU Data Generator** (`server/state/fake_gpu_data.h/cpp`)
- Generate fake but valid VkPhysicalDeviceProperties
- Generate fake VkPhysicalDeviceFeatures
- Generate fake VkQueueFamilyProperties
- Generate fake VkPhysicalDeviceMemoryProperties

**2. Device State Manager** (`server/state/device_state.h/cpp`)
- Track created devices per client
- Associate devices with parent physical device
- Track queues per device

### Client Components

**1. Physical Device State** (`client/state/physical_device_state.h/cpp`)
- Cache physical device properties
- Cache queue family properties
- Track enumerated physical devices

**2. Device State** (`client/state/device_state.h/cpp`)
- Track created devices
- Store device creation info
- Track queues retrieved from device

## Fake GPU Specifications

### VkPhysicalDeviceProperties

```
deviceName: "Venus Plus Virtual GPU"
deviceType: VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
vendorID: 0x10DE (NVIDIA-like)
deviceID: 0x1234
apiVersion: VK_API_VERSION_1_3
driverVersion: VK_MAKE_VERSION(1, 0, 0)

limits: (use reasonable values)
  maxImageDimension2D: 16384
  maxFramebufferWidth: 16384
  maxFramebufferHeight: 16384
  maxComputeWorkGroupCount: [65535, 65535, 65535]
  maxComputeWorkGroupSize: [1024, 1024, 64]
  maxMemoryAllocationCount: 4096
  maxBoundDescriptorSets: 8
  (set other limits to Vulkan 1.3 minimum or higher)
```

### VkPhysicalDeviceFeatures

```
Enable commonly used features:
  robustBufferAccess: VK_TRUE
  fullDrawIndexUint32: VK_TRUE
  independentBlend: VK_TRUE
  geometryShader: VK_TRUE
  tessellationShader: VK_TRUE
  sampleRateShading: VK_TRUE
  dualSrcBlend: VK_TRUE
  multiDrawIndirect: VK_TRUE
  drawIndirectFirstInstance: VK_TRUE
  fillModeNonSolid: VK_TRUE
  samplerAnisotropy: VK_TRUE
  textureCompressionBC: VK_TRUE
  vertexPipelineStoresAndAtomics: VK_TRUE
  fragmentStoresAndAtomics: VK_TRUE
  shaderStorageImageExtendedFormats: VK_TRUE
```

### VkQueueFamilyProperties

```
Queue Family 0:
  queueFlags: VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT
  queueCount: 4
  timestampValidBits: 64
  minImageTransferGranularity: {1, 1, 1}
```

### VkPhysicalDeviceMemoryProperties

```
Memory Type 0:
  propertyFlags: VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  heapIndex: 0

Memory Type 1:
  propertyFlags: VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
  heapIndex: 1

Memory Heap 0:
  size: 8GB (8589934592 bytes)
  flags: VK_MEMORY_HEAP_DEVICE_LOCAL_BIT

Memory Heap 1:
  size: 16GB (17179869184 bytes)
  flags: 0
```

## Detailed Requirements

### vkGetPhysicalDeviceProperties

**Client Requirements**:
- [ ] Verify physical device is valid
- [ ] Encode physical device handle
- [ ] Send command
- [ ] Receive properties structure
- [ ] Decode VkPhysicalDeviceProperties
- [ ] Store in client state (cache)
- [ ] Copy to output parameter

**Server Requirements**:
- [ ] Decode physical device handle
- [ ] Verify handle exists
- [ ] Generate fake properties (use constants)
- [ ] Encode properties structure
- [ ] Send reply

### vkGetPhysicalDeviceFeatures

**Client Requirements**:
- [ ] Similar to properties
- [ ] Handle VkPhysicalDeviceFeatures structure

**Server Requirements**:
- [ ] Return fake features (as specified above)

### vkGetPhysicalDeviceQueueFamilyProperties

**Client Requirements**:
- [ ] Handle two-call pattern (count, then data)
- [ ] Allocate array for queue family properties
- [ ] Cache results

**Server Requirements**:
- [ ] Return count: 1 queue family
- [ ] Return queue family properties (as specified)

### vkGetPhysicalDeviceMemoryProperties

**Client Requirements**:
- [ ] Receive memory properties structure
- [ ] Decode memory types and heaps arrays

**Server Requirements**:
- [ ] Return fake memory properties (as specified)
- [ ] Properly encode arrays

### vkCreateDevice

**Client Requirements**:
- [ ] Verify physical device is valid
- [ ] Allocate VkDevice handle
- [ ] Encode VkDeviceCreateInfo
  - [ ] Handle pQueueCreateInfos array
  - [ ] Handle ppEnabledLayerNames array
  - [ ] Handle ppEnabledExtensionNames array
  - [ ] Handle pEnabledFeatures structure
  - [ ] Handle pNext chain
- [ ] Send command
- [ ] Receive reply
- [ ] Store device in client state

**Server Requirements**:
- [ ] Decode VkDeviceCreateInfo
- [ ] Validate queue create infos reference valid queue families
- [ ] Store device handle mapping
- [ ] Associate device with physical device
- [ ] Track requested queues
- [ ] Send success reply

### vkGetDeviceQueue

**Client Requirements**:
- [ ] Verify device is valid
- [ ] Verify queue family index is valid
- [ ] Verify queue index is valid
- [ ] Allocate VkQueue handle
- [ ] Encode command
- [ ] Send command
- [ ] Receive reply
- [ ] Store queue in client state

**Server Requirements**:
- [ ] Decode device, queueFamilyIndex, queueIndex
- [ ] Verify device exists
- [ ] Verify queue family/index are valid
- [ ] Allocate queue handle
- [ ] Store queue handle mapping
- [ ] Associate queue with device
- [ ] Send reply with queue handle

### vkDestroyDevice

**Client Requirements**:
- [ ] Verify device is valid
- [ ] Encode device handle
- [ ] Send command
- [ ] Receive reply
- [ ] Clean up device state
- [ ] Clean up associated queues

**Server Requirements**:
- [ ] Decode device handle
- [ ] Remove device from handle map
- [ ] Remove associated queues
- [ ] Clean up device state
- [ ] Send success reply

## Message Encoding Considerations

### VkDeviceCreateInfo Encoding

**Complex structure requires encoding**:
- [ ] VkDeviceQueueCreateInfo array (dynamic size)
- [ ] Each VkDeviceQueueCreateInfo contains:
  - queueFamilyIndex
  - queueCount
  - pQueuePriorities (float array)
- [ ] Layer names (char** array)
- [ ] Extension names (char** array)
- [ ] VkPhysicalDeviceFeatures structure
- [ ] pNext chain (can be NULL for this phase)

**Encoding strategy**:
- Encode array sizes first
- Encode arrays element-by-element
- For strings: encode length, then characters
- For nested structures: encode recursively

## Test Application Requirements

### Test Scenario

**File**: `test-app/phase03/phase03_test.cpp`

**Test Steps**:
1. [ ] Create instance (from Phase 2)
2. [ ] Enumerate physical devices (from Phase 2)
3. [ ] Get physical device properties
4. [ ] Verify device name is "Venus Plus Virtual GPU"
5. [ ] Get physical device features
6. [ ] Get queue family properties
7. [ ] Verify 1 queue family with graphics+compute support
8. [ ] Get memory properties
9. [ ] Create device with 1 queue from family 0
10. [ ] Get device queue
11. [ ] Verify queue handle is valid
12. [ ] Destroy device
13. [ ] Destroy instance

**Expected Output**:
```
Phase 3: Fake Device Creation
==============================
✅ Physical device properties:
   Name: Venus Plus Virtual GPU
   Type: Discrete GPU
   API Version: 1.3.0
✅ Physical device features: 15 features enabled
✅ Queue families: 1
   Family 0: Graphics | Compute | Transfer, 4 queues
✅ Memory: 2 types, 2 heaps
✅ vkCreateDevice succeeded
✅ vkGetDeviceQueue succeeded
✅ vkDestroyDevice succeeded
✅ Phase 3 PASSED
```

## Implementation Checklist

### Days 1-2: Physical Device Queries
- [ ] Design fake GPU data structures
- [ ] Implement fake data generator
- [ ] Implement vkGetPhysicalDeviceProperties (client + server)
- [ ] Implement vkGetPhysicalDeviceFeatures (client + server)
- [ ] Implement vkGetPhysicalDeviceQueueFamilyProperties (client + server)
- [ ] Implement vkGetPhysicalDeviceMemoryProperties (client + server)
- [ ] Test each command individually

### Days 3-4: Device Creation
- [ ] Design device state structures
- [ ] Implement vkCreateDevice (client + server)
- [ ] Handle complex VkDeviceCreateInfo encoding
- [ ] Test device creation

### Days 5-6: Queue Retrieval & Device Destruction
- [ ] Implement vkGetDeviceQueue (client + server)
- [ ] Implement vkDestroyDevice (client + server)
- [ ] Test complete workflow

### Day 7: Testing & Integration
- [ ] Implement phase03 test app
- [ ] Test end-to-end
- [ ] Fix bugs
- [ ] Run regression tests (Phases 1-2 still work)
- [ ] Update documentation
- [ ] Verify no memory leaks

## Success Criteria

- [ ] All code compiles without warnings
- [ ] Phase 3 test app passes
- [ ] Regression tests pass (Phases 1-2)
- [ ] Fake GPU data matches specifications
- [ ] Queue family validation works correctly
- [ ] Device creation with multiple queues works
- [ ] No memory leaks (valgrind clean)
- [ ] Server handles multiple clients creating devices

## Deliverables

- [ ] Fake GPU data generator
- [ ] Physical device query implementations (4 commands)
- [ ] Device creation/destruction implementations
- [ ] Queue retrieval implementation
- [ ] Client physical device state management
- [ ] Client device state management
- [ ] Server device state management
- [ ] Phase 3 test application
- [ ] Updated CMakeLists.txt
- [ ] Documentation updates

## Common Issues & Solutions

**Issue**: VkDeviceCreateInfo encoding fails
**Solution**: Ensure proper handling of dynamic arrays and nested structures

**Issue**: Queue family index validation fails
**Solution**: Check that requested queue family index < queue family count

**Issue**: Memory properties encoding incorrect
**Solution**: Properly encode memoryTypeCount, memoryTypes array, memoryHeapCount, memoryHeaps array

**Issue**: Device not cleaned up properly
**Solution**: Ensure all associated queues are removed from handle map

## Next Steps

Once Phase 3 is complete and all tests pass, proceed to **[PHASE_04.md](PHASE_04.md)** for resource management (buffers, images, memory).
