Path-Traced Lighting Universe And ReSTIR Identity Guide
=======================================================

Purpose
-------

This folder is for focused work on replacing the current frame-local Doom
analytic light candidate table with a stable path-tracing light universe.

The immediate bug this is meant to address is ReSTIR PT temporal/spatial NEE
reuse corrupting light identity. With:

    r_pathTracingRestirPTTemporalAnalyticNeeReuse 1

previous-frame NEE reservoirs can be interpreted through the current frame's
compact light array. When animated/strobed Doom lights change active state, or
when a robot/light producer drops out of the current view path, the same packed
light index can refer to a different physical light. The symptom is lights
flickering, turning off, or strobing globally even though current-frame direct
lighting is still sound.

Current Default
---------------

The validated default is:

    r_pathTracingRestirPTTemporalAnalyticNeeReuse 1

The off state still exists as a diagnostic split: it disables previous-frame NEE
light-reservoir reuse while preserving current-frame NEE sampling and non-NEE
temporal reuse. After the robot/strobe endpoint validation, the identity/remap
path and pre-combine stale-NEE rejection are the default ReSTIR PT path rather
than a manual opt-in.

Validated Robot/Strobe Result
-----------------------------

2026-05-14 validation showed that stable light identity/remap alone was not the
full fix. The symptom was:

    stationary analytic lights turned off or flickered while the camera/player
    were stationary

    movement brought the lights back

    a nearby robot/strobe point-light animation temporarily repaired every
    affected light in the area while it was active

The effective fix was to reject bad previous NEE reservoirs before RTXDI
temporal combine. The mode 32/50 wrapper now scans the reprojected 3x3
previous-reservoir neighborhood and sets local temporal max age to zero if a
previous NEE reservoir cannot translate through the current Doom analytic remap
or fails the analytic light-state compatibility guard. This prevents invalid
zero-target or stale-light NEE history from surviving into RTXDI combine and
bias-correction bookkeeping.

Stable identity, previous analytic light records, inverse-shift loading, and
light-state checks are still required infrastructure. They are necessary to make
the pre-combine rejection meaningful, but they were not sufficient by
themselves.

Agent Execution Contract
------------------------

This work is split into Builder, Reviewer, and Feedback roles.

A Builder may implement only the assigned task file. A Reviewer may inspect and
request changes, but must not broaden scope. A Feedback agent must produce
concrete, file-specific next actions, not vague advice.

Universal invariant:

    A compact Doom analytic candidate/proposal index is never a light identity.

Stable identity domain:

    Doom light universe index / stable light key.

Sampling domain:

    Current-frame compact proposal list, candidate list, portal-prioritized
    list, alias list, or GPU-capped list.

Reservoir rule:

    A reservoir may carry a fast current-frame index only if it also carries or
    can translate through stable identity.

    On temporal/spatial reuse, the stable identity, generation, and domain must
    validate before the sample is used.

Forbidden fixes:

    Do not fix the bug by sorting candidates differently.

    Do not fix the bug by preserving zero-radiance slots only in the compact
    candidate list.

    Do not fix the bug by globally resetting history on animated/strobed light
    changes.

    Do not fix the bug by hashing light color/intensity into a scene reset
    signature.

    Do not fix the bug by shrinking the authored light universe to the GPU
    proposal cap.

    Do not reinterpret failed remaps as light index 0.

    Do not default-enable previous-frame analytic NEE reuse until the remap is
    validated. This validation is now complete for the robot/strobe endpoint
    case.

Allowed temporary safety:

    Keep r_pathTracingRestirPTTemporalAnalyticNeeReuse as a runtime diagnostic
    switch. Set it to 0 only to isolate current-frame NEE sampling from previous
    light history.

Review Form
-----------

Use this mandatory form for every agent patch:

    Task reviewed:
    Commit/patch reviewed:
    Files changed:

    Scope check:
        [ ] Patch only touches files allowed by the task.
        [ ] Patch does not silently implement later tasks.
        [ ] Previous-frame analytic NEE reuse remains disabled unless this is Task 03.
        [ ] No Debug build path was introduced.

    Identity check:
        [ ] Compact candidate/proposal index is not used as temporal identity.
        [ ] Universe/stable light ID exists separately from proposal index.
        [ ] Zero-radiance/off/out-of-area/suppressed lights remain addressable or rejectable.
        [ ] Dynamic/projectile lights have generation or conservative invalidation.
        [ ] Unknown entityNumber is not collapsed to 0.
        [ ] Candidate caps do not cap away the authored identity domain.

    Remap check:
        [ ] Previous->current and current->previous remap behavior is defined.
        [ ] Failed remap is explicit and cannot mean valid index 0.
        [ ] Duplicate keys are rejected or conservatively invalidated.
        [ ] Domain tags prevent emissive/environment/Doom analytic collisions.

    Shader/RAB check:
        [ ] RAB_TranslateLightIndex handles Doom analytic lights through remap.
        [ ] RAB_LoadLightInfo validates domain, range, validity/generation, finite data.
        [ ] Invalid loads become empty/rejected lights, not another light.
        [ ] Previous-frame loads access previous-frame light data where required.

    Diagnostics check:
        [ ] Dump includes universe count, proposal count, zero-radiance count, matched remap count, missing remap count, duplicate count, invalid reason counts.
        [ ] Robot endpoint repro can show candidate/proposal count changes without universe identity corruption.
        [ ] Per-light spam is one-shot/capped.

    Validation check:
        [ ] Current-frame PT lighting unchanged for Task 01.
        [ ] Current-frame NEE debug modes still work for Task 02/03.
        [ ] Reuse off/on A/B was run for Task 03.
        [ ] Failed remaps reject reservoirs.

Feedback-Agent Output Format
----------------------------

Feedback agents must respond in this exact format:

    FEEDBACK RESULT
    ===============

    Verdict:
        PASS | NEEDS_CHANGES | BLOCKED

    Primary concern:
        One sentence.

    Required changes:
        1. file:line_or_function - exact required change
        2. file:line_or_function - exact required change

    Identity risk:
        LOW | MEDIUM | HIGH
        Explanation:

    Reuse safety:
        SAFE_TO_KEEP_OFF | SAFE_TO_TEST_ON | UNSAFE
        Explanation:

    Diagnostics evidence:
        - universe count:
        - proposal count:
        - remap valid:
        - remap invalid:
        - zero radiance:
        - duplicate keys:
        - reservoir rejects:

    Forbidden-fix scan:
        [ ] no global history reset fix
        [ ] no candidate-sort-only fix
        [ ] no proposal-cap identity fix
        [ ] no failed-remap-to-zero fix
        [ ] no reuse-on default before validated remap/rejection

    Next action:
        Exact task-local instruction for the builder.

Current Problem
---------------

The current Doom analytic path builds a per-frame candidate list:

    neo/renderer/NVRHI/PathTraceDoomLights.cpp
    neo/renderer/NVRHI/PathTraceDoomLights.h

The shader bridge consumes that table as a RAB light list:

    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightInfoRuntime.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/PathTracer/RAB_PathTracer.hlsli
    neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl

That candidate list is useful for current-frame direct lighting, diagnostics,
and bounded proposal generation. It is not a durable temporal identity space.

Frame-local candidate tables can change because of:

    animated/strobed light material state
    active/inactive zero-radiance frames
    selected area and portal selection
    suppression and visibility policy
    distance/order sorting
    candidate GPU caps
    force-enable debug paths
    view-dependent collection

Temporal/spatial reservoirs must never treat a compact candidate slot as a
light identity.

Guiding Rule
------------

Separate the authored light universe from the sampled proposal list.

The light universe is the stable identity domain. The proposal list is only a
current-frame sampling acceleration structure.

ReSTIR reservoirs should store or translate through a stable light ID. The
shader may also carry a current buffer index as a fast path, but it must be
validated against the stable identity before reuse.

Reference Pattern
-----------------

Use the local references as architecture examples only:

    E:\prog\references\Q2RTX-rayreconstruction-master
    E:\prog\references\RTXDI-main

Q2RTX keeps persistent-ish light polygon records and cluster light lists. The
cluster lists contain indices into the light record table; they are not the
identity themselves. Dynamic/model lights are remapped from previous to current
before ReSTIR reuse.

RTXDI's bridge contract explicitly requires:

    RAB_TranslateLightIndex(lightIndex, currentToPrevious)
    RAB_LoadLightInfo(index, previousFrame)

The FullSample prepares current and previous light buffers, tracks previous
offsets by scene-light identity, and writes a bidirectional mapping buffer.

Do not copy Q2RTX or RTXDI sample source code, comments, struct layouts, shader
helper bodies, or packing choices into this repository. Use them only as an
architecture checklist.

Target Doom-Side Architecture
-----------------------------

The Doom path should evolve toward these layers:

1. Doom light universe ownership

   A PT-owned CPU structure records every Doom renderer/game light that can
   become a path-traced analytic light. This is keyed by stable Doom identity,
   not by current candidate slot.

2. Current light state

   Each universe entry carries current-frame state:

       origin/radius
       color/intensity
       active or zero-radiance state
       selected/suppressed/sampleable state
       portal/area metadata
       generation/validity flags

   Zero-radiance and currently unsampleable lights should remain addressable if
   they might be referenced by previous reservoirs.

3. Previous/current remap

   The universe maintains previous-to-current and current-to-previous mapping
   by stable identity. If a light cannot be mapped, reuse must reject that
   reservoir.

4. Sampling/proposal lists

   Compact current-frame lists can still exist for bounded proposal generation,
   portal-aware sampling, and performance. They should contain universe IDs or
   map back to universe IDs.

5. Shader-visible light records

   The RAB layer should load lights from the stable light domain or use a
   current proposal index validated against identity/generation. `RAB_LoadLightInfo`
   and `RAB_TranslateLightIndex` must match RTXDI's expectations.

6. Reservoir validation

   Reused reservoirs must be rejected when their light:

       no longer maps to the current frame
       maps to a different render light/entity/generation
       is zero radiance for current sampling
       is missing required sample data
       has invalid or non-finite radiance/PDF data

Recommended Task Order
----------------------

1. Add a CPU-side Doom light universe scaffold and diagnostics with no shader
   behavior change.

2. Build stable current/previous light remap data and expose it to the shader
   bridge.

3. Move RAB analytic light loading and NEE recording onto stable light IDs.

4. Re-enable previous-frame NEE reuse behind validation and diagnostic modes.

5. Fold emissive triangle/light-card identity into the same universe policy
   only after Doom analytic identity is proven.

Strict Build And Validation Rule
--------------------------------

Do not use legacy Debug builds for this work.

For ReSTIR/light-universe development, use a documented non-Debug PT lane from
`docs/pathtracing.txt`. The currently useful lane for instrumented PT work is:

    cmake --preset win64-pt-dev
    cmake --build --preset win64-pt-dev-release

Run those commands from:

    E:\prog\rbdoom-3-bfg-rt\neo

Copy the executable to the prepared game folder:

    Copy-Item -LiteralPath E:\prog\rbdoom-3-bfg-rt\build-win64-pt-dev\Release\RBDoom3BFG.exe -Destination E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe -Force

If shader files are touched, also copy the generated path-tracing shader blobs
from:

    E:\prog\rbdoom-3-bfg-rt\base\renderprogs2

to the matching paths under:

    E:\prog\rbdoom-3-BFG-prebuilt\base\renderprogs2

Read First
----------

Before editing code, read:

    docs/pathtracing.txt
    docs/pathtrace_modules.txt
    docs/pathtrace_lighting_universe/README.txt
    docs/pathtrace_lighting_universe/q2rtx_rtxdi_reference_notes.txt
    docs/pathtrace_lighting_universe/restir_light_identity_minimum_contract.txt
    docs/pathtrace_lighting_universe/restir_light_identity_doom_mapping.txt

Important local code:

    neo/renderer/NVRHI/PathTraceDoomLights.cpp
    neo/renderer/NVRHI/PathTraceDoomLights.h
    neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp
    neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp
    neo/renderer/NVRHI/PathTraceSmokeResources.cpp
    neo/renderer/NVRHI/PathTraceReservoirs.cpp
    neo/renderer/NVRHI/PathTraceRestirPT.cpp
    neo/renderer/NVRHI/PathTraceRestirPT.h
    neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl
    neo/shaders/builtin/pathtracing/pathtrace_nee.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightInfoRuntime.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightSamplingCore.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/RAB_LightSample.hlsli
    neo/shaders/builtin/pathtracing/RtxdiBridge/PathTracer/RAB_PathTracer.hlsli

Non-Goals For Early Tasks
-------------------------

Do not redesign the whole path tracer.

Do not replace the current Doom analytic direct-lighting evaluator in Task 01.

Task 01 did not enable previous-frame NEE reservoir reuse. Later validation
promoted reuse-on to the default; keep the off CVar for diagnosis.

Do not fix the bug by globally hashing light color/intensity into the reservoir
scene reset signature. That would wipe useful history on every strobe frame
instead of providing stable identity.

Do not solve this by shrinking the authored light universe to a small cap.
Keep caps on sampled proposals, not on the authored universe.

Do not collapse emissive triangles, Doom analytic lights, and environment lights
into one large rewrite until the Doom analytic path is stable.

Do not stage, commit, or push unless explicitly asked.
