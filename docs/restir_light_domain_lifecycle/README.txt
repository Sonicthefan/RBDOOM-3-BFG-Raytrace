ReSTIR Light-Domain Lifecycle Repair
====================================

Purpose
-------

This folder defines a repair lane for rbdoom's ReSTIR light-domain lifecycle
and bridge ownership before the PDF + NEE verifier or temporal accumulator are
connected to the full game light set.

The suspected failure is not ReSTIR math. The suspected failure is that
emissive and Doom analytic light domains are being merged or persisted with
stale previous-frame/map state, so a once-working analytic path can be
contaminated by emissive-only state and stop receiving fresh lights as the
player moves.

This lane proves that the light domains are truthful:

    current counts are correct
    previous counts are correct only when valid
    remaps describe the exact final current domain
    map and render-world changes clear stale state
    analytics remain valid when emissives are present but disabled


Boundary
--------

This task is upstream of PDF + NEE and temporal accumulation.

The lanes are:

    1. light-domain lifecycle repair
        Reset ownership, producer isolation, remap ordering, and unified bridge
        contamination checks.

    2. PDF + NEE verifier
        Current-frame direct-light estimator proof using a truthful light
        domain. Do not use it to repair lifecycle state.

    3. clean temporal accumulator / integration
        Feed a verified current-frame domain and estimator into temporal.

Do not change reservoir math, NEE math, temporal reuse, spatial reuse, best
lights, or denoising in this lane.


Known Failure Hypotheses
------------------------

H-01: Partial scene reset on map/render-world change

    PathTraceSmokeSceneBuild.cpp detects renderWorldChanged || mapChanged, but
    its inline clear is not equivalent to ResetRayTracingSmokeSceneResources.
    It can leave m_sceneInputs, previous emissive state, retired packages, and
    previous buffers/counts alive.

H-02: Doom analytic universe reset keyed only on renderWorld pointer

    PathTraceDoomLights.cpp resets the global Doom analytic universe only when
    the renderWorld pointer changes. If the same renderWorld object survives a
    map transition, persistent authored analytic state can survive too.

H-03: Emissive remap built before final emissive domain

    The current path builds previousEmissiveTriangles and emissiveLightRemap,
    then later mutates/replaces emissiveTriangles through the light universe.
    A manager can then receive a remap that describes an older list than the
    one actually uploaded.

H-04: Unified bridge contamination

    The final RAB source can consume a combined emissive + analytic payload
    domain. If emissive current/previous/remap state is stale, analytic ranges,
    offsets, and budgets can be affected even when the analytic producer still
    generates valid lights.


Reference Roots
---------------

RTX Remix:

    E:/prog/references/dxvk-remix-git

RTXDI SDK:

    E:/prog/references/RTXDI-main


Reference File-Equivalents
--------------------------

Use Remix for light-manager lifecycle and pass/domain shape, not copied source:

    E:/prog/references/dxvk-remix-git/src/dxvk/rtx_render/rtx_light_manager.cpp
    E:/prog/references/dxvk-remix-git/src/dxvk/rtx_render/rtx_light_manager.h
    E:/prog/references/dxvk-remix-git/src/dxvk/shaders/rtx/pass/rtxdi/rtxdi_initial_sampling.comp.slang

Use RTXDI for domain/PDF expectations when a task reaches estimator handoff:

    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/DI/InitialSampling.hlsli
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/DI/Reservoir.hlsli


Required rbdoom Files
---------------------

Workers must read the relevant files before implementation:

    neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp
    neo/renderer/NVRHI/PathTraceSmokeResources.cpp
    neo/renderer/NVRHI/PathTraceDoomLights.cpp
    neo/renderer/NVRHI/PathTraceRestirLightManager.cpp
    neo/renderer/NVRHI/PathTraceRemixLightManager.cpp
    neo/renderer/NVRHI/PathTraceUnifiedLight.cpp
    neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp
    neo/renderer/NVRHI/PathTraceSceneInputs.h


Core Rule
---------

Do not repair this by changing estimator math or suppressing lights.

The repair target is the producer-to-consumer light domain:

    raw producer records
    final current domain
    previous domain validity
    current-to-previous remap
    active payload buffer
    active range offsets/counts
    shader-visible counts and controls

If a worker cannot prove those are coherent, they must stop before touching
PDF + NEE or temporal code.


Standing Restrictions
---------------------

Do not:

    edit reservoir math
    edit PDF + NEE math
    edit temporal accumulation
    edit spatial reuse
    edit best-light sampling
    edit denoiser/confidence/gradient paths
    use fallback albedo or beauty output as proof
    use mode-56 RRX behavior as lifecycle proof
    use broad light suppression as a fix
    hide stale state by clearing every frame
    claim success from dumps, green bands, buffer survival, or non-crashing
        builds alone


Primary Proof
-------------

The proof is domain truth, followed by visual behavior:

    analytic-only works
    emissive-only works
    analytics still work when emissives are present but sampled at zero
    enabling emissive sampling does not freeze analytic updates
    map/area transitions do not inherit stale previous-map lights

The worker must document:

    raw current counts
    final current counts
    previous counts
    remap valid/invalid counts
    active range offsets/counts
    payload count match/mismatch
    first-frame-after-map-change previous-domain state


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
    to
    E:/prog/rbdoom-3-BFG-prebuilt/base/renderprogs2

Do not nest renderprogs2 inside renderprogs2.


Document Set
------------

Read all files in this folder before starting:

    README.txt
    contract_matrix.txt
    cvar_contract.txt
    io_whitelist.txt
    validation_matrix.txt
    worker_protocol.txt
    worker_tasks.txt
    worker_launch_prompt.txt
