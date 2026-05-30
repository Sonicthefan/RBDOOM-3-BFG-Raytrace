ReSTIR Light Manager Pivot
==========================

Purpose
-------

This folder now drives a pivot away from patching rbdoom's existing light
universe toward a light-only RTX Remix-shaped manager contract.

The goal is not to invent a new ReSTIR algorithm. The goal is to make rbdoom
produce the same kind of light-domain contract that RTXDI expects and RTX Remix
uses for hostile game light streams:

    Doom / emissive observations
        -> Doom-aware adapter classification
        -> manager-owned current light domain
        -> manager-owned previous light domain
        -> explicit current-to-previous map
        -> explicit previous-to-current map
        -> RAB_LoadLightInfo / RAB_TranslateLightIndex
        -> ReSTIR

The useful Doom-specific logic from the old path may be carried forward as
adapter code. The old rbdoom light universe must not define the new manager
identity domain.

Strategic direction:

    Do not spend major effort making the old rbdoom reservoir/light chain
    correct. Use it to identify failure modes and to keep legacy comparison
    outputs alive. The accepted path is a new RTX Remix-compliant chain grown
    outward from PathTraceRestirLightManager:

        manager-owned light domain
        manager-owned current/previous maps
        manager-owned RAB route
        RTXDI/RTX Remix-shaped temporal validation
        final ReSTIR consumers

    Each layer should be rebuilt to match the reference contract before the
    next layer is connected.

    Before building the next layer, inspect the RTX Remix light manager and the
    immediate module it feeds. The rbdoom manager must output the same contract
    shape expected by that consumer, so downstream work can follow RTXDI/Remix
    math and avoid local policy inventions.

Initial scope:

    Do not try to match the entire RTX Remix lighting stack at once. The first
    viable target is basic diffuse DI/GI with a sane ReSTIR output that can feed
    DLSS Ray Reconstruction without broken history. Secondary features can be
    added back after this baseline is stable.

    Temporal reprojection/reuse is the critical path. User reference testing
    showed RTX Remix's ReSTIR temporal path doing most of the visible stability
    work, more than DLSS RR itself. If rbdoom does not achieve temporal
    reprojection behavior on par with Remix for the basic diffuse DI/GI target,
    the ReSTIR chain is not considered working.

    Use the RTX Remix temporal reuse viewer as the practical visual benchmark.
    Under rapid or erratic camera movement, the reference temporal view keeps
    most of the frame functional instead of collapsing to invalid history. A
    rbdoom temporal debug view that loses most of the frame under similar motion
    is a hard failure even if the final denoised image sometimes hides it.


Current State
-------------

The previous LM-01 through LM-08 sequence produced useful scaffolding:

    PathTraceRestirLightManager.*
    uploaded current/previous/map debug buffers
    debug view for map status
    opt-in RAB routing through r_pathTracingRestirLightManagerRAB
    a partial structural/mapping/payload signature split

That work is a starting point only. LM-08 was not the final design. It mostly
removes animated payload data from a reservoir clear signature; it does not
rebuild the light universe in the RTX Remix shape.

New agents must not continue by "tuning LM-08." Use the LU-RM scaffold slices
or the LU-BR bridge slices in implementation_slices.txt. LU-BR is the current
route for analyzing and rebuilding the LightManager -> RtxContext/RAB ->
RTXDI/ReSTIR temporal bridge.

Important recovered evidence:

    docs/restir_light_manager/session_recovery_dynamic_analytic_bug_2026_05_19.txt

records the lost-session proof that halting dynamic light updates stopped the
accumulation/reservoir issue. It also records the incorrect emergency workaround
that treated animated Doom analytic payload changes as remap incompatibility.
Do not reintroduce that workaround as the fix.

Reference debugging note:

    Portal RTX / RTX Remix contains extensive runtime debug toggles for the
    light, RTXDI, ReSTIR GI, gradient, denoiser, and feature-validation paths.
    Use those toggles as empirical guidance for priority. If a Remix feature can
    be disabled with little impact, it can be deferred. If disabling it causes
    the same class of breakage rbdoom has, treat that feature as an early
    required contract layer.


Read Order
----------

1. docs/restir_light_manager/README.txt
2. docs/restir_light_manager/reference_contract.txt
3. docs/restir_light_manager/local_rbdoom_mapping.txt
4. docs/restir_light_manager/implementation_slices.txt
5. docs/restir_light_manager/worker_protocol.txt
6. docs/restir_light_manager/worker_prompts.txt
7. docs/restir_friendly/light_universe_heavy_rebuild_from_references.txt


Reference Roots
---------------

RTXDI:

    E:/prog/references/RTXDI-main

RTX Remix:

    E:/prog/references/dxvk-remix-git

rbdoom:

    E:/prog/rbdoom-3-bfg-rt

Runtime folder:

    E:/prog/rbdoom-3-BFG-prebuilt


Build And Deploy
----------------

Build from:

    E:/prog/rbdoom-3-bfg-rt/neo

Build command:

    cmake --build --preset win64-pt-dev-release

Deploy exe:

    copy E:/prog/rbdoom-3-bfg-rt/build-win64-pt-dev/Release/RBDoom3BFG.exe
    to E:/prog/rbdoom-3-BFG-prebuilt/RBDoom3BFG.exe

Deploy shaders after shader changes:

    copy the contents of E:/prog/rbdoom-3-bfg-rt/base/renderprogs2
    to E:/prog/rbdoom-3-BFG-prebuilt/base/renderprogs2

Do not create:

    E:/prog/rbdoom-3-BFG-prebuilt/base/renderprogs2/renderprogs2


Hard Rules
----------

1. Use RTXDI and RTX Remix as structural references only.

   Do not copy/paste licensed source. Match the output contract and dataflow
   shape with rbdoom-native code.

2. Do not change ReSTIR math.

   Reservoir weights, target PDFs, MIS terms, visibility rules, temporal
   thresholds, spatial thresholds, finalization, and RTXDI helper logic are out
   of scope.

3. Preserve legacy split outputs during the pivot.

   Keep existing emissive, Doom analytic, and unified buffers alive until the
   manager path has debug visibility and user acceptance.

4. Doom code is an adapter, not the manager.

   Doom entity/lightDef/renderLight identity, portal diagnostics, temporary
   light rejection, and emissive identity are input classification. They must
   feed the manager contract; they must not become ad hoc ReSTIR policies.

5. Animated payload changes are not structural resets.

   Flickering radiance, color, intensity, radius, position, and material
   emission changes may change shading and target evaluation. They must not
   force a global reservoir clear and must not by themselves invalidate a stable
   light mapping.

6. Workers get one small slice only.

   Do not ask a worker to "make it like Remix." Give the worker exactly one
   LU-RM or LU-BR slice and exact owned files.

7. Reviews require active-flow proof.

   A manager-shaped class, current/previous maps, plausible dump values, raw
   reservoir storage, or a non-crashing build are not compliance proof. A
   worker or reviewer must show:

       active consumer proof:
           exact file/line evidence that the new output is read by the next
           active rbdoom module, shader binding, RAB callback, or dispatch path

       negative-path proof:
           exact behavior when mapping/identity fails. For light/reservoir
           work this means local candidate/sample rejection, not a broad
           reservoir clear

       runtime diagnostic proof:
           diagnostics separating storage survival, mapping failure, payload
           churn, global clear, and inactive consumer state

   If those proofs are missing, the slice is diagnostic-only or incomplete even
   if the code shape resembles RTX Remix.
