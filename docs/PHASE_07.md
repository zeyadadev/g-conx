# Phase 07: Real GPU Execution

**Server executes REAL Vulkan commands on GPU**

## Overview

**Goal**: Replace fake execution with real Vulkan API calls on server GPU.

**Duration**: 14 days (Days 36-49)

**Prerequisites**: Phase 06 complete and passing

## Objectives

- ✅ Server initializes real Vulkan instance and device
- ✅ Server creates real GPU resources
- ✅ Server executes real command buffers
- ✅ Handle mapping: client handles ↔ real GPU handles
- ✅ Real fences signal when GPU work completes
- ✅ Vulkan validation layers work on server
- ✅ Test app triggers real GPU work

**🎉 MILESTONE**: First real GPU execution over network!

## Major Changes

### Server Initialization

**New: Real Vulkan Setup** (`server/vulkan_init.cpp`)
- [ ] Create real VkInstance on server startup
- [ ] Enumerate and select physical device
- [ ] Create real VkDevice
- [ ] Get real queue handles
- [ ] Enable validation layers (for debugging)

### Handle Mapping Upgrade

**Old (Phases 2-6)**:
```
client_handle → client_handle (fake mapping)
```

**New (Phase 7+)**:
```
client_handle → real_gpu_handle (bidirectional mapping)
```

### Command Execution

**Old (Phases 2-6)**:
```
Server receives command → Validates → Returns success (no GPU work)
```

**New (Phase 7+)**:
```
Server receives command → Translates handles → Calls real vkXXX → Returns real result
```

## Detailed Requirements

### Server Startup
**File**: `server/vulkan/vulkan_init.cpp`

- [ ] Create VkInstance with validation layers
- [ ] Enumerate physical devices, select first discrete GPU
- [ ] Query physical device properties (use real values now)
- [ ] Create VkDevice with required queues
- [ ] Store real instance, physical device, device globally
- [ ] Store real queue handles

### vkCreateBuffer (Real Implementation)
**Old behavior**: Store fake mapping
**New behavior**:
- [ ] Decode client's VkBufferCreateInfo
- [ ] Translate device handle: `real_device = handle_map[client_device]`
- [ ] Call real `vkCreateBuffer(real_device, pCreateInfo, NULL, &real_buffer)`
- [ ] Store mapping: `handle_map[client_buffer] = real_buffer`
- [ ] Return real VkResult

### vkAllocateMemory (Real Implementation)
**New behavior**:
- [ ] Translate device handle
- [ ] Call real `vkAllocateMemory(real_device, pAllocateInfo, NULL, &real_memory)`
- [ ] Store mapping: `handle_map[client_memory] = real_memory`
- [ ] Return result

### vkBindBufferMemory (Real Implementation)
**New behavior**:
- [ ] Translate device, buffer, memory handles
- [ ] Call real `vkBindBufferMemory(real_device, real_buffer, real_memory, offset)`
- [ ] Return result

### vkQueueSubmit (Real Implementation)
**New behavior**:
- [ ] Translate queue handle: `real_queue = handle_map[client_queue]`
- [ ] For each VkSubmitInfo:
  - Translate wait semaphore handles
  - Translate command buffer handles
  - Translate signal semaphore handles
- [ ] Translate fence handle (if provided)
- [ ] Call real `vkQueueSubmit(real_queue, submitCount, pSubmits, real_fence)`
- [ ] Return result

**IMPORTANT**: GPU work is now asynchronous! Fence signals when GPU finishes.

### vkWaitForFences (Real Implementation)
**Old behavior**: Return immediately (fake signaled)
**New behavior**:
- [ ] Translate fence handles to real fences
- [ ] Call real `vkWaitForFences(real_device, fenceCount, pFences, waitAll, timeout)`
- [ ] Return real result (VK_SUCCESS or VK_TIMEOUT)

### Handle Translation Helper
**File**: `server/vulkan/handle_translator.h/cpp`

```
Template function: translate_handle<T>

Purpose: Convert client handle to real handle

Requirements:
  - Fast lookup (O(1))
  - Type-safe
  - Return VK_NULL_HANDLE if not found
  - Support all Vulkan handle types
```

### Resource Cleanup
**Important**: When server destroys resources:
- [ ] Call real vkDestroyXXX on real handle
- [ ] Remove from handle map
- [ ] Remove from resource tracker

## Validation Layers

### Server-Side Validation

**Enable on server startup**:
```
Layers to enable:
  - VK_LAYER_KHRONOS_validation

Instance create info:
  - enabledLayerCount = 1
  - ppEnabledLayerNames = ["VK_LAYER_KHRONOS_validation"]

Device create info:
  - enabledLayerCount = 1
  - ppEnabledLayerNames = ["VK_LAYER_KHRONOS_validation"]
```

**Set up debug messenger**:
- [ ] Implement VkDebugUtilsMessengerEXT
- [ ] Log validation errors to server console
- [ ] Optionally send validation errors to client

### Testing with Validation

**Run server**:
```bash
./venus-server --validation
```

**Expected**: Server logs validation messages if any Vulkan usage errors occur

## Test Application

**File**: `test-app/phase07/phase07_test.cpp`

**Test Steps**:
1. [ ] Create instance, device, queue
2. [ ] Create staging buffer (host visible)
3. [ ] Allocate and bind memory
4. [ ] Map memory, write test pattern
5. [ ] Unmap memory
6. [ ] Create device buffer (device local)
7. [ ] Allocate and bind memory
8. [ ] Create command buffer
9. [ ] Record: vkCmdCopyBuffer (staging → device)
10. [ ] Create fence
11. [ ] Submit to queue with fence
12. [ ] Wait for fence (REAL wait this time!)
13. [ ] Verify fence is signaled
14. [ ] Cleanup

**Expected Output**:
```
Phase 7: Real GPU Execution
============================
Server GPU: NVIDIA GeForce RTX 3080
✅ Created staging buffer (host visible)
✅ Allocated and bound memory
✅ Created device buffer (device local)
✅ Allocated and bound memory
✅ Recorded copy command
✅ Submitted to queue
✅ Waiting for fence...
✅ Fence signaled after GPU execution (took 5ms)
✅ Real GPU work completed!
✅ No validation errors
✅ Phase 7 PASSED

🎉 MILESTONE: First real GPU execution over network!
```

## Implementation Checklist

### Days 1-3: Server Vulkan Initialization
- [ ] Implement server Vulkan instance creation
- [ ] Implement physical device selection
- [ ] Implement device creation
- [ ] Implement queue retrieval
- [ ] Test initialization

### Days 4-6: Handle Translation
- [ ] Upgrade handle map to bidirectional
- [ ] Implement handle translator helper
- [ ] Update all command handlers to translate handles

### Days 7-10: Real Resource Creation
- [ ] Update vkCreateBuffer (call real Vulkan)
- [ ] Update vkAllocateMemory (call real Vulkan)
- [ ] Update vkBindBufferMemory (call real Vulkan)
- [ ] Update vkCreateImage (call real Vulkan)
- [ ] Update all resource commands
- [ ] Test resource creation

### Days 11-12: Real Command Execution
- [ ] Update vkQueueSubmit (call real Vulkan)
- [ ] Update vkWaitForFences (call real Vulkan)
- [ ] Test submission and waiting

### Days 13-14: Testing & Validation
- [ ] Enable validation layers
- [ ] Implement phase 7 test app
- [ ] Test end-to-end
- [ ] Fix all validation errors
- [ ] Regression tests
- [ ] Performance testing (measure latency)

## Success Criteria

- [ ] Server initializes real Vulkan
- [ ] All commands call real Vulkan API
- [ ] Handle translation works correctly
- [ ] Real GPU executes commands
- [ ] Fences signal after real GPU work
- [ ] No validation errors
- [ ] Phase 7 test passes
- [ ] Regression tests pass
- [ ] No memory leaks on server
- [ ] Server handles client disconnect gracefully

## Deliverables

- [ ] Server Vulkan initialization code
- [ ] Handle translator
- [ ] Updated command handlers (all call real Vulkan)
- [ ] Validation layer integration
- [ ] Phase 7 test application
- [ ] Performance measurements

## Common Issues & Solutions

**Issue**: Validation layer not found
**Solution**: Install vulkan-validation-layers package

**Issue**: Handle translation returns VK_NULL_HANDLE
**Solution**: Verify handle was created and mapping stored

**Issue**: GPU crashes on submission
**Solution**: Check validation errors, verify resource bindings

**Issue**: Fence never signals
**Solution**: Verify real vkQueueSubmit succeeded, check queue is correct

**Issue**: Memory not accessible
**Solution**: Verify memory type is host visible for mapping

## Performance Notes

**Expected Latency**:
- Command encoding: < 1ms
- Network transfer (localhost): < 1ms
- GPU execution: varies (1-100ms depending on work)
- Total round-trip: ~2-100ms

**Optimization opportunities** (future):
- Batch multiple commands
- Async submission (don't wait for reply)
- Command buffering

## Next Steps

**🎉 Congratulations!** You now have real GPU execution over network!

Proceed to **[PHASE_08.md](PHASE_08.md)** to implement memory data transfer.
