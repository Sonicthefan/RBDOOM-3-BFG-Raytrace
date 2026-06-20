# GPU Optimization Notes

Working notes from the GPU performance pass on the path-tracing lanes (started
2026-06-19). Focus so far: the **clean ReSTIR GI lane**
(`neo/shaders/builtin/pathtracing/remix_restir_gi/pathtrace_clean_restir_gi.rt.hlsl`
+ `neo/renderer/NVRHI/PathTraceCleanRestirGi.cpp`).

## Files
- [`profiling_setup.md`](profiling_setup.md) — how to get usable GPU profiles:
  the Vulkan extension that had to be enabled, the marker cvar, the marker
  names, and the diagnostic cvars used to localize cost.
- [`restir_gi_results.md`](restir_gi_results.md) — what was tried, what worked,
  what didn't, with numbers and the reasoning/lessons behind each.

- [`rtx_remix_reference_notes.md`](rtx_remix_reference_notes.md) - local
  RTX Remix performance-structure notes: ray-query compute usage, shader
  specialization, DI/GI sample budgets, and the rbdoom experiments suggested by
  the comparison.

## TL;DR lessons (read before optimizing more)
1. **`VK_EXT_debug_utils` must be enabled** or all NVRHI markers and resource
   debug-names are silent no-ops — Nsight shows unnamed dispatches/resources.
   This was the original "Nsight gives no detail" problem.
2. The dominant stall on these passes is **"No Instructions"** = instruction
   fetch / I-cache / low occupancy from a **broad raygen entry point**. The
   "top instruction" columns in the stall view are a red herring.
3. **Splitting a raygen helps only when it redistributes genuinely different
   work** (e.g. RT-core-bound trace vs. divergent shade). Merely separating two
   dispatches that already exist but share one entry point does **not** help and
   can regress (~3 ms — see the temporal/spatial phase split).
4. **SER is a ~10% finisher, not a fix for big gaps**, and it does not raise
   occupancy. Don't reach for it first.
5. Reservoir/payload data is **lean** (32 B GI reservoir, 40 B payload) — these
   passes are not memory-bandwidth bound; don't chase that.
6. **Build the exe and shaders together.** A stale exe vs. fresh shader blob
   (or vice-versa) desyncs the RT pipeline's entry-point list and produces
   "failed to create GI pipeline" → purple screen.
