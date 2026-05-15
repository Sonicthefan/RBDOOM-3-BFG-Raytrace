RBDoom3 BFG RT/PT Payload And Live-State Reduction Guide
========================================================

Purpose
-------

This folder is for focused work on reducing ray tracing shader payload size,
RayGen live state, and device-removal risk in the current path tracing branch.

Do not use these tasks as a general shader split, ReSTIR upgrade, material
rewrite, or quality-improvement assignment. The immediate goal is smaller,
safer kernels with behavior preserved.

Current Problem Statement
-------------------------

Nsight analysis of the current mode 18 path shows very high RayGen live state
at hot callsites:

- hottest callsite: about 447 bytes live state
- other hot callsites: about 398, 371, and 317 bytes
- observed full-screen vkCmdTraceRaysKHR at 1280x720: about 26.43 ms
- tiled dispatch reduced single dispatch size but did not stop texture-like
  artifacts followed by device removal
- disabling texture sampling did not stop device removal

This points away from "raw ray count" and toward shader pressure, divergence,
large payloads, monolithic live ranges, and possibly resource pressure under
long GPU work.

Current Payload
---------------

The main shader payload currently lives in:

    neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl

Current structure:

    struct PathTraceSmokePayload
    {
        uint value;
        float hitT;
        float3 normal;
        float3 geometricNormal;
        float3 tangent;
        float3 bitangent;
        float2 texCoord;
        float4 vertexColor;
        float4 vertexColorAdd;
        uint surfaceClass;
        uint translucentSubtype;
        uint triangleClassAndFlags;
        uint materialId;
        uint materialIndex;
        uint instanceId;
        uint primitiveIndex;
        uint shadowIgnoreInstanceId;
        uint shadowIgnorePrimitiveIndex;
        uint shadowIgnoreMaterialId;
    };

This is roughly 136 bytes before compiler live-range effects. Nsight live-state
numbers above 300 bytes are plausible because the payload is combined with
large raygen locals, many helper function calls, and multiple TraceRay paths in
one monolithic shader.

Why Payload Size Matters
------------------------

The payload is carried through TraceRay and visible to raygen, closest-hit,
any-hit, and miss shaders. A large payload can:

- increase register pressure
- reduce occupancy
- keep too many values live across TraceRay calls
- worsen shader compile time
- make unrelated debug fields cost real performance in the mode 18 integrator
- amplify device-removal risk when GPU work becomes long enough to stress the
  driver/runtime

The issue is not just the byte size of the payload. The current monolithic
shader also passes the payload by value to many helpers and computes shading
data in closest-hit that some rays do not need.

Non-Goals
---------

Do not do these in the first payload task:

- do not split pathtrace_smoke.rt.hlsl into multiple shader libraries
- do not move mode 18 to a new C++ pass yet
- do not add SER
- do not add new quality features
- do not change material classification
- do not change BVH/source3/portal membership
- do not redesign ReSTIR
- do not switch fields to float16_t as the first optimization
- do not remove debug modes
- do not change visual output intentionally

The first pass should be a small, testable reduction of payload pressure.

Recommended Order
-----------------

1. Add a minimal shadow payload.

   Shadow rays currently use the full PathTraceSmokePayload through
   TraceSmokeShadowVisibility. This is the best first target because shadow rays
   only need hit/ignore state and should not carry normals, tangents, colors,
   UVs, material table fields, or surface-class data.

2. Reduce payload pass-by-value.

   Many helpers take PathTraceSmokePayload by value. Convert safe hot helpers to
   use `in PathTraceSmokePayload payload` where it preserves behavior and is
   accepted by the HLSL compiler. Do this after the shadow payload change, not
   at the same time if the patch becomes hard to review.

3. Create a compact mode 18 primary/integrator payload.

   This is a second task. It should carry only hit identity and enough data to
   reconstruct shading after TraceRay. Legacy debug modes can keep the old large
   payload until the mode-family shader split.

4. Split mode 18 shader family.

   Only after the integrator no longer depends on the full debug payload should
   a new pathtrace_toy_integrator.rt.hlsl or equivalent be created.

First Target: Shadow Payload
----------------------------

Target shape:

    struct PathTraceSmokeShadowPayload
    {
        uint hit;
        uint rayMode;
        uint ignoreInstanceId;
        uint ignorePrimitiveIndex;
        uint ignoreMaterialId;
    };

The final exact fields can differ, but the shadow payload should stay small.
It should not contain normals, tangents, UVs, vertex colors, material index,
surface class, or primary-hit material metadata unless a concrete correctness
reason is documented.

Ray mode is needed because current any-hit behavior distinguishes ordinary
primary/secondary alpha handling from shadow-ray handling. If the implementation
can preserve that behavior with fewer fields, document it.

Expected Shadow-Ray Semantics
-----------------------------

TraceSmokeShadowVisibility should continue to:

- return 1.0 when no occluder is found
- return 0.0 when an occluder is found
- optionally ignore a specified emissive source by instance, primitive, or
  material ID
- keep current alpha/translucent any-hit behavior for shadow rays
- preserve current debug behavior unless a bug is found and documented

The shadow miss shader should set "no hit" state on the small shadow payload.
The shadow any-hit shader should only read fields from the small shadow payload.
The main closest-hit shader should not run for shadow rays if the trace flags
continue to use RAY_FLAG_SKIP_CLOSEST_HIT_SHADER.

Validation Expectations
-----------------------

Minimum validation:

- rebuild PathTracingShaders
- rebuild RBDoom3BFG
- run mode 18 with the same CVar settings used before the change
- compare direct-light/shadow behavior against the previous build
- test with selected point lights and Doom analytic lights
- test with texture sampling off and on
- confirm no intentional visual behavior change

Nsight validation if available:

- capture the same mode 18 scene/settings before and after
- record RayGen live-state bytes at the same hot callsites
- record vkCmdTraceRaysKHR duration at the same resolution/settings
- record whether device removal/artifacts change

Do not claim success solely from FPS. The useful metric for this task is lower
live state and unchanged behavior.

Build Notes
-----------

The existing docs cover CMake presets and build lanes. The legacy Debug lane is
compile/link sanity only:

    cmake -S neo -B build
    cmake --build build --config Debug --target PathTracingShaders
    cmake --build build --config Debug --target RBDoom3BFG

For prepared-folder PT validation, use a preset Release/PT lane:

    cd neo
    cmake --build --preset win64-pt-dev-release

    Copy-Item -LiteralPath E:\prog\rbdoom-3-bfg-rt\build-win64-pt-dev\Release\RBDoom3BFG.exe -Destination E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe -Force

Do not copy `build\Debug\RBDoom3BFG.exe` for normal PT testing.

If the shader is touched, copy the updated generated renderprogs2 output into
the prepared game folder used for testing. The main files are:

    base/renderprogs2/dxil/builtin/pathtracing/pathtrace_smoke.rt.bin
    base/renderprogs2/spirv/builtin/pathtracing/pathtrace_smoke.rt.bin

Keep copies and testing paths consistent with the current project docs and the
user's prepared game directory.
