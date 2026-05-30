ReSTIR Temporal Compliance Reset
================================

Purpose
-------

This folder supersedes the current RRX reservoir/accumulation task chain for
DI temporal accumulation work.

The current rbdoom RRX accumulator must be treated as a diagnostic hybrid, not
as an RTXDI or RTX Remix compliant implementation. It uses RTXDI structures and
some RTXDI calls, but the working accumulation behavior currently depends on a
debug fallback that feeds the previous temporal reservoir back into initial
sampling as a previous-best candidate.

That fallback is useful evidence only. It is not shippable, and it must not be
used as the active solution.


Hard Diagnosis
--------------

The current active path is not a strict RTX Remix port.

Known non-compliant behavior:

    debug previous-temporal-as-best can be required for visible accumulation
    mode 72 does not reproduce the temporal behavior visible in debug views
    spatial reuse was activated before temporal/final replay was proven
    the page/pass ownership does not yet have proven Remix equivalence
    worker reviews previously accepted scaffold existence as parity

Known useful evidence:

    73/74 can show temporal identity/history in fallback mode
    reservoir storage/readback can survive across frames
    light translation can work in at least some cases
    motion-vector reprojection can work in at least some cases

The task is not to tune the fallback. The task is to rebuild the active DI
temporal path so it works with:

    r_pathTracingRestirPTRrxDebugPreviousTemporalAsBest 0


Authoritative References
------------------------

RTX Remix source root:

    E:/prog/references/dxvk-remix-git

RTXDI source root:

    E:/prog/references/RTXDI-main

Required Remix files:

    src/dxvk/rtx_render/rtx_rtxdi_rayquery.cpp
    src/dxvk/rtx_render/rtx_rtxdi_rayquery.h
    src/dxvk/shaders/rtx/pass/rtxdi/rtxdi_initial_sampling.comp.slang
    src/dxvk/shaders/rtx/pass/rtxdi/rtxdi_temporal_reuse.comp.slang
    src/dxvk/shaders/rtx/pass/rtxdi/rtxdi_spatial_reuse.comp.slang
    src/dxvk/shaders/rtx/pass/rtxdi/rtxdi_compute_gradients.comp.slang
    src/dxvk/shaders/rtx/algorithm/rtxdi/RtxdiApplicationBridge.slangh
    submodules/rtxdi/rtxdi-sdk/include/rtxdi/ResamplingFunctions.slangh

Required RTXDI files:

    Libraries/Rtxdi/Include/Rtxdi/DI/TemporalResampling.hlsli
    Libraries/Rtxdi/Include/Rtxdi/DI/SpatialResampling.hlsli
    Libraries/Rtxdi/Include/Rtxdi/DI/Reservoir.hlsli
    Libraries/Rtxdi/Include/Rtxdi/DI/ReservoirStorage.hlsli

Primary rbdoom files currently involved:

    neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp
    neo/renderer/NVRHI/PathTraceCVars.cpp
    neo/shaders/builtin/pathtracing/pathtrace_restir_combined_resolve.rt.hlsl
    neo/shaders/builtin/pathtracing/pathtrace_restir_local_debug_reservoirs.hlsli
    neo/shaders/builtin/pathtracing/remix_rtxdi/rtxdi_initial_sampling.rt.hlsl
    neo/shaders/builtin/pathtracing/remix_rtxdi/rtxdi_temporal_reuse.rt.hlsl
    neo/shaders/builtin/pathtracing/remix_rtxdi/rtxdi_spatial_reuse.rt.hlsl
    neo/shaders/builtin/pathtracing/remix_bridge/RAB_LightBridge.hlsli
    neo/shaders/builtin/pathtracing/remix_bridge/RAB_LightSamplingBridge.hlsli
    neo/shaders/builtin/pathtracing/remix_bridge/RAB_ReservoirBridge.hlsli
    neo/shaders/builtin/pathtracing/remix_bridge/RAB_SurfaceBridge.hlsli


Read Order
----------

1. docs/restir_temporal_compliance_reset/README.txt
2. docs/restir_temporal_compliance_reset/manager_prompt.txt
3. docs/restir_temporal_compliance_reset/contract_matrix.txt
4. docs/restir_temporal_compliance_reset/worker_protocol.txt
5. docs/restir_temporal_compliance_reset/worker_tasks.txt
6. docs/restir_temporal_compliance_reset/validation_matrix.txt


Non-Negotiable Rules
--------------------

1. No new ReSTIR math.

   Do not invent reservoir weights, target PDFs, MIS terms, history rules,
   visibility rules, temporal thresholds, or spatial thresholds.

2. No fallback promotion.

   The debug previous-temporal-as-best path may remain as a diagnostic switch,
   but it must not be the active solution and must not be used to claim
   temporal compliance.

3. No spatial activation before temporal proof.

   Spatial reuse may be audited or isolated, but it must not be inserted into
   the active output path until the DI temporal pass works without the fallback
   previous-best debug mode.

4. No scaffold parity claims.

   A similarly named file, buffer, cvar, dump, or green debug view is not
   compliance. Compliance requires active producer-to-consumer proof, negative
   path proof, and runtime visual/diagnostic proof.

5. Active mode 72 is the blocker.

   Debug views are useful only if they explain why mode 72 fails. A worker has
   not fixed the blocker until mode 72 can accumulate DI temporally without
   debug previous-best injection.

6. Do not falsely assert another feature is preventing it from working to hide that reservoir and temporal accumulation functions have not been compeleted. 
Multiple agents have been caught in attempts to avoid fixing core reservoir and accumulation functions by deceiving users.

