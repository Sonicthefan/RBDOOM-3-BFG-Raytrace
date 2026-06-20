# GPU Profiling Setup

How to get a usable Nsight Graphics GPU Trace of the path-tracing lanes.

## 1. The Vulkan extension that had to be enabled (the unlock)

`neo/sys/DeviceManager_VK.cpp` — added `VK_EXT_DEBUG_UTILS_EXTENSION_NAME` to the
**optional instance extensions** set (next to the existing
`VK_EXT_DEBUG_REPORT_EXTENSION_NAME`):

```cpp
VulkanExtensionSet optionalExtensions =
{
    // instance
    {
        ...
        VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME,
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME   // <-- added
    },
    ...
};
```

**Why it matters:** NVRHI gates `commandList->beginMarker/endMarker` and object
naming (`debugName`) on `m_Context.extensions.EXT_debug_utils`
(`extern/nvrhi/src/vulkan/vulkan-queries.cpp`, `vulkan-device.cpp`). It is
auto-enabled when the extension string is passed in `deviceDesc.instanceExtensions`,
which `DeviceManager_VK` does from `enabledExtensions.instance`. Without the
extension, **every** `idRenderLog` marker and every resource `debugName` (84+ in
the path-trace code) was a no-op, so Nsight saw a wall of unnamed dispatches and
`VkImage(0x...)` resources. Overhead is negligible when no profiler is attached;
auto-enabled only if the driver supports it (every modern NVIDIA driver does).

## 2. Marker cvar

`r_pathTracingNsightGpuMarkers` (CVAR_RENDERER | CVAR_INTEGER, **default 0**) —
set to `1` to emit GPU debug markers around the RT dispatches. Declared in
`neo/renderer/NVRHI/PathTraceCVars.cpp`.

```
r_pathTracingNsightGpuMarkers 1
```

Markers also feed Nsight's range profiler, so per-pass GPU timing falls out of
the capture without any app-side timestamp-query code.

## 3. Marker names (what you'll see in the timeline)

**Main RTXDI lane** (`PathTraceSmokeDispatch.cpp`, pre-existing, RAII
`PathTraceGpuMarkerScope`):
- `PT56.0 PrimarySurfacePrepass`, `PT56.1 GIInitialProducer`,
  `PT56.2 DirectTemporalProducer`, `PT56.3 DirectSpatialReservoirProducer`,
  `PT56.3b DITemporalPrepass`, `PT56.4 ReflectionProducer`.

**Clean ReSTIR GI lane** (`PathTraceCleanRestirGi.cpp`, added this pass; inline
`beginMarker`/`endMarker` gated on `nsightGpuMarkers`):
- `CleanGI.0a IndirectProducerTrace` — bounce trace + surface G-buffer write.
- `CleanGI.0b IndirectProducerShade`  — secondary-vertex direct NEE.
- `CleanGI.0c InitSeed`               — INIT-page clear + specular/NEE producer seeds.
- `CleanGI.1 TemporalReuse`           — initial reservoir + temporal contract.
- `CleanGI.2 SpatialReuse`            — spatial reuse + final shading/resolve.
- `CleanGI.3 BoilingFilter`           — compute boiling filter / resolve.

## 4. Diagnostic cvars (used to localize cost *inside* a pass, no rebuild)

Toggle these and re-capture to find which work in a pass dominates before
touching code:

| cvar | default | effect |
|---|---|---|
| `r_pathTracingCleanRestirGiMaxBounces` | 1 | 1 = primary->secondary only; 2 adds the continuation bounce (dead at 1). |
| `r_pathTracingCleanRestirGiSecondaryDirectProbability` | 1 | 0 skips the secondary-hit direct NEE (shadow rays + light sampling). |
| `r_pathTracingCleanRestirGiProducerSimple` | 0 | Uses `CleanGI.0a IndirectProducerSimple`: a one-dispatch TraceRay baseline that writes the raw GI initial sample directly, bypassing the split trace/shade producer, ray-query producer, NEE-cache secondary path, and continuation bounce. |
| `r_pathTracingCleanRestirGiSpecularProducer` | 0 | The specular-GI producer (a full specular bounce trace + NEE). **Enables specular reflections in GI**; a significant cost when on. |
| `r_pathTracingCleanRestirGiNeeCacheSeed` | 0 | NEE-cache seed at the temporal initial-sample step. Off by default. |

## 5. How to read the stall column

- **"No Instructions"** = warp had no fetched instruction to issue → I-cache
  miss / front-end starvation, usually from a **broad/divergent entry point** or
  **low occupancy**. Fix: narrow the entry point (only via work redistribution)
  or reduce register pressure. *Ignore the "top instruction" columns — they show
  what ran in the few issuing cycles, not the bottleneck.*
- **"Long Scoreboard"** = waiting on global/texture loads → memory-bound. Fix:
  shrink payload/reservoir, improve access patterns. (Not the bottleneck on the
  GI lane — data is lean.)
- Also grab **registers/thread** and **achieved occupancy** per pass — that's
  the number that decides whether a "No Instructions" pass is occupancy-limited
  (reducible via live-state reduction) or genuinely I-cache-bound.

## 6. Build / run sync (important)

`cmake --build --preset win64-pt-dev-release` (from `neo/`) compiles both the
C++ exe and the GI RT shader library (dedicated ShaderMake target, DXIL+SPIRV).
Outputs:
- exe: `build-win64-pt-dev/Release/RBDoom3BFG.exe`
- shaders: `base/renderprogs2/{dxil,spirv}/builtin/pathtracing/remix_restir_gi/pathtrace_clean_restir_gi.rt.bin`

**Run the exe and shaders from the same build.** A new exe (more raygen entry
points in the pipeline) against an old shader blob → `getShader` returns null →
"failed to create GI pipeline" → purple screen. If you deploy artifacts
elsewhere, deploy both.

Optional: `r_useValidationLayers 2` enables the Vulkan debug runtime + validation
layers (see `DeviceManager_VK::CreateDeviceAndSwapChain`), which print the exact
`vkCreateRayTracingPipelinesKHR` failure reason — use this to diagnose real
pipeline-creation failures.
