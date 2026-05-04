RBDoom 3 BFG PT Scene Producer Replacement Guide
================================================

This guide is the implementation-facing companion to
`pathtrace_scene_producer_q2rtx_plan.md`. Keep the Q2RTX plan as background
research. Use this file as the next coding target because it narrows the work to
getting a reliable path-traced scene producer first, then optimizing and
specializing later.

Core Verdict
------------

The previous plan contains correct lessons:

- `PathTraceSceneUniverse` should not become the universal scene producer.
- Rigid entities must not be baked into a world-space static cache.
- Mesh identity and instance identity must be separated.
- Doom area/portal membership matters later for emissive light sampling.
- Static emissives should eventually become persistent light records.
- Temporal/ReSTIR reuse requires stable light and instance identity.

The mistake is trying to build a perfect Q2RTX-like scene, light, and temporal
system before the geometry scene works.

The replacement order is:

1. Preserve the current fallback.
2. Keep `PathTraceSceneUniverse` as static-map preload/diagnostics only.
3. Add a final drawSurf mirror for live frame truth.
4. Feed a Q2RTX-style dynamic-frame BLAS from live drawSurfs.
5. Build TLAS from static plus dynamic buckets.
6. Only then promote stable rigid meshes to reusable BLAS plus TLAS instances.
7. Only then add persistent emissive light records and area/portal light lists.
8. Only then revisit temporal/ReSTIR remapping.

Non-Negotiable Rules
--------------------

Do not bake rigid entities into static world geometry:

    bad:
        rigid entity -> transform vertices to world space -> append to static cache

    correct early fallback:
        rigid/unknown live drawSurf -> append to per-frame DynamicFrameBLAS

    correct later optimization:
        rigid mesh -> local-space reusable BLAS
        rigid instance -> TLAS transform updated per frame

Mesh keys must not include transforms. A mesh key identifies geometry. It must
not include:

- model matrix,
- current origin,
- current axis,
- current view visibility,
- current area,
- current frame number.

Transform belongs to an instance record.

Static-world scanning is not the live scene. `PathTraceSceneUniverse` may scan
static map/world surfaces from `renderWorld`, but it must not try to own:

- doors,
- movers,
- props,
- monsters,
- weapons,
- skinned/deformed models,
- particles,
- GUIs,
- callback-generated geometry,
- decals/overlays,
- transient effects.

Those stay in the live drawSurf/dynamic path until explicitly promoted.

DrawSurf capture is the live truth. For anything that is not proven static-world
geometry, the first reliable source is the final visible raster drawSurf stream.
This does not make the drawSurf list the final perfect path-tracing scene; it
means it is the safest live input while the real scene producer is being built.

Do not implement ReSTIR, temporal reuse, or light-list features before geometry
works. Persistent emissive lights, area lists, and temporal remapping are later
stages.

Target Architecture
-------------------

Modules:

    PathTraceSceneUniverse
        Static map/world scanner only.
        Builds static-world records and diagnostics.
        Optional source for StaticWorldBLAS preload.

    PathTraceDrawSurfCapture
        Mirrors final visible raster drawSurfs.
        Creates live MeshObservation and InstanceObservation records.
        Primary source for non-static and unknown geometry.

    PathTraceGeometryUniverse
        Owns persistent MeshRecord objects.
        Owns reusable BLAS cache later.
        Mesh identity excludes transform.

    PathTraceInstanceUniverse
        Owns per-frame InstanceRecord objects.
        Owns current transform, previous transform, entity identity, visibility flags.
        Becomes TLAS input later.

    PathTraceDynamicFrameGeometry
        Temporary per-frame world-space dynamic geometry bucket.
        Rebuilt every frame.
        Used before rigid promotion is ready.

    PathTraceSceneProducer
        Merges static-world preload plus dynamic-frame drawSurf fallback.
        Produces the current shader-compatible static/dynamic buffers first.
        Later produces multi-BLAS/multi-instance TLAS input.

    PathTraceSceneDiagnostics
        Owns compact dumps, counters, and diff logs.

First working BLAS/TLAS shape:

    Static BLAS instance 0:
        static map/world geometry only

    Dynamic BLAS instance 1:
        live non-static/unknown drawSurf geometry rebuilt every frame

    TLAS:
        instance 0 -> StaticWorldBLAS, identity transform
        instance 1 -> DynamicFrameBLAS, identity transform

This is deliberately crude. Static map geometry can be persistent, while
dynamic/effect-like geometry can initially be bucketed and rebuilt each frame.

Scene Source CVars
------------------

Keep existing behavior safe by default:

    r_pathTracingSceneSource 0
        Current stable drawSurf producer only. Fallback/default while developing.

    r_pathTracingSceneSource 1
        Static scene-universe diagnostics only. No BLAS feed.

    r_pathTracingSceneSource 2
        StaticWorldBLAS from PathTraceSceneUniverse plus existing dynamic fallback.
        Static world only. No rigid promotion.

    r_pathTracingSceneSource 3
        StaticWorldBLAS from PathTraceSceneUniverse plus DynamicFrameBLAS from
        live drawSurf mirror. This is the first new target mode.

    r_pathTracingSceneSource 4
        Mode 3 plus experimental rigid mesh promotion to reusable BLAS/TLAS
        instances. Off by default until diagnostics prove stable identity.

Do not create a mode that means "static scene universe plus baked rigid
entities." That mode encourages the wrong representation.

Data Structures
---------------

Names may change, but keep the separation.

`PtMeshKey`:

```cpp
struct PtMeshKey {
    const srfTriangles_t* tri;        // stable if available
    uintptr_t vertexBufferIdentity;   // optional/future
    uintptr_t indexBufferIdentity;    // optional/future
    int numVerts;
    int numIndexes;
    uint32_t vertexFormat;
    uint32_t materialId;              // metadata; acceptable in early key
    uint32_t sourceKind;              // staticWorld, drawSurf, model, transient, etc.
};
```

Guardrail: no transform fields.

`PtMeshRecord`:

```cpp
struct PtMeshRecord {
    PtMeshKey key;
    uint64_t stableHash;
    const idMaterial* baseMaterial;
    idStr materialName;
    idStr modelName;
    int firstSeenFrame;
    int lastSeenFrame;
    int seenCount;
    int blasHandle;                   // future
    bool localSpaceValid;
    bool contentHashKnown;
    uint64_t contentHash;
};
```

`PtInstanceRecord`:

```cpp
struct PtInstanceRecord {
    uint64_t instanceId;
    PtMeshKey meshKey;
    const idRenderEntityLocal* entity;
    int entityIndex;
    int surfaceIndex;
    float objectToWorld[16];
    float previousObjectToWorld[16];
    uint32_t materialOverrideId;
    uint32_t sourceFlags;
    uint32_t trustFlags;
    int currentArea;
    int previousFrameSeen;
};
```

Transform belongs here.

`PtDynamicFrameBucket`:

```cpp
struct PtDynamicFrameBucket {
    std::vector<PathTraceSmokeVertex> vertices;
    std::vector<uint32_t> indexes;
    std::vector<uint32_t> triangleClasses;
    std::vector<uint32_t> triangleMaterials;
    int sourceSurfaceCount;
    int skippedSurfaceCount;
    int zeroAreaSkipCount;
    int invalidIndexSkipCount;
    int limitSkipCount;
};
```

This bucket may store world-space vertices because it is rebuilt every frame.

Implementation Stages
---------------------

Stage A: stabilize the static-world path.

Objective: make `PathTraceSceneUniverse` safe and boring.

Requirements:

- Only true static world/map geometry is included.
- No rigid entity baking.
- No skinned/deformed/callback/weapon/transient objects.
- Build once per map/world generation.
- Keep diagnostics for area membership, static triangles, material counts, and
  current drawSurf comparison.

Acceptance criteria:

    r_pathTracingSceneSource 0 still works exactly as before.
    r_pathTracingSceneSource 1 logs static-world inventory only.
    r_pathTracingSceneSource 2 can preload static world without moving/rigid objects.
    No rigid entity appears in the static-world preload counters.

Stage B: add drawSurf mirror diagnostics.

Objective: record final visible drawSurfs as mesh and instance observations
without feeding BLAS yet.

For each final drawSurf, record:

- `srfTriangles_t*`,
- vertex/index counts,
- material pointer/name/id after overrides when available,
- entity pointer/index when available,
- surface index when available,
- object-to-world transform,
- source classification guess,
- skip reason if not usable.

Diagnostics:

    drawSurfMirror.surfaces
    drawSurfMirror.uniqueMeshKeys
    drawSurfMirror.instances
    drawSurfMirror.staticWorldMatches
    drawSurfMirror.nonStaticSurfaces
    drawSurfMirror.skinnedOrDeformed
    drawSurfMirror.callbackOrGenerated
    drawSurfMirror.particlesOrTransient
    drawSurfMirror.invalidOrSkipped

Acceptance criteria:

    Counts are stable across frames in a still view.
    Transform changes are visible for moving entities.
    Mesh keys do not change only because the transform changed.
    Static-world drawSurfs can be matched against SceneUniverse records.

Stage C: build DynamicFrameBLAS from non-static drawSurf mirror.

Objective: make the first robust composite scene.

Selection rule:

    if drawSurf matches static-world preload:
        do not append to dynamic bucket
    else if drawSurf is usable triangles:
        append transformed world-space triangles to DynamicFrameBucket
    else:
        skip with reason

Build policy:

    DynamicFrameBLAS:
        rebuilt every frame
        prefer fast build
        reuse/overallocate GPU buffers to avoid realloc churn
        identity transform in TLAS

Acceptance criteria:

    r_pathTracingSceneSource 3 shows static world plus live moving/visible objects.
    Moving doors/props do not corrupt static cache.
    DynamicFrameBLAS build count is one per frame, not one per surface.
    Skipping is logged by reason.
    The old source 0 fallback remains available.

Stage D: add basic source classification history.

Objective: observe stability before promotion.

For every mesh/instance observation, maintain:

    firstSeenFrame
    lastSeenFrame
    seenCount
    sameTransformCount
    changedTransformCount
    geometryContentHash if cheap/available
    materialOverrideHistory
    entityIdHistory

Classify only from observed facts:

    Stable static-ish:
        same mesh + same transform for many frames

    Rigid dynamic candidate:
        same mesh + changing transform + stable vertex/index data

    Deforming/skinned dynamic:
        vertex content changes, dynamic model, joints, callback, or generated geometry

    Transient:
        short-lived, tiny/effect-like, particle/GUI/beam/sprite/decal, or unstable identity

No behavior change yet. Diagnostics only.

Acceptance criteria:

    Classifier identifies obvious movers as rigid dynamic candidates.
    Classifier does not promote skinned characters, callbacks, particles, GUI, or weapon hacks.

Stage E: promote rigid dynamic meshes experimentally.

Objective: move safe rigid objects out of DynamicFrameBLAS.

Promotion requirements:

- Mesh key stable.
- Vertex/index content stable or source known immutable.
- Entity identity stable.
- Material override stable enough for current shader contract.
- Transform changes only at instance level.
- Not skinned/deformed/callback/transient.

Representation:

    Reusable local-space BLAS per promoted mesh.
    TLAS instance per visible promoted object.
    Transform supplied through TLAS.
    Do not bake promoted rigid vertices into world-space static geometry.

Fallback:

    If any requirement fails, keep object in DynamicFrameBLAS.

Acceptance criteria:

    Promoted rigid object moves correctly through TLAS transform.
    Promoted object disappears/reappears without leaking stale instances.
    Transform-only motion does not rebuild the mesh BLAS.
    DynamicFrameBLAS triangle count drops when promotion is active.

Stage F: static emissive light records.

Objective: only after geometry is reliable, extract persistent static emissives.

Record:

- stable light id,
- source static surface id,
- material id/name,
- triangle or polygon geometry,
- normal,
- area membership,
- luminance/color estimate,
- runtime-material-state confidence.

Do not feed all static emissives globally by default.

Acceptance criteria:

    Static emissive counts are stable per map.
    Records have area membership or logged fallback reason.
    Mode 20 can still fall back to current-frame candidates.

Stage G: area/portal light lists.

Objective: replace global emissive sampling with selected light lists.

First conservative rule:

    selected areas = current camera/player area + N portal steps
    selected static lights = lights whose source area is in selected areas

Later rule:

    selected lights = lights whose source area can affect shaded point area

Acceptance criteria:

    Light candidate list size is logged.
    Mode 20 sampled candidate origin is logged.
    Areas with no candidates fall back cleanly.

Stage H: temporal/ReSTIR identity.

Do not start this until:

- static light ids are stable,
- model/rigid light ids are stable,
- previous-to-current remap exists,
- runtime emissive state signatures exist,
- geometry confidence flags exist,
- shaded point area membership is available or fallback is logged.

What To Avoid
-------------

Avoid these implementation patterns:

    fake drawSurf construction inside PT capture
    surface key includes modelMatrix
    rigid entity appended to static buffer
    surface-only emissive promotion for multi-surface props
    ReSTIR temporal reuse before light id remap
    whole-map global emissive list as default sampling source
    per-surface BLAS rebuilds for every drawSurf
    classifying dynamic/static only from idRenderModel type

Do not delete the current fallback path. The fallback is the safety rail.

Diagnostics Required Before Behavior Changes
--------------------------------------------

Every new source mode should print one compact line per frame or on dump
request:

    PTSceneProducer:
        sourceMode
        staticWorldSurfaces/staticWorldTriangles
        staticBLASCacheHit
        drawSurfObserved/drawSurfUsable/drawSurfSkipped
        dynamicFrameSurfaces/dynamicFrameTriangles
        uniqueMeshKeys
        instanceRecords
        promotedRigidInstances
        transientFallbackSurfaces
        tlasInstances
        skippedInvalid/skippedZeroArea/skippedLimit/skippedUnsupported

On verbose dump, include top omitted reasons:

    unsupportedSkinned
    unsupportedCallback
    unsupportedGUI
    unsupportedParticle
    materialNotDrawn
    invalidIndexes
    missingVerts
    zeroAreaOnly
    matchedStaticWorld
    alreadyPromotedRigid

Near-Term Codex Task
--------------------

Implement diagnostics-only `PathTraceDrawSurfCapture` and
`PathTraceInstanceUniverse`.

Do not change BLAS/TLAS output yet.
Do not remove the existing drawSurf fallback.
Do not add rigid promotion.
Do not add emissive light records.
Do not add ReSTIR/temporal remapping.

For each final raster drawSurf, record a `MeshObservation` and
`InstanceObservation`. The `MeshObservation` key must not include
transform/modelMatrix. The `InstanceObservation` must include current transform
and entity identity when available. Add `r_pathTracingInstanceUniverseDump 1` to
print counts and sample records. Compare observed static-world drawSurfs against
`PathTraceSceneUniverse` static records. Log mismatches and skip reasons.

Completion criteria:

    A still frame produces stable mesh/instance counts.
    Moving rigid objects change instance transform without changing mesh key.
    Static-world surfaces can be identified separately from non-static drawSurfs.
    No BLAS/TLAS behavior changes in this slice.

Second Codex Task
-----------------

After diagnostics look sane, implement `r_pathTracingSceneSource 3`.

Use `StaticWorldBLAS` from `PathTraceSceneUniverse` for true static map/world
surfaces. Build one `DynamicFrameBLAS` per frame from usable drawSurf mirror
records that are not matched to the static-world preload. Keep output compatible
with the existing static/dynamic shader contract. Build TLAS with static
identity instance plus dynamic identity instance. Do not promote rigid meshes
yet.

Completion criteria:

    SceneSource 3 renders static world plus visible live objects.
    Moving objects do not enter or invalidate the static cache.
    DynamicFrameBLAS is rebuilt once per frame from a merged dynamic bucket.
    Fallback source 0 remains intact.

Third Codex Task
----------------

Only after source 3 works, implement diagnostics-only rigid promotion
candidates.

Track mesh/instance history. Identify same-mesh/changing-transform objects. Log
promotion candidates but keep them in `DynamicFrameBLAS`. Exclude skinned,
callback, GUI, particle, weapon-depth-hack, model-depth-hack, and
generated/deforming surfaces.

Completion criteria:

    Obvious doors/movers/rigid props appear as candidates.
    Characters/effects/GUI/particles do not.
    No render behavior change yet.

Minimal Success Definition
--------------------------

The first meaningful success is not perfect static/dynamic classification. The
first meaningful success is:

    Static map geometry is present off-camera through a static-world preload.
    Visible live objects are present through a dynamic-frame drawSurf bucket.
    Moving objects do not poison static geometry.
    The old fallback is still available.
    The diagnostic log clearly explains what was included, skipped, or matched.

Once that works, the project can evolve toward reusable rigid BLAS, persistent
light records, area/portal light lists, and temporal remapping. Do not skip to
those later systems before this foundation works.
