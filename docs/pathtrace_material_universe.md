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
- Future reflection, glass/transmission, denoise, upscale, and DLSS/RR work
  should extend these stable facts semantically before adding shader-visible
  resources. Reserved semantic fields include roughness, F0/specular color,
  metallic when a conventional source exists, opacity, alpha cutoff,
  transmission/tint color, default IOR, thickness approximation, height/parallax
  or displacement height, material class flags, and texture-role availability.
- Do not store current-frame descriptor indexes or visible texture binding slots
  in persistent material records. Those are rebuilt by
  PathTraceDynamicMaterialState after the visible texture set is known.
- Do not treat dynamic/game-logic material expression state as stable truth
  unless a future bridge explicitly records that state and invalidation rule.
- Prefer engine material/image/stage state over filename/path hints. Filename
  tokens can remain compatibility tie-breakers for legacy content, but they are
  not the final material model.
- Preserve dynamic/evaluated material state separately from stable material
  capability. For example, a material may be capable of transmission or emissive
  contribution while the current frame's material parms, animations, or
  GUI/cinematic state make the evaluated value different.

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

Reflection, Glass, And Denoise Reservations
-------------------------------------------

Reflection/specular:

- MaterialUniverse should eventually provide roughness and F0/specular facts
  from the best available engine/PBR source. Doom legacy specular-map
  conversion is a stopgap until material roles are interpreted more completely.
- Reflection history must not be folded into diffuse/ReSTIR history. Future
  reflection output/history should have separate names and reset policy, and
  should carry hit distance, confidence, roughness, F0, geometric normal,
  shading normal, and motion information needed by denoisers/upscalers.
- Half-resolution or checkerboard reflection policy is allowed later, but it
  should be expressed as a pass setting, not hidden in material classification.

Glass/transmission:

- Current glass/window handling is a debug visibility approximation. Do not
  promote it to final policy by treating all glass as ordinary alpha test.
- Reserved material facts are opacity, alpha cutoff, default IOR, tint or
  transmission color, thickness approximation, and Doom window/liquid class
  flags. Nested surface and liquid behavior remain user-decision items.
- Ask before finalizing Doom-specific glass/liquid behavior. Until then,
  transmission bounce limits should remain reserved/fail-closed and any visual
  glass handling should stay clearly labeled as temporary.

Denoise/upscale/Ray Reconstruction:

- Required material-facing inputs are albedo, normal, roughness, depth,
  specular/F0, diffuse/specular separation, emissive contribution, exposure
  context, and history reset reasons. MaterialUniverse should own stable
  material capability/facts; frame resources or pass outputs should own the
  evaluated current-frame images.
- Do not integrate DLSS/RR against camera-only motion. Ray Reconstruction needs
  geometry/material/history contracts that can distinguish current and previous
  surface identity and motion.
