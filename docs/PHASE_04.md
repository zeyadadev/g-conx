# Phase 04: Fake Resource Management

**Create buffers, images, and memory with fake allocation**

## Overview

**Goal**: Implement buffer, image, and memory management commands without actual GPU resource allocation.

**Duration**: 7 days (Days 15-21)

**Prerequisites**: Phase 03 complete and passing

## Objectives

By the end of this phase:

- ✅ Client can create and destroy buffers
- ✅ Client can create and destroy images
- ✅ Client can allocate and free memory
- ✅ Client can bind buffers/images to memory
- ✅ Server tracks resources without real allocation
- ✅ Server generates fake but valid memory requirements
- ✅ Test app successfully manages resources

## Commands to Implement

### Buffer Commands
- [ ] `vkCreateBuffer`
- [ ] `vkDestroyBuffer`
- [ ] `vkGetBufferMemoryRequirements`
- [ ] `vkGetBufferMemoryRequirements2` (optional)
- [ ] `vkBindBufferMemory`
- [ ] `vkBindBufferMemory2` (optional)

### Image Commands
- [ ] `vkCreateImage`
- [ ] `vkDestroyImage`
- [ ] `vkGetImageMemoryRequirements`
- [ ] `vkGetImageMemoryRequirements2` (optional)
- [ ] `vkGetImageSubresourceLayout`
- [ ] `vkBindImageMemory`
- [ ] `vkBindImageMemory2` (optional)

### Memory Commands
- [ ] `vkAllocateMemory`
- [ ] `vkFreeMemory`
- [ ] `vkGetDeviceMemoryCommitment` (optional)

## New Components Required

### Server Components

**1. Resource Tracker** (`server/state/resource_tracker.h/cpp`)
- Track all created resources (buffers, images, memory)
- Associate resources with owning device
- Track bindings between resources and memory
- Support resource queries by handle

**2. Memory Requirements Generator** (`server/state/memory_requirements.h/cpp`)
- Generate fake but valid VkMemoryRequirements
- Calculate alignment based on resource type
- Select appropriate memory type bits

**3. Binding Validator** (`server/state/binding_validator.h/cpp`)
- Validate buffer-to-memory bindings
- Validate image-to-memory bindings
- Check memory offset alignment
- Check memory size sufficiency

### Client Components

**1. Resource State Manager** (`client/state/resource_state.h/cpp`)
- Track created buffers and images
- Cache memory requirements
- Track memory allocations
- Track resource-memory bindings

## Fake Resource Specifications

### Buffer Memory Requirements Generation

```
For VkBuffer:
  size = requested buffer size (rounded up to 256-byte alignment)
  alignment = 256 bytes
  memoryTypeBits = 0x3 (supports memory types 0 and 1)
```

### Image Memory Requirements Generation

```
For VkImage:
  size = calculate based on:
    - format (bytes per pixel)
    - dimensions (width × height × depth)
    - mip levels
    - array layers
    - round up to 4KB alignment
  alignment = 4096 bytes (4KB)
  memoryTypeBits = 0x1 (supports only memory type 0 - device local)
```

### Memory Allocation Tracking

```
Track for each VkDeviceMemory:
  - Allocation size
  - Memory type index
  - Allocation flags (if any)
  - Bound resources (list of buffers/images)
```

## Detailed Requirements

### vkCreateBuffer

**Client Requirements**:
- [ ] Verify device is valid
- [ ] Allocate VkBuffer handle
- [ ] Encode VkBufferCreateInfo:
  - flags
  - size
  - usage
  - sharingMode
  - queueFamilyIndexCount
  - pQueueFamilyIndices array
- [ ] Send command
- [ ] Receive reply
- [ ] Store buffer in state

**Server Requirements**:
- [ ] Decode VkBufferCreateInfo
- [ ] Validate size > 0
- [ ] Validate usage flags
- [ ] Store buffer handle mapping
- [ ] Track buffer in resource tracker
- [ ] Send success reply

### vkGetBufferMemoryRequirements

**Client Requirements**:
- [ ] Verify buffer is valid
- [ ] Send buffer handle
- [ ] Receive VkMemoryRequirements
- [ ] Cache requirements in buffer state
- [ ] Return to application

**Server Requirements**:
- [ ] Decode buffer handle
- [ ] Look up buffer in resource tracker
- [ ] Generate fake memory requirements (see spec above)
- [ ] Send reply with requirements

### vkAllocateMemory

**Client Requirements**:
- [ ] Verify device is valid
- [ ] Allocate VkDeviceMemory handle
- [ ] Encode VkMemoryAllocateInfo:
  - allocationSize
  - memoryTypeIndex
  - pNext chain (can be NULL)
- [ ] Send command
- [ ] Receive reply
- [ ] Store memory in state

**Server Requirements**:
- [ ] Decode VkMemoryAllocateInfo
- [ ] Validate allocationSize > 0
- [ ] Validate memoryTypeIndex < memory type count
- [ ] Store memory handle mapping
- [ ] Track allocation in resource tracker
- [ ] Send success reply

### vkBindBufferMemory

**Client Requirements**:
- [ ] Verify device, buffer, and memory are valid
- [ ] Verify buffer not already bound
- [ ] Encode device, buffer, memory, memoryOffset
- [ ] Send command
- [ ] Receive reply
- [ ] Update buffer state (mark as bound)

**Server Requirements**:
- [ ] Decode device, buffer, memory, memoryOffset
- [ ] Validate buffer exists and not already bound
- [ ] Validate memory exists
- [ ] Validate offset alignment (use buffer's requirements)
- [ ] Validate offset + buffer size <= memory size
- [ ] Record binding in resource tracker
- [ ] Send success reply

### vkCreateImage

**Client Requirements**:
- [ ] Verify device is valid
- [ ] Allocate VkImage handle
- [ ] Encode VkImageCreateInfo:
  - flags
  - imageType
  - format
  - extent (width, height, depth)
  - mipLevels
  - arrayLayers
  - samples
  - tiling
  - usage
  - sharingMode
  - queueFamilyIndexCount
  - pQueueFamilyIndices
  - initialLayout
- [ ] Send command
- [ ] Receive reply
- [ ] Store image in state

**Server Requirements**:
- [ ] Decode VkImageCreateInfo
- [ ] Validate parameters (extent > 0, mipLevels > 0, etc.)
- [ ] Store image handle mapping
- [ ] Track image in resource tracker
- [ ] Send success reply

### vkGetImageMemoryRequirements

**Client Requirements**:
- [ ] Similar to buffer requirements
- [ ] Cache in image state

**Server Requirements**:
- [ ] Generate requirements based on image properties
- [ ] Use formula from spec above
- [ ] Send reply

### vkBindImageMemory

**Client Requirements**:
- [ ] Similar to buffer binding
- [ ] Handle image-specific validation

**Server Requirements**:
- [ ] Similar to buffer binding
- [ ] Use image alignment requirements

### vkFreeMemory

**Client Requirements**:
- [ ] Verify device and memory are valid
- [ ] Verify no resources still bound (optional check)
- [ ] Send command
- [ ] Receive reply
- [ ] Remove from state

**Server Requirements**:
- [ ] Decode memory handle
- [ ] Verify all bindings are released (optional)
- [ ] Remove from handle map
- [ ] Remove from resource tracker
- [ ] Send success reply

### vkDestroyBuffer / vkDestroyImage

**Client Requirements**:
- [ ] Verify resource is valid
- [ ] Send command
- [ ] Receive reply
- [ ] Remove from state

**Server Requirements**:
- [ ] Verify resource exists
- [ ] Remove any bindings
- [ ] Remove from handle map
- [ ] Remove from resource tracker
- [ ] Send success reply

## Resource Tracker Specification

### Data Structure

```
For each client:
  - Map<VkBuffer, BufferInfo>
    - BufferInfo: size, usage, bound_memory, bound_offset
  - Map<VkImage, ImageInfo>
    - ImageInfo: format, extent, mipLevels, bound_memory, bound_offset
  - Map<VkDeviceMemory, MemoryInfo>
    - MemoryInfo: size, type_index, bound_resources[]
```

### Operations

- [ ] Add resource (buffer/image/memory)
- [ ] Remove resource
- [ ] Query resource info
- [ ] Record binding
- [ ] Remove binding
- [ ] Query bindings for memory
- [ ] Validate binding (alignment, size)

## Test Application Requirements

### Test Scenario

**File**: `test-app/phase04/phase04_test.cpp`

**Test Steps**:
1. [ ] Create instance and device (Phases 2-3)
2. [ ] Create buffer (1MB, transfer src|dst usage)
3. [ ] Get buffer memory requirements
4. [ ] Verify requirements: size >= 1MB, alignment = 256
5. [ ] Allocate memory (requirements.size, host visible type)
6. [ ] Bind buffer to memory
7. [ ] Create image (256x256 RGBA, optimal tiling)
8. [ ] Get image memory requirements
9. [ ] Allocate memory for image (device local type)
10. [ ] Bind image to memory
11. [ ] Destroy buffer
12. [ ] Destroy image
13. [ ] Free both memory allocations
14. [ ] Destroy device and instance

**Expected Output**:
```
Phase 4: Fake Resource Management
==================================
✅ vkCreateBuffer (1MB) succeeded
✅ Memory requirements: size=1048576, alignment=256
✅ vkAllocateMemory (host visible) succeeded
✅ vkBindBufferMemory succeeded
✅ vkCreateImage (256x256 RGBA) succeeded
✅ Memory requirements: size=262144, alignment=4096
✅ vkAllocateMemory (device local) succeeded
✅ vkBindImageMemory succeeded
✅ Resource cleanup succeeded
✅ Phase 4 PASSED
```

## Implementation Checklist

### Days 1-2: Buffer Management
- [ ] Implement resource tracker
- [ ] Implement memory requirements generator
- [ ] Implement vkCreateBuffer (client + server)
- [ ] Implement vkGetBufferMemoryRequirements (client + server)
- [ ] Implement vkDestroyBuffer (client + server)
- [ ] Test buffer creation/destruction

### Days 3-4: Memory Management
- [ ] Implement vkAllocateMemory (client + server)
- [ ] Implement vkFreeMemory (client + server)
- [ ] Implement vkBindBufferMemory (client + server)
- [ ] Implement binding validator
- [ ] Test buffer-memory binding

### Days 5-6: Image Management
- [ ] Implement vkCreateImage (client + server)
- [ ] Implement vkGetImageMemoryRequirements (client + server)
- [ ] Implement vkBindImageMemory (client + server)
- [ ] Implement vkDestroyImage (client + server)
- [ ] Test image-memory binding

### Day 7: Testing & Integration
- [ ] Implement phase04 test app
- [ ] Test complex scenarios (multiple resources)
- [ ] Test binding validation errors
- [ ] Run regression tests
- [ ] Fix bugs
- [ ] Update documentation

## Success Criteria

- [ ] All code compiles without warnings
- [ ] Phase 4 test app passes
- [ ] Regression tests pass (Phases 1-3)
- [ ] Memory requirements calculation is correct
- [ ] Binding validation works (alignment, size)
- [ ] Multiple resources can be bound to same memory (if offsets allow)
- [ ] No memory leaks
- [ ] Server handles resource cleanup on client disconnect

## Deliverables

- [ ] Resource tracker implementation
- [ ] Memory requirements generator
- [ ] Binding validator
- [ ] Buffer management commands (3 commands)
- [ ] Image management commands (4 commands)
- [ ] Memory management commands (2 commands)
- [ ] Client resource state management
- [ ] Server resource state management
- [ ] Phase 4 test application
- [ ] Updated CMakeLists.txt
- [ ] Documentation updates

## Common Issues & Solutions

**Issue**: Memory requirement size calculation wrong
**Solution**: Account for alignment rounding and format bytes-per-pixel

**Issue**: Binding validation fails incorrectly
**Solution**: Check offset is aligned to requirements.alignment

**Issue**: Resource not cleaned up on destroy
**Solution**: Remove from all tracking structures (handle map + resource tracker)

**Issue**: Multiple bindings to same memory fail
**Solution**: Allow multiple bindings if offsets don't overlap

## Next Steps

Once Phase 4 is complete and all tests pass, proceed to **[PHASE_05.md](PHASE_05.md)** for command recording.
