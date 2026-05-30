Mode 18 NEE Extraction And RTXDI-Shaped Replacement Guide
=========================================================

Purpose
-------

This folder is for focused work on extracting mode 18 next-event estimation
(NEE) from the monolithic `pathtrace_smoke.rt.hlsl` integrator and replacing
the current brute-force debug loop with a native, bounded, RTXDI-shaped light
sampling path.

The first extraction target is a dedicated native shader module/include for
mode 18 NEE. `pathtrace_smoke.rt.hlsl` may still call that module during the
first task, but new NEE logic should not remain scattered through the
monolithic shader. A later task can move the module behind a separate shader
family or pass once the data contract is stable.

The goal is not to import NVIDIA sample code. The goal is to study the RTXDI
sample architecture, preserve compatible extension points, and implement the
smallest useful RBDoom3-native replacement for the current mode 18 NEE pain
point.

Current Evidence
----------------

The current mode 18 timing evidence is recorded in:

    docs/pathtrace_payload_live_state/mode18_nee_timing_findings.txt

Important observations:

    1280x720, 3 SPP, maxDepth 1, NEE 1:  2.4 ms
    1280x720, 3 SPP, maxDepth 2, NEE 1: 17.8 ms
    1280x720, 3 SPP, maxDepth 2, NEE 0:  2.8 ms
    3 SPP, maxDepth 2, NEE 1, diffuse secondary disabled: 2.8 ms
    3 SPP, maxDepth 2, NEE 1, selected-light loop disabled: about +0 ms

This points at secondary-bounce NEE fan-out as the immediate runtime cliff:

    samples per pixel
      * path depth
      * selected-light count
      * shadow visibility rays

The payload/live-state problem still matters, but the next measured pain point
is the brute-force direct-light evaluation performed at path vertices.

Important Current Code
----------------------

Primary shader:

    neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl

Expected new shader module:

    neo/shaders/builtin/pathtracing/pathtrace_nee.hlsli

Important current functions:

    EvaluateSmokeDirectLighting
    EvaluateDoomAnalyticSphereLights
    TraceSmokeShadowVisibility
    EvaluateSmokeToyPathTrace

Important current CVars:

    r_pathTracingNextEventEstimation
    r_pathTracingDisableSelectedLightLoop
    r_pathTracingDisableDiffuseSecondaryRay
    r_pathTracingAnalyticLightCandidates
    r_pathTracingDisableAnalyticLightLoop
    r_pathTracingAnalyticLightMaxGpu

Reference Material
------------------

Use NVIDIA RTXDI only as a reference for architecture, naming, data contracts,
and validation expectations. Do not copy code bodies, structs, functions, or
shader implementation text from the NVIDIA samples or runtime into this repo
unless the user explicitly confirms a licensing decision.

Local RTXDI reference paths:

    E:\prog\references\RTXDI-main\Doc\RestirPT.md
    E:\prog\references\RTXDI-main\Doc\ShaderAPI.md
    E:\prog\references\RTXDI-main\Doc\NoiseAndBias.md
    E:\prog\references\RTXDI-main\Samples\FullSample
    E:\prog\references\RTXDI-main\Libraries\Rtxdi

The useful RTXDI concepts to preserve are:

- one or a small bounded number of proposals per path vertex
- explicit light sample records
- explicit target PDF or target-function evaluation
- visibility separated from light selection
- selected sample shading with PDF compensation
- path records that can distinguish NEE, emissive hit, environment miss, and
  later BRDF/MIS samples
- pass roles such as initial sampling, temporal reuse, spatial reuse, and final
  shading, even if this task only implements the initial sampled NEE bridge

Licensing Boundary
------------------

Many RTXDI sample and runtime files carry NVIDIA RTX SDK license terms or
`LicenseRef-NvidiaProprietary` headers. Agents must not paste, translate, or
lightly rewrite NVIDIA implementation code into RBDoom3.

Allowed:

- study the pass layout
- study the data-flow concepts
- study what extension points are needed
- implement a native RBDoom3 version from first principles
- cite RTXDI file paths in docs as references

Not allowed:

- copying function bodies
- copying exact struct layouts from sample files
- copying shader helper implementations
- copying comments or prose from NVIDIA source into RBDoom source
- importing broad sample code as a shortcut

Target Architecture
-------------------

Replace the current full selected-light loop with a small sampled NEE path.

Legacy shape:

    for each path vertex:
        for each selected light:
            trace a shadow ray
            accumulate direct lighting

Target first replacement:

    for each path vertex:
        build or load a compact light candidate set
        choose one or a small fixed number of light samples
        compute a target weight / PDF
        trace visibility only for selected samples
        shade using PDF compensation

The first implementation does not need temporal reuse, spatial reuse, ReGIR,
environment map sampling, BRDF/MIS, or full ReSTIR PT. It must leave room for
those later.

Recommended Task Order
----------------------

1. Document and isolate the current mode 18 NEE boundary.

   Do not change behavior yet. Identify exactly where selected lights, Doom
   analytic lights, emissive hits, visibility, and direct shading happen.

2. Extract the NEE/direct-lighting code into a native shader module.

   Create a dedicated include such as `pathtrace_nee.hlsli`. Move the current
   NEE surface setup, selected-light loop, analytic-light hook, source
   categories, and sampled NEE helpers there as behavior-preserving code first.
   The monolithic raygen may still call the module, but new NEE implementation
   work must live behind the module boundary.

3. Add a native bounded sampled NEE mode inside the NEE module.

   Add a development CVar that selects between off, sampled, and legacy full
   loop. Keep the legacy full-loop mode available for comparison.

4. Introduce RTXDI-shaped extension records.

   Add small native structs/helpers for light samples, surface samples, and
   selected NEE results. They should be rich enough for later RTXDI/ReSTIR PT
   callbacks but only need to drive bounded sampled NEE in this task.

5. Validate against the timing matrix.

   The first success criterion is not perfect lighting quality. It is removing
   the secondary-NEE fan-out cliff while preserving plausible direct lighting
   and keeping the old full-loop mode for comparison.

6. Later split the NEE module into a separate shader family or pass.

   Do this only after the native sampled NEE contract is stable. A separate
   pass/pipeline may be needed for compile-time/live-state reduction, but it is
   intentionally not combined with the first sampled-NEE implementation unless
   the user explicitly widens the task.

Non-Goals
---------

Do not do these in the first NEE extraction task:

- do not import NVIDIA sample code
- do not wire full RTXDI or ReSTIR PT
- do not implement temporal or spatial resampling
- do not implement ReGIR or light presampling buffers
- do not redesign material classification
- do not redesign geometry, BVH, or portal walking
- do not compact the primary payload unless strictly necessary
- do not remove legacy debug modes
- do not treat the legacy full selected-light loop as the retained replacement
  path; any inspection of it is historical/debug evidence only
- do not treat secondary NEE disabled as the final solution
- do not scatter new NEE implementation code through `pathtrace_smoke.rt.hlsl`
- do not move NEE to a new C++ pass or shader pipeline in the first sampled
  NEE task unless the user explicitly widens scope

Build And Test Notes
--------------------

Do not use the legacy Debug lane for this work. It is not representative for
PT validation and it conflicts with the current build guidance in
`docs/pathtracing.txt`.

For prepared-folder PT validation, use a preset Release/PT lane. Prefer PT
retail unless the task explicitly needs PT-dev-only diagnostics:

    cd neo
    cmake --preset win64-pt-retail
    cmake --build --preset win64-pt-retail-release

    Copy-Item -LiteralPath E:\prog\rbdoom-3-bfg-rt\build-win64-pt-retail\Release\RBDoom3BFG.exe -Destination E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe -Force

If shaders are touched, copy the updated `renderprogs2` output to the user's
prepared game directory too. The broader build and game-copy workflow is
documented in:

    docs/pathtracing.txt
    docs/pathtrace_modules.txt

If PT-dev-only diagnostics are required, use `win64-pt-dev-release`. Do not add
Debug commands to focused path-tracing task docs.
