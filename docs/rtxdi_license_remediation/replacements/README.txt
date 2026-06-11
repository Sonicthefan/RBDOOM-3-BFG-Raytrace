Replacement Staging Area
========================

Finished rbdoom-owned replacement code, staged here un-wired so weaker
agents can do the mechanical integration. Nothing in this folder is built.

Files
-----

RandomSamplerState.hlsli
    Replaces Rtxdi/Utils/RandomSamplerState.hlsli (plan Step 4). The math
    is final; do not re-derive it during integration. White-noise core is
    the PCG output hash (Jarzynski & Olano 2020); blue-noise path is mask
    lookup + per-dimension R2 toroidal shift + Cranley-Patterson rotation.

PathTraceBlueNoise.h / .cpp
    C++ stub that owns the blue-noise mask Texture2DArray. Only
    LoadMaskBlob (engine VFS) and the resource/binding hookup are
    unfinished; marked TODO(integration).

GISpatialResampling.hlsli
    Replaces Rtxdi/GI/SpatialResampling.hlsli (plan Step 7). Math is
    final; do not re-derive during integration. Signature-compatible with
    the call site in pathtrace_clean_restir_gi.rt.hlsl. Three bias modes
    via sparams.biasCorrectionMode: 1/M (OFF), 1/Z (BASIC, matches the
    current lane default), and PAIRWISE (enhanced; balance-heuristic
    pairwise MIS, normalization proof in the file). Two neighbor modes:
    the existing disc-offset buffer (default), or reciprocal reuse
    textures (define RBPT_GI_SPATIAL_RECIPROCAL + the RBPT_GI_REUSE_*
    macros; ReSTIR PT Enhanced Sec. 3). Sources: Ouyang 2021 (jacobian,
    1/Z), GRIS course notes (weights), ReSTIR PT Enhanced CC BY 4.0
    (reciprocal selection). The NVIDIA header was not opened.

PathTraceReuseTextureGen.cpp
    CPU generator for the reciprocal reuse textures (paper Sec. 3.1:
    paired link indices + tiled 2x2 shuffles, alternating diagonal
    offset). Iteration count uses the paper's Eq. 3 closed form
    (verified against the published PDF: floor(sigma^2/2 + 1.46/sigma
    + 1.76/sigma^2 + 0.656/sigma^3 + 0.5)); the measured stddev is
    returned as a cross-check (assert achievedSigma ~ targetSigma at the
    call site). Also provides the per-frame transform bits consumed by
    RBPT_GIReuseNeighborDelta.

Integration checklist (mechanical)
----------------------------------

1.  Move RandomSamplerState.hlsli to
    neo/shaders/builtin/pathtracing/cleanroom_common/ and repoint the one
    include in RtxdiBridge/RAB_RandomSamplerState.hlsli plus every shader
    that includes "Rtxdi/Utils/RandomSamplerState.hlsli" directly
    (list: docs/rtxdi_license_remediation/impacted_files.txt).
    The RTXDI_* function names are kept so call sites need no edits.
    Caution: the state struct gained fields beyond (seed, index); it still
    works round-tripped through two uints, but blue noise is disabled on
    that path (see step 5).

2.  Build without RBPT_ENABLE_BLUE_NOISE first. This is a pure white-noise
    drop-in; all lanes must compile and run. Image will differ slightly
    from the NVIDIA RNG (different sequences) - that is expected and fine.
    Verify no banding/correlation artifacts in the DI/GI noisy debug views.

3.  Move PathTraceBlueNoise.h/.cpp to neo/renderer/NVRHI/, add to CMake,
    finish LoadMaskBlob with the engine file API, Init/Shutdown next to the
    other PT global resources, bind GetTexture() to slot t_RbptBlueNoise
    (pick a real register/space; t127 in the hlsli is a placeholder) in the
    binding layouts of the clean DI and clean GI passes.

4.  Add cvar r_pathTracingBlueNoise (default 0) in PathTraceCVars.cpp and
    compile the clean-lane shaders in a second permutation with
    -D RBPT_ENABLE_BLUE_NOISE (or a static define once validated).
    Mask data: a raw blob (layers*128*128 bytes, layer-major, R8) at
    base/textures/bluenoise/stbn_scalar_128x128x64.raw. Converting the
    NVIDIA STBN scalar set to that layout is fine for LOCAL TESTING ONLY -
    record it in impacted_files.txt as swap-before-release; the long-term
    plan is an own void-and-cluster generator emitting the same blob.

5.  Optional follow-up: widen RAB_RandomSamplerState (RtxdiBridge/
    RAB_RandomSamplerState.hlsli) to carry pixel/frame/pass through instead
    of just (seed,index), so resampling code that round-trips through the
    RAB wrapper also gets blue noise. Until then those paths silently use
    white noise, which is correct, just not upgraded.

GI spatial integration checklist (mechanical)
---------------------------------------------

a.  Baseline swap: repoint the #include "Rtxdi/GI/SpatialResampling.hlsli"
    in pathtrace_clean_restir_gi.rt.hlsl at the new file. The existing
    call site compiles unchanged (RTXDI_NEIGHBOR_OFFSETS_BUFFER and
    RAB_ClampSamplePositionIntoView are already defined above the
    include). Provide RBPT_GI_CAMERA_ORIGIN (current camera world pos)
    or override RBPT_GI_SURFACE_LINEAR_DEPTH with a lane accessor.
    sparams gained jacobianCutoff: leave 0 for the default (10.0).
    Keep biasCorrectionMode at BASIC - output should match the old
    header's quality closely (not bit-exact; RNG consumption differs).

b.  Pairwise mode: set biasCorrectionMode = RTXDI_BIAS_CORRECTION_PAIRWISE
    behind a cvar (suggest extending r_pathTracingCleanRestirGiBiasCorrection
    range). No other wiring. Expect slightly less energy loss at
    depth/normal discontinuities than BASIC.

c.  Reciprocal neighbors (optional, after a+b prove out): run
    PathTraceReuseTextureGen at startup for k textures (sizes
    254/230/210/186, targetSigma matched to the lane's current radii -
    start sigma ~40 as the stand-in for the 85/200 px disc radii and
    tune), upload as R16G16_SINT, bind, define
    RBPT_GI_SPATIAL_RECIPROCAL, RBPT_GI_REUSE_DELTA, RBPT_GI_REUSE_SIZE,
    RBPT_GI_REUSE_TRANSFORM_BITS (new uint constant per frame from
    MakeFrameTransformBits). Note: this lands the paper's correlation/
    symmetry benefits but NOT its 50% shift-cost saving - that needs
    pair-shared dispatch (one thread computes both pixels of a pair and
    writes both outputs), which is a later perf task and barely matters
    for GI's ray-free reconnection shifts anyway.

Validation
----------

    A/B with the cvar: temporal+spatial OFF, look at the raw 1-spp DI and
    GI debug views. Blue noise ON should show visibly more even noise
    (no clumps/voids) at identical brightness. Any brightness shift means
    a histogram bug - stop and report, do not retune exposure.
    Let temporal accumulate 64+ frames static: both modes must converge to
    the same image. Divergence means a [0,1) range bug.

    GI spatial: with the swapped header at BASIC, A/B against the old
    build (the private oracle) - converged static images must match in
    brightness; spatial on vs off must not shift energy, only variance.
    PAIRWISE vs BASIC: same converged image; any systematic dimming
    points at the canonical MIS accounting (pairsSeen path) - report,
    do not rescale. Reciprocal mode: converged image again identical;
    per-frame transform bits frozen to 0 must produce visible static
    correlation patterns (proof the pairing works), re-randomized per
    frame they must disappear.
