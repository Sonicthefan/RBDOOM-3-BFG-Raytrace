Path Trace Emissive Sampling Optimization Guide
===============================================

Purpose
-------

This folder is for focused work on extracting and optimizing path-tracing
emissive proposal sampling.

The immediate trigger is mode 56 ReSTIR PT profiling: bad locations are
GPU-bound inside `vkCmdTraceRays`, RT core utilization is low, and Nsight points
at repeated `SmokeEmissiveTriangles` structured-buffer loads inside the
weighted emissive triangle selector. This suggests shader-side emissive
proposal selection is doing expensive linear scans over the authored emissive
triangle inventory.

Do not use this task folder as a reason to shrink the authored Doom emissive
universe. The goal is to preserve all authored emissive candidates while making
proposal sampling cheaper and easier to reason about.

Current Evidence
----------------

Mode 56 emits four `vkCmdTraceRays` events:

1. GI initial producer.
2. Direct temporal producer.
3. Direct spatial producer.
4. Final combined consumer.

User Nsight validation reports:

- Events 2 and 3 dominate frame time by far.
- Event 1 also roughly doubles in bad locations, but remains below events 2 and
  3.
- RT core utilization is only roughly 5-10% in the expensive cases.
- Sparsity visibly changes reconstruction, but does not provide meaningful
  performance relief.
- A hot SPIR-V line is an `OpLoad` of `PathTraceSmokeEmissiveTriangle` inside a
  loop over the emissive triangle buffer.

The likely source maps to:

    neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl
    SelectSmokeWeightedEmissiveTriangle

Current Hot Contract
--------------------

The current selector scans:

    SmokeEmissiveTriangles[0..emissiveTriangleCount)

and accumulates `sampleWeightAndPdf.y` until the random target is reached. It
also tracks the largest weight as a fallback. That is O(N) structured-buffer
loads per emissive proposal. Older ReSTIR PT docs referred to direct
temporal/spatial producer passes here, but DI/direct is currently raw reservoir
output; treat those labels as historical scaffolding.

Target End State
----------------

1. Emissive proposal code is isolated in a small shader include.
2. Existing behavior is preserved first, using the same linear scan behind the
   new boundary.
3. CPU builds a compact emissive distribution table from the existing full
   emissive inventory.
4. The shader uses the distribution table for O(log N) CDF lookup or O(1) alias
   lookup when valid.
5. The full authored emissive triangle buffer remains available for shading,
   attribution, visibility, and final light-info reconstruction.
6. The old linear selector remains as a fallback and A/B validation path.

Current Implementation Status
-----------------------------

Task 01 is complete: `SelectSmokeWeightedEmissiveTriangle` now lives behind
`neo/shaders/builtin/pathtracing/pathtrace_emissive_sampling.hlsli`, with the
legacy linear scan preserved as `SelectSmokeLinearWeightedEmissiveTriangle`.

Task 02A is implemented as a CDF table. CPU scene build creates
`PathTraceEmissiveDistributionEntry` records from the existing full emissive
triangle inventory after light-universe merge, uploads them as
`PathTraceEmissiveDistribution` at shader SRV `t46`, and passes count/valid/
fallback metadata through `EmissiveDistributionInfo`. The public shader selector
uses a binary-search CDF lookup when valid and falls back to the old linear scan
otherwise. The full authored `SmokeEmissiveTriangles` buffer is unchanged and
remains the source for shading, attribution, visibility ignore, and light-info
reconstruction.

Validation status: `cmake --build --preset win64-pt-dev-release` passed on
2026-05-14, and the rebuilt executable plus `base/renderprogs2` outputs were
copied to the prepared game folder. User runtime testing on 2026-05-14 reported
the result looked fine in the target ReSTIR/PT modes, so the CDF path has passed
the first visual smoke check. Follow-up user runtime testing reported roughly a
50% FPS boost in a previously laggy spot, which strongly supports the CDF
selector as the right first optimization. Exact Nsight per-pass A/B timing for
mode 56 with `r_pathTracingEmissiveDistribution 0` versus `1` is still useful
to confirm how much of the improvement landed in direct producer events 2 and
3 versus shared/location-sensitive work.

A/B control:

    r_pathTracingEmissiveDistribution 0  // force legacy linear scan
    r_pathTracingEmissiveDistribution 1  // use CDF when valid (default)

Non-Goals
---------

- Do not reduce the authored emissive triangle count as a fix.
- Do not change Doom material emissive classification in the extraction task.
- Do not merge this with GI temporal/spatial reuse.
- Do not rewrite mode 18 or the whole raygen while doing the first extraction.
- Do not remove Doom analytic light proposal paths.
- Do not treat Debug builds as performance evidence.

Required Reading
----------------

Read these first:

    docs/pathtracing.txt
    docs/pathtrace_modules.txt
    docs/ReSTIR/current_restir_pt_state.txt
    docs/ReSTIR/rough_restir_pt_preview_plan.txt
    docs/ReSTIR/ray_sparsity_plan.txt
    docs/pathtrace_emissive_sampling/task_01_extract_emissive_sampling.txt
    docs/pathtrace_emissive_sampling/task_02_build_emissive_distribution.txt

Relevant code:

    neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl
    neo/shaders/builtin/pathtracing/RtxdiBridge/PathTracer/RAB_PathTracer.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightInfoRuntime.hlsli
    neo/renderer/NVRHI/PathTraceLightSelection.cpp
    neo/renderer/NVRHI/PathTraceSceneCapture.cpp
    neo/renderer/NVRHI/PathTraceSmokeResources.cpp
    neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp
    neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp

Suggested Task Order
--------------------

1. Task 01 complete: extraction-only include boundary.
2. Task 02A complete: CPU-built CDF proposal table with linear fallback and
   low-noise smoke-log distribution stats.
3. Validate CDF on/off in-game with the modes below, then profile mode 56
   events 2 and 3 in Nsight.
4. Validate with modes 50, 51, 54, 55, 56, and mode 18 with
   `r_pathTracingRestirPTDirectLighting 1`.
5. Only then consider an alias table, further producer-pass consolidation, or
   reuse work.
