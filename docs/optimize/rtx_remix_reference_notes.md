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

The GI-specific finding from the deeper pass is that Remix does not run ReSTIR
GI as an isolated full-screen "producer trace + producer shade + reuse" lane. It
binds ReSTIR GI radiance, hit-geometry, and reservoir outputs into the existing
`Integrate Indirect Raytracing` pass, then later ReSTIR GI temporal/spatial
compute passes consume those compact side outputs. The expensive secondary path
work is therefore shared with the normal indirect integrator instead of being
duplicated by a separate GI-only producer.

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

### ReSTIR GI Production Is A Side Output Of Integrate Indirect

Host dispatch:
`src/dxvk/rtx_render/rtx_pathtracer_integrate_indirect.cpp`

The indirect integrator binds ReSTIR GI resources before tracing indirect rays:

- `bindIntegrateIndirectPathTracingResources(...)` binds `ReSTIR GI Radiance`,
  `ReSTIR GI Hit Geometry`, and the GI reservoir buffer.
- `Integrate Indirect Raytracing` then selects ray-query compute, ray-query
  raygen, or trace-ray raygen for the same indirect integration pass.

Shader path:
`src/dxvk/shaders/rtx/pass/integrate/integrate_indirect.slangh`
`src/dxvk/shaders/rtx/algorithm/integrator_indirect.slangh`

The shader writes a compact GI side channel while doing ordinary indirect path
integration:

- At initialization, it writes a ReSTIR GI radiance factor into the radiance
  texture instead of carrying the factor live through the whole integrator.
- While resolving the first suitable secondary hit, it writes hit position,
  normal, portal id, and opaque/non-opaque state into the hit-geometry target.
- At the end of path integration, it reloads the stored factor, multiplies by
  accumulated path radiance, encodes the indirect path length, and stores the
  final ReSTIR GI candidate radiance.

This is not the same shape as rbdoom's current `CleanGI.0a` trace secondary hit
-> large secondary surface buffer -> `CleanGI.0b` shade secondary surface ->
reuse flow. In Remix, the comparable candidate data is emitted while the
indirect path is already being traced and shaded.

Implication: optimizing rbdoom's standalone `CleanGI.0a/0b` passes has a hard
ceiling. The larger win is to stop duplicating secondary indirect work. If a
live rbdoom indirect/path-tracing pass already traces the same first indirect
ray and evaluates the same direct lighting, the ReSTIR GI producer should become
a compact side-output mode of that pass. If no such pass is active in the clean
GI route, merging `0a` and `0b` back together is not the Remix design; it just
recreates the earlier broad producer megakernel.

### Specular Handling Is MIS, Not A Second Full Producer

Options:
`src/dxvk/rtx_render/rtx_restir_gi_rayquery.h`

Shader path:
`src/dxvk/shaders/rtx/algorithm/integrator_indirect.slangh`
`src/dxvk/shaders/rtx/pass/rtxdi/restir_gi_final_shading.comp.slang`

Remix does not appear to run a separate full-screen specular seed trace/shade
producer comparable to rbdoom's `CleanGI.0d/0e`. It accepts or rejects the first
sample by roughness/lobe rules, supports virtual samples for highly specular
paths, steals mature ReSTIR GI samples inside the path tracer, and blends the
initial indirect result with the ReSTIR GI result using roughness/parallax MIS in
final shading.

Implication: rbdoom's separate specular producer is likely a major structural
cost. A Remix-shaped alternative is not "optimize the specular producer"; it is
to make the normal indirect sample remain the specular fallback and use MIS to
blend ReSTIR GI where it is reliable, especially on rougher surfaces.

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

### 1. Stop Duplicating The First Indirect Path

Audit whether any active rbdoom path-tracing or ReSTIR PT pass already traces
the first indirect ray and evaluates secondary direct lighting before clean GI
reuse. If yes, add a narrow side-output mode there:

- ReSTIR GI radiance factor / final candidate radiance,
- hit position and normal,
- indirect path length,
- opaque/non-opaque or alpha-tested-hit flag,
- optional portal/subview metadata if needed.

Then disable the standalone `CleanGI.0a/0b` producer path for an A/B. Measure
the whole GI lane, not just the removed events.

If there is no existing active pass to piggyback on, keep the split producer for
correctness and avoid merging it back into a monolithic raygen. The earlier
combined producer was exactly the broad-shader shape Remix is avoiding.

### 2. Reframe The Specular Producer As A Fallback/MIS Problem

Test whether specular producer work can be replaced by:

- normal first-indirect-path output for the initial/specular signal,
- ReSTIR GI only where roughness/lobe conditions make it reliable,
- final-shading MIS between initial indirect and ReSTIR GI output.

This is a larger correctness slice than a simple performance toggle, but it
matches the Remix structure better than a second full-screen specular trace and
shade producer.

Local A/B hook: `r_pathTracingCleanRestirGiSpecularProducer 2` keeps specular GI
final-output eligibility active but skips the separate full-screen specular seed
producer (`FirstIndirect.0d/0e`). Mode `1` is the previous full seed producer.
Use mode `2` to test whether fallback/reuse plus the normal first-indirect path
is visually close enough to justify removing or heavily restricting the separate
specular producer.

### 3. Finish The GI Producer Ray-Query Correctness Path

Current rbdoom ray-query producer work is still useful as a lower-level variant,
but it should not be mistaken for the full Remix design. Once pure ray-query mode
renders rough/diffuse surfaces correctly, compare:

- TraceRay raygen producer,
- RayQuery raygen producer, if practical,
- RayQuery compute producer,
- producer side-output from an existing indirect pass, if available.

Measure the sum of producer + shade + specular seed + reuse, not just the trace
event.

### 4. Add Compile-Time Variants For Expensive Feature Combinations

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

### 5. Make DI Budgets Remix-Like Under ReSTIR PT/GI

Test a profile where DI is intentionally small:

- 4 initial candidates,
- 2 spatial samples,
- 4 disocclusion samples,
- history length around 4,
- no sample stealing,
- ray-traced spatial bias correction toggleable.

This matches the direction from Remix and the current observation that DI
temporal was much too expensive before stripping.

### 6. Try Adaptive GI Spatial Samples

Use reservoir history/age to select:

- 4 spatial samples while history is young or unstable,
- 1 spatial sample when history is mature,
- optional alternating larger/smaller radius by tile/frame.

This should be tested with boiling/noise captures, not just frame time, because
the whole point is trading converged reuse work against temporal stability.

### 7. Prefer Ray-Query Compute For Simple Traversal, Not Uber-Shading

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

1. Keep DI and GI reservoirs separate, but make the first-indirect trace/shade
   payload estimator-neutral instead of GI-only.
2. Build a robust standalone first-indirect candidate producer and let Clean GI
   be the first consumer.
3. A/B that producer against standalone `CleanGI.0a/0b` once it can feed the
   same initial GI reservoir contract.
4. Rework specular GI contribution toward initial-path fallback + final MIS
   instead of a second full-screen specular producer.
5. Fix pure ray-query GI producer correctness as a lower-level variant, not the
   only path to Remix parity.
6. Add targeted compile-time variants for the exact profiled feature sets.
7. Revisit SER only after the pass structure has stabilized.
