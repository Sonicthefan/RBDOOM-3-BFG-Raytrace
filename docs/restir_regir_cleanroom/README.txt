Standalone ReGIR Clean-Room Lane
================================

Purpose
-------

This folder defines a standalone rbdoom ReGIR-style candidate-cache lane.

The goal is not to connect ReGIR to PDFNEE, clean RTXDI, RRX, temporal reuse,
or mode 56 immediately. The first goal is to prove that rbdoom can build and
visualize a stable spatial light-candidate cache:

    world-space cells behave coherently while the camera moves
    the grid center and cell size are explicit and not silently overridden
    cells populate from the current RAB light universe
    selected analytic and emissive light identities are visible and stable
    source PDF / inverse source PDF is explicit
    selected slots are stable while the camera, lights, and map state are stable
    current-frame mean contribution does not show unexplained hard cell seams
    empty and fallback cells are loud

Only after those proofs pass may a later PDFNEE task consume ReGIR as an
optional source distribution.


Boundary
--------

ReGIR is both CPU and GPU work.

CPU owns:

    CVars and parameters
    resource allocation
    constant upload
    dispatch scheduling
    debug dump text
    optional future portal-derived bounds/center hints

GPU owns:

    world-position to cell mapping
    ReGIR build / presampling dispatch
    candidate cache writes
    current-pixel candidate selection from a cell
    debug visualization of cells, occupancy, identity, and PDFs

ReGIR must not own:

    final direct-light contribution
    temporal accumulation
    spatial reuse
    best-light feedback
    denoiser/confidence/gradient paths
    RRX reservoir pages
    old rbdoom NEE records
    light identity continuity that already belongs to r_pathTracingRemixLightUniverse
    current/previous light remap tables that duplicate the Remix Light Universe
    active fallback light domains outside the current Remix/RAB light universe


Default-Functionality Rule
--------------------------

Future ReGIR work must be a normal module first and a debug surface second.

Default CVars must mean default functionality:

    one enable CVar builds the cache with production-intended defaults
    default light source is the current Remix/RAB light universe
    default parameters are usable for the current Doom scale
    default output exposes the module's intended candidate-cache behavior
    no extra debug CVar sequence is required before cells populate

Additional CVars are diagnostic only. They may select cell overlays, dumps,
forced domains, manual centers, or validation views, but they must not be
required to make the cache populate or to make selected lights replay through
RAB.

A task fails if the normal path requires a multi-CVar recipe, if enabling the
module only shows a diagnostic/status view, if useful behavior exists only in
one debug view, or if default behavior deletes lights without an explicit
source-distribution contract and acceptance proof.


Reference Roots
---------------

RTXDI SDK:

    E:/prog/references/RTXDI-main

Reference file-equivalents:

    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/ReGIR/ReGIR.h
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/ReGIR/ReGIRParameters.h
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/ReGIR/ReGIRSampling.hlsli
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Source/ReGIR.cpp
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/LightSampling/PresamplingFunctions.hlsli
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/LightSampling/LocalLightSelection.hlsli
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/LightSampling/RISBuffer.hlsli
    E:/prog/references/RTXDI-main/Samples/FullSample/Shaders/LightingPasses/Presampling/PresampleReGIR.hlsl

Use these for contract shape. Do not copy source text into rbdoom.


Proposed Module Shape
---------------------

CPU:

    neo/renderer/NVRHI/PathTraceReGIR.cpp
    neo/renderer/NVRHI/PathTraceReGIR.h
    neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp
    neo/renderer/NVRHI/PathTraceSmokeResources.cpp
    neo/renderer/NVRHI/PathTracePrimaryPass.h
    neo/renderer/NVRHI/PathTraceCVars.cpp
    neo/renderer/NVRHI/PathTraceCVars.h

Shaders:

    neo/shaders/builtin/pathtracing/pathtrace_regir_debug.rt.hlsl
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_ReGIR.hlsli

The worker may choose adjacent names, but must declare the exact write set
before editing.


Standing Restrictions
---------------------

Do not:

    connect ReGIR to PDFNEE before REGIR-00 through REGIR-06 pass
    connect ReGIR to PDFNEE while selected slots change from frameIndex alone
    connect ReGIR to PDFNEE with a one-slot consumer as the final proof
    hide PDFNEE-consumer parameter overrides from the ReGIR dump
    edit the clean RTXDI temporal accumulator
    edit RRX temporal/spatial paths
    route proof through debugMode 56
    enable spatial reuse
    enable best-light sampling
    use denoiser/confidence/gradient paths as proof
    use fallback albedo/beauty
    make ReGIR a second light universe
    fall back to DoomAnalyticLights, SmokeEmissiveTriangles, or the purged
        legacy light manager in active ReGIR source views
    suppress broad lights to make cells look correct
    treat Doom portals as the ReGIR estimator
    claim success from dumps or non-crashing builds alone


Primary Proof
-------------

The first proof is visual and standalone:

    ReGIR debug route enabled
    no temporal accumulation
    no spatial reuse
    no PDFNEE consume
    no mode 56
    raw debug views only from the ReGIR module

The user must be able to inspect:

    cell shape / cell id
    cell stability while moving
    cell occupancy
    selected analytic light identity
    selected emissive identity
    sourcePdf / invSourcePdf
    fallback / empty-cell reason
    stable slot-selection policy
    cell-local mean contribution before any stochastic single-slot consumer

Standalone ReGIR is not accepted if it only proves that a cell can return some
light. It must also prove that the cell field is stable enough to be used as a
current-frame PDFNEE source distribution. Stationary surfaces must not flicker
because the consumer selects a different candidate slot from frameIndex alone.
Hard boundaries in a mean-contribution view are a failing signal unless the
worker can prove they are only the explicit cell debug overlay.


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
    validation_matrix.txt
    worker_protocol.txt
    worker_tasks.txt
    worker_launch_prompt.txt
