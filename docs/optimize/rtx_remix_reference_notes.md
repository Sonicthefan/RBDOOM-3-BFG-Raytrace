# RTX Remix Reference Notes

Findings from a read-only pass over the local RTX Remix checkout at
`E:/prog/references/dxvk-remix-git` on 2026-06-20. The goal was to understand
why Remix Nsight captures are dominated by `vkCmdDispatch` while still showing
high RT-core use, and which parts are worth testing in rbdoom.

## Main Conclusion

Remix is not fast because `vkCmdDispatch` itself fixes divergence. Its useful
patterns are:

1. ray-query compute variants for some path-tracing passes,
2. many compile-time shader variants instead of broad runtime branches,
3. ReSTIR DI/GI reuse as screen-space compute,
4. modest DI budgets,
5. adaptive reuse work after history converges.

Compute ray query can still use RT cores. Nsight labels the command as a compute
dispatch because the shader stage is compute, but traversal is still performed
by `rayQueryEXT` / SPIR-V ray-query operations. Judge these passes by RTCORE
throughput, active threads per warp, coherence, live registers, and total pass
time, not by whether the timeline event is `vkCmdDispatch` or
`vkCmdTraceRaysKHR`.

## Evidence In Remix

### Integrate Indirect Has Explicit Tracing Modes

Host switch:
`src/dxvk/rtx_render/rtx_pathtracer_integrate_indirect.cpp`

`DxvkPathtracerIntegrateIndirect::dispatch` supports three modes for the same
indirect integration pass:

- `RayQuery`: bind a compute shader and `dispatch`.
- `RayQueryRayGen`: bind a raytracing pipeline and `traceRays`.
- `TraceRay`: bind a raytracing pipeline and `traceRays`.

Shader variants:
`src/dxvk/shaders/rtx/pass/integrate/integrate_indirect.slang`

The same pass has separate variants for:

- `integrate_indirect_rayquery*.comp`
- `integrate_indirect_rayquery_raygen*.rgen`
- `integrate_indirect_raygen*.rgen`
- SER / non-SER
- NEE cache on/off
- NRC on/off
- WBOIT on/off

The compute and raygen variants wrap the same
`integrate_indirect_pass(...)` function, but the selected variant strips large
feature combinations at compile time.

### Ray Query Avoids TraceRay Payload/SBT Overhead

Common ray abstraction:
`src/dxvk/shaders/rtx/concept/ray/ray.slangh`

The ray-query path initializes a `rayQueryEXT`, advances it to the committed hit,
builds a compact `RayHitInfo`, and calls the hit function directly. The comment
explicitly notes that no payload encode/decode is needed in this path. The
TraceRay path must encode/decode payload around the ray pipeline boundary and use
hit/miss shader groups.

Implication: ray-query compute is a good candidate for simple closest-hit
producer and visibility work, especially when the hit shader would otherwise be
mostly a trampoline back into shared material evaluation.

### ReSTIR GI Reuse Is Compute-Only

Host dispatch:
`src/dxvk/rtx_render/rtx_restir_gi_rayquery.cpp`

`DxvkReSTIRGIRayQuery::dispatch` runs:

- `ReSTIR GI Temporal Reuse` as compute,
- `ReSTIR GI Spatial Reuse` as compute,
- `ReSTIR GI Final Shading` as compute.

These passes bind full-screen primary buffers and reservoir buffers. They are
not raygen passes. Some final validation can trace visibility rays from compute,
but the reservoir contract remains a screen-space compute workflow.

Implication: do not try to improve rbdoom temporal/spatial reservoir work by
moving it to raygen. If the work is screen-space resampling, compute is the
right default shape.

### RTXDI/DI Is Deliberately Small

Options:
`src/dxvk/rtx_render/rtx_rtxdi_rayquery.h`

Observed defaults:

- `initialSampleCount = 4`
- `spatialSamples = 2`
- `disocclusionSamples = 4`
- `maxHistoryLength = 4`
- `enableTemporalReuse = true`
- `enableSpatialReuse = true`
- `enableInitialVisibility = true`

There is also an important performance note on `enableSampleStealing`: no
visible IQ gain was observed, but it caused a considerable perf drop, cited as
8% in the integrate pass.

Implication: rbdoom DI should stay cheap when ReSTIR PT/GI is active. If DI
temporal or spatial costs are large, first test Remix-like low budgets and
disable sample stealing / heavy bias correction before deeper shader surgery.

### Initial Visibility Is Deferred To The Selected Sample

Shader:
`src/dxvk/shaders/rtx/pass/rtxdi/rtxdi_initial_sampling.comp.slang`

The shader defines `RTXDI_DEFER_INITIAL_VISIBILITY 1`. Candidate sampling runs
without tracing visibility for every random candidate, then visibility is updated
only for the selected reservoir sample when initial visibility is enabled.

Implication: when auditing rbdoom DI/GI candidate generation, check whether
visibility rays are being traced per candidate or only for the selected
reservoir. Per-candidate visibility is likely too expensive for these budgets.

### ReSTIR GI Spatial Reuse Is Adaptive

Shader:
`src/dxvk/shaders/rtx/pass/rtxdi/restir_gi_spatial_reuse.comp.slang`

Spatial reuse uses a large search radius and 4 samples only while the reservoir
history is young. Once history is mature, it drops to 1 spatial sample, with a
smaller radius on alternating tiles.

Implication: this is directly worth trying in rbdoom clean GI. A fixed high
spatial sample count after convergence is probably wasted work.

## rbdoom Experiments Suggested By This Comparison

### 1. Finish The GI Producer Ray-Query Correctness Path

Current rbdoom ray-query producer work is the right kind of experiment, but it
must be correctness-clean before performance conclusions matter. Once pure
ray-query mode renders rough/diffuse surfaces correctly, compare the same
producer as:

- TraceRay raygen,
- RayQuery raygen, if practical,
- RayQuery compute.

Measure the sum of trace + shade + reuse, not just the trace event.

### 2. Add Compile-Time Variants For Expensive Feature Combinations

Remix relies heavily on variant stripping. The closest rbdoom candidates are:

- specular producer on/off,
- NEE-cache seed on/off,
- secondary direct NEE on/off,
- ray-query producer on/off,
- rough fallback on/off,
- portal/translucency/material feature gates, where applicable.

Do not create all combinations blindly. Start with the combinations used in
profiling, then check register count, active threads, and pass time. The local
phase-split regression showed that splitting entry points without changing the
executed work is not enough.

### 3. Make DI Budgets Remix-Like Under ReSTIR PT/GI

Test a profile where DI is intentionally small:

- 4 initial candidates,
- 2 spatial samples,
- 4 disocclusion samples,
- history length around 4,
- no sample stealing,
- ray-traced spatial bias correction toggleable.

This matches the direction from Remix and the current observation that DI
temporal was much too expensive before stripping.

### 4. Try Adaptive GI Spatial Samples

Use reservoir history/age to select:

- 4 spatial samples while history is young or unstable,
- 1 spatial sample when history is mature,
- optional alternating larger/smaller radius by tile/frame.

This should be tested with boiling/noise captures, not just frame time, because
the whole point is trading converged reuse work against temporal stability.

### 5. Prefer Ray-Query Compute For Simple Traversal, Not Uber-Shading

Good candidates:

- visibility validation,
- selected-reservoir shadow rays,
- simple closest-hit producer trace,
- hit ID / geometry reconstruction paths.

Poor candidates:

- broad material uber-shading with many light-domain branches,
- paths that still need all the same divergent direct NEE immediately after the
  trace,
- rewrites that only change the API command name without reducing payload,
  branches, or live state.

## Nsight Interpretation

For Remix-style compute ray queries, a `vkCmdDispatch` can legitimately show RT
core throughput. In Nsight, compare:

- RTCORE throughput,
- active threads per warp,
- thread coherence,
- SM issue throughput,
- live registers,
- top stalls,
- total event time.

If RTCORE throughput rises but active threads/coherence remain poor, the pass is
still divergent. If RTCORE stays low and the event is mostly long scoreboard,
look for memory or texture access. If the event is mostly no-instruction with
low active threads, compute mode did not solve the real occupancy/coherence
problem.

## Current Priority Order

1. Fix pure ray-query GI producer correctness.
2. A/B producer TraceRay vs ray-query after correctness is stable.
3. Strip DI to Remix-like budgets when ReSTIR PT/GI is enabled.
4. Add adaptive GI spatial sample counts.
5. Add targeted compile-time variants for the exact profiled feature sets.
6. Revisit SER only after the pass structure has stabilized.
