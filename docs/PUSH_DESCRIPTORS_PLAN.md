# Push Descriptor Support Implementation Plan

**Status:** Planning Phase
**Priority:** Medium - Required for some compute applications (ncnn, vkpeak)
**Estimated Effort:** 3-4 days
**Complexity:** Medium-High

## Overview

Push descriptors (VK_KHR_push_descriptor) are a Vulkan extension that allows applications to "push" descriptor data directly into a command buffer without allocating descriptor sets. This is a performance optimization that reduces descriptor set allocation overhead and memory usage.

## Problem Statement

Applications like vkpeak and ncnn query for push descriptor functions:
- `vkCmdPushDescriptorSetKHR`
- `vkCmdPushDescriptorSetWithTemplateKHR`
- And newer variants (vk1.3+): `vkCmdPushDescriptorSet2`, etc.

When these functions return NULL, the application may crash if it tries to call them without proper null checks.

## Background: What Are Push Descriptors?

### Traditional Descriptor Workflow
```
1. Create VkDescriptorSetLayout
2. Create VkDescriptorPool
3. Allocate VkDescriptorSet from pool
4. Update descriptor set with vkUpdateDescriptorSets
5. Bind descriptor set: vkCmdBindDescriptorSets(commandBuffer, set)
```

### Push Descriptor Workflow
```
1. Create VkDescriptorSetLayout (with PUSH_DESCRIPTOR flag)
2. Push descriptors directly: vkCmdPushDescriptorSetKHR(commandBuffer, descriptors)
   (No allocation, no pool, no separate update step)
```

### Benefits
- **No descriptor set allocation** - reduces memory usage
- **No descriptor pool management** - simpler code
- **Lower latency** - no indirection through descriptor sets
- **Better for dynamic content** - descriptors change frequently

### Limitations
- Limited to first descriptor set (set=0 typically)
- Cannot be used with all pipeline layouts
- Driver-dependent maximum descriptor count

## Technical Requirements

### Extension Properties
```c
VkPhysicalDeviceProperties2 props;
VkPhysicalDevicePushDescriptorPropertiesKHR pushDescriptorProps;
props.pNext = &pushDescriptorProps;

// pushDescriptorProps.maxPushDescriptors - maximum descriptors that can be pushed
// Typical values: 32-256 descriptors
```

### Pipeline Layout Requirements
```c
VkDescriptorSetLayoutCreateInfo layoutInfo = {};
layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
// Only set 0 can use push descriptors typically
```

## Implementation Plan

### Phase 1: Core Push Descriptor Support (Days 1-2)

#### 1.1 Client-Side Implementation

**File:** `client/icd/commands/descriptor_commands.cpp`

Use the protocol’s existing unsuffixed entrypoints (`vkCmdPushDescriptorSet`) and alias them for the KHR names in `vkGetDeviceProcAddr` rather than inventing KHR-specific wire calls. The Venus protocol already exposes the dispatch hooks (`vn_async_vkCmdPushDescriptorSet`, `dispatch_vkCmdPushDescriptorSet`).

**Key Tasks:**
1. Validate command buffer is recording
2. Translate handles in `pDescriptorWrites`:
   - Buffer handles → remote buffer handles
   - Image view handles → remote image view handles
   - Buffer view handles → remote buffer view handles
   - Sampler handles → remote sampler handles
3. Translate pipeline layout handle to remote
4. Encode command with Venus protocol
5. Send to server via `vn_async_vkCmdPushDescriptorSet`
6. Gate support on real-device capability: only advertise and surface the functions when the server reports `VK_KHR_push_descriptor` (or core version >= 1.1 that includes it) and use the server-reported `VkPhysicalDevicePushDescriptorPropertiesKHR::maxPushDescriptors`.

**Handle Translation Pattern:**
```cpp
// Need to deep-copy VkWriteDescriptorSet array and translate handles
std::vector<VkWriteDescriptorSet> remote_writes(descriptorWriteCount);
std::vector<VkDescriptorBufferInfo> remote_buffer_infos;
std::vector<VkDescriptorImageInfo> remote_image_infos;
std::vector<VkBufferView> remote_buffer_views;

for (uint32_t i = 0; i < descriptorWriteCount; ++i) {
    remote_writes[i] = pDescriptorWrites[i];

    switch (pDescriptorWrites[i].descriptorType) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            // Translate buffer handles in pBufferInfo
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            // Translate image view handles in pImageInfo
            break;
        // ... etc
    }
}
```

**Register in vkGetDeviceProcAddr:**
```cpp
// Core name and KHR alias should both resolve
if (strcmp(pName, "vkCmdPushDescriptorSet") == 0 ||
    strcmp(pName, "vkCmdPushDescriptorSetKHR") == 0) {
    return (PFN_vkVoidFunction)vkCmdPushDescriptorSet;
}
```

#### 1.2 Server-Side Implementation

**File:** `server/renderer_decoder.c`

Implement dispatch handler with the existing protocol struct (`vn_command_vkCmdPushDescriptorSet`) and wire it to `dispatch_vkCmdPushDescriptorSet`. The handler mirrors `vkUpdateDescriptorSets` translation and must reject recording when the command buffer is not in the RECORDING state. Validate layout translation and set index (set 0 only) before calling the real Vulkan `vkCmdPushDescriptorSet`.

**Register dispatch:** hook `renderer->ctx.dispatch_vkCmdPushDescriptorSet`.

### Phase 2: Push Descriptors with Templates (Day 3)

#### 2.1 Client Implementation

**File:** `client/icd/commands/descriptor_commands.cpp`

Implement `vkCmdPushDescriptorSetWithTemplate` (core) and alias the KHR name. This relies on template metadata persisted at `vkCreateDescriptorUpdateTemplate` time; finish that storage first and reuse the parsing path for both `vkUpdateDescriptorSetWithTemplate` and the push-descriptor variant to avoid duplicate logic.

**Complexity:** This is the tricky one because `pData` is raw memory that must be interpreted according to the template.

**Challenge:** The template defines the memory layout of `pData`:
```c
struct MyDescriptorData {
    VkBuffer buffer;              // offset 0
    VkDescriptorBufferInfo info;  // offset 8
    VkImageView imageView;        // offset 32
    // ... layout defined by template
};
```

**Solution:**
1. Retrieve template metadata (stored during vkCreateDescriptorUpdateTemplate)
2. Parse `pData` according to template entries
3. Translate handles in the parsed data
4. Send to server

**Required State Tracking:**
- Store `VkDescriptorUpdateTemplateCreateInfo` during template creation
- Specifically the `pDescriptorUpdateEntries` array
- Use this to interpret `pData`

#### 2.2 Server Implementation

Similar to client - must interpret `pData` and translate handles.

### Phase 3: Capability Wiring & Validation (Day 4)

#### 3.1 Capability Exposure
Leverage the server-reported capabilities instead of hardcoding. `vkEnumerateDeviceExtensionProperties` already forwards the server list; only surface push-descriptor entrypoints when the extension (or core 1.1+) is present. Likewise, plumb through `vkGetPhysicalDeviceProperties2` to read `VkPhysicalDevicePushDescriptorPropertiesKHR` from the server—no guessed limits.

#### 3.2 Descriptor Set Layout Validation

**File:** `client/icd/commands/descriptor_commands.cpp`

In `vkCreateDescriptorSetLayout`:
- Check for `VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR` flag
- Store this flag in layout metadata
- Validate constraints (set must be 0, descriptor count limits based on `maxPushDescriptors`)

### Phase 4: Newer Variants (Optional)

The Vulkan 1.3+ variants are similar but use different structures:
- `vkCmdPushDescriptorSet2KHR` - uses `VkPushDescriptorSetInfoKHR`
- `vkCmdPushDescriptorSetWithTemplate2KHR` - uses `VkPushDescriptorSetWithTemplateInfoKHR`

These can be implemented later if needed.

## State Tracking Requirements

### Client State Extensions

**File:** `client/state/pipeline_state.h/cpp`

Add to `DescriptorSetLayoutInfo`:
```cpp
struct DescriptorSetLayoutInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout remote_handle = VK_NULL_HANDLE;
    bool is_push_descriptor = false;  // NEW
};
```

Add to `DescriptorUpdateTemplateInfo`:
```cpp
struct DescriptorUpdateTemplateInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorUpdateTemplate remote_handle = VK_NULL_HANDLE;

    // NEW: Store template definition for pData parsing
    VkDescriptorUpdateTemplateType template_type;
    VkPipelineBindPoint bind_point;
    std::vector<VkDescriptorUpdateTemplateEntry> entries;
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout pipeline_layout;
};
```

### Server State Extensions

**File:** `server/state/resource_tracker.h/cpp`

Similar additions for server-side tracking.

## Venus Protocol Considerations

### Encoding pData for Templates

The `pData` pointer in `vkCmdPushDescriptorSetWithTemplateKHR` requires special handling:

```cpp
// Venus protocol must encode the raw memory blob
vn_encode_blob(encoder, pData, data_size);

// Where data_size is computed from template entries
size_t compute_template_data_size(const VkDescriptorUpdateTemplateCreateInfo* info) {
    size_t max_offset = 0;
    for (uint32_t i = 0; i < info->descriptorUpdateEntryCount; ++i) {
        const auto& entry = info->pDescriptorUpdateEntries[i];
        size_t entry_end = entry.offset +
                          entry.stride * entry.descriptorCount;
        max_offset = std::max(max_offset, entry_end);
    }
    return max_offset;
}
```

### Handle Translation in pData

The tricky part: `pData` contains Vulkan handles embedded in the raw memory. These must be translated:

```cpp
void translate_template_data(
    const VkDescriptorUpdateTemplateCreateInfo* template_info,
    const void* src_data,
    void* dst_data) {

    memcpy(dst_data, src_data, data_size);

    for (const auto& entry : template_info->pDescriptorUpdateEntries) {
        for (uint32_t i = 0; i < entry.descriptorCount; ++i) {
            size_t offset = entry.offset + i * entry.stride;

            switch (entry.descriptorType) {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
                    VkDescriptorBufferInfo* info =
                        (VkDescriptorBufferInfo*)((char*)dst_data + offset);
                    info->buffer = translate_buffer_handle(info->buffer);
                    break;
                }
                // ... handle other descriptor types
            }
        }
    }
}
```

## Testing Strategy

### Unit Tests

**File:** `client/tests/test_push_descriptors.cpp`

Test cases:
1. Basic push descriptor set (uniform buffer)
2. Push descriptor set (storage buffer + image)
3. Push descriptor set with template
4. Multiple push descriptor calls in one command buffer
5. Push descriptors with dynamic offsets (if supported)
6. Error cases: invalid set, too many descriptors

### Integration Tests

**File:** `test-app/integration/test_push_descriptors.cpp`

1. Create pipeline with push descriptor layout
2. Record command buffer with push descriptors
3. Execute and verify results (compute shader)

### Real-World Application Tests

1. **vkpeak** - Compute performance benchmark
2. **ncnn** - Neural network inference
3. **llama.cpp** - LLM inference (if it uses push descriptors)

## Implementation Checklist

### Client Side
- [ ] Implement `vkCmdPushDescriptorSetKHR`
- [ ] Implement `vkCmdPushDescriptorSetWithTemplateKHR`
- [ ] Update `vkCreateDescriptorUpdateTemplate` to store template definition
- [ ] Add push descriptor flag to descriptor set layout tracking
- [ ] Register functions in `vkGetDeviceProcAddr`
- [ ] Report extension in `vkEnumerateDeviceExtensionProperties`
- [ ] Report properties in `vkGetPhysicalDeviceProperties2`
- [ ] Add unit tests

### Server Side
- [ ] Implement `server_dispatch_vkCmdPushDescriptorSetKHR`
- [ ] Implement `server_dispatch_vkCmdPushDescriptorSetWithTemplateKHR`
- [ ] Add bridge functions in `server_state.cpp`
- [ ] Update ResourceTracker for template metadata
- [ ] Register dispatch handlers
- [ ] Add unit tests

### Testing
- [ ] Unit tests pass
- [ ] Integration tests pass
- [ ] vkpeak runs without crashes
- [ ] ncnn computes correctly
- [ ] Performance validation (push vs traditional)

## Known Limitations

1. **Set 0 Only:** Most implementations only support push descriptors for set 0
2. **Descriptor Count:** Limited by `maxPushDescriptors` property (typically 32-256)
3. **Update After Bind:** Push descriptors don't support UPDATE_AFTER_BIND flag
4. **Complexity:** The template variant requires complex memory parsing

## Performance Considerations

### Client Side
- Pre-allocate translation buffers to avoid repeated allocations
- Cache template metadata lookup
- Optimize handle translation loops

### Server Side
- Minimize copies during handle translation
- Use stack allocation for small descriptor counts
- Profile hot paths with real applications

## Future Enhancements

1. **Vulkan 1.3 Variants:** Implement `vkCmdPushDescriptorSet2KHR` variants
2. **Push Constants Integration:** Often used together
3. **Descriptor Indexing:** Advanced descriptor features
4. **Validation:** Add more extensive validation of push descriptor usage

## References

- [VK_KHR_push_descriptor spec](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_push_descriptor.html)
- [Vulkan Guide - Push Descriptors](https://github.com/KhronosGroup/Vulkan-Guide/blob/master/chapters/extensions/VK_KHR_push_descriptor.adoc)
- [Mesa Venus driver source](https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/virtio/vulkan)
- [ncnn Vulkan backend](https://github.com/Tencent/ncnn/tree/master/src/layer/vulkan)

## Related Files

### Client
- `client/icd/commands/descriptor_commands.cpp` - Push descriptor implementations
- `client/state/pipeline_state.h/cpp` - Layout and template tracking
- `client/icd/icd_entrypoints.cpp` - Function registration

### Server
- `server/renderer_decoder.c` - Dispatch handlers
- `server/server_state.cpp` - Bridge functions
- `server/state/resource_tracker.h/cpp` - Metadata tracking

### Tests
- `client/tests/test_push_descriptors.cpp` - Unit tests
- `test-app/integration/test_push_descriptors.cpp` - Integration tests

## Estimated Timeline

| Phase | Duration | Deliverable |
|-------|----------|-------------|
| Phase 1: Core Push Descriptors | 2 days | `vkCmdPushDescriptorSetKHR` working |
| Phase 2: Template Support | 1 day | `vkCmdPushDescriptorSetWithTemplateKHR` working |
| Phase 3: Extension Support | 0.5 days | Extension properly reported |
| Phase 4: Testing & Polish | 0.5 days | All tests passing, vkpeak works |
| **Total** | **4 days** | Full push descriptor support |

## Success Criteria

✅ vkpeak runs without crashes
✅ ncnn computes correctly using push descriptors
✅ All unit and integration tests pass
✅ Performance within 5% of native Vulkan
✅ No memory leaks or handle leaks
✅ Code follows existing Venus Plus patterns

## Next Steps After This Plan

1. **Review this plan** with team/stakeholders
2. **Create implementation tasks** in issue tracker
3. **Set up test environment** with vkpeak and ncnn
4. **Begin Phase 1 implementation** - Core push descriptors
5. **Iterate and test** incrementally

---

**Document Version:** 1.0
**Last Updated:** 2025-12-04
**Author:** Claude (AI Assistant)
**Status:** Ready for Implementation
