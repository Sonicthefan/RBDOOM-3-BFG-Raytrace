RBDoom3 BFG RT Geometry Universe Notes
======================================

Purpose
-------

PathTraceGeometryUniverse is the CPU-side owner for persistent RT smoke geometry
records. Its first job is the static-world cache: stable surface identity,
shader-facing static geometry arrays, and enough current/previous range
bookkeeping to prepare for future motion-vector work.

This is not a general container for every visible draw surface. Doom 3 BFG is
still mostly single-threaded on the render side, so the universe must stay cheap
in normal gameplay.

Guardrails
----------

Fast lookup:

- Static surface identity lookup must stay close to O(1). TouchStaticSurface
  uses the key-to-record lookup map, not a linear scan over all cached records.
- If records are compacted or reordered later, update or rebuild the lookup map
  at the same ownership boundary.

Validation cost:

- Heavy validation is opt-in through r_pathTracingGeometryUniverseValidate.
- Normal gameplay should not run duplicate-key scans, full range validation, or
  other O(n^2) checks every frame.
- The existing static BLAS summary reports validate=total/range/duplicate/
  history/key-vector. Clean validation is validate=0/0/0/0/0.

Geometry scope:

- The persistent record model is for static and later semi-static geometry with
  stable identity.
- Static-world cache population and routed rigid residency must not depend only
  on the current raster-visible drawSurf list. Raster visibility is a freshness
  source, not a complete RT residency rule.
- Do not route muzzle flashes, particles, sparks, temporary overlays, decals
  that exist for only one frame, or other one-frame transient effects into
  persistent records.
- Transient/dynamic data should stay in the per-frame dynamic capture path until
  it has a deliberate identity/history design.

Area/portal residency:

- Source3 static preload and rigid residency use Doom renderer area/portal
  membership to keep RT geometry available when raster culling stops submitting
  it. The current area source is the renderer area API:
  `PointInArea`, `NumPortalsInArea`, and `GetPortal`.
- Do not use raw BSP leaf/internal cells as the RT residency unit. The useful
  unit in the current implementation is Doom renderer portal areas.
- `PathTraceSceneUniverse` may scan static map/world surfaces and provide
  selected-area static preload records, but it must remain static
  diagnostics/preload. It is not allowed to synthesize dynamic, skinned,
  material-override, callback, or game-logic-driven scene records.
- Rigid residency keeps cached route-ready rigid instances whose last known
  area is in the selected area set. It uses last observed transform/material
  metadata for cache-only instances; this is acceptable for static-ish rigid
  props, but moving off-screen objects need later per-frame update/freshness
  work.
- Current+1 portal selection was disproven as a general validation rule in Mars
  City. Many visually continuous rooms are several renderer portal areas deep.
  Depth 4 is the current validation baseline used by the mode 18/20 preset 4
  stack for static preload, rigid residency, and light-area diagnostics.
- `PS_BLOCK_VIEW` portal edges are counted and reported, but currently do not
  stop source3 static preload, rigid residency, or light-area traversal. This is
  deliberate while validating RT residency because hard-blocking on Doom raster
  visibility state reproduced missing-BVH behavior. Revisit this only with a
  stronger portal taxonomy and targeted tests.

Lifecycle:

- Clear() resets all static records, lookup data, static geometry arrays, and
  generation state. Use it for full scene/cache reset boundaries.
- A static surface that is not seen on the immediately following frame is marked
  disappeared for that frame and its history is invalidated. Long-unseen cached
  surfaces are retained without staying dirty forever.
- Current static geometry memory is not reclaimed per disappeared surface.
  Reclaiming memory requires a deliberate compaction/rebuild path.
- Compaction or rebuild is appropriate at level/load boundaries, explicit debug
  reset commands, or a future full-universe rebuild pass. It should not happen
  opportunistically in the middle of normal frame capture.

Thread ownership:

- PathTraceGeometryUniverse is single-owner mutable state today.
- Worker jobs may prepare immutable append/upload packets from snapshots, but
  only the render-thread owner should mutate universe records, lookup maps, or
  backing vectors unless a deliberate synchronization design is added.
- Do not make helpers retain raw record pointers across a mutation that can grow
  or compact the record vector.

Material link:

- Static surface records store the persistent material id captured from Doom.
- That id is the bridge to PathTraceMaterialUniverse records and facts.
- PathTraceDynamicMaterialState still builds the compact shader-visible material
  table for the current frame. Do not treat a record materialId as a direct
  shader table row unless the active table builder explicitly maps it.

Growth and allocation:

- Reserve record and lookup capacity where approximate level surface counts are
  known. Scene capture reserves static record capacity from viewDef draw-surface
  counts before appending new static records.
- Keep static vertex/index/class/material vectors bounded by the existing RT
  smoke caps until GPU buffer ownership moves into the universe.
- If future work adds per-level estimates, reserve static records and backing
  arrays at level-cache creation rather than growing them one surface at a time.

Near-Term Direction
-------------------

The next useful work is still CPU-side and behavior-preserving:

- add a one-shot diagnostic dump for invalid/dirty records,
- define a deliberate cache reset/compaction command,
- then start a separate identity/history design for rigid dynamic surfaces.

Do not expand the persistent record model to every dynamic visual effect just
because the static cache has history fields. Motion vectors need identity, but
not every visible triangle deserves persistent identity.
