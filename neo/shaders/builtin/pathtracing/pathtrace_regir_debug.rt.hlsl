#include "../../vulkan.hlsli"
#include "RtxdiBridge/RAB_ReGIR.hlsli"

struct PathTraceReGIRPayload
{
    uint value;
    float hitT;
};

struct PathTraceReGIRShadowPayload
{
    uint hit;
};

struct PathTraceDoomAnalyticLightCandidate
{
    float4 originAndRadius;
    float4 colorAndIntensity;
    float4 doomRadiusAndArea;
    uint flags;
    uint renderLightIndex;
    uint entityNumber;
    uint padding0;
};

struct PathTraceSmokeEmissiveTriangle
{
    float4 centerAndArea;
    float4 normalAndLuminance;
    float4 uvBounds;
    float4 centroidUvAndWeight;
    float4 estimatedRadianceAndLuminance;
    float4 sampleWeightAndPdf;
    uint materialIndex;
    uint instanceId;
    uint primitiveIndex;
    uint flags;
    uint emissiveTextureIndex;
    uint emissiveTextureWidth;
    uint emissiveTextureHeight;
    uint materialId;
    uint universeMaterialIndex;
    uint identityHashLo;
    uint identityHashHi;
    uint padding0;
};

VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> SmokeOutput : register(u1);
RaytracingAccelerationStructure SmokeScene : register(t0);
StructuredBuffer<PathTraceSmokeEmissiveTriangle> SmokeEmissiveTriangles : register(t16);
StructuredBuffer<PathTraceDoomAnalyticLightCandidate> DoomAnalyticLights : register(t27);
RWStructuredBuffer<PathTraceReGIRCandidateRecord> ReGIRCandidateCache : register(u72);

cbuffer PathTraceSmokeConstants : register(b2)
{
    float4 CameraOriginAndTMax : packoffset(c0);
    float4 CameraForwardAndTanX : packoffset(c1);
    float4 CameraLeftAndTanY : packoffset(c2);
    float4 CameraUpAndDebugMode : packoffset(c3);
    float4 EmissiveInfo : packoffset(c73);
    float4 DoomAnalyticLightInfo : packoffset(c76);
    float4 DispatchTileInfo : packoffset(c91);
    float4 ReGIRInfo0 : packoffset(c110);
    float4 ReGIRInfo1 : packoffset(c111);
    float4 ReGIRInfo2 : packoffset(c112);
    float4 ReGIRInfo3 : packoffset(c113);
};

uint2 PathTraceReGIRDispatchTileOffset()
{
    return uint2((uint)max(DispatchTileInfo.x, 0.0), (uint)max(DispatchTileInfo.y, 0.0));
}

uint2 PathTraceReGIRFullOutputSize()
{
    const uint2 size = uint2((uint)max(DispatchTileInfo.z, 0.0), (uint)max(DispatchTileInfo.w, 0.0));
    return (size.x > 0u && size.y > 0u) ? size : DispatchRaysDimensions().xy;
}

float4 PathTraceReGIRMissColor(uint view)
{
    if (view == 1u)
    {
        return float4(0.0, 0.16, 0.38, 1.0);
    }
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 PathTraceReGIRDebugColor(float3 worldPosition)
{
    const uint enable = (uint)ReGIRInfo0.x;
    const uint view = (uint)ReGIRInfo0.y;
    const uint mode = (uint)ReGIRInfo0.z;
    const uint centerMode = (uint)ReGIRInfo0.w;
    const float cellSize = ReGIRInfo1.x;
    const uint3 gridDimensions = uint3((uint)ReGIRInfo1.y, (uint)ReGIRInfo1.z, (uint)ReGIRInfo1.w);
    if (enable == 0u || view == 0u || mode == 0u)
    {
        return float4(0.25, 0.0, 0.0, 1.0);
    }

    const PathTraceReGIRCellDebug cell = PathTraceReGIRMapWorldPositionToCell(
        worldPosition,
        CameraOriginAndTMax.xyz,
        centerMode,
        cellSize,
        gridDimensions);
    const bool boundary = cell.boundary < 0.035;
    if (view == 1u)
    {
        return cell.valid != 0u
            ? float4(boundary ? float3(1.0, 1.0, 1.0) : float3(0.0, 0.72, 0.24), 1.0)
            : float4(1.0, 0.0, 0.82, 1.0);
    }
    if (view == 2u)
    {
        const float3 color = PathTraceReGIRCellColor(cell.globalCoord);
        return float4(boundary ? float3(1.0, 1.0, 1.0) : color, 1.0);
    }
    if (view == 3u)
    {
        if (cell.valid == 0u)
        {
            return float4(1.0, 0.0, 0.82, 1.0);
        }
        return float4(boundary ? float3(1.0, 0.9, 0.0) : float3(0.0, 0.85, 0.18), 1.0);
    }
    return float4(0.45, 0.0, 0.0, 1.0);
}

uint PathTraceReGIRSelectEmissiveTriangle(uint hash, uint emissiveCount, out float selectedPdf)
{
    if (emissiveCount == 0u)
    {
        selectedPdf = 0.0;
        return 0u;
    }

    selectedPdf = 1.0 / max((float)emissiveCount, 1.0);
    return hash % emissiveCount;
}

PathTraceReGIRCandidateRecord PathTraceReGIRBuildCandidateForCellSlot(PathTraceReGIRCellDebug cell, uint slotInCell, uint slotIndex)
{
    const uint buildSamples = max((uint)ReGIRInfo2.y, 1u);
    const uint lightDomain = (uint)ReGIRInfo2.z;
    const uint analyticCount = (uint)max(DoomAnalyticLightInfo.x, 0.0);
    const uint emissiveCount = (uint)max(EmissiveInfo.x, 0.0);

    PathTraceReGIRCandidateRecord candidate;
    if (lightDomain == 0u)
    {
        if (analyticCount == 0u)
        {
            return PathTraceReGIREmptyCandidate(cell.localCellIndex, slotIndex, PATH_TRACE_REGIR_EMPTY_NO_ANALYTIC_LIGHTS);
        }

        const uint proposalSeed = PathTraceReGIRHashCell(cell.globalCoord + int3((int)(slotInCell % buildSamples), (int)slotInCell, (int)(slotInCell / buildSamples + 1u)));
        const uint selectedAnalytic = proposalSeed % analyticCount;
        const PathTraceDoomAnalyticLightCandidate analyticLight = DoomAnalyticLights[selectedAnalytic];
        candidate.lightIndex = selectedAnalytic;
        candidate.lightClass = PATH_TRACE_REGIR_LIGHT_CLASS_DOOM_ANALYTIC;
        candidate.sourcePdf = 1.0 / max((float)analyticCount, 1.0);
        candidate.invSourcePdf = (float)analyticCount;
        candidate.cellIndex = cell.localCellIndex;
        candidate.slotIndex = slotIndex;
        candidate.flags = PATH_TRACE_REGIR_CANDIDATE_VALID | PATH_TRACE_REGIR_CANDIDATE_WRITTEN;
        candidate.globalIdentity = analyticLight.renderLightIndex;
    }
    else if (lightDomain == 1u)
    {
        if (emissiveCount == 0u)
        {
            return PathTraceReGIREmptyCandidate(cell.localCellIndex, slotIndex, PATH_TRACE_REGIR_EMPTY_NO_EMISSIVE_LIGHTS);
        }

        float selectedPdf = 0.0;
        const uint proposalSeed = PathTraceReGIRHashCell(cell.globalCoord + int3((int)(slotInCell % buildSamples), (int)slotInCell, (int)(slotInCell / buildSamples + 1u)));
        const uint selectedEmissive = PathTraceReGIRSelectEmissiveTriangle(proposalSeed, emissiveCount, selectedPdf);
        const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[selectedEmissive];
        candidate.lightIndex = selectedEmissive;
        candidate.lightClass = PATH_TRACE_REGIR_LIGHT_CLASS_EMISSIVE_TRIANGLE;
        candidate.sourcePdf = max(selectedPdf, 1.0e-8);
        candidate.invSourcePdf = 1.0 / max(candidate.sourcePdf, 1.0e-8);
        candidate.cellIndex = cell.localCellIndex;
        candidate.slotIndex = slotIndex;
        candidate.flags = PATH_TRACE_REGIR_CANDIDATE_VALID | PATH_TRACE_REGIR_CANDIDATE_WRITTEN;
        candidate.globalIdentity = emissiveTriangle.identityHashLo;
    }
    else
    {
        return PathTraceReGIREmptyCandidate(cell.localCellIndex, slotIndex, PATH_TRACE_REGIR_EMPTY_UNSUPPORTED_DOMAIN);
    }
    return candidate;
}

void PathTraceReGIRBuildCandidateCache(uint linearDispatchIndex, uint dispatchPixelCount)
{
    const uint enable = (uint)ReGIRInfo0.x;
    const uint mode = (uint)ReGIRInfo0.z;
    const uint centerMode = (uint)ReGIRInfo0.w;
    const float cellSize = ReGIRInfo1.x;
    const uint3 gridDimensions = uint3((uint)ReGIRInfo1.y, (uint)ReGIRInfo1.z, (uint)ReGIRInfo1.w);
    const uint lightsPerCell = max((uint)ReGIRInfo2.x, 1u);
    const uint cellCount = (uint)ReGIRInfo2.w;
    const uint candidateSlotCount = (uint)ReGIRInfo3.y;
    if (enable == 0u || mode == 0u || dispatchPixelCount == 0u || cellCount == 0u || candidateSlotCount == 0u)
    {
        return;
    }

    for (uint slotIndex = linearDispatchIndex; slotIndex < candidateSlotCount; slotIndex += dispatchPixelCount)
    {
        const uint cellIndex = slotIndex / lightsPerCell;
        const uint slotInCell = slotIndex - cellIndex * lightsPerCell;
        if (cellIndex >= cellCount)
        {
            return;
        }

        const PathTraceReGIRCellDebug cell = PathTraceReGIRMapLocalCellIndex(
            cellIndex,
            CameraOriginAndTMax.xyz,
            centerMode,
            cellSize,
            gridDimensions);
        if (cell.valid != 0u)
        {
            ReGIRCandidateCache[slotIndex] = PathTraceReGIRBuildCandidateForCellSlot(cell, slotInCell, slotIndex);
        }
        else
        {
            ReGIRCandidateCache[slotIndex] = PathTraceReGIREmptyCandidate(cellIndex, slotIndex, PATH_TRACE_REGIR_EMPTY_OUT_OF_VOLUME);
        }
    }
}

PathTraceReGIRCandidateRecord PathTraceReGIRReadPrimaryCandidate(PathTraceReGIRCellDebug cell, uint lightsPerCell)
{
    const uint slotIndex = cell.localCellIndex * lightsPerCell;
    return ReGIRCandidateCache[slotIndex];
}

float4 PathTraceReGIRCandidateDebugColor(float3 worldPosition)
{
    const uint enable = (uint)ReGIRInfo0.x;
    const uint view = (uint)ReGIRInfo0.y;
    const uint mode = (uint)ReGIRInfo0.z;
    const uint centerMode = (uint)ReGIRInfo0.w;
    const float cellSize = ReGIRInfo1.x;
    const uint3 gridDimensions = uint3((uint)ReGIRInfo1.y, (uint)ReGIRInfo1.z, (uint)ReGIRInfo1.w);
    const uint lightsPerCell = max((uint)ReGIRInfo2.x, 1u);
    const uint cellCount = (uint)ReGIRInfo2.w;
    if (enable == 0u || mode == 0u)
    {
        return float4(0.25, 0.0, 0.0, 1.0);
    }

    const PathTraceReGIRCellDebug cell = PathTraceReGIRMapWorldPositionToCell(
        worldPosition,
        CameraOriginAndTMax.xyz,
        centerMode,
        cellSize,
        gridDimensions);
    if (cell.valid == 0u || cell.localCellIndex >= cellCount)
    {
        return float4(1.0, 0.0, 0.82, 1.0);
    }

    const PathTraceReGIRCandidateRecord candidate = PathTraceReGIRReadPrimaryCandidate(cell, lightsPerCell);
    const bool candidateValid = (candidate.flags & PATH_TRACE_REGIR_CANDIDATE_VALID) != 0u;

    const bool boundary = cell.boundary < 0.035;
    if (view == 4u)
    {
        uint validSlots = 0u;
        [loop]
        for (uint slotInCell = 0u; slotInCell < lightsPerCell; ++slotInCell)
        {
            const PathTraceReGIRCandidateRecord slotCandidate = ReGIRCandidateCache[cell.localCellIndex * lightsPerCell + slotInCell];
            validSlots += (slotCandidate.flags & PATH_TRACE_REGIR_CANDIDATE_VALID) != 0u ? 1u : 0u;
        }
        const float occupancy = saturate((float)validSlots / max((float)lightsPerCell, 1.0));
        return float4(boundary ? float3(1.0, 1.0, 1.0) : float3(0.05, occupancy, 0.18), 1.0);
    }
    if (!candidateValid)
    {
        return float4(1.0, 0.42, 0.0, 1.0);
    }
    if (view == 5u)
    {
        if (candidate.lightClass != PATH_TRACE_REGIR_LIGHT_CLASS_DOOM_ANALYTIC)
        {
            return float4(0.75, 0.0, 0.0, 1.0);
        }
        const uint identityHash = candidate.globalIdentity != 0xffffffffu ? candidate.globalIdentity : candidate.lightIndex;
        const float3 color = PathTraceReGIRCellColor(int3(identityHash, identityHash >> 8u, identityHash >> 16u));
        return float4(boundary ? float3(1.0, 1.0, 1.0) : color, 1.0);
    }
    if (view == 6u)
    {
        if (candidate.lightClass != PATH_TRACE_REGIR_LIGHT_CLASS_EMISSIVE_TRIANGLE)
        {
            return float4(0.75, 0.0, 0.0, 1.0);
        }
        const PathTraceSmokeEmissiveTriangle emissiveTriangle = SmokeEmissiveTriangles[candidate.lightIndex];
        const float3 color = PathTraceReGIRCellColor(int3(emissiveTriangle.identityHashLo, emissiveTriangle.identityHashHi, candidate.lightIndex));
        return float4(boundary ? float3(1.0, 1.0, 1.0) : color, 1.0);
    }
    if (view == 7u)
    {
        const float pdfBand = saturate(candidate.sourcePdf * 128.0);
        const float invBand = saturate(candidate.invSourcePdf / 512.0);
        return float4(pdfBand, invBand, candidate.sourcePdf > 0.0 ? 0.25 : 0.0, 1.0);
    }
    return PathTraceReGIRDebugColor(worldPosition);
}

[shader("raygeneration")]
void RayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy + PathTraceReGIRDispatchTileOffset();
    const uint2 dimensions = PathTraceReGIRFullOutputSize();
    if (pixel.x >= dimensions.x || pixel.y >= dimensions.y)
    {
        return;
    }

    const uint2 dispatchDimensions = DispatchRaysDimensions().xy;
    const uint linearDispatchIndex = DispatchRaysIndex().x + DispatchRaysIndex().y * dispatchDimensions.x;
    const uint dispatchPixelCount = dispatchDimensions.x * dispatchDimensions.y;
    if ((uint)ReGIRInfo3.x == 1u)
    {
        PathTraceReGIRBuildCandidateCache(linearDispatchIndex, dispatchPixelCount);
        return;
    }

    const float2 uv = (float2(pixel) + 0.5) / float2(dimensions);
    const float2 ndc = uv * 2.0 - 1.0;
    RayDesc ray;
    ray.Origin = CameraOriginAndTMax.xyz;
    ray.Direction = normalize(
        CameraForwardAndTanX.xyz +
        CameraLeftAndTanY.xyz * (-ndc.x * CameraForwardAndTanX.w) +
        CameraUpAndDebugMode.xyz * (-ndc.y * CameraLeftAndTanY.w));
    ray.TMin = 0.1;
    ray.TMax = CameraOriginAndTMax.w;

    PathTraceReGIRPayload payload;
    payload.value = 0u;
    payload.hitT = 0.0;
    TraceRay(SmokeScene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);
    if (payload.value == 0u)
    {
        SmokeOutput[pixel] = PathTraceReGIRMissColor((uint)ReGIRInfo0.y);
        return;
    }

    const float3 worldPosition = ray.Origin + ray.Direction * payload.hitT;
    const uint view = (uint)ReGIRInfo0.y;
    SmokeOutput[pixel] = (view >= 4u && view <= 7u)
        ? PathTraceReGIRCandidateDebugColor(worldPosition)
        : PathTraceReGIRDebugColor(worldPosition);
}

[shader("miss")]
void Miss(inout PathTraceReGIRPayload payload)
{
    payload.value = 0u;
}

[shader("miss")]
void ShadowMiss(inout PathTraceReGIRShadowPayload payload)
{
    payload.hit = 0u;
}

[shader("anyhit")]
void AnyHit(inout PathTraceReGIRPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
}

[shader("anyhit")]
void ShadowAnyHit(inout PathTraceReGIRShadowPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.hit = 1u;
}

[shader("closesthit")]
void ClosestHit(inout PathTraceReGIRPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.value = 1u;
    payload.hitT = RayTCurrent();
}

[shader("closesthit")]
void ShadowClosestHit(inout PathTraceReGIRShadowPayload payload, BuiltInTriangleIntersectionAttributes attributes)
{
    payload.hit = 1u;
}
