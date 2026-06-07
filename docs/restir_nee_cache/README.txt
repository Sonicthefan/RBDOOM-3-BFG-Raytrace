>>> BINDING CONTRACT AMENDMENT — worker_tasks.txt overrides conflicts here <<<
  A   ONE solid-angle measure: sourcePdf = P_select * solidAnglePdf; resolve
      does NOT re-divide by solidAnglePdf. Selection-only sourcePdf is a bug.
  A.2 cache/fallback is a real mixture pdf (both branches), not the taken branch.
  B   cells WORLD-ANCHORED: no strobe under camera motion. Onion = escape hatch.
  C   validation must prove convergence (cache+fallback -> same accumulated image
      as fallback-only) + emissive stress + movement no-strobe, not just baseline.
  Start task: NEECACHE-09R.
================================================================================

Remix-Style NEE Cache / RLU Spatial Proposal Lane
=================================================

Purpose
-------

This folder defines the rbdoom Remix-style NEE cache and related spatial
proposal work.

The default Remix-aligned goal is to build a NEE cache for GI / secondary
direct-to-light sampling. The cache learns useful emissive triangle/surface
candidates from path/secondary events, then the indirect integrator consumes
those candidates as NEE proposals with explicit PDFs and MIS. It is not the
default primary-DI lighting path.

Primary DI still needs ReGIR-style spatial proposals, but that is a separate
consumer role: clean RTXDI DI may use a lightweight spatial proposal provider
that returns dense RLU light indices and source PDFs, then clean DI owns
RTXDI_StreamSample, temporal/spatial reuse, visibility, and shading. DI must
not consume a cell-owned finished reservoir or a cell-owned final light.

The existing PDFNEE/cache consume path is preserved as an alternate visual
testing mode. On some scenes it can produce very good raw image quality once
settled, so do not delete it. It must remain fenced as experimental and must
not be treated as the default Remix-like architecture until a later task proves
how to stabilize and make it performant.

Runtime note:

    The PDFNEE sourcePolicy 2 lane is a rbdoom research/visual experiment. It
    resembles some current-frame many-light proposal ideas, but RTX Remix does
    not consume its NEE cache as primary DI, and ReSTIR PT / RTXDI do not supply
    the missing stabilization and performance machinery for using this cache as
    production primary DI. Do not continue by trying to force sourcePolicy 2
    into the main DI path.

This is not a continuation of the old PDFNEE/ReGIR verifier routes. Those
routes are diagnostic history only. The current accepted lighting identity is
the RLU dense current index consumed by clean RTXDI DI and by the replacement
PDFNEE current-frame producer.


Design Direction
----------------

Default implementation target:

    fixed-budget Remix-style NEE cache
    camera-centered log/hash world cells
    learned per-cell task values from path/secondary events
    emissive triangle/surface candidates first
    exact source PDF and MIS inputs for every selected candidate
    explicit fallback for empty or invalid cache cells
    indirect/GI consumer boundary, not primary DI replacement
    debug views matching the useful Remix developer views

Primary DI proposal target:

    ReGIR-style spatial proposal provider over the current RLU dense domain
    per-pixel stochastic candidate draw from the mapped cell or nearby cells
    full-RLU fallback mixture with explicit sourcePdf/invSourcePdf
    clean RTXDI DI initial sampling consumes the selected dense RLU identity
    clean DI temporal/spatial accumulation remains unchanged
    no cell-owned final light and no cell-owned finished reservoir

Preserved alternate visual target:

    PDFNEE sourcePolicy 2 / current-frame cache consume remains available for
        image-quality experiments
    it may use the same provider buffers and cache/fallback PDFs
    it must stay opt-in and diagnostic/experimental
    it must not become proof that the Remix-style GI cache or clean DI proposal
        provider is complete
    instability here does not prove the Remix GI/secondary NEE cache failed

Fallback implementation target, only if the DI proposal target fails:

    full ReGIR/onion or bounded grid proposal provider only as a later escape
        hatch after lightweight cache/cell proposal RIS has been attempted
    still selecting dense RLU identities
    still preserving exact source PDF and fallback mixture contracts
    still not owning final direct-light contribution

The cache/grid/proposal provider is only a proposal source. It must not become
a separate lighting path with private RAB math.


First Missing Contract
----------------------

The first concrete implementation contract is the NEE-cache provider ABI.

NEECACHE-01 must define the exact rbdoom-owned shader struct, binding slots,
buffer ownership, lifetime/reset policy, and function boundary for returning:

    selectedDenseRluIndex
    sourcePdf
    invSourcePdf
    sourceLabel
    mixtureProbability
    fallbackReason

No candidate algorithm work may start until that ABI is documented.

Current NEECACHE-01 ABI document:

    provider_abi.txt

Current first missing contract for the experimental PDFNEE visual lane after
NEECACHE-08:

    none for the preserved visual/research path; do not treat sourcePolicy 2
    as production primary-DI proof

Current missing contract for Remix-aligned clean DI proposal integration:

    none for the completed NEECACHE-09 first slice; clean RTXDI DI view 12 has
    validated r_pathTracingCleanRtxdiDiNeeCacheProvider with provider/cell/
    candidate SRVs and fallback to the existing clean initial proposal path

Current missing contract for Remix-aligned secondary/GI consumption:

    none for the completed NEECACHE-10 first slice; the secondary consumer is
    opt-in, current-RLU emissive-only, falls back on stale/empty candidates,
    and has a clean-DI view 8 band 10 diagnostic snapshot path

NEECACHE-02 adds only the standalone cell-mapping debug route. NEECACHE-03
adds a debug-primary-hit task accumulator in task slot 0 for each mapped cell.
NEECACHE-04 populates current-RLU emissive candidate records in the candidate
buffer and exposes debugView 5. NEECACHE-05 adds current-RLU Doom analytic
candidate records and exposes debugViews 6, 7, and 10. NEECACHE-06 writes
PathTraceNeeCacheProviderResults at u74 and exposes debugViews 8 and 9 for the
cache/fallback source and fallback reason. NEECACHE-07 lets the replacement
PDFNEE current producer select that provider in the preserved alternate visual
lane with
r_pathTracingRestirPdfNeeVerifierSourcePolicy 2. PDFNEE binds u74 as SRV t74
for provider readiness/debug parity and binds the fixed candidate list as SRV
t77, then performs the cache/fallback draw per pixel so one preselected cell
result cannot make the cache grid visible. PDFNEE still owns reservoir
construction, visibility, and RAB lighting replay. This is not the default
Remix-style NEE cache consume path. NEECACHE-08 adds multiple RIS proposals per
PDFNEE pixel for that preserved visual/research path. NEECACHE-09 adds the
separate clean RTXDI DI opt-in
r_pathTracingCleanRtxdiDiNeeCacheProvider, binds the NEE cache provider/cell/
candidate buffers as SRV t74/t75/t77 in the clean sentinel, and streams selected
dense current RLU identities through clean DI initial sampling.
NEECACHE-10 adds the opt-in secondary/GI NEE consumer at the ReSTIR PT
path-tracer NEE boundary and a held clean-DI view 8 band 10 candidate-field
diagnostic snapshot for inspection without continuous refresh.

NEECACHE-05/06 candidate data is invalidated on current RLU structural, mapping,
payload, and payload-only changes. Payload-only animation can move or retune
lights without changing dense identity, so persistent cell candidate relevance
must not survive those frames. Dense RLU replay through RAB remains the light
dereference boundary, and both the provider and PDFNEE consumer recompute
current cell weights before selection. Stored candidate weights are build-time
hints, not consume proof.

Reusable analytic-light consumers must additionally honor the RLU stability
classification on each dense Doom analytic payload. The full/current RLU typed
range may contain current-frame-only lights for direct lighting. Persistent
cache, ReGIR-style cells, and temporal-style reuse may write or reuse only
records marked stableCacheable by RLU. Consumers must keep the dense RLU index
as identity and replay through RAB; they must not dereference
DoomAnalyticLights[sourceIndex] or invent local analytic-light stability tests.


Reference Roots
---------------

RTX Remix:

    E:/prog/references/dxvk-remix-git

Primary Remix NEE-cache file-equivalents:

    E:/prog/references/dxvk-remix-git/src/dxvk/rtx_render/rtx_nee_cache.h
    E:/prog/references/dxvk-remix-git/src/dxvk/rtx_render/rtx_nee_cache.cpp
    E:/prog/references/dxvk-remix-git/src/dxvk/rtx_render/rtx_resources.cpp
    E:/prog/references/dxvk-remix-git/src/dxvk/shaders/rtx/algorithm/nee_cache.h
    E:/prog/references/dxvk-remix-git/src/dxvk/shaders/rtx/algorithm/nee_cache_data.h
    E:/prog/references/dxvk-remix-git/src/dxvk/shaders/rtx/algorithm/nee_cache_light.slangh
    E:/prog/references/dxvk-remix-git/src/dxvk/shaders/rtx/algorithm/integrator_indirect.slangh
    E:/prog/references/dxvk-remix-git/src/dxvk/shaders/rtx/pass/nee_cache/update_nee_cache.comp.slang
    E:/prog/references/dxvk-remix-git/src/dxvk/shaders/rtx/pass/integrate/visualize_nee.comp.slang

Remix consume ordering to preserve:

    rtx_context.cpp dispatchPathTracing:
        GBuffer
        RTXDI DI
        NEE cache
        indirect/path integration

    rtx_pathtracer_integrate_indirect.cpp:
        NEE cache is compiled into and consumed by indirect integration.

Do not reinterpret the Remix NEE cache as the primary DI pass.

RTXDI SDK:

    E:/prog/references/RTXDI-main

Primary RTXDI/ReGIR file-equivalents:

    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/ReGIR/ReGIR.h
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/ReGIR/ReGIRParameters.h
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/ReGIR/ReGIRSampling.hlsli
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Source/ReGIR.cpp
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/LightSampling/PresamplingFunctions.hlsli
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/DI/InitialSampling.hlsli

Use these for contract shape. Do not copy source text into rbdoom.


Existing rbdoom ReGIR Module
----------------------------

The existing files are useful scaffolding, not the production solution:

    neo/renderer/NVRHI/PathTraceReGIR.cpp
    neo/renderer/NVRHI/PathTraceReGIR.h
    neo/shaders/builtin/pathtracing/pathtrace_regir_debug.rt.hlsl
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_ReGIR.hlsli

Reusable pieces:

    CVar and dump style
    candidate-buffer allocation pattern
    debug route and binding-set pattern
    RAB replay diagnostic shape
    empty/fallback reason coloring

Do not reuse as the production core:

    axis-wise onion mapping
    debug-view-gated candidate build
    legacy split-domain fallback
    old source PDF assumptions
    private analytic/emissive weighting as final estimator proof


Proposed Module Shape
---------------------

CPU:

    neo/renderer/NVRHI/PathTraceNeeCache.cpp
    neo/renderer/NVRHI/PathTraceNeeCache.h
    neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp
    neo/renderer/NVRHI/PathTraceSmokeResources.cpp
    neo/renderer/NVRHI/PathTracePrimaryPass.h
    neo/renderer/NVRHI/PathTraceCVars.cpp
    neo/renderer/NVRHI/PathTraceCVars.h

Shaders:

    neo/shaders/builtin/pathtracing/pathtrace_nee_cache_debug.rt.hlsl
    neo/shaders/builtin/pathtracing/pathtrace_nee_cache_build.rt.hlsl
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_NeeCache.hlsli
    neo/shaders/builtin/pathtracing/pathtrace_restir_pdf_nee_rlu_current.rt.hlsl

The worker may choose adjacent names only after declaring the exact write set.


Standing Restrictions
---------------------

Do not:

    edit clean RTXDI temporal accumulation
    edit RRX temporal/spatial paths
    enable spatial reuse as proof
    enable best-light feedback as proof
    use denoiser/confidence/gradient paths as proof
    route proof through mode 56
    revive old PDFNEE lightMode 8/9 as the solution
    use Doom portal candidate pools as the default source
    make a second light universe
    suppress broad lights to make candidate views look correct
    claim success from dumps, debug heatmaps, or non-crashing builds alone
    copy Remix or RTXDI source text into rbdoom


Primary Proof
-------------

Standalone proof first:

    NEE cache enabled
    no PDFNEE consume
    no temporal proof
    no spatial reuse
    no best-light feedback
    debug views only from this module

The user must be able to inspect:

    cell id / hash occupancy
    accumulated task value
    emissive triangle candidate map
    analytic light candidate map
    candidate source PDF
    uniform/full-RLU fallback rate
    empty/fallback reason
    selected dense RLU identity
    RAB replay validity

After standalone proof, split work by consumer:

    default Remix-like consumer:
        GI / secondary NEE samples emissive triangle/surface cache candidates
        with explicit PDFs and MIS

    primary DI consumer:
        clean RTXDI DI consumes only dense-RLU proposal identities/PDFs from a
        lightweight ReGIR-style spatial provider, then clean DI owns the
        reservoir and accumulation path

    preserved alternate consumer:
        PDFNEE sourcePolicy 2 may continue as an opt-in visual experiment and
        should remain available for research comparisons


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
    provider_abi.txt
    validation_matrix.txt
    worker_protocol.txt
    worker_tasks.txt
    worker_launch_prompt.txt
