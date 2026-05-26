#ifndef RB_PATH_TRACING_RAB_UNIFIED_LIGHT_DEBUG_HLSLI
#define RB_PATH_TRACING_RAB_UNIFIED_LIGHT_DEBUG_HLSLI

static const uint RB_UNIFIED_LIGHT_TYPE_INVALID = 0u;
static const uint RB_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE = 1u;
static const uint RB_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC = 2u;

struct RestirPTDebugUnifiedLightRecord
{
    uint type;
    uint sourceIndex;
    uint flags;
    uint valid;
    float3 radiance;
    float luminance;
    float sourcePdf;
    float sourceWeight;
    int previousIndex;
    uint identityA;
    uint identityB;
};

uint RestirPTDebugUnifiedCurrentLightCount()
{
    return RAB_GetCurrentEmissiveTriangleCount() + RAB_GetCurrentDoomAnalyticLightCount();
}

uint RestirPTDebugUnifiedGridIndex(uint2 pixel)
{
    const uint2 dimensions = max(PathTraceFullOutputSize(), uint2(1u, 1u));
    const uint cellSize = 12u;
    const uint tileX = pixel.x / cellSize;
    const uint tileY = pixel.y / cellSize;
    const uint tilesX = max((dimensions.x + cellSize - 1u) / cellSize, 1u);
    return tileY * tilesX + tileX;
}

RestirPTDebugUnifiedLightRecord RestirPTDebugEmptyUnifiedLightRecord()
{
    RestirPTDebugUnifiedLightRecord record = (RestirPTDebugUnifiedLightRecord)0;
    record.previousIndex = -1;
    record.sourceIndex = RAB_INVALID_LIGHT_INDEX;
    return record;
}

RestirPTDebugUnifiedLightRecord RestirPTDebugLoadUnifiedCurrentLight(uint unifiedIndex)
{
    RestirPTDebugUnifiedLightRecord record = RestirPTDebugEmptyUnifiedLightRecord();
    const uint emissiveCount = RAB_GetCurrentEmissiveTriangleCount();
    const uint analyticCount = RAB_GetCurrentDoomAnalyticLightCount();
    const uint currentLightCount = emissiveCount + analyticCount;
    if (unifiedIndex >= currentLightCount)
    {
        return record;
    }

    if (unifiedIndex < emissiveCount)
    {
        const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[unifiedIndex];
        record.type = RB_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE;
        record.sourceIndex = unifiedIndex;
        record.flags = emissiveTriangle.flags | emissiveTriangle.padding0;
        record.radiance = max(emissiveTriangle.estimatedRadianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * max(ToyPathInfo.z, 0.0);
        record.luminance = max(emissiveTriangle.estimatedRadianceAndLuminance.w, RAB_Luminance(record.radiance));
        record.sourcePdf = max(emissiveTriangle.sampleWeightAndPdf.y, 0.0);
        record.sourceWeight = max(max(emissiveTriangle.sampleWeightAndPdf.x, emissiveTriangle.estimatedRadianceAndLuminance.w), 0.0);
        record.identityA = emissiveTriangle.identityHashLo;
        record.identityB = emissiveTriangle.identityHashHi;
        record.valid =
            emissiveTriangle.centerAndArea.w > 1.0e-6 &&
            all(record.radiance == record.radiance) &&
            record.sourceWeight > 0.0 ? 1u : 0u;

        const uint previousEmissiveCount = RAB_GetPreviousEmissiveTriangleCount();
        const PathTraceEmissiveLightRemap remap = SmokeEmissiveRemap[unifiedIndex];
        if (RAB_SmokeEmissiveRemapValid(remap) && remap.currentToPreviousIndex >= 0)
        {
            const uint previousEmissiveIndex = (uint)remap.currentToPreviousIndex;
            record.previousIndex = previousEmissiveIndex < previousEmissiveCount ? (int)previousEmissiveIndex : -1;
        }
        return record;
    }

    const uint analyticIndex = unifiedIndex - emissiveCount;
    const uint currentIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.x, 0.0);
    const uint previousIdentityCount = (uint)max(DoomAnalyticLightRemapInfo.y, 0.0);
    const uint remapCount = (uint)max(DoomAnalyticLightRemapInfo.z, 0.0);
    if (analyticIndex >= analyticCount || analyticIndex >= currentIdentityCount)
    {
        record.type = RB_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
        record.sourceIndex = analyticIndex;
        return record;
    }

    const PathTraceDoomAnalyticLightCandidate analyticLight = DoomAnalyticLights[analyticIndex];
    const PathTraceDoomAnalyticLightCandidateIdentity identity = DoomAnalyticCurrentIdentities[analyticIndex];
    const float radius = max(analyticLight.originAndRadius.w, 0.01);
    const float influenceRadius = max(analyticLight.doomRadiusAndArea.x, 1.0);
    const float intensityScale = max(DoomAnalyticLightInfo.z, 0.0);
    record.type = RB_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC;
    record.sourceIndex = analyticIndex;
    record.flags = analyticLight.flags | identity.flags;
    record.radiance = max(analyticLight.colorAndIntensity.rgb, float3(0.0, 0.0, 0.0)) * intensityScale;
    record.luminance = RAB_Luminance(record.radiance);
    record.sourcePdf = 0.0;
    record.sourceWeight = max(max(record.radiance.r, record.radiance.g), record.radiance.b) * (4.0 * RTXDI_PI * radius * radius) * influenceRadius;
    record.identityA = analyticLight.renderLightIndex;
    record.identityB = analyticLight.entityNumber;
    record.valid =
        RAB_DoomAnalyticIdentitySampleable(identity) &&
        all(analyticLight.originAndRadius.xyz == analyticLight.originAndRadius.xyz) &&
        all(record.radiance == record.radiance) &&
        radius > 0.0 &&
        influenceRadius > 0.0 &&
        record.sourceWeight > 0.0 ? 1u : 0u;

    const uint remapIndex = RAB_DoomAnalyticIdentityRemapIndex(identity);
    if (RAB_DoomAnalyticIdentityValid(identity) && remapIndex < remapCount)
    {
        const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[remapIndex];
        if (RAB_DoomAnalyticRemapValid(remap) && remap.currentToPreviousCandidateIndex >= 0)
        {
            const uint previousAnalyticIndex = (uint)remap.currentToPreviousCandidateIndex;
            if (previousAnalyticIndex < previousIdentityCount &&
                RAB_DoomAnalyticLightStateCompatible(analyticLight, DoomAnalyticPreviousLights[previousAnalyticIndex]))
            {
                record.previousIndex = (int)(RAB_GetPreviousEmissiveTriangleCount() + previousAnalyticIndex);
            }
        }
    }
    return record;
}

float3 RestirPTDebugUnifiedLightTypeColor(uint type)
{
    if (type == RB_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return float3(0.05, 0.75, 0.12);
    }
    if (type == RB_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        return float3(0.05, 0.80, 0.95);
    }
    return float3(0.0, 0.0, 0.0);
}

uint RestirPTDebugUnifiedPreviousLightCount()
{
    return RAB_GetPreviousEmissiveTriangleCount() + (uint)max(DoomAnalyticLightRemapInfo.y, 0.0);
}

int RestirPTDebugUnifiedSignedPreviousIndex(uint previousIndex)
{
    return previousIndex == PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX ? -1 : (int)previousIndex;
}

uint RestirPTDebugCpuUnifiedLightValid(PathTraceUnifiedLightRecord record)
{
    if (record.type != PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE &&
        record.type != PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        return 0u;
    }
    if (record.sourceIndex == PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX)
    {
        return 0u;
    }
    if (!all(record.positionAndRadius.xyz == record.positionAndRadius.xyz) ||
        !all(record.radianceAndLuminance.rgb == record.radianceAndLuminance.rgb) ||
        record.sourceWeight != record.sourceWeight ||
        record.sourceWeight <= 0.0)
    {
        return 0u;
    }
    if (record.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return record.normalAndArea.w > 1.0e-6 ? 1u : 0u;
    }
    return record.positionAndRadius.w > 0.0 && record.uvOrDoomParams.x > 0.0 ? 1u : 0u;
}

bool RestirPTDebugApproximatelyEqual(float a, float b, float relativeTolerance)
{
    const float scale = max(max(abs(a), abs(b)), 1.0);
    return abs(a - b) <= max(1.0e-4, scale * relativeTolerance);
}

float RestirPTDebugCpuUnifiedRuntimeScale(uint type)
{
    if (type == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return max(ToyPathInfo.z, 0.0);
    }
    if (type == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        return max(DoomAnalyticLightInfo.z, 0.0);
    }
    return 0.0;
}

float3 RestirPTDebugCpuUnifiedScaledRadiance(PathTraceUnifiedLightRecord record)
{
    return max(record.radianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * RestirPTDebugCpuUnifiedRuntimeScale(record.type);
}

float RestirPTDebugCpuUnifiedScaledLuminance(PathTraceUnifiedLightRecord record, float3 scaledRadiance)
{
    if (record.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return max(record.radianceAndLuminance.w, RAB_Luminance(scaledRadiance));
    }
    return RAB_Luminance(scaledRadiance);
}

float RestirPTDebugCpuUnifiedScaledSourceWeight(PathTraceUnifiedLightRecord record)
{
    if (record.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC)
    {
        return record.sourceWeight * RestirPTDebugCpuUnifiedRuntimeScale(record.type);
    }
    return record.sourceWeight;
}

float3 RestirPTDebugRabLightInfoTypeColor(uint lightType)
{
    if (lightType == RAB_LIGHT_TYPE_EMISSIVE_TRIANGLE)
    {
        return RestirPTDebugUnifiedLightTypeColor(PATH_TRACE_UNIFIED_LIGHT_TYPE_EMISSIVE_TRIANGLE);
    }
    if (lightType == RAB_LIGHT_TYPE_DOOM_ANALYTIC_SPHERE)
    {
        return RestirPTDebugUnifiedLightTypeColor(PATH_TRACE_UNIFIED_LIGHT_TYPE_DOOM_ANALYTIC);
    }
    return float3(0.0, 0.0, 0.0);
}

float4 RestirPTDebugCompareRabLightInfo(RAB_LightInfo splitInfo, RAB_LightInfo unifiedInfo)
{
    const bool splitValid = RAB_IsLightInfoValid(splitInfo);
    const bool unifiedValid = RAB_IsLightInfoValid(unifiedInfo);
    if (!splitValid && !unifiedValid)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (splitValid != unifiedValid)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (splitInfo.lightType != unifiedInfo.lightType)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (splitInfo.lightIndex != unifiedInfo.lightIndex)
    {
        return float4(1.0, 0.85, 0.05, 1.0);
    }
    if (splitInfo.materialIndex != unifiedInfo.materialIndex)
    {
        return float4(1.0, 0.35, 0.0, 1.0);
    }
    if (!RestirPTDebugApproximatelyEqual(splitInfo.position.x, unifiedInfo.position.x, 0.001) ||
        !RestirPTDebugApproximatelyEqual(splitInfo.position.y, unifiedInfo.position.y, 0.001) ||
        !RestirPTDebugApproximatelyEqual(splitInfo.position.z, unifiedInfo.position.z, 0.001) ||
        !RestirPTDebugApproximatelyEqual(splitInfo.radius, unifiedInfo.radius, 0.001) ||
        !RestirPTDebugApproximatelyEqual(splitInfo.influenceRadius, unifiedInfo.influenceRadius, 0.001) ||
        !RestirPTDebugApproximatelyEqual(splitInfo.area, unifiedInfo.area, 0.001))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (!RestirPTDebugApproximatelyEqual(splitInfo.radiance.x, unifiedInfo.radiance.x, 0.01) ||
        !RestirPTDebugApproximatelyEqual(splitInfo.radiance.y, unifiedInfo.radiance.y, 0.01) ||
        !RestirPTDebugApproximatelyEqual(splitInfo.radiance.z, unifiedInfo.radiance.z, 0.01) ||
        !RestirPTDebugApproximatelyEqual(splitInfo.weight, unifiedInfo.weight, 0.05))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    return float4(RestirPTDebugRabLightInfoTypeColor(splitInfo.lightType), 1.0);
}

float4 EvaluateRestirPTUnifiedLoadCurrentCompareView(uint2 pixel)
{
    const uint unifiedIndex = RestirPTDebugUnifiedGridIndex(pixel);
    if (unifiedIndex >= RestirPTDebugUnifiedCurrentLightCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    return RestirPTDebugCompareRabLightInfo(
        RAB_LoadSplitLightInfo(unifiedIndex, false),
        RAB_LoadUnifiedLightInfo(unifiedIndex, false));
}

float4 EvaluateRestirPTUnifiedLoadPreviousCompareView(uint2 pixel)
{
    const uint unifiedIndex = RestirPTDebugUnifiedGridIndex(pixel);
    if (unifiedIndex >= RestirPTDebugUnifiedCurrentLightCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (MotionVectorInfo.y < 0.5)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    return RestirPTDebugCompareRabLightInfo(
        RAB_LoadSplitLightInfo(unifiedIndex, true),
        RAB_LoadUnifiedLightInfo(unifiedIndex, true));
}

float2 RestirPTDebugUnifiedSampleUv(uint unifiedIndex)
{
    const uint hash = unifiedIndex * 1664525u + 1013904223u;
    const float ux = (float)(hash & 0xffffu) / 65535.0;
    const float uy = (float)((hash >> 16u) & 0xffffu) / 65535.0;
    return float2(ux, uy);
}

float4 RestirPTDebugCompareRabLightSample(RAB_LightSample splitSample, RAB_LightSample unifiedSample)
{
    const bool splitValid = splitSample.valid != 0u;
    const bool unifiedValid = unifiedSample.valid != 0u;
    if (!splitValid && !unifiedValid)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    if (splitValid != unifiedValid)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (splitSample.lightType != unifiedSample.lightType)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (splitSample.lightIndex != unifiedSample.lightIndex)
    {
        return float4(1.0, 0.85, 0.05, 1.0);
    }
    if (!RestirPTDebugApproximatelyEqual(splitSample.position.x, unifiedSample.position.x, 0.001) ||
        !RestirPTDebugApproximatelyEqual(splitSample.position.y, unifiedSample.position.y, 0.001) ||
        !RestirPTDebugApproximatelyEqual(splitSample.position.z, unifiedSample.position.z, 0.001) ||
        !RestirPTDebugApproximatelyEqual(splitSample.distance, unifiedSample.distance, 0.001))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (!RestirPTDebugApproximatelyEqual(splitSample.areaPdf, unifiedSample.areaPdf, 0.01) ||
        !RestirPTDebugApproximatelyEqual(splitSample.solidAnglePdf, unifiedSample.solidAnglePdf, 0.01))
    {
        return float4(1.0, 0.35, 0.0, 1.0);
    }
    if (!RestirPTDebugApproximatelyEqual(splitSample.radiance.x, unifiedSample.radiance.x, 0.01) ||
        !RestirPTDebugApproximatelyEqual(splitSample.radiance.y, unifiedSample.radiance.y, 0.01) ||
        !RestirPTDebugApproximatelyEqual(splitSample.radiance.z, unifiedSample.radiance.z, 0.01))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    return float4(RestirPTDebugRabLightInfoTypeColor(splitSample.lightType), 1.0);
}

float4 EvaluateRestirPTUnifiedSampleCompareView(RAB_Surface surface, uint2 pixel)
{
    const uint unifiedIndex = RestirPTDebugUnifiedGridIndex(pixel);
    if (unifiedIndex >= RestirPTDebugUnifiedCurrentLightCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const float2 uv = RestirPTDebugUnifiedSampleUv(unifiedIndex);
    const RAB_LightInfo splitInfo = RAB_LoadSplitLightInfo(unifiedIndex, false);
    const RAB_LightInfo unifiedInfo = RAB_LoadUnifiedLightInfo(unifiedIndex, false);
    return RestirPTDebugCompareRabLightSample(
        RAB_SampleSplitPolymorphicLight(splitInfo, surface, uv),
        RAB_SampleUnifiedPolymorphicLight(unifiedInfo, surface, uv));
}

float4 EvaluateRestirPTUnifiedSampleNumericView(RAB_Surface surface, uint2 pixel)
{
    const uint unifiedIndex = RestirPTDebugUnifiedGridIndex(pixel);
    if (unifiedIndex >= RestirPTDebugUnifiedCurrentLightCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }
    const RAB_LightInfo lightInfo = RAB_LoadUnifiedLightInfo(unifiedIndex, false);
    const RAB_LightSample sample = RAB_SampleUnifiedPolymorphicLight(lightInfo, surface, RestirPTDebugUnifiedSampleUv(unifiedIndex));
    if (sample.valid == 0u)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const uint cellSize = 12u;
    const uint2 localPixel = pixel % cellSize;
    if (localPixel.x < (cellSize / 2u) && localPixel.y < (cellSize / 2u))
    {
        const float heat = saturate(log2(1.0 + RAB_Luminance(sample.radiance)) / 12.0);
        return float4(RestirPTDebugRabLightInfoTypeColor(sample.lightType) * (0.15 + heat * 0.85), 1.0);
    }
    if (localPixel.x >= (cellSize / 2u) && localPixel.y < (cellSize / 2u))
    {
        const float heat = saturate(log2(1.0 + sample.areaPdf) / 12.0);
        return float4(heat, 0.85 * heat, 0.05, 1.0);
    }
    if (localPixel.x < (cellSize / 2u))
    {
        const float heat = saturate(log2(1.0 + sample.solidAnglePdf) / 12.0);
        return float4(0.05, 0.85 * heat, heat, 1.0);
    }
    const float heat = saturate(log2(1.0 + sample.distance) / 12.0);
    return float4(heat, 0.05, 0.85 * heat, 1.0);
}

float4 EvaluateRestirPTCpuUnifiedLightTypeView(uint2 pixel)
{
    const uint unifiedIndex = RestirPTDebugUnifiedGridIndex(pixel);
    if (unifiedIndex >= RestirPTDebugUnifiedCurrentLightCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const PathTraceUnifiedLightRecord record = PathTraceUnifiedLights[unifiedIndex];
    if (record.type == PATH_TRACE_UNIFIED_LIGHT_TYPE_INVALID)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (RestirPTDebugCpuUnifiedLightValid(record) == 0u)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    return float4(RestirPTDebugUnifiedLightTypeColor(record.type), 1.0);
}

float4 EvaluateRestirPTCpuUnifiedLightCompareView(uint2 pixel)
{
    const uint unifiedIndex = RestirPTDebugUnifiedGridIndex(pixel);
    if (unifiedIndex >= RestirPTDebugUnifiedCurrentLightCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const PathTraceUnifiedLightRecord cpuRecord = PathTraceUnifiedLights[unifiedIndex];
    const RestirPTDebugUnifiedLightRecord virtualRecord = RestirPTDebugLoadUnifiedCurrentLight(unifiedIndex);
    if (RestirPTDebugCpuUnifiedLightValid(cpuRecord) == 0u)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    if (virtualRecord.valid == 0u)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (cpuRecord.type != virtualRecord.type)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (cpuRecord.sourceIndex != virtualRecord.sourceIndex)
    {
        return float4(1.0, 0.85, 0.05, 1.0);
    }
    if (RestirPTDebugUnifiedSignedPreviousIndex(cpuRecord.previousIndex) != virtualRecord.previousIndex)
    {
        return float4(1.0, 0.35, 0.0, 1.0);
    }
    const float3 scaledCpuRadiance = RestirPTDebugCpuUnifiedScaledRadiance(cpuRecord);
    const float scaledCpuLuminance = RestirPTDebugCpuUnifiedScaledLuminance(cpuRecord, scaledCpuRadiance);
    if (!RestirPTDebugApproximatelyEqual(scaledCpuRadiance.x, virtualRecord.radiance.x, 0.01) ||
        !RestirPTDebugApproximatelyEqual(scaledCpuRadiance.y, virtualRecord.radiance.y, 0.01) ||
        !RestirPTDebugApproximatelyEqual(scaledCpuRadiance.z, virtualRecord.radiance.z, 0.01) ||
        !RestirPTDebugApproximatelyEqual(scaledCpuLuminance, virtualRecord.luminance, 0.01))
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (!RestirPTDebugApproximatelyEqual(RestirPTDebugCpuUnifiedScaledSourceWeight(cpuRecord), virtualRecord.sourceWeight, 0.05))
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    return float4(RestirPTDebugUnifiedLightTypeColor(cpuRecord.type), 1.0);
}

float4 EvaluateRestirPTCpuUnifiedLightRemapView(uint2 pixel)
{
    const uint unifiedIndex = RestirPTDebugUnifiedGridIndex(pixel);
    if (unifiedIndex >= RestirPTDebugUnifiedCurrentLightCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (MotionVectorInfo.y < 0.5)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const PathTraceUnifiedLightRecord record = PathTraceUnifiedLights[unifiedIndex];
    if (RestirPTDebugCpuUnifiedLightValid(record) == 0u)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }

    const uint previousIndex = PathTraceUnifiedLightRemap[unifiedIndex];
    if (previousIndex == PATH_TRACE_UNIFIED_LIGHT_INVALID_INDEX)
    {
        return RestirPTReferenceCurrentToPreviousLightFailureColor(unifiedIndex);
    }
    if (previousIndex >= RestirPTDebugUnifiedPreviousLightCount())
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (record.previousIndex != previousIndex)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const PathTraceUnifiedLightRecord previousRecord = PathTraceUnifiedPreviousLights[previousIndex];
    if (previousRecord.type != record.type)
    {
        return float4(1.0, 0.85, 0.05, 1.0);
    }
    return float4(RestirPTDebugUnifiedLightTypeColor(record.type), 1.0);
}

float4 EvaluateRestirPTUnifiedLightTypeView(uint2 pixel)
{
    const uint unifiedIndex = RestirPTDebugUnifiedGridIndex(pixel);
    if (unifiedIndex >= RestirPTDebugUnifiedCurrentLightCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const RestirPTDebugUnifiedLightRecord record = RestirPTDebugLoadUnifiedCurrentLight(unifiedIndex);
    if (record.type == RB_UNIFIED_LIGHT_TYPE_INVALID)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (record.valid == 0u)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    return float4(RestirPTDebugUnifiedLightTypeColor(record.type), 1.0);
}

float4 EvaluateRestirPTUnifiedLightRadianceView(uint2 pixel)
{
    const uint unifiedIndex = RestirPTDebugUnifiedGridIndex(pixel);
    if (unifiedIndex >= RestirPTDebugUnifiedCurrentLightCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const RestirPTDebugUnifiedLightRecord record = RestirPTDebugLoadUnifiedCurrentLight(unifiedIndex);
    if (record.valid == 0u)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    if (record.luminance != record.luminance || record.luminance < 0.0)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (record.luminance <= 0.0)
    {
        return float4(0.95, 0.85, 0.05, 1.0);
    }

    const float heat = saturate(log2(1.0 + record.luminance) / 12.0);
    const float3 typeColor = RestirPTDebugUnifiedLightTypeColor(record.type);
    return float4(typeColor * (0.15 + heat * 0.85), 1.0);
}

float4 EvaluateRestirPTUnifiedLightRemapView(uint2 pixel)
{
    const uint unifiedIndex = RestirPTDebugUnifiedGridIndex(pixel);
    if (unifiedIndex >= RestirPTDebugUnifiedCurrentLightCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    if (MotionVectorInfo.y < 0.5)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }

    const RestirPTDebugUnifiedLightRecord record = RestirPTDebugLoadUnifiedCurrentLight(unifiedIndex);
    if (record.valid == 0u)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    if (record.previousIndex < 0)
    {
        return RestirPTReferenceCurrentToPreviousLightFailureColor(unifiedIndex);
    }

    const float3 typeColor = RestirPTDebugUnifiedLightTypeColor(record.type);
    return float4(typeColor, 1.0);
}

float4 EvaluateRestirPTUnifiedLightNumericView(uint2 pixel)
{
    const uint unifiedIndex = RestirPTDebugUnifiedGridIndex(pixel);
    if (unifiedIndex >= RestirPTDebugUnifiedCurrentLightCount())
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const RestirPTDebugUnifiedLightRecord record = RestirPTDebugLoadUnifiedCurrentLight(unifiedIndex);
    if (record.type == RB_UNIFIED_LIGHT_TYPE_INVALID)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (record.valid == 0u)
    {
        return float4(0.55, 0.05, 0.80, 1.0);
    }
    if (record.sourceIndex == RAB_INVALID_LIGHT_INDEX)
    {
        return float4(1.0, 0.0, 0.75, 1.0);
    }
    if (record.sourceWeight != record.sourceWeight || record.sourceWeight <= 0.0 || record.sourceWeight > 3.402823e+38)
    {
        return float4(0.85, 0.02, 0.02, 1.0);
    }
    if (record.sourcePdf != record.sourcePdf || record.sourcePdf < 0.0 || record.sourcePdf > 3.402823e+38)
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (record.sourcePdf <= 0.0)
    {
        return float4(0.05, 0.18, 0.75, 1.0);
    }
    return float4(RestirPTDebugUnifiedLightTypeColor(record.type), 1.0);
}

#endif
