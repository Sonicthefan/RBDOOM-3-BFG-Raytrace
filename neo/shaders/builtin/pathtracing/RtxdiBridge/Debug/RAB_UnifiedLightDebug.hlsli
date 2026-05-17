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

    if (RAB_DoomAnalyticIdentityValid(identity) && identity.universeIndex < remapCount)
    {
        const PathTraceDoomAnalyticLightRemap remap = DoomAnalyticRemap[identity.universeIndex];
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
