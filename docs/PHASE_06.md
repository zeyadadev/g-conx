# Phase 06: Fake Command Submission

**Submit commands to queue with fake execution**

## Overview

**Goal**: Implement synchronization objects and queue submission. Server immediately signals fences (no real execution).

**Duration**: 7 days (Days 29-35)

**Prerequisites**: Phase 05 complete and passing

## Objectives

- ✅ Client can create/destroy fences and semaphores
- ✅ Client can submit command buffers to queues
- ✅ Client can wait for fences
- ✅ Server immediately signals fences (fake completion)
- ✅ Test app successfully submits and waits

**🎉 MILESTONE**: Complete fake Vulkan pipeline!

## Commands to Implement

### Fence Commands
- [ ] `vkCreateFence`
- [ ] `vkDestroyFence`
- [ ] `vkGetFenceStatus`
- [ ] `vkResetFences`
- [ ] `vkWaitForFences`

### Semaphore Commands  
- [ ] `vkCreateSemaphore`
- [ ] `vkDestroySemaphore`
- [ ] `vkGetSemaphoreCounterValue` (timeline semaphores)
- [ ] `vkWaitSemaphores` (timeline semaphores)
- [ ] `vkSignalSemaphore` (timeline semaphores)

### Queue Commands
- [ ] `vkQueueSubmit`
- [ ] `vkQueueWaitIdle`
- [ ] `vkDeviceWaitIdle`

## New Components

### Server Components
**1. Sync Object Manager** (`server/state/sync_manager.h/cpp`)
- Track fences and their signaled state
- Track semaphores
- Support timeline semaphores
- Immediately signal fences on submission

**2. Submission Tracker** (`server/state/submission_tracker.h/cpp`)
- Track submitted command buffers
- Associate submissions with fences/semaphores

### Client Components
**1. Sync State** (`client/state/sync_state.h/cpp`)
- Track fence signaled state (cached from server)
- Track semaphore values (for timeline)

## Fake Synchronization Behavior

```
For this phase (fake execution):
  - vkQueueSubmit: Server immediately marks fence as signaled
  - vkWaitForFences: Client queries server, server returns signaled
  - vkGetFenceStatus: Server always returns VK_SUCCESS (signaled)
  - Timeline semaphores: Server immediately advances to signaled value
  
Later phase (Phase 7+, real execution):
  - Actually wait for GPU work to complete
```

## Detailed Requirements

### vkCreateFence
**Client**:
- [ ] Encode VkFenceCreateInfo (flags - signaled or unsignaled)
- [ ] Allocate fence handle
- [ ] Store initial state based on flags

**Server**:
- [ ] Store fence handle mapping
- [ ] Track initial signaled state
- [ ] If CREATE_SIGNALED_BIT: mark as signaled immediately

### vkWaitForFences
**Client**:
- [ ] Encode: fence count, fence array, waitAll flag, timeout
- [ ] Send to server
- [ ] Receive result (success or timeout)
- [ ] For fake impl: server always returns success immediately

**Server**:
- [ ] Decode parameters
- [ ] For fake impl: always return VK_SUCCESS (all fences signaled)
- [ ] For Phase 7+: actually check fence status

### vkQueueSubmit
**Client**:
- [ ] Encode VkSubmitInfo array:
  - waitSemaphoreCount, pWaitSemaphores, pWaitDstStageMask
  - commandBufferCount, pCommandBuffers
  - signalSemaphoreCount, pSignalSemaphores
- [ ] Encode fence handle (can be VK_NULL_HANDLE)
- [ ] Send submission

**Server**:
- [ ] Decode submission
- [ ] Validate command buffers are in EXECUTABLE state
- [ ] Validate semaphores exist
- [ ] For fake impl: immediately signal fence if provided
- [ ] For fake impl: immediately signal all signal semaphores
- [ ] Track submission

### vkQueueWaitIdle
**Client**:
- [ ] Encode queue handle
- [ ] Send command

**Server**:
- [ ] For fake impl: return immediately
- [ ] For Phase 7+: wait for all submissions on this queue

### vkDeviceWaitIdle
**Client**:
- [ ] Encode device handle
- [ ] Send command

**Server**:
- [ ] For fake impl: return immediately
- [ ] For Phase 7+: wait for all queues on this device

## VkSubmitInfo Encoding

```
Structure to encode:
  VkSubmitInfo {
    pNext
    waitSemaphoreCount
    pWaitSemaphores → VkSemaphore array
    pWaitDstStageMask → VkPipelineStageFlags array
    commandBufferCount
    pCommandBuffers → VkCommandBuffer array
    signalSemaphoreCount
    pSignalSemaphores → VkSemaphore array
  }

Encoding order:
  1. Encode waitSemaphoreCount
  2. If count > 0:
     - Encode wait semaphores array
     - Encode wait stage masks array
  3. Encode commandBufferCount
  4. If count > 0:
     - Encode command buffers array
  5. Encode signalSemaphoreCount
  6. If count > 0:
     - Encode signal semaphores array
```

## Test Application

**File**: `test-app/phase06/phase06_test.cpp`

**Test Steps**:
1. [ ] Setup: instance, device, queue (Phases 2-3)
2. [ ] Create command pool and buffer (Phase 5)
3. [ ] Record simple commands (Phase 5)
4. [ ] Create fence (unsignaled)
5. [ ] Create 2 semaphores (wait and signal)
6. [ ] Submit command buffer with fence
7. [ ] Wait for fence (should return immediately in fake mode)
8. [ ] Verify fence is signaled
9. [ ] Cleanup

**Expected Output**:
```
Phase 6: Fake Command Submission
=================================
✅ vkCreateFence succeeded
✅ vkCreateSemaphore (wait) succeeded
✅ vkCreateSemaphore (signal) succeeded
✅ vkQueueSubmit succeeded
✅ vkWaitForFences (timeout=1s) succeeded
✅ Fence signaled immediately (fake execution)
✅ vkQueueWaitIdle succeeded
✅ Cleanup succeeded
✅ Phase 6 PASSED

🎉 MILESTONE: Complete fake Vulkan pipeline!
```

## Implementation Checklist

### Days 1-2: Fence Implementation
- [ ] Implement fence creation/destruction
- [ ] Implement fence wait/reset
- [ ] Implement fence status query
- [ ] Fake signaling logic

### Days 3-4: Semaphore Implementation
- [ ] Implement binary semaphores
- [ ] Implement timeline semaphores
- [ ] Semaphore wait/signal

### Days 5-6: Queue Submission
- [ ] Implement vkQueueSubmit
- [ ] Handle VkSubmitInfo encoding
- [ ] Implement vkQueueWaitIdle
- [ ] Implement vkDeviceWaitIdle
- [ ] Fake immediate completion

### Day 7: Testing
- [ ] Phase 6 test app
- [ ] Test complex submissions (multiple buffers, semaphores)
- [ ] Regression tests
- [ ] Fix bugs

## Success Criteria

- [ ] Fence creation/wait works
- [ ] Semaphore creation works
- [ ] Queue submission succeeds
- [ ] Wait operations return immediately (fake)
- [ ] Phase 6 test passes
- [ ] All regression tests pass (Phases 1-5)
- [ ] No memory leaks

## Deliverables

- [ ] Fence commands (5 commands)
- [ ] Semaphore commands (5 commands)
- [ ] Queue commands (3 commands)
- [ ] Sync object manager
- [ ] Submission tracker
- [ ] Phase 6 test application

## Important Notes

**Fake Execution in This Phase**:
- Server does NOT execute commands on GPU
- Fences are signaled immediately
- This tests the communication pipeline
- Real execution comes in Phase 7

**Why This Approach**:
- Validates entire protocol stack first
- Easier to debug communication issues
- Ensures state management works
- Phase 7 adds real GPU execution to working base

## Next Steps

**🎉 Congratulations!** You now have a complete (fake) Vulkan ICD pipeline!

Proceed to **[PHASE_07.md](PHASE_07.md)** to add REAL GPU execution!
