# Venus Plus Development Roadmap

**Phase-by-phase implementation plan with milestones and deliverables**

## Table of Contents

1. [Overview](#overview)
2. [Phase Timeline](#phase-timeline)
3. [Phase Summaries](#phase-summaries)
4. [Phase Dependencies](#phase-dependencies)
5. [Progress Tracking](#progress-tracking)
6. [Milestone Checklist](#milestone-checklist)

## Overview

Venus Plus development follows an **incremental, test-driven approach** with 10 distinct phases. Each phase:

- ✅ Builds on previous phases
- ✅ Has clear, measurable deliverables
- ✅ Includes end-to-end tests
- ✅ Is fully documented before moving to next phase
- ✅ Results in working (even if limited) client-server communication

**Key Principle**: **"Always have something working"**

From day 1, we have a complete (though minimal) system that runs end-to-end. Each phase adds functionality while maintaining backward compatibility.

## Phase Timeline

```
Phase 01: ████████░░ (Days  1-3)   Network Communication
Phase 02: ░░░░░░░░░░ (Days  4-7)   Fake Instance
Phase 03: ░░░░░░░░░░ (Days  8-14)  Fake Device
Phase 04: ░░░░░░░░░░ (Days 15-21)  Fake Resources
Phase 05: ░░░░░░░░░░ (Days 22-28)  Fake Commands
Phase 06: ░░░░░░░░░░ (Days 29-35)  Fake Submission
Phase 07: ░░░░░░░░░░ (Days 36-49)  Real Execution (Simple)
Phase 08: ░░░░░░░░░░ (Days 50-60)  Memory Transfer
Phase 09: ░░░░░░░░░░ (Days 61-75)  Compute Shaders
Phase 10: ░░░░░░░░░░ (Days 76-90)  Graphics Rendering

Total: ~90 days (3 months)
```

## Phase Summaries

### Phase 01: Network Communication (Days 1-3)

**Goal**: Prove TCP communication works with Venus protocol encoding/decoding

**Scope**:
- Implement ONE simple command: `vkEnumerateInstanceVersion`
- Client encodes using Venus protocol
- Send over TCP socket
- Server receives, decodes, sends fake reply
- Client receives and decodes reply

**Deliverables**:
- [ ] Basic TCP client/server classes
- [ ] Venus encoder/decoder wrappers
- [ ] Message protocol (header + payload)
- [ ] Client implements `vkEnumerateInstanceVersion`
- [ ] Server handles `vkEnumerateInstanceVersion`
- [ ] Test app calls `vkEnumerateInstanceVersion` successfully
- [ ] Unit tests for encoding/decoding
- [ ] Documentation: PHASE_01.md

**Success Criteria**:
```bash
$ ./venus-server --port 5556 &
$ VK_DRIVER_FILES=./venus_icd.json ./venus-test-app --phase 1
Phase 1: Network Communication
✅ vkEnumerateInstanceVersion returned: 1.3.0
✅ PASSED
```

**Time Estimate**: 3 days

---

### Phase 02: Fake Instance (Days 4-7)

**Goal**: Create Vulkan instance with fake data

**Scope**:
- `vkCreateInstance` → Server returns fake instance handle
- `vkDestroyInstance` → Server tracks and destroys handle
- `vkEnumeratePhysicalDevices` → Server returns 1 fake GPU
- Server maintains handle mapping (client handle → server handle)

**Deliverables**:
- [ ] Handle allocator (client-side)
- [ ] Handle map (server-side: client → real)
- [ ] Client implements: `vkCreateInstance`, `vkDestroyInstance`, `vkEnumeratePhysicalDevices`
- [ ] Server handles same commands
- [ ] Test app creates instance, enumerates GPUs
- [ ] Documentation: PHASE_02.md

**Success Criteria**:
```bash
$ ./venus-test-app --phase 2
Phase 2: Fake Instance
✅ vkCreateInstance succeeded
✅ Found 1 physical device
✅ vkDestroyInstance succeeded
✅ PASSED
```

**Time Estimate**: 4 days

---

### Phase 03: Fake Device (Days 8-14)

**Goal**: Create logical device and queues

**Scope**:
- `vkGetPhysicalDeviceProperties` → Return fake GPU name, limits
- `vkGetPhysicalDeviceFeatures` → Return fake features
- `vkGetPhysicalDeviceQueueFamilyProperties` → Return fake queue families
- `vkCreateDevice` → Return fake device handle
- `vkGetDeviceQueue` → Return fake queue handle
- `vkDestroyDevice` → Cleanup

**Deliverables**:
- [ ] Fake GPU properties (name: "Venus Plus Virtual GPU")
- [ ] Fake queue families (1 graphics+compute family)
- [ ] Client implements all physical device query commands
- [ ] Client implements device creation/destruction
- [ ] Server handles all commands with fake data
- [ ] Test app creates device successfully
- [ ] Documentation: PHASE_03.md

**Success Criteria**:
```bash
$ ./venus-test-app --phase 3
Phase 3: Fake Device
✅ GPU: Venus Plus Virtual GPU
✅ Queue families: 1 (graphics+compute)
✅ vkCreateDevice succeeded
✅ vkGetDeviceQueue succeeded
✅ vkDestroyDevice succeeded
✅ PASSED
```

**Time Estimate**: 7 days

---

### Phase 04: Fake Resources (Days 15-21)

**Goal**: Create buffers, images, and memory (fake allocation)

**Scope**:
- `vkCreateBuffer` → Server returns handle, doesn't allocate real buffer
- `vkGetBufferMemoryRequirements` → Return fake requirements
- `vkCreateImage` → Server returns handle, doesn't allocate real image
- `vkGetImageMemoryRequirements` → Return fake requirements
- `vkAllocateMemory` → Server returns handle, doesn't allocate real memory
- `vkBindBufferMemory`, `vkBindImageMemory` → Server tracks binding
- Destroy commands

**Deliverables**:
- [ ] Client implements buffer/image/memory commands
- [ ] Server tracks handles without real allocation
- [ ] Resource state tracking (server-side)
- [ ] Test app creates buffer, allocates memory, binds successfully
- [ ] Documentation: PHASE_04.md

**Success Criteria**:
```bash
$ ./venus-test-app --phase 4
Phase 4: Fake Resources
✅ vkCreateBuffer (1024 bytes) succeeded
✅ vkGetBufferMemoryRequirements: size=1024
✅ vkAllocateMemory succeeded
✅ vkBindBufferMemory succeeded
✅ Resource cleanup succeeded
✅ PASSED
```

**Time Estimate**: 7 days

---

### Phase 05: Fake Command Recording (Days 22-28)

**Goal**: Record command buffers (server validates but doesn't execute)

**Scope**:
- `vkCreateCommandPool`
- `vkAllocateCommandBuffers`
- `vkBeginCommandBuffer`
- `vkCmdCopyBuffer` (first vkCmd* command!)
- `vkCmdFillBuffer`
- `vkEndCommandBuffer`
- Destroy commands

**Deliverables**:
- [ ] Client implements command pool and buffer commands
- [ ] Client implements basic vkCmd* commands
- [ ] Server validates commands (syntax check only)
- [ ] Server tracks command buffer state
- [ ] Test app records simple command buffer
- [ ] Documentation: PHASE_05.md

**Success Criteria**:
```bash
$ ./venus-test-app --phase 5
Phase 5: Fake Command Recording
✅ vkCreateCommandPool succeeded
✅ vkAllocateCommandBuffers succeeded
✅ vkBeginCommandBuffer succeeded
✅ vkCmdCopyBuffer recorded
✅ vkCmdFillBuffer recorded
✅ vkEndCommandBuffer succeeded
✅ PASSED
```

**Time Estimate**: 7 days

---

### Phase 06: Fake Submission (Days 29-35)

**Goal**: Submit commands to queue (server receives but doesn't execute)

**Scope**:
- `vkCreateFence`
- `vkCreateSemaphore`
- `vkQueueSubmit`
- `vkWaitForFences` → Server immediately signals
- `vkQueueWaitIdle` → Server returns immediately
- Destroy commands

**Deliverables**:
- [ ] Client implements sync object creation
- [ ] Client implements queue submission
- [ ] Server accepts submission, immediately signals fence
- [ ] Test app submits work and waits successfully
- [ ] Documentation: PHASE_06.md

**Success Criteria**:
```bash
$ ./venus-test-app --phase 6
Phase 6: Fake Submission
✅ vkCreateFence succeeded
✅ vkQueueSubmit succeeded
✅ vkWaitForFences (timeout=1s) succeeded
✅ Fence signaled immediately
✅ PASSED
```

**Time Estimate**: 7 days

**🎉 MILESTONE**: Complete submission pipeline (end-to-end fake Vulkan)

---

### Phase 07: Real Execution - Simple (Days 36-49)

**Goal**: Server actually calls real Vulkan for simple commands

**Scope**:
- Server creates REAL Vulkan instance, device, queues
- Server allocates REAL buffers and memory
- Server records and submits REAL commands
- Still no memory transfer (use device-local only)

**Deliverables**:
- [ ] Server initializes real Vulkan on startup
- [ ] Server creates real GPU objects
- [ ] Handle mapping: client handles ↔ real GPU handles
- [ ] Server executes real vkQueueSubmit
- [ ] Server waits for real fences
- [ ] Test app triggers real GPU work
- [ ] Validation layers enabled on server
- [ ] Documentation: PHASE_07.md

**Success Criteria**:
```bash
$ ./venus-server --validation &
$ ./venus-test-app --phase 7
Phase 7: Real Execution
✅ Server GPU: NVIDIA GeForce RTX 3080
✅ Real buffer created on server GPU
✅ Real memory allocated on server GPU
✅ Real command buffer recorded
✅ Real queue submission succeeded
✅ Real fence signaled by GPU
✅ No validation errors
✅ PASSED
```

**Time Estimate**: 14 days

**🎉 MILESTONE**: First real GPU execution over network!

---

### Phase 08: Memory Transfer (Days 50-60)

**Goal**: Transfer buffer/image data over network

**Scope**:
- `vkMapMemory` → Client prepares shadow buffer
- Custom command: `TRANSFER_MEMORY_DATA`
- `vkUnmapMemory` → Client sends data to server
- Server writes data to real GPU memory
- Add reverse transfer for reading back

**Deliverables**:
- [ ] Client shadow buffer management
- [ ] Memory transfer protocol extension
- [ ] Client sends data on unmap
- [ ] Server receives and writes to GPU memory
- [ ] Read-back support (GPU → client)
- [ ] Test app: write data, execute compute, read back
- [ ] Documentation: PHASE_08.md

**Success Criteria**:
```bash
$ ./venus-test-app --phase 8
Phase 8: Memory Transfer
✅ Allocated 1MB buffer
✅ Mapped memory (client-side shadow)
✅ Wrote test data (0x12345678 pattern)
✅ Transferred 1MB to server (10ms)
✅ GPU copied buffer to buffer
✅ Read back data from server
✅ Data validation: PASSED
✅ PASSED
```

**Time Estimate**: 11 days

**🎉 MILESTONE**: Complete data transfer pipeline!

---

### Phase 09: Simple Compute (Days 61-75)

**Goal**: Run compute shader that processes data

**Scope**:
- Shader module creation
- Descriptor sets and layouts
- Pipeline creation (compute)
- Dispatch compute work
- Read results back
- Example: Add two arrays

**Deliverables**:
- [ ] Client implements shader module commands
- [ ] Client implements descriptor set commands
- [ ] Client implements compute pipeline commands
- [ ] Client implements `vkCmdDispatch`
- [ ] Server executes compute shader
- [ ] Test app: run "add arrays" compute shader
- [ ] Verify results are correct
- [ ] Documentation: PHASE_09.md

**Success Criteria**:
```bash
$ ./venus-test-app --phase 9
Phase 9: Compute Shader
✅ Loaded compute shader (simple_add.spv)
✅ Created descriptor set layout
✅ Created compute pipeline
✅ Uploaded input arrays A[1024], B[1024]
✅ Dispatched compute (1024 elements)
✅ Downloaded result array C[1024]
✅ Verification: C[i] = A[i] + B[i] ✓
✅ PASSED
```

**Time Estimate**: 15 days

**🎉 MILESTONE**: Real GPU computation over network!

---

### Phase 10: Simple Graphics (Days 76-90)

**Goal**: Render a triangle

**Scope**:
- Render pass creation
- Framebuffer creation
- Graphics pipeline creation
- Vertex buffers
- Drawing commands
- Swapchain (offscreen initially)
- Image readback to display result

**Deliverables**:
- [ ] Client implements render pass commands
- [ ] Client implements framebuffer commands
- [ ] Client implements graphics pipeline commands
- [ ] Client implements vertex buffer binding
- [ ] Client implements draw commands
- [ ] Server renders to offscreen image
- [ ] Client reads back rendered image
- [ ] Test app: render colored triangle, save to file
- [ ] Documentation: PHASE_10.md

**Success Criteria**:
```bash
$ ./venus-test-app --phase 10
Phase 10: Graphics Rendering
✅ Created render pass
✅ Created graphics pipeline (vertex + fragment shaders)
✅ Created framebuffer (800x600)
✅ Uploaded vertex data (3 vertices)
✅ Recorded draw commands
✅ Submitted rendering
✅ Downloaded rendered image
✅ Saved to triangle.png
✅ PASSED

Open triangle.png to see your rendered triangle!
```

**Time Estimate**: 15 days

**🎉🎉🎉 MILESTONE**: Complete graphics rendering over network!

---

## Phase Dependencies

```
Phase 01 (Network)
    │
    ▼
Phase 02 (Instance) ─── Requires: Phase 01
    │
    ▼
Phase 03 (Device) ───── Requires: Phase 02
    │
    ▼
Phase 04 (Resources) ── Requires: Phase 03
    │
    ▼
Phase 05 (Commands) ─── Requires: Phase 04
    │
    ▼
Phase 06 (Submission) ─ Requires: Phase 05
    │
    ▼
Phase 07 (Real Exec) ── Requires: Phase 06
    │
    ▼
Phase 08 (Memory) ───── Requires: Phase 07
    │
    ├──────────────────┐
    ▼                  ▼
Phase 09 (Compute)   Phase 10 (Graphics)
Requires: Phase 08   Requires: Phase 08

Note: Phase 09 and 10 can be developed in parallel
```

## Progress Tracking

### Current Status

**Current Phase**: Phase 1 (Not Started)
**Overall Progress**: 0% (0/10 phases complete)

### Phase Completion Checklist

- [ ] **Phase 01**: Network Communication
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] Documentation complete
  - [ ] Code reviewed

- [ ] **Phase 02**: Fake Instance
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] Documentation complete
  - [ ] Code reviewed

- [ ] **Phase 03**: Fake Device
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] Documentation complete
  - [ ] Code reviewed

- [ ] **Phase 04**: Fake Resources
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] Documentation complete
  - [ ] Code reviewed

- [ ] **Phase 05**: Fake Commands
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] Documentation complete
  - [ ] Code reviewed

- [ ] **Phase 06**: Fake Submission
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] Documentation complete
  - [ ] Code reviewed

- [ ] **Phase 07**: Real Execution
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] Documentation complete
  - [ ] Code reviewed

- [ ] **Phase 08**: Memory Transfer
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] Documentation complete
  - [ ] Code reviewed

- [ ] **Phase 09**: Compute Shaders
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] Documentation complete
  - [ ] Code reviewed

- [ ] **Phase 10**: Graphics Rendering
  - [ ] Implementation complete
  - [ ] Tests passing
  - [ ] Documentation complete
  - [ ] Code reviewed

## Milestone Checklist

### Major Milestones

- [ ] **M1**: First network message sent and received (Phase 1)
- [ ] **M2**: First Vulkan object created remotely (Phase 2)
- [ ] **M3**: Complete fake Vulkan pipeline (Phase 6)
- [ ] **M4**: First real GPU execution (Phase 7)
- [ ] **M5**: Data transfer working (Phase 8)
- [ ] **M6**: First compute shader executed (Phase 9)
- [ ] **M7**: First triangle rendered (Phase 10)
- [ ] **M8**: Project complete!

### Quality Gates

Each phase must pass these before moving to next:

1. ✅ All tests pass
2. ✅ Code compiles without warnings
3. ✅ Documentation complete
4. ✅ No memory leaks (valgrind clean)
5. ✅ Passes through one complete test scenario

## Risk Mitigation

### Potential Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Venus protocol integration issues | High | Study Mesa code, Phase 1 validates early |
| Network performance bottleneck | Medium | Optimize in Phase 8+, acceptable initially |
| Handle mapping bugs | Medium | Extensive testing in each phase |
| Memory management complexity | High | Phase 8 dedicated to this |
| Synchronization edge cases | Medium | Incremental sync testing |

### Contingency Plans

- **If Phase takes too long**: Split into sub-phases, deliver partial functionality
- **If blocking bug**: Document, create minimal workaround, file issue for later
- **If scope creep**: Defer to future phases or post-1.0

## Beyond Phase 10

### Future Enhancements (Post-1.0)

- **Optimization**: Batching, compression, caching
- **Swapchain**: Real window rendering (not just offscreen)
- **Multi-client**: Server handles multiple clients
- **Authentication**: Secure client-server communication
- **Windows/macOS**: Cross-platform support
- **Advanced features**: Ray tracing, mesh shaders, etc.

---

**Next**: Start with [PHASE_01.md](PHASE_01.md) for detailed Phase 1 implementation guide.
