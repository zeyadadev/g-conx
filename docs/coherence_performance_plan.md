# Coherent Memory Fix Follow-Up

## Goals
- Keep mapped CPU reads correct without dragging performance far below native drivers.
- Avoid unnecessary GPU→CPU copies, especially for large allocations (KV buffers).
- Validate with llama.cpp runs and inspect transfer volume.

## Tasks
- Instrument a short run with `VENUS_TRACE_MEM=1`, `VENUS_LOG_LEVEL=NONE`, and `VENUS_LOG_CATEGORIES=MEMORY=INFO` to measure how many `READ_TRANSFER` operations happen and their sizes; confirm only small ranges are auto-invalidated without turning on unrelated debug logs.
- Tag only the output/logit buffer for `invalidate_on_wait` if possible; ensure large allocations stay opt-out unless explicitly invalidated by the app.
- If corruption persists, raise the small-map threshold slightly (e.g., 32 MiB) and rerun; otherwise keep the current 16 MiB cap.
- Re-run llama perf and compare per-token latency to a native driver; document the delta and remaining overhead sources (network round-trips, waits).
- Upstream a minimal patchset: invalidate-on-wait for small mapped ranges, per-allocation flagging, and leave out debug tracing/IDE changes.

## Targeted logging (remote-focused)
- Default logging is now WARN; to avoid global debug noise set `VENUS_LOG_LEVEL=NONE` and opt-in only to memory traces with `VENUS_LOG_CATEGORIES=MEMORY=INFO`.
- Enable the coherence trace with `VENUS_TRACE_MEM=1` to log auto-invalidate batches (ranges scanned, bytes copied, skipped ranges >16 MiB or dirty).
- Leave all other categories at NONE/WARN so remote runs aren't slowed by unrelated logging while still surfacing memory-coherence diagnostics.

## Coherence knobs (remote perf)
- `VENUS_INVALIDATE_ON_WAIT=0` disables automatic invalidate-on-wait entirely (use when you want to prove corruption vs performance).
- `VENUS_INVALIDATE_MAX_BYTES=<bytes|MiB|KiB>` caps auto-invalidate batch size (default 16 MiB). Lower this to trim readbacks for small buffers only.
- `VENUS_INVALIDATE_HANDLES=0xA,0xB,...` (optional) whitelists which mapped handles may auto-invalidate; anything else is skipped.
- When `VENUS_TRACE_MEM=1`, each eligible mapping logs once (`handle`, `size`, `threshold`) plus per-wait batch summaries, so you can target the specific buffers that must stay enabled.
