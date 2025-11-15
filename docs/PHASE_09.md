# Phase 09: Compute Shaders

**Run compute shaders on remote GPU**

## Overview

**Goal**: Implement compute pipeline creation and dispatch. Run first real GPU computation.

**Duration**: 15 days (Days 61-75)

**Prerequisites**: Phase 08 complete and passing

## Objectives

- ✅ Client can create shader modules
- ✅ Client can create descriptor set layouts and pools
- ✅ Client can allocate and update descriptor sets
- ✅ Client can create compute pipelines
- ✅ Client can dispatch compute work
- ✅ Test app runs "add two arrays" compute shader
- ✅ Results verified on client

**🎉 MILESTONE**: Real GPU computation over network!

## Commands to Implement

### Shader Module
- [ ] `vkCreateShaderModule`
- [ ] `vkDestroyShaderModule`

### Descriptor Set Layout
- [ ] `vkCreateDescriptorSetLayout`
- [ ] `vkDestroyDescriptorSetLayout`

### Descriptor Pool
- [ ] `vkCreateDescriptorPool`
- [ ] `vkDestroyDescriptorPool`
- [ ] `vkResetDescriptorPool`

### Descriptor Set
- [ ] `vkAllocateDescriptorSets`
- [ ] `vkFreeDescriptorSets`
- [ ] `vkUpdateDescriptorSets`

### Pipeline Layout
- [ ] `vkCreatePipelineLayout`
- [ ] `vkDestroyPipelineLayout`

### Compute Pipeline
- [ ] `vkCreateComputePipelines`
- [ ] `vkDestroyPipeline`

### Command Recording
- [ ] `vkCmdBindPipeline` (compute)
- [ ] `vkCmdBindDescriptorSets`
- [ ] `vkCmdDispatch`
- [ ] `vkCmdPipelineBarrier` (for memory barriers)

## Test Compute Shader

**File**: `test-app/phase09/shaders/simple_add.comp`

```glsl
#version 450

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) buffer InputA {
    float a[];
};

layout(set = 0, binding = 1) buffer InputB {
    float b[];
};

layout(set = 0, binding = 2) buffer Output {
    float c[];
};

void main() {
    uint idx = gl_GlobalInvocationID.x;
    c[idx] = a[idx] + b[idx];
}
```

**Compilation**:
```bash
glslangValidator -V simple_add.comp -o simple_add.spv
```

## Detailed Requirements

### vkCreateShaderModule

**Client**:
- [ ] Read SPIR-V file (e.g., simple_add.spv)
- [ ] Encode VkShaderModuleCreateInfo:
  - codeSize (in bytes)
  - pCode (SPIR-V binary)
- [ ] Send to server
- [ ] Receive shader module handle

**Server**:
- [ ] Decode shader module create info
- [ ] Validate SPIR-V magic number (0x07230203)
- [ ] Call `vkCreateShaderModule(real_device, &createInfo, NULL, &real_module)`
- [ ] Store handle mapping
- [ ] Return result

### vkCreateDescriptorSetLayout

**Client**:
- [ ] Encode VkDescriptorSetLayoutCreateInfo:
  - bindingCount
  - pBindings array (VkDescriptorSetLayoutBinding)
    - binding index
    - descriptorType (STORAGE_BUFFER, etc.)
    - descriptorCount
    - stageFlags (COMPUTE_BIT)
- [ ] Send to server

**Server**:
- [ ] Decode create info
- [ ] Call `vkCreateDescriptorSetLayout(...)`
- [ ] Store mapping
- [ ] Return result

### vkCreateDescriptorPool

**Client**:
- [ ] Encode VkDescriptorPoolCreateInfo:
  - maxSets
  - poolSizeCount
  - pPoolSizes array (type, descriptorCount)
- [ ] Send to server

**Server**:
- [ ] Call `vkCreateDescriptorPool(...)`
- [ ] Store mapping

### vkAllocateDescriptorSets

**Client**:
- [ ] Encode VkDescriptorSetAllocateInfo:
  - descriptorPool
  - descriptorSetCount
  - pSetLayouts array
- [ ] Send to server
- [ ] Receive descriptor set handles (array)

**Server**:
- [ ] Translate pool and layout handles
- [ ] Call `vkAllocateDescriptorSets(...)`
- [ ] Store mappings for all descriptor sets
- [ ] Return handles

### vkUpdateDescriptorSets

**Client**:
- [ ] Encode update operations:
  - VkWriteDescriptorSet array:
    - dstSet
    - dstBinding
    - descriptorType
    - descriptorCount
    - pBufferInfo (for buffer descriptors)
- [ ] Send to server

**Server**:
- [ ] Decode write descriptor sets
- [ ] Translate descriptor set and buffer handles
- [ ] Call `vkUpdateDescriptorSets(...)`

### vkCreatePipelineLayout

**Client**:
- [ ] Encode VkPipelineLayoutCreateInfo:
  - setLayoutCount
  - pSetLayouts array
  - pushConstantRangeCount
  - pPushConstantRanges (can be 0 for now)
- [ ] Send to server

**Server**:
- [ ] Translate descriptor set layout handles
- [ ] Call `vkCreatePipelineLayout(...)`
- [ ] Store mapping

### vkCreateComputePipelines

**Client**:
- [ ] Encode VkComputePipelineCreateInfo:
  - stage (VkPipelineShaderStageCreateInfo)
    - stage: COMPUTE_BIT
    - module: shader module handle
    - pName: entry point ("main")
  - layout: pipeline layout handle
- [ ] Send to server

**Server**:
- [ ] Translate shader module and layout handles
- [ ] Call `vkCreateComputePipelines(...)`
- [ ] Store pipeline mapping
- [ ] Return result

### vkCmdBindPipeline

**Client**:
- [ ] Encode command buffer, bind point (COMPUTE), pipeline handles
- [ ] Send to server

**Server**:
- [ ] Translate handles
- [ ] Call `vkCmdBindPipeline(real_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, real_pipeline)`

### vkCmdBindDescriptorSets

**Client**:
- [ ] Encode:
  - command buffer
  - pipeline bind point (COMPUTE)
  - pipeline layout
  - first set index
  - descriptor set count
  - pDescriptorSets array
  - dynamic offset count
  - pDynamicOffsets
- [ ] Send to server

**Server**:
- [ ] Translate all handles
- [ ] Call `vkCmdBindDescriptorSets(...)`

### vkCmdDispatch

**Client**:
- [ ] Encode command buffer, groupCountX, groupCountY, groupCountZ
- [ ] Send to server

**Server**:
- [ ] Call `vkCmdDispatch(real_cmd_buffer, groupCountX, groupCountY, groupCountZ)`

## Test Application

**File**: `test-app/phase09/phase09_test.cpp`

**Test Steps**:
1. [ ] Setup: instance, device, queue (from earlier phases)
2. [ ] Load SPIR-V shader: simple_add.spv
3. [ ] Create shader module
4. [ ] Create 3 buffers: inputA, inputB, output (storage buffers)
5. [ ] Allocate and bind memory (host visible)
6. [ ] Create descriptor set layout (3 storage buffer bindings)
7. [ ] Create descriptor pool
8. [ ] Allocate descriptor set
9. [ ] Update descriptor sets (bind 3 buffers)
10. [ ] Create pipeline layout
11. [ ] Create compute pipeline
12. [ ] Map inputA memory, write test data (e.g., [1.0, 2.0, 3.0, ...])
13. [ ] Map inputB memory, write test data (e.g., [10.0, 20.0, 30.0, ...])
14. [ ] Unmap memories (transfer to server)
15. [ ] Record command buffer:
    - vkCmdBindPipeline
    - vkCmdBindDescriptorSets
    - vkCmdDispatch(numElements / 256, 1, 1)
16. [ ] Submit and wait for fence
17. [ ] Map output buffer memory
18. [ ] Read back results
19. [ ] Verify: output[i] = inputA[i] + inputB[i]
20. [ ] Cleanup

**Expected Output**:
```
Phase 9: Compute Shader
=======================
✅ Loaded shader: simple_add.spv (size: 1024 bytes)
✅ Created shader module
✅ Created 3 storage buffers (4KB each)
✅ Created descriptor set layout
✅ Created descriptor pool
✅ Allocated descriptor set
✅ Updated descriptor sets
✅ Created pipeline layout
✅ Created compute pipeline
✅ Uploaded input data:
   InputA: [1.0, 2.0, 3.0, ..., 1024.0]
   InputB: [10.0, 20.0, 30.0, ..., 10240.0]
✅ Recorded compute dispatch (4 workgroups)
✅ Submitted to GPU
✅ GPU execution complete (took 2ms)
✅ Downloaded output data
✅ Verification:
   output[0] = 11.0 (expected 11.0) ✓
   output[1] = 22.0 (expected 22.0) ✓
   output[2] = 33.0 (expected 33.0) ✓
   ...
   output[1023] = 11264.0 (expected 11264.0) ✓
✅ All 1024 results correct!
✅ Phase 9 PASSED

🎉 MILESTONE: First compute shader executed on remote GPU!
```

## Implementation Checklist

### Days 1-3: Shader Module & SPIR-V
- [ ] Implement vkCreateShaderModule
- [ ] Add SPIR-V loading utility
- [ ] Compile test shader
- [ ] Test shader module creation

### Days 4-6: Descriptor Sets
- [ ] Implement descriptor set layout
- [ ] Implement descriptor pool
- [ ] Implement descriptor set allocation
- [ ] Implement descriptor set updates
- [ ] Test descriptor management

### Days 7-9: Pipeline Creation
- [ ] Implement pipeline layout
- [ ] Implement compute pipeline creation
- [ ] Test pipeline creation

### Days 10-12: Command Recording
- [ ] Implement vkCmdBindPipeline
- [ ] Implement vkCmdBindDescriptorSets
- [ ] Implement vkCmdDispatch
- [ ] Test command recording

### Days 13-15: End-to-End Testing
- [ ] Write phase 9 test app
- [ ] Test full compute shader workflow
- [ ] Verify results
- [ ] Regression tests
- [ ] Fix bugs
- [ ] Performance measurements

## Success Criteria

- [ ] Shader module creation works
- [ ] Descriptor sets work
- [ ] Compute pipeline creation succeeds
- [ ] Dispatch executes on GPU
- [ ] Results are correct
- [ ] Phase 9 test passes
- [ ] Regression tests pass
- [ ] No validation errors

## Deliverables

- [ ] Shader module commands
- [ ] Descriptor set layout/pool commands
- [ ] Descriptor set allocation/update commands
- [ ] Pipeline layout/compute pipeline commands
- [ ] Compute binding/dispatch commands
- [ ] SPIR-V loader utility
- [ ] Test compute shader (GLSL + compiled SPIR-V)
- [ ] Phase 9 test application

## Common Issues

**Issue**: Shader module creation fails
**Solution**: Verify SPIR-V is valid (use spirv-val)

**Issue**: Descriptor set update fails
**Solution**: Check descriptor types match, bindings are valid

**Issue**: Pipeline creation fails
**Solution**: Check validation errors, verify shader entry point is "main"

**Issue**: Dispatch doesn't execute
**Solution**: Verify workgroup counts are > 0, pipeline is bound

**Issue**: Wrong results
**Solution**: Check buffer bindings, verify shader logic, check dispatch size

## Next Steps

Proceed to **[PHASE_10.md](PHASE_10.md)** for graphics rendering!
