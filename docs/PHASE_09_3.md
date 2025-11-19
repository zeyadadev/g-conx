# Phase 9_3: Complete Compute Path

**Complete all compute-related Vulkan commands for full compute application support**

## Overview

**Goal**: Implement all remaining compute-related commands to enable arbitrary compute applications (llama.cpp, etc.)

**Prerequisites**: Phase 9 (basic compute) and Phase 10 complete

## Current State Analysis

### Already Implemented (Phase 9)
- ✅ `vkCreateShaderModule` / `vkDestroyShaderModule`
- ✅ `vkCreateDescriptorSetLayout` / `vkDestroyDescriptorSetLayout`
- ✅ `vkCreateDescriptorPool` / `vkDestroyDescriptorPool`
- ✅ `vkResetDescriptorPool`
- ✅ `vkAllocateDescriptorSets` / `vkFreeDescriptorSets`
- ✅ `vkUpdateDescriptorSets`
- ✅ `vkCreatePipelineLayout` / `vkDestroyPipelineLayout`
- ✅ `vkCreateComputePipelines` / `vkDestroyPipeline`
- ✅ `vkCmdBindPipeline` (compute)
- ✅ `vkCmdBindDescriptorSets`
- ✅ `vkCmdDispatch`
- ✅ `vkCmdPipelineBarrier`
- ✅ `vkCreateSampler` / `vkDestroySampler`
- ✅ `vkCreateBufferView` / `vkDestroyBufferView`

### Missing Commands (This Phase)

## Objectives

- [ ] Push constants fully working
- [ ] Indirect dispatch commands
- [ ] Pipeline cache for performance
- [ ] Query pools for timestamps/statistics
- [ ] Events for fine-grained sync
- [ ] Specialization constants verified
- [ ] All descriptor types working
- [ ] Test with complex compute workload

---

## Part 1: Push Constants (Critical)

Push constants are used by virtually all real compute applications for passing small, frequently-changing data.

### Commands to Implement

#### `vkCmdPushConstants`
**Client**:
```cpp
void vkCmdPushConstants(
    VkCommandBuffer commandBuffer,
    VkPipelineLayout layout,
    VkShaderStageFlags stageFlags,
    uint32_t offset,
    uint32_t size,
    const void* pValues);
```
- Encode command buffer, layout, stage flags, offset, size
- Encode push constant data (size bytes)
- Send to server

**Server**:
- Decode all parameters
- Translate command buffer and layout handles
- Call real `vkCmdPushConstants`

### Test Case
```glsl
#version 450
layout(push_constant) uniform PushConstants {
    uint offset;
    float scale;
} pc;

layout(set = 0, binding = 0) buffer Data { float values[]; };

void main() {
    uint idx = gl_GlobalInvocationID.x;
    values[idx] = values[idx + pc.offset] * pc.scale;
}
```

---

## Part 2: Indirect Dispatch

Allows GPU-driven workload sizes - essential for advanced compute patterns.

### Commands to Implement

#### `vkCmdDispatchIndirect`
**Client**:
```cpp
void vkCmdDispatchIndirect(
    VkCommandBuffer commandBuffer,
    VkBuffer buffer,
    VkDeviceSize offset);
```
- Encode command buffer, buffer handle, offset
- Send to server

**Server**:
- Translate handles
- Call real `vkCmdDispatchIndirect`

#### `vkCmdDispatchBase` (Vulkan 1.1)
**Client**:
```cpp
void vkCmdDispatchBase(
    VkCommandBuffer commandBuffer,
    uint32_t baseGroupX,
    uint32_t baseGroupY,
    uint32_t baseGroupZ,
    uint32_t groupCountX,
    uint32_t groupCountY,
    uint32_t groupCountZ);
```

### Test Case
```cpp
// Write dispatch parameters to buffer
VkDispatchIndirectCommand cmd = {4, 1, 1};
memcpy(mapped_ptr, &cmd, sizeof(cmd));
// ...
vkCmdDispatchIndirect(cmdBuffer, indirectBuffer, 0);
```

---

## Part 3: Pipeline Cache

Improves pipeline creation performance - important for apps creating many pipelines.

### Commands to Implement

#### `vkCreatePipelineCache`
**Client**:
```cpp
VkResult vkCreatePipelineCache(
    VkDevice device,
    const VkPipelineCacheCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkPipelineCache* pPipelineCache);
```
- Allocate client handle
- Encode create info (initialDataSize, pInitialData)
- Send to server
- Return result

**Server**:
- Decode create info
- Call real `vkCreatePipelineCache`
- Store handle mapping

#### `vkDestroyPipelineCache`
- Standard destroy pattern

#### `vkGetPipelineCacheData`
**Client**:
```cpp
VkResult vkGetPipelineCacheData(
    VkDevice device,
    VkPipelineCache pipelineCache,
    size_t* pDataSize,
    void* pData);
```
- Query size first (pData = nullptr)
- Then retrieve data
- Server sends cache data back to client

**Server**:
- Call real `vkGetPipelineCacheData`
- Send data back to client

#### `vkMergePipelineCaches`
```cpp
VkResult vkMergePipelineCaches(
    VkDevice device,
    VkPipelineCache dstCache,
    uint32_t srcCacheCount,
    const VkPipelineCache* pSrcCaches);
```

---

## Part 4: Query Pools

Required for performance profiling and GPU-driven algorithms.

### Commands to Implement

#### `vkCreateQueryPool`
**Client**:
```cpp
VkResult vkCreateQueryPool(
    VkDevice device,
    const VkQueryPoolCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkQueryPool* pQueryPool);
```
- Encode: queryType, queryCount, pipelineStatistics flags

**Server**:
- Call real `vkCreateQueryPool`
- Store mapping

#### `vkDestroyQueryPool`
- Standard destroy pattern

#### `vkCmdResetQueryPool`
```cpp
void vkCmdResetQueryPool(
    VkCommandBuffer commandBuffer,
    VkQueryPool queryPool,
    uint32_t firstQuery,
    uint32_t queryCount);
```

#### `vkCmdBeginQuery` / `vkCmdEndQuery`
```cpp
void vkCmdBeginQuery(
    VkCommandBuffer commandBuffer,
    VkQueryPool queryPool,
    uint32_t query,
    VkQueryControlFlags flags);

void vkCmdEndQuery(
    VkCommandBuffer commandBuffer,
    VkQueryPool queryPool,
    uint32_t query);
```

#### `vkCmdWriteTimestamp`
```cpp
void vkCmdWriteTimestamp(
    VkCommandBuffer commandBuffer,
    VkPipelineStageFlagBits pipelineStage,
    VkQueryPool queryPool,
    uint32_t query);
```

#### `vkGetQueryPoolResults`
**Client**:
```cpp
VkResult vkGetQueryPoolResults(
    VkDevice device,
    VkQueryPool queryPool,
    uint32_t firstQuery,
    uint32_t queryCount,
    size_t dataSize,
    void* pData,
    VkDeviceSize stride,
    VkQueryResultFlags flags);
```
- Send query request to server
- Server retrieves results and sends back

#### `vkCmdCopyQueryPoolResults`
```cpp
void vkCmdCopyQueryPoolResults(
    VkCommandBuffer commandBuffer,
    VkQueryPool queryPool,
    uint32_t firstQuery,
    uint32_t queryCount,
    VkBuffer dstBuffer,
    VkDeviceSize dstOffset,
    VkDeviceSize stride,
    VkQueryResultFlags flags);
```

#### `vkResetQueryPool` (Vulkan 1.2 / VK_EXT_host_query_reset)
```cpp
void vkResetQueryPool(
    VkDevice device,
    VkQueryPool queryPool,
    uint32_t firstQuery,
    uint32_t queryCount);
```

---

## Part 5: Events

Fine-grained synchronization within and across command buffers.

### Commands to Implement

#### `vkCreateEvent` / `vkDestroyEvent`
```cpp
VkResult vkCreateEvent(
    VkDevice device,
    const VkEventCreateInfo* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkEvent* pEvent);
```

#### `vkGetEventStatus`
```cpp
VkResult vkGetEventStatus(VkDevice device, VkEvent event);
```
- Returns VK_EVENT_SET or VK_EVENT_RESET

#### `vkSetEvent` / `vkResetEvent`
```cpp
VkResult vkSetEvent(VkDevice device, VkEvent event);
VkResult vkResetEvent(VkDevice device, VkEvent event);
```

#### `vkCmdSetEvent` / `vkCmdResetEvent`
```cpp
void vkCmdSetEvent(
    VkCommandBuffer commandBuffer,
    VkEvent event,
    VkPipelineStageFlags stageMask);

void vkCmdResetEvent(
    VkCommandBuffer commandBuffer,
    VkEvent event,
    VkPipelineStageFlags stageMask);
```

#### `vkCmdWaitEvents`
```cpp
void vkCmdWaitEvents(
    VkCommandBuffer commandBuffer,
    uint32_t eventCount,
    const VkEvent* pEvents,
    VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask,
    uint32_t memoryBarrierCount,
    const VkMemoryBarrier* pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier* pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount,
    const VkImageMemoryBarrier* pImageMemoryBarriers);
```

---

## Part 6: Specialization Constants

Verify and test specialization constant support in pipeline creation.

### Verification Needed

The `VkComputePipelineCreateInfo.stage.pSpecializationInfo` field should already be encoded. Verify:

1. `VkSpecializationInfo` is properly encoded:
   - mapEntryCount
   - pMapEntries (array of VkSpecializationMapEntry)
   - dataSize
   - pData

2. Server properly decodes and passes to `vkCreateComputePipelines`

### Test Case
```glsl
#version 450
layout(local_size_x_id = 0) in;  // Specialization constant
layout(constant_id = 1) const uint BATCH_SIZE = 64;

void main() {
    // Use gl_WorkGroupSize.x and BATCH_SIZE
}
```

```cpp
VkSpecializationMapEntry entries[] = {
    {0, 0, sizeof(uint32_t)},  // local_size_x
    {1, 4, sizeof(uint32_t)},  // BATCH_SIZE
};
uint32_t data[] = {256, 128};

VkSpecializationInfo spec_info = {
    .mapEntryCount = 2,
    .pMapEntries = entries,
    .dataSize = sizeof(data),
    .pData = data
};
```

---

## Part 7: Descriptor Type Coverage

Verify all descriptor types work correctly for compute.

### Types to Test

| Descriptor Type | Use Case |
|----------------|----------|
| `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` | ✅ Already tested |
| `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` | Constants, matrices |
| `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` | Image load/store |
| `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` | Texture sampling |
| `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` | Common pattern |
| `VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER` | Buffer as image |
| `VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER` | Buffer as image |

### Verify `vkUpdateDescriptorSets` Handles

- `VkDescriptorBufferInfo` for buffer types
- `VkDescriptorImageInfo` for image/sampler types
- `VkBufferView` for texel buffer types

---

## Implementation Checklist

### Week 1: Push Constants & Indirect Dispatch

- [ ] Implement `vkCmdPushConstants`
  - [ ] Client encoding
  - [ ] Server decoding
  - [ ] Wire up in dispatcher
- [ ] Implement `vkCmdDispatchIndirect`
- [ ] Implement `vkCmdDispatchBase`
- [ ] Add push constant test
- [ ] Add indirect dispatch test

### Week 2: Pipeline Cache & Query Pools

- [ ] Implement pipeline cache commands
  - [ ] `vkCreatePipelineCache`
  - [ ] `vkDestroyPipelineCache`
  - [ ] `vkGetPipelineCacheData`
  - [ ] `vkMergePipelineCaches`
- [ ] Implement query pool commands
  - [ ] `vkCreateQueryPool`
  - [ ] `vkDestroyQueryPool`
  - [ ] `vkCmdResetQueryPool`
  - [ ] `vkCmdBeginQuery` / `vkCmdEndQuery`
  - [ ] `vkCmdWriteTimestamp`
  - [ ] `vkGetQueryPoolResults`
  - [ ] `vkCmdCopyQueryPoolResults`
  - [ ] `vkResetQueryPool`
- [ ] Add pipeline cache test
- [ ] Add timestamp query test

### Week 3: Events & Specialization Constants

- [ ] Implement event commands
  - [ ] `vkCreateEvent` / `vkDestroyEvent`
  - [ ] `vkGetEventStatus`
  - [ ] `vkSetEvent` / `vkResetEvent`
  - [ ] `vkCmdSetEvent` / `vkCmdResetEvent`
  - [ ] `vkCmdWaitEvents`
- [ ] Verify specialization constants
- [ ] Add event synchronization test
- [ ] Add specialization constant test

### Week 4: Descriptor Types & Integration

- [ ] Test uniform buffer descriptors
- [ ] Test storage image descriptors
- [ ] Test combined image sampler
- [ ] Test texel buffer descriptors
- [ ] Create comprehensive Phase 9_3 test
- [ ] Run regression tests
- [ ] Performance benchmarks

---

## Test Application

**File**: `test-app/phase09_3/phase09_3_test.cpp`

### Test 1: Push Constants
```cpp
// Shader uses push constants for offset and scale
// Verify data is correctly passed through pipeline
```

### Test 2: Indirect Dispatch
```cpp
// Fill buffer with dispatch parameters
// Execute indirect dispatch
// Verify correct number of invocations
```

### Test 3: Timestamp Queries
```cpp
// Create timestamp query pool
// Record timestamps before/after compute
// Read back and calculate elapsed time
```

### Test 4: Specialization Constants
```cpp
// Create pipeline with custom local_size
// Verify correct workgroup size in shader
```

### Test 5: Event Synchronization
```cpp
// Set event after first dispatch
// Wait for event before second dispatch
// Verify ordering
```

### Test 6: Uniform Buffer + Storage Image
```cpp
// Use uniform buffer for transform matrix
// Use storage image for output
// Verify combined descriptor usage
```

---

## Success Criteria

- [ ] All push constant tests pass
- [ ] Indirect dispatch works correctly
- [ ] Pipeline cache can be created and used
- [ ] Query pool results are accurate
- [ ] Events synchronize correctly
- [ ] Specialization constants work
- [ ] All descriptor types function
- [ ] Phase 9_3 comprehensive test passes
- [ ] Regression tests pass
- [ ] No validation errors

---

## Command Summary

### New Commands (27 total)

**Push Constants (1)**
- `vkCmdPushConstants`

**Indirect Dispatch (2)**
- `vkCmdDispatchIndirect`
- `vkCmdDispatchBase`

**Pipeline Cache (4)**
- `vkCreatePipelineCache`
- `vkDestroyPipelineCache`
- `vkGetPipelineCacheData`
- `vkMergePipelineCaches`

**Query Pool (8)**
- `vkCreateQueryPool`
- `vkDestroyQueryPool`
- `vkCmdResetQueryPool`
- `vkCmdBeginQuery`
- `vkCmdEndQuery`
- `vkCmdWriteTimestamp`
- `vkGetQueryPoolResults`
- `vkCmdCopyQueryPoolResults`
- `vkResetQueryPool`

**Events (7)**
- `vkCreateEvent`
- `vkDestroyEvent`
- `vkGetEventStatus`
- `vkSetEvent`
- `vkResetEvent`
- `vkCmdSetEvent`
- `vkCmdResetEvent`
- `vkCmdWaitEvents`

---

## Common Issues

**Issue**: Push constants not updating
**Solution**: Verify offset/size alignment, check stage flags match shader

**Issue**: Indirect dispatch reads wrong values
**Solution**: Check buffer offset, verify VkDispatchIndirectCommand layout

**Issue**: Query results are zero
**Solution**: Ensure query pool is reset before use, check result flags

**Issue**: Specialization constants ignored
**Solution**: Verify constant_id matches mapEntry constantID

**Issue**: Event deadlock
**Solution**: Check stage masks, ensure event is set before wait

---

## Next Steps

After Phase 9_3:
- Test with llama.cpp Vulkan backend
- Profile performance with real workloads
- Consider additional optimizations (command batching, async operations)

---

## Deliverables

- [ ] Push constants command
- [ ] Indirect dispatch commands
- [ ] Pipeline cache commands
- [ ] Query pool commands
- [ ] Event commands
- [ ] Specialization constant support verification
- [ ] Descriptor type coverage tests
- [ ] Phase 9_3 test application
- [ ] Updated documentation
