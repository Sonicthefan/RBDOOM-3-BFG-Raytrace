// Purpose:
//     Supplies rbdoom data for ReSTIR PT RAB supplier contract diagnostic views.
//
// NVIDIA reference:
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_Surface.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightInfo.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightSampling.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_MISCallbacks.hlsli
//     E:\prog\references\RTXDI-main\Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\PathTracer\RAB_PathTracer.hlsli
//
// Current rbdoom supplier:
//     RAB surface/material helpers, light-domain loading/sampling, visibility
//     callbacks, path-tracer user data, and debug reservoir pages.
//
// Current deviation:
//     These helpers diagnose rbdoom's current RAB supplier state. They do not
//     implement missing suppliers or change reservoir behavior.
//
// Extraction rule:
//     This file must preserve existing behavior until a later correction task.

#ifndef RB_PATH_TRACING_RESTIR_REFERENCE_RAB_CONTRACT_DEBUG_HLSLI
#define RB_PATH_TRACING_RESTIR_REFERENCE_RAB_CONTRACT_DEBUG_HLSLI
float4 EvaluateRestirPTReferenceBrdfContractView(RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
    {
        return float4(0.45, 0.05, 0.75, 1.0);
    }

    const float3 specularF0 = GetSpecularF0(surface.material);
    const float specularMax = max(max(specularF0.r, specularF0.g), specularF0.b);
    const float roughness = saturate(GetRoughness(surface.material));
    if (specularMax <= 0.02)
    {
        return float4(0.02, 0.75, 0.12, 1.0);
    }
    if (roughness <= 0.08)
    {
        return float4(1.00, 0.05, 0.05, 1.0);
    }
    if (roughness <= 0.35)
    {
        return float4(1.00, 0.45, 0.00, 1.0);
    }
    return float4(0.95, 0.85, 0.05, 1.0);
}

float4 RestirPTReferenceNeeSampleContractColor(RAB_Surface surface, RTXDI_PTReservoir reservoir)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.04, 0.04, 0.04, 1.0);
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    if (reservoir.WeightSum <= 0.0 ||
        !all(reservoir.TargetFunction == reservoir.TargetFunction) ||
        RTXDI_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(0.75, 0.02, 0.02, 1.0);
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const uint lightIndex = RTXDI_SampledLightData_GetLightIndex(sampledLightData);
    if (lightIndex >= RAB_GetCurrentLightCount())
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(
        lightInfo,
        surface,
        RTXDI_SampledLightData_GetUVDataFloat2(sampledLightData));
    if (lightSample.valid == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (lightSample.solidAnglePdf <= 0.0 ||
        !all(lightSample.position == lightSample.position) ||
        !all(lightSample.radiance == lightSample.radiance))
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }

    if (lightSample.lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return float4(0.90, 0.35, 0.05, 1.0);
    }
    if (lightSample.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
    {
        return float4(0.05, 0.80, 0.95, 1.0);
    }
    return float4(0.05, 0.75, 0.12, 1.0);
}

float4 EvaluateRestirPTReferenceNeeSampleContractView(RAB_Surface surface, uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir reservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    return RestirPTReferenceNeeSampleContractColor(surface, reservoir);
}

float4 RestirPTReferencePathTraceMetadataColor(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const bool connectsToNee = RTXDI_ConnectsToNeeLight(reservoir);
    if (!connectsToNee)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (reservoir.PathLength == 0u)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    if (reservoir.RcVertexLength == reservoir.PathLength && reservoir.PartialJacobian <= 0.0)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (reservoir.RcVertexLength + 1u == reservoir.PathLength && reservoir.RcWiPdf <= 0.0)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (reservoir.RcVertexLength > reservoir.PathLength)
    {
        return float4(0.05, 0.62, 0.18, 1.0);
    }
    if (reservoir.PathLength <= 3u)
    {
        return float4(0.90, 0.35, 0.05, 1.0);
    }
    return float4(0.05, 0.80, 0.95, 1.0);
}

float4 EvaluateRestirPTReferencePathTraceMetadataView(uint2 pixel)
{
    const uint2 reservoirPixel = PathTraceFullPixelToRestirDirectPixel(pixel);
    const RTXDI_PTReservoir reservoir = LoadRestirPTInitialDirectReservoir(reservoirPixel);
    return RestirPTReferencePathTraceMetadataColor(reservoir);
}

float4 RestirPTReferenceNeeRecordScalarColor(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (reservoir.WeightSum <= 0.0 || reservoir.WeightSum != reservoir.WeightSum || abs(reservoir.WeightSum) > 65504.0)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (reservoir.PathLength == 0u || reservoir.RcVertexLength > reservoir.PathLength + 1u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }

    const float targetLuminance = RAB_Luminance(max(reservoir.TargetFunction, float3(0.0, 0.0, 0.0)));
    if (!all(reservoir.TargetFunction == reservoir.TargetFunction) || targetLuminance <= 0.0)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const bool connectsToLightVertex = reservoir.RcVertexLength == reservoir.PathLength;
    const bool connectsToPreLightVertex = reservoir.RcVertexLength + 1u == reservoir.PathLength;
    const bool packedLightData = isinf(reservoir.Radiance.x);
    if (connectsToLightVertex && !packedLightData)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (!connectsToLightVertex && packedLightData)
    {
        return float4(0.55, 0.22, 0.75, 1.0);
    }
    if (connectsToLightVertex && (!RAB_IsFinitePositive(reservoir.PartialJacobian)))
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (connectsToPreLightVertex && (!RAB_IsFinitePositive(reservoir.RcWiPdf) || !RAB_IsFinitePositive(reservoir.PartialJacobian)))
    {
        return float4(0.05, 0.80, 0.95, 1.0);
    }
    if (!packedLightData && (!all(reservoir.Radiance == reservoir.Radiance) || RAB_Luminance(max(reservoir.Radiance, float3(0.0, 0.0, 0.0))) <= 0.0))
    {
        return float4(0.95, 0.22, 0.04, 1.0);
    }

    if (connectsToLightVertex)
    {
        return float4(0.05, 0.75, 0.12, 1.0);
    }
    if (connectsToPreLightVertex)
    {
        return float4(0.00, 0.35, 0.55, 1.0);
    }
    return float4(0.05, 0.62, 0.18, 1.0);
}

float4 RestirPTReferencePathTracerEntryColor(RAB_Surface surface, uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 6u) / dimensions.x, 5u);
    const bool validSurface = RAB_IsSurfaceValid(surface);
    const bool diffuseSurface = validSurface && RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface);
    const bool neeEnabled = IntegratorInfo2.w >= 0.5;
    const bool secondaryRayDisabled = PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_DIFFUSE_SECONDARY_RAY);
    const uint maxPathDepth = clamp((uint)max(IntegratorInfo.y, 1.0), 1u, 4u);
    const uint diffuseBounceLimit = clamp((uint)max(IntegratorInfo.z, 0.0), 0u, 3u);
    const uint configuredSecondaryBounces = min(
        diffuseBounceLimit,
        maxPathDepth > 0u ? maxPathDepth - 1u : 0u);
    const uint debugMode = (uint)CameraUpAndDebugMode.w;
    const bool indirectInitialMode = debugMode >= 53u && debugMode <= 56u;
    const uint maxSecondaryBounces = indirectInitialMode ? max(configuredSecondaryBounces, 1u) : configuredSecondaryBounces;
    const bool secondaryConfigured = maxSecondaryBounces > 0u && !secondaryRayDisabled;
    const uint currentLightCount = RAB_GetCurrentLightCount();

    if (band == 0u)
    {
        if (!validSurface)
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
        return diffuseSurface ? float4(0.05, 0.75, 0.12, 1.0) : float4(0.55, 0.05, 0.80, 1.0);
    }
    if (band == 1u)
    {
        if (!diffuseSurface)
        {
            return validSurface ? float4(0.55, 0.05, 0.80, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
        }
        if (!neeEnabled)
        {
            return float4(0.85, 0.02, 0.02, 1.0);
        }
        if (currentLightCount == 0u)
        {
            return float4(1.0, 0.45, 0.0, 1.0);
        }
        return float4(0.05, 0.75, 0.12, 1.0);
    }
    if (band == 2u)
    {
        if (!diffuseSurface)
        {
            return validSurface ? float4(0.55, 0.05, 0.80, 1.0) : float4(0.0, 0.0, 0.0, 1.0);
        }
        return secondaryConfigured ? float4(0.05, 0.75, 0.12, 1.0) : float4(0.05, 0.18, 0.75, 1.0);
    }
    if (band == 3u)
    {
        return secondaryConfigured ? float4(0.85, 0.02, 0.02, 1.0) : float4(0.05, 0.62, 0.18, 1.0);
    }
    if (band == 4u)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    if (configuredSecondaryBounces > 1u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (indirectInitialMode && configuredSecondaryBounces == 0u)
    {
        return float4(0.00, 0.35, 0.55, 1.0);
    }
    return float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferenceRandomReplayMetadataColor(RTXDI_PTReservoir reservoir)
{
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (reservoir.PathLength == 0u)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (reservoir.RcVertexLength > reservoir.PathLength)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (reservoir.RcVertexLength <= 2u)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    if (reservoir.RandomSeed == 0u && reservoir.RandomIndex == 0u)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (reservoir.RandomSeed == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (reservoir.RandomIndex == 0xffu)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    return float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferenceRandomSamplerBridgeColor(uint2 pixel)
{
    const uint frameIndex = (uint)max(RestirPTInfo.x, 0.0);
    const uint passSeed = 0x7a53d1u;
    RTXDI_RandomSamplerState rtxdiPixelRng = RTXDI_InitRandomSampler(pixel, frameIndex, passSeed);
    RAB_RandomSamplerState rabPixelRng = RAB_InitRandomSampler(pixel, frameIndex, passSeed);
    if (rtxdiPixelRng.seed != rabPixelRng.seed || rtxdiPixelRng.index != rabPixelRng.index)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    RTXDI_RandomSamplerState rtxdiDirectRng = RTXDI_CreateRandomSamplerFromDirectSeed(0x12345678u, 37u);
    RAB_RandomSamplerState rabDirectRng = RAB_CreateRandomSamplerFromDirectSeed(0x12345678u, 37u);
    if (rtxdiDirectRng.seed != rabDirectRng.seed || rtxdiDirectRng.index != rabDirectRng.index)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    [unroll]
    for (uint i = 0u; i < 4u; ++i)
    {
        const float rtxdiValue = RTXDI_GetNextRandom(rtxdiPixelRng);
        const float rabValue = RAB_GetNextRandom(rabPixelRng);
        if (rtxdiValue != rabValue)
        {
            return float4(0.95, 0.85, 0.05, 1.0);
        }
        if (rtxdiPixelRng.seed != rabPixelRng.seed || rtxdiPixelRng.index != rabPixelRng.index)
        {
            return float4(1.0, 0.45, 0.0, 1.0);
        }
    }

    const float rtxdiDirectValue = RTXDI_GetNextRandom(rtxdiDirectRng);
    const float rabDirectValue = RAB_GetNextRandom(rabDirectRng);
    if (rtxdiDirectValue != rabDirectValue ||
        rtxdiDirectRng.seed != rabDirectRng.seed ||
        rtxdiDirectRng.index != rabDirectRng.index)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    return float4(0.05, 0.75, 0.12, 1.0);
}

bool RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType pathType, uint expectedFlag)
{
    RAB_PathTracerUserData ptud = RAB_EmptyPathTracerUserData();
    ptud.flags = RAB_PTUD_FLAG_ENVIRONMENT_MAP_MISS_UNBRIDGED;
    RAB_PathTracerUserDataSetPathType(ptud, pathType);
    const uint expectedFlags =
        RAB_PTUD_FLAG_ENVIRONMENT_MAP_MISS_UNBRIDGED |
        RAB_PTUD_FLAG_PATH_TYPE_SET |
        expectedFlag;
    return ptud.pathType == pathType && ptud.flags == expectedFlags;
}

float4 RestirPTReferencePathTracerUserDataBridgeColor()
{
    const RAB_PathTracerUserData emptyPtud = RAB_EmptyPathTracerUserData();
    if (emptyPtud.flags != 0u ||
        emptyPtud.pathType != 0u ||
        emptyPtud.reconnectionDenoiserHitDistance != 0.0 ||
        emptyPtud.lastBounceDenoiserHitDistance != 0.0)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    if (!RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_Initial, RAB_PTUD_FLAG_PATH_TYPE_INITIAL) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_Temporal, RAB_PTUD_FLAG_PATH_TYPE_TEMPORAL) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_TemporalInverse, RAB_PTUD_FLAG_PATH_TYPE_TEMPORAL_INVERSE) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_Spatial, RAB_PTUD_FLAG_PATH_TYPE_SPATIAL) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_SpatialInverse, RAB_PTUD_FLAG_PATH_TYPE_SPATIAL_INVERSE) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_DebugTemporalRetrace, RAB_PTUD_FLAG_PATH_TYPE_DEBUG_TEMPORAL_RETRACE) ||
        !RestirPTReferenceCheckPathTracerUserDataPathType(RTXDI_PTPathTraceInvocationType_DebugSpatialRetrace, RAB_PTUD_FLAG_PATH_TYPE_DEBUG_SPATIAL_RETRACE))
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    RAB_PathTracerUserData callbackPtud = RAB_EmptyPathTracerUserData();
    RAB_Surface surface = RAB_EmptySurface();
    RAB_SetSurfaceWorldPos(surface, float3(1.0, 2.0, 3.0));

    RTXDI_PTReservoir neighborSample = RTXDI_EmptyPTReservoir();
    neighborSample.TranslatedWorldPosition = float3(1.0, 2.0, 8.0);
    RAB_ReconnectionDenoiserCallback(neighborSample, surface, callbackPtud);
    if ((callbackPtud.flags & RAB_PTUD_FLAG_RECONNECTION_DENOISER_CALLBACK) == 0u ||
        abs(callbackPtud.reconnectionDenoiserHitDistance - 5.0) > 0.0001)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    RAB_LastBounceDenoiserCallback(float3(4.0, 6.0, 3.0), surface, callbackPtud);
    if ((callbackPtud.flags & RAB_PTUD_FLAG_LAST_BOUNCE_DENOISER_CALLBACK) == 0u ||
        abs(callbackPtud.lastBounceDenoiserHitDistance - 5.0) > 0.0001)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }

    RAB_NoteEnvironmentMapMissUnbridged(callbackPtud);
    if ((callbackPtud.flags & RAB_PTUD_FLAG_ENVIRONMENT_MAP_MISS_UNBRIDGED) == 0u)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    return float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferencePsrDenoiserPayloadColor(uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 6u) / dimensions.x, 5u);

    if (band == 0u)
    {
        const RAB_PathTracerUserData emptyPtud = RAB_EmptyPathTracerUserData();
        if (emptyPtud.flags != 0u ||
            emptyPtud.pathType != 0u ||
            emptyPtud.reconnectionDenoiserHitDistance != 0.0 ||
            emptyPtud.lastBounceDenoiserHitDistance != 0.0)
        {
            return float4(0.85, 0.02, 0.02, 1.0);
        }
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    if (band == 1u)
    {
        return RestirPTReferencePathTracerUserDataBridgeColor();
    }

    RAB_PathTracerUserData callbackPtud = RAB_EmptyPathTracerUserData();
    RAB_Surface surface = RAB_EmptySurface();
    RAB_SetSurfaceWorldPos(surface, float3(1.0, 2.0, 3.0));

    RTXDI_PTReservoir neighborSample = RTXDI_EmptyPTReservoir();
    neighborSample.TranslatedWorldPosition = float3(1.0, 2.0, 8.0);
    RAB_ReconnectionDenoiserCallback(neighborSample, surface, callbackPtud);
    const bool reconnectionScalarOk =
        ((callbackPtud.flags & RAB_PTUD_FLAG_RECONNECTION_DENOISER_CALLBACK) != 0u) &&
        abs(callbackPtud.reconnectionDenoiserHitDistance - 5.0) <= 0.0001;

    RAB_LastBounceDenoiserCallback(float3(4.0, 6.0, 3.0), surface, callbackPtud);
    const bool lastBounceScalarOk =
        ((callbackPtud.flags & RAB_PTUD_FLAG_LAST_BOUNCE_DENOISER_CALLBACK) != 0u) &&
        abs(callbackPtud.lastBounceDenoiserHitDistance - 5.0) <= 0.0001;

    if (band == 2u)
    {
        return reconnectionScalarOk ? float4(0.95, 0.85, 0.05, 1.0) : float4(0.85, 0.02, 0.02, 1.0);
    }
    if (band == 3u)
    {
        return lastBounceScalarOk ? float4(0.95, 0.85, 0.05, 1.0) : float4(0.85, 0.02, 0.02, 1.0);
    }
    if (band == 4u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }

    RAB_NoteEnvironmentMapMissUnbridged(callbackPtud);
    return ((callbackPtud.flags & RAB_PTUD_FLAG_ENVIRONMENT_MAP_MISS_UNBRIDGED) != 0u)
        ? float4(0.05, 0.62, 0.18, 1.0)
        : float4(0.85, 0.02, 0.02, 1.0);
}

float4 RestirPTReferencePathTracerContinuationPolicyColor(uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint band = min((pixel.x * 6u) / dimensions.x, 5u);
    const uint rrDepth = clamp((uint)max(IntegratorInfo2.z, 0.0), 0u, 8u);
    const uint maxPathDepth = clamp((uint)max(IntegratorInfo.y, 1.0), 1u, 4u);
    const uint diffuseBounceLimit = clamp((uint)max(IntegratorInfo.z, 0.0), 0u, 3u);
    const bool secondaryConfigured = min(diffuseBounceLimit, maxPathDepth > 0u ? maxPathDepth - 1u : 0u) > 0u &&
        !PathTraceSafetyDisabled(RT_PT_SAFETY_DISABLE_DIFFUSE_SECONDARY_RAY);
    const bool rrConfigured = rrDepth > 0u;

    if (band == 0u)
    {
        return rrConfigured ? float4(0.05, 0.75, 0.12, 1.0) : float4(0.05, 0.18, 0.75, 1.0);
    }
    if (band == 1u)
    {
        return rrConfigured ? float4(1.0, 0.45, 0.0, 1.0) : float4(0.05, 0.62, 0.18, 1.0);
    }
    if (band == 2u)
    {
        return rrConfigured && secondaryConfigured ? float4(0.85, 0.02, 0.02, 1.0) : float4(0.05, 0.62, 0.18, 1.0);
    }
    if (band == 3u)
    {
        return secondaryConfigured ? float4(1.0, 0.0, 0.75, 1.0) : float4(0.05, 0.62, 0.18, 1.0);
    }
    if (band == 4u)
    {
        return float4(0.55, 0.22, 0.75, 1.0);
    }

    return rrConfigured && secondaryConfigured
        ? float4(0.95, 0.85, 0.05, 1.0)
        : float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferenceVisibilityPolicyColor(RAB_Surface surface, RTXDI_PTReservoir reservoir)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const uint lightIndex = RTXDI_SampledLightData_GetLightIndex(sampledLightData);
    if (lightIndex >= RAB_GetCurrentLightCount())
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(
        lightInfo,
        surface,
        RTXDI_SampledLightData_GetUVDataFloat2(sampledLightData));
    if (lightSample.valid == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }

    float3 lightDir;
    float lightDistance;
    const bool geometryVisible = RAB_GetConservativeLightSampleGeometry(surface, lightSample, lightDir, lightDistance);
    if (!geometryVisible)
    {
        return float4(0.45, 0.05, 0.75, 1.0);
    }

    const bool candidateVisible = RAB_GetCandidateNeeVisibility(surface, lightSample);
    const bool selectedVisible = RAB_GetSelectedNeeVisibility(surface, lightSample);
    const bool strictVisible = RAB_GetConservativeVisibility(surface, lightSample);
    if (strictVisible && candidateVisible && selectedVisible)
    {
        return float4(0.05, 0.75, 0.12, 1.0);
    }
    if (!strictVisible && candidateVisible && selectedVisible)
    {
        return float4(0.05, 0.45, 0.95, 1.0);
    }
    if (!strictVisible && candidateVisible && !selectedVisible)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (!strictVisible && !candidateVisible && !selectedVisible)
    {
        return float4(0.95, 0.22, 0.04, 1.0);
    }
    return float4(1.0, 0.0, 0.75, 1.0);
}

float4 RestirPTReferenceLightSampleNumericColor(RAB_Surface surface, RTXDI_PTReservoir reservoir)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (!RTXDI_IsValidPTReservoir(reservoir))
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const RTXDI_SampledLightData sampledLightData = RTXDI_GetSampledLightData(reservoir);
    if (!RTXDI_SampledLightData_IsValidLightData(sampledLightData))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const uint lightIndex = RTXDI_SampledLightData_GetLightIndex(sampledLightData);
    if (lightIndex >= RAB_GetCurrentLightCount())
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(
        lightInfo,
        surface,
        RTXDI_SampledLightData_GetUVDataFloat2(sampledLightData));
    if (lightSample.valid == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (!all(lightSample.position == lightSample.position) || any(abs(lightSample.position) > 1.0e20))
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (!all(lightSample.radiance == lightSample.radiance) || any(abs(lightSample.radiance) > 65504.0))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (!RAB_IsFinitePositive(lightSample.solidAnglePdf))
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (!RAB_IsFinitePositive(lightSample.distance))
    {
        return float4(0.0, 0.75, 0.95, 1.0);
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    if (!all(lightDir == lightDir) || !RAB_IsFinitePositive(lightDistance))
    {
        return float4(0.45, 0.05, 0.75, 1.0);
    }

    const float targetPdf = RAB_GetLightSampleTargetPdfForSurface(lightSample, surface);
    if (targetPdf != targetPdf || abs(targetPdf) > 65504.0)
    {
        return float4(0.95, 0.22, 0.04, 1.0);
    }
    if (targetPdf <= 0.0)
    {
        return float4(0.05, 0.45, 0.95, 1.0);
    }

    return lightSample.lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE
        ? float4(0.05, 0.80, 0.95, 1.0)
        : float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferenceEmissiveHitMapColor(RAB_Surface surface)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const float visibleEmissive = RAB_Luminance(max(surface.material.emissiveRadiance, float3(0.0, 0.0, 0.0)));
    if (visibleEmissive <= 0.0)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const uint emissiveTriangleCount = RAB_GetCurrentEmissiveTriangleCount();
    if (emissiveTriangleCount == 0u)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    uint matchCount = 0u;
    uint firstMatch = RAB_INVALID_LIGHT_INDEX;
    bool dynamicMatch = false;
    [loop]
    for (uint lightIndex = 0u; lightIndex < emissiveTriangleCount; ++lightIndex)
    {
        const PathTraceSmokeEmissiveTriangle candidate = SmokeEmissiveTriangles[lightIndex];
        const bool identityMatch =
            candidate.instanceId == surface.instanceId &&
            candidate.primitiveIndex == surface.primitiveIndex &&
            candidate.materialId == surface.materialId;
        if (identityMatch)
        {
            if (firstMatch == RAB_INVALID_LIGHT_INDEX)
            {
                firstMatch = lightIndex;
            }
            dynamicMatch = dynamicMatch || ((candidate.padding0 & RT_SMOKE_EMISSIVE_TRIANGLE_HISTORY_DYNAMIC) != 0u);
            ++matchCount;
        }
    }

    if (matchCount == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (matchCount > 1u)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(firstMatch, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    if (!all(lightInfo.position == lightInfo.position) ||
        !all(lightInfo.radiance == lightInfo.radiance) ||
        lightInfo.area <= 0.0)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (RAB_Luminance(max(lightInfo.radiance, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    return dynamicMatch ? float4(0.05, 0.80, 0.95, 1.0) : float4(0.05, 0.75, 0.12, 1.0);
}

float4 RestirPTReferenceCurrentToPreviousLightFailureColor(uint lightIndex)
{
    const uint currentEmissiveTriangleCount = RAB_GetCurrentEmissiveTriangleCount();
    if (lightIndex < currentEmissiveTriangleCount)
    {
        const PathTraceEmissiveLightRemap remap = SmokeEmissiveRemap[lightIndex];
        if (!RAB_SmokeEmissiveRemapValid(remap))
        {
            return float4(0.55, 0.05, 0.80, 1.0);
        }
        if (remap.currentToPreviousIndex < 0)
        {
            return float4(1.0, 0.45, 0.0, 1.0);
        }
        return float4(0.95, 0.22, 0.04, 1.0);
    }

    const uint analyticCount = RAB_GetCurrentDoomAnalyticLightCount();
    const uint currentIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.x, 0.0);
    const uint previousIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.y, 0.0);
    const uint remapCount = (uint)max(DoomAnalyticLightRemapInfo.z, 0.0);
    const uint currentAnalyticIndex = lightIndex - currentEmissiveTriangleCount;
    if (currentAnalyticIndex >= analyticCount || currentAnalyticIndex >= currentIdentityCount)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    const PathTraceDoomAnalyticLightCandidateIdentity currentIdentity = DoomAnalyticCurrentIdentities[currentAnalyticIndex];
    if (!RAB_DoomAnalyticIdentitySampleable(currentIdentity))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (currentIdentity.universeIndex >= remapCount)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }

    const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[currentIdentity.universeIndex];
    if (!RAB_DoomAnalyticRemapValid(remap) || remap.currentToPreviousCandidateIndex < 0)
    {
        const uint reason = currentIdentity.invalidReasonFlags | remap.invalidReasonFlags;
        if ((reason & 0x00000001u) != 0u)
        {
            return float4(1.0, 0.45, 0.0, 1.0);
        }
        if ((reason & 0x00000004u) != 0u)
        {
            return float4(0.95, 0.85, 0.05, 1.0);
        }
        if ((reason & 0x00000010u) != 0u)
        {
            return float4(0.15, 0.25, 1.0, 1.0);
        }
        if ((reason & 0x00000400u) != 0u)
        {
            return float4(0.05, 0.45, 0.95, 1.0);
        }
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const uint previousAnalyticIndex = (uint)remap.currentToPreviousCandidateIndex;
    if (previousAnalyticIndex >= previousIdentityCount)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (!RAB_DoomAnalyticLightStateCompatible(DoomAnalyticLights[currentAnalyticIndex], DoomAnalyticPreviousLights[previousAnalyticIndex]))
    {
        return float4(0.05, 0.45, 0.95, 1.0);
    }

    return float4(0.55, 0.05, 0.80, 1.0);
}

float4 RestirPTReferenceLightDomainLoadColor(uint2 pixel)
{
    const uint2 dimensions = PathTraceFullOutputSize();
    const uint currentLightCount = RAB_GetCurrentLightCount();
    if (currentLightCount == 0u || dimensions.x == 0u || dimensions.y == 0u)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const bool previousFrame = pixel.y >= (dimensions.y >> 1u);
    const uint halfHeight = max(dimensions.y >> 1u, 1u);
    const uint localY = previousFrame ? pixel.y - halfHeight : pixel.y;
    const uint cellSize = 12u;
    const uint tileX = pixel.x / cellSize;
    const uint tileY = min(localY, halfHeight - 1u) / cellSize;
    const uint tilesX = max((dimensions.x + cellSize - 1u) / cellSize, 1u);
    const uint lightIndex = tileY * tilesX + tileX;
    if (lightIndex >= currentLightCount)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint emissiveCount = RAB_GetCurrentEmissiveTriangleCount();
    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, previousFrame);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        if (previousFrame)
        {
            return RestirPTReferenceCurrentToPreviousLightFailureColor(lightIndex);
        }
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    if (!all(lightInfo.position == lightInfo.position) || !all(lightInfo.radiance == lightInfo.radiance))
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (lightInfo.area <= 0.0 || lightInfo.weight <= 0.0)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (RAB_Luminance(max(lightInfo.radiance, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    return lightIndex < emissiveCount ? float4(0.05, 0.75, 0.12, 1.0) : float4(0.05, 0.80, 0.95, 1.0);
}

float4 RestirPTReferenceLightDomainSampleColor(RAB_Surface surface, uint2 pixel)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint2 dimensions = PathTraceFullOutputSize();
    const uint currentLightCount = RAB_GetCurrentLightCount();
    if (currentLightCount == 0u || dimensions.x == 0u || dimensions.y == 0u)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint cellSize = 12u;
    const uint tileX = pixel.x / cellSize;
    const uint tileY = pixel.y / cellSize;
    const uint tilesX = max((dimensions.x + cellSize - 1u) / cellSize, 1u);
    const uint lightIndex = tileY * tilesX + tileX;
    if (lightIndex >= currentLightCount)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint emissiveCount = RAB_GetCurrentEmissiveTriangleCount();
    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, float2(0.37, 0.61));
    if (lightSample.valid == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }
    if (!all(lightSample.position == lightSample.position) ||
        !all(lightSample.radiance == lightSample.radiance))
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (!RAB_IsFinitePositive(lightSample.solidAnglePdf) ||
        !RAB_IsFinitePositive(lightSample.distance))
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (RAB_Luminance(max(lightSample.radiance, float3(0.0, 0.0, 0.0))) <= 0.0)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    float3 lightDir;
    float lightDistance;
    RAB_GetLightDirDistance(surface, lightSample, lightDir, lightDistance);
    if (!all(lightDir == lightDir) || !RAB_IsFinitePositive(lightDistance))
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }

    const float targetPdf = RAB_GetLightSampleTargetPdfForSurface(lightSample, surface);
    if (targetPdf != targetPdf || abs(targetPdf) > 65504.0)
    {
        return float4(0.05, 0.45, 0.95, 1.0);
    }
    if (targetPdf <= 0.0)
    {
        if (!RAB_SurfaceSupportsOpaqueDiffuseBrdf(surface))
        {
            return float4(0.35, 0.05, 0.75, 1.0);
        }

        const float3 surfaceNormal = RAB_SafeNormalize(RAB_GetSurfaceNormal(surface), RAB_GetSurfaceGeoNormal(surface));
        const float3 surfaceGeoNormal = RAB_GetSurfaceGeoNormal(surface);
        const float3 surfaceViewDir = RAB_GetSurfaceViewDir(surface);
        if (dot(surfaceNormal, lightDir) <= 0.0)
        {
            return float4(0.02, 0.08, 0.45, 1.0);
        }
        if (dot(surfaceGeoNormal, lightDir) <= 0.0)
        {
            return float4(0.00, 0.35, 0.55, 1.0);
        }
        if (dot(surfaceNormal, surfaceViewDir) <= 0.0 || dot(surfaceGeoNormal, surfaceViewDir) <= 0.0)
        {
            return float4(0.55, 0.22, 0.75, 1.0);
        }
        return float4(0.15, 0.25, 1.0, 1.0);
    }

    return lightIndex < emissiveCount ? float4(0.05, 0.75, 0.12, 1.0) : float4(0.05, 0.80, 0.95, 1.0);
}

float4 RestirPTReferenceLightDomainVisibilityColor(RAB_Surface surface, uint2 pixel)
{
    if (!RAB_IsSurfaceValid(surface))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint2 dimensions = PathTraceFullOutputSize();
    const uint currentLightCount = RAB_GetCurrentLightCount();
    if (currentLightCount == 0u || dimensions.x == 0u || dimensions.y == 0u)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const uint cellSize = 12u;
    const uint tileX = pixel.x / cellSize;
    const uint tileY = pixel.y / cellSize;
    const uint tilesX = max((dimensions.x + cellSize - 1u) / cellSize, 1u);
    const uint lightIndex = tileY * tilesX + tileX;
    if (lightIndex >= currentLightCount)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const RAB_LightInfo lightInfo = RAB_LoadLightInfo(lightIndex, false);
    if (!RAB_IsLightInfoValid(lightInfo))
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const RAB_LightSample lightSample = RAB_SamplePolymorphicLight(lightInfo, surface, float2(0.37, 0.61));
    if (lightSample.valid == 0u)
    {
        return float4(1.0, 0.45, 0.0, 1.0);
    }

    float3 lightDir;
    float lightDistance;
    const bool geometryVisible = RAB_GetConservativeLightSampleGeometry(surface, lightSample, lightDir, lightDistance);
    if (!geometryVisible)
    {
        return float4(0.45, 0.05, 0.75, 1.0);
    }

    const bool candidateVisible = RAB_GetCandidateNeeVisibility(surface, lightSample);
    const bool selectedVisible = RAB_GetSelectedNeeVisibility(surface, lightSample);
    const bool strictVisible = RAB_GetConservativeVisibility(surface, lightSample);
    if (strictVisible && candidateVisible && selectedVisible)
    {
        return float4(0.05, 0.75, 0.12, 1.0);
    }
    if (!strictVisible && candidateVisible && selectedVisible)
    {
        return float4(0.05, 0.45, 0.95, 1.0);
    }
    if (!strictVisible && candidateVisible && !selectedVisible)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }
    if (!strictVisible && !candidateVisible && !selectedVisible)
    {
        return float4(0.95, 0.22, 0.04, 1.0);
    }
    if (strictVisible && !candidateVisible && !selectedVisible)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (strictVisible && !candidateVisible && selectedVisible)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (strictVisible && candidateVisible && !selectedVisible)
    {
        return float4(0.55, 0.22, 0.75, 1.0);
    }
    if (!strictVisible && !candidateVisible && selectedVisible)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    return float4(1.0, 0.0, 0.75, 1.0);
}

#endif
