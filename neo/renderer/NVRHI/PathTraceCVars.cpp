#include "precompiled.h"
#pragma hdrstop

#include "PathTraceCVars.h"

idCVar r_pathTracingDebugMode(
    "r_pathTracingDebugMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug output mode: 0 = hit/miss, 1 = depth, 2 = interpolated normal, 3 = surface class, 4 = UV, 5 = geometric normal, 6 = material ID, 7 = material table, 8 = sampled diffuse texture, 9 = alpha test preview, 10 = albedo, 11 = translucent overlay inspection, 12 = translucent subtype, 13 = fixed Lambert lighting, 14 = selected point-light shadows, 15 = selected light influence, 16 = normal map, 17 = specular map, 18 = toy one-bounce path trace, 19 = emissive triangle inventory" );

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
