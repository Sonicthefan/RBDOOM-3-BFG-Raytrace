RBDoom3 BFG RT Material Universe Notes
======================================

Purpose
-------

PathTraceMaterialUniverse is the CPU-side owner for persistent RT smoke material
records. It stores stable material facts and stable material-universe indexes
that can survive frame-local material table rebuilds.

This is not the shader descriptor table. Per-frame descriptor indexes, visible
texture bindings, compact active material row order, and texture probe state
belong to PathTraceDynamicMaterialState.

Guardrails
----------

Material scope:

- Store stable facts only: material id, stable universe index, fallback albedo,
  material flags, alpha/cutout facts, texture availability facts, emissive facts,
  and similar data derived from material/texture metadata.
- Do not store current-frame descriptor indexes or visible texture binding slots
  in persistent material records. Those are rebuilt by
  PathTraceDynamicMaterialState after the visible texture set is known.
- Do not treat dynamic/game-logic material expression state as stable truth
  unless a future bridge explicitly records that state and invalidation rule.

Identity:

- The shader-facing materialId remains the Doom material-name hash used by the
  active material table and triangle material arrays.
- Persistent record lookup now keys by materialId plus a 64-bit material-name
  hash. This keeps the existing shader contract while reducing the risk that a
  rare materialId collision causes two persistent records to oscillate.
- The record's universeIndex is stable persistent identity. It is not a compact
  active table row.

Lifecycle:

- ClearSmokeMaterialUniverse() resets persistent records, stats, validation log
  throttling, and universe-index allocation.
- Use ClearSmokeMaterialUniverse() at full cache reset boundaries: level unload,
  renderer shutdown, vid_restart, map changes, or a future explicit PT cache
  reset command.
- Do not clear the universe every frame. The point of the universe is to avoid
  rediscovering and rebuilding stable material records during normal gameplay.

Growth and allocation:

- ReserveSmokeMaterialUniverse() reserves the persistent lookup map before a
  material table build. The current table builder reserves from the approximate
  static+dynamic material id count plus slack.
- Future level-load estimates should reserve closer to the expected level
  material count to avoid unordered_map rehash/allocation hitches during first
  discovery.
- Avoid adding per-surface heap allocation in hot material paths.

Validation cost:

- Persistent-record validation is gated by r_pathTracingMaterialUniverseValidate.
- r_pathTracingMaterialUniverseValidate defaults to 0 and should stay off during
  normal gameplay.
- Table-compatibility validation remains separate behind
  r_pathTracingMaterialUniverseTableValidate.
- Keep heavy validation, duplicate scans, or broad recomputation behind explicit
  diagnostic CVars.

Thread ownership:

- PathTraceMaterialUniverse is single-owner mutable render/main-thread state
  today.
- Worker jobs may prepare immutable material candidates or texture metadata
  packets, but should not mutate the global persistent record map directly.
- If threaded mutation is ever needed, add a deliberate synchronization and
  snapshot design first.

Stats:

- GetSmokeMaterialUniverseStats() reports persistent record count, universe
  index count, cumulative hits/misses/rebuilds/signature checks, cumulative
  validation checks/mismatches, and frame-local versions of the churn counters.
- BeginSmokeMaterialUniverseFrame() resets the frame-local counters before the
  material table path runs.
- Existing smoke logs print both cumulative and frame-local counters so material
  churn, signature recomputation, cache-hit behavior, and validation cost are
  visible without enabling expensive validation.

Avoiding redundant work:

- The active material table should call GetSmokePersistentMaterialRecord once per
  unique active material row, not once per surface/triangle when a stable mapping
  already exists.
- Static triangle material-index buffers are tied to compact active table row
  order. Do not casually change active row order; validate with
  r_pathTracingMaterialUniverseTableValidate when touching that path.
- Future geometry-universe records may cache resolved universeIndex values, but
  shader-visible row indexes must still come from PathTraceDynamicMaterialState.

Relation to geometry universe:

- Geometry records store persistent materialId as their stable material link.
- Material records map that materialId/name identity to persistent facts and a
  stable universeIndex.
- PathTraceDynamicMaterialState maps visible material ids to compact
  shader-visible material rows and descriptor slots for the current frame.
