ReSTIR Reservoir Commissioning Notes
Date: 2026-05-16
Project: rbdoom-3-bfg-rt path tracing / mode-56-derived ReSTIR PT work

Purpose
-------
This folder commissions focused agents for the next ReSTIR reservoir work.

The immediate image-quality problem is not DLSS Ray Reconstruction itself. The
mode-56-derived path is exposing raw or under-reused reservoir dropout as image
radiance, especially in the GI view. Bright surfaces can show many black/dark
grey samples. DLSS/RR treats those samples as real high-frequency signal.

The near-term goal is to move the current implementation toward the baseline
ReSTIR PT pass structure:

    initial reservoir
        -> temporal reservoir reuse
        -> spatial reservoir reuse
        -> final visibility / validation
        -> final shading
        -> RR-facing radiance buffers

The first commissioned implementation stage must focus on baseline reservoir
reuse. Do not jump ahead into RTX Remix production hardening or ReSTIR PT
Enhanced research features unless the user explicitly commissions that later
stage.

Repository Context
------------------
Repository:

    E:\prog\rbdoom-3-bfg-rt

Prepared game folder:

    E:\prog\rbdoom-3-BFG-prebuilt

Build from:

    E:\prog\rbdoom-3-bfg-rt\neo

Build command:

    cmake --build --preset win64-pt-dev-release

After building, copy:

    E:\prog\rbdoom-3-bfg-rt\build-win64-pt-dev\Release\RBDoom3BFG.exe
        -> E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

After shader changes, copy:

    E:\prog\rbdoom-3-bfg-rt\base\renderprogs2
        -> E:\prog\rbdoom-3-BFG-prebuilt\base\renderprogs2

Read First
----------
Before changing code, read:

    docs\pathtracing.txt
    docs\pathtrace_modules.txt
    docs\ReSTIR\current_restir_pt_state.txt
    docs\ReSTIR\rough_restir_pt_preview_plan.txt
    docs\ReSTIR\real_pass_structure_plan.txt
    docs\restir_reservoir\task_01_gi_reservoir_reuse.txt
    docs\restir_reservoir\code_sketches.txt

Reference codebases are local:

    E:\prog\references\RTXDI-main
    E:\prog\references\dxvk-remix-git
    E:\prog\references\Q2RTX-rayreconstruction-master
    E:\prog\references\lin2026restirptenhanced.pdf

Use local references for understanding only. Do not copy/paste licensed source
code from reference projects into this repository. Translate concepts into
branch-local code that fits the existing RAB bridge and shader style.

Standing Constraints
--------------------
- Keep mode 18 stable/playable behavior unchanged unless explicitly asked.
- Do not use Debug builds for real RT/PT validation.
- Do not stage or commit docs\pathtracing_handoff.txt unless explicitly asked.
- Do not shrink the authored Doom light universe to hide performance problems.
  Bound proposals per shading point instead.
- Do not merge ReSTIR debug libraries back into the core mode-18 path.
- Do not treat mode 56 as a finished production renderer. It is a staging and
  validation path.
- Do not add RTX Remix hardening, ReSTIR PT Enhanced features, unified
  reservoirs, reflection reservoirs, or denoiser rewrites in the same slice as
  the baseline GI reservoir reuse work.
- Stop after the commissioned stage is implemented, built, deployed, and
  summarized. Do not continue into the next stage without a new user request.

Stage Order
-----------
Stage 01: Baseline GI reservoir reuse

    Make the GI contribution consumed by mode 56 come from a completed GI
    temporal/spatial reservoir path instead of directly shading the initial GI
    reservoir. This is the first task in this folder.

Stage 02: RR-safe final shading and dense output shaping

    After Stage 01 is accepted, refine final shading so RR input is fed by a
    denser, validated lighting estimate rather than raw stochastic dropout.
    Keep diffuse/specular and hit-distance output requirements explicit.

Stage 03: RTX Remix-style production hardening

    Only after the baseline pipeline works: adaptive history, sample stealing,
    larger spatial recovery while history is weak, final visibility invalidation,
    boiling/firefly controls, and RR compatibility behavior.

Stage 04: Unified DI/GI PT reservoir direction

    Only after the split DI/GI reservoir path is functioning and measurable:
    evaluate moving direct lighting into the same PT reservoir stream as GI, as
    suggested by newer ReSTIR PT research.

Stage 05: ReSTIR PT Enhanced techniques

    Duplication maps/adaptive temporal cap, vector-valued shading weights,
    paired spatial reuse, footprint reconnection diagnostics, and dual motion
    vector disocclusion support. These are not Stage 01 features.

Current Root Symptom
--------------------
The current mode-56-derived resolve path still has a GI path that loads the
initial reservoir and directly turns it into radiance. This is the key behavior
to remove from the final RR-facing path.

Relevant current shader location:

    neo\shaders\builtin\pathtracing\pathtrace_restir_combined_resolve.rt.hlsl

Look for:

    LoadRestirPTInitialReservoir(pixel)

That load is acceptable for diagnostics, but not as the production GI estimate
fed into RR.

Expected Agent Behavior
-----------------------
The agent should work like this:

1. Read the listed docs.
2. Inspect the current code and confirm the active mode-56 pass route.
3. Implement only the commissioned stage.
4. Build with the PT dev-release lane.
5. Deploy the executable and shaders to the prepared game folder.
6. Report exact files changed, validation performed, and remaining limits.
7. Stop.

If a later stage looks tempting, write a note in the final answer. Do not start
it.

