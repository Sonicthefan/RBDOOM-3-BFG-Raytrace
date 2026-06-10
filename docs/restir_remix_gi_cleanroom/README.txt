ReSTIR Remix GI Clean-Room Build
================================

Purpose
-------

This folder defines the clean-room build lane for rbdoom indirect-illumination
(ReSTIR GI) based on RTX Remix behavior. It is the successor lane to
docs/restir_remix_di_cleanroom, which produced the functional DI path
(initial sampling + temporal reuse + spatial reuse + NEE cache feed).

The GI lane reuses the DI lane's rules verbatim unless overridden here:

    clean-room boundary        (see ../restir_remix_di_cleanroom/README.txt)
    worker protocol            (see ../restir_remix_di_cleanroom/worker_protocol.txt)
    io whitelist discipline    (see ../restir_remix_di_cleanroom/io_whitelist.txt)

Clean-room rule:

    The Remix and RTXDI files named by this folder are behavioral/API
    references only. Do not copy files, comments, shader bodies, helper
    functions, struct layouts, or renamed blocks from E:/prog/references into
    rbdoom. Use existing rbdoom helpers and the public RTXDI headers through
    normal includes/calls. If matching Remix requires code that is not already
    expressible through rbdoom data and upstream RTXDI APIs, mark that feature
    deferred instead of transliterating reference source.

Scope of this lane:

    GI initial-sample producer (one indirect bounce ray + shading at hit)
    GI reservoir storage pages
    GI temporal reuse
    GI spatial reuse
    GI final shading into a dedicated indirect output channel
    reservoir-owned debug output for every stage

Deferred (do not build until the baseline above is proven):

    virtual samples
    reflection reprojection
    portal-space sample transforms
    sample stealing into the path tracer
    gradient-based lighting validation (needs functional gradients pipeline)
    pairwise / ray-traced bias correction beyond BASIC
    DLSS-RR compatibility mode
    boiling filter (DI already has one; port only after baseline)
    denoising / NRD-style demodulated outputs
    multibounce beyond the first indirect vertex's NEE estimate


Existing GI Stubs Are Contracts, Not Implementations
----------------------------------------------------

The repo already contains frozen GI contract stubs:

    neo/shaders/builtin/pathtracing/remix_restir_gi/restir_gi_temporal_reuse.rt.hlsl
    neo/shaders/builtin/pathtracing/remix_bridge/RAB_GIReservoirBridge.hlsli
    neo/shaders/builtin/pathtracing/remix_bridge/RAB_GIInitialSampleBridge.hlsli
    neo/shaders/builtin/pathtracing/remix_bridge/RAB_GITemporalValidationBridge.hlsli

These are deliberately non-functional: default callbacks return empty
reservoirs / false, and a deferredFeatureMask records what was skipped.
Workers must implement the REMIX_RAB_* callback layer (the "external
callbacks" / "external bindings" override points) rather than rewriting the
contract files. The reservoir page bridge (RAB_GIReservoirBridge) is already
functional ABI code and is reused as-is.


Primary Ground Truth
--------------------

RTX Remix ReSTIR GI, in the mode where:

    one indirect sample per pixel from the integrator
    GI temporal reuse on, jacobian on, BASIC bias correction
    GI spatial reuse on (can be toggled off for the first proof)
    virtual samples, reflection reprojection, portals, sample stealing,
        lighting validation, DLSS-RR mode all OFF
    final shading writing indirect diffuse radiance only

Human-facing accumulation proof, in order:

    1. temporal off, spatial off: one noisy indirect bounce per pixel
       (color bleeding visible as noise on flat diffuse surfaces)
    2. temporal on: same estimate converging over frames
    3. camera motion: reprojection tracking plus noisy disocclusion
    4. spatial on: faster convergence, no obvious bias or smearing
    5. a known color-bleed scenario (bright colored wall near neutral
       floor) shows the expected bleed

Debug bands, valid-pixel counters, and non-crashing builds are diagnostics
only and are not proof of any of the five steps.


Math Source Decision
--------------------

Use the upstream RTXDI SDK GI headers as the resampling math source, exactly
like the DI lane used the upstream DI headers:

    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/GI/Reservoir.hlsli
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/GI/TemporalResampling.hlsli
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/GI/SpatialResampling.hlsli
    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include/Rtxdi/GI/BoilingFilter.hlsli

Remix itself uses a modified rtxdi-sdk submodule (ReSTIRGI_* names,
avgWeight field, search-radius / backup-pixel temporal parameters). Those
Remix-only extensions are deferred features. The upstream GI headers are
already include-path reachable (RTXDI_RUNTIME_DIR) and match the frozen rbdoom
stub contract, which compiles against RTXDI_GIReservoir and
RTXDI_GITemporalResampling. No ReSTIR math may be changed or re-derived; if
the upstream signature cannot express a Remix behavior, record it as deferred
instead of approximating it.

Calling upstream RTXDI APIs from the configured include path is allowed.
Copying or retyping upstream header implementation into rbdoom files is not.


File Map
--------

    README.txt              this file
    remix_gi_contract.txt   behavioral contract extracted from Remix + RTXDI GI
    di_reuse_map.txt        what is reused from the DI lane vs built new
    cvar_contract.txt       clean-room GI cvar set
    worker_tasks.txt        ordered implementation tasks RGI-00 .. RGI-08
    validation_matrix.txt   required proofs per task
