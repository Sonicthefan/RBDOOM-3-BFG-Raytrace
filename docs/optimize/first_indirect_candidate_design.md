# First-Indirect Candidate Design

## Goal

Keep Clean DI and Clean GI as separate reservoir systems for now, but stop
designing ray/path work as GI-only machinery. New first-indirect tracing should
produce an estimator-neutral candidate that Clean GI can consume today and a
future ReSTIR PT Enhanced-style unified reservoir experiment can consume later.

This is the robust standalone path, not a piggyback on a defunct mode-56 path.

## Current Direction

The existing Clean GI producer is still the active consumer:

- `CleanGI.0a` traces the first indirect bounce and writes a trace-to-shade
  surface record.
- `CleanGI.0b` shades that secondary surface and writes GI producer radiance.
- `CleanGI.1/2` run temporal/spatial reuse and resolve.

The trace-to-shade record is now named as a first-indirect candidate surface in
shader code:

`neo/shaders/builtin/pathtracing/cleanroom_common/pathtrace_first_indirect_candidate.hlsli`

Clean GI aliases this record for compatibility, so the current rendering path is
unchanged. The host buffer stride remains 144 bytes.

## Design Rules

1. DI and GI reservoirs stay separate until explicitly opened as a later stage.
2. The first-indirect producer should not encode GI-only assumptions into its
   core payload.
3. Candidate data should preserve the fields needed for later reconnectibility:
   world position, geometric and shading normals, view direction, roughness,
   material identity, instance/primitive identity, sampled lobe state, path
   length, radiance, and source PDF/weight terms where available.
4. Direct-light visibility should be selected-sample or reuse-stage work, not
   repeated for every random candidate unless a diagnostic proves it is needed.
5. Any new split must move genuinely different work into different kernels.
   Splitting trace from shade is valid; splitting the same live state across two
   broad kernels is not.

## Intended Next Shape

The next producer rewrite should move from "CleanGI producer" toward:

```text
Primary surface
  -> first-indirect ray sample
  -> first-indirect trace candidate
  -> first-indirect shade candidate
  -> Clean GI initial reservoir consumer
```

The last step is GI-specific. The earlier steps should remain reusable.

## Explicit Non-Goals

- Do not merge DI and GI reservoir pages in this increment.
- Do not implement ReSTIR PT Enhanced paper features yet.
- Do not revive mode 56 as the active production path.
- Do not hide producer failures with brightness floors, roughness gates, or
  broad fallback rendering.

## Future Unified-Reservoir Hook

If we later try the paper's unified DI/GI reservoir idea, the handoff point is
the first-indirect candidate result, not the current Clean GI reservoir page.
The unified experiment should consume direct-light candidates and first-indirect
candidates through a common candidate interface, while retaining enough metadata
to evaluate target functions and reconnectibility correctly.
