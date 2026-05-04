RBDoom3 BFG RT/PT BLAS Replacement Plan
=======================================

Purpose
-------

This note records the current "raster-style BLAS" problem in the experimental
RT/PT path and the plan for replacing it with a path-tracing-owned scene
selection model.

The short version: `viewDef->drawSurfs` is a raster draw list. It answers "what
should this camera draw right now?" Path tracing needs a different answer:
"what geometry and emissive surfaces can affect rays near this shaded point or
near the player?" Treating the raster answer as the RT scene source is the root
of most current pop-in and occlusion instability.

Current Pitfalls
----------------

Raster visibility is not ray visibility:

- `viewDef->drawSurfs` has already passed through Doom's raster-facing portal,
  frustum, scissor, occlusion, material, and draw-surface decisions.
- A surface can be invisible to the camera and still be required for ray
  occlusion, shadows, emissive lighting, or secondary bounces.
- If a dynamic object leaves the raster-visible draw list, it also leaves the
  current dynamic BLAS, which causes shadow/occlusion pop-in.

The current static cache is still seeded by visible surfaces:

- PathTraceGeometryUniverse stabilizes static surface identity and upload cost,
  but the current population path still discovers surfaces through the frame's
  drawSurf list.
- This makes it more stable than pure current-frame dynamic capture, but it is
  still not a complete map/world geometry inventory.

Mode 20 light candidates inherit the same visibility problem:

- Visible/frame emissive candidates are useful diagnostics, but they miss
  off-camera emissives that should affect nearby shading.
- Injecting full-map emissives directly into mode 20 proved that the data is
  probably affordable, but the naive version was wrong: it scanned too much at
  frame time and gave the reservoir a global unpartitioned light list.

Fake drawSurfs are a trap:

- A first attempt to retain nearby dynamic occluders by fabricating drawSurf and
  viewEntity state inside PT capture caused level-load black screens/hangs.
- Doom frontend drawSurf creation has assumptions about frame allocation,
  shader registers, callbacks, dynamic models, visibility state, and renderer
  timing. PT capture should not synthesize pretend frontend surfaces as a
  shortcut.

Global brute force is also a trap:

- A full-level static emissive sweep dramatically increased GPU work and could
  trigger device removal, even though it showed the hardware has substantial
  headroom.
- The failure was not "Doom levels are too large." The failure was frame-time
  discovery plus global unpartitioned sampling plus no portal/area membership.

Design Goal
-----------

Raster visibility should become a hint, not the source of truth.

The path tracing scene builder should own a compact scene universe that can
answer:

- which static world geometry belongs to the current path-traced region,
- which static emissives belong to that region,
- which rigid entity geometry is nearby or in connected areas,
- which rigid emissives are nearby or in connected areas,
- which transient/skinned/particle surfaces must remain current-frame only,
- which raster-visible drawSurfs are useful fallback/debug inputs.

Replacement Plan
----------------

1. Keep the current baseline stable.

- Keep mode 18 and mode 20 playable through the existing visible drawSurf path.
- Keep `r_pathTracingWorldStaticEmissives` defaulted to `0`.
- Keep any dynamic occluder-retention experiment defaulted to `0` unless it is
  rebuilt without fake drawSurf synthesis.
- Do not destabilize the stable path while building the replacement.

2. Build a PT-owned static world inventory.

- Populate static world geometry from renderer/world/model data at map-load,
  first PT use, or an explicit rebuild boundary.
- Do not scan the entire static world during the per-frame scene build.
- Store material ids, surface identity, geometry ranges, world bounds, and
  future area/portal membership.
- Preserve the existing PathTraceGeometryUniverse guardrails: O(1)-style lookup,
  explicit reset/compaction boundaries, and no expensive validation by default.

3. Add static emissives to the same inventory.

- Derive static emissive candidates from the cached static geometry/material
  universe rather than from current raster visibility.
- Store enough identity to make mode 20 candidate origins debuggable.
- Do not feed all static emissives globally into mode 20 by default.

4. Attach coarse spatial membership.

- Start with Doom portal/area membership where it is available.
- First selection rule can be deliberately simple: current area plus connected
  areas within N portal steps, optionally bounded by radius.
- For surfaces spanning areas or lacking clean area assignment, use conservative
  fallback membership and diagnostics rather than silently dropping them.

5. Add rigid entity records separately from drawSurfs.

- Represent ordinary rigid entity model surfaces as PT records with entity
  index/handle, model pointer or stable model id, surface index, material id,
  transform, bounds, and area refs.
- Update transforms and runtime material state per frame.
- Do not synthesize drawSurfs for these records. Convert model surfaces to RT
  vertices directly through PT-owned code paths.
- Keep skinned, callback, particle, overlay, GUI, flare, and other transient or
  continuously generated surfaces on the current-frame drawSurf fallback until
  they have a deliberate identity/motion design.

6. Assemble BLAS from PT scene data.

- Static BLAS should come from selected cached static regions, not from "what
  the camera drew this frame."
- Rigid dynamic BLAS should come from selected rigid entity records, not from
  raster visibility.
- Current-frame raster drawSurfs should remain a fallback for genuinely
  transient/skinned/weird surfaces.

7. Feed mode 20 from the same selected region.

- Static emissive candidates come from selected static records.
- Rigid emissive candidates come from selected rigid entity records.
- Transient emissive candidates come from current-frame fallback drawSurfs.
- Whole-map emissive sampling can remain a stress/debug path, but it should not
  be the default reservoir input.

Diagnostics Needed
------------------

Before adding spatial reuse or temporal reuse, add compact diagnostics that show:

- player/current area id,
- selected portal/area count,
- selected static surface and triangle counts,
- selected rigid entity surface and triangle counts,
- selected static emissive count,
- selected rigid emissive count,
- current-frame fallback drawSurf count,
- omitted surface/entity counts by reason,
- whether mode 20 sampled a visible/frame, static, rigid, or transient candidate.

These diagnostics should answer "what did the PT scene include and why?" without
requiring shader-resource contract changes unless absolutely necessary.

Near-Term Slice
---------------

The next useful implementation slice is not another radius hack inside current
drawSurf capture. It is a small, read-only inventory/diagnostic pass that walks
renderer world data and reports candidate static/rigid records plus coarse
area/portal membership, without feeding those records into BLAS yet.

Once the inventory report looks sane, wire a capped static-only selection into
mode 19 diagnostics, then into BLAS/mode 20 behind an opt-in CVar.
