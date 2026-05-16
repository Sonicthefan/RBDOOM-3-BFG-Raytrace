ReSTIR Reservoir Commissioning Notes
Date: 2026-05-16
Project: rbdoom-3-bfg-rt path tracing / mode-56-derived ReSTIR PT work

Purpose
-------
This folder commissions focused agents for ReSTIR reservoir work.

The current visual problem is that mode-56-derived GI can expose raw or
under-reused reservoir dropout as image radiance. Bright surfaces can contain
many black/dark grey samples, and DLSS/RR treats those samples as important
high-frequency detail.

Important operating rule: visual ReSTIR work must be gated by the user. Agents
are not reliable judges of whether a noisy path-traced image is better. Every
slice in this folder must produce a narrow visual/debug state, build/deploy it,
then stop so the user can inspect it in-game.

Do not commission or execute "Task 01" as one large implementation. It is split
into micro-slices in `task_01_gi_reservoir_reuse.txt`.

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
    docs\ReSTIR\real_pass_structure_plan.txt
    docs\restir_reservoir\task_01_gi_reservoir_reuse.txt
    docs\restir_reservoir\stage_gate_policy.txt
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
  baseline GI reservoir investigation.
- Do not switch mode 56 final resolve to a new GI reservoir path until the user
  has visually accepted diagnostics proving that path is better than the raw GI
  initial reservoir.

Failed Attempt Warning
----------------------
A previous Stage 01 attempt failed because it tried to do the whole stage at
once. Specific mistakes to avoid:

- It added a side GI reservoir buffer, but RTXDI temporal/spatial functions
  still operated on the main reservoir buffer.
- It regenerated an initial reservoir and stored it as "temporal" instead of
  calling true temporal resampling.
- It reused generic spatial helper behavior without proving the GI domain was
  valid.
- It made mode 56 consume the new GI spatial output unconditionally, with no
  visual acceptance and no fallback.

Do not repeat that pattern. If a slice cannot prove its visual output, stop and
report the blocker.

Stage Order
-----------
Stage 01: Baseline GI reservoir investigation and reuse

    This is not one task. It is split into 01A, 01B, 01C, 01D, and 01E. Each
    micro-slice must stop for user visual review.

Stage 02: RR-safe final shading and dense output shaping

    Only after Stage 01E is visually accepted. Refine final shading so RR input
    is fed by a denser, validated lighting estimate rather than raw stochastic
    dropout.

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

Expected Agent Behavior
-----------------------
The agent should work like this:

1. Read the listed docs.
2. Identify the exact commissioned micro-slice.
3. If no micro-slice is named, do only 01A or ask for clarification.
4. Implement only that micro-slice.
5. Build with the PT dev-release lane.
6. Deploy the executable and shaders to the prepared game folder.
7. Report exact files changed, what visual/debug mode to inspect, and what the
   user should compare.
8. Stop.

If a later stage looks tempting, write a note in the final answer. Do not start
it.

