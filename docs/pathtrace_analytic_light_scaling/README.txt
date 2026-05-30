Analytic Light Scaling And ReSTIR-Ready Sampling Guide
=====================================================

Purpose
-------

This folder is for focused work on making large Doom analytic-light counts
performant in the path tracer.

Doom 3 is intentionally light-heavy. The runtime goal is not to make a
full-loop direct-light pass slightly cheaper. The runtime goal is to stop
looping every analytic light at every path vertex, convert analytic lights into
true sampleable records, and prepare those records for ReSTIR/PT reuse.

The guiding rule is:

    bounded sampled proposals, not bounded authored-light universe

Do not solve performance by pretending most authored Doom lights do not exist.
Keep many lights available as persistent light records, then build a
sampleable distribution that lets the shader evaluate one or a small bounded
number of proposals per vertex.

Current Problem
---------------

The extracted NEE shader module improved code organization but did not reduce
the main analytic-light work:

    EvaluateSmokeMode18NeeDirectLighting
        -> EvaluateDoomAnalyticSphereLightsForSurface
            -> loop all uploaded/capped Doom analytic lights
                -> usually evaluate one shadow ray per accepted light

This still happens after selected-light sampling. Therefore secondary and
reflection NEE still pay full analytic-light fanout. That is why analytic-heavy
scenes showed little or no performance gain after the first NEE extraction.

The next performance step must change the algorithm:

    old:
        for each path vertex:
            for each analytic light:
                evaluate + maybe trace visibility

    target:
        for each path vertex:
            choose a small bounded number of analytic-light candidates
            evaluate only those candidates
            trace visibility only for selected candidates
            keep the full loop as an explicit reference/debug mode

Architecture Layers
-------------------

The long-term analytic-light path should be split into three layers:

1. Light universe

   Persistent Doom light records with stable identity, area/portal membership,
   active state, shape/radius/type, color/intensity, shadow flags, and any
   special-case flags needed by Doom materials or scripted lights.

2. Sampling distribution

   Per-frame, per-area, or per-portal candidate sets, weighted lists, or alias
   tables. This layer is where many authored lights become cheap to sample
   without disappearing from the renderer.

3. NEE/ReSTIR sample record

   A sampled analytic light must carry source identity, sampled point or
   direction, proposal PDF, target weight/PDF, visibility policy/result, and
   replay metadata. Mode 18 NEE can consume this now; ReSTIR/PT should later
   reuse and resample the same kind of proposal over time and space.

Temporary upload caps are allowed as safety limits while the branch is
experimental, but they are not the lighting strategy. The strategy is a broad
eligible light universe plus bounded proposal evaluation.

Strict Build And Validation Rule
--------------------------------

Do not use legacy Debug builds for this work.

Do not copy, paste, revive, or recommend legacy commands that build from the
old `build` directory with the Debug configuration, and do not copy an
executable from a Debug output folder.

For normal validation, use PT retail:

    cmake --preset win64-pt-retail
    cmake --build --preset win64-pt-retail-release

Copy the PT retail executable to the prepared game folder:

    Copy-Item -LiteralPath E:\prog\rbdoom-3-bfg-rt\build-win64-pt-retail\Release\RBDoom3BFG.exe -Destination E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe -Force

If shader code changes, also build/copy the generated shader outputs from the
same PT retail build tree and source tree. At minimum, confirm the following
files are refreshed in the prepared game folder when `pathtrace_smoke.rt.hlsl`
or included pathtracing HLSL files change:

    E:\prog\rbdoom-3-BFG-prebuilt\base\renderprogs2\dxil\builtin\pathtracing\pathtrace_smoke.rt.bin
    E:\prog\rbdoom-3-BFG-prebuilt\base\renderprogs2\spirv\builtin\pathtracing\pathtrace_smoke.rt.bin

Useful copy commands:

    Copy-Item -LiteralPath E:\prog\rbdoom-3-bfg-rt\base\renderprogs2\dxil\builtin\pathtracing\pathtrace_smoke.rt.bin -Destination E:\prog\rbdoom-3-BFG-prebuilt\base\renderprogs2\dxil\builtin\pathtracing\pathtrace_smoke.rt.bin -Force
    Copy-Item -LiteralPath E:\prog\rbdoom-3-bfg-rt\base\renderprogs2\spirv\builtin\pathtracing\pathtrace_smoke.rt.bin -Destination E:\prog\rbdoom-3-BFG-prebuilt\base\renderprogs2\spirv\builtin\pathtracing\pathtrace_smoke.rt.bin -Force

If PT-dev-only diagnostic CVars or logging are required, use the Release PT dev
lane instead:

    cmake --preset win64-pt-dev
    cmake --build --preset win64-pt-dev-release

Use PT dev only for diagnostics. PT retail is the default validation lane for
this folder.

Read First
----------

Before editing code, read:

    docs/pathtracing.txt
    docs/pathtrace_modules.txt
    docs/pathtrace_nee_extraction/README.txt
    docs/pathtrace_analytic_light_scaling/README.txt

Important code files:

    neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl
    neo/shaders/builtin/pathtracing/pathtrace_nee.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightSampling.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightInfo.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightSample.hlsli
    neo/renderer/NVRHI/PathTraceDoomLights.cpp
    neo/renderer/NVRHI/PathTraceDoomLights.h
    neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp
    neo/renderer/NVRHI/PathTraceCVars.cpp
    neo/renderer/NVRHI/PathTraceCVars.h

Local reference material:

    E:\prog\references\RTXDI-main\Doc\RestirPT.md
    E:\prog\references\RTXDI-main\Doc\ShaderAPI.md
    E:\prog\references\RTXDI-main\Samples\FullSample
    E:\prog\references\RTXDI-main\Libraries\Rtxdi

The NVIDIA material is a reference only. Do not copy or lightly rewrite NVIDIA
sample/runtime code into this repository.

Recommended Task Order
----------------------

1. Lock the current analytic-light NEE contract.

   Document the exact current behavior, modes, CVars, shader calls, and cost
   points. Preserve the legacy full-loop analytic-light path as a reference
   mode.

2. Add a bounded sampled analytic-light mode for secondary/reflection NEE.

   `r_pathTracingSecondaryAnalyticNeeMode 0` must skip secondary analytic NEE.
   `r_pathTracingSecondaryAnalyticNeeMode 1` samples a small bounded number of
   analytic lights instead of looping all of them. The first implementation uses
   `r_pathTracingSecondaryAnalyticNeeSamples` uniform proposals from the current
   uploaded analytic candidate buffer and compensates the discrete light-selection
   PDF. The default is sampled mode with four proposals after runtime testing
   showed large performance gains without obvious image loss. `2` keeps the
   legacy full analytic-light loop as the reference/debug mode. Primary direct
   lighting can continue using the full loop until sampled primary lighting is
   validated. The bounded value is the number of proposals evaluated per vertex,
   not the total number of authored lights kept in the universe.

3. Build a better analytic-light candidate distribution.

   Start with a simple weighted global or portal-walk-local distribution.
   Then add area/portal-aware lists so large light counts scale with Doom's
   spatial structure rather than full-scene fanout. The distribution should
   represent the broad eligible light set; it should not discard most lights
   just to make the shader loop short.

4. Make the analytic-light sample record ReSTIR-ready.

   Store valid source IDs, PDFs, target weights, and replay-safe metadata.
   Separate legacy full-loop contribution fields from true sampled-light fields
   so future reservoir/RAB code cannot double-apply PDFs. The current native NEE
   contract exposes `ValidateSmokeDoomAnalyticLightSampleForReplay`,
   `EvaluateSmokeDoomAnalyticLightSampleForSurface`, and
   `SmokeNeeAnalyticTargetWeight`; those helpers validate source type/index,
   render-light identity, Doom entity identity, positive PDFs, sample distance,
   and the combined `lightSelectionPdf * solidAnglePdf` before replay/final
   shading. Analytic samples currently mark solid-angle-scaled local radiance with
   `SMOKE_NEE_SAMPLE_FLAG_ANALYTIC_SOLID_ANGLE_SCALED`, so future reservoir
   code must not treat `radiance` as an unscaled physical source value or divide
   by `solidAnglePdf` a second time.

5. Hand the stable sample contract to the ReSTIR/PT bridge.

   ReSTIR/PT should own reservoirs, temporal reuse, spatial reuse, ping-pong
   resources, rejection, final selected-sample shading, and history validation.
   The NEE module should only build/evaluate samples and expose clean PDFs and
   target weights. The RAB light bridge mirrors the same rule with
   `RAB_IsReplayableLightSample` before target-PDF and conservative-visibility
   callbacks consume a selected light sample. Its path-tracer NEE probe uses a
   local RIS selector for multiple analytic trials instead of keeping the
   highest-scoring trial while reporting the raw uniform light PDF. RAB Doom
   analytic sampling also honors the same Doom influence radius and quadratic
   falloff as mode 18 native analytic NEE.

Non-Goals
---------

Do not do these in this focused sequence:

- do not use Debug builds for validation
- do not remove the legacy full analytic-light loop
- do not cap the authored analytic-light universe to a tiny number as the
  long-term performance solution
- do not convert all primary direct lighting to sampled mode in the first task
- do not wire full temporal/spatial ReSTIR in the NEE task
- do not import or copy NVIDIA sample code
- do not redesign material classification
- do not redesign the geometry universe
- do not redesign portal walking, except to consume existing portal-walk light
  membership data for candidate selection
- do not treat shader include extraction as a performance win by itself
- do not merge this with payload compaction unless the user explicitly widens
  scope

Success Criteria
----------------

- Secondary/reflection analytic NEE no longer loops every uploaded analytic
  light by default.
- The legacy full-loop analytic mode is not a retained replacement path; any
  inspection of it is historical/debug evidence only.
- Sampled analytic mode has bounded proposal and visibility-ray counts while
  the eligible authored-light universe remains broad.
- The sample record makes PDF/weight semantics explicit and cannot be mistaken
  for a reservoir-ready record when it is still legacy-scaled.
- PT retail validation uses copied executable and copied renderprogs2 shader
  outputs when shaders changed.
- Nsight or timing logs show analytic-light work scaling with sample count, not
  total uploaded light count, for the sampled secondary mode.
