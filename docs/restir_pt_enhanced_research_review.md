ReSTIR PT Enhanced Research Review
==================================

Date: 2026-05-10

Local paper:

    E:\prog\references\lin2026restirptenhanced.pdf

Official page:

    https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/

Scope
-----

Compare the new ReSTIR PT Enhanced paper direction against this branch's current
ReSTIR/PT bridge and refactor plan. This is planning guidance only; it does not
change renderer behavior.

Current Branch Baseline
-----------------------

The branch already matches several major production-shape requirements:

- ReSTIR PT uses a separate RTXDI packed reservoir buffer, not the older smoke
  reservoir ABI.
- `PathTraceRestirPT` uploads RTXDI PT parameters, including hybrid shift,
  reconnection, temporal, boiling-filter, and spatial parameter blocks.
- Modes 26-33 validate initial and temporal reservoir paths, primary-surface
  history, camera reprojection, temporal shading preview, and source
  attribution.
- The current core refactor plan explicitly wants separate initial, temporal,
  spatial, visibility/final shading, and debug visualizer passes.
- RAB initial sampling already records NEE and emissive/indirect path events
  through the PT path-tracer context rather than treating the reservoir as a
  direct-light-only container.

Main Conclusion
---------------

The paper does not invalidate the current plan. It mostly confirms that the
branch is moving in the right direction: explicit passes, RTXDI-shaped
reservoirs, hybrid shift/reconnection, broad candidate sources, and profiled
stage ownership.

There are, however, several items that should be pulled earlier into the plan
because they affect pass layout, resource ownership, and validation semantics.
They should be designed before spatial reuse and final shading are treated as
stable.

Early Include Items
-------------------

1. Duplication map / duplication-based history reduction.

This is the most concrete early addition. The current local bridge has
`RAB_GetDuplicationMapCount()` returning 0, and RTXDI defaults keep
duplication-based history reduction disabled. The reference sample has explicit
FillSampleID and ComputeDuplicationMap passes plus a `PTDuplicationMap` texture.

Do not enable the RTXDI flag until the branch owns:

- a sample-id output for the selected PT reservoir sample,
- a duplication-map resource,
- a compute/raygen pass that fills the map after temporal/spatial reservoir
  output,
- a debug view,
- pass barriers and timing around the extra work.

This should become a task adjacent to the temporal/spatial pass split, not a
late denoiser-only feature. Without it, temporal reuse can over-trust duplicated
samples and spatial boost decisions remain less robust.

2. Reciprocal neighbor selection for spatial reuse.

The current plan says to add spatial resampling with neighbor tests, but it does
not specify the new paper's reciprocal-neighbor strategy. Since the branch is
not copy-pasting NVIDIA's pass layout anyway, spatial pass design should reserve
room for a reciprocal/pair-aware neighbor policy instead of hard-baking a naive
"each pixel pulls K random neighbors" path as the only implementation.

This does not need to block initial or temporal validation. It should shape the
first serious spatial task because it changes how neighbor offsets, selected
neighbors, debug views, and performance measurements are interpreted.

3. Footprint-based reconnection must remain the default validation target.

The local RTXDI defaults already use footprint reconnection parameters. Keep
that direction. Do not regress to fixed distance thresholds to make early tests
look less noisy. The local plan should expose and dump reconnection parameters
when debugging temporal/spatial artifacts, because bad footprint inputs will
look like reservoir instability even when the reservoir math is correct.

4. Unified direct/global reservoir semantics should guide final shading.

The paper emphasizes unifying direct and global illumination in the same
reservoirs. This branch's RAB path already moves that way by recording NEE,
emissive hits, and continuation-path events into RTXDI PT reservoirs.

Implication: mode 20 and native mode 18 direct-light paths should remain
validation/proposal references, not the long-term architecture. Doom analytic
lights and emissive triangles should feed the PT initial-sampling/proposal
system; final ReSTIR PT shading should consume one coherent PT reservoir stream
rather than separate "direct reservoir" and "indirect reservoir" products unless
a later denoiser split explicitly requires that output decomposition.

5. Disocclusion and color-noise controls should be planned before raising
resolution.

The branch already reports mode 31/32 blue edge bands and high-resolution
device-removal risk. The paper's direction says robustness is not only a
denoiser afterthought. Before making temporal/spatial modes the default visual
path, add explicit diagnostics for:

- disocclusion / fallback sampling decisions,
- spatial boost decisions,
- selected-sample duplication counts,
- color/luminance clamping or firefly guards,
- final visibility and preview visibility cost.

This is still after baseline correctness, but before calling the temporal or
spatial result production-ready.

Non-Divergences
---------------

These current plan items still look correct and should not be rewritten because
of the paper:

- Do not shrink the Doom authored light universe to a tiny list. Keep broad
  candidates and improve proposal distributions.
- Keep RTXDI packed PT reservoirs separate from local smoke reservoirs.
- Keep RAB callbacks as the contract boundary rather than importing sample-app
  renderer structure.
- Split monolithic raygen work into named passes with barriers and timing.
- Keep visibility separately toggleable and profiled.
- Keep primary surface, material, roughness/F0, motion, and object identity
  fields reserved even while early validation is diffuse-only.
- Keep Release/PT validation separate from Debug/validation-layer timing.

Recommended Plan Insertions
---------------------------

Add these before or inside `task_05_restir_pass_split.txt`:

1. Add a duplication-map subtask after temporal output exists and before spatial
   reuse is judged:

       temporal output -> fill selected sample ID -> compute duplication map
       -> bind map through RAB_GetDuplicationMapCount -> enable RTXDI flag only
       after a debug view confirms sane counts

2. Expand the spatial pass task to compare at least two neighbor policies:

       baseline RTXDI/random neighbor pull
       reciprocal-neighbor/pair-aware strategy inspired by the paper

   Keep the first implementation behind a CVar or pass-plan option until
   measured in the Release/PT lane.

3. Add reconnection-parameter diagnostics:

       mode, min footprint, footprint sigma, roughness threshold, distance
       threshold, max RC vertex length, selected reservoir RcVertexLength,
       PartialJacobian

4. Treat direct-light mode 20 as a reference path only. The long-term final
   shading task should read the RTXDI PT reservoir selected sample and produce
   lighting from that unified event stream.

5. Add a "temporal/spatial robustness" checklist before lifting the 720p safety
   rule:

       duplication map valid
       disocclusion boost visible/debuggable
       spatial/temporal passes separately timed
       visibility rays separately toggled
       no stale current/previous light identity reuse
       no camera-only reprojection claims for moving-object history

Reviewer Verdict
----------------

No major rewrite is needed. The significant early adjustment is to make
duplication maps, reciprocal-neighbor spatial reuse, and footprint reconnection
diagnostics first-class plan items. These are not cosmetic optimizations; they
change how temporal confidence, spatial cost, and robustness are validated.

The branch should continue with the current modular bridge and pass-split plan,
but avoid stabilizing a naive spatial reuse implementation as "the" architecture
before these paper-driven pieces are accounted for.
