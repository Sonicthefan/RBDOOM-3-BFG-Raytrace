#include "precompiled.h"
#pragma hdrstop

// Per-frame scene build orchestration for the RT smoke/path tracing path.
//
// This file keeps the top-level build order visible: capture Doom surfaces,
// build material/emissive data, create and upload buffers, submit acceleration
// structures, create bindings, commit resources, then run scene diagnostics.
// Lower-level classification, capture, resource, and diagnostic work stays in
// the narrower PathTrace* modules.

#include "PathTraceAcceleration.h"
#include "PathTraceCVars.h"
#include "PathTraceDebugDumps.h"
#include "PathTraceDrawSurfCapture.h"
#include "PathTraceDynamicMaterialState.h"
#include "PathTraceEmissiveCandidates.h"
#include "PathTraceMaterialUniverse.h"
#include "PathTraceMaterialTextureDiscovery.h"
#include "PathTracePrimaryPass.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceSceneUniverse.h"
#include "PathTraceSmokeResources.h"
#include "PathTraceSurfaceClassification.h"
#include "../RenderBackend.h"
#include "../Image.h"
#include "../Passes/CommonPasses.h"
#include "../../framework/Common_local.h"
#include "../../sys/DeviceManager.h"

#include <algorithm>
#include <vector>

extern DeviceManager* deviceManager;

namespace {

const int RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES = 120;
const int RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS = 65536;
const int RT_SMOKE_GEOMETRY_VALIDATION_DUMP_RECORDS = 16;
const int RT_PT_RESIDENT_BOUNDS_OVERLAY_SAFE_BOXES = 64;

int g_smokeLastSceneTimingLogMs = -1000000;
uint64 g_smokeLastGeometryValidationDumpGeneration = 0;
int g_smokeLastGeometryValidationDumpErrors = 0;

struct RtSmokeStaticDrawSurfCounts
{
    int surfaces = 0;
    int triangles = 0;
};

void ApplySmokeRoutedScenePreset(int debugMode, int requestedPreset, const char* label)
{
    const int preset = idMath::ClampInt(0, 4, requestedPreset);
    const bool mode18 = debugMode == 18;
    const bool mode20 = debugMode == 20;
    if (!mode18 && !mode20)
    {
        return;
    }

    r_pathTracingDebugMode.SetInteger(debugMode);
    r_pathTracingSceneSource.SetInteger(3);
    r_pathTracingRigidBlasGpuScaffold.SetInteger(1);
    r_pathTracingRigidBlasGpuBuild.SetInteger(1);
    r_pathTracingRigidTlasRoute.SetInteger(1);
    r_pathTracingRigidRouteMode18.SetInteger(mode18 ? 1 : r_pathTracingRigidRouteMode18.GetInteger());
    r_pathTracingRigidRouteMode20.SetInteger(mode20 ? 1 : r_pathTracingRigidRouteMode20.GetInteger());
    r_pathTracingRigidRouteRemoveDynamic.SetInteger(1);
    r_pathTracingRigidRouteEmissiveCards.SetInteger(1);
    r_pathTracingRigidResidency.SetInteger(1);
    r_pathTracingStaticAreaPreload.SetInteger(1);

    const int portalSteps = preset == 4 ? 4 : 1;
    r_pathTracingRigidResidencyPortalSteps.SetInteger(portalSteps);
    r_pathTracingStaticAreaPreloadPortalSteps.SetInteger(portalSteps);
    r_pathTracingLightAreaPortalSteps.SetInteger(portalSteps);

    r_pathTracingLightAreaFilter.SetInteger(mode20 && preset >= 2 ? 1 : 0);
    r_pathTracingLightAreaFilterApply.SetInteger(mode20 && preset == 3 ? 1 : 0);
    r_pathTracingLightAreaOverflowMax.SetInteger(512);
    r_pathTracingLightUniverseChurn.SetInteger(mode20 && preset >= 2 ? 1 : 0);

    const int presetRigidRouteMaxInstances = preset == 4 ? 256 : 64;
    if (r_pathTracingRigidRouteMaxInstances.GetInteger() < presetRigidRouteMaxInstances)
    {
        r_pathTracingRigidRouteMaxInstances.SetInteger(presetRigidRouteMaxInstances);
    }

    common->Printf("PathTracePrimaryPass: applied %s preset %d source3=1 rigidRoute=1 rigidResidency=1 staticPreload=1 rigidEmissiveCards=1 portalSteps=%d lightAreaDiag=%d lightAreaApply=%d bvhValidation=%d churn=%d overflowMax=512 rigidRouteMax=%d\n",
        label ? label : "mode test",
        preset,
        portalSteps,
        mode20 && preset >= 2 ? 1 : 0,
        mode20 && preset == 3 ? 1 : 0,
        preset == 4 ? 1 : 0,
        mode20 && preset >= 2 ? 1 : 0,
        r_pathTracingRigidRouteMaxInstances.GetInteger());
}

nvrhi::ObjectType GetPathTraceCommandObjectType()
{
    if (deviceManager && deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        return nvrhi::ObjectTypes::VK_CommandBuffer;
    }
    return nvrhi::ObjectTypes::D3D12_GraphicsCommandList;
}

RtSmokeStaticDrawSurfCounts CountCurrentStaticDrawSurfs(const viewDef_t* viewDef)
{
    RtSmokeStaticDrawSurfCounts counts;
    if (!viewDef || !viewDef->drawSurfs)
    {
        return counts;
    }

    for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
    {
        const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
        const srfTriangles_t* tri = nullptr;
        if (!ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr))
        {
            continue;
        }
        if (ClassifySmokeSurface(viewDef, drawSurf, tri) != RtSmokeSurfaceClass::StaticWorld)
        {
            continue;
        }
        ++counts.surfaces;
        counts.triangles += tri->numIndexes / 3;
    }
    return counts;
}

void AppendRigidResidencyBoundsOverlayLines(
    const std::vector<RtPathTraceRigidResidencyBoundsBox>& boxes,
    std::vector<RtPathTraceBoundsOverlayLine>& lines)
{
    static const int edgeStarts[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3 };
    static const int edgeEnds[12] = { 1, 2, 3, 0, 5, 6, 7, 4, 4, 5, 6, 7 };

    for (const RtPathTraceRigidResidencyBoundsBox& box : boxes)
    {
        if (!box.valid || lines.size() + 12 > RT_PT_BOUNDS_OVERLAY_MAX_LINES)
        {
            continue;
        }

        for (int edgeIndex = 0; edgeIndex < 12; ++edgeIndex)
        {
            RtPathTraceBoundsOverlayLine line;
            line.startAndPad = idVec4(box.corners[edgeStarts[edgeIndex]].x, box.corners[edgeStarts[edgeIndex]].y, box.corners[edgeStarts[edgeIndex]].z, 0.0f);
            line.endAndPad = idVec4(box.corners[edgeEnds[edgeIndex]].x, box.corners[edgeEnds[edgeIndex]].y, box.corners[edgeEnds[edgeIndex]].z, 0.0f);
            line.color = box.color;
            lines.push_back(line);
        }
    }
}

int CountSmokeDynamicSurfaces(const RtSmokeSurfaceClassStats& stats)
{
    return stats.rigidEntitySurfaces + stats.skinnedDeformedSurfaces + stats.particleAlphaSurfaces + stats.unknownSurfaces;
}

int CountSmokeDynamicTriangles(const RtSmokeSurfaceClassStats& stats)
{
    return stats.rigidEntityTriangles + stats.skinnedDeformedTriangles + stats.particleAlphaTriangles + stats.unknownTriangles;
}

std::vector<uint32_t> BuildSortedUniqueMaterialIds(const std::vector<uint32_t>& materialIds)
{
    std::vector<uint32_t> uniqueIds = materialIds;
    std::sort(uniqueIds.begin(), uniqueIds.end());
    uniqueIds.erase(std::unique(uniqueIds.begin(), uniqueIds.end()), uniqueIds.end());
    return uniqueIds;
}

struct RtSmokeSourceCompareMaterialDiff
{
    int oldUnique = 0;
    int source3Unique = 0;
    int missing = 0;
    int extra = 0;
    uint32_t missingSamples[8] = {};
    uint32_t extraSamples[8] = {};
    int missingSampleCount = 0;
    int extraSampleCount = 0;
};

RtSmokeSourceCompareMaterialDiff CompareSmokeDynamicMaterialIds(
    const std::vector<uint32_t>& oldMaterialIds,
    const std::vector<uint32_t>& source3MaterialIds)
{
    RtSmokeSourceCompareMaterialDiff diff;
    const std::vector<uint32_t> oldUnique = BuildSortedUniqueMaterialIds(oldMaterialIds);
    const std::vector<uint32_t> source3Unique = BuildSortedUniqueMaterialIds(source3MaterialIds);
    diff.oldUnique = static_cast<int>(oldUnique.size());
    diff.source3Unique = static_cast<int>(source3Unique.size());

    size_t oldIndex = 0;
    size_t source3Index = 0;
    while (oldIndex < oldUnique.size() || source3Index < source3Unique.size())
    {
        if (source3Index >= source3Unique.size() || (oldIndex < oldUnique.size() && oldUnique[oldIndex] < source3Unique[source3Index]))
        {
            if (diff.missingSampleCount < static_cast<int>(sizeof(diff.missingSamples) / sizeof(diff.missingSamples[0])))
            {
                diff.missingSamples[diff.missingSampleCount++] = oldUnique[oldIndex];
            }
            ++diff.missing;
            ++oldIndex;
        }
        else if (oldIndex >= oldUnique.size() || source3Unique[source3Index] < oldUnique[oldIndex])
        {
            if (diff.extraSampleCount < static_cast<int>(sizeof(diff.extraSamples) / sizeof(diff.extraSamples[0])))
            {
                diff.extraSamples[diff.extraSampleCount++] = source3Unique[source3Index];
            }
            ++diff.extra;
            ++source3Index;
        }
        else
        {
            ++oldIndex;
            ++source3Index;
        }
    }
    return diff;
}

void PrintSmokeMaterialIdSamples(const char* label, const uint32_t* samples, int sampleCount)
{
    if (sampleCount <= 0)
    {
        return;
    }

    char sampleText[256];
    sampleText[0] = '\0';
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        idStr::Append(sampleText, sizeof(sampleText), va("%s%u", sampleIndex == 0 ? "" : ",", samples[sampleIndex]));
    }
    common->Printf("PathTracePrimaryPass: PT source3 compare %s materialIds=[%s]\n", label, sampleText);
}

void DumpSource3CaptureCompare(
    const viewDef_t* viewDef,
    uint64 frameIndex,
    const RtSmokeSurfaceClassStats& source3ClassStats,
    const RtSmokeSurfaceSkipStats& source3SkipStats,
    int source3Surfaces,
    int source3Verts,
    int source3Indexes,
    const std::vector<uint32_t>& source3DynamicIndexes,
    const std::vector<uint32_t>& source3DynamicTriangleMaterialIds)
{
    std::vector<PathTraceSmokeVertex> oldDynamicVertices;
    std::vector<uint32_t> oldDynamicIndexes;
    std::vector<uint32_t> oldDynamicTriangleClasses;
    std::vector<uint32_t> oldDynamicTriangleMaterialIds;
    RtSmokeGeometryUniverse oldGeometryUniverse;
    bool oldStaticCacheChanged = false;
    idVec3 oldSceneOrigin = vec3_origin;
    int oldSurfaces = 0;
    int oldVerts = 0;
    int oldIndexes = 0;
    int oldAnchorTriangle = -1;
    RtSmokeSurfaceClassStats oldClassStats;
    RtSmokeSurfaceSkipStats oldSkipStats;
    RtSmokeDynamicGeometryStats oldDynamicStats;
    RtSmokeAttributeStats oldAttributeStats;
    RtSmokeMaterialStats oldMaterialStats;
    RtSmokeBucketRanges oldBucketRanges;
    RtSmokeSceneCaptureTiming oldCaptureTiming;

    oldGeometryUniverse.BeginFrame(frameIndex);
    const bool oldCaptured = CaptureDoomSurfacesForSmokeTest(
        viewDef,
        oldDynamicVertices,
        oldDynamicIndexes,
        oldDynamicTriangleClasses,
        oldDynamicTriangleMaterialIds,
        oldGeometryUniverse,
        oldStaticCacheChanged,
        oldSceneOrigin,
        oldSurfaces,
        oldVerts,
        oldIndexes,
        oldAnchorTriangle,
        oldClassStats,
        oldSkipStats,
        oldDynamicStats,
        oldAttributeStats,
        oldMaterialStats,
        oldBucketRanges,
        oldCaptureTiming,
        nullptr,
        false,
        false,
        false);
    oldGeometryUniverse.EndFrame();

    const RtSmokeGeometryUniverseStats oldGeometryStats = oldGeometryUniverse.GetStats(false);
    const RtSmokeSourceCompareMaterialDiff materialDiff = CompareSmokeDynamicMaterialIds(oldDynamicTriangleMaterialIds, source3DynamicTriangleMaterialIds);

    common->Printf("PathTracePrimaryPass: PT source3 compare capturedOld=%d totals old/source3 surfaces=%d/%d verts=%d/%d indexes=%d/%d staticTris=%d/%d dynTris=%d/%d oldStaticCache tris=%d records=%d\n",
        oldCaptured ? 1 : 0,
        oldSurfaces,
        source3Surfaces,
        oldVerts,
        source3Verts,
        oldIndexes,
        source3Indexes,
        oldClassStats.staticWorldTriangles,
        source3ClassStats.staticWorldTriangles,
        CountSmokeDynamicTriangles(oldClassStats),
        CountSmokeDynamicTriangles(source3ClassStats),
        oldGeometryStats.staticTriangles,
        oldGeometryStats.staticRecords);
    common->Printf("PathTracePrimaryPass: PT source3 compare classes old/source3 static=%d/%d rigid=%d/%d skinned=%d/%d particle=%d/%d unknown=%d/%d dynamicSurfaces=%d/%d dynamicIndexes=%d/%d\n",
        oldClassStats.staticWorldSurfaces,
        source3ClassStats.staticWorldSurfaces,
        oldClassStats.rigidEntitySurfaces,
        source3ClassStats.rigidEntitySurfaces,
        oldClassStats.skinnedDeformedSurfaces,
        source3ClassStats.skinnedDeformedSurfaces,
        oldClassStats.particleAlphaSurfaces,
        source3ClassStats.particleAlphaSurfaces,
        oldClassStats.unknownSurfaces,
        source3ClassStats.unknownSurfaces,
        CountSmokeDynamicSurfaces(oldClassStats),
        CountSmokeDynamicSurfaces(source3ClassStats),
        static_cast<int>(oldDynamicIndexes.size()),
        static_cast<int>(source3DynamicIndexes.size()));
    common->Printf("PathTracePrimaryPass: PT source3 compare dynamicMaterials unique old/source3=%d/%d missing=%d extra=%d triangleIds=%d/%d\n",
        materialDiff.oldUnique,
        materialDiff.source3Unique,
        materialDiff.missing,
        materialDiff.extra,
        static_cast<int>(oldDynamicTriangleMaterialIds.size()),
        static_cast<int>(source3DynamicTriangleMaterialIds.size()));
    PrintSmokeMaterialIdSamples("missingFromSource3", materialDiff.missingSamples, materialDiff.missingSampleCount);
    PrintSmokeMaterialIdSamples("extraInSource3", materialDiff.extraSamples, materialDiff.extraSampleCount);
    common->Printf("PathTracePrimaryPass: PT source3 compare skips old/source3 null=%d/%d geom=%d/%d material=%d/%d space=%d/%d model=%d/%d invalid=%d/%d nonCurrent=%d/%d limits=%d/%d zero=%d/%d gui=%d/%d callback=%d/%d\n",
        oldSkipStats.nullSurface,
        source3SkipStats.nullSurface,
        oldSkipStats.missingGeometry,
        source3SkipStats.missingGeometry,
        oldSkipStats.nullMaterial,
        source3SkipStats.nullMaterial,
        oldSkipStats.nullSpace,
        source3SkipStats.nullSpace,
        oldSkipStats.nullModel,
        source3SkipStats.nullModel,
        oldSkipStats.invalidIndexCount,
        source3SkipStats.invalidIndexCount,
        oldSkipStats.nonCurrentCache,
        source3SkipStats.nonCurrentCache,
        oldSkipStats.limitExceeded,
        source3SkipStats.limitExceeded,
        oldSkipStats.zeroAreaOnly,
        source3SkipStats.zeroAreaOnly,
        oldSkipStats.guiSurface,
        source3SkipStats.guiSurface,
        oldSkipStats.callbackEntity,
        source3SkipStats.callbackEntity);
}

}

void PathTracePrimaryPass::BuildRayTracingSmokeTestScene(const viewDef_t* viewDef)
{
    OPTICK_EVENT("PT Build Scene");

    const int sceneStartMs = Sys_Milliseconds();
    const int mode18Preset = r_pathTracingMode18TestPreset.GetInteger();
    if (mode18Preset != 0)
    {
        ApplySmokeRoutedScenePreset(18, mode18Preset, "mode18 test");
        r_pathTracingMode18TestPreset.SetInteger(0);
    }
    const int mode20Preset = r_pathTracingMode20TestPreset.GetInteger();
    if (mode20Preset != 0)
    {
        ApplySmokeRoutedScenePreset(20, mode20Preset, "mode20 test");
        r_pathTracingMode20TestPreset.SetInteger(0);
    }
    m_smokeSceneBuilt = false;
    m_smokeBoundsOverlayLines.clear();
    m_smokeBoundsOverlayLineCount = 0;
    m_smokeBoundsOverlayViewValid = false;
    const int requestedDebugMode = idMath::ClampInt(0, 25, r_pathTracingDebugMode.GetInteger());
    const bool enableTextureProbe = (requestedDebugMode >= 8 && requestedDebugMode <= 20);

    if (!m_smokeTlas || !m_smokeBindingLayout || !m_smokeTextureBindlessLayout || !m_smokeTextureDescriptorTable || !m_smokeOutputTexture || !m_smokeAccumulationTexture || !m_smokeConstantsBuffer || !m_smokeBoundsOverlayLineBuffer)
    {
        return;
    }
    if (viewDef && m_smokeLightUniverseRenderWorld != viewDef->renderWorld)
    {
        m_smokeLightUniverse.Clear();
        m_smokeLightUniverseRenderWorld = viewDef->renderWorld;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return;
    }

    if (viewDef)
    {
        m_smokeBoundsOverlayCameraOrigin = viewDef->renderView.vieworg;
        m_smokeBoundsOverlayCameraForward = viewDef->renderView.viewaxis[0];
        m_smokeBoundsOverlayCameraLeft = viewDef->renderView.viewaxis[1];
        m_smokeBoundsOverlayCameraUp = viewDef->renderView.viewaxis[2];
        m_smokeBoundsOverlayCameraForward.Normalize();
        m_smokeBoundsOverlayCameraLeft.Normalize();
        m_smokeBoundsOverlayCameraUp.Normalize();
        m_smokeBoundsOverlayTanX = idMath::Tan(DEG2RAD(viewDef->renderView.fov_x * 0.5f));
        m_smokeBoundsOverlayTanY = idMath::Tan(DEG2RAD(viewDef->renderView.fov_y * 0.5f));
        for (int matrixElement = 0; matrixElement < 16; ++matrixElement)
        {
            m_smokeBoundsOverlayModelViewMatrix[matrixElement] = viewDef->worldSpace.modelViewMatrix[matrixElement];
            m_smokeBoundsOverlayProjectionMatrix[matrixElement] = viewDef->projectionMatrix[matrixElement];
        }
        m_smokeBoundsOverlayViewValid = true;
    }

    nvrhi::ICommandList* commandList = m_backend ? m_backend->GL_GetCommandList() : nullptr;
    if (!commandList)
    {
        return;
    }
    const bool optickGpuMarkers = r_pathTracingOptickGpuMarkers.GetInteger() != 0;
    if (optickGpuMarkers)
    {
        OPTICK_GPU_CONTEXT((void*)commandList->getNativeObject(GetPathTraceCommandObjectType()));
    }

    std::vector<PathTraceSmokeVertex> dynamicVertexData;
    std::vector<uint32_t> dynamicIndexData;
    std::vector<uint32_t> dynamicTriangleClassData;
    std::vector<uint32_t> dynamicTriangleMaterialData;
    int sourceSurfaces = 0;
    int sourceVerts = 0;
    int sourceIndexes = 0;
    int anchorTriangle = -1;
    RtSmokeSurfaceClassStats classStats;
    RtSmokeSurfaceSkipStats skipStats;
    RtSmokeDynamicGeometryStats dynamicStats;
    RtSmokeAttributeStats attributeStats;
    RtSmokeMaterialStats materialStats;
    RtSmokeBucketRanges bucketRanges;
    const bool dumpClassReasons = r_pathTracingClassDump.GetInteger() != 0;
    RtSmokeSurfaceClassReasonSamples reasonSamples;
    bool staticCacheChanged = false;
    RtSmokeSceneCaptureTiming captureTiming;
    std::vector<PathTraceSmokeVertex>& staticVertexCache = m_smokeGeometryUniverse.StaticVertices();
    std::vector<uint32_t>& staticIndexCache = m_smokeGeometryUniverse.StaticIndexes();
    std::vector<uint32_t>& staticTriangleClassCache = m_smokeGeometryUniverse.StaticTriangleClasses();
    std::vector<uint32_t>& staticTriangleMaterialCache = m_smokeGeometryUniverse.StaticTriangleMaterials();
    const int captureStartMs = Sys_Milliseconds();
    bool usingDoomSurfaces = false;
    const int sceneSource = idMath::ClampInt(0, 3, r_pathTracingSceneSource.GetInteger());
    const bool useSceneUniverseStaticGeometry = sceneSource == 2;
    const bool useDrawSurfMirrorDynamicFrame = sceneSource == 3;
    const bool enableRigidRouteForMode =
        useDrawSurfMirrorDynamicFrame &&
        r_pathTracingRigidTlasRoute.GetInteger() != 0 &&
        r_pathTracingRigidBlasGpuScaffold.GetInteger() != 0 &&
        r_pathTracingRigidBlasGpuBuild.GetInteger() != 0 &&
        (requestedDebugMode == 23 || requestedDebugMode == 24 || requestedDebugMode == 25 ||
            (requestedDebugMode == 18 && r_pathTracingRigidRouteMode18.GetInteger() != 0) ||
            (requestedDebugMode == 20 && r_pathTracingRigidRouteMode20.GetInteger() != 0));
    const int source2RigidEntities = sceneSource == 2 ? idMath::ClampInt(0, 2, r_pathTracingSceneSource2RigidEntities.GetInteger()) : 0;
    const bool dumpSceneUniverse = r_pathTracingSceneUniverseDump.GetInteger() != 0;
    const bool dumpInstanceUniverse = r_pathTracingInstanceUniverseDump.GetInteger() != 0;
    const bool dumpRigidMeshUniverse = r_pathTracingRigidMeshUniverseDump.GetInteger() != 0;
    const RtSmokeStaticDrawSurfCounts currentStaticDrawSurfs = useSceneUniverseStaticGeometry ? CountCurrentStaticDrawSurfs(viewDef) : RtSmokeStaticDrawSurfCounts();
    if (sceneSource != m_smokeSceneSourceLast || (useSceneUniverseStaticGeometry && source2RigidEntities != m_smokeSceneSource2RigidEntitiesLast))
    {
        common->Printf("PathTracePrimaryPass: PT scene source changed %d/%d -> %d/%d; clearing static geometry cache\n",
            m_smokeSceneSourceLast,
            m_smokeSceneSource2RigidEntitiesLast,
            sceneSource,
            source2RigidEntities);
        m_smokeGeometryUniverse.Clear();
        m_smokeStaticBlasCacheValid = false;
        m_smokeStaticBlasSignature = 0;
        m_smokeSceneUniverseStaticBuildGeneration = 0;
        m_smokeSceneRebuildLogged = false;
        m_smokeSceneSourceLast = sceneSource;
        m_smokeSceneSource2RigidEntitiesLast = source2RigidEntities;
    }
    uint64 sceneUniverseGeneration = 0;
    if (useSceneUniverseStaticGeometry && m_sceneUniverse.EnsureBuilt(viewDef))
    {
        sceneUniverseGeneration = m_sceneUniverse.GetStats().generation;
        if (m_smokeSceneUniverseStaticBuildGeneration != 0 && m_smokeSceneUniverseStaticBuildGeneration != sceneUniverseGeneration)
        {
            common->Printf("PathTracePrimaryPass: PT scene universe generation changed %llu -> %llu; clearing source-2 static geometry cache\n",
                static_cast<unsigned long long>(m_smokeSceneUniverseStaticBuildGeneration),
                static_cast<unsigned long long>(sceneUniverseGeneration));
            m_smokeGeometryUniverse.Clear();
            m_smokeStaticBlasCacheValid = false;
            m_smokeStaticBlasSignature = 0;
            m_smokeSceneUniverseStaticBuildGeneration = 0;
            m_smokeSceneRebuildLogged = false;
        }
    }
    if (useSceneUniverseStaticGeometry && source2RigidEntities != 0)
    {
        m_smokeGeometryUniverse.Clear();
        m_smokeStaticBlasCacheValid = false;
        m_smokeStaticBlasSignature = 0;
        m_smokeSceneUniverseStaticBuildGeneration = 0;
    }
    RtPathTraceSceneUniverseBuildStats sceneUniverseStaticBuildStats;
    {
        OPTICK_EVENT("PT Capture Doom Surfaces");
        m_smokeGeometryUniverse.BeginFrame(++m_smokeGeometryFrameIndex);
        if (useSceneUniverseStaticGeometry)
        {
            OPTICK_EVENT("PT Build Scene Universe Static Geometry");
            RtSmokeSurfaceClassStats sceneUniverseClassStats;
            RtSmokeSurfaceSkipStats sceneUniverseSkipStats;
            RtSmokeAttributeStats sceneUniverseAttributeStats;
            RtSmokeMaterialStats sceneUniverseMaterialStats;
            RtSmokeBucketRanges sceneUniverseBucketRanges;
            sceneUniverseStaticBuildStats = m_sceneUniverse.BuildFullStaticGeometry(viewDef, m_smokeGeometryUniverse, sceneUniverseClassStats, sceneUniverseSkipStats, sceneUniverseAttributeStats, sceneUniverseMaterialStats, sceneUniverseBucketRanges);
            skipStats.invalidIndexCount += sceneUniverseSkipStats.invalidIndexCount;
            skipStats.limitExceeded += sceneUniverseSkipStats.limitExceeded;
            skipStats.zeroAreaOnly += sceneUniverseSkipStats.zeroAreaOnly;
        }
        if (useDrawSurfMirrorDynamicFrame)
        {
            usingDoomSurfaces = CaptureDoomSurfacesForSmokeTest(viewDef, dynamicVertexData, dynamicIndexData, dynamicTriangleClassData, dynamicTriangleMaterialData, m_smokeGeometryUniverse, staticCacheChanged, m_smokeSceneOrigin, sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle, classStats, skipStats, dynamicStats, attributeStats, materialStats, bucketRanges, captureTiming, dumpClassReasons ? &reasonSamples : nullptr, false, false, true);
            if (r_pathTracingStaticAreaPreload.GetInteger() != 0)
            {
                const int staticRecordsBefore = static_cast<int>(m_smokeGeometryUniverse.StaticSurfaceRecords().size());
                const int staticVertsBefore = static_cast<int>(m_smokeGeometryUniverse.StaticVertices().size());
                const RtPathTraceSceneUniverseBuildStats staticAreaPreloadStats = m_sceneUniverse.BuildSelectedStaticGeometry(
                    viewDef,
                    m_smokeGeometryUniverse,
                    classStats,
                    skipStats,
                    attributeStats,
                    materialStats,
                    bucketRanges,
                    idMath::ClampInt(0, 8, r_pathTracingStaticAreaPreloadPortalSteps.GetInteger()),
                    r_pathTracingStaticAreaPreloadDump.GetInteger() != 0);
                if (staticAreaPreloadStats.built)
                {
                    usingDoomSurfaces = true;
                    sourceSurfaces += staticAreaPreloadStats.surfaces;
                    sourceVerts += staticAreaPreloadStats.vertices;
                    sourceIndexes += staticAreaPreloadStats.indexes;
                    if (static_cast<int>(m_smokeGeometryUniverse.StaticSurfaceRecords().size()) != staticRecordsBefore ||
                        static_cast<int>(m_smokeGeometryUniverse.StaticVertices().size()) != staticVertsBefore)
                    {
                        staticCacheChanged = true;
                    }
                }
                if (r_pathTracingStaticAreaPreloadDump.GetInteger() != 0)
                {
                    r_pathTracingStaticAreaPreloadDump.SetInteger(0);
                }
            }
            const RtSmokeSurfaceClassStats staticClassStats = classStats;
            const RtSmokeBucketRange staticBucketRange = bucketRanges.buckets[0];
            const int staticSourceSurfaces = classStats.staticWorldSurfaces;
            const int staticSourceVerts = classStats.staticWorldVerts;
            const int staticSourceIndexes = classStats.staticWorldIndexes;

            RtSmokeSurfaceClassStats mirrorClassStats;
            RtSmokeSurfaceSkipStats mirrorSkipStats;
            RtSmokeDynamicGeometryStats mirrorDynamicStats;
            RtSmokeAttributeStats mirrorAttributeStats;
            RtSmokeMaterialStats mirrorMaterialStats;
            RtSmokeBucketRanges mirrorBucketRanges;
            RtSmokeSceneCaptureTiming mirrorCaptureTiming;
            RtSmokeSurfaceClassReasonSamples mirrorReasonSamples;
            int mirrorSourceSurfaces = 0;
            int mirrorSourceVerts = 0;
            int mirrorSourceIndexes = 0;
            const bool usingMirrorDynamicFrame = CapturePathTraceDynamicFrameFromDrawSurfMirror(viewDef, nullptr, &m_smokeGeometryUniverse, dynamicVertexData, dynamicIndexData, dynamicTriangleClassData, dynamicTriangleMaterialData, mirrorSourceSurfaces, mirrorSourceVerts, mirrorSourceIndexes, mirrorClassStats, mirrorSkipStats, mirrorDynamicStats, mirrorAttributeStats, mirrorMaterialStats, mirrorBucketRanges, mirrorCaptureTiming, dumpClassReasons ? &mirrorReasonSamples : nullptr);

            classStats = RtSmokeSurfaceClassStats();
            classStats.staticWorldSurfaces = staticClassStats.staticWorldSurfaces;
            classStats.staticWorldVerts = staticClassStats.staticWorldVerts;
            classStats.staticWorldIndexes = staticClassStats.staticWorldIndexes;
            classStats.staticWorldTriangles = staticClassStats.staticWorldTriangles;
            classStats.rigidEntitySurfaces = mirrorClassStats.rigidEntitySurfaces;
            classStats.rigidEntityVerts = mirrorClassStats.rigidEntityVerts;
            classStats.rigidEntityIndexes = mirrorClassStats.rigidEntityIndexes;
            classStats.rigidEntityTriangles = mirrorClassStats.rigidEntityTriangles;
            classStats.skinnedDeformedSurfaces = mirrorClassStats.skinnedDeformedSurfaces;
            classStats.skinnedDeformedVerts = mirrorClassStats.skinnedDeformedVerts;
            classStats.skinnedDeformedIndexes = mirrorClassStats.skinnedDeformedIndexes;
            classStats.skinnedDeformedTriangles = mirrorClassStats.skinnedDeformedTriangles;
            classStats.particleAlphaSurfaces = mirrorClassStats.particleAlphaSurfaces;
            classStats.particleAlphaVerts = mirrorClassStats.particleAlphaVerts;
            classStats.particleAlphaIndexes = mirrorClassStats.particleAlphaIndexes;
            classStats.particleAlphaTriangles = mirrorClassStats.particleAlphaTriangles;
            classStats.unknownSurfaces = mirrorClassStats.unknownSurfaces;
            classStats.unknownVerts = mirrorClassStats.unknownVerts;
            classStats.unknownIndexes = mirrorClassStats.unknownIndexes;
            classStats.unknownTriangles = mirrorClassStats.unknownTriangles;
            skipStats = mirrorSkipStats;
            dynamicStats = mirrorDynamicStats;
            attributeStats = mirrorAttributeStats;
            materialStats = mirrorMaterialStats;
            bucketRanges = mirrorBucketRanges;
            bucketRanges.buckets[0] = staticBucketRange;
            captureTiming.dynamicPassClassifyMs += mirrorCaptureTiming.dynamicPassClassifyMs;
            captureTiming.dynamicAppendMs += mirrorCaptureTiming.dynamicAppendMs;
            captureTiming.rtCpuSkinningAppendMs += mirrorCaptureTiming.rtCpuSkinningAppendMs;
            captureTiming.bucketMergeMs += mirrorCaptureTiming.bucketMergeMs;
            captureTiming.appendMs += mirrorCaptureTiming.appendMs;
            captureTiming.validationMs += mirrorCaptureTiming.validationMs;
            sourceSurfaces = staticSourceSurfaces + mirrorSourceSurfaces;
            sourceVerts = staticSourceVerts + mirrorSourceVerts;
            sourceIndexes = staticSourceIndexes + mirrorSourceIndexes;
            usingDoomSurfaces = usingDoomSurfaces || usingMirrorDynamicFrame;
        }
        else
        {
            usingDoomSurfaces = CaptureDoomSurfacesForSmokeTest(viewDef, dynamicVertexData, dynamicIndexData, dynamicTriangleClassData, dynamicTriangleMaterialData, m_smokeGeometryUniverse, staticCacheChanged, m_smokeSceneOrigin, sourceSurfaces, sourceVerts, sourceIndexes, anchorTriangle, classStats, skipStats, dynamicStats, attributeStats, materialStats, bucketRanges, captureTiming, dumpClassReasons ? &reasonSamples : nullptr, useSceneUniverseStaticGeometry, source2RigidEntities != 0);
        }
        {
            OPTICK_EVENT("PT DrawSurf Mirror");
            m_instanceUniverse.BeginFrame(m_smokeGeometryFrameIndex, viewDef);
            CapturePathTraceDrawSurfMirror(viewDef, useSceneUniverseStaticGeometry ? &m_sceneUniverse : nullptr, &m_smokeGeometryUniverse, m_instanceUniverse, &m_smokeBoundsOverlayLines);
            m_smokeBoundsOverlayLineCount = static_cast<int>(m_smokeBoundsOverlayLines.size());
        }
        if (useSceneUniverseStaticGeometry)
        {
            if (sceneUniverseStaticBuildStats.built)
            {
                staticCacheChanged = staticCacheChanged || !sceneUniverseStaticBuildStats.cacheHit;
                sourceSurfaces += sceneUniverseStaticBuildStats.surfaces;
                sourceVerts += sceneUniverseStaticBuildStats.vertices;
                sourceIndexes += sceneUniverseStaticBuildStats.indexes;
                const int staticSceneUniverseSurfaces = Max(0, sceneUniverseStaticBuildStats.surfaces - sceneUniverseStaticBuildStats.rigidEntitySurfaces);
                const int staticSceneUniverseTriangles = Max(0, sceneUniverseStaticBuildStats.triangles - sceneUniverseStaticBuildStats.rigidEntityTriangles);
                classStats.staticWorldSurfaces += staticSceneUniverseSurfaces;
                classStats.staticWorldIndexes += staticSceneUniverseTriangles * 3;
                classStats.staticWorldTriangles += staticSceneUniverseTriangles;
                classStats.rigidEntitySurfaces += sceneUniverseStaticBuildStats.rigidEntitySurfaces;
                classStats.rigidEntityIndexes += sceneUniverseStaticBuildStats.rigidEntityTriangles * 3;
                classStats.rigidEntityTriangles += sceneUniverseStaticBuildStats.rigidEntityTriangles;
                bucketRanges.buckets[0].surfaceCount += sceneUniverseStaticBuildStats.surfaces;
                usingDoomSurfaces = true;
                sceneUniverseGeneration = m_sceneUniverse.GetStats().generation;
                m_smokeSceneUniverseStaticBuildGeneration = sceneUniverseGeneration;
            }
        }
        m_smokeGeometryUniverse.EndFrame();
    }
    if (useDrawSurfMirrorDynamicFrame && r_pathTracingSceneSourceCompare.GetInteger() != 0)
    {
        DumpSource3CaptureCompare(
            viewDef,
            m_smokeGeometryFrameIndex,
            classStats,
            skipStats,
            sourceSurfaces,
            sourceVerts,
            sourceIndexes,
            dynamicIndexData,
            dynamicTriangleMaterialData);
        r_pathTracingSceneSourceCompare.SetInteger(0);
    }
    if (useDrawSurfMirrorDynamicFrame && r_pathTracingRigidMeshValidate.GetInteger() != 0)
    {
        const RtPathTraceRigidMeshValidationStats rigidMeshValidationStats =
            m_smokeGeometryUniverse.ValidateRigidMeshCandidatesAgainstDynamicPayload(
                dynamicTriangleClassData,
                dynamicTriangleMaterialData,
                RT_SMOKE_TRIANGLE_CLASS_MASK,
                SmokeSurfaceClassId(RtSmokeSurfaceClass::RigidEntity));
        m_smokeGeometryUniverse.DumpRigidMeshValidationStats(rigidMeshValidationStats, sceneSource);
        r_pathTracingRigidMeshValidate.SetInteger(0);
    }
    if (useDrawSurfMirrorDynamicFrame && r_pathTracingRigidBlasPlanDump.GetInteger() != 0)
    {
        const RtPathTraceRigidBlasPlanStats rigidBlasPlanStats = m_smokeGeometryUniverse.BuildRigidBlasPlanStats(&classStats);
        m_smokeGeometryUniverse.DumpRigidBlasPlanStats(rigidBlasPlanStats, sceneSource);
        r_pathTracingRigidBlasPlanDump.SetInteger(0);
    }
    if (useDrawSurfMirrorDynamicFrame && r_pathTracingRigidBlasInputDump.GetInteger() != 0)
    {
        const RtPathTraceRigidBlasInputStats rigidBlasInputStats = m_smokeGeometryUniverse.BuildRigidBlasInputStats();
        m_smokeGeometryUniverse.DumpRigidBlasInputStats(rigidBlasInputStats, sceneSource);
        r_pathTracingRigidBlasInputDump.SetInteger(0);
    }
    if (useDrawSurfMirrorDynamicFrame)
    {
        const bool rigidBlasGpuScaffold = r_pathTracingRigidBlasGpuScaffold.GetInteger() != 0;
        const bool rigidBlasGpuBuild = rigidBlasGpuScaffold && r_pathTracingRigidBlasGpuBuild.GetInteger() != 0;
        const bool dumpRigidBlasGpu = r_pathTracingRigidBlasGpuDump.GetInteger() != 0;
        if (rigidBlasGpuScaffold)
        {
            const RtPathTraceRigidBlasGpuStats rigidBlasGpuStats = m_smokeGeometryUniverse.UpdateRigidBlasGpuScaffold(device, commandList, rigidBlasGpuBuild);
            if (dumpRigidBlasGpu)
            {
                m_smokeGeometryUniverse.DumpRigidBlasGpuStats(rigidBlasGpuStats, sceneSource, true, rigidBlasGpuBuild);
                r_pathTracingRigidBlasGpuDump.SetInteger(0);
            }
        }
        else
        {
            m_smokeGeometryUniverse.ReleaseRigidBlasGpuScaffold();
            if (dumpRigidBlasGpu)
            {
                RtPathTraceRigidBlasGpuStats rigidBlasGpuStats;
                rigidBlasGpuStats.frameIndex = m_smokeGeometryFrameIndex;
                m_smokeGeometryUniverse.DumpRigidBlasGpuStats(rigidBlasGpuStats, sceneSource, false, false);
                r_pathTracingRigidBlasGpuDump.SetInteger(0);
            }
        }
    }
    if (useDrawSurfMirrorDynamicFrame)
    {
        const bool rigidResidencyBoundsDebug = requestedDebugMode == 21 || requestedDebugMode == 22;
        const bool rigidResidencyEnabled =
            r_pathTracingRigidResidency.GetInteger() != 0 &&
            (enableRigidRouteForMode || rigidResidencyBoundsDebug);
        const RtPathTraceRigidResidencyStats rigidResidencyStats = m_smokeGeometryUniverse.UpdateRigidResidency(
            viewDef,
            m_instanceUniverse,
            rigidResidencyEnabled,
            idMath::ClampInt(0, 8, r_pathTracingRigidResidencyPortalSteps.GetInteger()));
        const int boundsOverlayMode = r_pathTracingSceneBoundsOverlay.GetInteger();
        const bool appendRigidResidencyBounds =
            rigidResidencyEnabled &&
            (boundsOverlayMode == 3 || boundsOverlayMode == 5 || (rigidResidencyBoundsDebug && boundsOverlayMode < 3));
        const bool appendStaticCacheBounds =
            boundsOverlayMode == 4 || boundsOverlayMode == 5 || boundsOverlayMode == 6;
        if ((appendRigidResidencyBounds || appendStaticCacheBounds) &&
            (rigidResidencyBoundsDebug || r_pathTracingSceneBoundsOverlay.GetInteger() != 0) &&
            static_cast<int>(m_smokeBoundsOverlayLines.size()) < RT_PT_BOUNDS_OVERLAY_MAX_LINES)
        {
            const int remainingLines = RT_PT_BOUNDS_OVERLAY_MAX_LINES - static_cast<int>(m_smokeBoundsOverlayLines.size());
            const int requestedResidentBoxes = Max(0, r_pathTracingSceneBoundsOverlayMax.GetInteger());
            int remainingBoxes = Min(Min(requestedResidentBoxes, RT_PT_RESIDENT_BOUNDS_OVERLAY_SAFE_BOXES), remainingLines / 12);
            if (appendStaticCacheBounds && remainingBoxes > 0)
            {
                std::vector<RtPathTraceRigidResidencyBoundsBox> staticBoundsBoxes;
                staticBoundsBoxes.reserve(remainingBoxes);
                m_smokeGeometryUniverse.CollectStaticSurfaceBoundsBoxes(staticBoundsBoxes, remainingBoxes, boundsOverlayMode == 6);
                AppendRigidResidencyBoundsOverlayLines(staticBoundsBoxes, m_smokeBoundsOverlayLines);
                remainingBoxes -= static_cast<int>(staticBoundsBoxes.size());
            }
            if (appendRigidResidencyBounds && remainingBoxes > 0)
            {
                std::vector<RtPathTraceRigidResidencyBoundsBox> residentBoundsBoxes;
                residentBoundsBoxes.reserve(remainingBoxes);
                m_smokeGeometryUniverse.CollectRigidResidencyBoundsBoxes(residentBoundsBoxes, remainingBoxes);
                AppendRigidResidencyBoundsOverlayLines(residentBoundsBoxes, m_smokeBoundsOverlayLines);
            }
            m_smokeBoundsOverlayLineCount = static_cast<int>(m_smokeBoundsOverlayLines.size());
        }
        if (r_pathTracingRigidResidencyDump.GetInteger() != 0)
        {
            m_smokeGeometryUniverse.DumpRigidResidencyStats(rigidResidencyStats, sceneSource);
            r_pathTracingRigidResidencyDump.SetInteger(0);
        }
    }
    if (useDrawSurfMirrorDynamicFrame && r_pathTracingRigidTlasPlanDump.GetInteger() != 0)
    {
        const RtPathTraceRigidTlasPlanStats rigidTlasPlanStats = m_smokeGeometryUniverse.BuildRigidTlasPlanStats(m_instanceUniverse, &classStats);
        m_smokeGeometryUniverse.DumpRigidTlasPlanStats(rigidTlasPlanStats, sceneSource);
        r_pathTracingRigidTlasPlanDump.SetInteger(0);
    }
    const int captureMs = Sys_Milliseconds() - captureStartMs;
    if (dumpInstanceUniverse || r_pathTracingSmokeLog.GetInteger() != 0)
    {
        RtPathTraceInstanceUniverseDiagnosticDesc instanceDiagnosticDesc;
        instanceDiagnosticDesc.dumpRequested = dumpInstanceUniverse;
        instanceDiagnosticDesc.sceneSource = sceneSource;
        instanceDiagnosticDesc.legacySourceSurfaces = sourceSurfaces;
        instanceDiagnosticDesc.legacyClassStats = &classStats;
        instanceDiagnosticDesc.legacySkipStats = &skipStats;
        m_instanceUniverse.RunDiagnostics(instanceDiagnosticDesc);
    }
    if (dumpRigidMeshUniverse || r_pathTracingSmokeLog.GetInteger() != 0)
    {
        m_smokeGeometryUniverse.RunRigidMeshCandidateDiagnostics(dumpRigidMeshUniverse, sceneSource, &classStats);
    }
    if (sceneSource > 0 || dumpSceneUniverse)
    {
        const int drawSurfStaticSurfaces = useSceneUniverseStaticGeometry ? currentStaticDrawSurfs.surfaces : classStats.staticWorldSurfaces;
        const int drawSurfStaticTriangles = useSceneUniverseStaticGeometry ? currentStaticDrawSurfs.triangles : classStats.staticWorldTriangles;
        m_sceneUniverse.RunDiagnostics(viewDef, &m_smokeGeometryUniverse, sceneSource, dumpSceneUniverse, drawSurfStaticSurfaces, drawSurfStaticTriangles);
        if (dumpSceneUniverse && useSceneUniverseStaticGeometry)
        {
            common->Printf("PathTracePrimaryPass: PT scene source2 staticBuild built=%d cacheHit=%d surfaces=%d triangles=%d verts=%d indexes=%d emissiveSurfaces=%d rigidEntities=%d/%d skipped invalid/limits/zero=%d/%d/%d sourceTotals=%d/%d/%d\n",
                sceneUniverseStaticBuildStats.built ? 1 : 0,
                sceneUniverseStaticBuildStats.cacheHit ? 1 : 0,
                sceneUniverseStaticBuildStats.surfaces,
                sceneUniverseStaticBuildStats.triangles,
                sceneUniverseStaticBuildStats.vertices,
                sceneUniverseStaticBuildStats.indexes,
                sceneUniverseStaticBuildStats.emissiveCapableSurfaces,
                sceneUniverseStaticBuildStats.rigidEntitySurfaces,
                sceneUniverseStaticBuildStats.rigidEntityTriangles,
                sceneUniverseStaticBuildStats.skippedInvalid,
                sceneUniverseStaticBuildStats.skippedLimits,
                sceneUniverseStaticBuildStats.skippedZeroArea,
                sourceSurfaces,
                sourceVerts,
                sourceIndexes);
        }
    }
    if (!usingDoomSurfaces)
    {
        if (!m_smokeWaitingForDoomSurfaceLogged)
        {
            common->Printf("PathTracePrimaryPass: waiting for center camera ray Doom surface hit to build RT smoke BLAS\n");
            m_smokeWaitingForDoomSurfaceLogged = true;
        }
        return;
    }

    RtSmokeMaterialMetadataRegistrationTiming metadataTiming;
    {
        OPTICK_EVENT("PT Register Material Metadata");
        metadataTiming = RegisterSmokeMaterialTextureInfoForFrame(viewDef, enableTextureProbe);
        if (r_pathTracingWorldStaticEmissives.GetInteger() != 0 || useSceneUniverseStaticGeometry)
        {
            const RtSmokeMaterialMetadataRegistrationTiming worldStaticMetadataTiming = RegisterSmokeWorldStaticMaterialTextureInfo(viewDef, enableTextureProbe);
            metadataTiming.metadataMs += worldStaticMetadataTiming.metadataMs;
            metadataTiming.registrationMs += worldStaticMetadataTiming.registrationMs;
        }
    }
    const int metadataMs = metadataTiming.metadataMs;
    const int metadataValidationMs = metadataTiming.validationMs;
    const int metadataRegistrationMs = metadataTiming.registrationMs;

    const int materialStartMs = Sys_Milliseconds();
    RtSmokeMaterialTableBuild materialTable;
    const int rigidRouteMaxInstances = idMath::ClampInt(1, 512, r_pathTracingRigidRouteMaxInstances.GetInteger());
    std::vector<uint32_t> fullLevelStaticEmissiveMaterialIds;
    if (r_pathTracingWorldStaticEmissives.GetInteger() != 0)
    {
        fullLevelStaticEmissiveMaterialIds = BuildSmokeWorldStaticEmissiveMaterialIds(viewDef);
    }
    std::vector<uint32_t> materialTableStaticIds = staticTriangleMaterialCache;
    materialTableStaticIds.insert(materialTableStaticIds.end(), fullLevelStaticEmissiveMaterialIds.begin(), fullLevelStaticEmissiveMaterialIds.end());
    if (enableRigidRouteForMode)
    {
        const std::vector<uint32_t> rigidRouteMaterialIds = m_smokeGeometryUniverse.CollectRigidRouteMaterialIds(m_instanceUniverse, rigidRouteMaxInstances);
        materialTableStaticIds.insert(materialTableStaticIds.end(), rigidRouteMaterialIds.begin(), rigidRouteMaterialIds.end());
    }
    uint64 materialTableSignature = 0;
    bool materialTableCacheHit = false;
    RtSmokeMaterialTableCompareStats materialUniverseTableCompareStats;
    const bool useMaterialUniverseTable = r_pathTracingMaterialUniverseTable.GetInteger() != 0;
    const bool validateMaterialUniverseTable = r_pathTracingMaterialUniverseTableValidate.GetInteger() != 0;
    const char* materialTablePath = useMaterialUniverseTable ? "universe" : "legacy";
    BeginSmokeMaterialUniverseFrame();
    if (useMaterialUniverseTable)
    {
        if (validateMaterialUniverseTable)
        {
            RtSmokeMaterialTableBuild legacyMaterialTable;
            uint32_t legacyLatchedTextureProbeMaterialId = m_smokeTextureProbeMaterialId;
            int legacyLatchedTextureProbeRequestedIndex = m_smokeTextureProbeRequestedIndex;
            BuildSmokeMaterialTableCached(legacyMaterialTable, materialTableStaticIds, dynamicTriangleMaterialData, legacyLatchedTextureProbeMaterialId, legacyLatchedTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
            BuildSmokeMaterialTableFromUniverseCached(materialTable, materialTableStaticIds, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
            materialUniverseTableCompareStats = CompareSmokeMaterialTables(legacyMaterialTable, materialTable);
            if (materialUniverseTableCompareStats.mismatches > 0)
            {
                common->Printf("PathTracePrimaryPass: RT smoke material universe table mismatch, falling back to legacy table for this frame (mismatches=%d material=%d/%d/%d indexes=%d/%d textures=%d/%d)\n",
                    materialUniverseTableCompareStats.mismatches,
                    materialUniverseTableCompareStats.materialCountMismatches,
                    materialUniverseTableCompareStats.materialIdMismatches,
                    materialUniverseTableCompareStats.materialRecordMismatches,
                    materialUniverseTableCompareStats.staticIndexMismatches,
                    materialUniverseTableCompareStats.dynamicIndexMismatches,
                    materialUniverseTableCompareStats.textureCountMismatches,
                    materialUniverseTableCompareStats.textureHandleMismatches);
                materialTable = legacyMaterialTable;
                m_smokeTextureProbeMaterialId = legacyLatchedTextureProbeMaterialId;
                m_smokeTextureProbeRequestedIndex = legacyLatchedTextureProbeRequestedIndex;
                materialTablePath = "legacyFallback";
            }
        }
        else
        {
            BuildSmokeMaterialTableFromUniverseCached(materialTable, materialTableStaticIds, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
        }
    }
    else
    {
        BuildSmokeMaterialTableCached(materialTable, materialTableStaticIds, dynamicTriangleMaterialData, m_smokeTextureProbeMaterialId, m_smokeTextureProbeRequestedIndex, enableTextureProbe, materialTableSignature, materialTableCacheHit);
        if (validateMaterialUniverseTable)
        {
            RtSmokeMaterialTableBuild universeMaterialTable;
            uint32_t universeLatchedTextureProbeMaterialId = m_smokeTextureProbeMaterialId;
            int universeLatchedTextureProbeRequestedIndex = m_smokeTextureProbeRequestedIndex;
            BuildSmokeMaterialTableFromUniverse(universeMaterialTable, materialTableStaticIds, dynamicTriangleMaterialData, universeLatchedTextureProbeMaterialId, universeLatchedTextureProbeRequestedIndex, enableTextureProbe);
            materialUniverseTableCompareStats = CompareSmokeMaterialTables(materialTable, universeMaterialTable);
        }
    }
    const RtSmokeMaterialTableCacheStats materialTableCacheStats = GetSmokeMaterialTableCacheStats();
    const RtSmokeMaterialTableBuildStats materialTableBuildStats = GetSmokeMaterialTableBuildStats();
    const RtSmokeMaterialUniverseStats materialUniverseStats = GetSmokeMaterialUniverseStats();
    if (!ValidateSmokeMaterialIndexes(materialTable))
    {
        common->Printf("PathTracePrimaryPass: invalid RT smoke material table, skipping scene build\n");
        return;
    }
    RtSmokeTextureCoverageStats textureCoverageStats;
    const bool needTextureCoverageStats = enableTextureProbe && r_pathTracingSmokeLog.GetInteger() != 0;
    if (needTextureCoverageStats)
    {
        textureCoverageStats = BuildSmokeTextureCoverageStats(
            materialTable,
            staticTriangleClassCache,
            materialTable.staticMaterialIndexes,
            dynamicTriangleClassData,
            materialTable.dynamicMaterialIndexes);
    }
    const int materialMs = Sys_Milliseconds() - materialStartMs;
    RtSmokeMaterialDiagnosticTriggerDesc materialDiagnosticDesc;
    materialDiagnosticDesc.viewDef = viewDef;
    materialDiagnosticDesc.materialTable = &materialTable;
    materialDiagnosticDesc.materialStats = &materialStats;
    materialDiagnosticDesc.enableTextureProbe = enableTextureProbe;
    RunSmokeMaterialDiagnosticTriggers(materialDiagnosticDesc);

    const bool buildRigidRouteBuffers = enableRigidRouteForMode;
    RtPathTraceRigidRouteBuild rigidRouteBuild;
    if (buildRigidRouteBuffers)
    {
        rigidRouteBuild = m_smokeGeometryUniverse.BuildRigidRouteBuffers(m_instanceUniverse, materialTable.materialIds, rigidRouteMaxInstances);
        if (r_pathTracingSmokeLog.GetInteger() != 0 && (m_smokeGeometryFrameIndex % 120ull) == 1ull)
        {
            common->Printf("PathTracePrimaryPass: PT rigid route buffers instances=%d max=%d seen/cache=%d/%d verts/indexes/tris=%d/%d/%d skipped nonRigid/missingMesh/missingBlas=%d/%d/%d missingMaterialIndex=%d\n",
                rigidRouteBuild.stats.emittedInstances,
                rigidRouteMaxInstances,
                rigidRouteBuild.stats.emittedSeenThisFrame,
                rigidRouteBuild.stats.emittedFromCache,
                rigidRouteBuild.stats.vertices,
                rigidRouteBuild.stats.indexes,
                rigidRouteBuild.stats.triangles,
                rigidRouteBuild.stats.skippedNonRigid,
                rigidRouteBuild.stats.skippedMissingMesh,
                rigidRouteBuild.stats.skippedMissingBlas,
                rigidRouteBuild.stats.missingMaterialTableIndex);
        }
    }

    RtSmokeEmissiveInventoryStats emissiveInventoryStats;
    const int emissiveStartMs = Sys_Milliseconds();
    std::vector<PathTraceSmokeEmissiveTriangle> emissiveTriangles;
    std::vector<PathTraceSmokeLightCandidate> lightCandidates;
    const int maxEmissiveRecords = idMath::ClampInt(1, RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS, r_pathTracingEmissiveInventoryMaxTriangles.GetInteger());
    {
        OPTICK_EVENT("PT Emissive Inventory");
        emissiveTriangles = BuildSmokeEmissiveTriangleInventory(
            materialTable.materialIds,
            materialTable.materials,
            staticVertexCache,
            staticIndexCache,
            staticTriangleClassCache,
            materialTable.staticMaterialIndexes,
            dynamicVertexData,
            dynamicIndexData,
            dynamicTriangleClassData,
            materialTable.dynamicMaterialIndexes,
            RT_SMOKE_MATERIAL_EMISSIVE,
            RT_SMOKE_TRIANGLE_CLASS_MASK,
            static_cast<uint32_t>(RtSmokeSurfaceClass::SkinnedDeformed),
            maxEmissiveRecords,
            emissiveInventoryStats);
        if (enableRigidRouteForMode && requestedDebugMode == 20)
        {
            AppendSmokeRigidRouteEmissiveTriangleInventory(
                materialTable.materialIds,
                materialTable.materials,
                rigidRouteBuild,
                RT_SMOKE_MATERIAL_EMISSIVE,
                maxEmissiveRecords,
                emissiveTriangles,
                emissiveInventoryStats);
        }
        if (r_pathTracingWorldStaticEmissives.GetInteger() != 0)
        {
            const int fullLevelStaticSupplementCap = idMath::ClampInt(0, maxEmissiveRecords, r_pathTracingWorldStaticEmissiveMaxTriangles.GetInteger());
            const int fullLevelStaticSupplementLimit = Min(maxEmissiveRecords, static_cast<int>(emissiveTriangles.size()) + fullLevelStaticSupplementCap);
            AppendSmokeWorldStaticEmissiveTriangleInventory(
                viewDef,
                materialTable.materialIds,
                materialTable.materials,
                RT_SMOKE_MATERIAL_EMISSIVE,
                SmokeSurfaceClassId(RtSmokeSurfaceClass::StaticWorld),
                fullLevelStaticSupplementLimit,
                emissiveTriangles,
                emissiveInventoryStats);
        }
        const int fullLevelStaticEmissiveTriangles = emissiveInventoryStats.fullLevelStaticTriangles;
        const int routedRigidEmissiveTriangles = emissiveInventoryStats.routedRigidTriangles;
        const int routedRigidInstances = emissiveInventoryStats.routedRigidInstances;
        const int routedRigidSeenInstances = emissiveInventoryStats.routedRigidSeenInstances;
        const int routedRigidCacheInstances = emissiveInventoryStats.routedRigidCacheInstances;
        const int routedRigidEmissiveInstances = emissiveInventoryStats.routedRigidEmissiveInstances;
        const int routedRigidEmissiveSeenInstances = emissiveInventoryStats.routedRigidEmissiveSeenInstances;
        const int routedRigidEmissiveCacheInstances = emissiveInventoryStats.routedRigidEmissiveCacheInstances;
        const int routedRigidCapturedTriangles = emissiveInventoryStats.routedRigidCapturedTriangles;
        const int routedRigidCappedTriangles = emissiveInventoryStats.routedRigidCappedTriangles;
        const int routedRigidInvalidTriangles = emissiveInventoryStats.routedRigidInvalidTriangles;
        const int routedRigidNonEmissiveTriangles = emissiveInventoryStats.routedRigidNonEmissiveTriangles;
        const float routedRigidArea = emissiveInventoryStats.routedRigidArea;
        const float routedRigidWeightedLuminance = emissiveInventoryStats.routedRigidWeightedLuminance;
        const int runtimeInactiveEmissiveTrianglesBeforeStatsRebuild = emissiveInventoryStats.skippedRuntimeInactiveTriangles;
        FinalizeSmokeEmissiveTriangleSamplingFields(emissiveTriangles, emissiveInventoryStats);
        emissiveInventoryStats = BuildSmokeEmissiveInventoryStatsForRecords(materialTable.materialIds, emissiveTriangles);
        emissiveInventoryStats.fullLevelStaticTriangles = fullLevelStaticEmissiveTriangles;
        emissiveInventoryStats.routedRigidTriangles = routedRigidEmissiveTriangles;
        emissiveInventoryStats.routedRigidInstances = routedRigidInstances;
        emissiveInventoryStats.routedRigidSeenInstances = routedRigidSeenInstances;
        emissiveInventoryStats.routedRigidCacheInstances = routedRigidCacheInstances;
        emissiveInventoryStats.routedRigidEmissiveInstances = routedRigidEmissiveInstances;
        emissiveInventoryStats.routedRigidEmissiveSeenInstances = routedRigidEmissiveSeenInstances;
        emissiveInventoryStats.routedRigidEmissiveCacheInstances = routedRigidEmissiveCacheInstances;
        emissiveInventoryStats.routedRigidCapturedTriangles = routedRigidCapturedTriangles;
        emissiveInventoryStats.routedRigidCappedTriangles = routedRigidCappedTriangles;
        emissiveInventoryStats.routedRigidInvalidTriangles = routedRigidInvalidTriangles;
        emissiveInventoryStats.routedRigidNonEmissiveTriangles = routedRigidNonEmissiveTriangles;
        emissiveInventoryStats.routedRigidArea = routedRigidArea;
        emissiveInventoryStats.routedRigidWeightedLuminance = routedRigidWeightedLuminance;
        emissiveInventoryStats.skippedRuntimeInactiveTriangles = runtimeInactiveEmissiveTrianglesBeforeStatsRebuild;
        lightCandidates = BuildSmokeLightCandidateBufferRecords(emissiveInventoryStats);
    }
    const int emissiveMs = Sys_Milliseconds() - emissiveStartMs;
    RtSmokeEmissiveInventoryDiagnosticTriggerDesc emissiveInventoryDiagnosticDesc;
    emissiveInventoryDiagnosticDesc.materialTable = &materialTable;
    emissiveInventoryDiagnosticDesc.emissiveTriangles = &emissiveTriangles;
    emissiveInventoryDiagnosticDesc.emissiveInventoryStats = &emissiveInventoryStats;
    RunSmokeEmissiveInventoryDiagnosticTriggers(emissiveInventoryDiagnosticDesc);
    const int runtimeInactiveEmissiveTriangles = emissiveInventoryStats.skippedRuntimeInactiveTriangles;
    {
        OPTICK_EVENT("PT Light Universe");
        emissiveTriangles = m_smokeLightUniverse.MergeFrameCandidates(
            viewDef,
            emissiveTriangles,
            maxEmissiveRecords,
            idMath::ClampInt(0, 8, r_pathTracingLightAreaPortalSteps.GetInteger()),
            r_pathTracingLightAreaFilter.GetInteger() != 0,
            r_pathTracingLightAreaFilterApply.GetInteger() != 0,
            idMath::ClampInt(0, maxEmissiveRecords, r_pathTracingLightAreaOverflowMax.GetInteger()),
            r_pathTracingLightUniverseChurn.GetInteger() != 0,
            r_pathTracingLightUniversePersistDynamic.GetInteger() != 0,
            r_pathTracingLightUniverseInjectMissingDynamic.GetInteger() != 0,
            idMath::ClampInt(1, 120, r_pathTracingLightUniverseDynamicMinSeenFrames.GetInteger()),
            idMath::ClampInt(1, 3600, r_pathTracingLightUniverseDynamicMaxMissingFrames.GetInteger()));
        emissiveInventoryStats = BuildSmokeEmissiveInventoryStatsForRecords(materialTable.materialIds, emissiveTriangles);
        lightCandidates = BuildSmokeLightCandidateBufferRecords(emissiveInventoryStats);
    }
    const RtSmokeLightUniverseStats lightUniverseStats = m_smokeLightUniverse.GetStats();
    if (r_pathTracingLightUniverseDump.GetInteger() != 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke light universe static=%d seen=%d new=%d updated=%d missing=%d semiStatic=%d dynSeen=%d dynPromoted=%d dynUpdated=%d dynMissing=%d dynAged=%d dynamicFrame=%d merged=%d area current/total=%d/%d selected steps/areas/edges/blocked=%d/%d/%d/%d staticKnown/unknown=%d/%d dynamicKnown/unknown=%d/%d mergedKnown/unknown=%d/%d mergedCurrent/selected/connected/disconnected=%d/%d/%d/%d connectedUnselected=%d portalSweep areas=%d/%d/%d/%d/%d merged=%d/%d/%d/%d/%d portalDepthBins depth0/1/2/3/4/>4/disconnected/unknown=%d/%d/%d/%d/%d/%d/%d/%d areaFilter enabled/applied=%d/%d steps=%d overflowMax=%d selected=%d connectedOverflow=%d disconnected=%d unknown=%d wouldUpload=%d wouldDrop=%d area %.2f/%.2f drop=%.2f weight %.3f/%.3f drop=%.3f dropWeight overflow/disconnected/unknown=%.3f/%.3f/%.3f persistDynamic=%d injectMissingDynamic=%d minSeen=%d maxMissing=%d generation=%llu\n",
            lightUniverseStats.persistentStaticTriangles,
            lightUniverseStats.staticSeenThisFrame,
            lightUniverseStats.staticNewThisFrame,
            lightUniverseStats.staticUpdatedThisFrame,
            lightUniverseStats.staticMissingThisFrame,
            lightUniverseStats.persistentDynamicTriangles,
            lightUniverseStats.dynamicSeenThisFrame,
            lightUniverseStats.dynamicPromotedThisFrame,
            lightUniverseStats.dynamicUpdatedThisFrame,
            lightUniverseStats.dynamicMissingThisFrame,
            lightUniverseStats.dynamicAgedOutThisFrame,
            lightUniverseStats.dynamicFrameTriangles,
            lightUniverseStats.mergedTriangles,
            lightUniverseStats.currentArea,
            lightUniverseStats.totalAreas,
            lightUniverseStats.selectedPortalSteps,
            lightUniverseStats.selectedAreaCount,
            lightUniverseStats.selectedPortalEdges,
            lightUniverseStats.selectedBlockedPortalEdges,
            lightUniverseStats.staticAreaKnownTriangles,
            lightUniverseStats.staticAreaUnknownTriangles,
            lightUniverseStats.dynamicAreaKnownTriangles,
            lightUniverseStats.dynamicAreaUnknownTriangles,
            lightUniverseStats.mergedAreaKnownTriangles,
            lightUniverseStats.mergedAreaUnknownTriangles,
            lightUniverseStats.mergedCurrentAreaTriangles,
            lightUniverseStats.mergedSelectedAreaTriangles,
            lightUniverseStats.mergedConnectedAreaTriangles,
            lightUniverseStats.mergedDisconnectedAreaTriangles,
            lightUniverseStats.mergedConnectedUnselectedAreaTriangles,
            lightUniverseStats.portalStepSelectedAreas[0],
            lightUniverseStats.portalStepSelectedAreas[1],
            lightUniverseStats.portalStepSelectedAreas[2],
            lightUniverseStats.portalStepSelectedAreas[3],
            lightUniverseStats.portalStepSelectedAreas[4],
            lightUniverseStats.portalStepMergedSelectedTriangles[0],
            lightUniverseStats.portalStepMergedSelectedTriangles[1],
            lightUniverseStats.portalStepMergedSelectedTriangles[2],
            lightUniverseStats.portalStepMergedSelectedTriangles[3],
            lightUniverseStats.portalStepMergedSelectedTriangles[4],
            lightUniverseStats.mergedPortalDepthBins[0],
            lightUniverseStats.mergedPortalDepthBins[1],
            lightUniverseStats.mergedPortalDepthBins[2],
            lightUniverseStats.mergedPortalDepthBins[3],
            lightUniverseStats.mergedPortalDepthBins[4],
            lightUniverseStats.mergedPortalDepthBins[5],
            lightUniverseStats.mergedPortalDepthBins[6],
            lightUniverseStats.mergedPortalDepthBins[7],
            lightUniverseStats.areaFilterEnabled,
            lightUniverseStats.areaFilterApplied,
            lightUniverseStats.areaFilterPortalSteps,
            lightUniverseStats.areaFilterOverflowMax,
            lightUniverseStats.areaFilterSelectedCandidates,
            lightUniverseStats.areaFilterConnectedOverflowCandidates,
            lightUniverseStats.areaFilterDisconnectedCandidates,
            lightUniverseStats.areaFilterUnknownCandidates,
            lightUniverseStats.areaFilterWouldUploadCandidates,
            lightUniverseStats.areaFilterWouldDropCandidates,
            lightUniverseStats.areaFilterPreArea,
            lightUniverseStats.areaFilterPostArea,
            lightUniverseStats.areaFilterDroppedArea,
            lightUniverseStats.areaFilterPreWeight,
            lightUniverseStats.areaFilterPostWeight,
            lightUniverseStats.areaFilterDroppedWeight,
            lightUniverseStats.areaFilterDroppedOverflowWeight,
            lightUniverseStats.areaFilterDroppedDisconnectedWeight,
            lightUniverseStats.areaFilterDroppedUnknownWeight,
            r_pathTracingLightUniversePersistDynamic.GetInteger() != 0 ? 1 : 0,
            r_pathTracingLightUniverseInjectMissingDynamic.GetInteger() != 0 ? 1 : 0,
            idMath::ClampInt(1, 120, r_pathTracingLightUniverseDynamicMinSeenFrames.GetInteger()),
            idMath::ClampInt(1, 3600, r_pathTracingLightUniverseDynamicMaxMissingFrames.GetInteger()),
            static_cast<unsigned long long>(lightUniverseStats.generation));
        if (lightUniverseStats.overflowSampleCount > 0)
        {
            for (int sampleIndex = 0; sampleIndex < lightUniverseStats.overflowSampleCount; ++sampleIndex)
            {
                const RtSmokeLightUniverseCandidateSample& sample = lightUniverseStats.overflowSamples[sampleIndex];
                if (!sample.valid)
                {
                    continue;
                }
                common->Printf("PathTracePrimaryPass: RT smoke light area overflow sample %d area=%d material=%u materialIndex=%u triArea=%.2f weight=%.3f distance=%.2f\n",
                    sampleIndex,
                    sample.areaNum,
                    sample.materialId,
                    sample.materialIndex,
                    sample.area,
                    sample.weight,
                    sample.distance);
            }
        }
        if (lightUniverseStats.droppedSampleCount > 0)
        {
            for (int sampleIndex = 0; sampleIndex < lightUniverseStats.droppedSampleCount; ++sampleIndex)
            {
                const RtSmokeLightUniverseCandidateSample& sample = lightUniverseStats.droppedSamples[sampleIndex];
                if (!sample.valid)
                {
                    continue;
                }
                common->Printf("PathTracePrimaryPass: RT smoke light area dropped sample %d reason=%s area=%d material=%u materialIndex=%u triArea=%.2f weight=%.3f distance=%.2f\n",
                    sampleIndex,
                    sample.reason ? sample.reason : "<unknown>",
                    sample.areaNum,
                    sample.materialId,
                    sample.materialIndex,
                    sample.area,
                    sample.weight,
                    sample.distance);
            }
        }
        common->Printf("PathTracePrimaryPass: RT smoke light origins persistentStatic=%d/%d currentDynamic=%d frameOnlyDynamic=%d persistableDynamic=%d promotedDynamic=%d unpromotedDynamic=%d injectedMissingDynamic=%d runtimeActive=%d runtimeInactiveSkipped=%d spatialMembership=unassigned temporalReuse=off\n",
            lightUniverseStats.staticMergedSeenTriangles,
            lightUniverseStats.staticMergedMissingTriangles,
            lightUniverseStats.dynamicFrameTriangles,
            lightUniverseStats.dynamicFrameOnlyTriangles,
            lightUniverseStats.dynamicPersistableFrameTriangles,
            lightUniverseStats.dynamicPromotedFrameTriangles,
            lightUniverseStats.dynamicUnpromotedFrameTriangles,
            lightUniverseStats.injectedMissingDynamicTriangles,
            emissiveInventoryStats.capturedTriangles,
            runtimeInactiveEmissiveTriangles);
        common->Printf("PathTracePrimaryPass: RT smoke light churn enabled=%d previous/current/stayed/entered/left=%d/%d/%d/%d/%d weight previous/current/stayed/entered/left=%.3f/%.3f/%.3f/%.3f/%.3f\n",
            lightUniverseStats.activeChurnEnabled,
            lightUniverseStats.activeChurnPrevious,
            lightUniverseStats.activeChurnCurrent,
            lightUniverseStats.activeChurnStayed,
            lightUniverseStats.activeChurnEntered,
            lightUniverseStats.activeChurnLeft,
            lightUniverseStats.activeChurnPreviousWeight,
            lightUniverseStats.activeChurnCurrentWeight,
            lightUniverseStats.activeChurnStayedWeight,
            lightUniverseStats.activeChurnEnteredWeight,
            lightUniverseStats.activeChurnLeftWeight);
        if (lightUniverseStats.enteredSampleCount > 0)
        {
            for (int sampleIndex = 0; sampleIndex < lightUniverseStats.enteredSampleCount; ++sampleIndex)
            {
                const RtSmokeLightUniverseCandidateSample& sample = lightUniverseStats.enteredSamples[sampleIndex];
                if (!sample.valid)
                {
                    continue;
                }
                common->Printf("PathTracePrimaryPass: RT smoke light churn entered sample %d area=%d material=%u materialIndex=%u triArea=%.2f weight=%.3f distance=%.2f\n",
                    sampleIndex,
                    sample.areaNum,
                    sample.materialId,
                    sample.materialIndex,
                    sample.area,
                    sample.weight,
                    sample.distance);
            }
        }
        if (lightUniverseStats.leftSampleCount > 0)
        {
            for (int sampleIndex = 0; sampleIndex < lightUniverseStats.leftSampleCount; ++sampleIndex)
            {
                const RtSmokeLightUniverseCandidateSample& sample = lightUniverseStats.leftSamples[sampleIndex];
                if (!sample.valid)
                {
                    continue;
                }
                common->Printf("PathTracePrimaryPass: RT smoke light churn left sample %d area=%d material=%u materialIndex=%u triArea=%.2f weight=%.3f distance=%.2f\n",
                    sampleIndex,
                    sample.areaNum,
                    sample.materialId,
                    sample.materialIndex,
                    sample.area,
                    sample.weight,
                    sample.distance);
            }
        }
        r_pathTracingLightUniverseDump.SetInteger(0);
    }

    const int bufferCreateStartMs = Sys_Milliseconds();
    RtSmokeSceneBufferCreateDesc bufferCreateDesc;
    bufferCreateDesc.device = device;
    bufferCreateDesc.existingBuffers.staticVertexBuffer = m_smokeStaticVertexBuffer;
    bufferCreateDesc.existingBuffers.staticIndexBuffer = m_smokeStaticIndexBuffer;
    bufferCreateDesc.existingBuffers.staticTriangleClassBuffer = m_smokeStaticTriangleClassBuffer;
    bufferCreateDesc.existingBuffers.staticTriangleMaterialBuffer = m_smokeStaticTriangleMaterialBuffer;
    bufferCreateDesc.existingBuffers.staticTriangleMaterialIndexBuffer = m_smokeStaticTriangleMaterialIndexBuffer;
    bufferCreateDesc.existingBuffers.dynamicVertexBuffer = m_smokeDynamicVertexBuffer;
    bufferCreateDesc.existingBuffers.dynamicIndexBuffer = m_smokeDynamicIndexBuffer;
    bufferCreateDesc.existingBuffers.dynamicTriangleClassBuffer = m_smokeDynamicTriangleClassBuffer;
    bufferCreateDesc.existingBuffers.dynamicTriangleMaterialBuffer = m_smokeDynamicTriangleMaterialBuffer;
    bufferCreateDesc.existingBuffers.dynamicTriangleMaterialIndexBuffer = m_smokeDynamicTriangleMaterialIndexBuffer;
    bufferCreateDesc.existingBuffers.materialTableBuffer = m_smokeMaterialTableBuffer;
    bufferCreateDesc.existingBuffers.emissiveTriangleBuffer = m_smokeEmissiveTriangleBuffer;
    bufferCreateDesc.existingBuffers.lightCandidateBuffer = m_smokeLightCandidateBuffer;
    bufferCreateDesc.existingBuffers.rigidRouteVertexBuffer = m_smokeRigidRouteVertexBuffer;
    bufferCreateDesc.existingBuffers.rigidRouteIndexBuffer = m_smokeRigidRouteIndexBuffer;
    bufferCreateDesc.existingBuffers.rigidRouteTriangleMaterialBuffer = m_smokeRigidRouteTriangleMaterialBuffer;
    bufferCreateDesc.existingBuffers.rigidRouteTriangleMaterialIndexBuffer = m_smokeRigidRouteTriangleMaterialIndexBuffer;
    bufferCreateDesc.existingBuffers.rigidRouteInstanceBuffer = m_smokeRigidRouteInstanceBuffer;
    bufferCreateDesc.staticVertexBytes = staticVertexCache.size() * sizeof(staticVertexCache[0]);
    bufferCreateDesc.staticIndexBytes = staticIndexCache.size() * sizeof(staticIndexCache[0]);
    bufferCreateDesc.staticTriangleClassBytes = staticTriangleClassCache.size() * sizeof(staticTriangleClassCache[0]);
    bufferCreateDesc.staticTriangleMaterialBytes = staticTriangleMaterialCache.size() * sizeof(staticTriangleMaterialCache[0]);
    bufferCreateDesc.staticTriangleMaterialIndexBytes = materialTable.staticMaterialIndexes.size() * sizeof(materialTable.staticMaterialIndexes[0]);
    bufferCreateDesc.dynamicVertexBytes = dynamicVertexData.size() * sizeof(dynamicVertexData[0]);
    bufferCreateDesc.dynamicIndexBytes = dynamicIndexData.size() * sizeof(dynamicIndexData[0]);
    bufferCreateDesc.dynamicTriangleClassBytes = dynamicTriangleClassData.size() * sizeof(dynamicTriangleClassData[0]);
    bufferCreateDesc.dynamicTriangleMaterialBytes = dynamicTriangleMaterialData.size() * sizeof(dynamicTriangleMaterialData[0]);
    bufferCreateDesc.dynamicTriangleMaterialIndexBytes = materialTable.dynamicMaterialIndexes.size() * sizeof(materialTable.dynamicMaterialIndexes[0]);
    bufferCreateDesc.materialTableBytes = materialTable.materials.size() * sizeof(materialTable.materials[0]);
    bufferCreateDesc.emissiveTriangleBytes = emissiveTriangles.size() * sizeof(emissiveTriangles[0]);
    bufferCreateDesc.lightCandidateBytes = lightCandidates.size() * sizeof(lightCandidates[0]);
    bufferCreateDesc.rigidRouteVertexBytes = rigidRouteBuild.vertices.size() * sizeof(PathTraceSmokeVertex);
    bufferCreateDesc.rigidRouteIndexBytes = rigidRouteBuild.indexes.size() * sizeof(uint32_t);
    bufferCreateDesc.rigidRouteTriangleMaterialBytes = rigidRouteBuild.triangleMaterials.size() * sizeof(uint32_t);
    bufferCreateDesc.rigidRouteTriangleMaterialIndexBytes = rigidRouteBuild.triangleMaterialIndexes.size() * sizeof(uint32_t);
    bufferCreateDesc.rigidRouteInstanceBytes = rigidRouteBuild.instances.size() * sizeof(PathTraceRigidRouteInstance);
    RtSmokeSceneBufferCreateResult bufferCreateResult;
    {
        OPTICK_EVENT("PT Create Scene Buffers");
        bufferCreateResult = CreateSmokeSceneBuffers(bufferCreateDesc);
    }
    if (!bufferCreateResult.Succeeded())
    {
        common->Printf("PathTracePrimaryPass: %s\n", bufferCreateResult.errorMessage ? bufferCreateResult.errorMessage : "failed to create RT smoke geometry buffers");
        return;
    }
    RtSmokeSceneBufferHandles smokeBuffers = bufferCreateResult.buffers;
    nvrhi::BufferHandle smokeStaticVertexBuffer = smokeBuffers.staticVertexBuffer;
    nvrhi::BufferHandle smokeStaticIndexBuffer = smokeBuffers.staticIndexBuffer;
    nvrhi::BufferHandle smokeStaticTriangleClassBuffer = smokeBuffers.staticTriangleClassBuffer;
    nvrhi::BufferHandle smokeStaticTriangleMaterialBuffer = smokeBuffers.staticTriangleMaterialBuffer;
    nvrhi::BufferHandle smokeStaticTriangleMaterialIndexBuffer = smokeBuffers.staticTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle smokeDynamicVertexBuffer = smokeBuffers.dynamicVertexBuffer;
    nvrhi::BufferHandle smokeDynamicIndexBuffer = smokeBuffers.dynamicIndexBuffer;
    nvrhi::BufferHandle smokeDynamicTriangleClassBuffer = smokeBuffers.dynamicTriangleClassBuffer;
    nvrhi::BufferHandle smokeDynamicTriangleMaterialBuffer = smokeBuffers.dynamicTriangleMaterialBuffer;
    nvrhi::BufferHandle smokeDynamicTriangleMaterialIndexBuffer = smokeBuffers.dynamicTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle smokeMaterialTableBuffer = smokeBuffers.materialTableBuffer;
    nvrhi::BufferHandle smokeEmissiveTriangleBuffer = smokeBuffers.emissiveTriangleBuffer;
    nvrhi::BufferHandle smokeLightCandidateBuffer = smokeBuffers.lightCandidateBuffer;
    nvrhi::BufferHandle smokeRigidRouteVertexBuffer = smokeBuffers.rigidRouteVertexBuffer;
    nvrhi::BufferHandle smokeRigidRouteIndexBuffer = smokeBuffers.rigidRouteIndexBuffer;
    nvrhi::BufferHandle smokeRigidRouteTriangleMaterialBuffer = smokeBuffers.rigidRouteTriangleMaterialBuffer;
    nvrhi::BufferHandle smokeRigidRouteTriangleMaterialIndexBuffer = smokeBuffers.rigidRouteTriangleMaterialIndexBuffer;
    nvrhi::BufferHandle smokeRigidRouteInstanceBuffer = smokeBuffers.rigidRouteInstanceBuffer;
    const int bufferCreateMs = Sys_Milliseconds() - bufferCreateStartMs;

    const int staticVertexCount = static_cast<int>(staticVertexCache.size());
    const int dynamicVertexCount = static_cast<int>(dynamicVertexData.size());
    const int staticIndexCount = bucketRanges.buckets[0].indexCount;
    const int dynamicIndexCount =
        bucketRanges.buckets[1].indexCount +
        bucketRanges.buckets[2].indexCount +
        bucketRanges.buckets[3].indexCount +
        bucketRanges.buckets[4].indexCount;
    const bool hasStaticBlas = staticIndexCount > 0;
    const bool hasDynamicBlas = dynamicIndexCount > 0;
    const RtSmokeBucketRange& staticBucketRange = bucketRanges.buckets[0];
    RtSmokeGeometryRange staticGeometryRange;
    staticGeometryRange.vertexOffset = staticBucketRange.vertexOffset;
    staticGeometryRange.vertexCount = staticBucketRange.vertexCount;
    staticGeometryRange.indexOffset = staticBucketRange.indexOffset;
    staticGeometryRange.indexCount = staticBucketRange.indexCount;
    staticGeometryRange.triangleOffset = staticBucketRange.triangleOffset;
    staticGeometryRange.triangleCount = staticBucketRange.triangleCount;
    const bool validateGeometryUniverse = r_pathTracingGeometryUniverseValidate.GetInteger() != 0;
    const RtSmokeGeometryUniverseStats geometryUniverseStats = m_smokeGeometryUniverse.GetStats(validateGeometryUniverse);
    if (validateGeometryUniverse && geometryUniverseStats.staticValidationErrors > 0)
    {
        if (g_smokeLastGeometryValidationDumpGeneration != geometryUniverseStats.generation ||
            g_smokeLastGeometryValidationDumpErrors != geometryUniverseStats.staticValidationErrors)
        {
            m_smokeGeometryUniverse.LogStaticValidationFailures(RT_SMOKE_GEOMETRY_VALIDATION_DUMP_RECORDS);
            g_smokeLastGeometryValidationDumpGeneration = geometryUniverseStats.generation;
            g_smokeLastGeometryValidationDumpErrors = geometryUniverseStats.staticValidationErrors;
        }
    }
    else if (geometryUniverseStats.staticValidationErrors == 0)
    {
        g_smokeLastGeometryValidationDumpErrors = 0;
    }
    const int staticVertexCacheCount = geometryUniverseStats.staticVerts;
    const int staticIndexCacheCount = geometryUniverseStats.staticIndexes;
    const int staticTriangleCacheCount = geometryUniverseStats.staticTriangles;
    const int staticCacheBytesKB = geometryUniverseStats.staticBytesKB;
    RtSmokeStaticBlasSignature staticSignature;
    staticSignature.vertexCount = staticGeometryRange.vertexCount;
    staticSignature.indexCount = staticGeometryRange.indexCount;
    staticSignature.triangleCount = staticGeometryRange.triangleCount;
    const int staticSignatureStartMs = Sys_Milliseconds();
    bool staticBlasSignatureReused = false;
    if (!staticCacheChanged && m_smokeStaticBlasCacheValid && m_smokeStaticBlasSignature != 0)
    {
        staticSignature.hash = m_smokeStaticBlasSignature;
        staticBlasSignatureReused = true;
    }
    else
    {
        {
            OPTICK_EVENT("PT Static BLAS Signature");
            staticSignature = ComputeSmokeStaticBlasSignature(staticVertexCache, staticIndexCache, staticTriangleClassCache, staticTriangleMaterialCache, staticGeometryRange, vec3_origin);
        }
    }
    const int staticBlasSignatureMs = Sys_Milliseconds() - staticSignatureStartMs;
    const uint64 reservoirSceneSignature = ComputeSmokeReservoirSceneSignature(
        materialTableSignature,
        staticSignature.hash,
        emissiveTriangles,
        lightCandidates);
    const bool staticBlasCacheHit = hasStaticBlas && m_smokeStaticBlasCacheValid && m_smokeStaticBlas &&
        m_smokeStaticVertexBuffer && m_smokeStaticIndexBuffer && m_smokeStaticTriangleClassBuffer && m_smokeStaticTriangleMaterialBuffer && m_smokeStaticTriangleMaterialIndexBuffer &&
        !staticCacheChanged && m_smokeStaticBlasSignature == staticSignature.hash;
    if (staticBlasCacheHit)
    {
        smokeStaticVertexBuffer = m_smokeStaticVertexBuffer;
        smokeStaticIndexBuffer = m_smokeStaticIndexBuffer;
        smokeStaticTriangleClassBuffer = m_smokeStaticTriangleClassBuffer;
        smokeStaticTriangleMaterialBuffer = m_smokeStaticTriangleMaterialBuffer;
        smokeStaticTriangleMaterialIndexBuffer = m_smokeStaticTriangleMaterialIndexBuffer;
        smokeBuffers.staticVertexBuffer = smokeStaticVertexBuffer;
        smokeBuffers.staticIndexBuffer = smokeStaticIndexBuffer;
        smokeBuffers.staticTriangleClassBuffer = smokeStaticTriangleClassBuffer;
        smokeBuffers.staticTriangleMaterialBuffer = smokeStaticTriangleMaterialBuffer;
        smokeBuffers.staticTriangleMaterialIndexBuffer = smokeStaticTriangleMaterialIndexBuffer;
    }

    if (!hasStaticBlas && !hasDynamicBlas)
    {
        common->Printf("PathTracePrimaryPass: no RT smoke BLAS ranges to build\n");
        return;
    }

    nvrhi::rt::AccelStructDesc smokeStaticBlasDesc;
    nvrhi::rt::AccelStructHandle smokeStaticBlas;
    if (hasStaticBlas)
    {
        if (staticBlasCacheHit)
        {
            smokeStaticBlas = m_smokeStaticBlas;
            smokeStaticBlasDesc = m_smokeStaticBlasDesc;
            ++m_smokeStaticBlasCacheHitCount;
        }
        else
        {
            RtSmokeBlasCreateDesc staticBlasCreateDesc;
            staticBlasCreateDesc.device = device;
            staticBlasCreateDesc.vertexBuffer = smokeStaticVertexBuffer;
            staticBlasCreateDesc.indexBuffer = smokeStaticIndexBuffer;
            staticBlasCreateDesc.vertexCount = staticVertexCount;
            staticBlasCreateDesc.indexCount = staticIndexCount;
            staticBlasCreateDesc.debugName = "PathTraceSmokeStaticWorldBLAS";
            RtSmokeBlasCreateResult staticBlasCreateResult;
            {
                OPTICK_EVENT("PT Create Static BLAS");
                staticBlasCreateResult = CreateSmokeBlas(staticBlasCreateDesc);
            }
            if (!staticBlasCreateResult.Succeeded())
            {
                common->Printf("PathTracePrimaryPass: failed to create RT smoke static BLAS\n");
                return;
            }
            smokeStaticBlasDesc = staticBlasCreateResult.accelStructDesc;
            smokeStaticBlas = staticBlasCreateResult.accelStruct;
            ++m_smokeStaticBlasCacheMissCount;
        }
    }

    nvrhi::rt::AccelStructDesc smokeDynamicBlasDesc;
    nvrhi::rt::AccelStructHandle smokeDynamicBlas;
    if (hasDynamicBlas)
    {
        RtSmokeBlasCreateDesc dynamicBlasCreateDesc;
        dynamicBlasCreateDesc.device = device;
        dynamicBlasCreateDesc.vertexBuffer = smokeDynamicVertexBuffer;
        dynamicBlasCreateDesc.indexBuffer = smokeDynamicIndexBuffer;
        dynamicBlasCreateDesc.vertexCount = dynamicVertexCount;
        dynamicBlasCreateDesc.indexCount = dynamicIndexCount;
        dynamicBlasCreateDesc.debugName = "PathTraceSmokeDynamicCandidateBLAS";
        RtSmokeBlasCreateResult dynamicBlasCreateResult;
        {
            OPTICK_EVENT("PT Create Dynamic BLAS");
            dynamicBlasCreateResult = CreateSmokeBlas(dynamicBlasCreateDesc);
        }
        if (!dynamicBlasCreateResult.Succeeded())
        {
            common->Printf("PathTracePrimaryPass: failed to create RT smoke dynamic BLAS\n");
            return;
        }
        smokeDynamicBlasDesc = dynamicBlasCreateResult.accelStructDesc;
        smokeDynamicBlas = dynamicBlasCreateResult.accelStruct;
    }

    const RtSmokeBufferUploadItem uploadItems[] = {
        { smokeStaticVertexBuffer, staticVertexCache.data(), staticVertexCache.size() * sizeof(PathTraceSmokeVertex), nvrhi::ResourceStates::AccelStructBuildInput, staticBlasCacheHit },
        { smokeStaticIndexBuffer, staticIndexCache.data(), staticIndexCache.size() * sizeof(uint32_t), nvrhi::ResourceStates::AccelStructBuildInput, staticBlasCacheHit },
        { smokeStaticTriangleClassBuffer, staticTriangleClassCache.data(), staticTriangleClassCache.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, staticBlasCacheHit },
        { smokeStaticTriangleMaterialBuffer, staticTriangleMaterialCache.data(), staticTriangleMaterialCache.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, staticBlasCacheHit },
        { smokeStaticTriangleMaterialIndexBuffer, materialTable.staticMaterialIndexes.data(), materialTable.staticMaterialIndexes.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, staticBlasCacheHit },
        { smokeDynamicVertexBuffer, dynamicVertexData.data(), dynamicVertexData.size() * sizeof(PathTraceSmokeVertex), nvrhi::ResourceStates::AccelStructBuildInput, false },
        { smokeDynamicIndexBuffer, dynamicIndexData.data(), dynamicIndexData.size() * sizeof(uint32_t), nvrhi::ResourceStates::AccelStructBuildInput, false },
        { smokeDynamicTriangleClassBuffer, dynamicTriangleClassData.data(), dynamicTriangleClassData.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, false },
        { smokeDynamicTriangleMaterialBuffer, dynamicTriangleMaterialData.data(), dynamicTriangleMaterialData.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, false },
        { smokeDynamicTriangleMaterialIndexBuffer, materialTable.dynamicMaterialIndexes.data(), materialTable.dynamicMaterialIndexes.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, false },
        { smokeMaterialTableBuffer, materialTable.materials.data(), materialTable.materials.size() * sizeof(PathTraceSmokeMaterial), nvrhi::ResourceStates::ShaderResource, false },
        { smokeEmissiveTriangleBuffer, emissiveTriangles.data(), emissiveTriangles.size() * sizeof(PathTraceSmokeEmissiveTriangle), nvrhi::ResourceStates::ShaderResource, false },
        { smokeLightCandidateBuffer, lightCandidates.data(), lightCandidates.size() * sizeof(PathTraceSmokeLightCandidate), nvrhi::ResourceStates::ShaderResource, false },
        { smokeRigidRouteVertexBuffer, rigidRouteBuild.vertices.data(), rigidRouteBuild.vertices.size() * sizeof(PathTraceSmokeVertex), nvrhi::ResourceStates::ShaderResource, false },
        { smokeRigidRouteIndexBuffer, rigidRouteBuild.indexes.data(), rigidRouteBuild.indexes.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, false },
        { smokeRigidRouteTriangleMaterialBuffer, rigidRouteBuild.triangleMaterials.data(), rigidRouteBuild.triangleMaterials.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, false },
        { smokeRigidRouteTriangleMaterialIndexBuffer, rigidRouteBuild.triangleMaterialIndexes.data(), rigidRouteBuild.triangleMaterialIndexes.size() * sizeof(uint32_t), nvrhi::ResourceStates::ShaderResource, false },
        { smokeRigidRouteInstanceBuffer, rigidRouteBuild.instances.data(), rigidRouteBuild.instances.size() * sizeof(PathTraceRigidRouteInstance), nvrhi::ResourceStates::ShaderResource, false }
    };
    RtSmokeBufferUploadBatchDesc uploadBatchDesc;
    uploadBatchDesc.commandList = commandList;
    uploadBatchDesc.items = uploadItems;
    uploadBatchDesc.itemCount = static_cast<int>(sizeof(uploadItems) / sizeof(uploadItems[0]));
    int bufferUploadMs = 0;
    if (optickGpuMarkers)
    {
        OPTICK_GPU_EVENT("PT GPU Upload Scene Buffers");
        bufferUploadMs = UploadSmokeAccelerationBuffers(uploadBatchDesc);
    }
    else
    {
        bufferUploadMs = UploadSmokeAccelerationBuffers(uploadBatchDesc);
    }

    RtSmokeAccelSubmitDesc accelSubmitDesc;
    std::vector<nvrhi::rt::InstanceDesc> rigidTlasRouteInstances;
    const bool routeRigidTlasInstances = enableRigidRouteForMode;
    if (routeRigidTlasInstances)
    {
        const int routedRigidInstances = m_smokeGeometryUniverse.BuildRigidTlasInstanceDescs(
            m_instanceUniverse,
            rigidTlasRouteInstances,
            2,
            0x02,
            rigidRouteMaxInstances);
        if (r_pathTracingSmokeLog.GetInteger() != 0 && routedRigidInstances > 0 && (m_smokeGeometryFrameIndex % 120ull) == 1ull)
        {
            common->Printf("PathTracePrimaryPass: PT rigid TLAS route debug mode active mode=%d routedInstances=%d renderPath=dynamicFallback traceMask=%s\n",
                requestedDebugMode,
                routedRigidInstances,
                requestedDebugMode == 23 ? "rigidOnly" : (requestedDebugMode == 24 ? "fallbackAndRigidValidation" : (requestedDebugMode == 25 ? "fallbackAndRigidLighting" : (requestedDebugMode == 20 ? "mode20Integration" : "mode18Integration"))));
        }
    }
    accelSubmitDesc.commandList = commandList;
    accelSubmitDesc.tlas = m_smokeTlas;
    accelSubmitDesc.staticBlas = smokeStaticBlas;
    accelSubmitDesc.dynamicBlas = smokeDynamicBlas;
    accelSubmitDesc.staticBlasDesc = smokeStaticBlasDesc;
    accelSubmitDesc.dynamicBlasDesc = smokeDynamicBlasDesc;
    accelSubmitDesc.extraTlasInstances = routeRigidTlasInstances ? &rigidTlasRouteInstances : nullptr;
    accelSubmitDesc.hasStaticBlas = hasStaticBlas;
    accelSubmitDesc.hasDynamicBlas = hasDynamicBlas;
    accelSubmitDesc.staticBlasCacheHit = staticBlasCacheHit;
    RtSmokeAccelSubmitTiming accelSubmitTiming;
    bool accelSubmitSucceeded = false;
    if (optickGpuMarkers)
    {
        OPTICK_GPU_EVENT("PT GPU Submit Acceleration Builds");
        accelSubmitSucceeded = SubmitSmokeAccelerationBuilds(accelSubmitDesc, accelSubmitTiming);
    }
    else
    {
        accelSubmitSucceeded = SubmitSmokeAccelerationBuilds(accelSubmitDesc, accelSubmitTiming);
    }
    if (!accelSubmitSucceeded)
    {
        common->Printf("PathTracePrimaryPass: failed to submit RT smoke acceleration structures\n");
        return;
    }
    const int blasSubmitMs = accelSubmitTiming.blasSubmitMs;
    const int tlasSubmitMs = accelSubmitTiming.tlasSubmitMs;
    const int accelSubmitMs = accelSubmitTiming.accelSubmitMs;
    const int instanceCount = accelSubmitTiming.instanceCount;

    const nvrhi::TextureHandle fallbackTexture = globalImages && globalImages->whiteImage ? globalImages->whiteImage->GetTextureHandle() : nullptr;
    if (!fallbackTexture)
    {
        common->Printf("PathTracePrimaryPass: failed to find RT smoke fallback material texture\n");
        return;
    }

    RtSmokeBindingBuildDesc bindingBuildDesc;
    bindingBuildDesc.device = device;
    bindingBuildDesc.tlas = m_smokeTlas;
    bindingBuildDesc.outputTexture = m_smokeOutputTexture;
    bindingBuildDesc.accumulationTexture = m_smokeAccumulationTexture;
    bindingBuildDesc.fallbackTexture = fallbackTexture;
    bindingBuildDesc.constantsBuffer = m_smokeConstantsBuffer;
    bindingBuildDesc.boundsOverlayLineBuffer = m_smokeBoundsOverlayLineBuffer;
    bindingBuildDesc.bindingLayout = m_smokeBindingLayout;
    bindingBuildDesc.textureBindlessLayout = m_smokeTextureBindlessLayout;
    bindingBuildDesc.existingTextureDescriptorTable = m_smokeTextureDescriptorTable;
    bindingBuildDesc.sampler = m_backend->GetCommonPasses().m_AnisotropicWrapSampler;
    bindingBuildDesc.buffers = smokeBuffers;
    bindingBuildDesc.reservoirBuffers = m_smokeReservoirBuffers;
    bindingBuildDesc.enableTextureProbe = enableTextureProbe;
    bindingBuildDesc.forceFallbackTexture = r_pathTracingTextureForceFallback.GetInteger() != 0;
    bindingBuildDesc.maxActiveTextures = RT_SMOKE_TEXTURE_EXPERIMENTAL_ACTIVE_CAP;

    RtSmokeBindingBuildResult bindingBuildResult;
    {
        OPTICK_EVENT("PT Create Binding Resources");
        bindingBuildResult = CreateSmokeBindingResources(bindingBuildDesc, materialTable);
    }
    if (!bindingBuildResult.Succeeded())
    {
        if (bindingBuildResult.failedTextureSlot >= 0)
        {
            common->Printf("PathTracePrimaryPass: %s %d\n", bindingBuildResult.errorMessage, bindingBuildResult.failedTextureSlot);
        }
        else
        {
            common->Printf("PathTracePrimaryPass: %s\n", bindingBuildResult.errorMessage ? bindingBuildResult.errorMessage : "failed to create RT smoke binding resources");
        }
        return;
    }

    RtSmokeSceneResourceCommitBuildDesc resourceCommitBuildDesc;
    resourceCommitBuildDesc.buffers = smokeBuffers;
    resourceCommitBuildDesc.staticBlasDesc = smokeStaticBlasDesc;
    resourceCommitBuildDesc.dynamicBlasDesc = smokeDynamicBlasDesc;
    resourceCommitBuildDesc.staticBlas = smokeStaticBlas;
    resourceCommitBuildDesc.dynamicBlas = smokeDynamicBlas;
    resourceCommitBuildDesc.hasStaticBlas = hasStaticBlas;
    resourceCommitBuildDesc.staticBlasSignature = staticSignature.hash;
    resourceCommitBuildDesc.bindingSet = bindingBuildResult.bindingSet;
    resourceCommitBuildDesc.textureDescriptorTable = bindingBuildResult.textureDescriptorTable;
    resourceCommitBuildDesc.activeTextureTable = &bindingBuildResult.activeTextureTable;
    resourceCommitBuildDesc.materialTableEntryCount = static_cast<int>(materialTable.materials.size());
    resourceCommitBuildDesc.emissiveTriangleCount = emissiveInventoryStats.capturedTriangles;
    resourceCommitBuildDesc.emissiveStaticTriangleCount = emissiveInventoryStats.staticTriangles;
    resourceCommitBuildDesc.emissiveDynamicTriangleCount = emissiveInventoryStats.dynamicTriangles;
    resourceCommitBuildDesc.lightCandidateCount = emissiveInventoryStats.candidateMaterials;
    resourceCommitBuildDesc.texturedLightCandidateCount = emissiveInventoryStats.texturedCandidateMaterials;
    resourceCommitBuildDesc.lightCandidateBytes = static_cast<int>(lightCandidates.size() * sizeof(lightCandidates[0]));
    resourceCommitBuildDesc.reservoirSceneSignature = reservoirSceneSignature;
    const RtSmokeSceneResourceCommitDesc resourceCommitDesc = CreateSmokeSceneResourceCommitDesc(resourceCommitBuildDesc);
    {
        OPTICK_EVENT("PT Commit Scene Resources");
        CommitRayTracingSmokeSceneResources(resourceCommitDesc);
    }

    const int sceneMs = Sys_Milliseconds() - sceneStartMs;
    RtSmokeSceneBuildDiagnosticLogDesc sceneLogDesc;
    sceneLogDesc.sceneMs = sceneMs;
    sceneLogDesc.captureMs = captureMs;
    sceneLogDesc.metadataMs = metadataMs;
    sceneLogDesc.metadataValidationMs = metadataValidationMs;
    sceneLogDesc.metadataRegistrationMs = metadataRegistrationMs;
    sceneLogDesc.materialMs = materialMs;
    sceneLogDesc.emissiveMs = emissiveMs;
    sceneLogDesc.bufferCreateMs = bufferCreateMs;
    sceneLogDesc.bufferUploadMs = bufferUploadMs;
    sceneLogDesc.accelSubmitMs = accelSubmitMs;
    sceneLogDesc.blasSubmitMs = blasSubmitMs;
    sceneLogDesc.tlasSubmitMs = tlasSubmitMs;
    sceneLogDesc.sourceSurfaces = sourceSurfaces;
    sceneLogDesc.sourceVerts = sourceVerts;
    sceneLogDesc.sourceIndexes = sourceIndexes;
    sceneLogDesc.anchorTriangle = anchorTriangle;
    sceneLogDesc.staticIndexCount = staticIndexCount;
    sceneLogDesc.dynamicIndexCount = dynamicIndexCount;
    sceneLogDesc.instanceCount = instanceCount;
    sceneLogDesc.requestedDebugMode = requestedDebugMode;
    sceneLogDesc.staticSurfaceCacheSize = geometryUniverseStats.staticSurfaces;
    sceneLogDesc.staticVertexCacheCount = staticVertexCacheCount;
    sceneLogDesc.staticIndexCacheCount = staticIndexCacheCount;
    sceneLogDesc.staticTriangleCacheCount = staticTriangleCacheCount;
    sceneLogDesc.staticSeenThisFrame = geometryUniverseStats.staticSeenThisFrame;
    sceneLogDesc.staticNewThisFrame = geometryUniverseStats.staticNewThisFrame;
    sceneLogDesc.staticDisappearedThisFrame = geometryUniverseStats.staticDisappearedThisFrame;
    sceneLogDesc.staticHistoryValid = geometryUniverseStats.staticHistoryValid;
    sceneLogDesc.staticPreviousRangeValid = geometryUniverseStats.staticPreviousRangeValid;
    sceneLogDesc.staticDirty = geometryUniverseStats.staticDirty;
    sceneLogDesc.staticValidationErrors = geometryUniverseStats.staticValidationErrors;
    sceneLogDesc.staticRangeErrors = geometryUniverseStats.staticRangeErrors;
    sceneLogDesc.staticDuplicateKeys = geometryUniverseStats.staticDuplicateKeys;
    sceneLogDesc.staticHistoryErrors = geometryUniverseStats.staticHistoryErrors;
    sceneLogDesc.staticKeyVectorMismatches = geometryUniverseStats.staticKeyVectorMismatches;
    sceneLogDesc.staticCacheBytesKB = staticCacheBytesKB;
    sceneLogDesc.staticBlasSignatureReused = staticBlasSignatureReused;
    sceneLogDesc.staticBlasSignatureMs = staticBlasSignatureMs;
    sceneLogDesc.staticBlasCacheHitCount = m_smokeStaticBlasCacheHitCount;
    sceneLogDesc.staticBlasCacheMissCount = m_smokeStaticBlasCacheMissCount;
    sceneLogDesc.sceneCaptureLogIntervalFrames = RT_SMOKE_SCENE_LOG_INTERVAL_FRAMES;
    sceneLogDesc.staticBlasCacheHit = staticBlasCacheHit;
    sceneLogDesc.materialTableCacheHit = materialTableCacheHit;
    sceneLogDesc.enableTextureProbe = enableTextureProbe;
    sceneLogDesc.dumpClassReasons = dumpClassReasons;
    sceneLogDesc.staticBlasSignature = staticSignature.hash;
    sceneLogDesc.materialTableSignature = materialTableSignature;
    sceneLogDesc.materialTablePath = materialTablePath;
    sceneLogDesc.captureTiming = captureTiming;
    sceneLogDesc.classStats = &classStats;
    sceneLogDesc.skipStats = &skipStats;
    sceneLogDesc.dynamicStats = &dynamicStats;
    sceneLogDesc.attributeStats = &attributeStats;
    sceneLogDesc.materialStats = &materialStats;
    sceneLogDesc.bucketRanges = &bucketRanges;
    sceneLogDesc.materialTable = &materialTable;
    sceneLogDesc.emissiveInventoryStats = &emissiveInventoryStats;
    sceneLogDesc.lightCandidateBytes = static_cast<int>(lightCandidates.size() * sizeof(lightCandidates[0]));
    sceneLogDesc.materialTableCacheStats = &materialTableCacheStats;
    sceneLogDesc.materialTableBuildStats = &materialTableBuildStats;
    sceneLogDesc.materialUniverseStats = &materialUniverseStats;
    sceneLogDesc.materialUniverseTableCompareStats = &materialUniverseTableCompareStats;
    sceneLogDesc.textureCoverageStats = &textureCoverageStats;
    sceneLogDesc.reasonSamples = &reasonSamples;
    sceneLogDesc.lastSceneTimingLogMs = &g_smokeLastSceneTimingLogMs;
    sceneLogDesc.sceneRebuildLogged = &m_smokeSceneRebuildLogged;
    sceneLogDesc.sceneLogCooldownFrames = &m_smokeSceneLogCooldownFrames;
    RunSmokeSceneBuildDiagnosticLogs(sceneLogDesc);
}
