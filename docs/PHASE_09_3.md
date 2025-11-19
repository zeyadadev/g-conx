# Phase 09_3: Complete Compute Path

**Complete every compute-specific Vulkan command so production compute workloads can execute through Venus Plus without workarounds.**

## Overview

**Goal**: Implement push constants, indirect dispatch, pipeline caches, query pools, events, specialization constants, and descriptor coverage so applications such as llama.cpp, Stable Diffusion preprocessors, and perf profilers run unmodified.

**Duration**: 8–10 days (assuming two engineers splitting client/server work).

**Prerequisites**: Phases 09, 09_1, and 09_2 are green; Phase 10 base (device/queue/lifetime plumbing) is merged; `common/venus-protocol` is regenerated from the desired `vk.xml`.

**Dependencies**
- Build tree generated with `cmake -S . -B build && cmake --build build`.
- Networking tunnel verified (`./build/server/venus-server --port 5556` reachable from the ICD).
- `test-app/phase09` still passes so we know compute regressions originate from this phase.

## Why This Matters

Push constants, indirect dispatch, query pools, and events show up in almost every real compute workload. Without them the loader either falls back to CPU implementations or exits with `VK_ERROR_FEATURE_NOT_PRESENT`. Completing this phase removes the remaining "holes" so real-world compute benchmarks can validate performance, profiling, and remote scheduling.

## Kickoff Checklist

- [ ] Run `ctest --test-dir build/client/tests --output-on-failure` plus `build/server/tests` to ensure the tree is clean.
- [ ] Capture baseline info: `VK_DRIVER_FILES=$PWD/build/client/venus_icd.x86_64.json vulkaninfo --summary > artifacts/phase09_3_pre.txt`.
- [ ] Reset server/client instances so no stale handles remain in `client/state/*`.
- [ ] Note all missing commands in `docs/INDEX.md` so other agents know this phase owns them.
- [ ] Rebuild the test app (`cmake --build build --target venus-test-app`) to pull in new shaders/tests.
- [ ] Coordinate TCP port 5556 usage before running both server and ICD.

## Current Coverage Snapshot

### Already Working (Phases 09 – 09_2)

- Shader modules, descriptor set layouts/pools/sets, and pipeline layouts.
- Compute pipelines (`vkCreateComputePipelines`) and pipeline binding.
- Descriptor updates for storage buffers and sampled buffers.
- Command buffer recording: bind pipeline, bind descriptor sets, dispatch, pipeline barriers.
- Samplers, buffer views, enumerations, driver branding, properties/layer queries.

### Still Missing in 09_3

| Area | Commands / Tasks | Notes |
|------|------------------|-------|
| Push constants | `vkCmdPushConstants` | Requires storing push constant ranges per pipeline layout |
| Indirect dispatch | `vkCmdDispatchIndirect`, `vkCmdDispatchBase` | Enables GPU-driven workloads |
| Pipeline caches | `vkCreatePipelineCache`, `vkDestroyPipelineCache`, `vkGetPipelineCacheData`, `vkMergePipelineCaches` | Needs handle tables and blob transfer |
| Query pools | `vkCreateQueryPool`, `vkDestroyQueryPool`, `vkCmdResetQueryPool`, `vkCmdBeginQuery`, `vkCmdEndQuery`, `vkCmdWriteTimestamp`, `vkGetQueryPoolResults`, `vkCmdCopyQueryPoolResults`, `vkResetQueryPool` | Timestamp & pipeline statistics |
| Events | `vkCreateEvent`, `vkDestroyEvent`, `vkGetEventStatus`, `vkSetEvent`, `vkResetEvent`, `vkCmdSetEvent`, `vkCmdResetEvent`, `vkCmdWaitEvents` | Requires new sync tracking |
| Specialization constants | Verify `VkSpecializationInfo` path | No new commands, but add validation/tests |
| Descriptor coverage | Ensure `vkUpdateDescriptorSets` handles storage images, sampled images, uniform buffers, texel buffers, samplers | Expand client encoding and tests |
| Integration tests | New `test-app/phase09_3` scenarios and round-trip gtests | Cover all new functionality |

## Scope & Deliverables

- Push constants and indirect dispatch wired end-to-end (client encoder, server decoder, validation).
- Pipeline cache management with persistence verification.
- Query pool + timestamp functionality validated round-trip.
- Event APIs (host + command buffer variants) implemented.
- Specialization constant and descriptor coverage verified.
- New regression tests in `test-app/phase09_3` plus gtests for client/server/common modules.
- Documentation updates (`docs/INDEX.md`, this file, and `docs/PHASES_02_TO_10_SUMMARY.md` entry).

## Implementation Guide

### 1. Push Constants (`vkCmdPushConstants`)

**Client (`client/icd/icd_entrypoints.cpp`, `client/state/pipeline_state.*`)**
- Extend `PipelineLayoutInfo` to store the push constant ranges from `VkPipelineLayoutCreateInfo`.
- Validate `offset` + `size` fits in at least one stored range before encoding.
- Use the generated `vn_async_vkCmdPushConstants` helper to emit the command (stage flags, offset, size, and inline data).
- Log a validation error if the command buffer is not recording (reuse `ensure_command_buffer_recording`).
- Add a client gtest (create one under `client/tests/` if needed) to ensure invalid ranges are rejected.

**Server (`server/renderer_decoder.c`)**
- Add `server_dispatch_vkCmdPushConstants`, translate the pipeline layout through `resource_tracker.get_real_pipeline_layout`, and call `vkCmdPushConstants`.

**Integration Test**
- Add `test-app/phase09_3/shaders/push_constant.comp` like the scatter example in the placeholder.
- Validate output buffer contents after dispatch.

### 2. Indirect Dispatch (`vkCmdDispatchIndirect`, `vkCmdDispatchBase`)

**Client**
- Track indirect buffer handles via `client/state/resource_state.*`; reject calls if the buffer is unknown.
- Encode `VkDeviceSize offset` verbatim.
- Gate `vkCmdDispatchBase` behind API version ≥ 1.1 (expose per-device version in `client/state/device_state.*`).
- Update `client/icd/icd_entrypoints.{h,cpp}` and loader negotiation tables.

**Server**
- Add dispatchers that translate buffer and command buffer handles, guard recording state, and call the Vulkan functions.
- Validate the buffer is bound to memory using `resource_tracker.buffer_exists` and log errors when not.

**Tests**
- Extend `test-app/phase09_3` to write a `VkDispatchIndirectCommand`, issue `vkCmdDispatchIndirect`, and verify invocation counts via buffer writes.

### 3. Pipeline Cache Management

**Client (`client/state/pipeline_state.*`)**
- Add `PipelineCacheInfo` storing device, remote handle, and last blob size.
- `vkCreatePipelineCache`: allocate a client handle, send `VkPipelineCacheCreateInfo` (including `pInitialData`), and store the remote handle.
- `vkGetPipelineCacheData`: honor the two-call pattern (`pData == nullptr` query vs. real blob). Copy server data into the client buffer.
- `vkMergePipelineCaches`: ensure all caches belong to the same device; encode the full list of handles.
- Allow `vkCreateComputePipelines` to accept optional caches now that handle mapping exists.

**Server (`server/state/resource_tracker.*`, `server/renderer_decoder.c`)**
- Introduce `PipelineCacheResource` mapping client handles to real caches and store them in `ResourceTracker`.
- Create/destroy caches via the tracker, ensuring cleanup on shutdown.
- `vkGetPipelineCacheData`: allocate a temporary buffer sized by the client request, call Vulkan, and send the blob back through the reply path.
- `vkMergePipelineCaches`: translate handles and forward.

**Tests**
- Add a server-side gtest (new file under `server/tests/`) verifying cache creation, data retrieval, and merge operations succeed.
- In `test-app/phase09_3`, create a cache, build pipelines twice, and assert the second creation reuses the cache (timing or non-zero blob length).

### 4. Query Pools (timestamps & statistics)

**Client**
- Create `client/state/query_state.{h,cpp}` (or extend an existing tracker) to record query pools (type, count, pipeline statistics, remote handle).
- Implement creation/destruction helpers and guard `firstQuery + queryCount <= pool.queryCount`.
- Encode device commands (`vkCmdResetQueryPool`, `vkCmdBeginQuery`, `vkCmdEndQuery`, `vkCmdWriteTimestamp`, `vkCmdCopyQueryPoolResults`) via generated async helpers.
- `vkGetQueryPoolResults`: send synchronous RPC to pull data, supporting `VK_QUERY_RESULT_PARTIAL_BIT`, `VK_QUERY_RESULT_WAIT_BIT`, and 32/64-bit reads.
- `vkResetQueryPool`: host command; forward to server immediately.

**Server**
- Extend `ResourceTracker` with `QueryPoolResource` storing type, count, stats flags, and real handles.
- Add dispatchers for each query command:
  - Device commands translate handles and call Vulkan on the recorded command buffer.
  - Host commands call Vulkan directly and copy data into replies.
- For timestamp queries, honor queue family support (log error if unsupported).

**Tests**
- Add a server gtest to reset/read pipeline statistics and host resets.
- `test-app/phase09_3`: include a timestamp query test capturing times before/after a dispatch and reading back via `vkGetQueryPoolResults`.

### 5. Events (host + command buffer)

**Client**
- Extend `client/state/sync_state.*` with `EventInfo` storing device, remote handle, and cached status.
- Implement `vkCreateEvent`, `vkDestroyEvent`, `vkGetEventStatus`, `vkSetEvent`, and `vkResetEvent` by allocating remote events through the ring and updating cached state.
- Encode `vkCmdSetEvent`, `vkCmdResetEvent`, and `vkCmdWaitEvents` into the command buffer stream. Reuse the existing pipeline barrier encoder for the barrier arrays.

**Server**
- Add event tracking to `venus_plus::SyncManager` (real handle map + status caching).
- Dispatch host commands by translating handles and calling Vulkan immediately.
- Command buffer commands must verify recording state and call the real Vulkan functions just like existing pipeline barrier code.

**Tests**
- Add host-side gtests verifying set/reset/status behavior.
- Integration test: command buffer writes to buffer A, sets event, waits on the event, then writes to buffer B. Validate ordering by checking buffer contents.

### 6. Specialization Constants

- Audit `vkCreateComputePipelines` in `client/icd/icd_entrypoints.cpp` to ensure `VkSpecializationInfo` (map entries + data) is serialized into the request (use `vn_write_array` helpers).
- In `server/state/resource_tracker.cpp`, ensure the temporary copies of `VkComputePipelineCreateInfo` include valid `VkSpecializationInfo` pointers before calling Vulkan.
- Add a `test-app/phase09_3` shader (`spec_constants.comp`) that uses `layout(local_size_x_id = 0)` and a constant ID for a math parameter. Provide different `VkSpecializationInfo` data and verify output changes.

### 7. Descriptor Type Coverage

- Update `vkUpdateDescriptorSets` encoding so every descriptor type used by compute shaders is handled:
  - `VK_DESCRIPTOR_TYPE_{UNIFORM,STORAGE}_BUFFER(_DYNAMIC)`
  - `VK_DESCRIPTOR_TYPE_{UNIFORM,STORAGE}_TEXEL_BUFFER`
  - `VK_DESCRIPTOR_TYPE_{SAMPLED,STORAGE}_IMAGE` and `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`
- Ensure `client/state/resource_state.*` can look up the backing image views, buffer views, and samplers referenced in descriptor writes.
- Add a table-driven gtest under `client/tests/` that exercises each descriptor type to prevent regressions.
- Extend the test app with a scenario that uses a uniform buffer plus storage image to confirm shader-visible behavior.

### 8. Protocol & Plumbing

- Update ICD exports (`client/icd/icd_entrypoints.cpp` and `.h`) so all new commands are reachable through `vkGetDeviceProcAddr`.
- Confirm `common/venus-protocol` already generates the needed encoders/decoders. If the generated headers lack any command, re-run the `update_venus_protocol.sh` helper using the provided `vk.xml`.
- Register the new server dispatchers inside `renderer->ctx` initialization in `server/renderer_decoder.c`.
- Keep documentation synchronized by updating `docs/INDEX.md` and `docs/PHASES_02_TO_10_SUMMARY.md` once commands are implemented.

## Test Plan

1. **Push Constant Scatter Test** (`test-app/phase09_3`)  
   Writes an offset + scale via push constants and validates the storage buffer.
2. **Indirect Dispatch Test**  
   Writes a `VkDispatchIndirectCommand`, executes `vkCmdDispatchIndirect`, and checks invocation counts.
3. **Pipeline Cache Persistence**  
   Creates a pipeline cache, builds pipelines twice, asserts the second build reuses the cache (non-zero blob + reduced time).
4. **Timestamp Query Test**  
   Records timestamps around a dispatch, reads them with `vkGetQueryPoolResults`, and ensures `t1 > t0`.
5. **Event Synchronization Test**  
   Host sets an event, a command buffer waits for it, and dispatches occur in order.
6. **Descriptor Matrix Test**  
   Each descriptor type is exercised by a shader verifying uniform buffers, storage images, texel buffers, and combined samplers.
7. **Specialization Constant Test**  
   Provides different specialization data and verifies the workgroup size/output changes accordingly.

Run `VK_DRIVER_FILES=$PWD/build/client/venus_icd.x86_64.json ./build/test-app/venus-test-app --phase 9_3` plus `--phase all` and `ctest --output-on-failure` before closing the phase.

## Suggested Schedule

| Week | Focus |
|------|-------|
| Week 1 | Push constants, indirect dispatch, descriptor coverage, initial tests. |
| Week 2 | Pipeline cache + query pool plumbing (unit + integration tests). |
| Week 3 | Events, specialization constants, stabilization, documentation. |

Adjust as needed, but keep pipeline cache/query pool changes isolated before layering on event work (they share encoder infrastructure).

## Exit Criteria & Deliverables

- ✅ All commands listed in the scope implemented and reachable via the ICD.
- ✅ Push constant, indirect dispatch, pipeline cache, query pool, and event tests pass locally and in CI.
- ✅ `VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT` runs clean (no validation errors/warnings) for the `phase09_3` test app run.
- ✅ `docs/INDEX.md` references this phase, and `docs/PHASES_02_TO_10_SUMMARY.md` highlights the completed commands.
- ✅ Demonstrated llama.cpp (or similar) running end-to-end with Venus Plus; attach logs in the PR description.
- ✅ Regression suite (`ctest --output-on-failure` + `venus-test-app --phase all`) passes.

## Risks & Mitigations

- **Large payloads in `vkGetPipelineCacheData` / `vkGetQueryPoolResults`**  
  Stream blobs in chunks (≤64 KiB) to avoid exhausting the ring buffer; update `common/network` framing if necessary.
- **Command buffer state mismatches**  
  Use the existing `command_buffer_recording_guard` and log when commands are invoked outside the recording window.
- **Stage mask confusion with events**  
  Add a helper translating deprecated bits to core stage flags and warn when unsupported bits are provided.
- **Descriptor aliasing**  
  Ensure resources referenced by descriptor sets cannot be freed until descriptors are freed; add validation inside `resource_state`.

## Follow-Ups After 09_3

- Run llama.cpp / benchmark suite to collect performance data.
- Profile serialization overhead and consider batching command buffer messages.
- Document any non-default system tuning (hugepages, driver overrides) in `docs/TROUBLESHOOTING.md`.

