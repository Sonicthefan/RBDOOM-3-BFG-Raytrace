Remix-Shaped Light Universe Clean-Room Lane
===========================================

Purpose
-------

This folder defines a new rbdoom light-universe lane shaped around RTX Remix's
RTXDI-facing light manager contract.

The immediate reason for this lane is the clean RTXDI view-12 recovery:

    r_pathTracingCleanRtxdiDiBypassLightUniverse 1 accumulates real Doom
    analytic lights, while the older persistent Doom analytic universe does not.

That result isolates the blocker to the light-universe/remap producer contract.
This lane exists to build a first-class replacement instead of continuing to
patch the old authored-light registry.


Core Contract
-------------

The RTXDI-facing light universe must expose the same kind of data Remix exposes:

    dense current light payload buffer
    dense previous light payload buffer
    current-to-previous light index mapping
    previous-to-current light index mapping
    typed light ranges and counts
    explicit invalid mapping for new, despawned, or unmatched lights

In the current rbdoom implementation the active shader-visible light universe
is:

    PathTraceRestirLightManagerCurrentPayload / CleanRtxdiDiRluCurrentLights
        at register(t66)
    PathTraceRestirLightManagerPreviousPayload / CleanRtxdiDiRluPreviousLights
        at register(t67)

These buffers are the active polymorphic light domain.  PathTraceUnifiedLights
at t59/t60 are legacy/debug inputs unless a task explicitly names them.  New DI,
PDFNEE, NEE-cache, or ReGIR work must not build a private replacement light
universe.  It must consume dense RLU indices through RAB loading, sampling, and
translation.

Animated payload changes are allowed. A light that keeps identity may change
color, intensity, radius, or position without invalidating unrelated lights.

Spawned/despawned effect lights must fail locally. They may map invalid until
matched, but they must not poison stable room lights.


Reference Roots
---------------

RTX Remix:

    E:/prog/references/dxvk-remix-git

Primary Remix file-equivalents:

    E:/prog/references/dxvk-remix-git/src/dxvk/rtx_render/rtx_light_manager.cpp
    E:/prog/references/dxvk-remix-git/src/dxvk/rtx_render/rtx_light_manager.h
    E:/prog/references/dxvk-remix-git/src/dxvk/rtx_render/rtx_context.cpp
    E:/prog/references/dxvk-remix-git/src/dxvk/shaders/rtx/pass/common_bindings.slangh
    E:/prog/references/dxvk-remix-git/src/dxvk/shaders/rtx/algorithm/rtxdi/RtxdiApplicationBridge.slangh

RTXDI SDK:

    E:/prog/references/RTXDI-main

Primary RTXDI file-equivalents:

    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/DI/InitialSampling.hlsli
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/DI/TemporalResampling.hlsli
    E:/prog/references/RTXDI-main/Samples/FullSample/Shaders/LightingPasses/RtxdiApplicationBridge/RAB_LightSampling.hlsli

Use these for contract shape. Do not copy source text into rbdoom.


Proposed Module Shape
---------------------

CPU:

    neo/renderer/NVRHI/PathTraceRemixLightManager.cpp
    neo/renderer/NVRHI/PathTraceRemixLightManager.h
    neo/renderer/NVRHI/PathTraceUnifiedLight.cpp
    neo/renderer/NVRHI/PathTraceUnifiedLight.h
    neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp
    neo/renderer/NVRHI/PathTraceSmokeResources.cpp
    neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp
    neo/renderer/NVRHI/PathTraceCVars.cpp
    neo/renderer/NVRHI/PathTraceCVars.h

Shaders:

    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightLoadRuntime.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightSamplingCore.hlsli
    neo/shaders/builtin/pathtracing/cleanroom_rtxdi/pathtrace_clean_rtxdi_di_sentinel.rt.hlsl
    optional new debug shader declared by the worker before editing

The worker may choose adjacent names only after declaring the exact write set.


Standing Restrictions
---------------------

Do not:

    invent ReSTIR math
    change RTXDI reservoir semantics
    enable spatial reuse before temporal proof
    route proof through mode 56
    use previous-temporal-as-best as Remix compliance proof
    use mirror/subview behavior as proof
    use full-map/full-domain brute force as the fix
    suppress broad lights as the fix
    use the NEE cache, ReGIR cells, old Doom analytic arrays, portal candidate
        pools, or split-domain fallbacks as the primary clean DI light universe
    treat debug heatmaps, storage survival, or non-crashing builds as proof
    claim RTXDI/Remix compliance without visible noisy-to-less-noisy real
        Doom analytic accumulation

Previous-best initial-sampling feedback is not part of the early RLU proof.  It
is allowed only in a later clean DI sampling task that explicitly owns the
previous-best input, translates it through RAB_TranslateLightIndex, combines it
with RTXDI reservoir helpers, and labels any previous-temporal-reservoir source
as an approximation rather than full Remix best-light parity.


Primary Proof
-------------

The first proof is not beauty integration, and it is not the final sampling
quality target.

The first proof is:

    clean RTXDI DI view 12
    real Doom analytic lights
    temporal on and off using the same raw flat-diffuse resolve
    no spatial reuse
    no best-light feedback
    no denoiser/confidence/gradient path
    no broad light suppression
    temporal off remains current-only noise
    temporal on visibly reduces noise around the same mean lighting

After that proof is stable, follow-up tasks may improve clean RTXDI DI proposal
quality by sampling the active RLU typed ranges and by adding Remix-style
previous-best seeding.  Those tasks must keep RLU ownership separate from clean
DI reservoir sampling: RLU owns light identity and payloads; clean RTXDI owns
DI proposal sampling and reuse.


Build / Deploy Lane
-------------------

Build from:

    E:/prog/rbdoom-3-bfg-rt/neo

Command:

    cmake --build --preset win64-pt-dev-release

Copy exe:

    E:/prog/rbdoom-3-bfg-rt/build-win64-pt-dev/Release/RBDoom3BFG.exe
    to
    E:/prog/rbdoom-3-BFG-prebuilt/RBDoom3BFG.exe

Copy shaders:

    contents of E:/prog/rbdoom-3-bfg-rt/base/renderprogs2
    to E:/prog/rbdoom-3-BFG-prebuilt/base/renderprogs2

Do not nest renderprogs2 inside renderprogs2.


Document Set
------------

Read all files in this folder before starting:

    README.txt
    contract_matrix.txt
    cvar_contract.txt
    io_whitelist.txt
    integration_handoff_plan.txt
    validation_matrix.txt
    worker_protocol.txt
    worker_tasks.txt
    worker_launch_prompt.txt

Follow-up task notes:

    rlu_07_remix_portal_debug_crosswalk.txt
    rlu_08_active_buffer_ownership.txt
    rlu_09_payload_replay_hardening.txt
    rlu_10_authoritative_range_sample_metadata.txt
    rlu_11_clean_di_range_provider.txt
