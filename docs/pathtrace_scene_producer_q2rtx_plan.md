RBDoom3 BFG RT/PT Scene Producer Plan with Q2RTX Reference
==========================================================

Purpose
-------

This document refines the Doom 3 BFG RT/PT BLAS replacement plan after checking
the local Q2RTX Ray Reconstruction reference at:

    E:\prog\references\Q2RTX-rayreconstruction-master

Q2RTX is only a reference. Do not copy it into the tree or revive the old
RTXPT/Donut attempt. The useful lesson is architectural: a mature path-traced id
Tech renderer does not treat the raster draw list as the path-traced scene. It
builds persistent world/model/light records, assigns spatial membership, and
uses that membership during light sampling and temporal reconstruction.

Implementation note: this file is background research and long-range context.
Use `pathtrace_scene_producer_replacement_guide.md` as the implementation-facing
next-task guide. The replacement guide intentionally narrows the next work to a
static-world preload plus drawSurf mesh/instance mirror before any rigid
promotion, persistent light records, area/portal light lists, or temporal reuse.

Relevant Q2RTX Patterns
-----------------------

Static BSP geometry is a persistent world inventory:

- `src/refresh/vkpt/bsp_mesh.c` walks BSP faces and model faces directly, not a
  raster drawSurf list.
- Each `VboPrimitive` stores triangle data, material id, emissive/alpha data,
  instance id, and a BSP cluster.
- Static world triangles get cluster membership by testing an off-center point
  against the BSP leaf tree.
- Some transparent/portal-like cases patch or symmetrize PVS visibility because
  ray/path tracing needs connectivity that raster assumptions may not expose.

Model geometry is represented as instances, not fake draw surfaces:

- `ModelInstance` records store source buffer, primitive offsets, transform,
  previous transform, cluster, material override, alpha, animation frame data,
  and render primitive offsets.
- Rigid BSP submodels and alias/IQM models are converted through model instance
  records and GPU-side instance geometry work, not by synthesizing frontend
  raster draw surfaces.
- Q2RTX explicitly tracks previous-to-current model instance remapping for
  temporal reprojection.

Emissives become light records:

- Static emissive surfaces are promoted into `light_poly_t` records.
- Partial emissive textures are clipped into polygonal light records using
  emissive texture extents rather than treating every source triangle as one
  uniform light.
- Model emissive lights are extracted separately and transformed when the model
  is instanced.
- Dynamic/game lights are injected as light records each frame.

Light sampling is cluster/PVS based:

- Static light polygons have a source cluster.
- Q2RTX computes cluster AABBs and builds compact `cluster_lights` plus
  `cluster_light_offsets`.
- For each light, it walks the source cluster PVS and adds the light to visible
  clusters when the light can affect that cluster.
- Model lights are injected into those per-cluster lists each frame.
- ReSTIR samples from the shaded point's cluster list, not from a whole-map
  global emissive list.

Temporal reuse depends on stable identity/remapping:

- Q2RTX keeps model previous/current mappings and model-light previous/current
  mappings.
- ReSTIR maps previous light ids to current ids before accepting temporal
  samples.
- Cluster id, instance id, primitive id, depth, and normals are part of the
  temporal validity story.

Implications for Doom 3 BFG
---------------------------

The previous plan was correct in direction but incomplete in a few places:

- A "PT scene producer" must produce spatial membership, not just geometry.
- Static emissive extraction should create light records linked to static
  geometry/material identity, not only triangle candidates.
- Rigid entity records need previous/current identity and transform history from
  the beginning, even if temporal reuse remains disabled.
- Mode 20 should move toward area/portal light lists before spatial/temporal
  reservoir reuse.
- Fake drawSurf construction inside PT capture is the wrong boundary.
- Whole-map geometry may be feasible, but whole-map light sampling is not the
  right default.
- Dynamic/model emissives should stay current-frame unless identity, transform,
  runtime material state, and occluder geometry are trustworthy.

Findings From The Source-2 Experiments
--------------------------------------

The first implementation slice added `PathTraceSceneUniverse` and
`r_pathTracingSceneSource 2`, where static Doom world surfaces can be inventoried
from `renderWorld->entityDefs` and fed into the existing static buffers.

Useful results:

- Static world/BSP inventory works as a preload/diagnostic path.
- Doom area/portal membership can be inventoried outside `viewDef->drawSurfs`.
- Whole static map geometry is small enough on tested maps to be useful as a
  correctness baseline.
- The old drawSurf-seeded static cache was missing off-camera static surfaces;
  source 2 removed selected static misses in the tested area.
- Raising the static-only cap exposed that static and dynamic buffers must be
  budgeted separately.

Failures and lessons:

- `PathTraceSceneUniverse` must not become the universal scene producer.
- Baking rigid entities into world-space static geometry is the wrong model.
  Doors, props, skinned things, movers, and mixed material objects break when
  entity transform/lifetime is collapsed into a static vertex cache.
- Surface-level rigid promotion is also wrong. Promoting only emissive surfaces
  leaves "floating emissive panels" while the rest of the prop remains
  raster/PVS-dependent.
- Whole-entity rigid promotion helps some off-camera props, but it is still a
  diagnostic hammer. It is too broad without per-instance identity, motion
  state, and update rules.
- Rigid promotion cannot safely reuse a static BLAS cache unless transform and
  material state are part of the instance update path.
- Characters and skinned/deformed surfaces must remain current-frame drawSurf
  fallback until a separate deforming-mesh path exists.
- Doom portal areas did not appear to be the direct cause of the observed
  glitches: rays traced across portals in modes 18 and 20. The failures matched
  identity/transform/category mistakes instead.

Revised Scene Ownership Model
-----------------------------

Use the modules with these responsibilities:

    PathTraceSceneUniverse
        Optional static map scanner, diagnostics, and static-world preload.

    PathTraceDrawSurfCapture
        Primary live scene source. Mirrors final raster draw surfaces for
        current geometry, transforms, material overrides, skins, dynamic model
        state, particles, GUI, and skinned/deformed objects.

    PathTraceGeometryUniverse
        Mesh and BLAS cache. Owns reusable geometry records independent of
        object-to-world transform.

    PathTraceInstanceUniverse
        Per-frame TLAS instance list. Owns instance transform, entity identity,
        material/skin override, previous transform, and current-frame
        visibility.

    PathTraceMaterialUniverse
        Material and texture registry.

The crucial representation shift is:

    current experimental source-2 shape:
        surface = geometry + material + transform + area + baked world vertices

    target shape:
        mesh = geometry + material/source format
        instance = mesh + transform + entity/material override + frame state

Mesh identity should contain:

- geometry pointer or GPU-buffer identity,
- vertex/index counts,
- vertex format,
- stable source `srfTriangles_t*` when available,
- material pointer/name as metadata.

Mesh identity must not contain:

- model matrix,
- current transform,
- current entity area,
- per-frame visibility.

Instance identity should contain:

- mesh key,
- current object-to-world transform,
- previous object-to-world transform when available,
- entity pointer/index/handle when available,
- material/skin/custom-shader override when available,
- current visibility/source flags.

Once this split exists, classification becomes observable rather than guessed:

- same mesh and same transform for many frames -> static-ish rigid,
- same mesh and changing transform -> rigid dynamic,
- changing vertex data -> deforming/skinned dynamic,
- appears once or is tiny/effect-like -> transient.

Replacement Boundary
--------------------

Keep the existing shader/resource contract for the first replacement stages:

- static BLAS instance remains instance 0,
- dynamic BLAS instance remains instance 1,
- existing static/dynamic vertex/index/class/material buffers remain,
- existing material table and mode 18/19/20 shader bindings remain,
- `PathTraceAcceleration.*` remains the NVRHI BLAS/TLAS helper layer.

Replace only the producer:

    old:
        viewDef->drawSurfs
        -> CaptureDoomSurfacesForSmokeTest
        -> static/dynamic buffers

    revised staged target:
        static scene universe preload for BSP/map surfaces
        + drawSurf mesh/instance mirror for live objects
        -> same static/dynamic buffers at first
        -> later multi-mesh BLAS + per-instance TLAS

New Module Shape
----------------

Add parallel scene producer modules rather than expanding
`PathTraceSceneCapture.cpp` into a monolith:

    PathTraceSceneUniverse.*
        Static world records, area/portal diagnostics, optional static preload.

    PathTraceDrawSurfCapture.*
        Live drawSurf mirror. Creates mesh records and instance records from
        final raster draw-surface submission without treating raster visibility
        as the whole PT world.

    PathTraceGeometryUniverse.*
        Mesh records, reusable geometry ranges, BLAS cache state.

    PathTraceInstanceUniverse.*
        Per-frame instance records and future TLAS instance assembly.

    PathTraceSceneSelection.*
        Per-frame selection that merges static preload records with live
        drawSurf instances.

    PathTraceSceneDiagnostics.*
        Dumps for area/portal membership, selected records, omitted records,
        and mode 20 light candidate origins.

Names can change, but keep the boundaries:

- universe owns persistent identity,
- drawSurf capture owns live current-frame instance truth,
- instance universe owns current-frame inclusion,
- diagnostics owns logging,
- acceleration still owns NVRHI BLAS/TLAS creation.

Stage 0: Keep Baseline Stable
-----------------------------

Do not delete or destabilize the current drawSurf path.

- `r_pathTracingWorldStaticEmissives` stays default `0`.
- `r_pathTracingDynamicOccluderRadius` stays default `0`.
- `CaptureDoomSurfacesForSmokeTest` remains the stable fallback.
- No fake drawSurf/viewEntity synthesis inside PT capture.

Add a source CVar before behavior changes:

    r_pathTracingSceneSource 0

Suggested values:

- 0: current drawSurf producer only.
- 1: new scene universe diagnostics only.
- 2: static scene universe feeds static world buffers as a correctness/preload
  experiment; dynamic drawSurf fallback remains live.

Do not add a "static + rigid scene universe" value as the main path. Rigid
entities need mesh/instance identity, not baked world-space static geometry.

Stage 1: Static World Inventory, Diagnostics Only
-------------------------------------------------

Build a read-only static world inventory without feeding BLAS.

For each static world surface/triangle, record:

- stable surface key,
- material id,
- triangle range,
- world-space bounds,
- center/off-center point,
- Doom area/portal membership if available,
- fallback membership reason if not available,
- can-emit material facts,
- runtime state unsupported/unknown flag.

Diagnostics:

- total static surfaces/triangles/materials,
- area membership counts,
- unassigned surface count,
- emissive-capable static count,
- surfaces that differ from current drawSurf-seeded static cache.

Important guardrail:

- Do not scan/rebuild this inventory every frame. Build at map load, first PT
  use, or explicit reset/rebuild.

Stage 2: Static Emissive Light Records
--------------------------------------

Derive static emissives from the static world inventory.

Store light records with:

- stable light id,
- source surface/material id,
- triangle or clipped polygon positions,
- center/off-center point,
- normal,
- area/portal membership,
- emissive color/luminance,
- runtime state signature if Doom material registers can affect it,
- suitability flags for mode 20, spatial reuse, and temporal reuse.

Do not feed all records globally into mode 20. First expose diagnostics:

- total static emissive records,
- records by area,
- records without area,
- largest/brightest records,
- records rejected for invalid/runtime-unknown state.

Stage 3: Area/Portal Light Lists
--------------------------------

Before changing reservoir reuse, build static light membership lists.

First simple rule:

- current area plus directly connected areas,
- then current area plus N portal steps,
- optionally radius-capped by camera/player position.

Better rule:

- approximate Q2RTX's cluster lists using Doom portal areas:
  - source area for each light,
  - area AABB or bounds,
  - portal/PVS-style reachability,
  - conservative "light can affect area" test.

Mode 20 should sample from the shaded point's area/selected-area light list when
available. If a shaded point has no area, fall back to current visible/frame
candidates and log it.

Stage 4: Static BLAS From Scene Universe
----------------------------------------

Feed the existing static buffers from selected scene-universe records.

Keep the output shape identical to today:

- `PathTraceSmokeVertex` array,
- static index array,
- static triangle class array,
- static triangle material id array,
- material table static material indexes.

Selection can start conservative:

- all cached static world if the level is small enough and upload cost is stable,
- or selected area set if whole static world is too expensive,
- but never per-frame full discovery.

Diagnostics must compare:

- scene-universe static triangle count,
- selected static triangle count,
- old drawSurf static triangle count,
- static BLAS cache hit/miss,
- missing current-view surfaces.

This stage is a static-world preload/checkpoint only. It should not be extended
to bake arbitrary rigid entities into the static cache.

Stage 5: DrawSurf Mesh/Instance Mirror
--------------------------------------

Add a drawSurf mirror that records final raster submission as mesh and instance
data.

For each visible drawSurf this frame:

- create or reuse a `MeshRecord`,
- append one `InstanceRecord`,
- record the current object-to-world transform,
- record entity pointer/index when available,
- record material/skin/custom-shader override when available,
- record whether the source is static world, rigid, skinned/deformed,
  particle/translucent, GUI, callback, or unknown.

`MeshRecord`:

- geometry pointer or GPU-buffer identity,
- vertex/index counts,
- vertex format/source kind,
- source `srfTriangles_t*` when stable,
- material pointer/name/id as metadata,
- no model matrix.

`InstanceRecord`:

- mesh key,
- object-to-world transform,
- previous transform if known,
- entity id/pointer if available,
- material/skin override,
- current-frame visibility/source flags.

Initially this mirror can be diagnostics-only. Do not feed the instance records
into a new TLAS until the record identity and counts are stable.

Stage 6: Rigid Entity Classification
------------------------------------

Classify rigid entities from observed mesh/instance history rather than from
one-shot renderWorld scanning.

Record and diagnose:

- entity index/handle,
- model pointer/name/hash,
- surface index,
- material id after skin/custom shader rules,
- current transform,
- previous transform,
- world bounds,
- area refs or fallback area,
- stable instance id,
- previous-to-current remap id,
- runtime material state signature.

Rigid objects must not be baked into world-space static geometry. Build or reuse
their mesh BLAS in local/object space and update the TLAS transform.

Initial exclusions:

- skinned/deformed characters,
- callback-generated custom shader objects,
- particles/sprites/flares/beams,
- GUI surfaces,
- overlays/decals,
- continuously generated dynamic models.

Those remain current-frame drawSurf fallback until separate identity/motion or
deforming-mesh handling exists.

Stage 7: Rigid Emissive Injection
---------------------------------

For rigid records with trustworthy identity and transform:

- derive model emissive light records,
- transform them per frame,
- assign area/portal membership from transformed center/off-center points,
- inject them into selected light lists for the current frame,
- keep previous-to-current remapping data for future temporal reuse.

If identity, transform, runtime state, or matching occluder geometry is not
trustworthy, keep the emissive current-frame only.

Stage 8: Transient Fallback
---------------------------

The current drawSurf capture path remains valuable for:

- skinned models with RT CPU skinning,
- particles/smoke/translucency,
- GUI surfaces when enabled,
- weird callback/custom-shader objects,
- one-frame effects.

Treat these as transient:

- feed dynamic BLAS current-frame only,
- feed mode 20 as current-frame candidates only,
- do not persist lights,
- do not temporal-reuse until identity/remap/motion data exists.

Stage 9: Temporal/ReSTIR Preconditions
--------------------------------------

Do not enable temporal reservoir reuse until these exist:

- stable static light ids,
- rigid/model light previous-to-current remap,
- runtime emissive state signatures,
- geometry/occluder confidence flags,
- shaded-point area/portal membership,
- rejection path for missing or remapped-away light ids.

Q2RTX's ReSTIR path explicitly remaps previous dynamic/model light ids to
current ids. Doom should do the same rather than retaining last-visible dynamic
emissive candidates.

Diagnostics Checklist
---------------------

Add compact dumps that answer:

- current camera/player area,
- center-hit/shaded-point area when available,
- selected area count,
- selected static surfaces/triangles,
- selected rigid entities/surfaces/triangles,
- transient drawSurf fallback count,
- static emissive records by area,
- rigid emissive records by area,
- light list size for current area,
- mode 20 sampled candidate origin,
- omitted static/rigid/transient records by reason,
- previous-to-current remap counts once history exists.

Oversights Avoided By The Q2RTX Check
-------------------------------------

- Do not stop at "nearby geometry." Store area/portal membership on geometry and
  lights.
- Do not keep emissives as only frame-visible triangles. Promote static
  emissives into persistent light records.
- Do not sample a global light universe by default. Build selected light lists.
- Do not invent drawSurfs. Convert model/world records directly.
- Do not defer identity/remap design until after temporal reuse. Add identity
  fields early, even while temporal reuse is disabled.
- Do not assume center-point area membership is always valid. Add fallback
  checks for corners/bounds and log unassigned records.
- Do not mix every dynamic class together. Rigid, skinned, particles, GUI, and
  callback effects need different trust levels.

Near-Term Implementation Slice
------------------------------

The completed first code slice adds static scene-universe diagnostics and an
opt-in static-world preload source. Keep that code as a static-map checkpoint,
not the final producer architecture.

The next code slice should be diagnostics-only and drawSurf-mirror oriented:

1. Add `PathTraceInstanceUniverse` and a minimal `PathTraceDrawSurfCapture`
   mirror path.
2. Record `MeshRecord` and `InstanceRecord` for each final raster drawSurf.
3. Dump mesh reuse, instance counts, transforms, entity ids, material override
   status, and source class counts.
4. Compare drawSurf mirror records against current static scene-universe records.
5. Do not feed the new instance records into BLAS/TLAS yet.

Useful dumps:

       r_pathTracingSceneUniverseDump 1
       r_pathTracingInstanceUniverseDump 1

Only after mesh/instance identity looks sane should the drawSurf mirror start
feeding a new BLAS/TLAS layout. The current two-instance static/dynamic shader
contract can remain as the fallback while this is developed.
