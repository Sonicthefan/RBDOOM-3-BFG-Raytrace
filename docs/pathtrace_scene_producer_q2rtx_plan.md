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

    staged target:
        PT scene inventory + PT scene selection
        -> same static/dynamic buffers

New Module Shape
----------------

Add a parallel scene producer module rather than expanding
`PathTraceSceneCapture.cpp`:

    PathTraceSceneUniverse.*
        Persistent records and map/entity/light inventory.

    PathTraceSceneSelection.*
        Per-frame selection of static areas, rigid entities, and fallback
        transient drawSurfs.

    PathTraceSceneDiagnostics.*
        Dumps for area/portal membership, selected records, omitted records,
        and mode 20 light candidate origins.

Names can change, but keep the boundaries:

- universe owns persistent identity,
- selection owns current-frame inclusion,
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
- 2: static scene universe feeds diagnostics/mode 19 only.
- 3: static scene universe feeds static buffers; dynamic drawSurf fallback.
- 4: static + rigid scene universe feeds buffers; transient drawSurf fallback.

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

Stage 5: Rigid Entity Inventory
-------------------------------

Add rigid entity records separately from drawSurfs.

Record:

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

Convert model surface vertices directly into the dynamic buffer using PT-owned
code. Do not call frontend drawSurf allocation or pretend to be a viewEntity.

Initial exclusions:

- skinned/deformed characters,
- callback-generated custom shader objects,
- particles/sprites/flares/beams,
- GUI surfaces,
- overlays/decals,
- continuously generated dynamic models.

Those remain current-frame drawSurf fallback until separate identity/motion
handling exists.

Stage 6: Rigid Emissive Injection
---------------------------------

For rigid records with trustworthy identity and transform:

- derive model emissive light records,
- transform them per frame,
- assign area/portal membership from transformed center/off-center points,
- inject them into selected light lists for the current frame,
- keep previous-to-current remapping data for future temporal reuse.

If identity, transform, runtime state, or matching occluder geometry is not
trustworthy, keep the emissive current-frame only.

Stage 7: Transient Fallback
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

Stage 8: Temporal/ReSTIR Preconditions
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

The next code slice should be diagnostics-only:

1. Add `r_pathTracingSceneSource` and keep default `0`.
2. Add a `PathTraceSceneUniverse` skeleton that can walk static world records
   once and count surfaces/materials/emissive-capable records.
3. Add area/portal membership diagnostics for those static records.
4. Add a one-shot dump:

       r_pathTracingSceneUniverseDump 1

5. Do not feed the new records into BLAS or mode 20 yet.

Only after that dump looks sane should the static scene universe feed mode 19
diagnostics, then the static BLAS input, then mode 20 light selection.
