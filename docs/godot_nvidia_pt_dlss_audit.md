Godot NVIDIA PT/DLSS Reference Audit
====================================

Reference checkout:

    E:\prog\references\godot-nvidia-pt-dlss

Audit date:

    2026-05-25

Purpose
-------

This note maps the reference renderer's path-tracing pipeline in the same
producer-to-consumer terms used by the rbdoom PT/ReSTIR work. The reference is
not a ReSTIR PT or RTXDI implementation by name: there are no `ReSTIR` or
`RTXDI` code paths in the inspected renderer files. It is still useful because
it has a full engine-integrated Vulkan ray-tracing path with scene/geometry
ownership, material translation, a light-candidate buffer, a stochastic
mini-reservoir NEE selector, path payload handoff, motion/depth output, and
DLSS Ray Reconstruction side buffers.

Primary files inspected
-----------------------

- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp`
- `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.h`
- `servers/rendering/renderer_rd/forward_clustered/render_raytracing.cpp`
- `servers/rendering/renderer_rd/forward_clustered/render_raytracing.h`
- `servers/rendering/renderer_rd/forward_clustered/scene_shader_raytracing.cpp`
- `servers/rendering/renderer_rd/forward_clustered/scene_shader_raytracing.h`
- `servers/rendering/renderer_rd/shaders/raytracing/scene_raytracing_raygen.glsl`
- `servers/rendering/renderer_rd/shaders/raytracing/raytracing_inc.glsl`
- `servers/rendering/renderer_rd/shaders/raytracing/raytracing_data_inc.glsl`
- `servers/rendering/renderer_rd/shaders/raytracing/raytracing_hit_inc.glsl`
- `servers/rendering/renderer_rd/shaders/raytracing/raytracing_lights_inc.glsl`
- `servers/rendering/renderer_rd/shaders/raytracing/raytracing_closest_hit_common_inc.glsl`

High-level verdict
------------------

The reference is a custom path tracer integrated into Godot's forward clustered
renderer. It replaces the opaque/motion-vector pass when RT is active, builds a
per-viewport TLAS from an RT-specific cull list, uploads compact geometry,
material, motion, and light buffers, dispatches a raygen/closest-hit/miss/any-hit
pipeline at internal render resolution, copies the RT color/depth into the
normal render buffers, then lets the regular upscaler/DLSS path consume those
outputs.

The "ReSTIR-like" part is narrow: direct lighting uses a per-hit stochastic
mini-batch reservoir over a CPU-prepared light buffer. It does not persist
reservoirs, has no temporal or spatial reuse, has no RTXDI RAB, and has no
previous-frame light remapping. Treat it as a good example of an engine-side
light universe feeding a bounded stochastic NEE selector, not as a ReSTIR PT
contract reference.

Frame-level chain
-----------------

The active path starts in `RenderForwardClustered`. When scene RT is enabled,
the raster opaque/motion-vector route is suppressed and the renderer fills only
the data still needed for alpha overlays and later passes. It computes packed
RT flags from the active path-tracing environment settings, including sample
count, max bounces, DLSS-RR, fog, and SER. It then calls:

1. `RenderRaytracing::build_tlas(...)`
2. `RenderRaytracing::update_uniform_set(...)`
3. `SceneShaderRaytracing::get_raytracing_pipeline(...)`
4. `RD::raytracing_list_trace_rays(...)`
5. RT depth copy and RT color copy back into the normal render scene buffers
6. DLSS/SR/upscaler path, optionally with DLSS-RR side inputs

The important architectural choice is that `build_tlas` and `update_uniform_set`
are separated from trace dispatch, but both use the same packed `rt_flags`.
That prevents shader permutation, uniform layout, sample count, and max-bounce
policy from drifting within a frame.

Geometry universe
-----------------

The geometry universe is owned by `RenderRaytracing` and is viewport-aware.
`RTViewportState` owns the TLAS and the per-viewport shader-visible buffers:

- TLAS
- geometry buffer
- material buffer
- motion index buffer
- previous-transform buffer
- light buffer
- parameter UBO

The viewport scope matters. The code comments call out that each viewport has a
different visibility set, TLAS instance composition, and `gl_InstanceCustomIndex`
mapping. Sharing this state would make hit shaders index the wrong geometry or
material records. This is directly relevant to rbdoom source3/portal work: the
shader-visible geometry record index must be scoped to the actual TLAS instance
set that the trace dispatch uses.

`build_tlas` walks `p_render_data->rt_instances`, not the regular opaque render
list. It handles these geometry classes:

- normal mesh surfaces
- skinned/blend-shape surfaces through per-frame deformed vertex buffers
- MultiMesh instances, either merged into a baked BLAS or expanded into many
  TLAS instances
- procedural RT instances with custom intersection shaders

Static mesh surfaces are cached by RID index/version and surface invalidation
counter. Deformed mesh surfaces are copied into PT-owned buffers each frame so
BLAS lifetime and previous-position data are not tied to Godot's mesh-instance
buffer lifetime. For moving rigid instances, the TLAS instance gets a compact
motion index pointing at previous object-to-world rows. For deformed surfaces,
the geometry record carries a previous-position buffer address.

Shader-visible geometry is not a high-level mesh handle. It is a compact
`GeometryData` record containing BDA addresses, vertex/index/attribute layout,
compression flags, primitive count, AABB compression data, and previous-position
address. Closest-hit code uses `gl_InstanceCustomIndexEXT` to index this array,
then reconstructs triangle indices, UV, color, TBN, and previous/motion data.

Material universe
-----------------

The material universe is also translated by `RenderRaytracing`. `RT_MaterialData`
is a 96-byte shader-visible record containing:

- bindless texture indexes for albedo, normal, ORM/roughness, and emission
- base color and alpha
- emission color/strength
- metallic, roughness, AO, specular
- flags for normal/emission/point filtering
- UV scale/offset
- normal-map strength
- optional custom material UBO device address

Standard Godot materials are translated into this compact record and bindless
texture table. Custom shader materials register ray-tracing hit groups through
`SceneShaderRaytracing`, receive an SBT offset, and may get a packed custom UBO
in a pooled device-address buffer. The renderer skips geometry whose custom hit
group is not compiled and ready in the active pipeline bundle.

The closest-hit shader has two material routes:

- hit group 0 evaluates StandardMaterial-like data directly from `MaterialData`
  and bindless textures
- custom hit groups inject translated custom material globals/fragments, then
  build the same local `MaterialResult` shape before shading

For rbdoom this reinforces the current material-universe direction: translate
engine materials into stable PT records before the integrator. Do not let the
core path-tracing shader depend on ad hoc engine material storage.

Light universe
--------------

The light universe is small and frame-local. `RenderRaytracing::gather_lights`
creates up to `RT_LIGHTS_MAX` light records. Directional lights come from the
normal frustum-culled light list and are always included first. Positional
lights come from the RT-specific AABB-culled light list (`p_render_data->rt_lights`),
are scored approximately by energy/luminance over camera distance squared, sorted,
and uploaded until the fixed budget is full.

`RT_LightData` contains:

- position or direction
- type: omni, directional, spot
- emission
- radius or directional angular radius
- distance attenuation/range data
- specular multiplier
- indirect-energy multiplier
- spot cone/falloff data

There is no stable light identity, previous-frame light table, compact-to-stable
remap, or light-reservoir persistence. This is the main gap relative to rbdoom's
ReSTIR work. The reference's CPU-side candidate selection is useful; its light
chain is not sufficient for temporal ReSTIR.

ReSTIR-like direct-light chain
-----------------------------

Direct lighting is implemented in `raytracing_lights_inc.glsl` and consumed by
`shade_and_bounce(...)` in `raytracing_closest_hit_common_inc.glsl`.

The per-hit sequence is:

1. Closest hit builds `HitData` and a local `MaterialResult`.
2. It adds emissive material radiance directly.
3. It calls `lights_evaluate_direct_lighting(...)` if the uploaded light count
   is nonzero.
4. That function samples `k = min(RT_LIGHT_RESERVOIR_SIZE, light_count)` random
   lights from the uploaded light buffer.
5. It rejects positional candidates outside range.
6. It performs a small reservoir selection over valid candidates by replacing
   the selected index with probability `1 / valid_found`.
7. It estimates the valid-light count from the valid ratio in the mini-batch.
8. It evaluates one selected light with cone/sphere/directional sampling,
   spot/range checks, shadow visibility, BRDF evaluation, attenuation, and
   indirect-energy policy.
9. It divides by the estimated light-selection PDF and adds the result through
   the current path throughput.

This is reservoir sampling in the local mathematical sense, but not ReSTIR:
the selected light is not packed into a persistent reservoir, there is no target
PDF stored for later replay, no temporal candidate, no spatial neighbor reuse,
and no final reservoir shading pass. It is closer to a bounded stochastic NEE
selector than to RTXDI DI.

Path tracing chain
------------------

The raygen shader owns the path loop. For each pixel and sample:

1. Build primary camera ray from inverse projection/view.
2. Initialize `PathState`: radiance 0, throughput 1, RNG from pixel/frame/sample.
3. Trace through the TLAS.
4. Closest hit shades the surface, writes primary auxiliary outputs when needed,
   performs direct NEE, samples a BRDF lobe for the next bounce, updates
   throughput, packs the next direction/hit distance/offset normal into payload,
   and leaves the path unterminated.
5. Raygen unpacks the payload, reconstructs the next origin from previous origin,
   ray direction, hit distance, and normal offset, then continues.
6. Miss shader terminates the path and adds sky/fog radiance.
7. Samples are averaged and written to the RT color image.

The payload is deliberately compact. Radiance and throughput are fp16-packed,
bounce counters and flags share one word, and the next ray direction plus normal
offset use octahedral packing. This is a useful example for rbdoom's payload
size work, but it also means it is not carrying ReSTIR path replay state such as
seed/consumed-random index, path-event type, target PDFs, or reservoir payloads.

Visibility and alpha
--------------------

Any-hit performs alpha testing. Standard hit group 0 samples albedo alpha and
compares against 0.5. Custom hit groups run the translated custom fragment and
ignore intersections below the material alpha scissor threshold.

Shadow rays use `SkipClosestHitShader` and terminate on first opaque hit. In
the non-SER path, the miss shader writes a visible marker into the temporary
payload for shadow rays; otherwise no miss means occluded. DLSS-RR specular-hit
distance uses inline ray queries and explicitly mirrors the alpha-test check
before confirming an intersection.

Temporal, motion, and DLSS-RR outputs
-------------------------------------

The path tracer writes the outputs that the rest of Godot expects:

- RT color image
- RT depth image
- velocity image
- optional DLSS-RR diffuse albedo
- optional DLSS-RR specular albedo
- optional DLSS-RR normal/roughness
- optional DLSS-RR specular hit distance

Primary misses write sky depth/velocity defaults. Primary hits write depth and
velocity from current hit position plus previous transform/previous-position
data. DLSS-RR side buffers are only written for sample 0 on the primary ray.
The forward clustered renderer later passes those textures into the DLSS effect
when DLSS-RR buffers exist.

The reference has a weakness called out in code: DLSS accumulation reset is set
to false because the engine path does not provide a reset signal. rbdoom should
avoid repeating that. If DLSS RR or another temporal consumer is fed by PT, the
integrator must own a real reset policy tied to camera cuts, projection changes,
scene history invalidation, material/geometry lifecycle, and path-tracing mode
changes.

What maps cleanly to rbdoom
---------------------------

- Per-viewport/per-dispatch geometry buffers indexed by TLAS instance custom
  index. This matches the need to keep rbdoom's source3 BVH, route records, and
  primary-surface producers in one coherent dispatch domain.
- PT-owned deformed buffers and previous-position retention. This is aligned
  with rbdoom's skinned previous-position and motion-vector direction.
- Material translation into compact PT records plus a bindless table. This
  supports the existing material-universe direction.
- Separate light candidate upload before shader sampling. The rbdoom analogue
  is Doom light entity -> analytic light candidate buffer -> RAB sampling and
  weighting.
- A bounded per-hit stochastic NEE selector as an intermediate step before full
  ReSTIR. This is useful for stress testing light-universe completeness without
  claiming temporal ReSTIR parity.
- Explicit primary auxiliary outputs for temporal reconstruction. This supports
  the rbdoom insistence that primary-surface/motion/depth outputs are core
  integrator products, not ReSTIR debug side effects.

What should not be copied as ReSTIR PT
-------------------------------------

- No persistent DI/PT reservoir pages.
- No temporal or spatial resampling.
- No current/previous light identity or remap table.
- No RTXDI `targetPdf`, `M`, `weightSum`, or packed reservoir ABI.
- No RAB callback surface for materials, lights, visibility, BRDF, MIS, or path
  replay.
- No final reservoir replay pass.
- No environment-light reservoir integration.
- No robust accumulation reset signal for DLSS RR.

The reference can inform rbdoom's scene/material/light preparation and simple
bounded NEE fallback. It should not be treated as an RTXDI/ReSTIR contract source.

Suggested rbdoom follow-up slices
---------------------------------

1. Compare `RTViewportState` against `PathTraceFrameResources` and
   `PathTraceGeometryUniverse`: ensure every shader-visible geometry/material
   index is scoped to the exact TLAS/domain used by a dispatch.
2. Use the reference's deformed-buffer ownership model to review rbdoom skinned
   previous-position retention and rigid previous-transform routing.
3. Add a local design note for a bounded non-ReSTIR analytic-light NEE selector
   that writes no reservoirs and makes no temporal claims. This could provide a
   baseline before widening the RAB/RTXDI path.
4. Keep the ReSTIR light universe stricter than this reference: stable authored
   identity, current/previous remap, candidate-pool persistence, and explicit
   replay validation remain mandatory.
5. Treat DLSS-RR reset policy as a first-class integrator contract before using
   any reconstructed output for validation.
