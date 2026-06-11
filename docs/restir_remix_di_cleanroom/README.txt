ReSTIR Remix DI Clean-Room Rebuild
==================================

Purpose
-------

This folder defines a clean-room rebuild lane for rbdoom direct-illumination
ReSTIR based on RTX Remix behavior.

The current RRX/synthetic reservoir path is contaminated by fallback output,
synthetic selectors, synthetic reservoir construction, display ramps, and
debug-only previous-temporal-as-best behavior. Do not repair that path as the
active solution.

This lane rebuilds the first Remix RTXDI DI stages as a new isolated rbdoom
path:

    raw DI initial sampling
    DI temporal reuse
    reservoir-owned debug output

Raw DI initial sampling is mandatory. It is the clean-room path's
NEE-equivalent direct-light sampling stage. Workers must not omit it, replace
it with old rbdoom NEE records, or jump directly to temporal reuse.

Deferred:

    spatial reuse
    best-light sampling
    gradients
    confidence
    denoising
    final beauty integration


Clean-Room Rule
---------------

Workers may inspect RTX Remix and RTXDI SDK sources to understand contracts,
resource flow, pass order, feature toggles, and expected behavior. For license
remediation work that replaces reservoir or resampling math, the proprietary
RTXDI implementation headers must remain closed; derive replacement math from
the public ReSTIR papers/course notes and rbdoom's own contracts instead.

Workers must not copy NVIDIA source code text into rbdoom.

Workers must implement rbdoom-owned code from the documented contract and from
rbdoom's own surface/light/material/visibility APIs.

No ReSTIR math may be changed. If a worker cannot express the required math
without copying source text, it must stop and report the missing adapter or
contract instead of inventing a formula.

License status, 2026-06-11:

    The current implemented clean-room DI lane violates the intended ownership
    boundary by including proprietary NVIDIA RTXDI runtime headers from:

        E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include

    That dependency supplies the DI reservoir layout/storage, initial sampling,
    temporal reuse, spatial reuse, parameter structs, and utility math. Those
    pieces are NVIDIA implementation code, not rbdoom-owned clean-room math.
    Until docs/rtxdi_license_remediation is complete, builds containing this
    lane and regenerated renderprogs2 blobs must not be distributed as
    GPL-clean rbdoom artifacts.


Primary Ground Truth
--------------------

Use Portal RTX / RTX Remix as the behavioral ground truth because it can be run
with only raw DI and temporal accumulation enabled.

Target behavior:

    flat or textured primary surfaces
    raw DI initial sampling
    DI temporal reuse
    spatial disabled
    best-light sampling disabled
    denoiser/confidence disabled

This is the minimum useful baseline. Spatial and best lights are quality/perf
work after this baseline is proven.

The human-facing accumulation proof is a raw flat-diffuse direct-light view.
Once the clean-room path can sample analytic lights or emissive lights, it must
show:

    temporal off: one sampled light candidate per pixel, noisy direct lighting
    temporal on: the same lighting estimate becoming less noisy over frames
    camera motion: temporal tracking/reprojection behavior and noisy
        disocclusion where history is invalid

Status colors, reservoir field bands, dumps, green valid pixels, and
non-crashing builds are diagnostics only. They must not be reported as proof of
functional temporal accumulation.


Reference Roots
---------------

RTX Remix:

    E:/prog/references/dxvk-remix-git

RTXDI SDK:

    E:/prog/references/RTXDI-main


Required Reading
----------------

Read these files before starting any worker task:

    docs/restir_remix_di_cleanroom/README.txt
    docs/restir_remix_di_cleanroom/remix_contract.txt
    docs/restir_remix_di_cleanroom/cvar_contract.txt
    docs/restir_remix_di_cleanroom/io_whitelist.txt
    docs/restir_remix_di_cleanroom/worker_protocol.txt
    docs/restir_remix_di_cleanroom/worker_tasks.txt
    docs/restir_remix_di_cleanroom/validation_matrix.txt


Standing Restrictions
---------------------

Do not:

    reuse the current synthetic primary patch path as proof
    reuse previous-temporal-as-best as the active solution
    activate clean-room DI through r_pathTracingDebugMode 56
    overload r_pathTracingRestirPTDiDebugView for clean-room proof
    route proof output through mode-56 fallback lighting
    use albedo or fallback scene lighting in reservoir proof views
    enable spatial before temporal proof
    enable best lights before temporal proof
    change reservoir math, targetPdf math, weight math, or history math
    add luminance averaging or history brightness ramps
    suppress broad classes of lights to make a proof pass
    claim compliance from names, buffer survival, green views, or no crash
    claim temporal accumulation from status/debug displays
    hide accumulation proof behind a dashboard that cannot show noisy lighting
        becoming less noisy
    omit the clean-room DI initial / NEE-equivalent producer


First Milestone
---------------

Milestone M1 is not beauty output.

M1 succeeds only when rbdoom has a new clean-room Remix-shaped DI initial plus
temporal path whose raw flat-diffuse validation output:

    owns every pixel
    uses analytic lights first
    uses one sampled light candidate per pixel for temporal-off validation
    shows noisy current-only direct lighting when temporal is disabled
    shows the same lighting becoming less noisy when temporal is enabled
    tracks/reprojects while moving, with current-only noise at disocclusions
    accumulates temporally with spatial and best lights disabled
    turns invalid when light input is disabled
    is controlled by r_pathTracingCleanRtxdiDi* CVars
    never falls back to albedo or normal scene lighting

Reservoir/status debug output is still required for diagnosing failures, but it
is not sufficient for the M1 accumulation claim.
