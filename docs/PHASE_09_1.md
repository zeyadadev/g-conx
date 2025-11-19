# Phase 9.1: Compute Application Compatibility

**Complete enumeration and memory operations for real compute applications**

## Overview

**Goal**: Fill the gaps preventing real compute applications and tools like vulkaninfo from working correctly. This phase adds missing enumeration commands, memory mapping, and resource views.

**Duration**: 5-7 days

**Prerequisites**: Phase 09 complete and passing

## Objectives

- [ ] vulkaninfo shows full device extensions and capabilities
- [ ] Applications can query device properties/features using Properties2/Features2
- [ ] Memory mapping works for CPU↔GPU data transfer
- [ ] Image views, samplers, and buffer views available for compute shaders
- [ ] Real compute applications can run without missing command errors

**🎯 MILESTONE**: Full compute application compatibility!

## Kickoff Checklist

- [ ] Re-run Phase 09 (`ctest --test-dir build/client/tests --output-on-failure`) to ensure compute pipelines still pass before layering more features
- [ ] Capture real GPU capabilities via native `vulkaninfo --summary > artifacts/phase09_1_baseline.txt` for later comparisons
- [ ] Inspect `client/state/handle_tables.*` and `server/state/handle_tables.*` to understand how existing enumerations route handles
- [ ] Confirm memory-transfer helpers from Phase 08 (`client/state/shadow_buffer.*` and `server/memory/*`) are green; we build on those flows
- [ ] Document current missing commands in `docs/INDEX.md` so other agents know this phase owns them
- [ ] Coordinate port (5556) availability and verify `venus-server` launches before adding new protocol messages

## Commands to Implement

### Device Extension Enumeration (Critical for vulkaninfo)
- [ ] `vkEnumerateDeviceExtensionProperties`
- [ ] `vkEnumerateDeviceLayerProperties`

### Extended Physical Device Queries
- [ ] `vkGetPhysicalDeviceProperties2`
- [ ] `vkGetPhysicalDeviceFeatures2`
- [ ] `vkGetPhysicalDeviceQueueFamilyProperties2`
- [ ] `vkGetPhysicalDeviceMemoryProperties2`
- [ ] `vkGetPhysicalDeviceImageFormatProperties`
- [ ] `vkGetPhysicalDeviceImageFormatProperties2`

### Memory Mapping (Critical for data transfer)
- [ ] `vkMapMemory`
- [ ] `vkUnmapMemory`
- [ ] `vkFlushMappedMemoryRanges`
- [ ] `vkInvalidateMappedMemoryRanges`

### Image Views
- [ ] `vkCreateImageView`
- [ ] `vkDestroyImageView`

### Samplers
- [ ] `vkCreateSampler`
- [ ] `vkDestroySampler`

### Buffer Views (for texel buffers)
- [ ] `vkCreateBufferView`
- [ ] `vkDestroyBufferView`

### Additional Required Commands
- [ ] `vkGetDeviceProcAddr`
- [ ] `vkGetBufferMemoryRequirements2`
- [ ] `vkGetImageMemoryRequirements2`

## Priority Order

### Tier 1: Critical (Days 1-2)
These block basic functionality:

1. **`vkEnumerateDeviceExtensionProperties`** - Without this, vulkaninfo shows no extensions and apps fail capability checks
2. **`vkMapMemory` / `vkUnmapMemory`** - Without this, no CPU↔GPU data transfer works

### Tier 2: High Priority (Days 3-4)
Required by most applications:

3. **`vkGetPhysicalDeviceProperties2` / `vkGetPhysicalDeviceFeatures2`** - Many apps require VK_KHR_get_physical_device_properties2
4. **`vkCreateImageView` / `vkDestroyImageView`** - Required for texture sampling in compute
5. **`vkCreateSampler` / `vkDestroySampler`** - Required for texture sampling

### Tier 3: Completeness (Days 5-7)
For full compatibility:

6. Remaining Properties2/Memory2 queries
7. Buffer views
8. Flush/Invalidate memory ranges

## Detailed Requirements

### vkEnumerateDeviceExtensionProperties

**Client**:
- [ ] Encode physical device handle and pLayerName (usually NULL)
- [ ] Handle two-call pattern: first call gets count, second gets data
- [ ] Return extension properties array

**Server**:
- [ ] Translate physical device handle
- [ ] Call real `vkEnumerateDeviceExtensionProperties(real_device, pLayerName, pCount, pProperties)`
- [ ] Return count and/or properties array

**Why Critical**: Apps check for specific extensions before using features. vulkaninfo displays available extensions.

### vkGetPhysicalDeviceProperties2

**Client**:
- [ ] Encode physical device handle
- [ ] Encode VkPhysicalDeviceProperties2 with pNext chain
- [ ] Common pNext structures:
  - VkPhysicalDeviceVulkan11Properties
  - VkPhysicalDeviceVulkan12Properties
  - VkPhysicalDeviceVulkan13Properties
  - VkPhysicalDeviceDriverProperties

**Server**:
- [ ] Translate physical device handle
- [ ] Call `vkGetPhysicalDeviceProperties2(real_device, pProperties)`
- [ ] Return filled structure including pNext chain

### vkMapMemory

**Client**:
- [ ] Encode device, memory, offset, size, flags
- [ ] Receive mapped pointer (as offset/identifier)
- [ ] Create local buffer to mirror mapped region
- [ ] Return local pointer to application

**Server**:
- [ ] Translate device and memory handles
- [ ] Call `vkMapMemory(real_device, real_memory, offset, size, flags, &ppData)`
- [ ] Store mapping info for this memory object
- [ ] Return success (actual data transfer happens on unmap)

**Data Transfer Protocol**:
- On `vkUnmapMemory`: Client sends buffer data to server
- Server writes data to mapped GPU memory
- For read-back: Reverse on next map

### vkUnmapMemory

**Client**:
- [ ] Collect data from local mapped buffer
- [ ] Encode device, memory, and buffer data
- [ ] Send to server
- [ ] Free local buffer

**Server**:
- [ ] Receive buffer data
- [ ] Write to mapped memory region (memcpy to ppData)
- [ ] Call `vkUnmapMemory(real_device, real_memory)`

### vkCreateImageView

**Client**:
- [ ] Encode VkImageViewCreateInfo:
  - image handle
  - viewType (1D, 2D, 3D, CUBE, ARRAY variants)
  - format
  - components (swizzle)
  - subresourceRange

**Server**:
- [ ] Translate image handle
- [ ] Call `vkCreateImageView(...)`
- [ ] Store handle mapping

### vkCreateSampler

**Client**:
- [ ] Encode VkSamplerCreateInfo:
  - magFilter, minFilter
  - mipmapMode
  - addressModeU/V/W
  - mipLodBias, anisotropyEnable, maxAnisotropy
  - compareEnable, compareOp
  - minLod, maxLod
  - borderColor
  - unnormalizedCoordinates

**Server**:
- [ ] Call `vkCreateSampler(...)`
- [ ] Store handle mapping

### vkEnumerateDeviceLayerProperties

**Client**:
- [ ] Support two-call pattern identical to extension enumeration
- [ ] Marshal optional `pLayerName` strings and guard against buffer overruns when copying back layer names/descriptions

**Server**:
- [ ] Translate physical device handles and call `vkEnumerateDeviceLayerProperties(real_device, pLayerName, pCount, pProperties)`
- [ ] Return counts and properties as provided by the backend; validation layers/tooling expect accurate results even if zero layers exist

### vkGetPhysicalDeviceFeatures2

**Client**:
- [ ] Encode `VkPhysicalDeviceFeatures2` plus arbitrary `pNext` feature chains (e.g., Vulkan11/12/13 features)
- [ ] Keep `sType` and pointer order intact so server can rewrite structs directly

**Server**:
- [ ] Invoke `vkGetPhysicalDeviceFeatures2` on the real device and copy the entire chain back without filtering unknown structs

### vkGetPhysicalDeviceQueueFamilyProperties2

**Client**:
- [ ] Handle count-only queries (when `pQueueFamilyProperties` is null) and full data requests
- [ ] Support nested `pNext` structures (e.g., checkpoint properties) and verify array sizes before decoding responses

**Server**:
- [ ] Translate physical device handle and call `vkGetPhysicalDeviceQueueFamilyProperties2`
- [ ] Return queue count, flags, timestamps support, etc., so logical device creation picks correct queues

### vkGetPhysicalDeviceMemoryProperties2

**Client**:
- [ ] Encode `VkPhysicalDeviceMemoryProperties2` and store the results locally for future allocation heuristics
- [ ] Ensure heap/type arrays remain aligned when decoded

**Server**:
- [ ] Forward call to `vkGetPhysicalDeviceMemoryProperties2` and copy results verbatim

### vkGetPhysicalDeviceImageFormatProperties / 2

**Client**:
- [ ] Encode `VkPhysicalDeviceImageFormatInfo2` for the modern call along with supporting output structs
- [ ] Provide fallback path for legacy `vkGetPhysicalDeviceImageFormatProperties` so older apps can still query

**Server**:
- [ ] Translate handles and call the matching driver API, then return supported tiling/usages
- [ ] Propagate `VK_ERROR_FORMAT_NOT_SUPPORTED` accurately so client can bail out early

### vkFlushMappedMemoryRanges / vkInvalidateMappedMemoryRanges

**Client**:
- [ ] Reuse shadow buffer metadata from Phase 08 to gather the touched ranges for flush/invalidate requests
- [ ] For flush: send modified data slices over VENUS_PLUS transfer commands before notifying the server
- [ ] For invalidate: request data from server and copy it into the mapped buffer before returning

**Server**:
- [ ] Issue real `vkFlushMappedMemoryRanges` / `vkInvalidateMappedMemoryRanges` when backend memory is non-coherent
- [ ] Only acknowledge once writes/reads are complete to avoid races with subsequent submissions

### vkCreateBufferView / vkDestroyBufferView

**Client**:
- [ ] Encode `VkBufferViewCreateInfo` (buffer handle, format, offset, range) and track view handles for descriptor bindings
- [ ] Destroy views deterministically during cleanup

**Server**:
- [ ] Translate buffer handles, call `vkCreateBufferView`, store mapping, and ensure `vkDestroyBufferView` frees backend objects

### vkGetDeviceProcAddr

**Client**:
- [ ] Ensure the ICD returns function pointers for all new commands so the loader can link them dynamically
- [ ] Update dispatcher tables so per-device lookups route through encoded network calls instead of default stubs

**Server**:
- [ ] None; this is handled fully client-side but must be tested with `vulkaninfo` dynamic queries

### vkGetBufferMemoryRequirements2 / vkGetImageMemoryRequirements2

**Client**:
- [ ] Encode input structs plus any chained requirements descriptors (drm modifier, sampler-ycbcr, etc.)
- [ ] Use returned sizes/alignment to drive new resource allocations before creating image views or buffer views

**Server**:
- [ ] Call Vulkan's V2 memory requirement APIs and pass results directly back to client

## Test Application

**File**: `test-app/phase09_1/phase09_1_test.cpp`

**Test 1: Extension Enumeration**
```cpp
// Verify extensions are enumerated
uint32_t count = 0;
vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
// Should return non-zero count

std::vector<VkExtensionProperties> extensions(count);
vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.data());
// Should have real extensions from GPU
```

**Test 2: Memory Mapping Round-Trip**
```cpp
// Create buffer, allocate host-visible memory
// Map memory
void* data;
vkMapMemory(device, memory, 0, bufferSize, 0, &data);

// Write test pattern
float* floatData = static_cast<float*>(data);
for (int i = 0; i < 1024; i++) {
    floatData[i] = static_cast<float>(i);
}

// Unmap (transfers to GPU)
vkUnmapMemory(device, memory);

// Submit compute shader that doubles values
// ...

// Map again to read back
vkMapMemory(device, memory, 0, bufferSize, 0, &data);

// Verify results
floatData = static_cast<float*>(data);
for (int i = 0; i < 1024; i++) {
    assert(floatData[i] == i * 2.0f);
}

vkUnmapMemory(device, memory);
```

**Test 3: Image View Creation**
```cpp
// Create image
// Create image view
VkImageViewCreateInfo viewInfo = {};
viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
viewInfo.image = image;
viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
// ...

VkImageView imageView;
vkCreateImageView(device, &viewInfo, nullptr, &imageView);
```

**Expected Output**:
```
Phase 9.1: Compute Application Compatibility
=============================================
✅ Device extensions enumerated: 125 extensions found
   VK_KHR_swapchain
   VK_KHR_maintenance1
   VK_KHR_get_memory_requirements2
   ... (showing first 3)
✅ vkGetPhysicalDeviceProperties2 works
   Driver: NVIDIA 535.154.05
   Vulkan 1.3 supported
✅ Memory mapping test:
   Created 4KB host-visible buffer
   Mapped memory successfully
   Wrote test pattern [0, 1, 2, ..., 1023]
   Unmapped (data transferred to GPU)
   Executed doubling compute shader
   Mapped memory for readback
   Results verified: all 1024 values correct
✅ Image view creation works
✅ Sampler creation works
✅ Phase 9.1 PASSED

🎯 Compute applications are now compatible!
```

## Implementation Checklist

### Days 1-2: Critical Commands
- [ ] Implement vkEnumerateDeviceExtensionProperties
- [ ] Implement vkEnumerateDeviceLayerProperties
- [ ] Implement vkMapMemory
- [ ] Implement vkUnmapMemory
- [ ] Test with vulkaninfo
- [ ] Test memory round-trip

### Days 3-4: Extended Queries & Views
- [ ] Implement vkGetPhysicalDeviceProperties2
- [ ] Implement vkGetPhysicalDeviceFeatures2
- [ ] Implement vkCreateImageView / vkDestroyImageView
- [ ] Implement vkCreateSampler / vkDestroySampler
- [ ] Test texture-based compute shader

### Days 5-7: Completeness & Testing
- [ ] Implement remaining Properties2/Memory2 queries
- [ ] Implement buffer views
- [ ] Implement Flush/Invalidate memory ranges
- [ ] Implement vkGetBufferMemoryRequirements2
- [ ] Implement vkGetImageMemoryRequirements2
- [ ] Write comprehensive test app
- [ ] Run vulkaninfo and verify full output
- [ ] Regression tests
- [ ] Fix bugs

## Success Criteria

- [ ] vulkaninfo --summary shows all device extensions
- [ ] vulkaninfo shows full device capabilities
- [ ] Memory mapping works for read and write
- [ ] Image views can be created and used
- [ ] Samplers can be created
- [ ] Phase 9.1 test passes
- [ ] Phase 9 regression tests still pass
- [ ] No validation errors

## Deliverables

- [ ] Device extension enumeration
- [ ] Extended property/feature query commands
- [ ] Memory mapping commands with data transfer
- [ ] Image view commands
- [ ] Sampler commands
- [ ] Buffer view commands
- [ ] Phase 9.1 test application
- [ ] Updated documentation

## Common Issues

**Issue**: vkEnumerateDeviceExtensionProperties returns 0 extensions
**Solution**: Check physical device handle translation, verify calling real API

**Issue**: Memory mapping crashes on unmap
**Solution**: Verify buffer size matches, check offset calculations

**Issue**: Properties2 returns garbage in pNext chain
**Solution**: Properly handle pNext chain encoding/decoding, check sType values

**Issue**: Image view creation fails
**Solution**: Verify image was created with correct usage flags, check format compatibility

## Module Updates

**Client**
- Extend `client/commands/physical_device_commands.cpp` with new enumeration/property handlers and expand `common/encode.h` for pNext marshalling
- Reuse and harden shadow-buffer helpers (`client/state/shadow_buffer.*`) so flush/invalidate share bookkeeping with map/unmap
- Add handle tables for image views, samplers, and buffer views in `client/state/handle_tables.*`
- Grow the test harness under `test-app/phase09_1` with reusable staging helpers in `test-app/common/compute_utils.*`

**Server**
- Add decoder cases in `server/decoder/command_decoder.cpp` plus per-type executors (memory requirements, buffer views, image views)
- Track lifetimes via `server/state/resource_tracker.*` to avoid leaks during reconnects
- Add memory-requirements helpers near `server/executor/memory_executor.*` for the V2 APIs

**Docs & Tooling**
- Update `docs/INDEX.md` with cross-links once commands land
- Capture baseline vulkaninfo output under `artifacts/`
- Add gtests around encode/decode flows in `common/tests/protocol_roundtrip_tests.cpp`

## Risks & Mitigations

- **Large pNext chains**: allocate temporary staging buffers sized to incoming byte counts; fuzz with validation layers on
- **Non-coherent memory stalls**: batch contiguous flush ranges and emit warnings when transfers exceed 10ms
- **Descriptor/view leaks**: run ASAN builds at least once per day and ensure `vkDestroy*` commands run during shutdown tests
- **Capability mismatches**: diff extension/feature lists against native vulkaninfo output and fail tests when new entries disappear

## Memory Mapping Architecture

```
Client                          Network                         Server
======                          =======                         ======

vkMapMemory()
  → Allocate local buffer
  → Send map request         →  [MAP_MEMORY msg]  →     vkMapMemory()
  ← Receive success          ←  [SUCCESS]         ←     Store mapping info
  → Return local ptr

[App writes to buffer]

vkUnmapMemory()
  → Read local buffer
  → Send with data           →  [UNMAP + DATA]    →     memcpy to GPU memory
  → Free local buffer                                    vkUnmapMemory()
  ← Receive success          ←  [SUCCESS]         ←

vkMapMemory() (readback)
  → Allocate local buffer
  → Send map request         →  [MAP_MEMORY]      →     vkMapMemory()
  ← Receive GPU data         ←  [SUCCESS + DATA]  ←     memcpy from GPU memory
  → Copy to local buffer
  → Return local ptr
```

## Verification & Telemetry

- Log enumeration counts, queue-family masks, and memory-property hashes under `logs/phase09_1/` for quick diffing between GPUs
- Add CI coverage for `VK_DRIVER_FILES=... ./build/test-app/venus-test-app --phase 9_1 --verbose`
- Track flush/invalidate throughput (bytes per second) and print summary lines in the test app output
- Persist extension list snapshots so regressions can be caught by diffing `artifacts/phase09_1_baseline.txt`

## Next Steps

After Phase 9.1, proceed to **[PHASE_10.md](PHASE_10.md)** for graphics rendering, which builds on the image view infrastructure added here.

Alternatively, test with real compute applications:
- Simple GPGPU frameworks
- Image processing tools
- ML inference engines (compute-only)
