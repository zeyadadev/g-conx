# Phase 05: Fake Command Recording

**Record command buffers without execution**

## Overview

**Goal**: Implement command pool and command buffer commands with fake recording (server validates syntax only).

**Duration**: 7 days (Days 22-28)

**Prerequisites**: Phase 04 complete and passing

## Objectives

- ✅ Client can create/destroy command pools
- ✅ Client can allocate/free command buffers
- ✅ Client can record commands (begin/end)
- ✅ Client can record basic transfer commands (copy, fill)
- ✅ Server validates command buffer state transitions
- ✅ Server validates recorded commands (syntax only, no execution)
- ✅ Test app successfully records command buffer

## Commands to Implement

### Command Pool
- [ ] `vkCreateCommandPool`
- [ ] `vkDestroyCommandPool`
- [ ] `vkResetCommandPool`

### Command Buffer
- [ ] `vkAllocateCommandBuffers`
- [ ] `vkFreeCommandBuffers`
- [ ] `vkBeginCommandBuffer`
- [ ] `vkEndCommandBuffer`
- [ ] `vkResetCommandBuffer`

### Recording Commands (Basic Transfer)
- [ ] `vkCmdCopyBuffer`
- [ ] `vkCmdCopyImage`
- [ ] `vkCmdBlitImage`
- [ ] `vkCmdCopyBufferToImage`
- [ ] `vkCmdCopyImageToBuffer`
- [ ] `vkCmdFillBuffer`
- [ ] `vkCmdUpdateBuffer`
- [ ] `vkCmdClearColorImage`

## New Components

### Server Components
**1. Command Buffer State Machine** (`server/state/command_buffer_state.h/cpp`)
- Track command buffer state: INITIAL, RECORDING, EXECUTABLE, INVALID
- Validate state transitions
- Track parent pool

**2. Command Validator** (`server/state/command_validator.h/cpp`)
- Validate vkCmd* commands (syntax only)
- Check resource handles exist
- Check buffer/image compatibility

### Client Components
**1. Command Buffer State** (`client/state/command_buffer_state.h/cpp`)
- Track recording state
- Cache command pool association

## Command Buffer State Machine

```
States:
  INITIAL    → After allocation
  RECORDING  → After vkBeginCommandBuffer
  EXECUTABLE → After vkEndCommandBuffer
  INVALID    → After error or parent pool reset

Valid transitions:
  INITIAL    → RECORDING   (vkBeginCommandBuffer)
  RECORDING  → EXECUTABLE  (vkEndCommandBuffer)
  EXECUTABLE → RECORDING   (vkBeginCommandBuffer with SIMULTANEOUS_USE)
  RECORDING  → INVALID     (error during recording)
  Any        → INITIAL     (vkResetCommandBuffer)
```

## Detailed Requirements

### vkCreateCommandPool
- [ ] Encode VkCommandPoolCreateInfo (flags, queueFamilyIndex)
- [ ] Allocate client handle
- [ ] Server validates queue family index
- [ ] Server stores pool handle mapping

### vkAllocateCommandBuffers
- [ ] Encode VkCommandBufferAllocateInfo
- [ ] Handle array of command buffers
- [ ] Server allocates multiple handles
- [ ] Server associates buffers with pool
- [ ] Set initial state to INITIAL

### vkBeginCommandBuffer
- [ ] Verify command buffer in INITIAL or EXECUTABLE state
- [ ] Encode VkCommandBufferBeginInfo
- [ ] Handle inheritance info (can be NULL for primary buffers)
- [ ] Server transitions state to RECORDING

### vkCmdCopyBuffer
- [ ] Verify command buffer in RECORDING state
- [ ] Encode src buffer, dst buffer, region count, regions array
- [ ] Each VkBufferCopy: srcOffset, dstOffset, size
- [ ] Server validates:
  - Source and dest buffers exist
  - Offsets + size within buffer bounds
  - No overlap (optional validation)

### vkCmdFillBuffer
- [ ] Verify RECORDING state
- [ ] Encode buffer, offset, size, data (uint32_t)
- [ ] Server validates buffer exists, offset+size valid

### vkEndCommandBuffer
- [ ] Verify RECORDING state
- [ ] Send command
- [ ] Server transitions state to EXECUTABLE

## Test Application

**File**: `test-app/phase05/phase05_test.cpp`

**Test Steps**:
1. [ ] Create device (Phases 2-3)
2. [ ] Create command pool (graphics queue family)
3. [ ] Allocate 1 primary command buffer
4. [ ] Create 2 buffers (src, dst - from Phase 4)
5. [ ] Begin command buffer
6. [ ] Record vkCmdFillBuffer (fill src with pattern)
7. [ ] Record vkCmdCopyBuffer (src → dst)
8. [ ] End command buffer
9. [ ] Free command buffer
10. [ ] Destroy command pool
11. [ ] Cleanup

**Expected Output**:
```
Phase 5: Fake Command Recording
================================
✅ vkCreateCommandPool succeeded
✅ vkAllocateCommandBuffers (1 buffer) succeeded
✅ vkBeginCommandBuffer succeeded
✅ vkCmdFillBuffer recorded
✅ vkCmdCopyBuffer recorded
✅ vkEndCommandBuffer succeeded
✅ Command buffer state: EXECUTABLE
✅ Cleanup succeeded
✅ Phase 5 PASSED
```

## Implementation Checklist

### Days 1-2: Command Pool
- [ ] Implement command pool creation/destruction
- [ ] Implement pool state tracking

### Days 3-4: Command Buffer Lifecycle
- [ ] Implement allocation/free
- [ ] Implement begin/end
- [ ] Implement state machine
- [ ] State validation

### Days 5-6: Recording Commands
- [ ] Implement vkCmdCopyBuffer
- [ ] Implement vkCmdFillBuffer
- [ ] Implement command validator
- [ ] Add other transfer commands

### Day 7: Testing
- [ ] Phase 5 test app
- [ ] Regression tests
- [ ] Fix bugs

## Success Criteria
- [ ] State machine works correctly
- [ ] Command validation catches errors
- [ ] No crashes on invalid state transitions
- [ ] Phase 5 test passes
- [ ] Regression tests pass

## Deliverables
- [ ] Command pool management (3 commands)
- [ ] Command buffer lifecycle (5 commands)
- [ ] Recording commands (8 commands)
- [ ] State machine implementation
- [ ] Command validator
- [ ] Phase 5 test application

## Next Steps
Proceed to **[PHASE_06.md](PHASE_06.md)** for command submission.
