#include "precompiled.h"
#pragma hdrstop

#include "PathTraceCVars.h"

idCVar r_pathTracingDebugMode(
    "r_pathTracingDebugMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "RT smoke debug output mode: 0 = hit/miss, 1 = depth, 2 = interpolated normal, 3 = surface class, 4 = UV, 5 = geometric normal, 6 = material ID, 7 = material table, 8 = sampled diffuse texture, 9 = alpha test preview, 10 = albedo, 11 = translucent overlay inspection, 12 = translucent subtype, 13 = fixed Lambert lighting, 14 = selected point-light shadows, 15 = selected light influence, 16 = normal map, 17 = specular map, 18 = toy one-bounce path trace, 19 = emissive triangle inventory, 20 = single-frame reservoir direct lighting, 21 = solid drawSurf bounds boxes, 22 = wireframe drawSurf bounds boxes, 23 = experimental routed rigid TLAS instances, 24 = fallback-vs-rigid-route overlap validation, 25 = routed rigid lighting validation, 26 = ReSTIR PT initial reservoir diagnostics, 27 = ReSTIR PT initial reservoir shading preview, 28 = ReSTIR PT initial reservoir visibility preview, 29 = ReSTIR PT primary-surface history validation, 30 = ReSTIR PT reprojection validation, 31 = ReSTIR PT temporal reservoir validation, 32 = ReSTIR PT temporal shading preview, 33 = temporal light-source attribution, 34-37 = path-tracer core visualizers, 38 = skinned object-motion vector diagnostic, 39 = routed-rigid object-motion eligibility, 40 = routed-rigid object-motion vector diagnostic, 41 = combined skinned/routed-rigid object-motion vector diagnostic, 42 = packed primary object-motion flags, 43 = packed object-motion reprojection match, 44 = previous static snapshot binding, 45 = previous static reprojection match, 46 = previous static motion-vector diagnostic, 47 = combined geometry motion-vector diagnostic, 48 = combined geometry reprojection-match diagnostic, 49 = combined geometry motion-source diagnostic, 50 = ReSTIR PT spatial reservoir shading preview, 51 = ReSTIR PT spatial source attribution, 52 = routed-rigid transform parity, 53 = ReSTIR PT indirect reservoir diagnostics, 54 = ReSTIR PT indirect reservoir shading, 55 = ReSTIR PT indirect path attribution, 56 = ReSTIR PT combined direct+GI preview" );

idCVar r_pathTracingMode20TestPreset(
    "r_pathTracingMode20TestPreset",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "One-shot mode 20 PT test preset: 1 = source3 routed rigid mode20, 2 = plus light-area diagnostics, 3 = plus light-area apply, 4 = BVH validation stack with no light-area apply" );

idCVar r_pathTracingMode18TestPreset(
    "r_pathTracingMode18TestPreset",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "One-shot mode 18 toy PT test preset: 1 = source3 routed rigid mode18, 4 = depth-4 static/rigid residency validation stack" );

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

idCVar r_pathTracingRigidRouteEmissiveDump(
    "r_pathTracingRigidRouteEmissiveDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current routed-rigid emissive append diagnostics once" );

idCVar r_pathTracingEmissiveInventoryMaxTriangles(
    "r_pathTracingEmissiveInventoryMaxTriangles",
    "4096",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum emissive triangles captured into the RT smoke inventory buffer" );

idCVar r_pathTracingSceneSource(
    "r_pathTracingSceneSource",
    "3",
    CVAR_RENDERER | CVAR_INTEGER,
    "PT scene producer source: 0 = legacy drawSurf producer only, 1 = scene-universe diagnostics only, 2 = full static scene-universe geometry plus dynamic drawSurf fallback, 3 = source3 portal-resident scene producer" );

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
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Source3 rigid BLAS GPU scaffold gate; set 0 to disable reusable rigid BLAS GPU resources" );

idCVar r_pathTracingRigidBlasGpuBuild(
    "r_pathTracingRigidBlasGpuBuild",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Source3 rigid BLAS build-submit gate; requires r_pathTracingRigidBlasGpuScaffold 1" );

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
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Source3 rigid TLAS route gate; active in debug modes 23/24/25 and modes 18/20 unless per-mode gates are disabled" );

idCVar r_pathTracingRigidRouteMode18(
    "r_pathTracingRigidRouteMode18",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 18 routed rigid integration gate; set 0 to force legacy dynamic fallback behavior" );

idCVar r_pathTracingRigidRouteMode20(
    "r_pathTracingRigidRouteMode20",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 20 routed rigid integration gate; set 0 to force legacy dynamic fallback behavior" );

idCVar r_pathTracingRigidRouteOverlapDump(
    "r_pathTracingRigidRouteOverlapDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 in debug mode 24/39 to read back overlap/eligibility validation, including mode 24 material/class mismatch buckets, and dump routed dynamic-removal stats once" );

idCVar r_pathTracingRigidRouteRemoveDynamic(
    "r_pathTracingRigidRouteRemoveDynamic",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Remove routed-ready rigid candidates from the source3 dynamic fallback; set 0 for legacy overlap/emergency testing" );

idCVar r_pathTracingRigidRouteEmissiveCards(
    "r_pathTracingRigidRouteEmissiveCards",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Promote safe entity-attached translucent signage/glow cards into source3 rigid residency so off-camera emissive panels remain routable" );

idCVar r_pathTracingRigidRouteMaxInstances(
    "r_pathTracingRigidRouteMaxInstances",
    "256",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum source3 rigid instances routed into material IDs, route buffers, and TLAS descriptors for experimental rigid route integration" );

idCVar r_pathTracingRigidResidency(
    "r_pathTracingRigidResidency",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Keep cached source3 rigid instances resident for the current Doom area plus portal-adjacent areas instead of only raster-visible drawSurfs" );

idCVar r_pathTracingRigidResidencyPortalSteps(
    "r_pathTracingRigidResidencyPortalSteps",
    "4",
    CVAR_RENDERER | CVAR_INTEGER,
    "Portal traversal depth for source3 rigid geometry residency; default depth 4 is the current source3 standard" );

idCVar r_pathTracingRigidResidencyDump(
    "r_pathTracingRigidResidencyDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump source3 rigid geometry portal residency stats once" );

idCVar r_pathTracingStaticAreaPreload(
    "r_pathTracingStaticAreaPreload",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Preload source3 static-world geometry for the current Doom area plus portal-adjacent areas instead of waiting for raster drawSurfs" );

idCVar r_pathTracingStaticAreaPreloadPortalSteps(
    "r_pathTracingStaticAreaPreloadPortalSteps",
    "4",
    CVAR_RENDERER | CVAR_INTEGER,
    "Portal traversal depth for source3 static-world area preload; default depth 4 is the current source3 standard" );

idCVar r_pathTracingStaticAreaPreloadDump(
    "r_pathTracingStaticAreaPreloadDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump source3 static-world area preload stats once" );

idCVar r_pathTracingSceneBoundsOverlay(
    "r_pathTracingSceneBoundsOverlay",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "PT bounds overlay: 0 = off, 1 = eligible rigid drawSurf candidates plus resident rigid, 2 = all mirrored categories plus resident rigid, 3 = resident rigid only, 4 = current static cache first, 5 = static cache plus resident rigid, 6 = cache-only static first" );

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

idCVar r_pathTracingPortalBruteforceFullMap(
    "r_pathTracingPortalBruteforceFullMap",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic override: 1 treats every render-world area as selected for PT scene, rigid-residency, emissive-light, and analytic-light portal selectors; very slow" );

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

idCVar r_pathTracingEmissiveBridgeDump(
    "r_pathTracingEmissiveBridgeDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump diagnostics-only scene-universe to RT smoke emissive bridge counts once" );

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
    "Legacy RT smoke light universe is purged; set to 1 to print the purge notice once" );

idCVar r_pathTracingLightUniversePersistDynamic(
    "r_pathTracingLightUniversePersistDynamic",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Purged legacy RT smoke light-universe knob; ignored by active lighting paths" );

idCVar r_pathTracingLightUniverseInjectMissingDynamic(
    "r_pathTracingLightUniverseInjectMissingDynamic",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Purged legacy RT smoke light-universe knob; ignored by active lighting paths" );

idCVar r_pathTracingLightUniverseDynamicMinSeenFrames(
    "r_pathTracingLightUniverseDynamicMinSeenFrames",
    "2",
    CVAR_RENDERER | CVAR_INTEGER,
    "Purged legacy RT smoke light-universe knob; ignored by active lighting paths" );

idCVar r_pathTracingLightUniverseDynamicMaxMissingFrames(
    "r_pathTracingLightUniverseDynamicMaxMissingFrames",
    "90",
    CVAR_RENDERER | CVAR_INTEGER,
    "Purged legacy RT smoke light-universe knob; ignored by active lighting paths" );

idCVar r_pathTracingLightUniverseChurn(
    "r_pathTracingLightUniverseChurn",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Purged legacy RT smoke light-universe knob; ignored by active lighting paths" );

idCVar r_pathTracingLightAreaPortalSteps(
    "r_pathTracingLightAreaPortalSteps",
    "4",
    CVAR_RENDERER | CVAR_INTEGER,
    "Portal traversal depth for RT smoke emissive light-area selection diagnostics; default depth 4 matches current source3 residency baseline" );

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

idCVar r_pathTracingRemixFramePrepareDump(
    "r_pathTracingRemixFramePrepareDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the CPU-only Remix-shaped frame preparation scaffold once" );

idCVar r_pathTracingRemixLightUniverseEnable(
    "r_pathTracingRemixLightUniverseEnable",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Default-off Remix-shaped light-universe CPU shell: owns dense current/previous light domains and maps without active shader consume" );

idCVar r_pathTracingRemixLightUniverseUseForCleanRtxdiDi(
    "r_pathTracingRemixLightUniverseUseForCleanRtxdiDi",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Allow clean RTXDI DI real-light routes to request and consume the Remix-shaped dense Doom analytic light domain" );

idCVar r_pathTracingRemixLightUniverseDebugView(
    "r_pathTracingRemixLightUniverseDebugView",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Reserved RLU debug view selector: 0 off, 1 route/status, 2 current identity, 3 previous identity, 4 current-to-previous map, 5 previous-to-current map, 6 classification, 7 payload signatures, 8 spawn/despawn, 9 RAB replay validity" );

idCVar r_pathTracingRemixLightUniverseDomain(
    "r_pathTracingRemixLightUniverseDomain",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "RLU dense-domain source: 0 Doom analytic only, 1 emissive only, 2 analytic plus emissive unified" );

idCVar r_pathTracingRemixLightUniverseDoomColorSource(
    "r_pathTracingRemixLightUniverseDoomColorSource",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Reserved RLU Doom analytic payload diagnostic color source: 0 material register, 1 current renderLight color, 2 authored/base color" );

idCVar r_pathTracingRemixLightUniverseDump(
    "r_pathTracingRemixLightUniverseDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 for one-shot RLU CPU shell dump, 2 for continuous dump while explicitly requested" );

idCVar r_pathTracingRemixLightUniverseStrictRemixMapping(
    "r_pathTracingRemixLightUniverseStrictRemixMapping",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "RLU mapping policy: 1 bases validity only on current/previous dense-domain identity; 0 allows diagnostic compatibility mode" );

idCVar r_pathTracingRemixLightManagerDump(
    "r_pathTracingRemixLightManagerDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the CPU-only Remix-shaped light manager contract once" );

idCVar r_pathTracingRemixLightManagerRAB(
    "r_pathTracingRemixLightManagerRAB",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Active RRX RAB light source: Remix Light Universe only; legacy ReSTIR manager fallback has been purged" );

idCVar r_pathTracingRemixRtxdiResourcesEnable(
    "r_pathTracingRemixRtxdiResourcesEnable",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable the Remix-shaped RTXDI reservoir resource owner for the normal active RRX route; 0 disables it except explicit dump/probe allocation" );

idCVar r_pathTracingRemixRtxdiResourcesDump(
    "r_pathTracingRemixRtxdiResourcesDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the Remix-shaped RTXDI reservoir resource owner once" );

idCVar r_pathTracingRestirLightManagerDump(
    "r_pathTracingRestirLightManagerDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Legacy ReSTIR light manager is purged; set to 1 to print the purge notice once" );

idCVar r_pathTracingRestirLightManagerRAB(
    "r_pathTracingRestirLightManagerRAB",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Purged legacy ReSTIR light-manager route; active RAB light domains come from Remix Light Universe" );

idCVar r_pathTracingReGIREnable(
    "r_pathTracingReGIREnable",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable the standalone clean-room ReGIR resource shell; does not route PDFNEE, temporal, spatial, RRX, best-light, or mode 56" );

idCVar r_pathTracingReGIRDebugView(
    "r_pathTracingReGIRDebugView",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Standalone ReGIR debug view selector: 0 disabled, 1 status, 2 cells, 3 validity, 4 occupancy, 5 analytic identity, 6 emissive identity, 7 sourcePdf, 8 empty reason, 9 deterministic slot, 10 cell mean" );

idCVar r_pathTracingReGIRMode(
    "r_pathTracingReGIRMode",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Standalone ReGIR layout mode: 0 disabled, 1 grid, 2 onion" );

idCVar r_pathTracingReGIRCellSize(
    "r_pathTracingReGIRCellSize",
    "256",
    CVAR_RENDERER | CVAR_FLOAT,
    "Standalone ReGIR smallest cell size in Doom world units" );

idCVar r_pathTracingReGIRGridX(
    "r_pathTracingReGIRGridX",
    "32",
    CVAR_RENDERER | CVAR_INTEGER,
    "Standalone ReGIR grid cells along world X for the initial debug shell" );

idCVar r_pathTracingReGIRGridY(
    "r_pathTracingReGIRGridY",
    "32",
    CVAR_RENDERER | CVAR_INTEGER,
    "Standalone ReGIR grid cells along world Y for the initial debug shell" );

idCVar r_pathTracingReGIRGridZ(
    "r_pathTracingReGIRGridZ",
    "16",
    CVAR_RENDERER | CVAR_INTEGER,
    "Standalone ReGIR grid cells along world Z for the initial debug shell" );

idCVar r_pathTracingReGIRLightsPerCell(
    "r_pathTracingReGIRLightsPerCell",
    "128",
    CVAR_RENDERER | CVAR_INTEGER,
    "Standalone ReGIR candidate slots stored per cell" );

idCVar r_pathTracingReGIRBuildSamples(
    "r_pathTracingReGIRBuildSamples",
    "16",
    CVAR_RENDERER | CVAR_INTEGER,
    "Standalone ReGIR light proposals streamed per cell candidate slot; clamped to 1..64" );

idCVar r_pathTracingReGIRLightDomain(
    "r_pathTracingReGIRLightDomain",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Standalone ReGIR light domain: 0 analytic only, 1 emissive only, 2 analytic plus emissive split domains" );

idCVar r_pathTracingReGIRCenterMode(
    "r_pathTracingReGIRCenterMode",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Standalone ReGIR center mode: 0 camera-centered, 1 map/static bounds centered, 2 manager-provided manual center" );

idCVar r_pathTracingReGIRManualCenterX(
    "r_pathTracingReGIRManualCenterX",
    "0",
    CVAR_RENDERER | CVAR_FLOAT,
    "Standalone ReGIR manual center X used when r_pathTracingReGIRCenterMode is 2" );

idCVar r_pathTracingReGIRManualCenterY(
    "r_pathTracingReGIRManualCenterY",
    "0",
    CVAR_RENDERER | CVAR_FLOAT,
    "Standalone ReGIR manual center Y used when r_pathTracingReGIRCenterMode is 2" );

idCVar r_pathTracingReGIRManualCenterZ(
    "r_pathTracingReGIRManualCenterZ",
    "0",
    CVAR_RENDERER | CVAR_FLOAT,
    "Standalone ReGIR manual center Z used when r_pathTracingReGIRCenterMode is 2" );

idCVar r_pathTracingReGIRDump(
    "r_pathTracingReGIRDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump standalone ReGIR mode, cells, domain counts, resource size, dispatch slots, and first missing contract once" );

idCVar r_pathTracingLightDump(
    "r_pathTracingLightDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current RT smoke selected debug light once" );

idCVar r_pathTracingDoomLightDump(
    "r_pathTracingDoomLightDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "One-shot Doom renderer light identity dump: 1 = area-aware summary and samples, 2 = verbose samples up to r_pathTracingDoomLightDumpMax" );

idCVar r_pathTracingDoomLightProbeDump(
    "r_pathTracingDoomLightProbeDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "One-shot nearest/player/crosshair Doom renderer light dump for identifying duplicate helper lights near emissives" );

idCVar r_pathTracingDoomLightDumpMax(
    "r_pathTracingDoomLightDumpMax",
    "64",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum Doom renderer light identity records printed by r_pathTracingDoomLightDump" );

idCVar r_pathTracingDoomLightProbeMax(
    "r_pathTracingDoomLightProbeMax",
    "8",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum nearest and crosshair Doom renderer light probe records printed by r_pathTracingDoomLightProbeDump" );

idCVar r_pathTracingAnalyticLightCandidates(
    "r_pathTracingAnalyticLightCandidates",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Build, upload, and shade analytic sphere-light candidates from active Doom lights" );

idCVar r_pathTracingRestirPTAnalyticLightCandidates(
    "r_pathTracingRestirPTAnalyticLightCandidates",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Allow ReSTIR PT debug modes 26-33 and 50-51 to build and shade analytic Doom light candidates when the global analytic-light CVar is off; set 0 for emissive-only ReSTIR validation" );

idCVar r_pathTracingAnalyticLightCandidateDump(
    "r_pathTracingAnalyticLightCandidateDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "One-shot dump of gated CPU-only analytic sphere-light candidate records" );

idCVar r_pathTracingAnalyticLightMaxGpu(
    "r_pathTracingAnalyticLightMaxGpu",
    "256",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum Doom analytic sphere-light candidates uploaded and sampled by the PT shader" );

idCVar r_pathTracingAnalyticLightIntensityScale(
    "r_pathTracingAnalyticLightIntensityScale",
    "4.0",
    CVAR_RENDERER | CVAR_FLOAT,
    "Radiance calibration scale for Doom analytic sphere lights in PT lighting" );

idCVar r_pathTracingAnalyticLightReplaceSelected(
    "r_pathTracingAnalyticLightReplaceSelected",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "When analytic Doom lights are enabled, suppress the legacy selected point-light array so analytic contribution can be inspected alone" );

idCVar r_pathTracingAnalyticSphereLightRadiusScale(
    "r_pathTracingAnalyticSphereLightRadiusScale",
    "0.08",
    CVAR_RENDERER | CVAR_FLOAT,
    "Analytic sphere-light radius as a fraction of Doom point-light radius" );

idCVar r_pathTracingAnalyticSphereLightRadiusMin(
    "r_pathTracingAnalyticSphereLightRadiusMin",
    "4.0",
    CVAR_RENDERER | CVAR_FLOAT,
    "Minimum analytic sphere-light radius" );

idCVar r_pathTracingAnalyticSphereLightRadiusMax(
    "r_pathTracingAnalyticSphereLightRadiusMax",
    "64.0",
    CVAR_RENDERER | CVAR_FLOAT,
    "Maximum analytic sphere-light radius" );

idCVar r_pathTracingAnalyticLightDoomRadiusCutoff(
    "r_pathTracingAnalyticLightDoomRadiusCutoff",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only: apply Doom authored radius as a hard analytic-light shading cutoff in the RAB sphere sampler" );

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

idCVar r_pathTracingNormalMapFlipGreen(
    "r_pathTracingNormalMapFlipGreen",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Flip sampled RT smoke normal-map green/Y before tangent-space reconstruction; diagnostic for Doom 3 legacy normal-map convention" );

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
    "Scale selected point-light contribution in mode 18 toy path tracing and ReSTIR PT analytic-light preview intensity" );

idCVar r_pathTracingToyEmissiveScale(
    "r_pathTracingToyEmissiveScale",
    "4.0",
    CVAR_RENDERER | CVAR_FLOAT,
    "Scale emissive material contribution in mode 18 toy path tracing and ReSTIR PT emissive preview intensity" );

idCVar r_pathTracingToyLightTraceCap(
    "r_pathTracingToyLightTraceCap",
    "8",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum selected point lights traced per mode 18 direct-light evaluation; clamps expensive shadow rays after light selection, max 32" );

idCVar r_pathTracingToyFakePBRSpecular(
    "r_pathTracingToyFakePBRSpecular",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use rbdoom3 legacy specmap-to-PBR roughness/F0 shading for mode 18 and related path-tracing visualizers; set 0 to opt out" );

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

idCVar r_pathTracingSamplesPerPixel(
    "r_pathTracingSamplesPerPixel",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 18 path tracer samples per pixel; default 1, clamp 1..4 for the current monolithic dispatch" );

idCVar r_pathTracingMaxPathDepth(
    "r_pathTracingMaxPathDepth",
    "2",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 18 maximum path depth including the primary hit; default 2 preserves the current one-bounce toy path" );

idCVar r_pathTracingDiffuseBounceLimit(
    "r_pathTracingDiffuseBounceLimit",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 18 diffuse secondary-bounce limit; default 1 preserves the current toy bounce" );

idCVar r_pathTracingSpecularBounceLimit(
    "r_pathTracingSpecularBounceLimit",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 18 specular/reflection secondary-bounce limit; default 0 keeps reflections disabled" );

idCVar r_pathTracingTransmissionBounceLimit(
    "r_pathTracingTransmissionBounceLimit",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Reserved transmission/glass bounce limit for the path tracer core; current shader path fails closed" );

idCVar r_pathTracingReflectionMode(
    "r_pathTracingReflectionMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 18 reflection mode: 0 off, 1 mirror-ish low-roughness validation bounce, 2 rougher validation bounce" );

idCVar r_pathTracingRussianRouletteDepth(
    "r_pathTracingRussianRouletteDepth",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Path depth where Russian roulette may start; 0 disables it in the current conservative shader path" );

idCVar r_pathTracingNextEventEstimation(
    "r_pathTracingNextEventEstimation",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable direct-light next-event-estimation style work in the mode 18 path tracer core" );

idCVar r_pathTracingSecondaryNeeMode(
    "r_pathTracingSecondaryNeeMode",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 18 secondary selected-light NEE mode: 0 off, 1 one sampled selected light, 2 legacy full selected-light loop" );

idCVar r_pathTracingSecondaryNeeVisibility(
    "r_pathTracingSecondaryNeeVisibility",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic gate for sampled secondary selected-light NEE visibility rays: 0 shades selected samples as visible, 1 traces shadow visibility" );

idCVar r_pathTracingSecondaryAnalyticNeeMode(
    "r_pathTracingSecondaryAnalyticNeeMode",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 18 secondary/reflection Doom analytic-light NEE: 0 off, 1 sampled/bounded proposals, 2 legacy full analytic-light loop" );

idCVar r_pathTracingSecondaryAnalyticNeeSamples(
    "r_pathTracingSecondaryAnalyticNeeSamples",
    "4",
    CVAR_RENDERER | CVAR_INTEGER,
    "Number of uniformly sampled Doom analytic-light proposals evaluated per secondary/reflection NEE vertex when r_pathTracingSecondaryAnalyticNeeMode is 1" );

idCVar r_pathTracingIntegratorDump(
    "r_pathTracingIntegratorDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current path tracer core settings and estimated ray budget once" );

idCVar r_pathTracingReservoirTwoSidedEmissives(
    "r_pathTracingReservoirTwoSidedEmissives",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Treat mode 20 and PDFNEE lightmode 7 emissive triangle samples as two-sided for diagnostics" );

idCVar r_pathTracingReservoirCandidateTrials(
    "r_pathTracingReservoirCandidateTrials",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 20 emissive reservoir candidate trials per pixel; higher values improve off-screen light selection at extra shader cost" );

idCVar r_pathTracingRestirPTPreviewVisibility(
    "r_pathTracingRestirPTPreviewVisibility",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 32 ReSTIR PT preview visibility gate: 0 = shade temporal reservoir only, 1 = trace an extra conservative visibility ray" );

idCVar r_pathTracingRestirPTPreviewMaxPixels(
    "r_pathTracingRestirPTPreviewMaxPixels",
    "921600",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 32 ReSTIR PT preview safety cap in pixels; default is 1280x720, 0 disables the cap" );

idCVar r_pathTracingRestirPTPreviewExposure(
    "r_pathTracingRestirPTPreviewExposure",
    "1",
    CVAR_RENDERER | CVAR_FLOAT,
    "Mode 32 ReSTIR PT rough lighting preview exposure multiplier" );

idCVar r_pathTracingRestirPTTemporalDepthThreshold(
    "r_pathTracingRestirPTTemporalDepthThreshold",
    "0.1",
    CVAR_RENDERER | CVAR_FLOAT,
    "ReSTIR PT temporal/spatial neighbor relative depth threshold for modes 31-33 and 50-51" );

idCVar r_pathTracingRestirPTTemporalNormalThreshold(
    "r_pathTracingRestirPTTemporalNormalThreshold",
    "0.35",
    CVAR_RENDERER | CVAR_FLOAT,
    "ReSTIR PT temporal/spatial neighbor normal dot threshold for modes 31-33 and 50-51; softened from RTXDI default for Doom normal-map stability" );

idCVar r_pathTracingRestirPTTemporalReservoirReuse(
    "r_pathTracingRestirPTTemporalReservoirReuse",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic gate for ReSTIR PT modes 31-33 and 50-51: 0 rejects previous-frame PT reservoirs while still generating current initial samples" );

idCVar r_pathTracingRestirPTTemporalFallbackSampling(
    "r_pathTracingRestirPTTemporalFallbackSampling",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Allow RTXDI ReSTIR PT temporal zero-motion fallback sampling for modes 31-33 and 50-51; set 0 to isolate same-material wrong-surface history reuse" );

idCVar r_pathTracingRestirPTTemporalAnalyticNeeReuse(
    "r_pathTracingRestirPTTemporalAnalyticNeeReuse",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Allow previous-frame NEE light reservoirs to be reused by ReSTIR PT temporal/spatial modes; set 0 to isolate current-frame NEE from previous light history" );

idCVar r_pathTracingRestirPTTemporalAnalyticLightChangeTolerance(
    "r_pathTracingRestirPTTemporalAnalyticLightChangeTolerance",
    "0.10",
    CVAR_RENDERER | CVAR_FLOAT,
    "Relative current/previous Doom analytic light color tolerance for ReSTIR PT temporal NEE remap; lower rejects animated light phases more aggressively" );

idCVar r_pathTracingRestirPTMaterialSimilarityMode(
    "r_pathTracingRestirPTMaterialSimilarityMode",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "ReSTIR PT temporal material similarity: 0 = legacy Doom material id/flags, 1 = RTXDI roughness/specular/diffuse, 2 = ignore diffuse, 3 = ignore specular, 4 = ignore roughness, 5 = accept all materials" );

idCVar r_pathTracingRestirPTTemporalNeighborDebugMode(
    "r_pathTracingRestirPTTemporalNeighborDebugMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 56 DI debug view 12 classifier: 0 = detailed status palette, 1 = temporal reuse bucket summary, 2 = NEE/light-remap bucket summary" );

idCVar r_pathTracingRestirPTUnifiedPrevToCurrentScan(
    "r_pathTracingRestirPTUnifiedPrevToCurrentScan",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only ReSTIR PT unified-light temporal remap proof: invert the current-to-previous unified remap in shader for previous-to-current translation" );

idCVar r_pathTracingRestirPTUnifiedLightLoad(
    "r_pathTracingRestirPTUnifiedLightLoad",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Legacy/debug RAB_LoadLightInfo comparison switch: 0 split buffers, 1 unified current/previous buffers; the active Remix-manager RRX route does not require this opt-in" );

idCVar r_pathTracingRestirPTUnifiedLightSample(
    "r_pathTracingRestirPTUnifiedLightSample",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Legacy/debug RAB_SamplePolymorphicLight comparison switch: 0 split light-type switch, 1 unified light-record type switch; the active Remix-manager RRX route uses the unified sample path internally" );

idCVar r_pathTracingRestirPTUnifiedNee(
    "r_pathTracingRestirPTUnifiedNee",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "ReSTIR PT NEE producer: 1 = RTXDI-shaped local-light DI initial producer for unified or manager RAB domains, 0 = explicit legacy rbdoom RIS fallback outside manager RAB" );

idCVar r_pathTracingRestirPTAnalyticLightTrials(
    "r_pathTracingRestirPTAnalyticLightTrials",
    "32",
    CVAR_RENDERER | CVAR_INTEGER,
    "Current-frame analytic Doom light proposal trials for ReSTIR PT NEE; higher values reduce sparse-light flicker at additional GPU cost" );

idCVar r_pathTracingRestirPTVisibilityPolicy(
    "r_pathTracingRestirPTVisibilityPolicy",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "ReSTIR PT NEE visibility policy: 0 = final/preview visibility only, 1 = selected NEE sample producer visibility, 2 = strict proposal-stream visibility" );

idCVar r_pathTracingRestirPTReflectionMode(
    "r_pathTracingRestirPTReflectionMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 56 ReSTIR PT reflection preview: 0 off, 1 sharp screen-space primary-surface lookup, 2 rough diagnostic screen-space lookup" );

idCVar r_pathTracingRestirPTSpatialSamples(
    "r_pathTracingRestirPTSpatialSamples",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 50 ReSTIR PT spatial neighbor samples per pixel; low by default for early Vulkan/driver stability" );

idCVar r_pathTracingRestirPTSpatialRadius(
    "r_pathTracingRestirPTSpatialRadius",
    "16",
    CVAR_RENDERER | CVAR_FLOAT,
    "Mode 50 ReSTIR PT spatial neighbor sampling radius in pixels" );

idCVar r_pathTracingRestirPTCombinedSpatialRadius(
    "r_pathTracingRestirPTCombinedSpatialRadius",
    "1",
    CVAR_RENDERER | CVAR_FLOAT,
    "Mode 56 ReSTIR PT direct spatial neighbor radius in pixels; kept separate from diagnostic modes 50/51" );

idCVar r_pathTracingRestirPTSpatialDiagnosticView(
    "r_pathTracingRestirPTSpatialDiagnosticView",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 51 spatial diagnostic view: 0 = source attribution, 1 = spatial acceptance/source-change status, 2 = temporal-vs-spatial source compare with local NEE replay check" );

idCVar r_pathTracingRestirPTDiDebugView(
    "r_pathTracingRestirPTDiDebugView",
    "72",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 56 DI comparison view: 72 = active default RRX DI Remix-style temporal final consumer, 0 = legacy normal view-0 comparison, 1 = raw DI reservoir estimate, 2 = DI temporal accumulator, 3 = DI spatial candidate, 4 = DI reservoir validity/M/WeightSum, 5 = DI spatial/temporal status, 6 = RTXDI final-shading input as TargetFunction*WeightSum, 7 = RTXDI final-shading input validity/target/weight/M, 8 = RTXDI page-chain state quadrants, 9 = RTXDI page-chain contribution quadrants, 10 = RTXDI temporal readiness/rejection proxy, 11 = RTXDI temporal page-flow classifier, 12 = RTXDI temporal neighbor classifier, 13 = RAB BRDF contract classifier, 14 = RTXDI initial NEE sampled-light payload classifier, 15 = RTXDI initial path metadata classifier, 16 = RTXDI final-shading input contract classifier, 17 = RTXDI final-shading target replay classifier, 18 = environment MIS contract classifier, 19 = RTXDI final-shading sampled-light payload classifier, 20 = RTXDI final-shading path metadata classifier, 21 = RTXDI final-shading random-replay metadata classifier, 22 = RAB random sampler bridge classifier, 23 = RAB path-tracer user-data bridge classifier, 24 = RAB visibility policy classifier, 25 = RAB light-sample numeric contract classifier, 26 = emissive hit-to-light map classifier, 27 = RAB light-domain load classifier, 28 = RAB light-domain sample classifier, 29 = RAB light-domain visibility classifier, 30 = initial NEE record scalar classifier, 31 = RAB path-tracer entry contract classifier, 32 = PSR denoiser payload classifier, 33 = RAB path continuation policy classifier, 34 = final shading output contract classifier, 35 = remaining supplier gap summary, 36 = unified light type/validity, 37 = unified light radiance heat, 38 = unified light current-to-previous remap, 39 = unified light source numeric sanity, 40 = CPU unified light type/validity, 41 = CPU unified versus virtual compare, 42 = CPU unified remap classifier, 43 = unified RAB load current A/B, 44 = unified RAB load previous A/B, 45 = unified RAB sample A/B, 46 = unified RAB sample numeric, 47 = RTXDI DI initial sample validity/type, 48 = RTXDI DI initial contribution preview, 49 = RTXDI DI initial reservoir numeric state, 50 = RTXDI DI initial visibility state, 51 = old NEE record versus RTXDI DI contribution split, 52 = old NEE record versus RTXDI DI selected-light compare, 53 = old NEE record versus RTXDI DI contribution ratio, 54 = PT initial versus final-shading-input contribution split, 55 = PT initial versus temporal-output contribution split, 56 = PT initial versus temporal-output state/history split, 57 = ReSTIR light-manager CPU map status, 58 = binary temporal keep mask, 59 = light-manager reservoir persistence probe, 60 = RRX DI reservoir single-light heartbeat, 61 = RRX DI initial survivor/replay light-type attribution, 62 = RRX DI legacy view-0 branch audit, 63 = RRX DI raw reservoir heartbeat, 64 = RRX DI same-dispatch echo, 65 = RRX DI same-page raw fields, 66 = RRX DI previous-page raw fields, 67 = active RAB translation parity, 68 = RRX DI temporal local invalidation, 69 = RRX DI temporal raw light-index tuple, 70 = RRX DI temporal input evidence, 71 = RRX DI synthetic temporal-core proof, 73 = RRX DI final-consumer full-screen classifier, 74 = RRX DI selected light identity hash, 75 = RRX DI weight-chain audit, 76 = RRX DI selected-light weight audit, 77 = RRX DI spatial proof bands" );

idCVar r_pathTracingRestirPTRrxDebugBypassMotion(
    "r_pathTracingRestirPTRrxDebugBypassMotion",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only RRX DI temporal probe: force previous pixel to current pixel and ignore motion-vector availability" );

idCVar r_pathTracingRestirPTRrxDebugBypassDepth(
    "r_pathTracingRestirPTRrxDebugBypassDepth",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only RRX DI temporal probe: relax RTXDI temporal depth similarity threshold" );

idCVar r_pathTracingRestirPTRrxDebugBypassNormal(
    "r_pathTracingRestirPTRrxDebugBypassNormal",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only RRX DI temporal probe: relax RTXDI temporal shading-normal similarity threshold" );

idCVar r_pathTracingRestirPTRrxDebugBypassSurfaceSimilarity(
    "r_pathTracingRestirPTRrxDebugBypassSurfaceSimilarity",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only local ReSTIR helpers: bypass rbdoom primary-surface material/geometry-normal similarity checks" );

idCVar r_pathTracingRestirPTRrxDebugBypassResetMask(
    "r_pathTracingRestirPTRrxDebugBypassResetMask",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only RRX DI temporal probe: ignore primary-surface RR reset-mask rejection before temporal reuse" );

idCVar r_pathTracingRestirPTRrxDebugBypassPortal(
    "r_pathTracingRestirPTRrxDebugBypassPortal",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only RRX DI temporal probe: reserved portal-space bypass bit; current rbdoom RRX DI surface bridge carries no portal index" );

idCVar r_pathTracingRestirPTRrxDebugFlatContribution(
    "r_pathTracingRestirPTRrxDebugFlatContribution",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only RRX DI combined output: replace valid temporal DI lighting with a fixed color after reservoir generation" );

idCVar r_pathTracingRestirPTRrxFinalConsumerOutput(
    "r_pathTracingRestirPTRrxFinalConsumerOutput",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Legacy/debug mode 56 view-0 replacement switch; active RRX proof uses r_pathTracingRestirPTDiDebugView 72 by default" );

idCVar r_pathTracingRestirPTRrxFinalConsumerCurrentOnly(
    "r_pathTracingRestirPTRrxFinalConsumerCurrentOnly",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only RRX DI final-consumer probe: consume the current-frame reservoir before temporal reuse" );

idCVar r_pathTracingRestirPTRrxSyntheticPrimaryPatch(
    "r_pathTracingRestirPTRrxSyntheticPrimaryPatch",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only RRX DI final-consumer output patch: show synthetic RTXDI temporal-core reservoir history in the primary preview" );

idCVar r_pathTracingRestirPTRrxTemporalPermutation(
    "r_pathTracingRestirPTRrxTemporalPermutation",
    "1",
    CVAR_RENDERER | CVAR_BOOL,
    "RRX DI temporal reuse: enable RTXDI permutation sampling by default for active Remix-style temporal descriptors; set 0 for debug comparison" );

idCVar r_pathTracingRestirPTRrxDisablePreviousBest(
    "r_pathTracingRestirPTRrxDebugPreviousTemporalAsBest",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only non-Remix RRX DI A/B: feed the projected previous temporal reservoir as the previous-best candidate; off by default" );

idCVar r_pathTracingRestirPdfNeeVerifierEnable(
    "r_pathTracingRestirPdfNeeVerifierEnable",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Replacement ReSTIR PDF + NEE RLU current producer: one-CVar current-frame direct lighting and clean current-reservoir output; diagnostics are optional" );

idCVar r_pathTracingRestirPdfNeeVerifierView(
    "r_pathTracingRestirPdfNeeVerifierView",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Legacy diagnostic only; replacement RLU current producer does not require a view recipe" );

idCVar r_pathTracingRestirPdfNeeVerifierLightMode(
    "r_pathTracingRestirPdfNeeVerifierLightMode",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Legacy diagnostic only; replacement RLU current producer always uses the dense current RLU lightIndex domain" );

idCVar r_pathTracingRestirPdfNeeVerifierSamples(
    "r_pathTracingRestirPdfNeeVerifierSamples",
    "32",
    CVAR_RENDERER | CVAR_INTEGER,
    "Replacement RLU current producer candidate proposals per pixel; default 32 for the one-CVar RLU path, clamped 1..64; use 64 for complex-scene diagnostics" );

idCVar r_pathTracingRestirPdfNeeVerifierVisibility(
    "r_pathTracingRestirPdfNeeVerifierVisibility",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Replacement RLU current producer visibility: 0 forced visible diagnostic, 1 use RAB NEE visibility policy; r_pathTracingRestirPTVisibilityPolicy controls selected/strict shadow rays" );

idCVar r_pathTracingRestirPdfNeeVerifierSourcePolicy(
    "r_pathTracingRestirPdfNeeVerifierSourcePolicy",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Replacement RLU current producer source policy: 0 full dense RLU uniform baseline, 1 RLU-04 range-stratified typed ranges using rangeSampleCount/(rangeCount*totalProposalSamples)" );

idCVar r_pathTracingRestirPdfNeeVerifierDomain(
    "r_pathTracingRestirPdfNeeVerifierDomain",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Legacy diagnostic only; replacement RLU current producer ignores this and uses full-domain uniform over the active dense RLU current page" );

idCVar r_pathTracingRestirPdfNeeVerifierDump(
    "r_pathTracingRestirPdfNeeVerifierDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the replacement RLU PDF + NEE current producer route, active light counts, and first missing contract once" );

idCVar r_pathTracingCleanRtxdiDiEnable(
    "r_pathTracingCleanRtxdiDiEnable",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI path: explicit enable; does not route through mode 56 or existing RRX debug views" );

idCVar r_pathTracingCleanRtxdiDiView(
    "r_pathTracingCleanRtxdiDiView",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room Remix DI debug view: 0 disabled, 1 route sentinel, 2 primary status, 3 analytic status, 4 raw flat current, 5 raw flat temporal, 6 raw flat split, 7 identity/M/history, 8 weight/targetPdf/rejection, 9 synthetic temporal, 10 synthetic analytic temporal, 11 synthetic overlap temporal, 12 real analytic full-domain temporal, 13 real analytic one-sample scalar diagnostic, 14 real analytic target-factor diagnostic, 15 real analytic binary gate diagnostic" );

idCVar r_pathTracingCleanRtxdiDiTemporal(
    "r_pathTracingCleanRtxdiDiTemporal",
    "1",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI path: enable temporal reuse; set 0 for current-only initial reservoir diagnostics" );

idCVar r_pathTracingCleanRtxdiDiSpatial(
    "r_pathTracingCleanRtxdiDiSpatial",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Reserved clean-room Remix DI spatial reuse switch; must remain off for the initial raw DI + temporal milestone" );

idCVar r_pathTracingCleanRtxdiDiBestLights(
    "r_pathTracingCleanRtxdiDiBestLights",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Reserved clean-room Remix DI best-light feedback switch; must remain off until raw DI temporal proof works without it" );

idCVar r_pathTracingCleanRtxdiDiDenoiser(
    "r_pathTracingCleanRtxdiDiDenoiser",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Reserved clean-room Remix DI denoiser/confidence/gradient switch; must remain off for reservoir-only proof" );

idCVar r_pathTracingCleanRtxdiDiFallbackLighting(
    "r_pathTracingCleanRtxdiDiFallbackLighting",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Reserved clean-room Remix DI beauty fallback switch; proof views must keep this off and use explicit status colors" );

idCVar r_pathTracingCleanRtxdiDiLightMode(
    "r_pathTracingCleanRtxdiDiLightMode",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room Remix DI light mode: 0 no lights negative test, 1 Doom analytic only, 2 emissive only deferred, 3 analytic plus emissive deferred" );

idCVar r_pathTracingCleanRtxdiDiExternalPdfNeeCurrent(
    "r_pathTracingCleanRtxdiDiExternalPdfNeeCurrent",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI consumes the PDFNEE-produced current reservoir page at u69 instead of rebuilding its own initial reservoir; first slice supports analytic split-domain indices" );

idCVar r_pathTracingCleanRtxdiDiDump(
    "r_pathTracingCleanRtxdiDiDump",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI one-shot route/resource dump for enable/view/features/light mode/page roles and dispatch dimensions" );

idCVar r_pathTracingCleanRtxdiDiGui(
    "r_pathTracingCleanRtxdiDiGui",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Show clean-room RTXDI DI route/domain/history diagnostics in the ImGui overlay; requires com_showFPS 2 or another active ImGui window" );

idCVar r_pathTracingCleanRtxdiDiCandidateCount(
    "r_pathTracingCleanRtxdiDiCandidateCount",
    "8",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room Remix DI diagnostic: RTXDI initial local-light candidate samples for real analytic views; default 8 matches the existing view-12 baseline" );

idCVar r_pathTracingCleanRtxdiDiView8Band(
    "r_pathTracingCleanRtxdiDiView8Band",
    "-1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room Remix DI diagnostic: force view 8 to show one diagnostic band full-screen; -1 keeps stacked bands, valid forced range is 0..9; band 8 classifies selected-sample black output cause, band 9 splits RLU selected/mapped replay causes" );

idCVar r_pathTracingCleanRtxdiDiResolveVisibilityReuse(
    "r_pathTracingCleanRtxdiDiResolveVisibilityReuse",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI diagnostic: in final resolve, reuse RTXDI reservoir visibility when valid before falling back to the current visibility trace" );

idCVar r_pathTracingCleanRtxdiDiResolveBrdfTarget(
    "r_pathTracingCleanRtxdiDiResolveBrdfTarget",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI diagnostic: make final resolve use the same RAB BRDF reflected-radiance function used by the target PDF instead of the flat-diffuse display resolve" );

idCVar r_pathTracingCleanRtxdiDiReferenceRab(
    "r_pathTracingCleanRtxdiDiReferenceRab",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room Remix DI diagnostic: 1 strict reference-shaped RAB; 2 altered radiance+PDF prove-use; 3 altered radiance only; 4 altered target/PDF only; 5 stable candidate-index radiance; 6 constant white radiance; 7 stable universe-identity radiance; 8 stable payload-key radiance; 9 constant white with resolve visibility forced on; 10 constant white with resolve visibility and reservoir throughput forced on" );

idCVar r_pathTracingCleanRtxdiDiView10LightCount(
    "r_pathTracingCleanRtxdiDiView10LightCount",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room Remix DI diagnostic: number of real Doom analytic payloads exposed to view 10 synthetic temporal sampling" );

idCVar r_pathTracingCleanRtxdiDiView10LightStart(
    "r_pathTracingCleanRtxdiDiView10LightStart",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room Remix DI diagnostic: first sampleable Doom analytic payload index exposed to view 10 synthetic temporal sampling" );

idCVar r_pathTracingCleanRtxdiDiView10PortalDomain(
    "r_pathTracingCleanRtxdiDiView10PortalDomain",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI diagnostic: force view 10 synthetic single-Doom-payload proof to pick from the same portal-domain analytic count as view 12" );

idCVar r_pathTracingCleanRtxdiDiSubviewDispatch(
    "r_pathTracingCleanRtxdiDiSubviewDispatch",
    "1",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI mirror/subview diagnostic: allow clean dispatch during subviews; set 0 to block the mirror-glitch route at entry" );

idCVar r_pathTracingCleanRtxdiDiSubviewReservoirPromote(
    "r_pathTracingCleanRtxdiDiSubviewReservoirPromote",
    "1",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI mirror/subview diagnostic: allow subview temporal reservoir pages to promote into previous history" );

idCVar r_pathTracingCleanRtxdiDiSubviewSurfacePromote(
    "r_pathTracingCleanRtxdiDiSubviewSurfacePromote",
    "1",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI mirror/subview diagnostic: allow subview primary-surface pages to promote into previous history" );

idCVar r_pathTracingCleanRtxdiDiView12FullAnalyticDomain(
    "r_pathTracingCleanRtxdiDiView12FullAnalyticDomain",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI view 12 diagnostic: use full current Doom analytic domain instead of portal-region analytic domain" );

idCVar r_pathTracingCleanRtxdiDiRelaxBrdfGates(
    "r_pathTracingCleanRtxdiDiRelaxBrdfGates",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI diagnostic: relax RAB opaque/geometric/view BRDF rejection gates; default off for baseline validation" );

idCVar r_pathTracingCleanRtxdiDiDoomTargetFloor(
    "r_pathTracingCleanRtxdiDiDoomTargetFloor",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI diagnostic: force valid Doom analytic light samples to have a nonzero target PDF floor; default off for baseline validation" );

idCVar r_pathTracingCleanRtxdiDiFrameFreeze(
    "r_pathTracingCleanRtxdiDiFrameFreeze",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI diagnostic: freeze the clean RTXDI frame index used by initial and temporal random streams; default off" );

idCVar r_pathTracingCleanRtxdiDiAnalyticDomainFreezeMs(
    "r_pathTracingCleanRtxdiDiAnalyticDomainFreezeMs",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room Remix DI diagnostic: freeze uploaded Doom analytic current/previous/remap buffers for this many milliseconds between refreshes; 0 disables the freeze" );

idCVar r_pathTracingCleanRtxdiDiBypassLightUniverse(
    "r_pathTracingCleanRtxdiDiBypassLightUniverse",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI diagnostic: bypass the persistent Doom analytic light universe and synthesize compact current/previous/remap buffers from uploaded active candidates; default off" );

idCVar r_pathTracingCleanRtxdiDiDoomColorSource(
    "r_pathTracingCleanRtxdiDiDoomColorSource",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room Remix DI diagnostic: Doom analytic payload color source; 0 material shader registers, 1 game current renderLight color, 2 game authored base color" );

idCVar r_pathTracingCleanRtxdiDiRequireProvenDoomLights(
    "r_pathTracingCleanRtxdiDiRequireProvenDoomLights",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Clean-room Remix DI diagnostic: require game-linked non-temporary Doom analytic lights in the uploaded clean RTXDI analytic domain; default off" );

idCVar r_pathTracingCleanRtxdiDiTemporalBiasCorrection(
    "r_pathTracingCleanRtxdiDiTemporalBiasCorrection",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room Remix DI diagnostic: RTXDI temporal bias correction mode; 0 off, 1 basic. Default matches the current clean view-12 path" );

idCVar r_pathTracingCleanRtxdiDiTemporalMaxHistory(
    "r_pathTracingCleanRtxdiDiTemporalMaxHistory",
    "5",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room Remix DI diagnostic: RTXDI temporal maxHistoryLength parameter; default 5 limits stale history during movement; use 0 to run temporal while suppressing previous-reservoir history contribution" );

idCVar r_pathTracingCleanRtxdiDiTemporalAudit(
    "r_pathTracingCleanRtxdiDiTemporalAudit",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Clean-room RTXDI DI temporal audit: 0 off, 1 encode per-pixel accumulator gates into the clean output and aggregate them into the ImGui panel via readback" );

idCVar r_pathTracingRestirPTView68Dump(
    "r_pathTracingRestirPTView68Dump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 56 DI view 68/69 terminal dump: 0 off, 1 one-shot, 2 continuous; forces a readback and classifies view-68 colors or aggregates view-69 raw tuples around the selected pixel" );

idCVar r_pathTracingRestirPTView68DumpX(
    "r_pathTracingRestirPTView68DumpX",
    "-1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 56 DI view 68/69 dump X pixel; -1 samples the center of the left diagnostic half" );

idCVar r_pathTracingRestirPTView68DumpY(
    "r_pathTracingRestirPTView68DumpY",
    "-1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 56 DI view 68/69 dump Y pixel; -1 samples the screen center" );

idCVar r_pathTracingRestirPTView68DumpRadius(
    "r_pathTracingRestirPTView68DumpRadius",
    "32",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 56 DI view 68/69 dump neighborhood radius in pixels" );

idCVar r_pathTracingRestirPTGiDebugView(
    "r_pathTracingRestirPTGiDebugView",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 56 GI comparison view: 0 = normal output, 1 = raw GI initial estimate, 2 = GI temporal accumulator, 3 = reserved GI spatial, 4 = initial reservoir validity/M/WeightSum" );

idCVar r_pathTracingRestirPTDirectResolutionScale(
    "r_pathTracingRestirPTDirectResolutionScale",
    "1",
    CVAR_RENDERER | CVAR_FLOAT,
    "Scale for staged ReSTIR direct-lighting reservoir domain in modes 50-51 and mode 18 ReSTIR direct; clamps to 0.25..1.0" );

idCVar r_pathTracingRestirPTRaySparsity(
    "r_pathTracingRestirPTRaySparsity",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Interleaved sparse update rate for staged ReSTIR direct-lighting producer rays; 1 disables, 2-8 updates roughly 1/N reservoir pixels per frame and reconstructs from active representatives" );

idCVar r_pathTracingRestirPTGiRaySparsity(
    "r_pathTracingRestirPTGiRaySparsity",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Interleaved sparse update rate for staged ReSTIR GI initial-reservoir producer rays in modes 53-56; 1 disables, 2-8 updates roughly 1/N full-resolution reservoir pixels per frame" );

idCVar r_pathTracingRestirPTPrimarySurfacePrepass(
    "r_pathTracingRestirPTPrimarySurfacePrepass",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "ReSTIR PT path: use the standalone primary-surface producer/resolve path so mode 56 avoids the combined monolithic shader; set 0 for legacy fallback testing" );

idCVar r_pathTracingRestirPTPassDump(
    "r_pathTracingRestirPTPassDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current ReSTIR PT pass plan and RTXDI reservoir buffer indices once" );

idCVar r_pathTracingSafetyDump(
    "r_pathTracingSafetyDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump path tracing dispatch safety metadata once before the RT dispatch" );

idCVar r_pathTracingDispatchTileEnable(
    "r_pathTracingDispatchTileEnable",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic mode: split path tracing DispatchRays into screen-space tiles while preserving the same scene and resources" );

idCVar r_pathTracingDispatchTileWidth(
    "r_pathTracingDispatchTileWidth",
    "512",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic tiled DispatchRays tile width in pixels when r_pathTracingDispatchTileEnable is set" );

idCVar r_pathTracingDispatchTileHeight(
    "r_pathTracingDispatchTileHeight",
    "512",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic tiled DispatchRays tile height in pixels when r_pathTracingDispatchTileEnable is set" );

idCVar r_pathTracingDispatchTileDump(
    "r_pathTracingDispatchTileDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the current path tracing tiled DispatchRays plan once" );

idCVar r_pathTracingDisableAnyHitAlpha(
    "r_pathTracingDisableAnyHitAlpha",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic kill switch: disable any-hit alpha, translucent, glass, and particle rejection while preserving shadow self-ignore" );

idCVar r_pathTracingDisableSelectedLightLoop(
    "r_pathTracingDisableSelectedLightLoop",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic kill switch: disable the legacy selected point-light loops in the smoke/path tracer shader" );

idCVar r_pathTracingDisableAnalyticLightLoop(
    "r_pathTracingDisableAnalyticLightLoop",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic kill switch: disable Doom analytic light loops and RAB analytic light sampling in the smoke/path tracer shader" );

idCVar r_pathTracingDisableEmissiveTriangleSampling(
    "r_pathTracingDisableEmissiveTriangleSampling",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic kill switch: disable emissive triangle sampling loops for mode 20 and ReSTIR PT validation paths" );

idCVar r_pathTracingEmissiveDistribution(
    "r_pathTracingEmissiveDistribution",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "Use the CPU-built emissive CDF proposal table when available; 0 forces the legacy shader linear scan" );

idCVar r_pathTracingDisableDiffuseSecondaryRay(
    "r_pathTracingDisableDiffuseSecondaryRay",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic kill switch: disable diffuse secondary continuation rays in the toy path tracer and RAB path tracer bridge" );

idCVar r_pathTracingDisableReflectionRay(
    "r_pathTracingDisableReflectionRay",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic kill switch: disable reflection/specular secondary rays in the toy path tracer" );

idCVar r_pathTracingDisablePrimarySurfaceHistory(
    "r_pathTracingDisablePrimarySurfaceHistory",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic kill switch: disable primary surface history writes, clears, copies, and previous-camera history validity" );

idCVar r_pathTracingDisableReservoirWrites(
    "r_pathTracingDisableReservoirWrites",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic kill switch: disable smoke and ReSTIR PT reservoir writes from the ray tracing shader" );

idCVar r_pathTracingDisableRestirVisibilityRay(
    "r_pathTracingDisableRestirVisibilityRay",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic kill switch: disable the ReSTIR PT preview visibility ray even when the preview visibility CVar or mode requests it" );

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

idCVar r_pathTracingPassTimingDump(
    "r_pathTracingPassTimingDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current RT/PT pseudo-pass CPU submit timings and debug-output classification once" );

idCVar r_pathTracingOptickGpuMarkers(
    "r_pathTracingOptickGpuMarkers",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable experimental Optick GPU markers inside the RT smoke/path tracing build and dispatch passes" );

idCVar r_pathTracingNsightGpuMarkers(
    "r_pathTracingNsightGpuMarkers",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Enable NVRHI GPU debug markers around RT smoke/path tracing dispatches for Nsight captures" );

idCVar r_pathTracingRestirPTGpuTimingDump(
    "r_pathTracingRestirPTGpuTimingDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to capture and print one frame of ReSTIR PT per-dispatch GPU timestamp timings" );

idCVar r_pathTracingReservoirDump(
    "r_pathTracingReservoirDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump current RT smoke reservoir ownership/reset state once" );

idCVar r_pathTracingSceneInputsDump(
    "r_pathTracingSceneInputsDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump the committed RT/PT scene input package once" );

idCVar r_pathTracingGpuSkinning(
    "r_pathTracingGpuSkinning",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental PT skinned GPU-skinning scaffold: 0 = CPU-skinned bridge only, 1 = diagnostic compute output buffer, 2 = compute overwrites skinned dynamic vertices before BLAS build" );

idCVar r_pathTracingMotionVectorExport(
    "r_pathTracingMotionVectorExport",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "PT private motion-vector export writer: 0 disabled, 1 write combined geometry current-to-previous pixel motion plus validity/source mask into private PT UAVs" );

idCVar r_pathTracingMotionVectorDisableRigid(
    "r_pathTracingMotionVectorDisableRigid",
    "0",
    CVAR_RENDERER | CVAR_BOOL,
    "Debug-only PT motion-vector quarantine: mark routed rigid surfaces as invalid motion instead of exporting rigid object-motion vectors" );

idCVar r_pathTracingRestirPTDirectLighting(
    "r_pathTracingRestirPTDirectLighting",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Opt-in mode 18 ReSTIR PT direct-lighting hybrid: 0 native mode 18 NEE, 1 replace primary direct NEE with completed spatial reservoir contribution; experimental and tuned for 1 spp" );

idCVar r_pathTracingRestirPTMode18DebugView(
    "r_pathTracingRestirPTMode18DebugView",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 18 ReSTIR hybrid debug view: 0 normal hybrid render, 1 native primary direct, 2 ReSTIR primary direct, 3 absolute delta heat, 4 ratio heat" );

idCVar r_pathTracingRestirPTMode18HeavyDirect(
    "r_pathTracingRestirPTMode18HeavyDirect",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Heavy-handed mode 18 ReSTIR hybrid test: 0 only replace primary direct NEE, 1 also suppress native secondary/reflection direct NEE so ReSTIR primary direct is easier to isolate" );

idCVar r_pathTracingRestirPTMode56Accumulation(
    "r_pathTracingRestirPTMode56Accumulation",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Temporary diagnostic accumulation for debug mode 56 final/subview output; 0 disabled, 1 enabled" );

idCVar r_pathTracingRestirPTMode56AccumulationMaxFrames(
    "r_pathTracingRestirPTMode56AccumulationMaxFrames",
    "256",
    CVAR_RENDERER | CVAR_INTEGER,
    "Maximum accumulated frames for temporary debug mode 56 final/subview output accumulation" );

idCVar r_pathTracingDLSSRRGuideDebugView(
    "r_pathTracingDLSSRRGuideDebugView",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Mode 56 DLSS RR guide debug view: 0 = off, 1 = albedo, 2 = normal, 3 = roughness, 4 = depth, 5 = hit distance, 6 = motion-vector mask, 7 = reset/disocclusion mask, 8 = specular albedo/F0, 9 = RR input HDR preview" );

idCVar r_pathTracingDLSSRRProbe(
    "r_pathTracingDLSSRRProbe",
    "1",
    CVAR_RENDERER | CVAR_INTEGER | CVAR_INIT,
    "Initialize the experimental Streamline DLSS/RR bridge at renderer startup and dump SDK feature support; set 0 before startup to disable" );

idCVar r_pathTracingDLSSRR(
    "r_pathTracingDLSSRR",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Experimental DLSS Ray Reconstruction evaluation gate for mode 56 primary-prepass output" );

idCVar r_pathTracingDLSSRRColorBuffersHDR(
    "r_pathTracingDLSSRRColorBuffersHDR",
    "1",
    CVAR_RENDERER | CVAR_INTEGER,
    "DLSS RR colorBuffersHDR option for mode 56; default 1 because the current Streamline/NGX RR path rejects the SDR-tagged option" );

idCVar r_pathTracingDLSSRRPreExposure(
    "r_pathTracingDLSSRRPreExposure",
    "1.0",
    CVAR_RENDERER | CVAR_FLOAT,
    "DLSS RR preExposure option for mode 56; keep 1.0 unless the input color has already been pre-exposed" );

idCVar r_pathTracingDLSSRRExposureScale(
    "r_pathTracingDLSSRRExposureScale",
    "1.0",
    CVAR_RENDERER | CVAR_FLOAT,
    "DLSS RR exposureScale option for mode 56; diagnostic color/exposure tuning for the experimental RR bridge" );

idCVar r_pathTracingDLSSRRSharpness(
    "r_pathTracingDLSSRRSharpness",
    "0.0",
    CVAR_RENDERER | CVAR_FLOAT,
    "DLSS RR sharpness option for mode 56, clamped 0..1" );

idCVar r_pathTracingDLSSRRVerbose(
    "r_pathTracingDLSSRRVerbose",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Verbose Streamline DLSS/RR probe logging, including required tags and Vulkan extension requirements" );

idCVar r_pathTracingPortalTransitionDump(
    "r_pathTracingPortalTransitionDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic PT transition logging: 0 off, 1 next portal-area transition then off, 2 every portal-area transition, 3 every scene/resource transition" );

idCVar r_pathTracingWaitForIdleOnPortalChange(
    "r_pathTracingWaitForIdleOnPortalChange",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Diagnostic: wait for GPU idle before replacing committed PT scene resources when portal/scene membership changes" );

idCVar r_pathTracingSceneRetireFrames(
    "r_pathTracingSceneRetireFrames",
    "6",
    CVAR_RENDERER | CVAR_INTEGER,
    "Number of frames to retain replaced PT scene packages before releasing old handles; clamped 0..32" );

idCVar r_pathTracingSceneRetireDump(
    "r_pathTracingSceneRetireDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "PT scene retirement logging: 0 off, 1 next retire/release event then off, 2 every retire/release event" );

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

idCVar r_pathTracingGeometryUniverseRangeDump(
    "r_pathTracingGeometryUniverseRangeDump",
    "0",
    CVAR_RENDERER | CVAR_INTEGER,
    "Set to 1 to dump one-shot RT smoke geometry-universe static current/previous range records" );
