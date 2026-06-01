RBDoom3 BFG RT/PT CPU Work Pipeline Plan
========================================

Purpose
-------

This document defines the code-only lane for moving PT scene/BVH preparation
out of the render-thread monolith without changing visible rendering behavior.
The first deliverable is not a broad BVH rewrite. It is a reusable CPU work
pipeline with a headless validation harness so agents can prove non-graphical
changes without launching the full game.

Use this for the `codex/code-only-worker` branch or an isolated worktree based
on that branch. Keep shader, estimator, and visual-debug work in the interactive
graphics lane.

Branch And Workspace Rules
--------------------------

- Work in a separate checkout such as:

      E:\prog\rbdoom-3-bfg-rt-code-worker

- Use branch:

      codex/code-only-worker

- Use a build directory inside that checkout, not the active graphics checkout.
- Do not copy executables or shaders into `E:\prog\rbdoom-3-BFG-prebuilt`
  unless explicitly asked.
- Do not edit active ReSTIR/graphics task docs as part of this lane.

Current BVH Location Map
------------------------

These are current code anchors for the first extraction. Treat line numbers as
orientation only; function names and ownership boundaries matter more once the
worker starts editing.

Main orchestration:

- `neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp`
  - `PathTracePrimaryPass::BuildRayTracingSmokeTestScene` starts near line 1447.
  - The BLAS/TLAS build flow is still embedded in this function.

Embedded acceleration-flow anchors:

- static BLAS signature/cache decision:
  `PathTraceSmokeSceneBuild.cpp`, near line 3859,
  `ComputeSmokeStaticBlasSignature`.
- static BLAS creation:
  `PathTraceSmokeSceneBuild.cpp`, near line 3913,
  `CreateSmokeBlas`.
- dynamic BLAS creation:
  `PathTraceSmokeSceneBuild.cpp`, near line 3940,
  `CreateSmokeBlas`.
- buffer upload batch:
  `PathTraceSmokeSceneBuild.cpp`, near lines 4084 and 4088,
  `UploadSmokeAccelerationBuffers`.
- rigid TLAS instance collection:
  `PathTraceSmokeSceneBuild.cpp`, near line 4163,
  `BuildRigidTlasInstanceDescs`.
- BLAS/TLAS submit:
  `PathTraceSmokeSceneBuild.cpp`, near lines 4192 and 4196,
  `SubmitSmokeAccelerationBuilds`.

TLAS allocation and persistent resource ownership:

- `neo/renderer/NVRHI/PathTraceSmokeResources.cpp`
  - `m_smokeTlas` is created near line 1031.
  - committed scene-package/resource handoff touches the same handles later in
    this file.
- `neo/renderer/NVRHI/PathTracePrimaryPass.h`
  - persistent acceleration fields are near line 192:
    `m_smokeStaticBlasDesc`, `m_smokeDynamicBlasDesc`,
    `m_smokeStaticBlas`, `m_smokeDynamicBlas`, and `m_smokeTlas`.
  - static BLAS cache counters/signature are near lines 85-88.

Rigid BLAS/TLAS side path:

- `neo/renderer/NVRHI/PathTraceGeometryUniverse.h`
  - rigid scaffold and TLAS planning declarations are near lines 570-591:
    `BuildRigidBlasPlanStats`, `UpdateRigidBlasGpuScaffold`,
    `BuildRigidTlasPlanStats`, and `BuildRigidTlasInstanceDescs`.
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.cpp`
  - rigid BLAS scaffold creation currently calls `CreateSmokeBlas` near line
    2325.
  - rigid TLAS instance creation starts at
    `RtSmokeGeometryUniverse::BuildRigidTlasInstanceDescs`, near line 3260.

High-Level Architecture
-----------------------

The durable pattern is:

1. Render thread captures an immutable frame snapshot.
2. Worker thread computes a plain CPU result.
3. Render thread accepts or rejects the result by generation tokens.
4. Render thread performs all NVRHI resource creation, uploads, barriers, and
   acceleration-structure command submission.

The first implementation should allow a synchronous diagnostic path and an
asynchronous worker path, but the asynchronous path must be allowed to miss a
frame. Do not block normal frames on the worker in the first version except for
an explicit diagnostic CVar or headless test mode.

Frame Snapshot Contract
-----------------------

Create a small immutable snapshot type for PT CPU work. It should contain only
owned values or lifetime-proven stable references. Do not let raw live
`viewDef`, `drawSurf`, `idRenderEntity`, mutable universe records, or transient
render-thread pointers cross worker boundaries unless the lifetime is proven and
documented at the field.

For the first BVH planning slice, the snapshot should capture enough data for:

- static and dynamic vertex/index spans or copied vectors,
- static/dynamic/rigid geometry ranges,
- material IDs or material classes needed by the plan,
- scene origin,
- static cache generation/signature inputs,
- rigid instance observations when the TLAS route plan needs them.

The snapshot must carry validity tokens:

- frame index,
- scene generation,
- map or render-world generation if available,
- geometry-universe generation,
- material-universe generation,
- light/RLU generation only when the worker result depends on light data.

CPU Job Result Contract
-----------------------

Worker output must be CPU data only. It may include:

- static BLAS signature,
- static cache hit/miss decision,
- static/dynamic BLAS build plan metadata,
- TLAS instance descriptor plan,
- rigid TLAS route plan,
- buffer upload-item plan metadata,
- diagnostics and timings.

Worker output must not include:

- NVRHI command recording,
- command-list access,
- GPU resource creation or destruction,
- binding-set creation,
- visible shader behavior changes.

Render-Thread Submit Boundary
-----------------------------

The render thread remains the sole owner of GPU-facing work unless a later task
proves a narrower NVRHI thread-safety contract.

Keep these on the render thread:

- NVRHI buffer creation or reuse,
- `writeBuffer`,
- resource state transitions and barriers,
- BLAS/TLAS object creation,
- `BuildBottomLevelAccelStruct`,
- `buildTopLevelAccelStruct`,
- binding-set and descriptor-table creation or mutation.

`PathTraceAcceleration.cpp` should remain the GPU-facing acceleration helper
layer. New CPU-only planning helpers should live beside it rather than moving
NVRHI submission code into worker-owned modules.

Lifetime Model
--------------

Use a small current/previous/pending model:

- Render thread publishes snapshot N.
- Worker computes result N.
- Render thread consumes the latest completed result whose generation tokens
  still match.
- If the result is late, reuse the previous valid result or run the existing
  synchronous path.
- If the result is stale, reject it and increment a stale-result counter.

The first version should be conservative. A missed async result should cost
performance opportunity, not correctness.

Diagnostics Contract
--------------------

Add one reusable timing/report object for this lane. It should be cheap in
normal gameplay and detailed only under an explicit dump/diagnostic toggle.

Track at least:

- snapshot capture ms,
- worker queue wait ms,
- worker execution ms,
- render submit ms,
- accepted result count,
- rejected stale result count,
- late result count,
- sync fallback count,
- current snapshot/result generation fields.

Do not add high-frequency console spam. Use one-shot or throttled reports.

Headless Harness Requirement
----------------------------

The first useful test target should not launch the game or create a GPU device.
It should exercise CPU-only snapshot/planning functions with deterministic
synthetic inputs.

Minimum useful harness cases:

- static BLAS signature is stable for identical inputs,
- signature changes when relevant vertex/index/material/class data changes,
- static cache hit/miss decision matches the old synchronous logic,
- TLAS plan emits the expected base static/dynamic instances,
- rigid-route plan includes valid observations and rejects missing/stale ones,
- stale result tokens are rejected by the submit-boundary acceptor,
- late results fall back without blocking the frame path.

The harness may be a small CMake executable or an engine command-line mode, but
prefer a small executable if it can link only the needed CPU modules. Avoid
pulling in renderer/device initialization for the first validation pass.

Good First BVH Scope
--------------------

Move only CPU planning first:

- static BLAS signature computation,
- static cache hit/miss decision,
- dynamic/static BLAS create descriptor planning,
- TLAS instance descriptor list construction,
- rigid TLAS route planning,
- upload-item plan metadata.

Leave the following untouched except for adapting to consume the result:

- NVRHI buffer creation,
- NVRHI acceleration-structure creation,
- buffer uploads,
- BLAS/TLAS GPU build command submission,
- binding-set creation,
- shader resources and shader code.

Recommended Module Shape
------------------------

Add narrowly scoped CPU modules:

- `PathTraceCpuWork.h/.cpp`
  - worker snapshot/result envelope,
  - generation tokens,
  - timing/report structs,
  - stale-result accept/reject helpers,
  - small wrapper over the existing engine job system if used.

- `PathTraceAccelerationPlan.h/.cpp`
  - CPU-only BVH/AS planning structs and functions,
  - static BLAS signature and cache-decision helpers,
  - TLAS instance plan builder,
  - upload plan metadata builder.

Keep:

- `PathTraceAcceleration.h/.cpp`
  - NVRHI BLAS/TLAS creation,
  - uploads,
  - barriers,
  - GPU build submission.

`PathTraceGeometryUniverse` remains single-owner mutable state. Workers may
consume immutable snapshots derived from it, but should not mutate universe
records, lookup maps, or backing vectors.

First Worker Task
-----------------

Task 01: Headless PT CPU planning harness and BVH planning boundary.

Goal:

- Establish a CPU-only snapshot/result/acceptance boundary for acceleration
  planning.
- Add a headless test or harness that can run without launching the game.
- Prove old-vs-new parity for static signature/cache-decision and TLAS plan
  construction on synthetic inputs.

Allowed files:

- `neo/renderer/NVRHI/PathTraceCpuWork.*`
- `neo/renderer/NVRHI/PathTraceAccelerationPlan.*`
- narrow adapters in `PathTraceAcceleration.*`,
  `PathTraceGeometryUniverse.*`, and `PathTraceSmokeSceneBuild.cpp`
- CMake/test harness files needed for the CPU-only executable
- this document, if the task needs a scope correction

Forbidden files:

- shaders,
- ReSTIR/RLU visual bridge files,
- active graphics task docs,
- runtime deployment folder,
- broad renderer pass rewrites.

Proof gates:

- configure/build the worker checkout,
- run the headless harness,
- show the harness output summary,
- report whether any remaining validation is visual or runtime-only.

Stop conditions:

- The change requires judging screenshots.
- The worker must touch shader behavior.
- The worker must mutate `PathTraceGeometryUniverse` from a background thread.
- NVRHI device or command-list access appears necessary inside the worker.
- The task starts changing estimator/light transport behavior instead of CPU
  planning.

Later Plug-In Points
--------------------

Once this pattern exists, these systems can use the same snapshot/result/submit
model:

- skinning CPU fallback or skinning prep,
- material classification,
- emissive candidate extraction,
- texture/material table planning,
- RLU payload/remap planning,
- static BLAS hashing,
- rigid instance/TLAS planning.
