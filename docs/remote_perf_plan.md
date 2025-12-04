# Remote Performance Plan (Venus Plus)

## Current bottleneck (from perf findings)
- Per-message latency dominates: steady-state `send` ~6–12 ms, `recv` ~9–11 ms; >10k messages per prompt run.
- Payload sizes are modest (~20–120 KB), so bandwidth is not the limiter.
- Invalidate is already small (~146 KB per wait); compute/submit cadence fixed by app; remaining ceiling is round-trip count and serialization.

## Goals
- Reduce effective round-trip cost per token by minimizing message count and in-flight serialization without breaking protocol semantics.
- Keep defaults compatible; use tracing to validate improvements.

## Work items
- **Coalesce protocol messages per submit/wait**: bundle small control messages into a single packet per submit/wait where ordering allows.
- **Pipeline send/recv**: add optional client receive thread/queue so multiple requests can be in flight; avoid strict request/response serialization on the main thread.
- **Batch small transfers**: ensure all per-wait memory reads are sent as one batch (already true for invalidate); audit other command paths for tiny sends.
- **Transport tuning**: keep TCP_NODELAY/QUICKACK and larger buffers; consider optional Unix-domain socket path for same-host; expose a server/client socket-buffer knob if needed.
- **Tracing**: keep `VENUS_TRACE_NET` summaries to validate improvements (calls, avg/max us, avg bytes).
- **Existing toggles for reference**:
  - Coherence/invalidate: `VENUS_INVALIDATE_MAX_BYTES`, `VENUS_INVALIDATE_ON_WAIT`, `VENUS_INVALIDATE_HANDLES`, `VENUS_TRACE_MEM`, `VENUS_LOG_LEVEL`, `VENUS_LOG_CATEGORIES`.
  - Network: `VENUS_TRACE_NET` (logs per-100 message stats), TCP_NODELAY/QUICKACK and larger socket buffers are enabled by default.
  - Submit cadence (llama Vulkan backend): `GGML_VULKAN_NODES_PER_SUBMIT`, `GGML_VULKAN_MULMAT_BYTES_PER_SUBMIT`, `GGML_VULKAN_SUBMIT_ALMOST_READY`.

## Implementation defaults/toggles
- Latency/coalesced submit/wait and pipelined receive are disabled; only baseline TCP path is used.
- `VENUS_SOCKET_BUFFER_BYTES=<bytes>` overrides the 4 MiB socket buffers on both client and server.

## Milestones
- M1: implement message coalescing where protocol permits; measure drop in send/recv call counts per token.
- M2: add optional pipelined receive (in-flight >1) behind env flag; measure RTT hiding and TPS impact.
- M3: harden/clean defaults; document flags/envs for latency mode; validate no regression in local/native runs.
