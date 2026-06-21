# Clean ReSTIR GI — Optimization Results & Lessons

Chronological log of what was tried on the clean ReSTIR GI lane, with numbers and
reasoning. GPU profiled with Nsight Graphics GPU Trace (see
[`profiling_setup.md`](profiling_setup.md)). Test config: `maxBounces = 1`,
specular producer **on**, NEE-cache seed off.

Current tuning note: the first-bounce GI NEE-cache seed is now a useful opt-in
when the cache producer is enabled:
`r_pathTracingNeeCacheEnable 1`, `r_pathTracingCleanRestirGiNeeCacheSeed 1`,
`r_pathTracingCleanRestirGiNeeCacheSecondary 0`. User A/B after the
mixture-PDF fix measured this path faster than seed-off. Enabling secondary GI
NEE cache (`r_pathTracingCleanRestirGiNeeCacheSecondary 1`) roughly broke even;
secondary modes 1 and 2 showed no meaningful performance difference in that
test, so the current performance recommendation is primary seed on, secondary
off.

## Starting point

`ProducerRayGen` (the indirect candidate producer) was ~77% of the GI frame,
stalls dominated by **"No Instructions" ~65%** with low RT-core saturation —
classic megakernel: one `traceRay` (incoherent diffuse bounce) followed by the
entire uber-material decode + 4-way direct NEE (NEE-cache / RLU-RIS / analytic /
emissive, each with shadow rays) inlined in raygen.

---

## What WORKED (kept)

### 1. Enable `VK_EXT_debug_utils` (foundational, not perf)
Without it every marker + resource name was a no-op. This is what made all
subsequent profiling possible. See `profiling_setup.md` §1.

### 2. Producer trace/shade split  ✅ kept
`ProducerRayGen` → **`ProducerTraceRayGen`** (bounce trace + geometry/material
reconstruction → writes a per-pixel `CleanGiProducerSurface` G-buffer, a
`RWStructuredBuffer` at `u92`, 144 B/pixel mirroring `RAB_Surface`) +
**`ProducerShadeRayGen`** (reads the G-buffer, re-seeds the identical RNG stream
skipping the 2 bounce randoms, runs the divergent direct NEE, writes radiance +
owns debug views). Same RT pipeline, two shader tables, dispatch order
Trace → Shade → Reuse.
- **Result:** `IndirectProducerTrace` → **0% "No Instructions"** (now RT-core
  bound — the healthy state). `IndirectProducerShade` → **65% → 49%** (narrower
  than the combined megakernel, but still carries the 4-way light divergence).
  Net modest improvement.
- **Why it worked:** it redistributed *genuinely different work* — RT-bound
  tracing vs. divergent shading — so the trace half became narrow and fast.

### 3. Specular-producer seed extraction  ✅ kept (~1 ms)
`CleanGiSeedInitPageFromSpecularProducer` (which runs a **full** specular bounce
trace + secondary NEE shade — a second producer) plus the INIT-page clear and
NEE-cache seed were running *inlined inside the temporal pass*. Moved them into a
new **`SeedRayGen`** pass (`CleanGI.0c InitSeed`) dispatched after the diffuse
producer and before the temporal contract, with a reservoir barrier between.
- **Result:** ~**1 ms** net better. Specular cost now isolated in
  `InitSeed` (**69% NI** — still a broad trace+shade megakernel).
  `TemporalReuse` dropped some work but its NI **rose to 85%** (see below).
- **Why it worked:** redistributed real, different work (a whole specular
  producer) out of the temporal entry point. Also cleanly isolates the specular
  producer so it can be trace/shade-split next, like the diffuse producer.
- **Gotcha:** the first attempt "failed to create GI pipeline" / purple screen —
  this was a **build desync** (stale exe vs. fresh shaders), not the design. A
  clean full build of exe + shaders together fixed it.

### 4. Mixed first-indirect specular mode - default (~8 ms)
`r_pathTracingCleanRestirGiSpecularProducer 2` replaces the separate full-screen
specular seed trace/shade (`FirstIndirect.0d/0e`) with a one-ray diffuse/specular
mixture in the normal `FirstIndirect.0a` producer. Low-roughness/high-F0
receivers use the same temporal/spatial reuse quarantine as the full specular
seed mode to avoid reflection smearing.
- **Result:** user Nsight A/B measured mode 2 about **8 ms faster** than mode 1;
  `FirstIndirect.0d/0e` accounted for roughly that whole delta. Extended DLSSRR
  playtesting found no detectable visual difference.
- **Why it worked:** it removes the duplicate full-screen specular first-indirect
  producer instead of optimizing around it. Mode 1 remains as the reference
  fallback; mode 2 is the default.

### 5. GI-owned secondary RLU candidate cap - A/B default 2
After removing `FirstIndirect.0d/0e`, `FirstIndirect.0b ShadeFast` became the
largest event. Its "fast" path still ran the DI-owned RLU RIS candidate loop
(`r_pathTracingCleanRtxdiDiCandidateCount`, default 8) before tracing the chosen
visibility ray. `r_pathTracingCleanRestirGiSecondaryRluCandidates` now caps only
GI first-secondary direct-light candidate selection; `8` reproduces the old
behavior, default `2` is the performance candidate.

### 6. GI NEE-cache first-bounce seed - opt-in performance candidate
`r_pathTracingCleanRestirGiNeeCacheSeed 1` lets the temporal initial sample merge
an INIT-page reservoir seeded from the NEE cache before the normal GI producer
sample is added. This is only useful when `r_pathTracingNeeCacheEnable 1` is also
set, because that global cvar owns provider resource allocation and candidate
builds.
- **Result:** user A/B measured performance up with the seed path enabled and
  `r_pathTracingCleanRestirGiNeeCacheSecondary 0`.
- **Why it worked:** it adds a first-bounce light proposal from the shared
  cache without replacing the normal GI producer sample. The post-fix source PDF
  accounts for cache/fallback as one proposal mixture, matching the stable DI
  additive cache shape and avoiding the old under/over-priced branch behavior.
- **Secondary note:** enabling secondary GI cache consumed the gain, roughly
  breaking even. Modes 1 and 2 showed no meaningful performance difference, so
  leave secondary off unless visual tests in small-emissive scenes justify the
  cost.

---

## What did NOT work (reverted, or kept but zero perf)

### A. Dedup redundant per-hit material loads — zero change
The producer loaded the material index / class+flags / full `PathTraceSmokeMaterial`
struct twice per secondary hit. Hoisted to a single load. **No measurable
change** — the shader compiler already CSE's idempotent SRV loads. Behavior-
preserving; harmless. (Lesson: micro-load-dedup won't move a "No Instructions"
bottleneck.)

### B. Compile-strip the unused continuation bounce — zero change, reverted
At `maxBounces = 1` the secondary->tertiary continuation never executes, but it's
gated by a *runtime* cbuffer value, so DXC compiles it in. Hypothesis: it inflated
worst-case register allocation → low occupancy. Added `-D CLEANGI_CONTINUATION_BOUNCE=0`
to dead-strip it. **No change.** Dead code behind a runtime branch was *not*
inflating the hot-path register count, and unexecuted code causes no I-cache
misses. Reverted.

### C. Temporal/spatial phase split — REGRESSED ~3 ms, reverted ❌
`ReuseRayGen` ran temporal (phase 0) and spatial (phase 1) as two **separate
dispatches sharing one entry point**, branched on a runtime `CleanRestirGiPhase`
flag. Split into `TemporalReuseRayGen` + `SpatialReuseRayGen`. **Regressed ~3 ms;
temporal NI ticked up.** Reverted.
- **Why it failed (key lesson):** the split **redistributed no work** — the two
  phases were already separate dispatches. Giving each its own entry point
  changed nothing about what each thread executes, so no occupancy gain, plus
  overhead. The "runtime flag => union compilation => low occupancy" theory did
  **not** hold here; DXC was handling the shared entry point's registers fine.

### D. Fat reservoir / payload hypothesis — ruled out
Checked the data sizes: `RTXDI_PackedGIReservoir` = **32 bytes** (8×uint32,
`static_assert`'d), 4 INIT/temporal/spatial pages → 128 B/pixel;
`PathTraceCleanRestirGiPayload` = **40 bytes** (`maxPayloadSize = 64`). All lean.
The reuse passes are **not** memory-bandwidth bound — don't chase reservoir/
payload shrinking. "No Instructions" (front-end), not "Long Scoreboard" (memory),
confirms this.

### E. SER (Shader Execution Reordering) — not pursued as primary
`VK_NV_ray_tracing_invocation_reorder` is unused. Per the user, SER gives only
~10% on this content in Remix and **does not raise occupancy** (reordering N
warps does nothing if you only have N warps resident). It's a finisher for
residual material divergence, not a fix for the big gaps. Deferred.

---

## Current bottleneck floor

After the wins above, the single largest GI event is the **temporal resampling
contract** (`CleanGiRunTemporalContract` → RTXDI GI temporal reuse), now at
**85% "No Instructions"** as **pure compute** (no `traceRay` in
`restir_gi_temporal_resampling.hlsli`). It is occupancy-starved on *executed*
code (likely the target-function evaluation that re-shades the stored sample to
weight it). The phase-split regression proved this is **not** fixable by moving
code out of the entry point. Remaining honest levers:
1. **Trace/shade-split `InitSeed`** (the 69% specular producer) — proven pattern,
   likely another ~1 ms-class incremental.
2. **Reduce live state in the temporal contract** to raise occupancy — needs the
   pass's registers/thread + achieved-occupancy number to know if it's
   register-limited (fixable) or genuinely I-cache-bound (algorithmic only).
3. **Algorithmic** (out of scope here): the "enhanced ReSTIR PT" optimizations
   (forced NEE reconnect, replay compaction, paired spatial reuse, Russian
   roulette, unify DI & GI). Note the user runs **separate DI + GI lanes**;
   "Unify DI & GI" is the biggest single line in that paper's table.

## Diagnostic playbook (what to do per slow pass)
1. Enable markers (`r_pathTracingNsightGpuMarkers 1`), capture GPU Trace.
2. Read the stall column: **No Instructions** → broad entry point / occupancy;
   **Long Scoreboard** → memory.
3. Use the diagnostic cvars (profiling_setup §4) to localize cost *inside* the
   pass before any code change.
4. Only split a raygen if it moves *genuinely different work* into the new pass.
   If it just relocates the same work, expect no gain or a regression.
5. Always validate pixel-identical output (these splits are behavior-preserving)
   and compare the **sum** of the new passes vs. the old single pass.
