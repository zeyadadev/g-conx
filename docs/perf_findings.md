# Remote Performance Findings (llama.cpp with Venus Plus)

## What is Venus Plus
- A split Vulkan implementation: client-side ICD that marshals Vulkan calls and a remote GPU server that executes them.
- Used to run Vulkan applications over the network while preserving Vulkan semantics; shared code lives in `common/`, client in `client/`, server in `server/`.

## Environment
- App: `llama.cpp` (Vulkan backend) running remote via Venus Plus ICD.
- Env knobs during measurements: `VENUS_TRACE_MEM=1`, `VENUS_LOG_LEVEL=NONE`, `VENUS_LOG_CATEGORIES=MEMORY=INFO`, `VENUS_INVALIDATE_MAX_BYTES=256KiB`, `VENUS_INVALIDATE_ON_WAIT=1`, no handle whitelist.

## Why auto-invalidate is needed
- Client keeps a shadow mapping for HOST_COHERENT memory; GPU writes are not immediately visible in the client shadow copy.
- Auto-invalidate issues a GPU→CPU readback on fence/semaphore completion so the mapped CPU view is up to date without relying on the app to call `vkInvalidateMappedMemoryRanges`.
- When auto-invalidate is disabled, `llama.cpp` output becomes corrupted, confirming the CPU side needs these readbacks for correctness.

## ICD constraints on submits/waits
- `vkQueueSubmit` order and signaling are part of Vulkan’s defined execution and synchronization semantics; the ICD cannot merge, defer, or reorder submits without changing observable behavior or return codes.
- `vkWaitForFences`/`vkGetFenceStatus` are host-visible sync points; altering when the host unblocks (by combining or skipping waits) would change the app’s ordering and timeout semantics.
- Cross-queue/thread ordering relies on the app’s exact submits and waits, so reducing the cadence must be done by the caller, not transparently inside the ICD.

## Network path observations (remote run with VENUS_TRACE_NET=1)
- Message volume: >10,000 send/recv calls during a single prompt run.
- Per-message latency (steady state): sends ~6–12 ms avg; recvs ~9–11 ms avg. Early spikes observed (send max ~4.6 s, recv max ~10.45 s) during model load/init.
- Payload sizes (steady state): send avg ~80–120 KB; recv avg ~20–33 KB. Latency dominates; bandwidth is not saturated.
- Even with invalidate capped to ~146 KB per wait, per-token TPS is limited by the number of round-trip messages and per-message RTT.

## Auto-invalidate (host-coherent readback)
- Eligible mappings observed:
  - Handle `0x9`, size `8,192` bytes.
  - Handle `0x11`, size `129,056` bytes.
  - Handle `0x19`, size `1,024` bytes.
  - Handle `0x23c`, size `16,640` bytes.
- Per-wait batch summary with 256 KiB cap:
  - `eligible=3`, `bytes≈146,720`, `largest≈129,056`, `skipped_dirty=0`, `skipped_large=0`, `skipped_handle=0`, `cap=262,144`.
  - Per-100-call summary: `avg_us≈18,348`, `avg_bytes≈144,160`, `max_us≈39,409` (microseconds).

## Sync path timing (client-side measured)
- vkQueueSubmit:
  - Summary at 100 calls: `avg_us≈5,167`, `max_us≈18,858`.
  - Summary at 1,000 calls: `avg_us≈6,204`, `max_us≈24,945`.
  - Summary at 1,800 calls: `avg_us≈6,299`, `max_us≈24,945`.
- vkWaitForFences:
  - Summary at 100 calls: `avg_us≈6,418`, `max_us≈23,990`.
  - Summary at 400 calls: `avg_us≈7,294`, `max_us≈91,376`.
  - Summary at 500 calls: `avg_us≈7,382`, `max_us≈91,376`.

## Effective per-wait data volume
- With `VENUS_INVALIDATE_MAX_BYTES=256KiB`, per-wait readback is ~146 KB across three mappings (listed above); no large allocations are being auto-invalidated.
