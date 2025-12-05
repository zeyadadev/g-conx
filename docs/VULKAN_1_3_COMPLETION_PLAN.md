# Vulkan 1.3 Completion Plan

**Goal**: eliminate the remaining 1.3 core gaps before advertising Vulkan 1.3 support.

## Render Pass & Draw Calls
- Implement `vkCmdBeginRenderPass2` / `vkCmdNextSubpass` / `vkCmdNextSubpass2` / `vkCmdEndRenderPass2` plus `vkCmdExecuteCommands`.
- Add indexed and indirect draws: `vkCmdBindIndexBuffer`, `vkCmdDrawIndexed`, `vkCmdDrawIndexedIndirect`, `vkCmdDrawIndexedIndirectCount`, `vkCmdDrawIndirect`, `vkCmdDrawIndirectCount`.
- Handle `vkCmdBindVertexBuffers2`, `vkCmdResolveImage`, `vkCmdClearAttachments`, `vkCmdClearDepthStencilImage`.
- **Tests**: headless render-pass gtest with subpasses + resolve; secondary command buffer execution; clears; integration triangle using indexed draw.

## Dynamic State & Device Mask
- Expose `vkCmdSetBlendConstants`, `vkCmdSetLineWidth`, `vkCmdSetDepthBias`, `vkCmdSetDepthBounds`, `vkCmdSetStencilCompareMask`, `vkCmdSetStencilReference`, `vkCmdSetStencilWriteMask`, `vkCmdSetDeviceMask`.
- **Tests**: table-driven dynamic-state encode/decode gtest validating server calls.

## Memory Binding & Pool Maintenance
- Add `vkBindBufferMemory2` / `vkBindImageMemory2` encoding/decoding and validation.
- Support `vkTrimCommandPool` and `vkGetDeviceMemoryCommitment`.
- **Tests**: multi-bind client gtest; server gtest for commitment; trim smoke test.

## Sparse & Device-Group Paths
- Implement `vkQueueBindSparse`, `vkGetDeviceQueue2`, `vkGetDeviceGroupPeerMemoryFeatures`, `vkEnumeratePhysicalDeviceGroups`.
- Add `vkGetImageSparseMemoryRequirements` and `vkGetImageSparseMemoryRequirements2`.
- **Tests**: round-trip enumeration/peer-feature gtests; sparse bind stub ensuring decode succeeds.

## Descriptor, Y′CbCr, and Private Data
- Cover `vkGetDescriptorSetLayoutSupport`, `vkCreateSamplerYcbcrConversion` / `vkDestroySamplerYcbcrConversion`, and private data (`vkCreate/DestroyPrivateDataSlot`, `vkSetPrivateData`, `vkGetPrivateData`).
- **Tests**: layout support + private data client gtests; server Y′CbCr creation/destroy smoke test.

## Format/External Queries & Tools
- Implement `vkGetPhysicalDeviceFormatProperties2`, `vkGetPhysicalDeviceSparseImageFormatProperties2`, external buffer/fence/semaphore properties, and `vkGetPhysicalDeviceToolProperties`.
- **Tests**: query gtest covering all structs with non-zero fields.

## Miscellaneous
- Add `vkGetRenderAreaGranularity`; ensure `vkEnumerateInstanceLayerProperties` returns an empty list.
- Decide when to flip reported API version to 1.3; keep visibility limited to `vk_icd*`.

## Protocol & Dispatch Table
- Regenerate Venus protocol if any of the above commands are missing from generated headers.
- Update `vkGet*ProcAddr` exports and verify `nm -D` only shows `vk_icd*`.
- Extend `test-app` (phase10 headless) and client/server gtests to cover the new commands.
