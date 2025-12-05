# Vulkan 1.4 Readiness Plan

**Prerequisite**: `VULKAN_1_3_COMPLETION_PLAN` is green. API reporting has been bumped to 1.4 after implementing and testing the items below.

## Protocol & Version Gates
- Regenerate `common/venus-protocol` from `vk.xml` containing `VK_VERSION_1_4`; refresh generated encoders/decoders.
- Do not bump `vkEnumerateInstanceVersion` or device versions until tests pass.
- Extend extension filtering and feature/property propagation to include `VkPhysicalDeviceVulkan14Features` and `VkPhysicalDeviceVulkan14Properties`.

## Commands to Implement (Core 1.4)
- Dynamic rendering setters: `vkCmdSetRenderingAttachmentLocations`, `vkCmdSetRenderingInputAttachmentIndices` (validate attachment counts).
- Line rasterization: `vkCmdSetLineStipple` gated by `VkPhysicalDeviceLineRasterizationFeatures/Properties`.
- Memory map v2: `vkMapMemory2`, `vkUnmapMemory2` via `VkMemoryMapInfo` / `VkMemoryUnmapInfo` with offset/size/flags validation.
- Index buffer update: `vkCmdBindIndexBuffer2` (size-aware binding).
- Descriptor/push updates: `vkCmdBindDescriptorSets2`, `vkCmdPushConstants2`, `vkCmdPushDescriptorSet2`, `vkCmdPushDescriptorSetWithTemplate2`, plus legacy `vkCmdPushDescriptorSet` / `vkCmdPushDescriptorSetWithTemplate` promotion coverage.
- Host image copy + transitions (Maintenance6): `vkCopyMemoryToImage`, `vkCopyImageToMemory`, `vkCopyImageToImage`, `vkTransitionImageLayout`, using `VkMemoryToImageCopy`, `VkImageToMemoryCopy`, `VkCopy*Info`, and `VkHostImageLayoutTransitionInfo`; validate `VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT` before issuing.
- Layout queries: `vkGetRenderingAreaGranularity`, `vkGetDeviceImageSubresourceLayout`, `vkGetImageSubresourceLayout2`.

## Features, Properties, and New Structs
- Plumb new structs through physical-device queries: global priority (`VkPhysicalDeviceGlobalPriorityQueryFeatures`, `VkQueueFamilyGlobalPriorityProperties`, `VkDeviceQueueGlobalPriorityCreateInfo`), shader subgroup rotate/float-controls2/expect-assume, line rasterization, vertex divisor + uint8 index, dynamic rendering local read, push descriptor properties, pipeline robustness, pipeline create flags2 and buffer usage flags2, maintenance5/6, host image copy features/properties, pipeline protected access, and Vulkan 1.4 properties arrays (copySrcLayouts/copyDstLayouts) with sensible defaults when the driver leaves them empty.
- Accept new pNext structures in pipeline/buffer creation (`VkPipelineCreateFlags2CreateInfo`, `VkBufferUsageFlags2CreateInfo`) and propagate to the server; log/ignore upper 32-bit flags if unsupported.

## Testing
- Add gtests for: map/unmap2, bind index buffer2, descriptor set bind2/push2, rendering attachment locations, host image copy and transition, line stipple.
- Extend physical-device query tests to assert 1.4 feature/property structs are populated.
- Add headless integration in `test-app` for host image copy once wired.

## Visibility & Dispatch Table
- Update `vkGet*ProcAddr` exports to include new 1.4 commands; ensure only `vk_icd*` remain public.
- Confirm protocol generation includes new command IDs; adjust generation scripts if necessary.
