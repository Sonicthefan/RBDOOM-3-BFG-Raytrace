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

- It also prints up to four top connected-overflow samples by candidate weight:

       RT smoke light area overflow sample N area=A material=M materialIndex=I weight=W distance=D

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


Autocompact Guard: Current Critical State 4
===========================================

Read this block first after compaction.

Branch/state:

- Branch: `codex/refactor-pathtrace-smoke-modules`.
- Latest committed base before this pending commit:

       02e4a524 pt: route rigid BLAS instances into smoke modes
       120d70c9 pt: add emissive light area selector diagnostics

- Current pending commit should include only:

       r_pathTracingMode20TestPreset helper
       r_pathTracingLightAreaOverflowMax default 512
       docs/build-log updates for preset and greedy overflow policy

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
       condump light_area_filter_apply

Expected:

       areaFilter enabled/applied=1/1
       runtimeActive == wouldUpload
       only disconnected/unknown or overflow beyond cap should be dropped
