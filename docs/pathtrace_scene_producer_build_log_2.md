RBDoom 3 BFG PT Scene Producer Build Log 2
==========================================

Start date: 2026-05-05

Branch: `codex/refactor-pathtrace-smoke-modules`

Purpose
-------

This is the continuation log for the scene producer replacement work. Keep this
file focused on work after the first source3/drawSurf-mirror diagnostics slice.
The previous detailed log is:

    docs/pathtrace_scene_producer_build_log.md

The previous log should remain the detailed historical record for:

- source3 drawSurf mirror scaffold
- PathTraceDrawSurfCapture
- PathTraceInstanceUniverse
- rigid mesh candidate/local-source diagnostics
- CPU rigid BLAS plan/input descriptor diagnostics
- failed raster/post-blit bounds overlay attempts
- debugMode 21 solid bounds boxes
- debugMode 22 wireframe bounds boxes

Current Handoff State
---------------------

Recent commits at the handoff point:

    3c583342 pt: add drawsurf mirror bounds diagnostics
    ae06417f docs: update path tracing scene producer notes
    43bae064 pt: fix bounds wireframe from inside boxes

Working baseline:

- Source0 remains the stable fallback.
- Source3 is the current production scaffold:

       existing drawSurf-seeded static cache
       + mirrored dynamic-frame drawSurfs
       + old shader-compatible static/dynamic render buffers

- Source3 does not use PathTraceSceneUniverse as the main render source.
- PathTraceSceneUniverse remains static map/world diagnostics/preload only.
- Rigid objects are still rendered through the dynamic fallback.
- No reusable rigid BLAS is created yet.
- No rigid BLAS is routed into TLAS yet.
- No rigid triangles are removed from the dynamic fallback yet.
- `r_pathTracingRigidBlasGpuScaffold` defaults to 0 and should create no GPU
  rigid BLAS resources while off.

Validated diagnostics before this log:

- Source3 matched source0 in tested frames.
- Local-source rigid mesh records matched the baked dynamic rigid payload at
  count/material level.
- CPU rigid BLAS plan/input descriptor dumps were valid in tested frames.
- Mode 21 renders solid captured drawSurf mirror bounds boxes.
- Mode 22 renders PT-native wireframe drawSurf mirror bounds boxes and is the
  preferred spatial/BVH inspection mode.
- Mode 22 now shows box edges even when the camera starts inside the box by
  using the AABB exit face.

Important CVars For This Phase
------------------------------

Scene source:

       r_pathTracingSceneSource 0
       r_pathTracingSceneSource 3

DrawSurf mirror diagnostics:

       r_pathTracingSceneSourceCompare 1
       r_pathTracingInstanceUniverseDump 1

Rigid diagnostics:

       r_pathTracingRigidMeshUniverseDump 1
       r_pathTracingRigidMeshValidate 1
       r_pathTracingRigidBlasPlanDump 1
       r_pathTracingRigidBlasInputDump 1
       r_pathTracingRigidBlasGpuScaffold 0

Bounds visualization:

       r_pathTracingDebugMode 21
       r_pathTracingDebugMode 22
       r_pathTracingSceneBoundsOverlayMax 16
       r_pathTracingSceneBoundsOverlay 1
       r_pathTracingSceneBoundsOverlay 2
       r_pathTracingSceneBoundsOverlayGpu 0

Keep `r_pathTracingSceneBoundsOverlayGpu 0`. The old shader-composited per-pixel
line overlay caused device removal/TDR. Modes 21/22 are the supported
visualization path for this branch state.

Current Guardrails
------------------

- Do not revive the old in-tree RTXPT/Donut attempt.
- Treat RTXDI/RTXPT/Donut/Q2RTX as external references only.
- Do not use PathTraceSceneUniverse records as the primary source3 scene feed.
- Do not bake rigid entities into world-space static geometry.
- Do not synthesize fake drawSurfs or fake viewEntities.
- Keep source0 as fallback.
- Preserve mode18/mode19/mode20 shader/resource contracts while scaffolding.
- Keep `r_pathTracingSkipRaster3D` default 1.
- Keep `r_pathTracingWorldStaticEmissives` default 0.
- Keep `r_pathTracingDynamicOccluderRadius` default 0.
- Use Release/PT builds for real testing.

Next Intended Production Slice
------------------------------

The next safe production slice is disabled GPU scaffold work for reusable rigid
mesh records.

Do:

1. Keep scaffold default-off:

       r_pathTracingRigidBlasGpuScaffold 0

2. Add or preserve a separate build-submit gate before actual BLAS builds if
   needed:

       r_pathTracingRigidBlasGpuBuild 0

3. Create persistent scaffold records for eligible rigid mesh hashes:

       meshHash
       materialId
       source tri/vb/ib identity
       local vertex/index counts
       CPU local vertices/indexes if needed
       NVRHI vertex/index buffers
       NVRHI BLAS desc/handle
       dirty/valid/buildQueued flags
       lastSeenFrame

4. Build/reference local-space mesh data only. Object-to-world transforms belong
   to future TLAS instances, not mesh vertices.

5. When scaffold is off, create no rigid GPU buffers, no BLAS handles, and no
   uploads.

6. When scaffold is on but build is off, create/upload per-mesh rigid BLAS input
   buffers only. Keep them separate from current static/dynamic render buffers.

7. When build is on, submit per-mesh BLAS builds only. Do not add them to TLAS.

8. Add a one-shot dump if not already present:

       r_pathTracingRigidBlasGpuDump 1

   It should report:

       mesh records
       buffers created/reused
       upload bytes
       BLAS handles created
       BLAS builds submitted
       failures/skips
       renderPath=dynamicFallback
       tlasRoute=oldStaticPlusDynamic

Do not:

- Do not route rendering through reusable rigid BLAS in this slice.
- Do not remove rigid triangles from the dynamic fallback in this slice.
- Do not change mode 18/19/20 shader contracts while adding scaffold records.

Build And Install Reminder
--------------------------

Build from:

       E:\prog\rbdoom-3-bfg-rt\neo

Command:

       cmake --build --preset win64-pt-dev-release

Copy executable:

       E:\prog\rbdoom-3-bfg-rt\build-win64-pt-dev\Release\RBDoom3BFG.exe

to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

When path tracing shaders change, also copy:

       E:\prog\rbdoom-3-bfg-rt\base\renderprogs2\dxil\builtin\pathtracing\pathtrace_smoke.rt.bin
       E:\prog\rbdoom-3-bfg-rt\base\renderprogs2\spirv\builtin\pathtracing\pathtrace_smoke.rt.bin

to the matching paths under:

       E:\prog\rbdoom-3-BFG-prebuilt\base\renderprogs2\

Append New Work Below
---------------------

Use concise dated sections. Include:

- intent
- files touched
- CVars added/changed
- validation commands
- tester result
- next exact step


2026-05-05: Rigid BLAS GPU Scaffold Step 1
------------------------------------------

Intent:

- Add the first default-off GPU scaffold for reusable rigid mesh records.
- Keep source3 rendering through the existing dynamic fallback.
- Do not route reusable rigid BLAS records into TLAS.
- Do not remove rigid triangles from the dynamic fallback.

Files touched:

- `neo/renderer/NVRHI/PathTraceCVars.h`
- `neo/renderer/NVRHI/PathTraceCVars.cpp`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.h`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.cpp`
- `neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp`
- `docs/pathtracing.txt`
- `docs/pathtrace_modules.txt`
- `docs/pathtrace_scene_producer_build_log_2.md`

CVars added:

       r_pathTracingRigidBlasGpuBuild 0
       r_pathTracingRigidBlasGpuDump 0

Existing CVar used:

       r_pathTracingRigidBlasGpuScaffold 0

Implementation:

- `PathTraceGeometryUniverse` now stores per-rigid-mesh scaffold handles:
  local vertex buffer, local index buffer, optional BLAS desc/handle, upload
  signature, and upload/build validity flags.
- When `r_pathTracingRigidBlasGpuScaffold 0`, the scene build releases scaffold
  handles and creates no rigid GPU buffers, BLAS handles, uploads, or builds.
- When `r_pathTracingRigidBlasGpuScaffold 1` and
  `r_pathTracingRigidBlasGpuBuild 0`, the universe builds local-space
  `PathTraceSmokeVertex` data directly from the candidate `srfTriangles_t`,
  uploads per-mesh vertex/index buffers, and leaves BLAS build submission off.
- When both scaffold and build gates are 1, it creates standalone per-mesh BLAS
  handles and submits BLAS builds. These BLAS handles are not inserted into TLAS.
- The dump reports buffer creation/reuse, upload bytes, BLAS handle/build
  counts, skips, and explicitly prints:

       renderPath=dynamicFallback
       tlasRoute=oldStaticPlusDynamic

Validation:

       cmake --build --preset win64-pt-dev-release

Build result:

- Release/PT build succeeded after rerunning outside the sandbox because CMake
  initially failed to update build directory metadata from the sandbox.
- No shader files changed.

Tester requirements:

Buffer-only pass:

       r_pathTracingDebugMode 18
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 0
       r_pathTracingRigidBlasGpuDump 1

Optional after buffer-only is stable:

       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidBlasGpuDump 1

Expected:

- Mode 18/20 visuals should remain equivalent to source3 dynamic fallback.
- Dump should show valid mesh records, vertex/index buffers created or reused,
  upload bytes on first observation, and `renderPath=dynamicFallback`.
- With build off, `blasBuildsSubmitted=0`.
- With build on, standalone `blasBuildsSubmitted` may be nonzero, but
  `tlasRoute=oldStaticPlusDynamic` should remain unchanged.

Next exact step:

- Use tester dumps to verify buffer-only scaffold stability and reuse. If stable,
  validate standalone BLAS build submission in a prop-heavy scene before any TLAS
  routing work starts.


2026-05-05: Rigid TLAS Instance Plan Diagnostics
------------------------------------------------

Intent:

- Add the last diagnostics-only gate before any rigid BLAS/TLAS routing.
- Match current-frame source3 visible rigid instances to local-source rigid mesh
  records and scaffold BLAS readiness.
- Keep active rendering on the old static/dynamic fallback.

Files touched:

- `neo/renderer/NVRHI/PathTraceCVars.h`
- `neo/renderer/NVRHI/PathTraceCVars.cpp`
- `neo/renderer/NVRHI/PathTraceInstanceUniverse.h`
- `neo/renderer/NVRHI/PathTraceInstanceUniverse.cpp`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.h`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.cpp`
- `neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp`
- `docs/pathtracing.txt`
- `docs/pathtrace_modules.txt`
- `docs/pathtrace_scene_producer_build_log_2.md`

CVar added:

       r_pathTracingRigidTlasPlanDump 0

Implementation:

- `PathTraceInstanceUniverse` now retains current-frame instance observations
  for downstream diagnostics.
- `PathTraceGeometryUniverse::BuildRigidTlasPlanStats()` walks those visible
  instances, filters rigid instances, matches each mesh hash against the rigid
  local-source records, and reports whether the matching record has GPU buffers
  and a standalone BLAS handle.
- The dump reports visible instances, rigid instances, planned TLAS-style
  instances, unique mesh count, missing/stale mesh records, missing GPU buffers,
  missing BLAS handles, material override instances, planned rigid triangles,
  baked fallback rigid triangles, and sample transform origins.
- The dump explicitly keeps:

       renderPath=dynamicFallback
       tlasRoute=oldStaticPlusDynamic

Test:

       r_pathTracingDebugMode 18
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasPlanDump 1

Expected:

- `plannedInstances` should match or explain the visible rigid instance count.
- `missingMeshRecord`, `staleMeshRecord`, `missingGpuBuffers`, and `missingBlas`
  should ideally be zero after the GPU scaffold has warmed up.
- `plannedRigidTris` should align with the baked rigid fallback triangle count
  in tested source3 scenes.
- No visual behavior change is expected.

Next exact step:

- If TLAS plan dumps are clean in door and prop-heavy scenes, add a new
  explicitly experimental source mode or CVar that routes a tiny selected subset
  of rigid BLAS records into TLAS while leaving all rigid triangles in the
  dynamic fallback for overlap validation.


2026-05-05: Experimental Rigid TLAS Route Debug View
----------------------------------------------------

Intent:

- Add the first real routed rigid TLAS instance path without changing mode 18/20.
- Validate local-space rigid BLAS plus TLAS transforms in isolation.
- Avoid invalid shading from current shader assumptions that instance 0 is
  static and instance 1 is the old dynamic fallback.

Files touched:

- `neo/renderer/NVRHI/PathTraceAcceleration.h`
- `neo/renderer/NVRHI/PathTraceAcceleration.cpp`
- `neo/renderer/NVRHI/PathTraceCVars.h`
- `neo/renderer/NVRHI/PathTraceCVars.cpp`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.h`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.cpp`
- `neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp`
- `neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl`
- `docs/pathtracing.txt`
- `docs/pathtrace_modules.txt`
- `docs/pathtrace_scene_producer_build_log_2.md`

CVar added:

       r_pathTracingRigidTlasRoute 0

Debug mode added:

       r_pathTracingDebugMode 23

Implementation:

- `PathTraceAcceleration` can append extra TLAS instance descs supplied by the
  scene build.
- `PathTraceGeometryUniverse::BuildRigidTlasInstanceDescs()` emits TLAS
  instances for visible rigid records that have standalone BLAS handles.
- Instance transforms are converted from Doom `modelMatrix` layout to NVRHI's
  3x4 affine transform layout.
- The route is active only when all of these are true:

       r_pathTracingDebugMode 23
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1

- Routed rigid instances use TLAS mask `0x02`.
- Mode 23 traces with mask `0x02`, so it sees only routed rigid instances.
- Existing static/dynamic fallback instances remain in the TLAS but are not
  visible to mode 23's ray mask.
- The shader closest-hit path now has a minimal early return for `InstanceID >=
  2`, avoiding reads from the old static/dynamic geometry buffers. This is a
  validation path, not final rigid shading.

Test:

       r_pathTracingDebugMode 23
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1
       r_pathTracingRigidTlasPlanDump 1
       r_pathTracingRigidRouteOverlapDump 1

Expected:

- Mode 23 should show only routed rigid TLAS instances.
- Door/prop positions should match mode 22 rigid bounds and source3 visible
  objects.
- Mode 18/20 should remain unchanged because the route is gated to mode 23.
- This does not remove rigid triangles from the dynamic fallback and does not
  provide final material/texture shading for routed rigid instances.

Tester result:

- First mode-23 route attempt crashed during TLAS build:

       Cannot build TLAS PathTraceSmokeTLAS with 21 instances which is greater
       than topLevelMaxInstances specified at creation (2)

Fix:

- `PathTraceSmokeResources.cpp` now creates `PathTraceSmokeTLAS` with
  `topLevelMaxInstances=128` instead of 2. Normal mode 18/20 still build the
  usual two instances, while mode 23 has enough capacity for the capped routed
  rigid diagnostic instances.

Second tester result:

- No capacity crash, but mode 23 rendered black while the TLAS plan dump still
  showed ready rigid instances.

Fix:

- `PathTraceSmokeDispatch.cpp` now clamps debug mode to 23 instead of 22, so
  the shader receives mode 23.
- Static/dynamic fallback TLAS instances now use mask `0x01`; routed rigid TLAS
  instances use mask `0x02`. Normal traces still use `0xff`, while mode 23 uses
  `0x02` to isolate routed rigid instances.

Final tester result for this slice:

- Mode 23 works. Routed rigid meshes display and animate correctly.
- Tested dump:

       rigidInstances=17
       plannedInstances=17
       uniqueMeshes=16
       gpuBuffers=17
       blas=17
       missing(mesh/stale/buffers/blas)=0/0/0/0
       plannedRigidTris=1187
       bakedRigidSurfaces/tris=17/1187
       triangleDelta=0

- This validates local-space rigid BLAS plus TLAS transforms.
- Mode 23 remains a transform/route validation view only. It does not provide
  final material/texture shading for routed rigid instances.
- Mode 18/20 still render through the old static/dynamic fallback.

Next exact step:

- If mode 23 validates transforms, add a packed rigid route metadata/buffer path
  so routed rigid closest hits can load correct vertices/materials instead of
  using the minimal mode-23 diagnostic payload.


Autocompact Guard: Current Critical State
=========================================

This bottom stub is intentionally short. Prefer it after compaction before
continuing source routing work.

Current validated state:

- Source3 is still the main scaffold:

       old drawSurf-seeded static cache
       + mirrored dynamic-frame drawSurfs
       + old shader-compatible static/dynamic buffers

- Rigid local-source mesh records are valid.
- Rigid GPU scaffold works:

       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1

- Rigid TLAS plan is clean in tested scenes:

       missing(mesh/stale/buffers/blas)=0/0/0/0
       plannedRigidTris == bakedRigidTris

- Mode 23 validates routed local-space rigid BLAS instances in TLAS:

       r_pathTracingDebugMode 23
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1

- Tester confirmed: routed rigid meshes display and animate correctly.

Important implementation facts:

- `PathTraceSmokeTLAS` now uses `topLevelMaxInstances=128`.
- Static/dynamic fallback TLAS instances use mask `0x01`.
- Routed rigid TLAS instances use mask `0x02`.
- Mode 23 traces mask `0x02` only.
- Normal modes trace `0xff`, but routed rigid route is currently gated to mode
  23 so mode 18/20 stay unchanged.
- Shader closest-hit has a minimal early return for `InstanceID >= 2` to avoid
  invalid reads from old static/dynamic buffers. This is diagnostic-only.

Do not do next:

- Do not remove rigid triangles from the dynamic fallback yet.
- Do not route rigid instances into mode 18/20 shading until shader-side rigid
  metadata/material/index lookup exists.
- Do not treat mode 23 as final shading; it only proves transforms/visibility.

Next exact production step:

- Add a packed rigid route metadata/buffer path so routed rigid closest hits can
  load correct local-space vertices, indexes, triangle material IDs, and material
  table indexes.
- Then add an overlap validation mode that traces normal scene plus routed rigid
  instances while still keeping dynamic fallback rigid triangles present.


2026-05-05: Packed Rigid Route Shader Contract
----------------------------------------------

Intent:

- Move mode 23 beyond transform-only validation.
- Let routed rigid closest hits read packed local-space vertices/indexes and
  material metadata instead of using the minimal `InstanceID >= 2` payload.
- Keep mode 18/20 unchanged.

Files touched:

- `neo/renderer/NVRHI/PathTraceGeometry.h`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.h`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.cpp`
- `neo/renderer/NVRHI/PathTracePrimaryPass.h`
- `neo/renderer/NVRHI/PathTraceSmokeResources.h`
- `neo/renderer/NVRHI/PathTraceSmokeResources.cpp`
- `neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp`
- `neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp`
- `neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl`
- `docs/pathtrace_scene_producer_build_log_2.md`

Implementation:

- Added GPU-facing `PathTraceRigidRouteInstance`.
- Added packed route buffers:

       rigid route vertices
       rigid route indexes
       rigid route triangle material IDs
       rigid route triangle material table indexes
       rigid route instances

- Added SRV bindings:

       t22 SmokeRigidRouteVertices
       t23 SmokeRigidRouteIndices
       t24 SmokeRigidRouteTriangleMaterials
       t25 SmokeRigidRouteTriangleMaterialIndexes
       t26 SmokeRigidRouteInstances

- `PathTraceGeometryUniverse::BuildRigidRouteBuffers()` emits packed route data
  in the same order as routed TLAS instances. For now it duplicates mesh data per
  routed instance, which is simple and acceptable for the capped mode-23 route.
- Mode 23 closest-hit for `InstanceID >= 2` now loads:

       route instance metadata
       local vertex/index data
       triangle material ID
       triangle material table index
       interpolated UV/color/normal

- Mode 23 output now shades with material debug albedo and a normal/depth cue.

Build:

       cmake --build --preset win64-pt-dev-release

Build result:

- Release/PT build succeeded.
- Shader binaries changed and must be copied with the executable.

Test:

       r_pathTracingDebugMode 23
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1
       r_pathTracingRigidTlasPlanDump 1

Expected:

- Mode 23 should still show routed rigid meshes in the correct animated
  positions.
- Colors should now come from material table debug albedo plus normal/depth cue,
  not only routed instance ID.
- Mode 18/20 should remain unchanged.

Next exact step:

- If mode 23 still validates, add an overlap validation mode/CVar that traces
  normal static/dynamic fallback plus routed rigid instances while keeping rigid
  fallback triangles present. Only after overlap is sane should removal from the
  dynamic fallback be attempted.


2026-05-05: Rigid Route Overlap Validation Mode
-----------------------------------------------

Intent:

- Add the validation step before removing rigid triangles from the dynamic
  fallback.
- Keep mode 18/20 unchanged.
- Avoid ambiguous duplicate-hit behavior by tracing fallback and routed rigid
  masks separately in the shader.

Files touched:

- `neo/renderer/NVRHI/PathTraceCVars.cpp`
- `neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp`
- `neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp`
- `neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl`
- `docs/pathtracing.txt`
- `docs/pathtrace_modules.txt`
- `docs/pathtrace_scene_producer_build_log_2.md`

Debug mode added:

       r_pathTracingDebugMode 24

Implementation:

- Mode 23 remains routed-rigid-only with trace mask `0x02`.
- Mode 24 enables the same routed rigid TLAS instances and packed route buffers
  as mode 23.
- Mode 24 raygen traces two separate payloads:

       fallback-only mask 0x01
       routed-rigid-only mask 0x02

- Mode 24 color code:

       green = routed rigid hit matches fallback hit distance
       cyan = routed rigid hit, no fallback hit
       blue = routed rigid hit in front of fallback hit
       orange = fallback hit in front of routed rigid hit
       dim gray = fallback hit only
       black = neither path hit

- Dynamic fallback rigid triangles are still present. This is intentional for
  overlap validation.
- `r_pathTracingRigidRouteOverlapDump 1` queues a one-shot readback and prints
  pixel proportions for these mode-24 color buckets.
- `r_pathTracingRigidRouteRemoveDynamic 1` is the next default-off validation
  gate. In mode 24 only, it skips routed-ready rigid candidates from the source3
  dynamic fallback. Expected mode-24 result after enabling it: previously green
  routed surfaces become cyan because the fallback copy is gone; orange remains
  possible where remaining fallback geometry is in front.

Test:

       r_pathTracingDebugMode 24
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1
       r_pathTracingRigidTlasPlanDump 1

Build:

       cmake --build --preset win64-pt-dev-release

Build result:

- Release/PT build succeeded.
- Copied `RBDoom3BFG.exe` and both DXIL/SPIR-V `pathtrace_smoke.rt.bin`
  shader binaries to the prepared game folder.

Expected:

- Correctly routed rigid meshes should mostly show green where their fallback
  dynamic copy is visible at the same hit distance.
- Blue/cyan or orange/red regions are the areas to inspect for mismatch,
  occlusion, stale transforms, or missing fallback correspondence.
- Mode 18/20 should remain unchanged.

Next exact step:

- Build and test mode 24. If overlap is clean, add a default-off removal gate
  that excludes routed rigid candidates from the dynamic fallback only when the
  matching route buffers and TLAS instances were emitted successfully.

Tester result:

- Mode 24 overlap was clean. Visual report: all routed rigid surfaces were green
  aside from orange partial occlusion.
- Readback dump:

       match=131739 (14.29%)
       rigidOnly=0
       rigidInFront=0
       fallbackInFront=13664 (1.48%)
       fallbackOnly=776197 (84.22%)
       neither=0
       unknown=0

Follow-up implementation:

- Added `r_pathTracingRigidRouteRemoveDynamic 1` for the first dynamic fallback
  subtraction test in mode 24 only.
- Removal currently checks persistent rigid mesh route readiness from the GPU
  scaffold/standalone BLAS cache before skipping the source3 dynamic fallback
  surface.


2026-05-05: Routed Rigid World-Space Normals
--------------------------------------------

Intent:

- Prepare routed rigid records for real lighting tests.
- Mode 24 overlap does not depend on normals, but modes 18/20 lighting expect
  payload normals/tangents in world space.

Files touched:

- `neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl`
- `docs/pathtracing.txt`
- `docs/pathtrace_modules.txt`
- `docs/pathtrace_scene_producer_build_log_2.md`

Implementation:

- Added shader helpers to transform object-space vectors/normals through
  `ObjectToWorld3x4()`.
- Routed rigid closest-hit now transforms local geometric normal and
  interpolated vertex normal into world space before writing the payload.
- Routed rigid closest-hit now computes local UV-gradient tangent/bitangent,
  transforms them into world space, and orthonormalizes against the world normal.

Expected:

- Mode 23 normal/debug material output should remain stable.
- Mode 24 overlap/removal behavior should remain unchanged.
- Future lighting tests can rely on routed rigid payload normals being in world
  space instead of local object space.


Autocompact Guard: Current Critical State 2
===========================================

- Source3 remains the scaffold:

       old drawSurf-seeded static cache
       + mirrored dynamic-frame drawSurfs
       + old shader-compatible static/dynamic buffers

- Rigid route gates:

       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1

- Mode 23:

       rigid-only trace mask 0x02
       validates routed local-space rigid BLAS/TLAS instances
       closest hit reads packed route vertex/index/material buffers

- Mode 24:

       fallback-only mask 0x01 + routed-rigid-only mask 0x02
       overlap validation before dynamic fallback removal
       green means routed rigid and fallback agree by hit distance
       r_pathTracingRigidRouteOverlapDump 1 prints per-color proportions

- Static/dynamic fallback TLAS instances use mask `0x01`.
- Routed rigid TLAS instances use mask `0x02`.
- `PathTraceSmokeTLAS` max instances is 128.
- Do not remove rigid triangles from the dynamic fallback until mode 24 is
  tested cleanly.
- Do not route rigid instances into mode 18/20 shading yet.


2026-05-05: Routed Rigid Lighting Validation Mode
-------------------------------------------------

Intent:

- Add a visual lighting validation path before touching mode 18/20.
- Use the already validated routed-rigid TLAS path and dynamic fallback removal.
- Exercise the routed rigid world-space normal/tangent payload under simple
  lighting.

Files touched:

- `neo/renderer/NVRHI/PathTraceCVars.cpp`
- `neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp`
- `neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp`
- `neo/renderer/NVRHI/PathTraceSmokeReadback.cpp`
- `neo/renderer/NVRHI/PathTraceDrawSurfCapture.cpp`
- `neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl`
- `docs/pathtracing.txt`
- `docs/pathtrace_modules.txt`
- `docs/pathtrace_scene_producer_build_log_2.md`

Debug mode added:

       r_pathTracingDebugMode 25

Implementation:

- Mode 25 enables the same routed rigid TLAS instances and packed route buffers
  as modes 23/24.
- Mode 25 traces the combined scene with mask `0xff`.
- With `r_pathTracingRigidRouteRemoveDynamic 1`, routed-ready rigid candidates
  are removed from the source3 dynamic fallback before rendering.
- The shader shades mode 25 with material debug albedo plus simple world-space
  normal lighting. Routed rigid hits receive a subtle cyan tint.
- Mode 18/20 are still unchanged.

Test:

       r_pathTracingDebugMode 25
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1
       r_pathTracingRigidRouteRemoveDynamic 1
       r_pathTracingRigidTlasPlanDump 1

Expected:

- Routed rigid props/doors should be present and lit coherently.
- No duplicate rigid copy should remain from the dynamic fallback.
- Characters/effects/other dynamic fallback geometry should still be present.
- Mode 18/20 should remain unchanged.


2026-05-05: Mode 18 Routed Rigid Integration Gate
-------------------------------------------------

Intent:

- Add the first explicitly gated real toy-PT integration path.
- Keep mode 20 unchanged.
- Preserve the source3 fallback and only route eligible rigid records when the
  new mode 18 gate is enabled.

Files touched:

- `neo/renderer/NVRHI/PathTraceCVars.h`
- `neo/renderer/NVRHI/PathTraceCVars.cpp`
- `neo/renderer/NVRHI/PathTraceDrawSurfCapture.cpp`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.h`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.cpp`
- `neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp`
- `docs/pathtracing.txt`
- `docs/pathtrace_modules.txt`
- `docs/pathtrace_scene_producer_build_log_2.md`

CVar added:

       r_pathTracingRigidRouteMode18 0

Implementation:

- Mode 18 can now opt into the routed rigid TLAS path only when all route gates
  are enabled:

       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1
       r_pathTracingRigidRouteMode18 1

- With `r_pathTracingRigidRouteRemoveDynamic 1`, routed-ready rigid surfaces are
  removed from the source3 dynamic fallback for mode 18 as well.
- Routed rigid material IDs are added to the frame material table before route
  buffer construction, so removing the dynamic fallback copy does not drop
  routed-rigid materials.
- Mode 18 still traces mask `0xff` and uses the existing toy path tracing shader
  path.
- Mode 20 remains unchanged.

Test:

       r_pathTracingDebugMode 18
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1
       r_pathTracingRigidRouteMode18 1
       r_pathTracingRigidRouteRemoveDynamic 1
       r_pathTracingRigidTlasPlanDump 1

Expected:

- Mode 18 should remain visually coherent.
- Rigid props/doors should be present through routed TLAS instances.
- Dynamic fallback rigid duplicate should be absent.
- Characters/effects/other non-rigid dynamic fallback geometry should remain.
- Performance may not improve yet because route buffers are still packed
  per-instance for validation; this test is correctness first.

Tester result note:

- First reported mode 18 test had no obvious visual or performance issues, but
  the dump did not include `r_pathTracingRigidRouteMode18 1`.
- The dump still showed:

       bakedRigidSurfaces/tris=16/1101
       triangleDelta=0

  That means the routed mode 18 removal path was probably not active; it was
  likely normal source3 mode 18 fallback.
- Retest must include `r_pathTracingRigidRouteMode18 1`. Expected confirmation
  for a real routed mode 18 removal run:

       bakedRigidSurfaces/tris=0/0
       triangleDelta=plannedRigidTris

Final tester result:

- Corrected mode 18 routed run included `r_pathTracingRigidRouteMode18 1`.
- Dump:

       plannedInstances=19
       gpuBuffers=19
       blas=19
       missing(mesh/stale/buffers/blas)=0/0/0/0
       plannedRigidTris=1115
       bakedRigidSurfaces/tris=0/0
       triangleDelta=1115

- Tester also did a quick check through other areas and reported no major
  crashes, glitches, visual errors, or performance issues.
- Mode 18 routed rigid integration is validated enough to proceed to a gated
  mode 20 route test.


Autocompact Guard: Current Critical State 3
===========================================

Use this block first after compaction.

Branch/state:

- Branch: `codex/refactor-pathtrace-smoke-modules`.
- Many files are intentionally modified; do not revert user/previous work.
- Do not stage/commit `docs/pathtracing_handoff.txt`.
- Build from `E:\prog\rbdoom-3-bfg-rt\neo`:

       cmake --build --preset win64-pt-dev-release

- Copy exe to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

- If shader changed, copy both:

       base\renderprogs2\dxil\builtin\pathtracing\pathtrace_smoke.rt.bin
       base\renderprogs2\spirv\builtin\pathtracing\pathtrace_smoke.rt.bin

Validated route milestones:

- Mode 23 routed rigid TLAS instances display and animate correctly.
- Packed route buffers are bound at t22-t26 and closest-hit for `InstanceID>=2`
  reads local vertices/indexes/material IDs/material table indexes.
- Binding order was fixed after a level-load crash; binding set order must match
  layout order 0..26.
- Mode 24 overlap validation was clean:

       match=14.29%
       rigidOnly=0
       rigidInFront=0
       fallbackInFront=1.48%
       fallbackOnly=84.22%
       unknown=0

- Mode 24 removal validation worked:

       removedSurfaces=18
       removedIndexes=3567
       plannedRigidTris=1189
       bakedRigidSurfaces/tris=0/0

- Mode 25 routed rigid lighting validation visually looked fine and had clean
  route readiness:

       plannedInstances=15
       gpuBuffers=15
       blas=15
       missing=0/0/0/0
       bakedRigidSurfaces/tris=0/0

Current route gates:

       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1

Mode meanings:

- Mode 23: routed-rigid-only trace mask `0x02`.
- Mode 24: fallback-only mask `0x01` plus routed-rigid-only mask `0x02`;
  overlap/removal validation and optional pixel bucket dump.
- Mode 25: combined trace mask `0xff`; simple material/normal lighting
  validation with routed rigid and optional dynamic removal.
- Mode 18 integration gate is validated in the tested Mars City areas.
- Mode 20 now has a separately gated route test path.

Critical CVars:

       r_pathTracingRigidRouteOverlapDump 1
       r_pathTracingRigidRouteRemoveDynamic 1
       r_pathTracingRigidRouteMode18 1
       r_pathTracingRigidRouteMode20 1

Validated mode 18 routed command:

       r_pathTracingDebugMode 18
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1
       r_pathTracingRigidRouteMode18 1
       r_pathTracingRigidRouteRemoveDynamic 1
       r_pathTracingRigidTlasPlanDump 1
       condump rigidroute_mode18_real

Validated mode 18 confirmation:

       plannedInstances > 0
       missing(mesh/stale/buffers/blas)=0/0/0/0
       bakedRigidSurfaces/tris=0/0
       triangleDelta=plannedRigidTris

Mode 20 route gate added:

- Added default-off `r_pathTracingRigidRouteMode20`.
- Mode 20 routing remains disabled unless this gate is set.
- When the gate is enabled, source3 mode 20 now:

       append routed rigid TLAS instances
       remove routed-ready rigid surfaces from the dynamic fallback
       include routed rigid material IDs in the frame material table
       otherwise keep mode 20 reservoir/emissive behavior unchanged

First test command:

       r_pathTracingDebugMode 20
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1
       r_pathTracingRigidRouteMode20 1
       r_pathTracingRigidRouteRemoveDynamic 1
       r_pathTracingRigidTlasPlanDump 1
       condump rigidroute_mode20

Important implementation facts:

- Static/dynamic fallback TLAS instances use mask `0x01`.
- Routed rigid TLAS instances use mask `0x02`.
- Normal/mode 18/mode 25 traces use mask `0xff`.
- `PathTraceSmokeTLAS` max instances is 128.
- Routed rigid closest-hit transforms local normals/tangent basis into world
  space through `ObjectToWorld3x4()`.
- Mode 18 route gate adds routed rigid material IDs to the frame material table
  before route buffer construction, so removing dynamic rigid copies should not
  lose materials.
- Apply the same material-table protection for mode 20 routing.
- Mode 20 uses reservoir/emissive paths; do not change reservoir logic in the
  first mode 20 route gate. The first test is geometry routing correctness only.

Mode 20 routed emissive follow-up:

- First tester result for `rigidroute_mode20`:

       plannedInstances=22
       gpuBuffers=22
       blas=22
       missing(mesh/stale/buffers/blas)=0/0/0/0
       plannedRigidTris=1125
       bakedRigidSurfaces/tris=0/0
       remainingDynamicRigidTris=0
       triangleDelta=1125

- Visual result: routed geometry mostly worked and prior glitchy areas looked
  more stable, but many emissives visible in mode 18 disappeared in mode 20.
- Diagnosis: geometry routing/removal worked; the missing emissives were likely
  routed-ready rigid triangles being removed from the dynamic fallback before
  mode 20's emissive candidate inventory could see them.
- Added a CPU-only routed rigid emissive append:

       RtPathTraceRigidRouteBuild::instanceObjectToWorld
       AppendSmokeRigidRouteEmissiveTriangleInventory(...)

- This does not change the GPU `PathTraceRigidRouteInstance` shader layout.
- Mode 20 with `r_pathTracingRigidRouteMode20 1` now appends emissive triangle
  candidates from routed rigid local vertices transformed by the current
  object-to-world matrix. The reservoir algorithm remains unchanged.
- `r_pathTracingEmissiveInventoryDump 1` now reports `routedRigid=...` in the
  inventory summary.

Final tester interpretation:

- Follow-up dump `rigidroute_mode20_emissive` confirmed:

       plannedInstances=22
       gpuBuffers=22
       blas=22
       missing(mesh/stale/buffers/blas)=0/0/0/0
       plannedRigidTris=1296
       bakedRigidSurfaces/tris=0/0
       remainingDynamicRigidTris=0
       triangleDelta=1296
       emissive inventory routedRigid=6

- Tester reported the emissives are present after the routed emissive append.
- Important mode 20 interpretation: regular ray-traced/idLight scene lights do
  not contribute to the mode 20 reservoir. Mode 20 currently samples
  `SmokeEmissiveTriangles` / `SmokeLightCandidates`, unlike mode 18's selected
  scene-light loop.
- Remaining raster-like flicker near the screen edge is expected if an emissive
  candidate source depends on current visible drawSurfs. This is a future
  persistent/off-screen emissive light-list problem, not a routed rigid TLAS
  geometry failure.
- Next logical production direction after this commit is Stage F/G style work:
  stable emissive light records plus area/portal-selected light candidate lists.


Post-commit Stage F/G Start: Light Area Diagnostics
==================================================

Objective:

- Start the persistent/off-screen emissive work without changing mode 20
  sampling or candidate filtering.
- Add diagnostics that prove emissive candidates can be assigned Doom
  area/portal membership before using that membership to select light lists.

Implemented first diagnostic slice:

- `RtSmokeLightUniverse::MergeFrameCandidates` now receives `viewDef`.
- Each persistent static/dynamic light-universe record stores `areaNum`.
- Area assignment uses `renderWorld->PointInArea(candidateCenter)` with a small
  normal-offset fallback if the exact center lands outside an area.
- `r_pathTracingLightUniverseDump 1` now prints:

       area current/total=A/B
       selected steps/areas/edges/blocked=A/B/C/D
       staticKnown/unknown=A/B
       dynamicKnown/unknown=A/B
       mergedKnown/unknown=A/B
       mergedCurrent/selected/connected/disconnected=A/B/C/D
       connectedUnselected=A
       portalSweep areas=A/B/C/D/E
       portalSweep merged=A/B/C/D/E
       portalDepthBins depth0/1/2/3/4/>4/disconnected/unknown=A/B/C/D/E/F/G/H

Important boundary:

- This does not filter mode 20 candidates.
- This does not add area/portal-selected light lists yet.
- It is strictly a diagnostic proof that emissive records have usable area
  membership or logged unknown-area counts.
- The selected-area set uses the existing `r_pathTracingScenePortalSteps`
  CVar. It does not affect light selection yet.
- A portal-depth sweep now reports selected area count and selected merged
  candidate count for depths 0, 1, 2, 3, and 4 in one dump.
- A direct portal-depth histogram now reports the minimum depth needed for each
  merged candidate, avoiding manual subtraction of cumulative sweep fields.

Suggested test command:

       r_pathTracingDebugMode 20
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1
       r_pathTracingRigidRouteMode20 1
       r_pathTracingRigidRouteRemoveDynamic 1
       r_pathTracingScenePortalSteps 1
       r_pathTracingLightUniverseDump 1
       condump light_area_diag

Expected first-pass result:

- No visual behavior change from the previous validated mode 20 route build.
- Dump should show a valid `area current/total`.
- `staticKnown + dynamicKnown` should be much larger than unknown counts in
  ordinary indoor areas. Unknown counts are useful diagnostics for candidates
  that need fallback handling before area/portal filtering.
- Compare `mergedSelected` and `connectedUnselected`: if many connected
  candidates are outside the selected portal depth, that confirms why selection
  must stay diagnostic until we tune area/portal steps and fallbacks.
- Use `portalSweep merged` to identify the first portal depth that captures most
  currently connected candidates without requiring repeated game runs.
- Use `portalDepthBins` to spot candidates that require depth 4+, are
  disconnected from the current view, or have unknown area membership.

Added diagnostic light-area selector CVars:

       r_pathTracingLightAreaPortalSteps 1
       r_pathTracingLightAreaFilter 0
       r_pathTracingLightAreaFilterApply 0
       r_pathTracingLightAreaOverflowMax 512

Behavior:

- `r_pathTracingLightAreaPortalSteps` is now the light-universe selected-area
  depth. It is intentionally separate from `r_pathTracingScenePortalSteps`.
- `r_pathTracingLightAreaFilter 1` enables diagnostic selector reporting only.
  It does not change uploaded mode 20 emissive buffers yet.
- `r_pathTracingLightAreaFilterApply 1` is the separate experimental
  render-affecting gate. It uploads selected-area candidates plus the
  highest-weight connected overflow candidates up to
  `r_pathTracingLightAreaOverflowMax`, and drops disconnected/unknown-area
  candidates.
- `r_pathTracingLightAreaOverflowMax` now defaults to 512. Current evidence
  supports a greedy connected-candidate policy: Doom's authored portal layout is
  already valuable culling, and ReSTIR/PT is expected to tolerate many lights.
  The cap is a safety limit, not an aggressive default quality knob.
- The diagnostic selector reports:

       areaFilter enabled/applied=A/B steps=C overflowMax=D
       selected=A connectedOverflow=B disconnected=C unknown=D
       wouldUpload=A wouldDrop=B
       area pre/post drop=A/B/C
       weight pre/post drop=A/B/C
       dropWeight overflow/disconnected/unknown=A/B/C

- It also prints up to four top connected-overflow samples by candidate weight:

       RT smoke light area overflow sample N area=A material=M materialIndex=I triArea=A weight=W distance=D

- It also prints up to four top dropped samples by candidate weight:

       RT smoke light area dropped sample N reason=R area=A material=M materialIndex=I triArea=A weight=W distance=D

Recommended next diagnostic command:

       r_pathTracingMode20TestPreset 2
       r_pathTracingLightUniverseDump 1
       condump light_area_filter_diag

Render-affecting test command, only after diagnostic counts look safe:

       r_pathTracingMode20TestPreset 3
       r_pathTracingLightUniverseDump 1
       condump light_area_filter_apply

Preset details:

- `r_pathTracingMode20TestPreset 1`: mode 20 + source3 + rigid route + dynamic
  rigid removal.
- `r_pathTracingMode20TestPreset 2`: preset 1 plus light-area diagnostics.
- `r_pathTracingMode20TestPreset 3`: preset 2 plus render-affecting light-area
  filter apply.
- The preset CVar resets itself to 0 after applying.

Expected:

- Visual output should remain unchanged.
- `wouldUpload` should be selected candidates plus up to 64 connected overflow
  candidates if a smaller test cap is set, or most/all connected overflow
  candidates with the default 512 cap.
- `wouldDrop` should account for connected overflow beyond the budget,
  disconnected candidates, and unknown-area candidates.
- Dropped `weight` should be small or explainable by disconnected/unknown
  candidates. High dropped weight means the area filter is too aggressive for
  that location.
- With apply enabled, visual/performance changes should be limited to the
  candidates dropped by `wouldDrop`. If a visible emissive disappears, disable
  apply and inspect disconnected/unknown-area counts.

Validation limit:

- Testing so far is confined to the opening section because the PT branch is
  missing enough game-logic behavior for deeper campaign validation.
- Do not hard-code a final portal-depth rule from this area. Keep portal steps
  configurable and keep apply default-off until broader map coverage is viable.

Preset/apply validation:

- `r_pathTracingMode20TestPreset 3` was tested successfully.
- Console confirmed:

       applied mode20 test preset 3 source3=1 rigidRoute=1 lightAreaDiag=1 lightAreaApply=1 overflowMax=512

- Resulting dump:

       merged=332
       area current/total=16/56
       selected steps/areas/edges/blocked=1/3/2/0
       staticKnown/unknown=52/0
       dynamicKnown/unknown=166/0
       mergedKnown/unknown=332/0
       mergedCurrent/selected/connected/disconnected=0/270/56/6
       connectedUnselected=56
       portalSweep merged=0/270/270/270/326
       portalDepthBins depth0/1/2/3/4/>4/disconnected/unknown=0/270/0/0/56/0/6/0
       areaFilter enabled/applied=1/1
       overflowMax=512
       selected=270
       connectedOverflow=56
       disconnected=6
       unknown=0
       wouldUpload=326
       wouldDrop=6
       runtimeActive=326

- Interpretation:

       The greedy default kept all selected candidates and all connected
       overflow candidates. Only disconnected candidates were dropped. Upload
       count matched the filter result.

Current filter-impact validation after diagnostic patch:

- Tester ran:

       r_pathTracingMode20TestPreset 3
       r_pathTracingLightUniverseDump 1
       condump light_area_filter_impact

- Relevant dump:

       static=170 seen=52 missing=118 semiStatic=174 dynSeen=174 merged=344
       selected steps/areas/edges/blocked=1/3/2/0
       staticKnown/unknown=52/0
       dynamicKnown/unknown=172/2
       mergedKnown/unknown=342/2
       mergedCurrent/selected/connected/disconnected=0/274/60/8
       connectedUnselected=60
       portalSweep merged=0/274/274/274/334
       portalDepthBins depth0/1/2/3/4/>4/disconnected/unknown=0/274/0/0/60/0/8/2
       areaFilter enabled/applied=1/1
       overflowMax=512
       selected=274
       connectedOverflow=60
       disconnected=8
       unknown=2
       wouldUpload=334
       wouldDrop=10
       area 27368.00/27007.39 drop=360.62
       weight 27368.004/27007.387 drop=360.617
       dropWeight overflow/disconnected/unknown=0.000/301.837/58.784
       runtimeActive=334

- Interpretation:

       This is a healthy first result. The render-affecting filter removed
       10/344 merged candidates, about 1.3 percent of the measured area/weight.
       No connected overflow was dropped. Dropped weight came only from
       disconnected and unknown-area candidates, and runtimeActive matched the
       filtered upload count.


Autocompact Guard: Current Critical State 4
===========================================

Read this block first after compaction.

Branch/state:

- Branch: `codex/refactor-pathtrace-smoke-modules`.
- Latest committed base:

       02e4a524 pt: route rigid BLAS instances into smoke modes
       120d70c9 pt: add emissive light area selector diagnostics
       7c34b687 pt: add mode 20 test preset

- Current uncommitted work is the filter-impact diagnostic patch only. It was
  built and copied before this guard update. Expected touched files:

       docs/pathtrace_scene_producer_build_log_2.md
       docs/pathtracing.txt
       neo/renderer/NVRHI/PathTraceLightUniverse.cpp
       neo/renderer/NVRHI/PathTraceLightUniverse.h
       neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp

- The filter-impact patch adds dump-only accounting for light-area filtering:

       area pre/post/drop
       weight pre/post/drop
       dropWeight overflow/disconnected/unknown
       top dropped candidate samples with reason, material, materialIndex,
       triArea, weight, and distance

- Build from `E:\prog\rbdoom-3-bfg-rt\neo`:

       cmake --build --preset win64-pt-dev-release

- Copy exe to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Current validated mode 20 route stack:

- `r_pathTracingMode20TestPreset 1`

       mode 20
       source3
       rigid BLAS GPU scaffold/build
       rigid TLAS route
       mode20 route gate
       routed-ready dynamic rigid removal
       light-area overflow default 512

- `r_pathTracingMode20TestPreset 2`

       preset 1 plus light-area diagnostics

- `r_pathTracingMode20TestPreset 3`

       preset 2 plus render-affecting light-area filter apply

Light-area selector policy:

- Default apply gate remains off:

       r_pathTracingLightAreaFilterApply 0

- Diagnostic gate:

       r_pathTracingLightAreaFilter 1

- Apply gate:

       r_pathTracingLightAreaFilterApply 1

- Portal depth default:

       r_pathTracingLightAreaPortalSteps 1

- Overflow cap default:

       r_pathTracingLightAreaOverflowMax 512

- Intended first behavior is greedy:

       keep selected portal-area candidates
       keep connected overflow candidates up to high cap
       drop disconnected/unknown-area candidates

Reasoning:

- Opening-section tests show Doom portal areas give useful free culling, but
  connected overflow should not be aggressively reduced yet.
- ReSTIR/PT is expected to tolerate many lights, so the cap is a safety limit
  rather than a normal quality knob.
- Broader campaign validation is currently blocked by missing PT branch
  game-logic behavior. Do not hard-code a final portal-depth heuristic yet.

Quick test command after build/copy:

       r_pathTracingMode20TestPreset 3
       r_pathTracingLightUniverseDump 1
       condump light_area_filter_impact

Expected:

       areaFilter enabled/applied=1/1
       runtimeActive == wouldUpload
       area pre/post drop=...
       weight pre/post drop=...
       dropWeight overflow/disconnected/unknown=...
       RT smoke light area dropped sample ...

Current tester result:

       wouldUpload=334
       wouldDrop=10
       runtimeActive=334
       area 27368.00/27007.39 drop=360.62
       weight 27368.004/27007.387 drop=360.617
       dropWeight overflow/disconnected/unknown=0.000/301.837/58.784

Interpretation:

- Healthy initial result. Dropped weight is low and explained entirely by
  disconnected/unknown-area candidates. Connected overflow was not dropped.

Next decision:

- If the user reports no visible regression from this run, commit the
  filter-impact patch.
- If the user reports visible light loss, keep apply off by default and tune
  the selector before committing behavior changes.


Mode 20 Active Light Churn Diagnostic
=====================================

Commit before this section:

       87254b9d pt: add light area filter impact diagnostics

Implemented the next diagnostic slice after the filter-impact patch:

- Added `r_pathTracingLightUniverseChurn`.
- Presets 2 and 3 now enable churn tracking automatically.
- `RtSmokeLightUniverse` now tracks the previous frame's uploaded emissive
  candidate set when churn is enabled.
- `r_pathTracingLightUniverseDump 1` now prints:

       RT smoke light churn enabled=E previous/current/stayed/entered/left=A/B/C/D/E
       weight previous/current/stayed/entered/left=A/B/C/D/E
       RT smoke light churn entered sample ...
       RT smoke light churn left sample ...

Intent:

- This diagnoses mode 20 edge flicker by separating reservoir/shading noise
  from actual uploaded light-set churn.
- High entered/left counts or weight during tiny camera movement indicates the
  active emissive list is changing materially.
- Low churn with visible flicker points away from light-list membership and
  toward reservoir sampling/noise, visibility, or shading.

Build status:

- Release/PT build passed after the churn patch.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Recommended churn test:

       r_pathTracingMode20TestPreset 3

Move/turn near the suspected portal or screen-edge flicker area for a few
seconds so churn has previous-frame history, then run:

       r_pathTracingLightUniverseDump 1
       condump light_churn_edge

Expected useful lines:

       RT smoke light universe ...
       RT smoke light origins ...
       RT smoke light churn enabled=1 previous/current/stayed/entered/left=...
       RT smoke light churn entered sample ...
       RT smoke light churn left sample ...

Interpretation:

- `entered`/`left` near zero during tiny view changes is good.
- Large entered/left `weight` identifies actual active-list instability.
- Top entered/left samples give material, area, triArea, weight, and distance
  so we can map churn back to portal classification or raster capture behavior.


Autocompact Guard: Current Critical State 5
===========================================

Read this block first after compaction.

Branch/state:

- Branch: `codex/refactor-pathtrace-smoke-modules`.
- Latest committed base:

       87254b9d pt: add light area filter impact diagnostics

- Current uncommitted work is the active-light churn diagnostic patch:

       docs/pathtrace_scene_producer_build_log_2.md
       docs/pathtracing.txt
       neo/renderer/NVRHI/PathTraceCVars.cpp
       neo/renderer/NVRHI/PathTraceCVars.h
       neo/renderer/NVRHI/PathTraceLightUniverse.cpp
       neo/renderer/NVRHI/PathTraceLightUniverse.h
       neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp

- Build/copy has already succeeded for this uncommitted patch.

What changed:

- New CVar:

       r_pathTracingLightUniverseChurn 0

- `r_pathTracingMode20TestPreset 2` and `3` enable churn automatically.
- Dump now includes active light-list churn:

       previous/current/stayed/entered/left
       weight previous/current/stayed/entered/left
       top entered/left samples

Current test request:

       r_pathTracingMode20TestPreset 3
       move/turn near flicker area for a few seconds
       r_pathTracingLightUniverseDump 1
       condump light_churn_edge

Next decision:

- If churn is low while flicker remains, investigate reservoir/noise or
  shading/visibility behavior.
- If churn is high, inspect entered/left sample material/area data and tune
  area filtering or capture persistence.


Rigid Geometry Portal Residency Scaffold
========================================

Implemented the first production slice for the stronger RT residency rule:

       current Doom area + selected adjacent portal areas
       = resident rigid geometry for routed source3 BVH/TLAS

New CVars:

       r_pathTracingRigidResidency 0
       r_pathTracingRigidResidencyPortalSteps 1
       r_pathTracingRigidResidencyDump 0

Behavior:

- DrawSurf capture now assigns `RtPathTraceInstanceObservation::currentArea`
  by transforming the local `srfTriangles_t::bounds` center to world space and
  calling Doom `PointInArea`.
- `RtSmokeGeometryUniverse` now keeps a persistent rigid instance cache keyed
  by source3 instance identity.
- When `r_pathTracingRigidResidency 1` is enabled, it selects current player
  area plus `r_pathTracingRigidResidencyPortalSteps` open portal steps and
  builds a resident rigid instance list from cached instances in those areas.
- Rigid route material collection, rigid route buffers, and extra rigid TLAS
  instance descriptors use the resident list instead of only the current
  raster-visible instance list while residency is enabled.
- Mode 20 test presets now enable rigid residency with portal depth 1.

Important boundary:

- This does not yet create new mesh records for never-seen areas. A fresh
  adjacent cell becomes fully resident after its rigid drawSurfs have been
  observed at least once and cached.
- Cached invisible movers use their last observed transform. Later motion-vector
  / per-frame update work should update moving resident objects in the selected
  cell range even if raster culls them.
- Dynamic/skinned/deforming objects remain raster-visible only.

Build status:

- Release/PT build passed after the residency scaffold.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Recommended residency test:

       r_pathTracingMode20TestPreset 3
       r_pathTracingRigidResidencyDump 1
       r_pathTracingLightUniverseDump 1
       condump rigid_residency_mode20

Expected useful lines:

       PathTracePrimaryPass: PT rigid residency ...
       routeSource=portalResident
       visibleRigid=A cachedRigid=B resident=C seen/cache=D/E routeReady=F

Interpretation:

- `residentFromCache > 0` proves cached rigid geometry is being kept in the
  route after it leaves the raster-visible drawSurf list.
- `skippedUnknownArea` identifies rigid candidates whose bounds center could
  not be mapped into Doom areas.
- `residentMissingBlas` means residency selected the instance, but the mesh has
  not yet produced a standalone rigid BLAS.

Residency wireframe viewer update:

- Mode 21/22 now appends resident rigid boxes from
  `RtSmokeGeometryUniverse::CollectRigidResidencyBoundsBoxes()` after the
  portal residency pass.
- This uses the same PT bounds overlay line buffer as the drawSurf mirror, so
  it appears in the existing mode 22 wireframe view.
- Color meanings for resident boxes:

       green   = visible this frame, resident, route-ready
       cyan    = cache-only resident, route-ready
       yellow  = resident but missing BLAS
       magenta = resident problem/fallback

- The critical visual proof is cyan boxes remaining when raster-visible boxes
  disappear around portal/occlusion edges.

Follow-up correction after first visual test:

- The first mode 22 run still mostly showed the old drawSurf behavior because
  `enableRigidRouteForMode` did not include modes 21/22. Switching from preset
  mode 20 to debug mode 22 disabled the rigid residency update.
- Fixed this by allowing modes 21/22 to enable rigid residency for visualization
  when `r_pathTracingRigidResidency 1` is set. This does not make mode 22 a
  shading route; it only populates the resident bounds overlay.
- Added `r_pathTracingSceneBoundsOverlay 3` as resident-rigid-only mode so the
  old raster-visible boxes can be removed from the view.

Recommended visual test:

       r_pathTracingMode20TestPreset 3
       r_pathTracingDebugMode 22
       r_pathTracingSceneBoundsOverlay 3
       r_pathTracingSceneBoundsOverlayMax 8

Move around a corner or portal edge where mode 22 previously showed boxes being
culled. Cache-only resident rigid geometry should remain visible as cyan even
when the old raster-visible drawSurf boxes would have disappeared.

Safety correction:

- Tests with `r_pathTracingSceneBoundsOverlayMax 256`, then 32, partially
  worked but caused GPU device removal after a few seconds. Mode 22 evaluates
  wireframe boxes in a full-screen PT shader loop.
- Resident overlay bounds now use Doom's `R_LocalPointToGlobal()` transform
  path and validate box coordinates/extents before upload. This was added after
  a low box count still caused device removal, which pointed at malformed or
  pathological resident boxes rather than only raw line count.
- The resident/static overlay safety cap is now 64 boxes. Start tests lower
  (`16`) and raise to `64` only after stability is confirmed.

Follow-up after resident-rigid visual test:

- Tester result: portals now visibly gate the resident selection, rigid objects
  survive full occlusion and turn cyan, characters are absent from this view,
  and level geometry is absent from this view.
- Interpretation: this is expected for `r_pathTracingSceneBoundsOverlay 3`.
  Mode 3 is resident-rigid-only. Cyan is the desired proof that cached rigid
  objects are still resident after raster/occlusion culling removes their
  drawSurfs. Skinned/deforming actors and static level surfaces are not part of
  that overlay category.
- Added static geometry cache visualization modes:

       r_pathTracingSceneBoundsOverlay 3 = resident rigid only
       r_pathTracingSceneBoundsOverlay 4 = static cache only
       r_pathTracingSceneBoundsOverlay 5 = static cache + resident rigid

- Static cache boxes are computed from the persistent static smoke vertex
  ranges. Grey means the static surface was seen this frame; cyan means the
  static surface is cache-only this frame. This viewer tests whether level
  surfaces remain in the static cache/BLAS path when no longer raster-visible.

Recommended follow-up visual tests:

       r_pathTracingMode20TestPreset 3
       r_pathTracingDebugMode 22
       r_pathTracingSceneBoundsOverlay 3
       r_pathTracingSceneBoundsOverlayMax 16

Then test static level cache:

       r_pathTracingSceneBoundsOverlay 4
       r_pathTracingSceneBoundsOverlayMax 16

If stable, raise `r_pathTracingSceneBoundsOverlayMax 64`.

Follow-up correction:

- Overlay mode 4 originally walked static cache records in insertion order. Old
  cached records from the previous cell could consume the first 16/64 boxes and
  hide newly selected/preloaded static records from the viewer.
- Static cache bounds collection now emits `seenThisFrame` records first, then
  cache-only records. This makes mode 4 answer "what did the current selected
  static preload touch this frame?" before showing older retained cache entries.
- Added `r_pathTracingSceneBoundsOverlay 6` to invert that ordering: cache-only
  static records first, then current selected/preloaded records. Use this when
  testing whether a previously preloaded cell is still retained after the
  player moves into another cell.
- The current+1 area walkers for rigid residency and static preload now count
  `PS_BLOCK_VIEW` portal edges but do not stop traversal on them. This matches
  the RT goal better: Doom's portal visibility/blocking state is diagnostic
  input, not authority for whether adjacent-cell geometry may exist in the ray
  scene. The dump still reports blocked edges so we can see when this happened.

Retest:

       r_pathTracingMode20TestPreset 3
       r_pathTracingStaticAreaPreloadDump 1
       r_pathTracingRigidResidencyDump 1
       r_pathTracingDebugMode 22
       r_pathTracingSceneBoundsOverlay 4
       r_pathTracingSceneBoundsOverlayMax 64
       condump static_area_preload_portals

Cache-retention viewer:

       r_pathTracingSceneBoundsOverlay 6
       r_pathTracingSceneBoundsOverlayMax 64

Expected change:

- Adjacent-cell static boxes should now be visible in mode 4 if the scene
  universe assigned area membership to those surfaces.
- Previously preloaded static boxes should remain visible in mode 6 after
  leaving their cell if they are still retained in the static cache.

Mode 20 BVH/Emissive Validation Alignment
=========================================

Mode 20 is now the preferred quick correctness test for whether the latest RT
scene inputs are actually feeding rays, not just the bounds viewer:

- Static geometry rays use the static cache/BLAS. With
  `r_pathTracingStaticAreaPreload 1`, that cache is seeded from current Doom
  area plus portal-adjacent static scene-universe surfaces.
- Rigid props use the routed rigid TLAS path when
  `r_pathTracingRigidRouteMode20 1` and `r_pathTracingRigidTlasRoute 1` are
  enabled.
- Static emissive triangles are built from the same static cache.
- Routed rigid emissive triangles are appended to the mode 20 emissive
  inventory from rigid route buffers.
- The light universe area selector now matches geometry traversal policy:
  `PS_BLOCK_VIEW` portal edges are counted in diagnostics but do not stop
  current+1 traversal.

Validation command:

       r_pathTracingMode20TestPreset 4
       r_pathTracingLightUniverseDump 1
       r_pathTracingStaticAreaPreloadDump 1
       r_pathTracingRigidResidencyDump 1
       condump mode20_bvh_validation

Interpretation:

- Preset 4 keeps the source3 static preload, resident rigid route, rigid
  emissive append, and light-area diagnostics, but disables render-affecting
  `r_pathTracingLightAreaFilterApply`. This avoids mistaking intentional
  light-list culling for missing BVH geometry.
- If mode 20 rays/lighting clearly hit the adjacent visible cell while overlay
  mode 4/6 is confusing, the bounds viewer is the problem.
- If mode 20 also behaves as if the adjacent cell is absent, the issue is in
  static area preload / selected area membership / static BLAS rebuild, not only
  the viewer.

Follow-up dump result:

- Tester ran preset 4 and reported:

       static level emissives that previously failed when occluded now stay
       occlusion/off-camera still breaks rigid-object emissives
       sub-cell/portal issues remain disruptive
       some distant doors disappear

- Dump interpretation:

       applied preset 4 correctly: lightAreaApply=0 bvhValidation=1
       staticCache records=552 before/after, so static cache is retained
       static area preload currentArea=30 selected area 30 + area 32
       rigid residency resident=53 seen/cache=3/50 routeReady=53 missing=0/0
       light universe dynSeen dropped 56 -> 32 while rigid residents remained

- Conclusion:

       static cache + static emissive inventory is now partly working
       rigid geometry residency is selecting cache-only route-ready objects
       rigid emissive contribution is still visibility-dependent somewhere after residency

- The active fault line is now routed rigid emissive inventory/persistence, not
  simply "rigid object absent from TLAS." We need a diagnostic that reports:

       routed rigid instances considered
       cache-only routed rigid instances
       emissive triangles emitted per routed instance/material
       skipped non-emissive / missing material table index / capped / invalid reasons

- Specific sample of interest from the dump:

       model='models/mapobjects/base/misc/emerlight.ase'
       material='models/mapobjects/base/misc/emerlight_light'
       selected=1 seen=0 routeReady=1 area=32

  This object is resident and route-ready while off-camera, but its emissive
  contribution still disappears. That is the best first target for routed rigid
  emissive diagnostics.

External idTech4 portal research note:

- User supplied `C:/Users/lizard/Downloads/idtech4_bvh_findings_for_coding_agent.txt`.
- Treat it as context only. It correctly warns not to use raw BSP leaf/internal
  cells as the RT visibility unit. Use Doom renderer portal areas.
- Our current source3 scaffolding already uses renderer area APIs:

       renderWorld->PointInArea(...)
       renderWorld->NumPortalsInArea(...)
       renderWorld->GetPortal(...)

- Therefore the external note does not supersede the current architecture. It
  is useful support for staying on portal areas, but it does not explain the
  latest rigid emissive failure by itself.

- The note recommends respecting `PS_BLOCK_VIEW` after basic adjacency testing.
  Current source3 diagnostics intentionally count but do not block on
  `PS_BLOCK_VIEW` while we validate RT residency. This remains experimental and
  may need refinement later, but it is not the current rigid emissive drop cause
  because preset 4 had `lightAreaApply=0`.

Critical Portal-Depth Correction
================================

Tester rechecked Doom 3 portal layout and found the previous assumption was too
conservative:

- Many visually single rooms are actually multiple renderer portal areas.
- A visible neighboring "room" may be several portal-area hops away.
- Current+1 is therefore not a reliable RT residency/preload scope.
- At least 4 portal levels is probably a more realistic validation value for
  Mars City style areas.

This matches previous mode 20 dump data:

       portalSweep areas=1/2/3/5/9
       portalSweep merged=42/48/48/75/78
       portalDepthBins depth0/1/2/3/4/>4 = 42/6/0/27/3/110

Interpretation:

- Depth 1 only added a small amount of candidate geometry/light data.
- Depth 3 produced the first large jump in merged candidates.
- Depth 4 is a better near-term validation baseline than depth 1.
- This likely explains some apparent cell/portal failures and why visually
  nearby geometry or emissives still disappeared even when current+1 was active.

Recommended next validation command:

       r_pathTracingMode20TestPreset 4
       r_pathTracingStaticAreaPreloadPortalSteps 4
       r_pathTracingRigidResidencyPortalSteps 4
       r_pathTracingLightAreaPortalSteps 4
       r_pathTracingStaticAreaPreloadDump 1
       r_pathTracingRigidResidencyDump 1
       r_pathTracingLightUniverseDump 1
       condump mode20_depth4_validation

Decision point:

- If depth 4 fixes missing cells/emissives without severe perf regression,
  update preset 4 to use depth 4 by default for BVH validation.
- Keep normal/default CVars conservative until performance and correctness are
  better understood.
- `blockedEdges` may be non-zero, but those edges should not prevent selected
  adjacent areas from contributing static or rigid residency records.

Selected-area diagnostic follow-up:

- Tester suspects the one-step portal walker may be selecting a utility or
  trigger-like volume instead of the visually expected neighboring room, or
  that Doom's portal viewer is not showing the same graph used by
  `NumPortalsInArea/GetPortal`.
- Added per-selected-area static preload dump lines:

       PT static area preload area[i] area=A depth=B portalEdges=C blockedEdges=D surfaces=E triangles=F emissiveSurfaces=G bounds=(...)-(...)

- Use these lines to distinguish a real geometry cell from a tiny/empty
  transition area:

       surfaces/triangles high = likely real static geometry cell
       surfaces/triangles zero or tiny = likely utility/transition/empty cell
       bounds tiny or far from visible neighbor = likely not the expected room

- If depth 1 selects a tiny intermediate area, depth 2 may be necessary in that
  doorway type, or we need a geometry-weighted portal traversal that can skip
  empty/utility areas while still stopping before excessive whole-map preload.


Static Area Preload Correction
==============================

Tester result from overlay mode 4:

- Static cache did not populate level geometry beyond the portal cell that Doom
  raster submitted.
- This confirmed that the source3 static cache was still seeded by
  `viewDef->drawSurfs`, so Doom's own portal/area frontend could prevent
  neighboring static cells from ever entering the RT static cache/BLAS.
- Sliding-door portals can be visually see-through in gameplay, but Doom may
  still omit the neighboring cell's drawSurfs/BVH inputs until its frontend
  decides the cell is active.

Implemented correction:

- Added `r_pathTracingStaticAreaPreload`.
- Added `r_pathTracingStaticAreaPreloadPortalSteps`.
- Added `r_pathTracingStaticAreaPreloadDump`.
- Mode 20 test preset now enables static area preload with portal depth 1.
- Source3 now calls `RtPathTraceSceneUniverse::BuildSelectedStaticGeometry()`
  after the visible static drawSurf capture. This uses the scene-universe static
  map inventory only as an area-selected static-world preload, not as the
  primary live scene producer.
- The preload appends or touches static-world surfaces whose area membership
  intersects current player area plus selected portal steps. Already counted
  visible static surfaces are not double-counted for the frame.
- Dynamic/rigid/skinned behavior is unchanged:

       static world = selected-area preload + existing static cache/BLAS
       rigid props  = drawSurf mesh cache + portal residency/TLAS route
       skinned      = still raster-visible dynamic fallback for now

Build status:

- Release/PT build passed after static area preload.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Recommended static preload test:

       r_pathTracingMode20TestPreset 3
       r_pathTracingStaticAreaPreloadDump 1
       r_pathTracingDebugMode 22
       r_pathTracingSceneBoundsOverlay 4
       r_pathTracingSceneBoundsOverlayMax 16
       condump static_area_preload

Interpretation:

- The dump should include:

       PT static area preload currentArea=... portalSteps=1 selectedAreas=...
       selectedSurfaces=A/B built surfaces=C triangles=D ...

- In overlay mode 4, static level boxes should now include current cell plus
  one portal-adjacent cell even when Doom raster/occlusion hides those surfaces.
- Grey means the static surface was also raster-seen this frame; cyan means the
  static surface is cached/preloaded but not raster-visible this frame.
- If neighboring static geometry still does not appear, inspect
  `selectedSurfaces`, `skipped invalid/limits/zero`, and whether the surface has
  valid area membership in the scene-universe diagnostics.


Autocompact Guard: Current Critical State 6
===========================================

Read this block first after compaction.

Branch/state:

- Branch: `codex/refactor-pathtrace-smoke-modules`.
- Latest committed base:

       87254b9d pt: add light area filter impact diagnostics

- Current uncommitted work includes two stacked diagnostics/production slices:

       active light churn diagnostic
       rigid geometry portal residency scaffold
       resident rigid mode 21/22 wireframe overlay
       static area preload for source3 static cache
       mode 20 BVH validation preset 4
       selected-area diagnostics
       cache-only static overlay mode 6

- Expected touched files:

       docs/pathtrace_scene_producer_build_log_2.md
       docs/pathtracing.txt
       neo/renderer/NVRHI/PathTraceCVars.cpp
       neo/renderer/NVRHI/PathTraceCVars.h
       neo/renderer/NVRHI/PathTraceDrawSurfCapture.cpp
       neo/renderer/NVRHI/PathTraceGeometryUniverse.cpp
       neo/renderer/NVRHI/PathTraceGeometryUniverse.h
       neo/renderer/NVRHI/PathTraceLightUniverse.cpp
       neo/renderer/NVRHI/PathTraceLightUniverse.h
       neo/renderer/NVRHI/PathTraceSceneUniverse.cpp
       neo/renderer/NVRHI/PathTraceSceneUniverse.h
       neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp

- Build/copy succeeded for the full uncommitted stack.

Core architectural correction:

- DrawSurf capture is freshness, not RT residency authority.
- Source3 rigid route should keep all cached rigid geometry from current Doom
  area plus selected portal steps resident in the BVH/TLAS. One step is too
  shallow for many visually single Doom 3 rooms; depth 4 is the current
  validation baseline.
- Source3 static cache should be populated from current Doom area plus selected
  portal steps, independent of raster drawSurf submission. Raster is no longer
  sufficient as the static-world seed. One step is too shallow for many test
  locations.
- `PS_BLOCK_VIEW` portal edges are counted in dumps but no longer stop source3
  current+1 static preload or rigid residency traversal. This is intentional
  until a stronger Doom portal taxonomy is available.
- Light area filtering now also counts but does not stop on `PS_BLOCK_VIEW`.
  Preset 4 disables render-affecting light-area filter apply so mode 20 can
  validate BVH/cache inputs without intentional light-list culling.
- Current validated status:

       static level emissives improved and persist under occlusion
       rigid geometry can remain resident/cache-only
       rigid emissives still disappear off-camera/occluded

- Current active suspect:

       depth-4 validation reported resident=156 routeReady=156, but the rigid
       route path was still capped at 64 for material IDs, route buffers, and
       TLAS descriptors. Mode 20 removes routed-ready rigid dynamic fallback,
       so any resident rigid instance beyond that cap could disappear and its
       emissive triangles would not enter the mode 20 rigid emissive append.

Latest patch:

- Added `r_pathTracingRigidRouteMaxInstances` default 64.
- Mode 20 test preset 4 raises `r_pathTracingRigidRouteMaxInstances` to at
  least 256.
- Replaced the three hard-coded rigid route caps with the new cvar:

       CollectRigidRouteMaterialIds
       BuildRigidRouteBuffers
       BuildRigidTlasInstanceDescs

- Raised `PathTraceSmokeTLAS` creation capacity from 128 to 512 instances so
  depth-4 resident rigid routing does not exceed TLAS creation limits.

Build status:

- Release/PT build passed after the rigid route cap patch.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Current test request:

       r_pathTracingMode20TestPreset 4
       r_pathTracingStaticAreaPreloadPortalSteps 4
       r_pathTracingRigidResidencyPortalSteps 4
       r_pathTracingLightAreaPortalSteps 4
       r_pathTracingRigidRouteMaxInstances 256
       r_pathTracingStaticAreaPreloadDump 1
       r_pathTracingRigidResidencyDump 1
       r_pathTracingLightUniverseDump 1
       condump mode20_depth4_route256

Visual residency test:

       r_pathTracingMode20TestPreset 3
       r_pathTracingDebugMode 22
       r_pathTracingSceneBoundsOverlay 3
       r_pathTracingSceneBoundsOverlayMax 16

Static cache visual test:

       r_pathTracingMode20TestPreset 3
       r_pathTracingSceneBoundsOverlay 4
       r_pathTracingSceneBoundsOverlayMax 64

Cache-only static retention visual test:

       r_pathTracingSceneBoundsOverlay 6
       r_pathTracingSceneBoundsOverlayMax 64

Color meanings:

       resident rigid green = visible this frame + resident + route-ready
       resident rigid cyan = cache-only resident + route-ready
       resident rigid yellow = resident but missing BLAS
       resident rigid magenta = resident problem/fallback
       static grey = static cache surface seen this frame
       static cyan = static cache surface not seen this frame

Key fields:

       PT rigid residency ... currentArea=A selectedAreas=B visibleRigid=C cachedRigid=D resident=E seen/cache=F/G routeReady=H skipped outside/unknown=I/J
       RT smoke light universe ...
       RT smoke light churn ...

Next decision:

- Do not start a broad portal/BVH rewrite from the external note.
- Do not assume current+1 is enough. User corrected this after rechecking
  portals: visually single rooms commonly span multiple portal areas. Depth 4
  should be tested before chasing deeper architectural faults.
- Next small check should verify whether `r_pathTracingRigidRouteMaxInstances
  256` eliminates the distant rigid object disappearance and most/all rigid
  emissive loss. If not, add routed rigid emissive append diagnostics showing
  emitted/capped/skipped cache-only resident emissive instances.


Rigid Route Emissive Shadow Fix
===============================

Input dump:

       mode20_depth4_route256.txt

Result:

- Preset 4 applied correctly:

       source3=1 rigidRoute=1 rigidResidency=1 staticPreload=1
       lightAreaApply=0 bvhValidation=1 churn=1 rigidRouteMax=256

- Light universe area filter was not the drop source in this dump:

       selected=342 wouldUpload=342 wouldDrop=0
       churn previous/current/stayed/entered/left=342/342/342/0/0

- This location had only current-frame visible resident rigid objects:

       resident=36 seen/cache=36/0 routeReady=36

  So it did not directly prove cache-only rigid emissive persistence, but it
  did confirm the route cap patch was active.

Shader fault found:

- `ClosestHit` already handled routed rigid instances with `InstanceID() >= 2`.
- The shadow `AnyHit` path exited immediately for `InstanceID() >= 2`, before
  applying the shadow ray's "ignore the sampled emissive primitive/material"
  rule.
- That meant routed rigid emissive samples could be shadowed by their own routed
  rigid triangles or same-material light triangles, while static/dynamic
  emissives had the intended ignore behavior.

Patch:

- Added routed-rigid-aware material lookup helpers in
  `pathtrace_smoke.rt.hlsl`.
- `AnyHit` now checks `shadowIgnoreInstanceId`, `shadowIgnorePrimitiveIndex`,
  and `shadowIgnoreMaterialId` before accepting routed rigid hits as opaque
  occluders.
- Routed rigid hits still skip alpha/decal filtering in `AnyHit` for now; they
  remain opaque occluders except for the sampled emissive/material ignore.

Build status:

- Release/PT build passed.
- ShaderMake recompiled the path tracing smoke RT shader library for DXIL and
  SPIR-V.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Current retest request:

       r_pathTracingMode20TestPreset 4
       r_pathTracingStaticAreaPreloadPortalSteps 4
       r_pathTracingRigidResidencyPortalSteps 4
       r_pathTracingLightAreaPortalSteps 4
       r_pathTracingRigidRouteMaxInstances 256
       r_pathTracingStaticAreaPreloadDump 1
       r_pathTracingRigidResidencyDump 1
       r_pathTracingLightUniverseDump 1
       condump mode20_depth4_rigid_emissive_shadowfix

Expected outcome:

- Rigid emissive objects that are routed and resident should contribute more
  like static/dynamic emissives when occluded/off-camera.
- If rigid emissives still fail, next patch should add per-routed-rigid
  emissive append diagnostics:

       route instances considered
       route emissive instances/materials found
       triangles emitted
       cache-only vs seen routed emissive counts
       capped/skipped invalid/material-table reasons


Routed Rigid Emissive Append Diagnostic
=======================================

Tester result after the shadow ignore patch:

       still doing it sadly

Interpretation:

- The shader self-shadow/material-ignore fix was not sufficient.
- The next fault line is whether the failing rigid emissive object actually
  reaches the routed rigid emissive append when it is occluded/off-camera, and
  whether it is present as a visible instance or cache-only resident instance.

Patch:

- Added `r_pathTracingRigidRouteEmissiveDump`.
- The route build now records per-emitted-instance seen/cache state.
- The emissive inventory dump now prints:

       RT smoke routed rigid emissives routeInstances=A seen/cache=B/C
       emissiveInstances=D seen/cache=E/F
       triangles considered/captured/capped=G/H/I
       nonEmissive=J invalid=K area=L weight=M

- `r_pathTracingRigidRouteEmissiveDump 1` uses the same dump path as
  `r_pathTracingEmissiveInventoryDump 1` but is named for this specific fault.

Build status:

- Release/PT build passed after the diagnostic patch.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Current retest request:

       r_pathTracingMode20TestPreset 4
       r_pathTracingStaticAreaPreloadPortalSteps 4
       r_pathTracingRigidResidencyPortalSteps 4
       r_pathTracingLightAreaPortalSteps 4
       r_pathTracingRigidRouteMaxInstances 256
       r_pathTracingRigidRouteEmissiveDump 1
       r_pathTracingRigidResidencyDump 1
       r_pathTracingLightUniverseDump 1
       condump mode20_rigid_emissive_append_diag

Decision after dump:

- If `emissiveInstances seen/cache` drops to zero for cache-only residents, the
  append/build path is still visibility-bound.
- If cache-only routed emissive instances and triangles are present but the
  object still does not illuminate, the issue is downstream in light universe
  persistence, reservoir sampling, or visibility evaluation rather than append.


Mode 20 Reservoir Candidate Trials
==================================

Input dump:

       mode20_rigid_emissive_twosided_diag.txt

Critical result:

- Two-sided reservoir emissives did not fix the failing off-screen panels.
- The routed rigid emissive append is working:

       routeInstances=83 seen/cache=3/80
       emissiveInstances=17 seen/cache=0/17
       triangles considered/captured/capped=156/156/0
       area=4383.13 weight=4383.127

- The failing `emerlight.ase` panels are cache-only resident and route-ready in
  both views.
- Light universe churn is zero in both views.

Interpretation:

- The failure is not BVH residency, not route append, not material capture, and
  not two-sided facing.
- Mode 20 still uses a single global emissive triangle sample per pixel. In the
  off-camera dump, all routed rigid emissives together are only about 4 percent
  of total emissive sampling weight:

       routed rigid weight 4383 / total weight 100935

- Looking directly at a panel shows its surface emissive. Once it is just
  off-screen, it depends on the one-candidate reservoir path selecting it or a
  nearby related triangle. That can look like binary screen-space behavior even
  when the light is present in the inventory.

Patch:

- Added `r_pathTracingReservoirCandidateTrials`, default 1, clamp 1..16.
- Mode 20 now draws N weighted emissive candidates per pixel and keeps the one
  with highest local unoccluded potential:

       area * ndotl * lightFacing / distance^2 / pdf

- It still traces one shadow ray for the selected candidate, so this is a
  limited diagnostic/RIS-style improvement rather than full ReSTIR.

Build status:

- Release/PT build passed.
- ShaderMake recompiled the path tracing smoke RT shader library for DXIL and
  SPIR-V.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Current retest request:

       r_pathTracingMode20TestPreset 4
       r_pathTracingStaticAreaPreloadPortalSteps 4
       r_pathTracingRigidResidencyPortalSteps 4
       r_pathTracingLightAreaPortalSteps 4
       r_pathTracingRigidRouteMaxInstances 256
       r_pathTracingReservoirTwoSidedEmissives 1
       r_pathTracingReservoirCandidateTrials 8
       r_pathTracingRigidRouteEmissiveDump 1
       r_pathTracingRigidResidencyDump 1
       r_pathTracingLightUniverseDump 1
       condump mode20_reservoir_trials8

If 8 helps but is too expensive, test 4. If 8 does not materially change the
off-screen panel contribution, test 16 once to separate candidate starvation
from actual visibility/shadow failure.


Rigid Emissive Card Promotion
=============================

Input result:

- `r_pathTracingReservoirCandidateTrials` had no visible impact on the two
  failing vending-machine panels.
- Comparing the mode 20 dumps showed the routed-rigid emissive append stayed
  stable:

       routeInstances=83 seen/cache=4/79 or 3/80
       emissiveInstances=17 seen/cache=0/17
       triangles considered/captured/capped=156/156/0

- The materials that disappeared between the direct view and the off-camera
  view were visible dynamic emissives, not routed rigid emissives:

       models/mapobjects/filler/snackmachine_snacks
       models/mapobjects/filler/sodamachinesq_add2
       models/mapobjects/filler/sodamachine_add2
       models/mapobjects/filler/sodamachine_add

Interpretation:

- This is not reservoir candidate starvation for the routed rigid inventory.
- These vending-machine panels are likely entity-attached translucent/additive
  signage cards. `ClassifySmokeSurface` classifies translucent/sorted materials
  as `ParticleAlpha` before the rigid-entity branch, so they never become rigid
  mesh candidates and disappear from the light universe when raster stops
  submitting them.

Patch:

- Added `r_pathTracingRigidRouteEmissiveCards`, default 0.
- Source3 drawSurf mirror and dynamic-frame capture now apply a narrow
  promotion gate:
  - must already classify as `ParticleAlpha`
  - must be attached to an entity
  - must be non-skinned, non-deforming, non-callback, non-generated, static
    model geometry
  - must not be GUI, particle-looking, glass, post-process, screen-texgen, or
    depth-hack material
  - material classifier must look like signage/glow and have additive or
    ambient emissive-card style stages
- Promoted surfaces become `RigidEntity` for source3 mesh/instance observation,
  rigid candidate recording, dynamic fallback removal, and routed rigid
  emissive append.
- Mode 20 test presets now enable `r_pathTracingRigidRouteEmissiveCards 1`.

Build status:

- Release/PT build passed.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Expected retest signal:

- With the failing vending panels off-camera, `r_pathTracingRigidRouteEmissiveDump
  1` should show routed rigid emissive triangle count above the previous 156 if
  those panels match the conservative gate.
- If the count does not rise, use `r_pathTracingCrosshairMaterialDump 1` while
  aiming at the panel to inspect which classifier/reject condition is too
  strict.

Current retest request:

       r_pathTracingMode20TestPreset 4
       r_pathTracingStaticAreaPreloadPortalSteps 4
       r_pathTracingRigidResidencyPortalSteps 4
       r_pathTracingLightAreaPortalSteps 4
       r_pathTracingRigidRouteMaxInstances 256
       r_pathTracingRigidRouteEmissiveDump 1
       r_pathTracingLightUniverseDump 1
       condump mode20_rigid_emissive_cards


Rigid Emissive Card Classifier Tightening
=========================================

User test result:

- The rigid emissive-card promotion fixed the two failing vending-machine
  panels.
- It also broke other materials because `_add...0200` generated texture
  variants are supposed to stay banned from the translucent/signage material
  classifier path.

Patch:

- Added `RtSmokeTranslucentClassifierInfo::hasAddDefault0200Texture`.
- `BuildSmokeTranslucentClassifierInfo` now scans material stage image names
  for `_add` plus a bounded `0200` generated texture code, including
  `#__0200` style names.
- The flag is rejected by:
  - translucent subtype signage/glow classification
  - translucent cutout alpha allowance
  - additive decal / white-key / RGB-keyed decal classifier helpers
  - translucent overlay-card texture discovery
  - emissive image discovery
  - scene-capture and scene-universe emissive name heuristics
  - source3 rigid emissive-card promotion
- Crosshair and translucent diagnostic dumps now print `addDefault0200`.

Build status:

- Release/PT build passed.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Current retest request:

       r_pathTracingMode20TestPreset 4
       r_pathTracingStaticAreaPreloadPortalSteps 4
       r_pathTracingRigidResidencyPortalSteps 4
       r_pathTracingLightAreaPortalSteps 4
       r_pathTracingRigidRouteMaxInstances 256
       r_pathTracingRigidRouteEmissiveDump 1
       r_pathTracingLightUniverseDump 1
       condump mode20_add0200_classifier_fix


Mode 18 New-Stack Preset
========================

Goal:

- Tester needs mode 18 visually connected to the current source3/static
  preload/rigid residency/routed rigid stack without entering the full CVar
  list manually.

Patch:

- Added `r_pathTracingMode18TestPreset`, default 0.
- Added shared routed-scene preset helper used by both mode 18 and mode 20
  presets.
- `r_pathTracingMode18TestPreset 1` applies:

       r_pathTracingDebugMode 18
       r_pathTracingSceneSource 3
       r_pathTracingRigidBlasGpuScaffold 1
       r_pathTracingRigidBlasGpuBuild 1
       r_pathTracingRigidTlasRoute 1
       r_pathTracingRigidRouteMode18 1
       r_pathTracingRigidRouteRemoveDynamic 1
       r_pathTracingRigidRouteEmissiveCards 1
       r_pathTracingRigidResidency 1
       r_pathTracingStaticAreaPreload 1

- `r_pathTracingMode18TestPreset 4` also sets:

       r_pathTracingStaticAreaPreloadPortalSteps 4
       r_pathTracingRigidResidencyPortalSteps 4
       r_pathTracingLightAreaPortalSteps 4
       r_pathTracingRigidRouteMaxInstances >= 256

Notes:

- Mode 18 already had low-level routed rigid integration through
  `r_pathTracingRigidRouteMode18`; the new patch makes it easy to activate the
  same current validation stack as mode 20.
- Mode 18 does not use the mode 20 reservoir direct-light path. It is for
  visual checking of routed geometry, static preload, rigid residency, point
  light shadows, surface emissives, and one-bounce toy PT behavior.

Build status:

- Release/PT build passed.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Current retest request:

       r_pathTracingMode18TestPreset 4
       r_pathTracingRigidResidencyDump 1
       r_pathTracingStaticAreaPreloadDump 1
       condump mode18_new_stack_visual


Routed Rigid Emissive Stage State Fix
=====================================

Input dump:

       mode18broken_light.txt

Observed material:

       models/mapobjects/swinglights/work/swinglighttex2
       class=particle/alpha subtype=signage/glow
       ambientBlend=1 nameGlow=1
       stage[0] condition=1.000

Problem:

- The mode 18 new-stack preset routed this interactive, game-logic-driven
  swing light through the rigid route.
- The routed rigid closest-hit shader hardcoded:

       triangleClassAndFlags = 1

  for all routed rigid hits.
- That discarded `RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF`, which is how the
  normal drawSurf dynamic path carries material stage condition/off state into
  shading and emissive inventory.
- Result: condition-driven routed emissive cards could get stuck visually on.

Patch:

- `SmokeDrawSurfaceHasActiveEmissiveStage()` is now shared instead of local to
  `PathTraceSceneCapture.cpp`.
- Source3 rigid mesh candidate observations now record
  `triangleClassAndFlags = rigidClass | emissiveStageOffFlag`.
- Rigid mesh candidate records update this flag every time the drawSurf is
  observed, so visible game-logic toggles can refresh the route state without
  rebuilding BLAS.
- `PathTraceRigidRouteInstance` now carries `triangleClassAndFlags`.
- Rigid route closest-hit and `LoadSmokeTriangleClassAndFlags()` now read that
  value instead of hardcoding active rigid class.
- Routed rigid emissive inventory skips route instances whose
  `triangleClassAndFlags` contains `RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF`.

Build status:

- Release/PT build passed.
- ShaderMake rebuilt the path tracing smoke RT shader library for DXIL and
  SPIR-V.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Current retest request:

       r_pathTracingMode18TestPreset 4
       r_pathTracingCrosshairMaterialDump 1
       condump mode18_swinglight_stage_state

Retest the swing light turning on/off. If it still sticks on while the crosshair
dump shows `condition=0.000`, then route state is still stale. If it sticks on
only after the drawSurf disappears entirely, the remaining issue is resident
cache invalidation for unobserved game-logic-controlled emissive cards.


Routed Rigid Stage-State Revert
===============================

User test result:

- Carrying `triangleClassAndFlags` through `PathTraceRigidRouteInstance` broke
  almost every surface in the game.

Immediate recovery:

- Reverted the GPU-facing route instance layout change.
- Reverted routed rigid closest-hit / class-flag reads back to the previous
  compact behavior:

       routed rigid hit => surfaceClass=RigidEntity, triangleClassAndFlags=1

- Reverted CPU route records/build buffers back to not carrying
  `triangleClassAndFlags`.
- Kept the mode 18 test preset and the vending-machine rigid emissive-card
  promotion.
- Added a narrow promotion exclusion for material names containing
  `swinglight`, keeping the known game-logic-driven swing light on the dynamic
  drawSurf path where it previously toggled correctly.

Build status:

- Release/PT build passed.
- ShaderMake rebuilt the path tracing smoke RT shader library for DXIL and
  SPIR-V.
- Built exe copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Current retest request:

       r_pathTracingMode18TestPreset 4
       r_pathTracingCrosshairMaterialDump 1
       condump mode18_swinglight_dynamic_recovery

Expected:

- General surfaces should recover from the route layout break.
- The swing light should no longer be promoted into routed rigid; it should show
  as `particle/alpha` / `signage/glow` on the dynamic path and keep its game
  logic on/off behavior.


Autocompact Guard: Current Critical State 6
===========================================

Read this block first after compaction.

Current user-reported state:

- `r_pathTracingMode18TestPreset 4` connected mode 18 to the current new stack.
- The first attempt to fix the interactive `swinglighttex2` stuck-on issue by
  adding `triangleClassAndFlags` to `PathTraceRigidRouteInstance` broke almost
  every surface in the game.
- That unsafe GPU route instance layout/shader change has been reverted.
- A recovery Release/PT build was made and copied to:

       E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Current intended behavior:

- Keep:
  - mode 18 test preset
  - source3 static preload
  - rigid residency
  - rigid route
  - vending-machine rigid emissive-card promotion
  - `_add + 0200` classifier exclusion
- Do not reintroduce the routed rigid `triangleClassAndFlags` GPU payload until
  the route struct/binding/shader ABI is handled deliberately.
- Current narrow recovery guard: material names containing `swinglight` are not
  promoted as rigid emissive cards, so that known game-logic-driven light stays
  on the dynamic drawSurf path where its on/off state was known to work.

Important diagnosis:

- `mode18broken_light.txt` showed:

       material='models/mapobjects/swinglights/work/swinglighttex2'
       class=particle/alpha subtype=signage/glow
       coverage=translucent sort=4.00 deform=none
       ambientBlend=1 nameGlow=1
       stage[0] condition=1.000

- The real issue is not the material name. It is that this is a material whose
  relevant stage condition/color is driven through evaluated shader registers,
  likely entity `shaderParms` / game logic.
- Rigid residency can keep geometry around while raster stops submitting it, but
  it does not yet have a safe per-frame material-register/state channel for
  cached/off-screen instances.

Recommended next implementation:

- Replace the temporary `swinglight` material-name exclusion with a general
  dynamic-register/material-parm detector.
- Best target:

       idMaterial helper:
       stage/register depends on EXP_REG_PARM*, EXP_REG_GLOBAL*, EXP_REG_TIME,
       EXP_REG_SOUND, or other non-constant expression input

- Then `PtMirrorCanPromoteRigidEmissiveCard()` should reject emissive/signage
  cards when the relevant ambient/additive stage condition or color registers
  are dynamic.

Expected policy after that:

       stable rigid signage/glow card -> promote/resident
       _add + 0200 -> reject by classifier
       parm/time/sound/global-driven emissive card -> keep dynamic drawSurf path

Testing commands after recovery/current build:

       r_pathTracingMode18TestPreset 4
       r_pathTracingCrosshairMaterialDump 1
       condump mode18_swinglight_dynamic_recovery

For vending regression check:

       r_pathTracingMode20TestPreset 4
       r_pathTracingRigidRouteEmissiveDump 1
       r_pathTracingLightUniverseDump 1
       condump mode20_vending_after_swinglight_exclusion
