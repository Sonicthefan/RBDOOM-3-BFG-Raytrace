Skinned Motion Vectors And GPU Skinning Transition Guide
=======================================================

Purpose
-------

This folder is for focused work on making Doom 3 skinned/deforming geometry
usable for path-traced motion vectors, ReSTIR/PT temporal reuse, denoisers, and
future DLSS Ray Reconstruction inputs.

The current path-tracing skinning path was built as an early prototype. It
works as a visual bridge, but it is not a durable motion-vector architecture.
The goal is to move from frame-local CPU-skinned PT vertices toward explicit
current/previous geometry contracts, and then toward a PT-owned GPU skinning
path.

Current Problem
---------------

The current PT scene capture path skins Doom vertices on the CPU while building
the dynamic PT vertex buffer:

    neo/renderer/NVRHI/PathTraceSkinning.cpp
    neo/renderer/NVRHI/PathTraceSceneCapture.cpp

The output is a final world-space `PathTraceSmokeVertex`:

    position
    normal
    texcoord
    color
    color2

That is enough for a first ray-traced visual path, but it is weak for motion
vectors because:

1. The current dynamic PT buffer is a frame-local output stream, not a stable
   source geometry universe.

2. The shader-visible vertex format stores final world-space positions and
   normals, not a clear source contract with base pose, joint indices, joint
   weights, current joint matrices, and previous joint matrices.

3. Previous-frame skinned positions are not exposed as a first-class buffer or
   range mapping.

4. Current dynamic buffer order can change with view selection, portal
   selection, drawSurf order, and transient surfaces. Previous-frame lookup must
   not rely on same offset by accident.

5. Raster GPU skinning already exists, but it skins inside raster vertex
   shaders. It does not produce a reusable skinned vertex buffer for BLAS build,
   path-tracing hit shading, or previous-frame reconstruction.

LOD, Topology, And Compaction Hazards
-------------------------------------

Do not assume Doom has no LOD risk just because classic Doom 3 visibility relies
heavily on portals.

The codebase has several topology/history hazards that must be treated
explicitly:

1. Material-surface LOD flags exist.

   `idMaterial` supports `lod1` through `lod4` and `persistentLOD`. In
   `tr_frontend_addmodels.cpp`, drawSurf creation skips a surface when its LOD
   material is not visible for the current camera distance. This is surface
   inclusion/exclusion, not necessarily a whole-model mesh swap, but it can
   still make current and previous surface membership differ.

   Motion-vector policy:

       if a skinned surface appears/disappears due to material LOD, mark
       previous invalid for that surface unless a future explicit remap exists.

2. MD5 and GLTF skinned models usually keep stable topology.

   The MD5/GLTF dynamic model paths update joint matrices and reuse source
   surface topology when the vertex/index counts match. This is the favorable
   case for previous skinned positions.

   Motion-vector policy:

       allow previous matching only when identity and vertex/index/triangle
       counts match.

3. MD3 and other dynamic/generated models can change pose/topology differently.

   MD3 is `DM_CACHED` and uses frame interpolation over per-frame vertex data.
   Particles, sprites, beams, liquids, and many material deforms are
   `DM_CONTINUOUS` or generated/deformed surfaces.

   Motion-vector policy:

       do not treat these as skinned MD5/GLTF surfaces. They need their own
       source/previous contract or should invalidate object motion.

4. PT dynamic buffer compaction/reordering is a real hazard.

   The PT dynamic vertex buffer is rebuilt from currently captured drawSurfs.
   Portal selection, view selection, material LOD, generated surfaces, and
   bucket ordering can change offsets.

   Motion-vector policy:

       previous and current buffers must be connected by explicit surface
       records and range maps. Never use same offset in the dynamic buffer as a
       correctness assumption.

5. Creation, deletion, teleport, skeleton reset, and count mismatch must reject
   history.

   If an object is newly created, was absent last frame, teleported, changed
   skeleton/joint count, changed source surface counts, or changed topology,
   mark previous skinned data invalid for that frame.

Guiding Rule
------------

Build stable current/previous geometry contracts before moving all skinning to
GPU compute.

Do not start by replacing every CPU transform with a compute shader. First make
the current CPU-skinned output trackable across frames. That lets the branch
validate motion-vector semantics, surface identity, and history rejection before
the more invasive GPU skinning stage.

Q2RTX Reference Pattern
-----------------------

Use the local Q2RTX references as architecture examples:

    E:\prog\references\Q2RTX-master
    E:\prog\references\Q2RTX-rayreconstruction-master

Important files:

    E:\prog\references\Q2RTX-master\src\refresh\vkpt\shader\global_ubo.h
    E:\prog\references\Q2RTX-master\src\refresh\vkpt\main.c
    E:\prog\references\Q2RTX-master\src\refresh\vkpt\shader\instance_geometry.comp
    E:\prog\references\Q2RTX-master\src\refresh\vkpt\shader\vertex_buffer.h
    E:\prog\references\Q2RTX-master\src\refresh\vkpt\shader\asvgf_gradient_reproject.comp

The Q2RTX pattern is:

1. Store per-instance current and previous transforms.

2. Store current and previous pose/frame offsets.

3. Store current and previous skeletal matrix offsets.

4. Build current-to-previous and previous-to-current instance maps from stable
   entity identity.

5. Make previous skeletal matrices addressable in the current frame.

6. Generate current triangle positions and previous triangle positions together.

7. Reproject by mapping previous dynamic instance IDs back to current dynamic
   instance IDs before reconstructing the surface.

Do not copy Q2RTX code, comments, structs, or shader helper bodies into this
repository. Use it only as an architecture checklist.

Target Doom-Side Architecture
-----------------------------

The Doom path should evolve toward these layers:

1. Skinned surface identity layer

   Records stable identity for each captured skinned/deforming surface:

       entity index or render-entity generation
       model/surface identity
       srfTriangles_t identity
       material id/material index
       current frame visibility/capture state
       current and previous vertex/index/triangle ranges

2. Previous skinned output bridge

   Keeps previous CPU-skinned output long enough to populate primary-surface
   previous world positions. This is an intermediate bridge, not the final
   GPU-skinning architecture.

3. Source geometry contract

   Adds a source format that preserves base local position, normal, tangent,
   uv, joint indices, joint weights, source surface identity, and material
   metadata. Do not use the final world-space `PathTraceSmokeVertex` as the
   GPU-skinning source.

4. Current/previous joint state

   Stores current and previous joint matrices or equivalent Doom joint state in
   GPU-visible buffers. Previous matrices must be intentionally retained and
   remapped, not rediscovered from the current drawSurf.

5. GPU skinned output

   A compute pass writes current world-space PT vertices and previous
   world-space positions/deltas. The current output feeds BLAS and hit shading;
   the previous output feeds motion vectors and temporal validation.

6. Primary surface and temporal consumers

   Primary surface records should eventually write real
   `previousPositionOrMotion` for skinned hits, with a valid bit and a rejection
   reason when no previous surface exists.

Recommended Task Order
----------------------

1. Add skinned surface identity/range tracking with no visual behavior change.

2. Add previous CPU-skinned vertex retention as a bridge and expose validity in
   scene inputs.

3. Wire a debug visualizer or diagnostic dump that proves current-to-previous
   skinned range matching is stable.

4. Add source/output contracts for PT GPU skinning, without switching the main
   path yet.

5. Add a PT-owned GPU compute skinning pass behind a CVar.

6. Switch selected skinned surfaces to GPU skinning after correctness is proven.

7. Populate primary-surface object motion for skinned hits.

Strict Build And Validation Rule
--------------------------------

Do not use legacy Debug builds for this work.

Do not copy, paste, revive, or recommend legacy commands that build from the
old `build` directory with the Debug configuration, and do not copy an
executable from a Debug output folder.

For normal validation, use PT retail:

    cmake --preset win64-pt-retail
    cmake --build --preset win64-pt-retail-release

Copy the PT retail executable to the prepared game folder:

    Copy-Item -LiteralPath E:\prog\rbdoom-3-bfg-rt\build-win64-pt-retail\Release\RBDoom3BFG.exe -Destination E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe -Force

If shader code changes, also copy the generated path-tracing shader blobs:

    Copy-Item -LiteralPath E:\prog\rbdoom-3-bfg-rt\base\renderprogs2\dxil\builtin\pathtracing\pathtrace_smoke.rt.bin -Destination E:\prog\rbdoom-3-BFG-prebuilt\base\renderprogs2\dxil\builtin\pathtracing\pathtrace_smoke.rt.bin -Force
    Copy-Item -LiteralPath E:\prog\rbdoom-3-bfg-rt\base\renderprogs2\spirv\builtin\pathtracing\pathtrace_smoke.rt.bin -Destination E:\prog\rbdoom-3-BFG-prebuilt\base\renderprogs2\spirv\builtin\pathtracing\pathtrace_smoke.rt.bin -Force

Read First
----------

Before editing code, read:

    docs/pathtracing.txt
    docs/pathtrace_modules.txt
    docs/pathtrace_skinned_motion_vectors/README.txt
    docs/pathtrace_skinned_motion_vectors/q2rtx_reference_notes.txt
    docs/pathtrace_skinned_motion_vectors/restir_temporal_geometry_motion_minimum_features.txt
    docs/pathtrace_skinned_motion_vectors/restir_temporal_geometry_doom_mapping.txt

Important local code:

    neo/renderer/NVRHI/PathTraceSkinning.cpp
    neo/renderer/NVRHI/PathTraceSkinning.h
    neo/renderer/NVRHI/PathTraceSceneCapture.cpp
    neo/renderer/NVRHI/PathTraceSceneCapture.h
    neo/renderer/NVRHI/PathTraceGeometry.h
    neo/renderer/NVRHI/PathTraceSceneInputs.h
    neo/renderer/NVRHI/PathTraceDrawSurfCapture.cpp
    neo/renderer/NVRHI/PathTraceSurfaceClassification.cpp
    neo/renderer/NVRHI/PathTraceSmokeSceneBuild.cpp
    neo/renderer/NVRHI/PathTraceSmokeResources.cpp
    neo/renderer/NVRHI/PathTracePrimarySurface.h
    neo/shaders/builtin/pathtracing/PathTracePrimarySurface.hlsli
    neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl
    neo/renderer/tr_frontend_addmodels.cpp
    neo/renderer/Model_md5.cpp
    neo/renderer/Model_gltf.cpp

Non-Goals For Early Tasks
-------------------------

Do not redesign the whole path tracer.

Do not change portal walking.

Do not change material classification.

Do not move every skinned surface to GPU compute in Task 01.

Do not remove the existing CPU-skinned bridge until the GPU path produces the
same current vertices and better previous-position data.

Do not treat same-buffer-offset matching as a valid previous-frame contract.

Research Mapping
----------------

The additional research document:

    docs/pathtrace_skinned_motion_vectors/restir_temporal_geometry_motion_minimum_features.txt

is a useful production target for ReSTIR/PT temporal geometry, but it is broader
than the first implementation slice. Use:

    docs/pathtrace_skinned_motion_vectors/restir_temporal_geometry_doom_mapping.txt

to translate those requirements into the current Doom branch.

The key interpretation is:

    CPU previous-skinned data is allowed only as a transitional bridge because
    current PT skinning is CPU-side.

    The final target remains GPU-resident current/previous deformed positions,
    stable instance/geometry identity, explicit invalid reasons, and primary-hit
    motion from PrimitiveID + barycentrics.

    Alternate/dual motion vectors, forward splatting, and full offscreen dynamic
    update are later ReSTIR Enhanced / denoiser-quality tasks, not Task 01.

Do not stage, commit, or push unless explicitly asked.
