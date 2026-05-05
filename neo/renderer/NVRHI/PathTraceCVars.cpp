#include "precompiled.h"
#pragma hdrstop

#include "PathTraceCVars.h"

idCVar r_pathTracingDebugMode(
    "r_pathTracingDebugMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug output mode: 0 = hit/miss, 1 = depth, 2 = interpolated normal, 3 = surface class, 4 = UV, 5 = geometric normal, 6 = material ID, 7 = material table, 8 = sampled diffuse texture, 9 = alpha test preview, 10 = albedo, 11 = translucent overlay inspection, 12 = translucent subtype, 13 = fixed Lambert lighting, 14 = selected point-light shadows, 15 = selected light influence, 16 = normal map, 17 = specular map, 18 = toy one-bounce path trace, 19 = emissive triangle inventory, 20 = single-frame reservoir direct lighting, 21 = solid drawSurf bounds boxes, 22 = wireframe drawSurf bounds boxes, 23 = experimental routed rigid TLAS instances, 24 = fallback-vs-rigid-route overlap validation, 25 = routed rigid lighting validation" );

idCVar r_pathTracingClassDump(
    "r_pathTracingClassDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump sampled RT smoke surface classification reasons once" );

idCVar r_pathTracingClassSummary(
    "r_pathTracingClassSummary",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to include RT smoke class counts in the throttled scene-capture summary" );

idCVar r_pathTracingDebugWidth(
    "r_pathTracingDebugWidth",
    "320",
    CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE,
    "RT smoke debug output width" );

idCVar r_pathTracingDebugHeight(
    "r_pathTracingDebugHeight",
    "180",
    CVAR_RENDERER | CVAR_INTEGER | CVAR_ARCHIVE,
    "RT smoke debug output height" );

idCVar r_pathTracingTextureProbeIndex(
    "r_pathTracingTextureProbeIndex",
    "-1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8 material table index to focus in texture probe logging; -1 = first safe texture" );

idCVar r_pathTracingTextureProbeReset(
    "r_pathTracingTextureProbeReset",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to release the latched RT smoke texture probe and select a new one" );

idCVar r_pathTracingTextureProbeDump(
    "r_pathTracingTextureProbeDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current RT smoke texture probe and sampled candidate list once" );

idCVar r_pathTracingAlphaDump(
    "r_pathTracingAlphaDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current RT smoke alpha-test material table once" );

idCVar r_pathTracingTextureFallbackDump(
    "r_pathTracingTextureFallbackDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current RT smoke albedo fallback materials once" );

idCVar r_pathTracingTranslucentDump(
    "r_pathTracingTranslucentDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current RT smoke translucent subtype classifier samples once" );

idCVar r_pathTracingCrosshairMaterialDump(
    "r_pathTracingCrosshairMaterialDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump detailed RT smoke material/stage info for the surface under the center crosshair once" );

idCVar r_pathTracingGuiDump(
    "r_pathTracingGuiDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump captured RT smoke in-world GUI draw surfaces once" );

idCVar r_pathTracingEmissiveInventoryDump(
    "r_pathTracingEmissiveInventoryDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current RT smoke emissive triangle inventory once" );

idCVar r_pathTracingEmissiveInventoryMaxTriangles(
    "r_pathTracingEmissiveInventoryMaxTriangles",
    "4096",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum emissive triangles captured into the RT smoke inventory buffer" );

idCVar r_pathTracingSceneSource(
    "r_pathTracingSceneSource",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "PT scene producer source: 0 = existing drawSurf producer only, 1 = scene-universe diagnostics only, 2 = full static scene-universe geometry plus dynamic drawSurf fallback, 3 = existing static cache plus mirrored dynamic-frame drawSurfs" );

idCVar r_pathTracingSceneSourceCompare(
    "r_pathTracingSceneSourceCompare",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump a one-shot source 0 vs source 3 capture comparison on the next source 3 frame" );

idCVar r_pathTracingSceneSource2RigidEntities(
    "r_pathTracingSceneSource2RigidEntities",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental source 2 rigid entity promotion: 0 = off, 1 = whole eligible rigid entities that contain emissive-capable surfaces, 2 = all eligible non-skinned non-callback static entity model surfaces" );

idCVar r_pathTracingInstanceUniverseDump(
    "r_pathTracingInstanceUniverseDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump diagnostics-only PT drawSurf mesh/instance mirror observations once" );

idCVar r_pathTracingRigidMeshUniverseDump(
    "r_pathTracingRigidMeshUniverseDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump diagnostics-only PT rigid drawSurf mesh reuse eligibility once" );

idCVar r_pathTracingRigidMeshValidate(
    "r_pathTracingRigidMeshValidate",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump a one-shot validation comparing rigid local-source records against the baked dynamic rigid payload" );

idCVar r_pathTracingRigidBlasPlanDump(
    "r_pathTracingRigidBlasPlanDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump a diagnostics-only reusable rigid BLAS/TLAS plan from local-source mesh records" );

idCVar r_pathTracingRigidBlasInputDump(
    "r_pathTracingRigidBlasInputDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump diagnostics-only CPU rigid BLAS build-input descriptors and validation" );

idCVar r_pathTracingRigidBlasGpuScaffold(
    "r_pathTracingRigidBlasGpuScaffold",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental disabled-by-default rigid BLAS GPU scaffold gate; 0 creates no rigid BLAS GPU resources" );

idCVar r_pathTracingRigidBlasGpuBuild(
    "r_pathTracingRigidBlasGpuBuild",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental rigid BLAS scaffold build-submit gate; requires r_pathTracingRigidBlasGpuScaffold 1 and does not route BLAS into TLAS" );

idCVar r_pathTracingRigidBlasGpuDump(
    "r_pathTracingRigidBlasGpuDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump one-shot rigid BLAS GPU scaffold buffer/build stats" );

idCVar r_pathTracingRigidTlasPlanDump(
    "r_pathTracingRigidTlasPlanDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump a diagnostics-only rigid TLAS instance plan from source3 visible rigid instances" );

idCVar r_pathTracingRigidTlasRoute(
    "r_pathTracingRigidTlasRoute",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental source3 rigid TLAS route gate; active in debug modes 23/24/25 and optionally mode 18/20 with their per-mode gates" );

idCVar r_pathTracingRigidRouteMode18(
    "r_pathTracingRigidRouteMode18",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental mode 18 routed rigid integration gate; requires source3 rigid route gates and remains off by default" );

idCVar r_pathTracingRigidRouteMode20(
    "r_pathTracingRigidRouteMode20",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental mode 20 routed rigid integration gate; requires source3 rigid route gates and remains off by default" );

idCVar r_pathTracingRigidRouteOverlapDump(
    "r_pathTracingRigidRouteOverlapDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 in debug mode 24 to read back the overlap validation image and dump per-color pixel proportions once" );

idCVar r_pathTracingRigidRouteRemoveDynamic(
    "r_pathTracingRigidRouteRemoveDynamic",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental mode 24/25 validation gate: remove routed-ready rigid candidates from the dynamic fallback after BLAS route overlap validates" );

idCVar r_pathTracingSceneBoundsOverlay(
    "r_pathTracingSceneBoundsOverlay",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "PT drawSurf mirror bounds overlay: 0 = off, 1 = eligible rigid candidates only, 2 = all mirrored categories" );

idCVar r_pathTracingSceneBoundsOverlayMax(
    "r_pathTracingSceneBoundsOverlayMax",
    "128",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum PT drawSurf mirror bounds boxes drawn per frame" );

idCVar r_pathTracingSceneBoundsDepthTest(
    "r_pathTracingSceneBoundsDepthTest",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Legacy no-op for the PT drawSurf bounds overlay; overlay is now blended inside the PT output" );

idCVar r_pathTracingSceneBoundsOverlayGpu(
    "r_pathTracingSceneBoundsOverlayGpu",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental PT shader composited bounds overlay gate; default off after device-removal risk in per-pixel overlay loop" );

idCVar r_pathTracingSceneUniverseDump(
    "r_pathTracingSceneUniverseDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump diagnostics-only PT static world scene-universe inventory once" );

idCVar r_pathTracingScenePortalSteps(
    "r_pathTracingScenePortalSteps",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Portal traversal depth for diagnostics-only PT scene-universe selected-area counts" );

idCVar r_pathTracingSceneUniverseVerbose(
    "r_pathTracingSceneUniverseVerbose",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Print verbose PT scene-universe area and surface samples in the one-shot dump" );

idCVar r_pathTracingWorldStaticEmissives(
    "r_pathTracingWorldStaticEmissives",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental: supplement mode 19/20 emissive candidates from full-level static world models; off by default due current GPU/device-removal risk" );

idCVar r_pathTracingWorldStaticEmissiveMaxTriangles(
    "r_pathTracingWorldStaticEmissiveMaxTriangles",
    "128",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum full-level static world emissive triangles to append when r_pathTracingWorldStaticEmissives is enabled" );

idCVar r_pathTracingDynamicOccluderRadius(
    "r_pathTracingDynamicOccluderRadius",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Radius around the camera that keeps rigid entity model surfaces in the RT dynamic BLAS even when raster visibility culls them; 0 disables" );

idCVar r_pathTracingDynamicOccluderMaxSurfaces(
    "r_pathTracingDynamicOccluderMaxSurfaces",
    "64",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum nearby rigid entity surfaces appended as dynamic RT occluder safety geometry per frame" );

idCVar r_pathTracingLightUniverseDump(
    "r_pathTracingLightUniverseDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump persistent RT smoke emissive light-universe stats once" );

idCVar r_pathTracingLightUniversePersistDynamic(
    "r_pathTracingLightUniversePersistDynamic",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Persist stable rigid-entity emissives in the RT smoke light universe; particles/skinned/non-rigid dynamics remain frame-local" );

idCVar r_pathTracingLightUniverseInjectMissingDynamic(
    "r_pathTracingLightUniverseInjectMissingDynamic",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Inject missing semi-static dynamic emissives into the RT smoke light list; disabled by default because matching dynamic occluder geometry is not persistent yet" );

idCVar r_pathTracingLightUniverseDynamicMinSeenFrames(
    "r_pathTracingLightUniverseDynamicMinSeenFrames",
    "2",
    CVAR_RENDERER | CVAR_INTEGER,
    "Frames a rigid-entity emissive must be seen before it is promoted to semi-static light-universe persistence" );

idCVar r_pathTracingLightUniverseDynamicMaxMissingFrames(
    "r_pathTracingLightUniverseDynamicMaxMissingFrames",
    "90",
    CVAR_RENDERER | CVAR_INTEGER,
    "Frames a promoted semi-static dynamic emissive may be missing before it is aged out of the light universe" );

idCVar r_pathTracingLightAreaPortalSteps(
    "r_pathTracingLightAreaPortalSteps",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Portal traversal depth for RT smoke emissive light-area selection diagnostics; default is current area plus directly connected portals" );

idCVar r_pathTracingLightAreaFilter(
    "r_pathTracingLightAreaFilter",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic-only RT smoke emissive light-area selector gate; 0 leaves mode 20 uploads unchanged" );

idCVar r_pathTracingLightAreaFilterApply(
    "r_pathTracingLightAreaFilterApply",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental render-affecting RT smoke emissive light-area selector gate; requires diagnostics to validate per-area behavior first" );

idCVar r_pathTracingLightAreaOverflowMax(
    "r_pathTracingLightAreaOverflowMax",
    "512",
    CVAR_RENDERER | CVAR_INTEGER,
    "Connected-area overflow budget for RT smoke emissive light-area selection; high default keeps connected candidates greedy and mainly drops disconnected/unknown candidates" );

idCVar r_pathTracingLightDump(
    "r_pathTracingLightDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current RT smoke selected debug light once" );

idCVar r_pathTracingLightCount(
    "r_pathTracingLightCount",
    "4",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum selected visible point lights for RT smoke debug modes 14/15; 0 = fixed directional fallback, max 32" );

idCVar r_pathTracingLightSelection(
    "r_pathTracingLightSelection",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke point-light selection: 0 = nearest camera lights, 1 = strongest estimated camera influence" );

idCVar r_pathTracingLightSpriteProxies(
    "r_pathTracingLightSpriteProxies",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Draw debug-only emissive sprite proxies for selected RT smoke lights in mode 14" );

idCVar r_pathTracingLightSpriteRadiusScale(
    "r_pathTracingLightSpriteRadiusScale",
    "0.04",
    CVAR_RENDERER | CVAR_FLOAT,
    "World-space radius scale for RT smoke selected-light sprite proxies" );

idCVar r_pathTracingLightSpriteIntensity(
    "r_pathTracingLightSpriteIntensity",
    "2.5",
    CVAR_RENDERER | CVAR_FLOAT,
    "Intensity multiplier for RT smoke selected-light sprite proxies" );

idCVar r_pathTracingTextureProbeDumpStart(
    "r_pathTracingTextureProbeDumpStart",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "First safe RT smoke texture candidate to print in the one-shot probe dump" );

idCVar r_pathTracingTextureProbeDumpCount(
    "r_pathTracingTextureProbeDumpCount",
    "12",
    CVAR_RENDERER | CVAR_INTEGER,
    "Number of safe RT smoke texture candidates to print in the one-shot probe dump" );

idCVar r_pathTracingTextureTableLimit(
    "r_pathTracingTextureTableLimit",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum safe captured material textures to bind for RT smoke texture debug modes; 0 = discovery/logging only, max 2048" );

idCVar r_pathTracingTextureTableStart(
    "r_pathTracingTextureTableStart",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "First safe captured diffuse texture candidate to bind for RT smoke debug mode 8" );

idCVar r_pathTracingTextureForceFallback(
    "r_pathTracingTextureForceFallback",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8: bind fallback white texture into active bindless slots instead of captured diffuse textures" );

idCVar r_pathTracingTextureSampleEnable(
    "r_pathTracingTextureSampleEnable",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8: enable actual bindless texture sampling; 0 keeps the descriptor table active but shades from material fallback colors" );

idCVar r_pathTracingTextureBindlessEnable(
    "r_pathTracingTextureBindlessEnable",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8: sample from the bindless texture table; 0 samples a regular fallback texture SRV for diagnostics" );

idCVar r_pathTracingTextureSampleMethod(
    "r_pathTracingTextureSampleMethod",
    "2",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug mode 8 texture fetch method: 0 = disabled/fallback color, 1 = SampleLevel diagnostic, 2 = Texture.Load stable default" );

idCVar r_pathTracingTextureFilter(
    "r_pathTracingTextureFilter",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke Texture.Load filtering: 0 = point, 1 = manual bilinear" );

idCVar r_pathTracingTextureDecode(
    "r_pathTracingTextureDecode",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Decode RT smoke material texture encodings such as Doom diffuse YCoCg" );

idCVar r_pathTracingForceTextureCodeUse(
    "r_pathTracingForceTextureCodeUse",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use four-digit Doom texture filename codes as RT smoke material image discovery hints" );

idCVar r_pathTracingUseNormalMaps(
    "r_pathTracingUseNormalMaps",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use sampled normal maps for RT smoke debug mode 14 direct lighting" );

idCVar r_pathTracingUseSpecularMaps(
    "r_pathTracingUseSpecularMaps",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use sampled specular maps for RT smoke debug mode 14 direct lighting" );

idCVar r_pathTracingUseEmissiveMaps(
    "r_pathTracingUseEmissiveMaps",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use sampled emissive/glow material stages for RT smoke debug mode 14 direct lighting" );

idCVar r_pathTracingEmissiveFallbackWithoutTexture(
    "r_pathTracingEmissiveFallbackWithoutTexture",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Keep material-universe emissive color active when an emissive texture cannot be assigned a bindless descriptor slot" );

idCVar r_pathTracingToyMaxRayDistance(
    "r_pathTracingToyMaxRayDistance",
    "1024",
    CVAR_RENDERER | CVAR_FLOAT,
    "Maximum mode 18 toy path-tracing secondary/direct ray distance; reduces leaks through incomplete visible-surface TLAS geometry" );

idCVar r_pathTracingToyLightScale(
    "r_pathTracingToyLightScale",
    "0.3",
    CVAR_RENDERER | CVAR_FLOAT,
    "Scale selected point-light contribution in mode 18 toy path tracing" );

idCVar r_pathTracingToyEmissiveScale(
    "r_pathTracingToyEmissiveScale",
    "4.0",
    CVAR_RENDERER | CVAR_FLOAT,
    "Scale emissive material contribution in mode 18 toy path tracing" );

idCVar r_pathTracingToyLightTraceCap(
    "r_pathTracingToyLightTraceCap",
    "8",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum selected point lights traced per mode 18 direct-light evaluation; clamps expensive shadow rays after light selection, max 32" );

idCVar r_pathTracingToyAccumulation(
    "r_pathTracingToyAccumulation",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Accumulate mode 18 toy path tracing across stable camera frames" );

idCVar r_pathTracingToyAccumMaxFrames(
    "r_pathTracingToyAccumMaxFrames",
    "64",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum accumulated frames for mode 18 toy path tracing" );

idCVar r_pathTracingReservoirTwoSidedEmissives(
    "r_pathTracingReservoirTwoSidedEmissives",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Treat mode 20 reservoir emissive triangle samples as two-sided for diagnostics" );

idCVar r_pathTracingSmokeParticleDither(
    "r_pathTracingSmokeParticleDither",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use stable alpha dithering for RT smoke particle cards and ignore them for debug shadow rays" );

idCVar r_pathTracingSmokeParticleAlphaScale(
    "r_pathTracingSmokeParticleAlphaScale",
    "0.25",
    CVAR_RENDERER | CVAR_FLOAT,
    "Opacity scale for RT smoke particle-card alpha dithering" );

idCVar r_pathTracingSmokeParticleEdgeFade(
    "r_pathTracingSmokeParticleEdgeFade",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Fade RT smoke particle-card dither opacity near card UV edges" );

idCVar r_pathTracingPortalWindowStochastic(
    "r_pathTracingPortalWindowStochastic",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use stable stochastic transparency for RT smoke portal/window surfaces" );

idCVar r_pathTracingPortalWindowAlphaScale(
    "r_pathTracingPortalWindowAlphaScale",
    "0.35",
    CVAR_RENDERER | CVAR_FLOAT,
    "Opacity scale for RT smoke portal/window stochastic transparency" );

idCVar r_pathTracingPortalWindowMinOpacity(
    "r_pathTracingPortalWindowMinOpacity",
    "0.05",
    CVAR_RENDERER | CVAR_FLOAT,
    "Minimum primary-ray opacity for RT smoke portal/window stochastic transparency" );

idCVar r_pathTracingPortalWindowShadowOpacity(
    "r_pathTracingPortalWindowShadowOpacity",
    "0.05",
    CVAR_RENDERER | CVAR_FLOAT,
    "Shadow-ray opacity multiplier for RT smoke portal/window stochastic transparency" );

idCVar r_pathTracingAdditiveDecalKey(
    "r_pathTracingAdditiveDecalKey",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Treat additive translucent decal/signage materials as RGB-keyed RT overlays" );

idCVar r_pathTracingAllowGuiTextures(
    "r_pathTracingAllowGuiTextures",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Allow strictly validated GUI/SWF-like material textures into RT smoke bindless texture diagnostics" );

idCVar r_pathTracingAllowGuiSurfaces(
    "r_pathTracingAllowGuiSurfaces",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Allow GUI/SWF draw-surface geometry cards into RT smoke capture diagnostics" );

idCVar r_pathTracingSkipCallbackEntities(
    "r_pathTracingSkipCallbackEntities",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Skip custom-shader deferred callback render entities in RT smoke capture to avoid item/pickup lifetime hazards" );

idCVar r_pathTracingAnchorRaycast(
    "r_pathTracingAnchorRaycast",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use center-ray scene anchor and dynamic-surface ordering for RT smoke capture; slower but useful when surface caps hide important geometry" );

idCVar r_pathTracingMaterialMetadataCache(
    "r_pathTracingMaterialMetadataCache",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Cache RT smoke per-material texture metadata; frame-local material tables are still rebuilt" );

idCVar r_pathTracingSmokeLog(
    "r_pathTracingSmokeLog",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable periodic RT smoke debug logging" );

idCVar r_pathTracingTimingLog(
    "r_pathTracingTimingLog",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable RT smoke slow-frame CPU timing logs" );

idCVar r_pathTracingTimingThreshold(
    "r_pathTracingTimingThreshold",
    "40",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke CPU timing log threshold in milliseconds" );

idCVar r_pathTracingTimingLogInterval(
    "r_pathTracingTimingLogInterval",
    "1000",
    CVAR_RENDERER | CVAR_INTEGER,
    "Minimum milliseconds between repeated RT smoke timing log lines; 0 logs every threshold hit" );

idCVar r_pathTracingOptickGpuMarkers(
    "r_pathTracingOptickGpuMarkers",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable experimental Optick GPU markers inside the RT smoke/path tracing build and dispatch passes" );

idCVar r_pathTracingReservoirDump(
    "r_pathTracingReservoirDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current RT smoke reservoir ownership/reset state once" );

idCVar r_pathTracingSkipRaster3D(
    "r_pathTracingSkipRaster3D",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Skip the normal 3D raster view after RT smoke/path tracing has produced its debug output; GUI views still render" );

idCVar r_pathTracingReadbackEnable(
    "r_pathTracingReadbackEnable",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable RT smoke UAV readback diagnostics; can stall the GPU/CPU while profiling" );

idCVar r_pathTracingMaterialCache(
    "r_pathTracingMaterialCache",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Cache the universe-derived RT smoke active material table when the material/signature inputs are unchanged" );

idCVar r_pathTracingMaterialUniverseValidate(
    "r_pathTracingMaterialUniverseValidate",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Validate persistent RT smoke material records against a fresh direct rebuild and log mismatches" );

idCVar r_pathTracingMaterialUniverseTable(
    "r_pathTracingMaterialUniverseTable",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Build the RT smoke frame material table from stable material-universe records instead of the legacy active order; default on" );

idCVar r_pathTracingMaterialUniverseTableValidate(
    "r_pathTracingMaterialUniverseTableValidate",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Build old and universe-derived RT smoke material tables and report whether their active material contents match" );

idCVar r_pathTracingGeometryUniverseValidate(
    "r_pathTracingGeometryUniverseValidate",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Run expensive RT smoke geometry-universe static record validation; reports validate=total/range/duplicate/history/keyVector" );
