#include "precompiled.h"
#pragma hdrstop

// Ray tracing dispatch for the RT smoke/path tracing path.
//
// Builds the per-frame raygen constants, transitions committed scene resources
// for shader access, dispatches the smoke RT pipeline, manages accumulation, and
// queues optional readback. Scene capture/resource ownership is intentionally
// outside this module.

#include "PathTraceCVars.h"
#include "PathTraceSmokeDispatch.h"
#include "PathTracePrimaryPass.h"
#include "PathTraceAcceleration.h"
#include "PathTraceDebugDumps.h"
#include "PathTraceDoomLights.h"
#include "PathTraceLightSelection.h"
#include "PathTraceRestirPasses.h"
#include "../RenderBackend.h"
#include "../RenderCommon.h"
#include "../../sys/DeviceManager.h"

#include <nvrhi/utils.h>

extern idCVar r_forceAmbient;
extern DeviceManager* deviceManager;

namespace {

const int RT_SMOKE_MAX_EMISSIVE_TRIANGLE_RECORDS = 65536;
int g_smokeLastDispatchTimingLogMs = -1000000;

nvrhi::ObjectType GetPathTraceCommandObjectType()
{
    if (deviceManager && deviceManager->GetGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN)
    {
        return nvrhi::ObjectTypes::VK_CommandBuffer;
    }
    return nvrhi::ObjectTypes::D3D12_GraphicsCommandList;
}

struct PathTraceSmokeConstants
{
    float cameraOriginAndTMax[4];
    float cameraForwardAndTanX[4];
    float cameraLeftAndTanY[4];
    float cameraUpAndDebugMode[4];
    float textureInfo[4];
    float lightOriginAndRadius[RT_SMOKE_MAX_DEBUG_LIGHTS][4];
    float lightColorAndIntensity[RT_SMOKE_MAX_DEBUG_LIGHTS][4];
    float lightInfo[4];
    float portalWindowInfo[4];
    float lightSpriteInfo[4];
    float toyPathInfo[4];
    float emissiveInfo[4];
    float boundsOverlayInfo[4];
    float doomAnalyticLightInfo[4];
    float doomAnalyticLightRemapInfo[4];
    float restirPTInfo[4];
    float integratorInfo[4];
    float integratorInfo2[4];
    float prevCameraOriginAndValid[4];
    float prevCameraForwardAndTanX[4];
    float prevCameraLeftAndTanY[4];
    float prevCameraUpAndTanY[4];
    float safetyInfo[4];
    float geometryInfo0[4];
    float geometryInfo1[4];
    float geometryInfo2[4];
    float geometryInfo3[4];
    float geometryInfo4[4];
    float dispatchTileInfo[4];
    float neeInfo[4];
    float motionVectorInfo[4];
};

struct PathTraceIntegratorSettings
{
    int samplesPerPixel = 1;
    int maxPathDepth = 2;
    int diffuseBounceLimit = 1;
    int specularBounceLimit = 0;
    int transmissionBounceLimit = 0;
    int reflectionMode = 0;
    int russianRouletteDepth = 0;
    int nextEventEstimation = 1;
    int secondaryNeeMode = 1;
    int secondaryNeeVisibility = 1;
    int secondaryAnalyticNeeMode = 2;
    int secondaryAnalyticNeeSamples = 1;
};

struct PathTraceDispatchTileSettings
{
    bool enabled = false;
    int tileWidth = 0;
    int tileHeight = 0;
    int tileColumns = 1;
    int tileRows = 1;
    int tileCount = 1;
    uint64 estimatedRaysPerTile = 0;
    uint64 estimatedRaysFullFrame = 0;
};

enum PathTraceSafetyDisableBits : uint32_t
{
    RT_PT_SAFETY_DISABLE_ANY_HIT_ALPHA = 1u << 0,
    RT_PT_SAFETY_DISABLE_SELECTED_LIGHT_LOOP = 1u << 1,
    RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP = 1u << 2,
    RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING = 1u << 3,
    RT_PT_SAFETY_DISABLE_DIFFUSE_SECONDARY_RAY = 1u << 4,
    RT_PT_SAFETY_DISABLE_REFLECTION_RAY = 1u << 5,
    RT_PT_SAFETY_DISABLE_PRIMARY_SURFACE_HISTORY = 1u << 6,
    RT_PT_SAFETY_DISABLE_RESERVOIR_WRITES = 1u << 7,
    RT_PT_SAFETY_DISABLE_RESTIR_VISIBILITY_RAY = 1u << 8,
};

uint64 HashSmokeDispatchValue(uint64 hash, uint64 value)
{
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

PathTraceIntegratorSettings BuildPathTraceIntegratorSettings()
{
    PathTraceIntegratorSettings settings;
    settings.samplesPerPixel = idMath::ClampInt(1, 4, r_pathTracingSamplesPerPixel.GetInteger());
    settings.maxPathDepth = idMath::ClampInt(1, 4, r_pathTracingMaxPathDepth.GetInteger());
    settings.diffuseBounceLimit = idMath::ClampInt(0, 3, r_pathTracingDiffuseBounceLimit.GetInteger());
    settings.specularBounceLimit = idMath::ClampInt(0, 2, r_pathTracingSpecularBounceLimit.GetInteger());
    settings.transmissionBounceLimit = idMath::ClampInt(0, 0, r_pathTracingTransmissionBounceLimit.GetInteger());
    settings.reflectionMode = idMath::ClampInt(0, 2, r_pathTracingReflectionMode.GetInteger());
    settings.russianRouletteDepth = idMath::ClampInt(0, 8, r_pathTracingRussianRouletteDepth.GetInteger());
    settings.nextEventEstimation = r_pathTracingNextEventEstimation.GetInteger() != 0 ? 1 : 0;
    settings.secondaryNeeMode = idMath::ClampInt(0, 2, r_pathTracingSecondaryNeeMode.GetInteger());
    settings.secondaryNeeVisibility = r_pathTracingSecondaryNeeVisibility.GetInteger() != 0 ? 1 : 0;
    settings.secondaryAnalyticNeeMode = idMath::ClampInt(0, 2, r_pathTracingSecondaryAnalyticNeeMode.GetInteger());
    settings.secondaryAnalyticNeeSamples = idMath::ClampInt(0, 8, r_pathTracingSecondaryAnalyticNeeSamples.GetInteger());
    return settings;
}

uint32_t BuildPathTraceSafetyDisableMask()
{
    uint32_t mask = 0u;
    mask |= r_pathTracingDisableAnyHitAlpha.GetInteger() != 0 ? RT_PT_SAFETY_DISABLE_ANY_HIT_ALPHA : 0u;
    mask |= r_pathTracingDisableSelectedLightLoop.GetInteger() != 0 ? RT_PT_SAFETY_DISABLE_SELECTED_LIGHT_LOOP : 0u;
    mask |= r_pathTracingDisableAnalyticLightLoop.GetInteger() != 0 ? RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP : 0u;
    mask |= r_pathTracingDisableEmissiveTriangleSampling.GetInteger() != 0 ? RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING : 0u;
    mask |= r_pathTracingDisableDiffuseSecondaryRay.GetInteger() != 0 ? RT_PT_SAFETY_DISABLE_DIFFUSE_SECONDARY_RAY : 0u;
    mask |= r_pathTracingDisableReflectionRay.GetInteger() != 0 ? RT_PT_SAFETY_DISABLE_REFLECTION_RAY : 0u;
    mask |= r_pathTracingDisablePrimarySurfaceHistory.GetInteger() != 0 ? RT_PT_SAFETY_DISABLE_PRIMARY_SURFACE_HISTORY : 0u;
    mask |= r_pathTracingDisableReservoirWrites.GetInteger() != 0 ? RT_PT_SAFETY_DISABLE_RESERVOIR_WRITES : 0u;
    mask |= r_pathTracingDisableRestirVisibilityRay.GetInteger() != 0 ? RT_PT_SAFETY_DISABLE_RESTIR_VISIBILITY_RAY : 0u;
    return mask;
}

bool PathTraceSafetyDisabled(uint32_t mask, PathTraceSafetyDisableBits bit)
{
    return (mask & static_cast<uint32_t>(bit)) != 0u;
}

PathTraceIntegratorSettings ApplyPathTraceSafetyKillSwitches(PathTraceIntegratorSettings settings, uint32_t safetyDisableMask)
{
    if (PathTraceSafetyDisabled(safetyDisableMask, RT_PT_SAFETY_DISABLE_DIFFUSE_SECONDARY_RAY))
    {
        settings.diffuseBounceLimit = 0;
    }
    if (PathTraceSafetyDisabled(safetyDisableMask, RT_PT_SAFETY_DISABLE_REFLECTION_RAY))
    {
        settings.specularBounceLimit = 0;
        settings.reflectionMode = 0;
    }
    return settings;
}

uint64 HashPathTraceIntegratorSettings(uint64 hash, const PathTraceIntegratorSettings& settings)
{
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.samplesPerPixel));
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.maxPathDepth));
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.diffuseBounceLimit));
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.specularBounceLimit));
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.transmissionBounceLimit));
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.reflectionMode));
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.russianRouletteDepth));
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.nextEventEstimation));
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.secondaryNeeMode));
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.secondaryNeeVisibility));
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.secondaryAnalyticNeeMode));
    hash = HashSmokeDispatchValue(hash, static_cast<uint64>(settings.secondaryAnalyticNeeSamples));
    return hash;
}

int EstimatePathTraceRaysPerPixel(const PathTraceIntegratorSettings& settings, int selectedLightCount, int analyticLightCount)
{
    const int diffuseRayCount = (settings.maxPathDepth > 1 && settings.diffuseBounceLimit > 0) ? 1 : 0;
    const int reflectionRayCount = (settings.maxPathDepth > 1 && settings.specularBounceLimit > 0 && settings.reflectionMode > 0) ? 1 : 0;
    const int secondarySurfaceCount = diffuseRayCount + reflectionRayCount;
    int neeTrials = 0;
    if (settings.nextEventEstimation != 0)
    {
        neeTrials += selectedLightCount + analyticLightCount;
        const int secondarySelectedTrials = settings.secondaryNeeMode == 2 ? selectedLightCount : (settings.secondaryNeeMode == 1 && selectedLightCount > 0 ? 1 : 0);
        const int secondaryAnalyticTrials = settings.secondaryAnalyticNeeMode == 2
            ? analyticLightCount
            : (settings.secondaryAnalyticNeeMode == 1 && analyticLightCount > 0 ? Min(settings.secondaryAnalyticNeeSamples, analyticLightCount) : 0);
        neeTrials += secondarySurfaceCount * (secondarySelectedTrials + secondaryAnalyticTrials);
    }
    return settings.samplesPerPixel * (1 + diffuseRayCount + reflectionRayCount + neeTrials);
}

PathTraceDispatchTileSettings BuildPathTraceDispatchTileSettings(int outputWidth, int outputHeight, int estimatedRaysPerPixel)
{
    PathTraceDispatchTileSettings settings;
    const int safeOutputWidth = Max(0, outputWidth);
    const int safeOutputHeight = Max(0, outputHeight);
    const uint64 estimatedRaysPerOutputPixel = static_cast<uint64>(Max(1, estimatedRaysPerPixel));
    settings.tileWidth = safeOutputWidth;
    settings.tileHeight = safeOutputHeight;
    settings.estimatedRaysFullFrame =
        static_cast<uint64>(safeOutputWidth) *
        static_cast<uint64>(safeOutputHeight) *
        estimatedRaysPerOutputPixel;

    if (safeOutputWidth <= 0 || safeOutputHeight <= 0 || r_pathTracingDispatchTileEnable.GetInteger() == 0)
    {
        settings.estimatedRaysPerTile = settings.estimatedRaysFullFrame;
        return settings;
    }

    settings.enabled = true;
    settings.tileWidth = idMath::ClampInt(1, safeOutputWidth, r_pathTracingDispatchTileWidth.GetInteger());
    settings.tileHeight = idMath::ClampInt(1, safeOutputHeight, r_pathTracingDispatchTileHeight.GetInteger());
    settings.tileColumns = (safeOutputWidth + settings.tileWidth - 1) / settings.tileWidth;
    settings.tileRows = (safeOutputHeight + settings.tileHeight - 1) / settings.tileHeight;
    settings.tileCount = settings.tileColumns * settings.tileRows;
    settings.estimatedRaysPerTile =
        static_cast<uint64>(settings.tileWidth) *
        static_cast<uint64>(settings.tileHeight) *
        estimatedRaysPerOutputPixel;
    return settings;
}

double PathTraceMicrosecondsToMilliseconds(uint64 elapsedUs)
{
    return static_cast<double>(elapsedUs) / 1000.0;
}

}

size_t GetPathTraceSmokeConstantsSize()
{
    return sizeof(PathTraceSmokeConstants);
}
void PathTracePrimaryPass::ExecuteRayTracingSmokeTest(const viewDef_t* viewDef)
{
    OPTICK_EVENT("PT Dispatch");

    const uint64 executeStartUs = Sys_Microseconds();
    if (!viewDef || !m_smokeSceneBuilt || !m_smokeShaderTable || !m_smokeBindingSet || !m_smokeTextureDescriptorTable || !m_frameResources.outputTexture || !m_frameResources.accumulationTexture || !m_frameResources.readbackTexture || !m_smokeConstantsBuffer || !m_restirPTConstantsBuffer || !m_smokeBoundsOverlayLineBuffer ||
        !m_smokeStaticVertexBuffer || !m_smokeStaticIndexBuffer || !m_smokeStaticTriangleClassBuffer || !m_smokeStaticTriangleMaterialBuffer || !m_smokeStaticTriangleMaterialIndexBuffer ||
        !m_smokeDynamicVertexBuffer || !m_smokeDynamicIndexBuffer || !m_smokeDynamicTriangleClassBuffer || !m_smokeDynamicTriangleMaterialBuffer || !m_smokeDynamicTriangleMaterialIndexBuffer || !m_smokeMaterialTableBuffer || !m_smokeEmissiveTriangleBuffer || !m_smokeLightCandidateBuffer || !m_smokeDoomAnalyticLightBuffer || !m_smokeDoomAnalyticPreviousLightBuffer ||
        !m_smokeDoomAnalyticCurrentIdentityBuffer || !m_smokeDoomAnalyticPreviousIdentityBuffer || !m_smokeDoomAnalyticRemapBuffer ||
        !m_smokeRigidRouteVertexBuffer || !m_smokeRigidRouteIndexBuffer || !m_smokeRigidRouteTriangleMaterialBuffer || !m_smokeRigidRouteTriangleMaterialIndexBuffer || !m_smokeRigidRouteInstanceBuffer)
    {
        return;
    }
    if (!m_frameResources.smokeReservoirBuffers.IsValidFor(m_frameResources.width, m_frameResources.height))
    {
        return;
    }
    if (!m_frameResources.restirPTReservoirBuffers.IsValidFor(static_cast<uint32_t>(m_frameResources.width), static_cast<uint32_t>(m_frameResources.height), rtxdi::CheckerboardMode::Off))
    {
        return;
    }
    if (!m_frameResources.primarySurfaceHistoryBuffers.IsValidFor(static_cast<uint32_t>(m_frameResources.width), static_cast<uint32_t>(m_frameResources.height)))
    {
        return;
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

    int debugMode = idMath::ClampInt(0, 52, r_pathTracingDebugMode.GetInteger());
    m_frameResources.settings.debugMode = debugMode;
    m_frameResources.settings.checkerboardMode = rtxdi::CheckerboardMode::Off;
    const bool requestedRestirPTDebugMode = IsPathTraceRestirPTDebugMode(debugMode);
    const bool requestedIntegratorDebugMode = debugMode >= 34 && debugMode <= 37;
    if ((debugMode == 8 || debugMode == 9 || debugMode == 10 || debugMode == 11 || debugMode == 12 || debugMode == 13 || debugMode == 14 || debugMode == 15 || debugMode == 18 || debugMode == 19 || debugMode == 20 || debugMode == 38 || debugMode == 39 || debugMode == 40 || debugMode == 41 || debugMode == 42 || debugMode == 43 || debugMode == 44 || debugMode == 45 || debugMode == 46 || debugMode == 47 || debugMode == 48 || debugMode == 49 || requestedRestirPTDebugMode || requestedIntegratorDebugMode) && r_pathTracingTextureTableLimit.GetInteger() <= 0)
    {
        debugMode = 7;
    }
    const bool restirPTDebugMode = IsPathTraceRestirPTDebugMode(debugMode);
    const bool integratorDebugMode = debugMode >= 34 && debugMode <= 37;
    const bool restirPTInitialOnlyMode = debugMode >= 26 && debugMode <= 28;
    const bool restirPTTemporalMode = debugMode == 31;
    const bool restirPTTemporalShadingMode = debugMode == 32;
    const bool restirPTAttributionMode = debugMode == 33;
    const bool restirPTSpatialShadingMode = debugMode == 50;
    const bool restirPTSpatialAttributionMode = debugMode == 51;
    if (restirPTInitialOnlyMode && !m_smokeRestirInitialShaderTable)
    {
        InitRayTracingSmokeRestirPipeline(0);
    }
    else if (restirPTTemporalMode && !m_smokeRestirShaderTable)
    {
        InitRayTracingSmokeRestirPipeline(1);
    }
    else if (restirPTTemporalShadingMode && !m_smokeRestirTemporalShadingShaderTable)
    {
        InitRayTracingSmokeRestirPipeline(2);
    }
    else if (restirPTAttributionMode && !m_smokeRestirAttributionShaderTable)
    {
        InitRayTracingSmokeRestirPipeline(3);
    }
    else if (restirPTSpatialShadingMode && !m_smokeRestirSpatialShaderTable)
    {
        InitRayTracingSmokeRestirPipeline(4);
    }
    else if (restirPTSpatialAttributionMode && !m_smokeRestirSpatialAttributionShaderTable)
    {
        InitRayTracingSmokeRestirPipeline(5);
    }
    if ((restirPTSpatialShadingMode || restirPTSpatialAttributionMode) && !m_smokeRestirShaderTable)
    {
        InitRayTracingSmokeRestirPipeline(1);
    }
    nvrhi::rt::State state;
    if (restirPTInitialOnlyMode && m_smokeRestirInitialShaderTable)
    {
        state.shaderTable = m_smokeRestirInitialShaderTable;
    }
    else if (restirPTTemporalMode && m_smokeRestirShaderTable)
    {
        state.shaderTable = m_smokeRestirShaderTable;
    }
    else if (restirPTTemporalShadingMode && m_smokeRestirTemporalShadingShaderTable)
    {
        state.shaderTable = m_smokeRestirTemporalShadingShaderTable;
    }
    else if (restirPTAttributionMode && m_smokeRestirAttributionShaderTable)
    {
        state.shaderTable = m_smokeRestirAttributionShaderTable;
    }
    else if (restirPTSpatialShadingMode && m_smokeRestirSpatialShaderTable)
    {
        state.shaderTable = m_smokeRestirSpatialShaderTable;
    }
    else if (restirPTSpatialAttributionMode && m_smokeRestirSpatialAttributionShaderTable)
    {
        state.shaderTable = m_smokeRestirSpatialAttributionShaderTable;
    }
    else
    {
        state.shaderTable = m_smokeShaderTable;
    }
    state.bindings = { m_smokeBindingSet, m_smokeTextureDescriptorTable };
    const uint32_t safetyDisableMask = BuildPathTraceSafetyDisableMask();
    const bool disableSelectedLightLoop = PathTraceSafetyDisabled(safetyDisableMask, RT_PT_SAFETY_DISABLE_SELECTED_LIGHT_LOOP);
    const bool disableAnalyticLightLoop = PathTraceSafetyDisabled(safetyDisableMask, RT_PT_SAFETY_DISABLE_ANALYTIC_LIGHT_LOOP);
    const bool disableEmissiveTriangleSampling = PathTraceSafetyDisabled(safetyDisableMask, RT_PT_SAFETY_DISABLE_EMISSIVE_TRIANGLE_SAMPLING);
    const bool disablePrimarySurfaceHistory = PathTraceSafetyDisabled(safetyDisableMask, RT_PT_SAFETY_DISABLE_PRIMARY_SURFACE_HISTORY);
    const bool disableReservoirWrites = PathTraceSafetyDisabled(safetyDisableMask, RT_PT_SAFETY_DISABLE_RESERVOIR_WRITES);
    const bool motionVectorExportEnabled = r_pathTracingMotionVectorExport.GetInteger() != 0;
    const bool restirPTPreviewVisibility = r_pathTracingRestirPTPreviewVisibility.GetInteger() != 0 && !PathTraceSafetyDisabled(safetyDisableMask, RT_PT_SAFETY_DISABLE_RESTIR_VISIBILITY_RAY);
    const RtPathTraceRestirPassPlan restirPTPassPlan = BuildPathTraceRestirPassPlan(debugMode, restirPTPreviewVisibility);
    const PathTraceIntegratorSettings integratorSettings = ApplyPathTraceSafetyKillSwitches(BuildPathTraceIntegratorSettings(), safetyDisableMask);
    const RtPathTraceDebugModeInfo debugModeInfo = GetPathTraceDebugModeInfo(debugMode);

    idVec3 cameraOrigin = viewDef->renderView.vieworg;
    idVec3 cameraForward = viewDef->renderView.viewaxis[0];
    idVec3 cameraLeft = viewDef->renderView.viewaxis[1];
    idVec3 cameraUp = viewDef->renderView.viewaxis[2];
    cameraForward.Normalize();
    cameraLeft.Normalize();
    cameraUp.Normalize();

    const int requestedLightCount = idMath::ClampInt(0, RT_SMOKE_MAX_DEBUG_LIGHTS, r_pathTracingLightCount.GetInteger());
    const int toyLightTraceCap = idMath::ClampInt(0, RT_SMOKE_MAX_DEBUG_LIGHTS, r_pathTracingToyLightTraceCap.GetInteger());
    const int selectedLightRequestCount = debugMode == 18 ? Min(requestedLightCount, toyLightTraceCap) : requestedLightCount;
    const int lightSelectionMode = idMath::ClampInt(0, 1, r_pathTracingLightSelection.GetInteger());
    float toyMaxRayDistance = idMath::ClampFloat(64.0f, 100000.0f, r_pathTracingToyMaxRayDistance.GetFloat());
    if (r_pathTracingSceneSource.GetInteger() == 2)
    {
        toyMaxRayDistance = 100000.0f;
    }

    uint64 accumulationSignature = 1469598103934665603ull;
    accumulationSignature = HashSmokeBytes(accumulationSignature, &debugMode, sizeof(debugMode));
    accumulationSignature = HashSmokeBytes(accumulationSignature, &m_frameResources.width, sizeof(m_frameResources.width));
    accumulationSignature = HashSmokeBytes(accumulationSignature, &m_frameResources.height, sizeof(m_frameResources.height));
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraOrigin.x, 100.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraOrigin.y, 100.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraOrigin.z, 100.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraForward.x, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraForward.y, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraForward.z, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraLeft.x, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraLeft.y, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraLeft.z, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraUp.x, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraUp.y, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, cameraUp.z, 10000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, viewDef->renderView.fov_x, 100.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, viewDef->renderView.fov_y, 100.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, r_forceAmbient.GetFloat(), 1000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, r_pathTracingToyLightScale.GetFloat(), 1000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, r_pathTracingToyEmissiveScale.GetFloat(), 1000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, r_pathTracingAnalyticLightIntensityScale.GetFloat(), 1000.0f);
    accumulationSignature = HashSmokeFloatQuantized(accumulationSignature, toyMaxRayDistance, 10.0f);
    accumulationSignature = HashSmokeDispatchValue(accumulationSignature, static_cast<uint64>(m_smokeDoomAnalyticLightCount));
    accumulationSignature = HashSmokeDispatchValue(accumulationSignature, static_cast<uint64>(idMath::ClampInt(0, 1024, r_pathTracingAnalyticLightMaxGpu.GetInteger())));
    accumulationSignature = HashSmokeDispatchValue(
        accumulationSignature,
        static_cast<uint64>(idMath::ClampInt(1, 16, r_pathTracingReservoirCandidateTrials.GetInteger())));
    accumulationSignature = HashSmokeDispatchValue(accumulationSignature, static_cast<uint64>(requestedLightCount));
    accumulationSignature = HashSmokeDispatchValue(
        accumulationSignature,
        static_cast<uint64>(idMath::ClampInt(0, RT_SMOKE_MAX_DEBUG_LIGHTS, r_pathTracingToyLightTraceCap.GetInteger())));
    accumulationSignature = HashSmokeDispatchValue(accumulationSignature, static_cast<uint64>(lightSelectionMode));
    accumulationSignature = HashSmokeDispatchValue(accumulationSignature, static_cast<uint64>(r_pathTracingToyFakePBRSpecular.GetInteger() != 0 ? 1 : 0));
    accumulationSignature = HashSmokeDispatchValue(accumulationSignature, static_cast<uint64>(r_pathTracingToyAccumulation.GetInteger() != 0 ? 1 : 0));
    accumulationSignature = HashSmokeDispatchValue(accumulationSignature, static_cast<uint64>(safetyDisableMask));
    accumulationSignature = HashPathTraceIntegratorSettings(accumulationSignature, integratorSettings);
    if (debugMode != 18 || r_pathTracingToyAccumulation.GetInteger() == 0 || accumulationSignature != m_frameResources.smokeAccumulationSignature)
    {
        m_frameResources.smokeAccumulationSignature = accumulationSignature;
        m_frameResources.smokeAccumulationFrameCount = 0;
    }
    const int accumulationMaxFrames = idMath::ClampInt(1, 4096, r_pathTracingToyAccumMaxFrames.GetInteger());
    const int accumulationFrameCount = debugMode == 18 && r_pathTracingToyAccumulation.GetInteger() != 0
        ? Min(m_frameResources.smokeAccumulationFrameCount, accumulationMaxFrames - 1)
        : 0;

    uint64 reservoirDispatchSignature = 1469598103934665603ull;
    reservoirDispatchSignature = HashSmokeBytes(reservoirDispatchSignature, &m_frameResources.smokeReservoirSceneSignature, sizeof(m_frameResources.smokeReservoirSceneSignature));
    reservoirDispatchSignature = HashSmokeBytes(reservoirDispatchSignature, &m_frameResources.width, sizeof(m_frameResources.width));
    reservoirDispatchSignature = HashSmokeBytes(reservoirDispatchSignature, &m_frameResources.height, sizeof(m_frameResources.height));
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraOrigin.x, 100.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraOrigin.y, 100.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraOrigin.z, 100.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraForward.x, 10000.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraForward.y, 10000.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraForward.z, 10000.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraLeft.x, 10000.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraLeft.y, 10000.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraLeft.z, 10000.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraUp.x, 10000.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraUp.y, 10000.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, cameraUp.z, 10000.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, viewDef->renderView.fov_x, 100.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, viewDef->renderView.fov_y, 100.0f);
    reservoirDispatchSignature = HashSmokeFloatQuantized(reservoirDispatchSignature, r_pathTracingAnalyticLightIntensityScale.GetFloat(), 1000.0f);
    reservoirDispatchSignature = HashSmokeDispatchValue(reservoirDispatchSignature, static_cast<uint64>(m_smokeDoomAnalyticLightCount));
    reservoirDispatchSignature = HashSmokeDispatchValue(reservoirDispatchSignature, static_cast<uint64>(idMath::ClampInt(0, 1024, r_pathTracingAnalyticLightMaxGpu.GetInteger())));
    if (reservoirDispatchSignature != m_frameResources.smokeReservoirDispatchSignature)
    {
        m_frameResources.smokeReservoirDispatchSignature = reservoirDispatchSignature;
        m_frameResources.smokeReservoirNeedsClear = true;
        ++m_frameResources.smokeReservoirResetCount;
        m_frameResources.MarkResetReason(RT_FRAME_RESET_RESERVOIR_DISPATCH_SIGNATURE);
    }

    if (r_pathTracingReservoirDump.GetInteger() != 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke reservoirs output=%dx%d records=%d bytes=%d sceneSig=%llu dispatchSig=%llu needsClear=%d resets=%d clears=%d emissive=%d/%d/%d lightCandidates=%d/%d(%db) doomAnalytic=%d(%db)\n",
            m_frameResources.width,
            m_frameResources.height,
            m_frameResources.smokeReservoirBuffers.reservoirCount,
            m_frameResources.smokeReservoirBuffers.reservoirBytes,
            static_cast<unsigned long long>(m_frameResources.smokeReservoirSceneSignature),
            static_cast<unsigned long long>(m_frameResources.smokeReservoirDispatchSignature),
            m_frameResources.smokeReservoirNeedsClear ? 1 : 0,
            m_frameResources.smokeReservoirResetCount,
            m_frameResources.smokeReservoirClearCount,
            m_smokeEmissiveTriangleCount,
            m_smokeEmissiveStaticTriangleCount,
            m_smokeEmissiveDynamicTriangleCount,
            m_smokeLightCandidateCount,
            m_smokeTexturedLightCandidateCount,
            m_smokeLightCandidateBytes,
            m_smokeDoomAnalyticLightCount,
            m_smokeDoomAnalyticLightBytes);
        m_frameResources.PrintDiagnostics("PathTracePrimaryPass");
        r_pathTracingReservoirDump.SetInteger(0);
    }

    const uint64 setupCompleteUs = Sys_Microseconds();
    const uint32_t restirPTFrameIndex = m_frameResources.restirPTFrameIndex++;
    m_frameResources.settings.frameIndex = restirPTFrameIndex;
    const RtRestirPTContextUpdateDesc restirPTContextDesc = BuildRestirPTContextUpdateDesc(
        restirPTPassPlan,
        static_cast<uint32_t>(m_frameResources.width),
        static_cast<uint32_t>(m_frameResources.height),
        restirPTFrameIndex,
        rtxdi::CheckerboardMode::Off,
        idMath::ClampFloat(0.0f, 1.0f, r_pathTracingRestirPTTemporalDepthThreshold.GetFloat()),
        idMath::ClampFloat(-1.0f, 1.0f, r_pathTracingRestirPTTemporalNormalThreshold.GetFloat()),
        r_pathTracingRestirPTTemporalReservoirReuse.GetInteger() != 0,
        r_pathTracingRestirPTTemporalFallbackSampling.GetInteger() != 0,
        static_cast<uint32_t>(idMath::ClampInt(1, 32, r_pathTracingRestirPTSpatialSamples.GetInteger())),
        idMath::ClampFloat(1.0f, 128.0f, r_pathTracingRestirPTSpatialRadius.GetFloat()));
    if (!UpdateRestirPTContextState(m_frameResources.restirPTContextState, restirPTContextDesc))
    {
        return;
    }
    const uint64 restirContextCompleteUs = Sys_Microseconds();
    const RtPathTraceRestirPassBufferSelection restirPTBufferSelection = ResolveRestirPTPassBufferSelection(
        restirPTPassPlan,
        m_frameResources.restirPTContextState.parameters);
    if (r_pathTracingRestirPTPassDump.GetInteger() != 0)
    {
        common->Printf("PathTracePrimaryPass: ReSTIR PT pass plan mode=%d label=%s producer=%s output=%s flags=0x%08x resampling=%d buffers initialOut=%u temporalIn=%u temporalOut=%u spatialIn=%u spatialOut=%u finalShadingIn=%u debugIn=%u previewVisibility=%d maxPixels=%d temporalThresholds depth=%.3f normal=%.3f temporalReuse=%d temporalFallback=%d spatial samples=%u radius=%.1f\n",
            debugMode,
            restirPTPassPlan.label,
            PathTraceRestirPassKindName(restirPTPassPlan.producer),
            PathTraceRestirPassKindName(restirPTPassPlan.output),
            restirPTPassPlan.flags,
            static_cast<int>(restirPTPassPlan.resamplingMode),
            restirPTBufferSelection.initialOutput,
            restirPTBufferSelection.temporalInput,
            restirPTBufferSelection.temporalOutput,
            restirPTBufferSelection.spatialInput,
            restirPTBufferSelection.spatialOutput,
            restirPTBufferSelection.finalShadingInput,
            restirPTBufferSelection.debugInput,
            (restirPTPassPlan.flags & RT_RESTIR_PASS_TRACES_VISIBILITY) != 0 ? 1 : 0,
            r_pathTracingRestirPTPreviewMaxPixels.GetInteger(),
            restirPTContextDesc.temporalDepthThreshold,
            restirPTContextDesc.temporalNormalThreshold,
            restirPTContextDesc.temporalReservoirReuse ? 1 : 0,
            restirPTContextDesc.temporalFallbackSampling ? 1 : 0,
            restirPTContextDesc.spatialSamples,
            restirPTContextDesc.spatialRadius);
        r_pathTracingRestirPTPassDump.SetInteger(0);
    }

    PathTraceSmokeConstants constants = {};
    constants.cameraOriginAndTMax[0] = cameraOrigin.x;
    constants.cameraOriginAndTMax[1] = cameraOrigin.y;
    constants.cameraOriginAndTMax[2] = cameraOrigin.z;
    constants.cameraOriginAndTMax[3] = 100000.0f;
    constants.cameraForwardAndTanX[0] = cameraForward.x;
    constants.cameraForwardAndTanX[1] = cameraForward.y;
    constants.cameraForwardAndTanX[2] = cameraForward.z;
    constants.cameraForwardAndTanX[3] = idMath::Tan(DEG2RAD(viewDef->renderView.fov_x * 0.5f));
    constants.cameraLeftAndTanY[0] = cameraLeft.x;
    constants.cameraLeftAndTanY[1] = cameraLeft.y;
    constants.cameraLeftAndTanY[2] = cameraLeft.z;
    constants.cameraLeftAndTanY[3] = idMath::Tan(DEG2RAD(viewDef->renderView.fov_y * 0.5f));
    constants.cameraUpAndDebugMode[0] = cameraUp.x;
    constants.cameraUpAndDebugMode[1] = cameraUp.y;
    constants.cameraUpAndDebugMode[2] = cameraUp.z;
    constants.cameraUpAndDebugMode[3] = static_cast<float>(debugMode);
    constants.textureInfo[0] = static_cast<float>(Max(0, static_cast<int>(m_smokeActiveTextureTable.size()) - 1));
    const int textureSampleMethod = r_pathTracingTextureSampleEnable.GetInteger() != 0
        ? idMath::ClampInt(0, 2, r_pathTracingTextureSampleMethod.GetInteger())
        : 0;
    constants.textureInfo[1] = static_cast<float>(textureSampleMethod);
    constants.textureInfo[2] = static_cast<float>(Max(0, m_smokeMaterialTableEntryCount));
    const bool integratorUsesSpecular = integratorSettings.reflectionMode > 0 || r_pathTracingToyFakePBRSpecular.GetInteger() != 0;
    const bool toyFakePBRSpecularEnabled = r_pathTracingToyFakePBRSpecular.GetInteger() != 0 && (debugMode == 18 || restirPTDebugMode || integratorDebugMode);
    const uint32_t textureFlags =
        (r_pathTracingTextureBindlessEnable.GetInteger() != 0 ? 1u : 0u) |
        (r_pathTracingTextureFilter.GetInteger() != 0 ? 2u : 0u) |
        (r_pathTracingTextureDecode.GetInteger() != 0 ? 4u : 0u) |
        (r_pathTracingUseNormalMaps.GetInteger() != 0 && (debugMode == 14 || debugMode == 18 || debugMode == 20 || restirPTDebugMode || integratorDebugMode) ? 8u : 0u) |
        (r_pathTracingUseSpecularMaps.GetInteger() != 0 && (debugMode == 14 || (integratorUsesSpecular && (debugMode == 18 || restirPTDebugMode || integratorDebugMode))) ? 16u : 0u) |
        (r_pathTracingUseEmissiveMaps.GetInteger() != 0 && (debugMode == 14 || debugMode == 18 || debugMode == 19 || debugMode == 20 || restirPTDebugMode || integratorDebugMode) ? 32u : 0u) |
        (r_pathTracingReservoirTwoSidedEmissives.GetInteger() != 0 && (debugMode == 20 || restirPTDebugMode) ? 64u : 0u) |
        (toyFakePBRSpecularEnabled ? 128u : 0u);
    constants.textureInfo[3] = static_cast<float>(textureFlags);
    const bool previousHistoryViewValid =
        !disablePrimarySurfaceHistory &&
        m_frameResources.primarySurfaceHistoryView.valid &&
        m_frameResources.primarySurfaceHistoryView.width == m_frameResources.width &&
        m_frameResources.primarySurfaceHistoryView.height == m_frameResources.height &&
        !m_frameResources.primarySurfaceHistoryNeedsClear;
    constants.prevCameraOriginAndValid[0] = m_frameResources.primarySurfaceHistoryView.origin.x;
    constants.prevCameraOriginAndValid[1] = m_frameResources.primarySurfaceHistoryView.origin.y;
    constants.prevCameraOriginAndValid[2] = m_frameResources.primarySurfaceHistoryView.origin.z;
    constants.prevCameraOriginAndValid[3] = previousHistoryViewValid ? 1.0f : 0.0f;
    constants.prevCameraForwardAndTanX[0] = m_frameResources.primarySurfaceHistoryView.forward.x;
    constants.prevCameraForwardAndTanX[1] = m_frameResources.primarySurfaceHistoryView.forward.y;
    constants.prevCameraForwardAndTanX[2] = m_frameResources.primarySurfaceHistoryView.forward.z;
    constants.prevCameraForwardAndTanX[3] = m_frameResources.primarySurfaceHistoryView.tanX;
    constants.prevCameraLeftAndTanY[0] = m_frameResources.primarySurfaceHistoryView.left.x;
    constants.prevCameraLeftAndTanY[1] = m_frameResources.primarySurfaceHistoryView.left.y;
    constants.prevCameraLeftAndTanY[2] = m_frameResources.primarySurfaceHistoryView.left.z;
    constants.prevCameraLeftAndTanY[3] = m_frameResources.primarySurfaceHistoryView.tanY;
    constants.prevCameraUpAndTanY[0] = m_frameResources.primarySurfaceHistoryView.up.x;
    constants.prevCameraUpAndTanY[1] = m_frameResources.primarySurfaceHistoryView.up.y;
    constants.prevCameraUpAndTanY[2] = m_frameResources.primarySurfaceHistoryView.up.z;
    constants.prevCameraUpAndTanY[3] = m_frameResources.primarySurfaceHistoryView.tanY;
    RtSmokeSelectedLight selectedLights[RT_SMOKE_MAX_DEBUG_LIGHTS];
    const bool restirPTAnalyticLightCandidates = restirPTDebugMode && r_pathTracingRestirPTAnalyticLightCandidates.GetInteger() != 0;
    const bool enableDoomAnalyticLights = !disableAnalyticLightLoop && (r_pathTracingAnalyticLightCandidates.GetInteger() != 0 || restirPTAnalyticLightCandidates);
    const bool replaceSelectedLightsWithAnalytic = enableDoomAnalyticLights && r_pathTracingAnalyticLightReplaceSelected.GetInteger() != 0;
    const int selectedLightCount = !disableSelectedLightLoop && (debugMode == 14 || debugMode == 15 || debugMode == 18) && !replaceSelectedLightsWithAnalytic
        ? CollectSelectedSmokePointLights(viewDef, cameraOrigin, selectedLights, selectedLightRequestCount, lightSelectionMode)
        : 0;
    const int analyticLightTraceCount = enableDoomAnalyticLights ? Min(m_smokeDoomAnalyticLightCount, idMath::ClampInt(0, 1024, r_pathTracingAnalyticLightMaxGpu.GetInteger())) : 0;
    const int estimatedRaysPerPixel = EstimatePathTraceRaysPerPixel(integratorSettings, selectedLightCount, analyticLightTraceCount);
    const PathTraceDispatchTileSettings dispatchTileSettings = BuildPathTraceDispatchTileSettings(m_frameResources.width, m_frameResources.height, estimatedRaysPerPixel);
    constants.lightInfo[0] = static_cast<float>(selectedLightCount);
    constants.lightInfo[1] = static_cast<float>(lightSelectionMode);
    constants.lightInfo[2] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingSmokeParticleAlphaScale.GetFloat());
    constants.lightInfo[3] =
        (r_pathTracingSmokeParticleDither.GetInteger() != 0 ? 1.0f : 0.0f) +
        (r_pathTracingSmokeParticleEdgeFade.GetInteger() != 0 ? 2.0f : 0.0f);
    constants.portalWindowInfo[0] = r_pathTracingPortalWindowStochastic.GetInteger() != 0 ? 1.0f : 0.0f;
    constants.portalWindowInfo[1] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingPortalWindowAlphaScale.GetFloat());
    constants.portalWindowInfo[2] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingPortalWindowMinOpacity.GetFloat());
    constants.portalWindowInfo[3] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingPortalWindowShadowOpacity.GetFloat());
    constants.lightSpriteInfo[0] = r_pathTracingLightSpriteProxies.GetInteger() != 0 ? 1.0f : 0.0f;
    constants.lightSpriteInfo[1] = idMath::ClampFloat(0.001f, 0.25f, r_pathTracingLightSpriteRadiusScale.GetFloat());
    constants.lightSpriteInfo[2] = idMath::ClampFloat(0.0f, 16.0f, r_pathTracingLightSpriteIntensity.GetFloat());
    constants.lightSpriteInfo[3] = idMath::ClampFloat(0.0f, 1.0f, r_forceAmbient.GetFloat());
    constants.toyPathInfo[0] = toyMaxRayDistance;
    constants.toyPathInfo[1] = idMath::ClampFloat(0.0f, 16.0f, r_pathTracingToyLightScale.GetFloat());
    constants.toyPathInfo[2] = idMath::ClampFloat(0.0f, 32.0f, r_pathTracingToyEmissiveScale.GetFloat());
    constants.toyPathInfo[3] = static_cast<float>(accumulationFrameCount);
    constants.emissiveInfo[0] = static_cast<float>(disableEmissiveTriangleSampling ? 0 : m_smokeEmissiveTriangleCount);
    constants.emissiveInfo[1] = static_cast<float>(disableEmissiveTriangleSampling ? 0 : m_smokeEmissiveStaticTriangleCount);
    constants.emissiveInfo[2] = static_cast<float>(idMath::ClampInt(1, 16, r_pathTracingReservoirCandidateTrials.GetInteger()));
    constants.emissiveInfo[3] = static_cast<float>(disableEmissiveTriangleSampling ? 0 : m_smokeLightCandidateCount);
    const bool enableGpuBoundsOverlay = r_pathTracingSceneBoundsOverlayGpu.GetInteger() != 0;
    const bool enableBoundsBoxDebugMode = debugMode == 21 || debugMode == 22;
    const int gpuBoundsOverlayLineCount = (enableGpuBoundsOverlay || enableBoundsBoxDebugMode) ? idMath::ClampInt(0, RT_PT_BOUNDS_OVERLAY_MAX_LINES, m_smokeBoundsOverlayLineCount) : 0;
    constants.boundsOverlayInfo[0] = static_cast<float>(gpuBoundsOverlayLineCount);
    constants.boundsOverlayInfo[1] = 1.35f;
    constants.boundsOverlayInfo[2] = enableGpuBoundsOverlay ? 1.0f : 0.0f;
    constants.boundsOverlayInfo[3] = 0.0f;
    constants.doomAnalyticLightInfo[0] = static_cast<float>(disableAnalyticLightLoop ? 0 : m_smokeDoomAnalyticLightCount);
    constants.doomAnalyticLightInfo[1] = static_cast<float>(idMath::ClampInt(0, 1024, r_pathTracingAnalyticLightMaxGpu.GetInteger()));
    constants.doomAnalyticLightInfo[2] = idMath::ClampFloat(0.0f, 16.0f, r_pathTracingAnalyticLightIntensityScale.GetFloat());
    constants.doomAnalyticLightInfo[3] =
        (enableDoomAnalyticLights ? 1.0f : 0.0f) +
        (replaceSelectedLightsWithAnalytic ? 2.0f : 0.0f);
    constants.doomAnalyticLightRemapInfo[0] = static_cast<float>(m_smokeDoomAnalyticCurrentIdentityCount);
    constants.doomAnalyticLightRemapInfo[1] = static_cast<float>(m_smokeDoomAnalyticPreviousIdentityCount);
    constants.doomAnalyticLightRemapInfo[2] = static_cast<float>(m_smokeDoomAnalyticRemapCount);
    constants.doomAnalyticLightRemapInfo[3] = static_cast<float>(m_smokePreviousEmissiveTriangleCount);
    constants.restirPTInfo[0] = static_cast<float>(restirPTFrameIndex);
    constants.restirPTInfo[1] = r_pathTracingNormalMapFlipGreen.GetInteger() != 0 ? 1.0f : 0.0f;
    constants.restirPTInfo[2] = (restirPTPassPlan.flags & RT_RESTIR_PASS_TRACES_VISIBILITY) != 0 ? 1.0f : 0.0f;
    constants.restirPTInfo[3] = idMath::ClampFloat(0.0f, 16.0f, r_pathTracingRestirPTPreviewExposure.GetFloat());
    constants.integratorInfo[0] = static_cast<float>(integratorSettings.samplesPerPixel);
    constants.integratorInfo[1] = static_cast<float>(integratorSettings.maxPathDepth);
    constants.integratorInfo[2] = static_cast<float>(integratorSettings.diffuseBounceLimit);
    constants.integratorInfo[3] = static_cast<float>(integratorSettings.specularBounceLimit);
    constants.integratorInfo2[0] = static_cast<float>(integratorSettings.transmissionBounceLimit);
    constants.integratorInfo2[1] = static_cast<float>(integratorSettings.reflectionMode);
    constants.integratorInfo2[2] = static_cast<float>(integratorSettings.russianRouletteDepth);
    constants.integratorInfo2[3] = static_cast<float>(integratorSettings.nextEventEstimation);
    constants.neeInfo[0] = static_cast<float>(integratorSettings.secondaryNeeMode);
    constants.neeInfo[1] = static_cast<float>(integratorSettings.secondaryNeeVisibility);
    constants.neeInfo[2] = static_cast<float>(integratorSettings.secondaryAnalyticNeeMode);
    constants.neeInfo[3] = static_cast<float>(integratorSettings.secondaryAnalyticNeeSamples);
    constants.motionVectorInfo[0] = motionVectorExportEnabled ? 1.0f : 0.0f;
    constants.motionVectorInfo[1] = r_pathTracingRestirPTTemporalAnalyticNeeReuse.GetInteger() != 0 ? 1.0f : 0.0f;
    constants.motionVectorInfo[2] = static_cast<float>(idMath::ClampInt(1, 128, r_pathTracingRestirPTAnalyticLightTrials.GetInteger()));
    constants.motionVectorInfo[3] = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingRestirPTTemporalAnalyticLightChangeTolerance.GetFloat());
    constants.safetyInfo[0] = static_cast<float>(safetyDisableMask);
    constants.safetyInfo[1] = static_cast<float>(Max(0, static_cast<int>(m_smokeActiveTextureTable.size()) - 1));
    constants.safetyInfo[2] = static_cast<float>(idMath::ClampInt(0, 1, r_pathTracingRestirPTSpatialDiagnosticView.GetInteger()));
    constants.safetyInfo[3] = 0.0f;
    constants.geometryInfo0[0] = static_cast<float>(Max(0, m_sceneInputs.geometry.staticVertexCount));
    constants.geometryInfo0[1] = static_cast<float>(Max(0, m_sceneInputs.geometry.staticIndexCount));
    constants.geometryInfo0[2] = static_cast<float>(Max(0, m_sceneInputs.geometry.staticTriangleCount));
    constants.geometryInfo0[3] = static_cast<float>(Max(0, m_sceneInputs.geometry.dynamicVertexCount));
    constants.geometryInfo1[0] = static_cast<float>(Max(0, m_sceneInputs.geometry.dynamicIndexCount));
    constants.geometryInfo1[1] = static_cast<float>(Max(0, m_sceneInputs.geometry.dynamicTriangleCount));
    constants.geometryInfo1[2] = static_cast<float>(Max(0, m_sceneInputs.geometry.rigidRouteVertexCount));
    constants.geometryInfo1[3] = static_cast<float>(Max(0, m_sceneInputs.geometry.rigidRouteIndexCount));
    constants.geometryInfo2[0] = static_cast<float>(Max(0, m_sceneInputs.geometry.rigidRouteTriangleCount));
    constants.geometryInfo2[1] = static_cast<float>(Max(0, m_sceneInputs.geometry.rigidRouteInstanceCount));
    constants.geometryInfo2[2] = static_cast<float>(m_frameResources.primarySurfaceHistoryBuffers.surfaceCount);
    constants.geometryInfo2[3] = static_cast<float>(m_frameResources.smokeReservoirBuffers.reservoirCount);
    constants.geometryInfo3[0] = static_cast<float>(Max(0, m_sceneInputs.geometry.skinnedPreviousPositionCount));
    constants.geometryInfo3[1] = static_cast<float>(Max(0, m_sceneInputs.geometry.skinnedSurfaceDispatchCount));
    constants.geometryInfo3[2] = static_cast<float>(Max(0, m_sceneInputs.geometry.skinnedTriangleDispatchIndexCount));
    constants.geometryInfo3[3] = 0.0f;
    constants.geometryInfo4[0] = static_cast<float>(Max(0, m_sceneInputs.geometry.previousStaticVertexCount));
    constants.geometryInfo4[1] = static_cast<float>(Max(0, m_sceneInputs.geometry.previousStaticIndexCount));
    constants.geometryInfo4[2] = static_cast<float>(Max(0, m_sceneInputs.geometry.previousStaticTriangleCount));
    constants.geometryInfo4[3] = static_cast<float>(Max(0, m_sceneInputs.geometry.previousStaticMaterialIndexCount));
    constants.dispatchTileInfo[0] = 0.0f;
    constants.dispatchTileInfo[1] = 0.0f;
    constants.dispatchTileInfo[2] = static_cast<float>(Max(0, m_frameResources.width));
    constants.dispatchTileInfo[3] = static_cast<float>(Max(0, m_frameResources.height));
    if (r_pathTracingIntegratorDump.GetInteger() != 0)
    {
        const uint64 estimatedDispatchRays =
            static_cast<uint64>(Max(0, m_frameResources.width)) *
            static_cast<uint64>(Max(0, m_frameResources.height)) *
            static_cast<uint64>(Max(1, estimatedRaysPerPixel));
        common->Printf("PathTracePrimaryPass: PT integrator settings output=%dx%d spp=%d maxDepth=%d diffuse/spec/trans=%d/%d/%d reflectionMode=%d rrDepth=%d nee=%d secondaryNeeMode=%d secondaryNeeVisibility=%d secondaryAnalyticNeeMode=%d secondaryAnalyticNeeSamples=%d selectedLights=%d analyticLights=%d estimatedRaysPerPixel=%d estimatedDispatchRays=%llu\n",
            m_frameResources.width,
            m_frameResources.height,
            integratorSettings.samplesPerPixel,
            integratorSettings.maxPathDepth,
            integratorSettings.diffuseBounceLimit,
            integratorSettings.specularBounceLimit,
            integratorSettings.transmissionBounceLimit,
            integratorSettings.reflectionMode,
            integratorSettings.russianRouletteDepth,
            integratorSettings.nextEventEstimation,
            integratorSettings.secondaryNeeMode,
            integratorSettings.secondaryNeeVisibility,
            integratorSettings.secondaryAnalyticNeeMode,
            integratorSettings.secondaryAnalyticNeeSamples,
            selectedLightRequestCount,
            analyticLightTraceCount,
            estimatedRaysPerPixel,
            static_cast<unsigned long long>(estimatedDispatchRays));
        r_pathTracingIntegratorDump.SetInteger(0);
    }
    if (r_pathTracingSafetyDump.GetInteger() != 0)
    {
        const int activeDescriptorCount = Max(0, static_cast<int>(m_smokeActiveTextureTable.size()) - 1);
        common->Printf(
            "PathTracePrimaryPass: PT safety pre-dispatch output=%dx%d debugMode=%d spp=%d maxDepth=%d bounce diffuse/spec/trans=%d/%d/%d reflectionMode=%d rrDepth=%d nee=%d secondaryNeeMode=%d secondaryNeeVisibility=%d secondaryAnalyticNeeMode=%d secondaryAnalyticNeeSamples=%d killMask=0x%08x selectedLights actual/effective=%d/%d analytic actual/effective=%d/%d emissive actual/effective=%d/%d lightCandidates actual/effective=%d/%d materialEntries=%d activeTextureDescriptors=%d activeTextureTableSize=%d geometry static(v/i/t)=%d/%d/%d dynamic(v/i/t)=%d/%d/%d rigid(v/i/t/inst)=%d/%d/%d/%d AS tlas/static/dynamic=%d/%d/%d primaryHistory count/bytes=%u/%llu smokeReservoir count/bytes=%d/%llu restirReservoir elements/bytes=%u/%llu bindingSet=%d textureTable=%d\n",
            m_frameResources.width,
            m_frameResources.height,
            debugMode,
            integratorSettings.samplesPerPixel,
            integratorSettings.maxPathDepth,
            integratorSettings.diffuseBounceLimit,
            integratorSettings.specularBounceLimit,
            integratorSettings.transmissionBounceLimit,
            integratorSettings.reflectionMode,
            integratorSettings.russianRouletteDepth,
            integratorSettings.nextEventEstimation,
            integratorSettings.secondaryNeeMode,
            integratorSettings.secondaryNeeVisibility,
            integratorSettings.secondaryAnalyticNeeMode,
            integratorSettings.secondaryAnalyticNeeSamples,
            safetyDisableMask,
            disableSelectedLightLoop ? 0 : selectedLightRequestCount,
            selectedLightCount,
            m_smokeDoomAnalyticLightCount,
            analyticLightTraceCount,
            m_smokeEmissiveTriangleCount,
            disableEmissiveTriangleSampling ? 0 : m_smokeEmissiveTriangleCount,
            m_smokeLightCandidateCount,
            disableEmissiveTriangleSampling ? 0 : m_smokeLightCandidateCount,
            m_smokeMaterialTableEntryCount,
            activeDescriptorCount,
            static_cast<int>(m_smokeActiveTextureTable.size()),
            m_sceneInputs.geometry.staticVertexCount,
            m_sceneInputs.geometry.staticIndexCount,
            m_sceneInputs.geometry.staticTriangleCount,
            m_sceneInputs.geometry.dynamicVertexCount,
            m_sceneInputs.geometry.dynamicIndexCount,
            m_sceneInputs.geometry.dynamicTriangleCount,
            m_sceneInputs.geometry.rigidRouteVertexCount,
            m_sceneInputs.geometry.rigidRouteIndexCount,
            m_sceneInputs.geometry.rigidRouteTriangleCount,
            m_sceneInputs.geometry.rigidRouteInstanceCount,
            m_smokeTlas ? 1 : 0,
            m_smokeStaticBlas ? 1 : 0,
            m_smokeDynamicBlas ? 1 : 0,
            m_frameResources.primarySurfaceHistoryBuffers.surfaceCount,
            static_cast<unsigned long long>(m_frameResources.primarySurfaceHistoryBuffers.surfaceBytes),
            m_frameResources.smokeReservoirBuffers.reservoirCount,
            static_cast<unsigned long long>(m_frameResources.smokeReservoirBuffers.reservoirBytes),
            m_frameResources.restirPTReservoirBuffers.reservoirElementCount,
            static_cast<unsigned long long>(m_frameResources.restirPTReservoirBuffers.reservoirBytes),
            m_smokeBindingSet ? 1 : 0,
            m_smokeTextureDescriptorTable ? 1 : 0);
        r_pathTracingSafetyDump.SetInteger(0);
    }
    if (r_pathTracingDispatchTileDump.GetInteger() != 0)
    {
        common->Printf(
            "PathTracePrimaryPass: PT tiled dispatch enabled=%d output=%dx%d tile=%dx%d tileCount=%d (%dx%d) estimatedRaysPerPixel=%d estimatedRaysPerTile=%llu estimatedRaysFullFrame=%llu\n",
            dispatchTileSettings.enabled ? 1 : 0,
            m_frameResources.width,
            m_frameResources.height,
            dispatchTileSettings.tileWidth,
            dispatchTileSettings.tileHeight,
            dispatchTileSettings.tileCount,
            dispatchTileSettings.tileColumns,
            dispatchTileSettings.tileRows,
            estimatedRaysPerPixel,
            static_cast<unsigned long long>(dispatchTileSettings.estimatedRaysPerTile),
            static_cast<unsigned long long>(dispatchTileSettings.estimatedRaysFullFrame));
        r_pathTracingDispatchTileDump.SetInteger(0);
    }
    for (int i = 0; i < selectedLightCount; i++)
    {
        constants.lightOriginAndRadius[i][0] = selectedLights[i].origin.x;
        constants.lightOriginAndRadius[i][1] = selectedLights[i].origin.y;
        constants.lightOriginAndRadius[i][2] = selectedLights[i].origin.z;
        constants.lightOriginAndRadius[i][3] = selectedLights[i].radius;
        constants.lightColorAndIntensity[i][0] = selectedLights[i].color.x;
        constants.lightColorAndIntensity[i][1] = selectedLights[i].color.y;
        constants.lightColorAndIntensity[i][2] = selectedLights[i].color.z;
        constants.lightColorAndIntensity[i][3] = selectedLights[i].spriteProxy ? 1.0f : 0.0f;
    }
    if ((debugMode == 14 || debugMode == 15 || debugMode == 18) && r_pathTracingLightDump.GetInteger() != 0)
    {
        common->Printf("PathTracePrimaryPass: RT smoke selected %d debug point lights selection=%s\n",
            selectedLightCount,
            lightSelectionMode == 0 ? "nearest" : "cameraInfluence");
        for (int i = 0; i < selectedLightCount; i++)
        {
            common->Printf("  light[%d]: index=%d origin=(%.2f %.2f %.2f) radius=%.2f distance=%.2f score=%.6f color=(%.3f %.3f %.3f) intensity=%.3f sprite=%d shader='%s'\n",
                i,
                selectedLights[i].index,
                selectedLights[i].origin.x,
                selectedLights[i].origin.y,
                selectedLights[i].origin.z,
                selectedLights[i].radius,
                idMath::Sqrt(selectedLights[i].distanceSquared),
                selectedLights[i].score,
                selectedLights[i].color.x,
                selectedLights[i].color.y,
                selectedLights[i].color.z,
                selectedLights[i].color.w,
                selectedLights[i].spriteProxy ? 1 : 0,
                selectedLights[i].shaderName.c_str());
        }
        r_pathTracingLightDump.SetInteger(0);
    }
    RunPathTraceDoomLightDiagnostics(viewDef);

    const uint64 constantsStartUs = Sys_Microseconds();
    if (optickGpuMarkers)
    {
        OPTICK_GPU_EVENT("PT GPU Write Dispatch Constants");
        commandList->writeBuffer(m_smokeConstantsBuffer, &constants, sizeof(constants));
        commandList->writeBuffer(m_restirPTConstantsBuffer, &m_frameResources.restirPTContextState.parameters, sizeof(m_frameResources.restirPTContextState.parameters));
        if (gpuBoundsOverlayLineCount > 0 && !m_smokeBoundsOverlayLines.empty())
        {
            commandList->writeBuffer(m_smokeBoundsOverlayLineBuffer, m_smokeBoundsOverlayLines.data(), sizeof(RtPathTraceBoundsOverlayLine) * gpuBoundsOverlayLineCount);
        }
    }
    else
    {
        commandList->writeBuffer(m_smokeConstantsBuffer, &constants, sizeof(constants));
        commandList->writeBuffer(m_restirPTConstantsBuffer, &m_frameResources.restirPTContextState.parameters, sizeof(m_frameResources.restirPTContextState.parameters));
        if (gpuBoundsOverlayLineCount > 0 && !m_smokeBoundsOverlayLines.empty())
        {
            commandList->writeBuffer(m_smokeBoundsOverlayLineBuffer, m_smokeBoundsOverlayLines.data(), sizeof(RtPathTraceBoundsOverlayLine) * gpuBoundsOverlayLineCount);
        }
    }
    const uint64 constantsCompleteUs = Sys_Microseconds();
    const uint64 barrierStartUs = constantsCompleteUs;
    if (optickGpuMarkers)
    {
        OPTICK_GPU_EVENT("PT GPU Dispatch Resource Barriers");
        commandList->setBufferState(m_smokeStaticVertexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeStaticIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeStaticTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeStaticTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeStaticTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDynamicVertexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDynamicIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDynamicTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDynamicTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDynamicTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeMaterialTableBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeEmissiveTriangleBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeLightCandidateBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDoomAnalyticLightBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDoomAnalyticPreviousLightBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDoomAnalyticCurrentIdentityBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDoomAnalyticPreviousIdentityBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDoomAnalyticRemapBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeRigidRouteVertexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeRigidRouteIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeRigidRouteTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeRigidRouteTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeRigidRouteInstanceBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeBoundsOverlayLineBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_frameResources.smokeReservoirBuffers.current, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setBufferState(m_frameResources.smokeReservoirBuffers.previous, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setBufferState(m_frameResources.smokeReservoirBuffers.spatialScratch, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setBufferState(m_frameResources.restirPTReservoirBuffers.reservoirs, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setBufferState(m_frameResources.primarySurfaceHistoryBuffers.current, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setBufferState(m_frameResources.primarySurfaceHistoryBuffers.previous, nvrhi::ResourceStates::UnorderedAccess);
        for (nvrhi::TextureHandle texture : m_smokeActiveTextureTable)
        {
            if (texture)
            {
                commandList->setTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
            }
        }
        commandList->setTextureState(m_frameResources.outputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(m_frameResources.accumulationTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(m_frameResources.motionVectorTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(m_frameResources.motionVectorMaskTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->commitBarriers();
    }
    else
    {
        commandList->setBufferState(m_smokeStaticVertexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeStaticIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeStaticTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeStaticTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeStaticTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDynamicVertexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDynamicIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDynamicTriangleClassBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDynamicTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDynamicTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeMaterialTableBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeEmissiveTriangleBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeLightCandidateBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDoomAnalyticLightBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDoomAnalyticPreviousLightBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDoomAnalyticCurrentIdentityBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDoomAnalyticPreviousIdentityBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeDoomAnalyticRemapBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeRigidRouteVertexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeRigidRouteIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeRigidRouteTriangleMaterialBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeRigidRouteTriangleMaterialIndexBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeRigidRouteInstanceBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_smokeBoundsOverlayLineBuffer, nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(m_frameResources.smokeReservoirBuffers.current, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setBufferState(m_frameResources.smokeReservoirBuffers.previous, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setBufferState(m_frameResources.smokeReservoirBuffers.spatialScratch, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setBufferState(m_frameResources.restirPTReservoirBuffers.reservoirs, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setBufferState(m_frameResources.primarySurfaceHistoryBuffers.current, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setBufferState(m_frameResources.primarySurfaceHistoryBuffers.previous, nvrhi::ResourceStates::UnorderedAccess);
        for (nvrhi::TextureHandle texture : m_smokeActiveTextureTable)
        {
            if (texture)
            {
                commandList->setTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
            }
        }
        commandList->setTextureState(m_frameResources.outputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(m_frameResources.accumulationTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(m_frameResources.motionVectorTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->setTextureState(m_frameResources.motionVectorMaskTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
        commandList->commitBarriers();
    }
    const uint64 barrierCompleteUs = Sys_Microseconds();
    if (disableReservoirWrites)
    {
        m_frameResources.smokeReservoirNeedsClear = true;
        m_frameResources.restirPTReservoirNeedsClear = true;
    }
    if (disablePrimarySurfaceHistory &&
        (!m_frameResources.primarySurfaceHistoryNeedsClear ||
            m_frameResources.primarySurfaceHistoryView.valid ||
            m_frameResources.primarySurfaceHistoryState.currentValid ||
            m_frameResources.primarySurfaceHistoryState.previousValid))
    {
        m_frameResources.InvalidatePrimarySurfaceHistory(RT_FRAME_RESET_PRIMARY_HISTORY);
    }
    const bool reservoirClearRequested = !disableReservoirWrites && m_frameResources.smokeReservoirNeedsClear;
    const uint64 reservoirClearStartUs = barrierCompleteUs;
    if (reservoirClearRequested)
    {
        if (optickGpuMarkers)
        {
            OPTICK_GPU_EVENT("PT GPU Clear Reservoirs");
        }
        if (ClearSmokeReservoirBuffers(commandList, m_frameResources.smokeReservoirBuffers))
        {
            m_frameResources.smokeReservoirNeedsClear = false;
            ++m_frameResources.smokeReservoirClearCount;
        }
    }
    const uint64 reservoirClearCompleteUs = Sys_Microseconds();
    const bool restirPTReservoirClearRequested = !disableReservoirWrites && requestedRestirPTDebugMode && m_frameResources.restirPTReservoirNeedsClear;
    const uint64 restirPTReservoirClearStartUs = reservoirClearCompleteUs;
    if (restirPTReservoirClearRequested)
    {
        if (optickGpuMarkers)
        {
            OPTICK_GPU_EVENT("PT GPU Clear ReSTIR PT Reservoirs");
        }
        if (ClearRestirPTReservoirBuffers(commandList, m_frameResources.restirPTReservoirBuffers))
        {
            m_frameResources.restirPTReservoirNeedsClear = false;
            ++m_frameResources.restirPTReservoirClearCount;
        }
    }
    const uint64 restirPTReservoirClearCompleteUs = Sys_Microseconds();
    const bool primaryHistoryClearRequested = !disablePrimarySurfaceHistory && m_frameResources.primarySurfaceHistoryNeedsClear;
    const uint64 primaryHistoryClearStartUs = restirPTReservoirClearCompleteUs;
    if (primaryHistoryClearRequested)
    {
        if (optickGpuMarkers)
        {
            OPTICK_GPU_EVENT("PT GPU Clear Primary Surface History");
        }
        if (ClearRestirPTPrimarySurfaceHistoryBuffers(commandList, m_frameResources.primarySurfaceHistoryBuffers))
        {
            m_frameResources.primarySurfaceHistoryNeedsClear = false;
        }
    }
    const uint64 primaryHistoryClearCompleteUs = Sys_Microseconds();
    const uint64 targetClearStartUs = primaryHistoryClearCompleteUs;
    if (optickGpuMarkers)
    {
        OPTICK_GPU_EVENT("PT GPU Clear Dispatch Targets");
        commandList->clearTextureFloat(m_frameResources.outputTexture, nvrhi::AllSubresources, nvrhi::Color(0.25f, 0.50f, 0.75f, 1.0f));
        if (accumulationFrameCount == 0)
        {
            commandList->clearTextureFloat(m_frameResources.accumulationTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
        }
        if (!motionVectorExportEnabled)
        {
            commandList->clearTextureUInt(m_frameResources.motionVectorMaskTexture, nvrhi::AllSubresources, 0u);
        }
    }
    else
    {
        commandList->clearTextureFloat(m_frameResources.outputTexture, nvrhi::AllSubresources, nvrhi::Color(0.25f, 0.50f, 0.75f, 1.0f));
        if (accumulationFrameCount == 0)
        {
            commandList->clearTextureFloat(m_frameResources.accumulationTexture, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
        }
        if (!motionVectorExportEnabled)
        {
            commandList->clearTextureUInt(m_frameResources.motionVectorMaskTexture, nvrhi::AllSubresources, 0u);
        }
    }
    const uint64 targetClearCompleteUs = Sys_Microseconds();
    nvrhi::rt::DispatchRaysArguments args;
    args.width = m_frameResources.width;
    args.height = m_frameResources.height;
    args.depth = 1;
    int timingDispatchWidth = args.width;
    int timingDispatchHeight = args.height;

    auto dispatchSmokeRays = [&](const nvrhi::rt::DispatchRaysArguments& dispatchArgs)
    {
        if (dispatchTileSettings.enabled)
        {
            timingDispatchWidth = dispatchTileSettings.tileWidth;
            timingDispatchHeight = dispatchTileSettings.tileHeight;
            for (int tileY = 0; tileY < m_frameResources.height; tileY += dispatchTileSettings.tileHeight)
            {
                for (int tileX = 0; tileX < m_frameResources.width; tileX += dispatchTileSettings.tileWidth)
                {
                    PathTraceSmokeConstants tileConstants = constants;
                    tileConstants.dispatchTileInfo[0] = static_cast<float>(tileX);
                    tileConstants.dispatchTileInfo[1] = static_cast<float>(tileY);
                    commandList->writeBuffer(m_smokeConstantsBuffer, &tileConstants, sizeof(tileConstants));

                    nvrhi::rt::DispatchRaysArguments tileArgs;
                    tileArgs.width = Min(dispatchTileSettings.tileWidth, m_frameResources.width - tileX);
                    tileArgs.height = Min(dispatchTileSettings.tileHeight, m_frameResources.height - tileY);
                    tileArgs.depth = 1;
                    commandList->dispatchRays(tileArgs);
                }
            }
        }
        else
        {
            commandList->dispatchRays(dispatchArgs);
        }
    };

    const uint64 setStateStartUs = targetClearCompleteUs;
    const bool spatialNeedsTemporalPrepass =
        PathTraceRestirPassRequiresTemporalPrepass(restirPTPassPlan) &&
        m_smokeRestirShaderTable &&
        state.shaderTable != m_smokeRestirShaderTable;
    if (spatialNeedsTemporalPrepass)
    {
        // RTXDI spatial resampling reads completed current-frame neighbor
        // surfaces and temporal reservoirs. Produce those in a separate
        // dispatch before the spatial shader consumes them.
        nvrhi::rt::State temporalPrepassState = state;
        temporalPrepassState.shaderTable = m_smokeRestirShaderTable;
        if (optickGpuMarkers)
        {
            OPTICK_GPU_EVENT("PT GPU ReSTIR Spatial Temporal Prepass");
            commandList->setRayTracingState(temporalPrepassState);
            dispatchSmokeRays(args);
        }
        else
        {
            commandList->setRayTracingState(temporalPrepassState);
            dispatchSmokeRays(args);
        }

        nvrhi::utils::BufferUavBarrier(commandList, m_frameResources.primarySurfaceHistoryBuffers.current);
        nvrhi::utils::BufferUavBarrier(commandList, m_frameResources.restirPTReservoirBuffers.reservoirs);
        nvrhi::utils::TextureUavBarrier(commandList, m_frameResources.outputTexture);
    }

    if (optickGpuMarkers)
    {
        OPTICK_GPU_EVENT("PT GPU Set Ray Tracing State");
        commandList->setRayTracingState(state);
    }
    else
    {
        commandList->setRayTracingState(state);
    }
    const uint64 setStateCompleteUs = Sys_Microseconds();

    const uint64 dispatchRaysStartUs = setStateCompleteUs;
    if (dispatchTileSettings.enabled)
    {
        if (optickGpuMarkers)
        {
            OPTICK_GPU_EVENT("PT GPU Dispatch Ray Tiles");
        }
        dispatchSmokeRays(args);
    }
    else
    {
        if (optickGpuMarkers)
        {
            OPTICK_GPU_EVENT("PT GPU Dispatch Rays");
            dispatchSmokeRays(args);
        }
        else
        {
            dispatchSmokeRays(args);
        }
    }
    const uint64 dispatchRaysCompleteUs = Sys_Microseconds();
    const uint64 historyCopyStartUs = dispatchRaysCompleteUs;
    if (!disablePrimarySurfaceHistory)
    {
        commandList->setBufferState(m_frameResources.primarySurfaceHistoryBuffers.current, nvrhi::ResourceStates::CopySource);
        commandList->setBufferState(m_frameResources.primarySurfaceHistoryBuffers.previous, nvrhi::ResourceStates::CopyDest);
        commandList->commitBarriers();
        commandList->copyBuffer(
            m_frameResources.primarySurfaceHistoryBuffers.previous,
            0,
            m_frameResources.primarySurfaceHistoryBuffers.current,
            0,
            m_frameResources.primarySurfaceHistoryBuffers.surfaceBytes);
    }
    const uint64 historyCopyCompleteUs = Sys_Microseconds();
    if (!disablePrimarySurfaceHistory)
    {
        RtPathTraceFrameCameraState currentHistoryView;
        currentHistoryView.valid = true;
        currentHistoryView.width = m_frameResources.width;
        currentHistoryView.height = m_frameResources.height;
        currentHistoryView.origin = cameraOrigin;
        currentHistoryView.forward = cameraForward;
        currentHistoryView.left = cameraLeft;
        currentHistoryView.up = cameraUp;
        currentHistoryView.tanX = constants.cameraForwardAndTanX[3];
        currentHistoryView.tanY = constants.cameraLeftAndTanY[3];
        const bool objectMotionAvailable =
            (m_sceneInputs.geometry.skinnedPreviousPositionBufferAvailable && m_sceneInputs.geometry.skinnedSurfaceDispatchCount > 0) ||
            (m_sceneInputs.geometry.previousTransformAvailable && m_sceneInputs.geometry.rigidRouteInstanceCount > 0);
        m_frameResources.SetPrimarySurfaceHistoryView(currentHistoryView, objectMotionAvailable);
    }
    if (debugMode == 18 && r_pathTracingToyAccumulation.GetInteger() != 0)
    {
        m_frameResources.smokeAccumulationFrameCount = Min(m_frameResources.smokeAccumulationFrameCount + 1, accumulationMaxFrames);
    }
    else
    {
        m_frameResources.smokeAccumulationFrameCount = 0;
    }
    const bool forceOverlapReadback = debugMode == 24 && r_pathTracingRigidRouteOverlapDump.GetInteger() != 0;
    bool readbackQueuedThisFrame = false;
    const uint64 readbackCopyStartUs = historyCopyCompleteUs;
    if ((r_pathTracingReadbackEnable.GetInteger() != 0 || forceOverlapReadback) && !m_frameResources.readbackQueued && (m_frameResources.readbackCooldownFrames <= 0 || forceOverlapReadback))
    {
        if (optickGpuMarkers)
        {
            OPTICK_GPU_EVENT("PT GPU Readback Copy");
            commandList->setTextureState(m_frameResources.outputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource);
            commandList->commitBarriers();
            commandList->copyTexture(m_frameResources.readbackTexture, nvrhi::TextureSlice(), m_frameResources.outputTexture, nvrhi::TextureSlice());
        }
        else
        {
            commandList->setTextureState(m_frameResources.outputTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::CopySource);
            commandList->commitBarriers();
            commandList->copyTexture(m_frameResources.readbackTexture, nvrhi::TextureSlice(), m_frameResources.outputTexture, nvrhi::TextureSlice());
        }
        m_frameResources.readbackQueued = true;
        m_frameResources.readbackDelayFrames = 2;
        m_frameResources.RecordReadbackQueued();
        readbackQueuedThisFrame = true;
        if (r_pathTracingSmokeLog.GetInteger() != 0)
        {
            common->Printf("PathTracePrimaryPass: queued RT smoke UAV readback\n");
        }
    }
    const uint64 readbackCopyCompleteUs = Sys_Microseconds();

    const uint64 totalSubmitUs = readbackCopyCompleteUs - executeStartUs;
    const int totalSubmitMsForThrottle = static_cast<int>((totalSubmitUs + 999u) / 1000u);
    const bool forcePassTimingDump = r_pathTracingPassTimingDump.GetInteger() != 0;
    if (forcePassTimingDump || ShouldLogSmokeTiming(totalSubmitMsForThrottle, Sys_Milliseconds(), g_smokeLastDispatchTimingLogMs))
    {
        RtPathTraceDispatchTimingLogDesc timingDesc;
        timingDesc.totalSubmitMs = PathTraceMicrosecondsToMilliseconds(totalSubmitUs);
        timingDesc.setupMs = PathTraceMicrosecondsToMilliseconds(setupCompleteUs - executeStartUs);
        timingDesc.restirContextMs = PathTraceMicrosecondsToMilliseconds(restirContextCompleteUs - setupCompleteUs);
        timingDesc.constantsMs = PathTraceMicrosecondsToMilliseconds(constantsCompleteUs - constantsStartUs);
        timingDesc.barrierMs = PathTraceMicrosecondsToMilliseconds(barrierCompleteUs - barrierStartUs);
        timingDesc.reservoirClearMs = PathTraceMicrosecondsToMilliseconds(reservoirClearCompleteUs - reservoirClearStartUs);
        timingDesc.primaryHistoryClearMs = PathTraceMicrosecondsToMilliseconds(primaryHistoryClearCompleteUs - primaryHistoryClearStartUs);
        timingDesc.targetClearMs = PathTraceMicrosecondsToMilliseconds(targetClearCompleteUs - targetClearStartUs);
        timingDesc.setStateMs = PathTraceMicrosecondsToMilliseconds(setStateCompleteUs - setStateStartUs);
        timingDesc.dispatchSubmitMs = PathTraceMicrosecondsToMilliseconds(dispatchRaysCompleteUs - dispatchRaysStartUs);
        timingDesc.historyCopyMs = PathTraceMicrosecondsToMilliseconds(historyCopyCompleteUs - historyCopyStartUs);
        timingDesc.readbackCopyMs = PathTraceMicrosecondsToMilliseconds(readbackCopyCompleteUs - readbackCopyStartUs);
        timingDesc.outputWidth = m_frameResources.width;
        timingDesc.outputHeight = m_frameResources.height;
        timingDesc.dispatchWidth = timingDispatchWidth;
        timingDesc.dispatchHeight = timingDispatchHeight;
        timingDesc.debugMode = debugMode;
        timingDesc.samplesPerPixel = integratorSettings.samplesPerPixel;
        timingDesc.maxPathDepth = integratorSettings.maxPathDepth;
        timingDesc.estimatedRaysPerPixel = estimatedRaysPerPixel;
        timingDesc.selectedLights = selectedLightRequestCount;
        timingDesc.analyticLights = analyticLightTraceCount;
        timingDesc.restirResamplingMode = static_cast<int>(restirPTPassPlan.resamplingMode);
        timingDesc.restirPreviewVisibility = (restirPTPassPlan.flags & RT_RESTIR_PASS_TRACES_VISIBILITY) != 0 ? 1 : 0;
        timingDesc.restirPreviewMaxPixels = r_pathTracingRestirPTPreviewMaxPixels.GetInteger();
        timingDesc.reservoirClearRequested = reservoirClearRequested;
        timingDesc.primaryHistoryClearRequested = primaryHistoryClearRequested;
        timingDesc.readbackQueued = readbackQueuedThisFrame;
        timingDesc.optickGpuMarkers = optickGpuMarkers;
        timingDesc.debugModeInfo = &debugModeInfo;
        timingDesc.restirPassLabel = restirPTPassPlan.label;
        LogPathTraceDispatchTiming(timingDesc);
        if (forcePassTimingDump)
        {
            r_pathTracingPassTimingDump.SetInteger(0);
        }
    }

    if (!m_smokeTestDispatched)
    {
        common->Printf("PathTracePrimaryPass: dispatched RT smoke camera raygen (%dx%d, debugMode=%d)\n", m_frameResources.width, m_frameResources.height, debugMode);
    }
    m_smokeTestDispatched = true;
}
