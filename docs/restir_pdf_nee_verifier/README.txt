ReSTIR PDF + NEE Verifier
=========================

Purpose
-------

This folder defines a standalone verification lane for rbdoom's ReSTIR direct
illumination estimator before it is connected to any known-good temporal
accumulator.

Clean RTXDI DI view 12 consuming the Remix Light Universe (RLU) has proven
that the current RLU producer and clean temporal consumer can function on real
Doom lighting. Replacement PDF/NEE work is for the separate question:

    can rbdoom produce a correct current-frame PDF + NEE / direct-light
    reservoir estimate from the current RLU dense domain and hand it to clean
    RTXDI DI?

Do not use temporal accumulation as evidence for this task. Do not use status
colors, green bands, dumps, or buffer survival as evidence. The proof is raw
flat-diffuse one-sample direct lighting with a documented estimator chain.


Boundary
--------

This verifier is standalone.

It must not be built inside the clean temporal accumulator proof as the first
step. The lanes are:

    1. clean accumulator sandbox
        Known-good real Doom temporal proof through RLU. Do not edit for this
        task.

    2. PDF + NEE verifier
        Current-frame estimator proof. Temporal off.

    3. integration mode
        Verified estimator feeding the known-good accumulator.

The old PDFNEE/NEE attempts are discarded as implementation routes. They were
built before the current working RLU path existed and must not be kept,
extended, or reactivated for this replacement. New work must start from the RLU
dense domain and produce either useful current-frame lighting or a
clean-compatible RTXDI DI current reservoir page with default functionality.

If the verifier fails, the bug is in light-domain/PDF/NEE/replay. If the
verifier passes but integration fails, the bug is in the handoff to temporal.


Default-Functionality Rule
--------------------------

Future PDF/NEE work must be built as a normal module, not as a puzzle of
debug switches.

Default CVars must mean default functionality:

    one enable CVar activates the module's intended current-frame estimator
    default domain/source choices are the production path
    default source PDF is full-domain uniform over current RLU dense count
    default output is the useful lighting/reservoir result
    the module works without requiring a sequence of extra diagnostic CVars

Additional CVars are diagnostic only. They may select views, dumps, forced
test domains, or validation bands, but they must not be required to make the
basic estimator function.

A task fails if its runtime proof requires five or more CVars to assemble the
normal code path, if default output is only a heatmap/status color, or if the
worker describes debug routing as the module's primary functionality.


Reference Roots
---------------

RTX Remix:

    E:/prog/references/dxvk-remix-git

RTXDI SDK:

    E:/prog/references/RTXDI-main


Required rbdoom Bridge Files
----------------------------

Workers must read these before implementation:

    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightLoadRuntime.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightSamplingCore.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightTarget.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_Visibility.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/PathTracer/RAB_PathTracerNee.hlsli
    neo/renderer/NVRHI/PathTraceRemixLightManager.cpp
    neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp
    neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp


Reference Math Files
--------------------

Use these for contract shape:

    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/DI/InitialSampling.hlsli
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/DI/Reservoir.hlsli

Use Remix only for pass/order/behavior shape, not copied source text:

    E:/prog/references/dxvk-remix-git/src/dxvk/shaders/rtx/pass/rtxdi/rtxdi_initial_sampling.comp.slang


Core Rule
---------

The verifier must use or exactly match the existing rbdoom RAB bridge:

    RAB_LoadActiveRrxLightInfo
    RAB_SampleActiveRrxPolymorphicLight
    RAB_GetLightSampleTargetPdfForSurface
    RAB_GetReflectedBsdfRadianceForSurface
    RAB_GetCandidateNeeVisibility or equivalent explicit visibility policy

Local duplicate replacements are not acceptable unless the worker proves exact
equivalence line-by-line against the shared RAB helper contract.

First-source policy:

    proposal domain = dense current RLU lightIndex range
        [0, currentRluLightCount)
    sourcePdf = 1.0 / currentRluLightCount
    invSourcePdf = currentRluLightCount
    selected reservoir identity = dense current RLU lightIndex

The first producer must not use payload sourcePdf/sourceWeight as its proposal
PDF. Doom analytic payload sourcePdf can be zero. Typed-range uniform,
bounded per-range, payload-weighted, portal, alias-table, and ReGIR policies
are deferred until a separate source PDF proof exists.


What Lives Where
----------------

CPU light universe / manager code owns:

    light payloads
    current and previous domains
    identity and remap
    source weights
    emissive source PDF values when available
    Doom analytic radiance/radius/influence payloads

Shader RAB bridge owns:

    light loading into RAB_LightInfo
    per-light sample replay
    solidAnglePdf
    targetPdf
    reflected BSDF radiance
    visibility
    final NEE-style contribution

Do not expect the CPU light manager to contain a complete PDF + NEE solution.
The viable estimator is the producer-consumer chain across CPU payloads and
shader RAB callbacks.


Standing Restrictions
---------------------

Do not:

    edit the known-good clean RTXDI DI / RLU temporal path
    enable temporal reuse in verifier proof views
    enable spatial reuse
    enable best-light sampling
    enable denoiser/confidence/gradient paths
    route proof through mode 56 or r_pathTracingRestirPTDiDebugView
    use old rbdoom NEE records as the proof output
    treat old PDFNEE/NEE routes as implementation paths
    use albedo, fallback beauty, or fallback direct lighting
    suppress broad light ranges to hide overlap problems
    claim success from names, dumps, green bands, or non-crashing builds
    treat SmokeOutput or PathTraceSmokeReservoir buffers as the clean
        accumulator handoff


Primary Proof
-------------

For each test rung:

    temporal off
    spatial off
    denoiser off
    one frame / current-frame estimator only
    raw flat diffuse direct-light output or clean current reservoir page
    no fallback albedo
    no beauty path underneath
    no old NEE contribution output

The worker must document:

    lightSelectionPdf
    solidAnglePdf
    targetPdf
    reservoir weight / inverse PDF
    reflectedRadiance
    finalContribution

For overlapping lights, the source PDF must sum to 1.0 across the active
proposal domain, or the worker must stop and report the missing proposal
contract.

For integration, the proof must additionally show that the selected light
identity, sample UV, targetPdf, sourcePdf, M, and reservoir weight are written
as an RTXDI_PackedDIReservoir current page before temporal reuse is enabled.


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
    integration_handoff_plan.txt
    current_frame_producer_design.txt
    rlu_02_implementation_notes.txt
    validation_matrix.txt
    worker_protocol.txt
    worker_tasks.txt
    worker_launch_prompt.txt
