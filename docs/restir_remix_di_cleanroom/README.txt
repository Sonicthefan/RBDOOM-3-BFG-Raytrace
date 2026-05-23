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
resource flow, pass order, feature toggles, and expected behavior.

Workers must not copy NVIDIA source code text into rbdoom.

Workers must implement rbdoom-owned code from the documented contract and from
rbdoom's own surface/light/material/visibility APIs.

No ReSTIR math may be changed. If a worker cannot express the required math
without copying source text, it must stop and report the missing adapter or
contract instead of inventing a formula.


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
    omit the clean-room DI initial / NEE-equivalent producer


First Milestone
---------------

Milestone M1 is not beauty output.

M1 succeeds only when rbdoom has a new clean-room Remix-shaped DI initial plus
temporal path whose reservoir-only debug output:

    owns every pixel
    shows explicit invalid colors
    shows current-only vs temporal-reused pixels
    accumulates temporally with spatial and best lights disabled
    turns current-only/noisy when temporal is disabled
    turns invalid when light input is disabled
    is controlled by r_pathTracingCleanRtxdiDi* CVars
    never falls back to albedo or normal scene lighting
