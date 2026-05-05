RBDoom 3 BFG PT Scene Producer Build Log
========================================

Date: 2026-05-05

Branch: `codex/refactor-pathtrace-smoke-modules`

Purpose
-------

This is a focused build log for the current scene producer replacement slice.
It records what was implemented, what failed, what was corrected, and what the
latest diagnostics proved. It exists to prevent attention drift after context
compaction. For the broader design, see:

- `docs/pathtrace_scene_producer_q2rtx_plan.md`
- `docs/pathtrace_scene_producer_replacement_guide.md`
- `docs/pathtrace_geometry_universe.md`
- `docs/pathtrace_material_universe.md`

Current Verdict
---------------

The useful path is the live raster drawSurf mirror:

    final visible drawSurf stream
        -> mesh identity without transform
        -> per-frame instance identity with transform/material/entity metadata
        -> old shader-compatible buffers until promotion is ready

Do not use `PathTraceSceneUniverse` as the primary scene representation. It is
useful as static map diagnostics/preload only. Using it as the universal RT scene
source caused missing static items, broken rigid props, strange emissive-only
fragments, holes, and severe performance regression.

Important source meanings as of this log:

    r_pathTracingSceneSource 0
        Existing stable drawSurf producer.

    r_pathTracingSceneSource 1
        Scene-universe diagnostics only.

    r_pathTracingSceneSource 2
        Full static scene-universe geometry plus old dynamic fallback.
        Known rough/debug path. Rigid entity scene-universe expansion is allowed
        only here.

    r_pathTracingSceneSource 3
        Existing drawSurf-seeded static cache plus mirrored dynamic-frame
        drawSurfs. This is the current production scaffold. It does not use
        `PathTraceSceneUniverse` static records as the main source.

Implemented Files
-----------------

New drawSurf mirror modules:

- `neo/renderer/NVRHI/PathTraceDrawSurfCapture.h`
- `neo/renderer/NVRHI/PathTraceDrawSurfCapture.cpp`
- `neo/renderer/NVRHI/PathTraceInstanceUniverse.h`
- `neo/renderer/NVRHI/PathTraceInstanceUniverse.cpp`

Extended existing modules:

- `neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp`
- `neo/renderer/NVRHI/PathTraceSceneCapture.h`
- `neo/renderer/NVRHI/PathTraceSceneCapture.cpp`
- `neo/renderer/NVRHI/PathTraceSceneUniverse.cpp`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.h`
- `neo/renderer/NVRHI/PathTraceGeometryUniverse.cpp`
- `neo/renderer/NVRHI/PathTracePrimaryPass.h`
- `neo/renderer/NVRHI/PathTraceCVars.h`
- `neo/renderer/NVRHI/PathTraceCVars.cpp`
- `neo/CMakeLists.txt`

New/changed CVars:

    r_pathTracingSceneSource
        Now accepts source 3.

    r_pathTracingSceneSourceCompare
        One-shot CPU-only source0 vs source3 comparison dump.

    r_pathTracingInstanceUniverseDump
        One-shot live drawSurf mesh/instance mirror diagnostics.

    r_pathTracingRigidMeshUniverseDump
        One-shot rigid mesh reuse eligibility diagnostics.

    r_pathTracingRigidMeshValidate
        One-shot validation comparing eligible local-source rigid mesh records
        against the currently baked dynamic rigid payload. Diagnostics only;
        source3 still renders through the dynamic fallback.

    r_pathTracingRigidBlasPlanDump
        One-shot diagnostics-only reusable rigid BLAS/TLAS plan from the
        local-source mesh records. It builds no GPU resources and does not
        change rendering.

    r_pathTracingRigidBlasInputDump
        One-shot diagnostics-only CPU rigid BLAS build-input descriptor dump.
        It validates counts, source identities, material IDs, vertex format,
        byte offsets, stride, and planned descriptor/instance counts. It builds
        no GPU resources.

    r_pathTracingSceneBoundsOverlay
        Live drawSurf mirror bounds overlay. 0 off, 1 eligible rigid candidates
        only, 2 all mirrored categories. Wireframe debug lines only.

    r_pathTracingSceneBoundsOverlayMax
        Maximum drawSurf mirror bounds boxes drawn per frame.

    r_pathTracingSceneBoundsDepthTest
        Legacy no-op from the first raster-debug-line attempt. The active
        overlay is composited inside the PT output now.

    r_pathTracingSceneBoundsOverlayGpu
        Default-off emergency gate for the PT shader-composited overlay path.
        Keep 0 after tester reported device removal when activating the overlay.

DrawSurf Mirror Slice
--------------------

`PathTraceDrawSurfCapture` now mirrors every visible raster drawSurf after the
old capture path has done its stable work.

For each valid drawSurf it builds:

    MeshObservation
        srfTriangles pointer
        ambient/index cache identities
        vertex/index counts
        vertex format
        material ID
        source class
        material/model names

    InstanceObservation
        mesh hash
        entity pointer/index if available
        render entity number
        current drawSurf index
        object-to-world transform
        material override marker
        source flags

Mesh identity deliberately excludes model matrix/transform. Instance identity
includes the current object-to-world transform and entity/material metadata.

`PathTraceInstanceUniverse` persists mesh records and instance transform history
only for diagnostics right now. It does not feed TLAS yet.

Source 3 Dynamic Frame Slice
----------------------------

`CapturePathTraceDynamicFrameFromDrawSurfMirror(...)` appends non-static mirror
drawSurfs into the existing shader-compatible dynamic geometry buffers.

Source 3 flow:

1. Run `CaptureDoomSurfacesForSmokeTest(...)` with `skipDynamicCapture=true`.
   This keeps the existing static drawSurf cache path.
2. Run the drawSurf mirror dynamic capture for non-static surfaces.
3. Merge old static stats with mirror dynamic stats.
4. Keep BLAS/TLAS creation and shader resource contracts unchanged.

This deliberately avoids a big-bang replacement. The rendered geometry still
uses the old static/dynamic buffer shape.

Failed Path: Source 3 Using Full Scene Universe
----------------------------------------------

An earlier source3 attempt used full static `PathTraceSceneUniverse` geometry as
part of the main scene feed.

Observed tester result:

- Many static items disappeared.
- Characters and some set pieces remained.
- Performance regressed from roughly 120 fps to roughly 40 fps.
- Dump showed source surface counts exploding because full static scene-universe
  records were being fed into the render path.

Correction:

- Source3 no longer feeds full static scene-universe geometry.
- Source3 uses existing drawSurf static cache plus mirror dynamic drawSurfs.
- `PathTraceSceneUniverse::BuildFullStaticGeometry` rigid entity behavior is
  restricted to source2.
- The toy max ray distance source2 override remains source2-only.

Validation Dumps
----------------

`surfaceuniverse.txt`

Source0 baseline plus mirror diagnostics matched old smoke capture exactly in
the tested frame:

    drawSurfs=88 usable=68 skipped=20
    uniqueMeshes=66 instances=68
    mirror classes static/rigid/skinned/particle/unknown=53/11/2/2/0
    oldSmoke classes static/rigid/skinned/particle/unknown=53/11/2/2/0
    missingMaterialOrSkin=0

`movement2.txt`

Door movement proved lifetime transform tracking can see moved rigid objects:

    lifetimeChanged(all/rigid)=2/1
    moved rigid entity=278
    model=models/mapobjects/doors/mcitydoor2l.lwo
    firstOrigin=(-592 -1344 128)
    currentOrigin=(-592 -1396.08 128)
    changes=61
    maxDelta=52.080078

`source3test.txt`

Bad source3 attempt:

    static scene-universe feed caused missing assets and 120 fps -> 40 fps

This is the run that proved `PathTraceSceneUniverse` must not become the main
scene source.

`source3test2.txt`

Corrected source3:

    drawSurfs=148 usable=108 skipped=40
    uniqueMeshes=107 instances=108
    mirror/old classes matched exactly: 59/25/14/10/0
    sourceSurfaces=108
    staticCache=59
    performance restored and assets returned

`source3mode20edge.txt` and `source3mode20edge_emissive.txt`

Mode20 edge/emissive issue looked like reservoir/noise/visibility behavior, not
source3 geometry loss:

    skoverhang/gottubelight visible as dynamic geometry
    gottubelight and sterlightdecal_add present in emissive inventory
    no dynamic light universe missing evidence in that dump

This should be ignored until a clearer repro appears.

`source3compare.txt`

One-shot source0 vs source3 comparison proved source3 equivalence for that
frame:

    totals old/source3 surfaces=84/84 verts=9223/9223 indexes=27510/27510
    staticTris=2116/2116
    dynTris=7054/7054
    classes static=44/44 rigid=23/23 skinned=13/13 particle=4/4 unknown=0/0
    dynamicMaterials unique old/source3=22/22 missing=0 extra=0
    skips matched: missingGeometry=39/39 gui=3/3

This is the strongest proof that source3 preserves the old producer payload
while exposing mesh/instance identity.

Rigid Mesh Candidate Slice
--------------------------

`PathTraceGeometryUniverse` now has a diagnostics-only rigid mesh candidate
registry.

The mirror feeds every observation into:

    RtPathTraceRigidMeshCandidateObservation

Eligibility is conservative:

- surface must be rigid
- geometry must have valid vertex/index counts
- material must be known
- local-space mesh must be valid
- reject skinned/deforming
- reject particles/transients
- reject GUI
- reject callback/generated
- reject static world
- reject existing static-cache matches

This path does not build reusable BLAS yet. It only records which mirrored rigid
surfaces are safe candidates for later local-space mesh BLAS plus per-frame TLAS
instances.

Local-Space Rigid Mesh Record Slice
-----------------------------------

`PathTraceGeometryUniverse` now persists non-rendering local-space mesh source
records for eligible rigid candidates.

Each eligible record keeps:

- mesh hash
- source `srfTriangles_t` pointer
- source vertex/index buffer identities
- vertex/index/triangle counts
- vertex format
- material ID
- source class ID
- material/model names
- first/last seen frame and per-frame instance count

This is deliberately not a render path yet. Source3 still bakes rigid drawSurfs
into the current dynamic-frame geometry buffer and builds BLAS/TLAS exactly as
before.

The rigid mesh universe dump now reports:

- eligible frame verts/indexes/triangles
- persistent local-source record verts/indexes/triangles
- current baked rigid surfaces/triangles
- estimated rigid triangles remaining after future promotion
- `renderPath=dynamicFallback`

This gives the next slice a measurable target: eligible rigid triangles should
match the rigid dynamic triangles that can eventually leave the per-frame dynamic
BLAS.

Strict Rigid Mesh Validation Slice
----------------------------------

`r_pathTracingRigidMeshValidate 1` runs a one-shot source3 validation after the
drawSurf mirror has populated local-source rigid mesh records and after the
current dynamic fallback payload has been captured.

It compares:

- baked dynamic rigid triangle count from `dynamicTriangleClassData`
- eligible local-source rigid triangle count from seen rigid records
- unique baked rigid material IDs
- unique eligible local-source material IDs
- missing/extra material ID samples

The validation does not compare transformed vertex positions yet, and it does
not change rendering. It is a strict count/material gate before reusable rigid
BLAS work begins.

Expected best case:

    triangleDelta=0
    missing=0
    extra=0
    renderPath=dynamicFallback

Rigid BLAS Plan Slice
---------------------

`r_pathTracingRigidBlasPlanDump 1` builds a CPU-only plan from the local-source
rigid mesh records seen this frame.

The plan reports:

- reusable mesh record count
- TLAS-style instance count
- local vertex/index/triangle counts for unique mesh records
- baked dynamic rigid surfaces/triangles in the current fallback payload
- planned rigid triangles removable from the dynamic fallback
- estimated remaining dynamic rigid triangles
- triangle delta between planned removal and current baked rigid payload
- sample mesh source identities

This is still diagnostics only:

    gpuBuild=0
    renderPath=dynamicFallback

Expected best case:

    plannedRemoveRigidTris == bakedRigidTriangles
    remainingDynamicRigidTris=0
    triangleDelta=0

Rigid BLAS Input Descriptor Slice
---------------------------------

`r_pathTracingRigidBlasInputDump 1` builds CPU-side, NVRHI-shaped descriptor
metadata from the rigid BLAS plan records.

It reports:

- descriptor count
- valid/invalid descriptor count
- planned geometry descriptor count
- planned instance count
- total vertex/index/triangle counts
- vertex/index byte sizes
- vertex/index formats
- vertex stride
- per-record byte offsets
- source `tri`, vertex buffer identity, and index buffer identity
- invalid reason counters

This is still diagnostics only:

    cpuDescriptorsOnly=1
    gpuBuild=0
    renderPath=dynamicFallback

Expected best case:

    descriptors == valid
    invalid=0
    invalids nullTri/verts/indexes/tris/format/source/material=0/0/0/0/0/0/0

Visual Bounds Overlay Slice
---------------------------

The drawSurf mirror can now draw color-coded wireframe bounds for visible
mirror records. This is a human spatial diagnostic only. It does not affect
capture payloads, BLAS/TLAS creation, or source routing.

The first attempt used `renderWorld->DebugLine`, which draws in the normal
raster debug layer and is not visible above PT when `r_pathTracingSkipRaster3D`
is 1. The active implementation uploads PT overlay line records and blends them
inside `pathtrace_smoke.rt.hlsl` after the mode18/mode20 output is written.

Tester result: the PT shader-composited overlay looked correct, but enabling it
caused immediate device removal. The per-pixel overlay loop is too risky for
this branch. `r_pathTracingSceneBoundsOverlayGpu` now defaults to 0 and disables
the shader loop even when category collection CVars are enabled. Next visual
attempt should use a low-cost GUI/HUD or post-blit overlay path, not a raygen
loop over all line records.

Follow-up implementation: the visible overlay now draws after PT is blitted to
LDR using raster `CommonRenderPasses::BlitTexture` calls. It projects the same
world-space bounds lines on CPU and draws small dotted screen-space markers with
the built-in white texture and constant-color blending. This is intentionally
crude, but it runs outside raygen and above the PT image.

Controls:

    r_pathTracingSceneBoundsOverlay 0
        Off.

    r_pathTracingSceneBoundsOverlay 1
        Draw only eligible rigid local-source candidates.

    r_pathTracingSceneBoundsOverlay 2
        Draw all mirrored categories.

    r_pathTracingSceneBoundsOverlayMax 128
        Cap boxes per frame.

    r_pathTracingSceneBoundsDepthTest 0
        No-op for the active PT-output overlay.

    r_pathTracingSceneBoundsOverlayGpu 0
        Keep off. 1 re-enables the risky shader-composited overlay path.

Color intent:

    green
        Static world/static-cache match.

    blue
        Eligible rigid candidate.

    red
        Rigid-looking surface rejected from reusable rigid promotion.

    magenta
        Skinned or deforming.

    gray
        Particle/transient.

    orange
        GUI.

    yellow
        Unknown/other.

The overlay currently uses transformed `srfTriangles_t::bounds`, so it is meant
to catch wrong category, wrong transform, stale position, and obviously absurd
bounds. It is not a precise per-triangle replacement for the existing capture
validation.

`localsourcemesh.txt`

Door area before movement:

    eligibleInstances=7
    eligibleUniqueMeshes=7
    localRecords(seen/total)=7/86
    frameVerts/indexes/tris=488/1014/338
    bakedRigidSurfaces/tris=7/338
    estimatedRigidTrisAfterPromotion=0
    renderPath=dynamicFallback

Door area after movement:

    eligibleInstances=4
    eligibleUniqueMeshes=4
    localRecords(seen/total)=4/105
    frameVerts/indexes/tris=388/840/280
    bakedRigidSurfaces/tris=4/280
    estimatedRigidTrisAfterPromotion=0
    renderPath=dynamicFallback

Prop-heavy area:

    eligibleInstances=33
    eligibleUniqueMeshes=31
    localRecords(seen/total)=31/187
    frameVerts/indexes/tris=2608/7323/2441
    bakedRigidSurfaces/tris=33/2441
    estimatedRigidTrisAfterPromotion=0
    renderPath=dynamicFallback

The local-source record path is covering every currently baked rigid dynamic
triangle in these tested frames. Eligible samples showed nonzero source `tri`,
vertex buffer identity, and index buffer identity fields. Reused source meshes
with different materials are currently separate mesh records, which matches the
active rule that mesh identity is geometry plus material.

`rigidmeshcandidates.txt`

Before door movement:

    observed=27
    rigid=4
    eligibleInstances=4
    eligibleUniqueMeshes=4
    persistentMeshes=83
    rejected=23

Eligible samples were exactly expected:

- fire extinguisher
- left door panel
- right door panel
- door frame

After moving/opening doors:

    observed=159
    rigid=15
    eligibleInstances=15
    eligibleUniqueMeshes=14
    persistentMeshes=104
    rejected=144
    reused=15
    newMeshes=0
    overrides=1

The repeated cone mesh correctly reused one mesh hash with multiple instances.
Static world and particles were rejected for expected reasons.

Moved-Rigid Transform Diagnostics
---------------------------------

The moved-rigid sample originally printed `maxMatrixDelta=0` if the object
returned to its original transform before the dump. The lifetime change count
still proved motion happened, but the amplitude was lost.

That has been corrected in `PathTraceInstanceUniverse` by tracking:

    maxObservedMatrixDelta
    maxObservedOriginDelta

Moved rigid samples now print both the current first-to-current matrix delta and
the max observed lifetime matrix/origin deltas. This should preserve door motion
amplitude even if the door returns to its starting transform before the dump.

Next Production Step
--------------------

Do not route rendering through the candidate records yet.

The next safe production slice is:

1. Validate `r_pathTracingRigidBlasInputDump 1` across the door scene and a
   prop-heavy scene.
2. If descriptors stay valid, add disabled-by-default GPU buffer/BLAS allocation
   scaffolding for selected rigid mesh records.
3. Keep old dynamic-frame geometry as the renderable fallback until validation
   proves selected records can be routed safely.

Guardrails
----------

- Do not revive the old in-tree RTXPT/Donut attempt.
- Treat RTXDI/RTXPT/Donut/Q2RTX as external references only.
- Do not feed `PathTraceSceneUniverse` records as the main source.
- Do not bake rigid objects into world-space static geometry.
- Do not synthesize fake drawSurfs or fake viewEntities.
- Keep source0 as stable fallback.
- Preserve mode18/mode19/mode20 shader/resource contracts while scaffolding.
- Keep `r_pathTracingWorldStaticEmissives` default `0`.
- Keep `r_pathTracingDynamicOccluderRadius` default `0`.
- Keep `r_pathTracingSkipRaster3D` default `1`.

Build/Test Commands
-------------------

Build:

    cd E:\prog\rbdoom-3-bfg-rt\neo
    cmake --build --preset win64-pt-dev-release

Copy:

    copy E:\prog\rbdoom-3-bfg-rt\build-win64-pt-dev\Release\RBDoom3BFG.exe E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Useful dumps:

    r_pathTracingSceneSource 3
    r_pathTracingSceneSourceCompare 1
    r_pathTracingInstanceUniverseDump 1
    condump source3compare

    r_pathTracingSceneSource 3
    r_pathTracingRigidMeshUniverseDump 1
    r_pathTracingInstanceUniverseDump 1
    condump rigidmeshcandidates



Autocompact Guard: Next GPU Scaffold Plan
=========================================

This section is intentionally concise and duplicated at the bottom so it
survives attention drift after context compaction.

Current validated state:

- Source3 renders through the existing dynamic fallback.
- Local-source rigid mesh records cover the baked rigid dynamic payload.
- Rigid mesh validation has passed at triangle/material level.
- Rigid BLAS plan has passed at planned-removal level.
- Rigid BLAS input descriptors are CPU-only and valid.
- DrawSurf mirror bounds overlay exists for human spatial diagnostics.
- First visual attempt used `renderWorld->DebugLine`; it was invisible because
  normal raster debug lines sit under PT when `r_pathTracingSkipRaster3D 1`.
- Second visual attempt composited overlay lines in `pathtrace_smoke.rt.hlsl`;
  it looked correct but caused immediate device removal/TDR. Keep
  `r_pathTracingSceneBoundsOverlayGpu 0`.
- Current safe visual path is post-PT-blit raster: CPU projects bounds lines and
  draws crude dotted 2D markers using `CommonRenderPasses::BlitTexture`.
- No reusable rigid BLAS is created yet.
- No rigid BLAS is in TLAS yet.
- No rigid triangles are removed from the dynamic fallback yet.

Do not do:

- Do not re-enable the shader-composited overlay path for normal testing.
- Do not put visual diagnostics in raygen per-pixel loops.
- Do not route rendering through reusable rigid BLAS yet.
- Do not remove rigid triangles from the dynamic fallback yet.

Safe overlay test:

       r_pathTracingDebugMode 18
       r_pathTracingSceneSource 3
       r_pathTracingSceneBoundsOverlayGpu 0
       r_pathTracingSceneBoundsOverlayMax 16
       r_pathTracingSceneBoundsOverlay 1

If stable but too sparse:

       r_pathTracingSceneBoundsOverlay 2

Next exact step:

1. If tester confirms the post-blit raster overlay is stable and visible, keep
   it as the visualization path and leave `r_pathTracingSceneBoundsOverlayGpu`
   default/off.

2. Then resume disabled GPU scaffold work for rigid BLAS records.

Next rigid BLAS slice: disabled GPU scaffold only.

1. Added default-off CVar:

       r_pathTracingRigidBlasGpuScaffold 0

2. Add optional second default-off CVar for actual build submission:

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

4. Build or reference local-space mesh data only. Do not apply object-to-world
   transforms to mesh vertices. Transform belongs to future TLAS instances.

5. When scaffold CVar is off, create no buffers, no BLAS handles, no uploads.

6. When scaffold CVar is on, create/upload per-mesh rigid BLAS input buffers
   only. Keep them separate from current static/dynamic render buffers.

7. When build CVar is on, submit per-mesh BLAS builds. Do not add them to TLAS.

8. Add one-shot dump:

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

9. Test stages:

       A. scaffold off: no behavior change
       B. scaffold on, build off: buffers/descs only
       C. scaffold on, build on: BLAS builds only

10. Do not route rendering through reusable rigid BLAS in this slice. TLAS
    routing and removal of rigid triangles from the dynamic fallback are later.


Autocompact guard update: bounds overlay visibility fix.

Problem observed:

- CPU/raster overlay path consumed frame time, which proved the overlay code was
  running, but no markers reached the video display.
- The likely cause was draw order/targeting: the overlay was drawn into the PT
  debug/LDR intermediate path instead of after the final LDR-to-current
  framebuffer copy.

Current fix:

- `PathTracePrimaryPass::BlitDebugOutput` no longer draws the raster bounds
  overlay.
- `RenderBackend.cpp` now calls
  `PathTracePrimaryPass::DrawBoundsOverlayRaster()` immediately after
  `Blit_Rendered2SwapChain` copies the LDR image to
  `deviceManager->GetCurrentFramebuffer()`.
- This keeps the crude marker overlay out of the raygen shader and places it on
  the final presented target.

Keep:

       r_pathTracingSceneBoundsOverlayGpu 0

Test:

       r_pathTracingDebugMode 18
       r_pathTracingSceneSource 3
       r_pathTracingSceneBoundsOverlayGpu 0
       r_pathTracingSceneBoundsOverlayMax 16
       r_pathTracingSceneBoundsOverlay 1

If stable but sparse:

       r_pathTracingSceneBoundsOverlay 2

If still no pixels:

- The next likely issue is projection/view-space mismatch, not scene capture.
  The overlay line collection is already executing.


Autocompact guard update: bounds overlay projection fix.

Problem observed after final-framebuffer routing:

- The raster overlay still consumed CPU but produced no visible pixels.
- This suggests capture and submission are active, but projected marker
  positions are probably outside the visible screen, or the target is still not
  the final displayed path.

Current fix:

- CPU marker projection no longer uses hand-derived camera axis math.
- `BuildRayTracingSmokeTestScene()` now captures the PT view's
  `worldSpace.modelViewMatrix` and `projectionMatrix`.
- `DrawBoundsOverlayRaster()` projects bounds line endpoints with those exact
  matrices before converting NDC to screen UVs.

Test is unchanged:

       r_pathTracingDebugMode 18
       r_pathTracingSceneSource 3
       r_pathTracingSceneBoundsOverlayGpu 0
       r_pathTracingSceneBoundsOverlayMax 16
       r_pathTracingSceneBoundsOverlay 1

If this still burns CPU with no markers, stop treating projection as the main
suspect. The next diagnostic should be a fixed screen-space marker drawn through
the same late overlay path, or a true GUI-model overlay emitted through the
visible HUD/crosshair layer.


Autocompact guard update: fixed screen-space overlay probe.

Problem observed after projection fix:

- Still no visible overlay, even though the CPU cost shows the code is active.

Current diagnostic build:

- When `r_pathTracingSceneBoundsOverlay` is nonzero and
  `r_pathTracingSceneBoundsOverlayGpu` is zero, `DrawBoundsOverlayRaster()` now
  first draws a fixed 32x32 magenta marker at the top-left of the target.
- The overlay is now attempted in two places:
  1. immediately after PT debug output is blitted into the PT LDR framebuffer,
  2. after the normal LDR-to-current-framebuffer blit.

Interpretation:

- If the magenta marker is visible but bounds are not, the final path is fine
  and the remaining bug is bounds projection/clipping.
- If neither the magenta marker nor bounds are visible, stop spending time on
  `CommonRenderPasses::BlitTexture` overlay placement. Move the visualization
  into the actual GUI/HUD model path that draws the crosshair/player GUI.


Autocompact guard update: fixed marker visible, bounds still absent.

Tester result:

- The fixed marker is visible, so the late overlay path reaches the screen.
- The marker appeared green/translucent instead of magenta, so blend-constant
  color coding is unreliable for this diagnostic path.
- Bounds were still absent.

Current fix:

- The diagnostic marker and bounds markers now draw solid white with no blend
  constant dependency.
- Bounds endpoint projection no longer rejects by near D3D depth range and now
  allows a much wider NDC range before screen clipping.
- `DrawBoundsOverlayRaster()` prints a throttled diagnostic line:

       PathTracePrimaryPass: bounds overlay raster lines=X projected=Y markers=Z

Interpretation:

- `lines=0`: line generation/category selection is the issue. Try
  `r_pathTracingSceneBoundsOverlay 2`.
- `lines>0 projected=0`: projection/clipping is still rejecting the records.
- `projected>0 markers=0`: marker placement/clipping is the issue.
- `markers>0` with no visible bounds: the per-bounds blits are not visually
  distinguishable enough; move to larger GUI/HUD quads.


Autocompact guard update: visible but misaligned bounds.

Tester result:

- Fixed marker reached the screen.
- Bounds overlay generated visible lines, proving line generation and late
  overlay placement are active.
- Lines appeared black/translucent and did not align with the camera/object
  positions.

Current fix:

- NDC-to-overlay UV conversion now flips Y for the blit coordinate system:

       uv.y = 0.5 - ndc.y * 0.5

- Marker blits now use additive white blending:

       src = One
       dst = One

  This is a diagnostic visibility choice. It should produce bright white
  markers independent of blend-constant behavior.

If still misaligned:

- The next suspect is not presentation. Inspect whether stored bounds endpoints
  are in the same world coordinate space as the captured view matrices, or
  temporarily project known world points such as `vieworg + viewaxis[0] * d`.


Autocompact guard update: debugMode 21 solid bounds boxes.

Decision:

- Stop iterating on raster/post-blit overlay. It reached the screen but wasted
  time on color, blend, GUI ordering, and screen-space alignment problems.
- Add a PT-native diagnostic mode that renders only solid captured bounds boxes.

Implemented:

- `r_pathTracingDebugMode 21` = solid drawSurf mirror bounds boxes.
- Mode 21 does not trace scene geometry and does not use the late raster
  overlay. It builds the camera ray, ray-intersects captured drawSurf AABBs in
  the raygen shader, and writes the closest box color directly to `SmokeOutput`.
- The shader derives boxes from the existing bounds-overlay line records:

       12 lines = 1 box
       max boxes in shader = 64

- Mode 21 automatically enables eligible-rigid bounds collection when
  `r_pathTracingSceneBoundsOverlay` is 0. Use overlay mode 2 to show all mirrored
  categories.
- `r_pathTracingSceneBoundsOverlayGpu` can remain 0. Mode 21 uploads the bounds
  line buffer for box data but does not run the old risky per-pixel line overlay.

Test:

       r_pathTracingDebugMode 21
       r_pathTracingSceneSource 3
       r_pathTracingSceneBoundsOverlayGpu 0
       r_pathTracingSceneBoundsOverlayMax 16

Optional all-category view:

       r_pathTracingSceneBoundsOverlay 2

Expected limitation:

- This is a replacement debug view, not an overlay. Switch back to 18/20 for the
  actual PT scene.


Autocompact guard update: debugMode 22 wireframe bounds boxes.

Tester result:

- Mode 21 works.
- `r_pathTracingSceneBoundsOverlay 2` in solid mode is too obstructed to be
  useful for spatial inspection.

Implemented:

- `r_pathTracingDebugMode 22` = PT-native wireframe drawSurf bounds boxes.
- It uses the same captured AABB records as mode 21, but only shades pixels
  near the box edges on the ray-hit face.
- It keeps the same shader cap:

       max boxes = 64

Test:

       r_pathTracingDebugMode 22
       r_pathTracingSceneSource 3
       r_pathTracingSceneBoundsOverlayGpu 0
       r_pathTracingSceneBoundsOverlayMax 16

Optional all-category wireframe:

       r_pathTracingSceneBoundsOverlay 2

Notes:

- Mode 22 is still a replacement debug view, not an overlay on mode 18/20.
- If edges are too fat/thin, adjust the shader's `edgeThickness` expression in
  `RenderSmokeBoundsWireframeBoxes()`.

Final tester result for this slice:

- Mode 22 is useful. It initially looked visually wrong, but the view is
  actually exposing the scene/BVH/source issues this diagnostic was meant to
  reveal.
- Mode 21 remains useful as a solid occupancy view, but mode 22 is the better
  default for spatial inspection because all-category solid boxes can occlude
  too much.
- At least two diagnostic colors are visible so far. Color mapping still comes
  from `PtMirrorBoundsColor()`:

       blue  = eligible rigid local-source candidate
       red   = rigid-looking but rejected from reusable rigid promotion
       green = static world/static-cache match
       other colors reserved for skinned/transient/GUI/unknown categories

Current recommendation:

- Use mode 22 for human spatial inspection of candidate bounds/BVH behavior.
- Use mode 21 only when solid volume occupancy is easier to reason about.
- Do not resume the post-blit overlay unless there is a specific reason to
  debug final compositor behavior.

Blit overlay lessons retained:

- `renderWorld->DebugLine` was not useful for this PT diagnostic because normal
  raster debug lines sit under or outside the PT-presented image when
  `r_pathTracingSkipRaster3D` is 1.
- The shader-composited line overlay proved visually attractive but caused
  device removal/TDR. Keep `r_pathTracingSceneBoundsOverlayGpu 0`.
- `CommonRenderPasses::BlitTexture` can reach the screen when called late: the
  fixed top-left marker appeared. This proved the late presentation path was not
  completely wrong.
- Blend-constant coloring through that blit path was unreliable in practice:
  the intended magenta marker appeared green/translucent.
- Projected line overlays consumed CPU and eventually produced visible lines,
  but alignment/color behavior made the path a poor use of time.
- The productive path was to move visualization into the PT raygen output as a
  dedicated replacement debug mode.
