#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_RESERVOIR_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_PT_RESERVOIR_HLSLI

#include "Rtxdi/RtxdiParameters.h"
#include "Rtxdi/Utils/Color.hlsli"
#include "restir_random_sampler_state.hlsli"
#include "Rtxdi/Utils/SampledLightData.hlsli"
#include "Rtxdi/Utils/ReservoirAddressing.hlsli"

#ifndef RTXDI_PT_RESERVOIR_BUFFER
#error "RTXDI_PT_RESERVOIR_BUFFER must name the ReSTIR PT reservoir RWStructuredBuffer."
#endif

#ifndef RTXDI_PTRESERVOIR_AGE_MAX
#define RTXDI_PTRESERVOIR_AGE_MAX 31u
#endif

struct RTXDI_PTReservoir
{
    static const uint MaxM = 0x3fffffffu;

    uint M;
    float WeightSum;
    float3 TargetFunction;
    float3 Radiance;
    float3 TranslatedWorldPosition;
    float3 WorldNormal;
    uint PathLength;
    uint RcVertexLength;
    float RcWiPdf;
    float PartialJacobian;
    uint RandomSeed;
    uint RandomIndex;
    uint Age;
    bool ShouldBoostSpatialSamples;
};

#ifndef RTXDI_PackedPTReservoir
struct RTXDI_PackedPTReservoir
{
    uint4 Data0;
    uint4 Data1;
    uint4 Data2;
    uint4 Data3;
};
#endif

uint RTXDI_PTPackMetadata(uint pathLength, uint rcVertexLength, uint age, bool shouldBoostSpatialSamples)
{
    return
        min(pathLength, 0xfffu) |
        (min(rcVertexLength, 0xfffu) << 12) |
        (min(age, RTXDI_PTRESERVOIR_AGE_MAX) << 24) |
        (shouldBoostSpatialSamples ? 0x20000000u : 0u);
}

void RTXDI_PTUnpackMetadata(
    uint packedMetadata,
    out uint pathLength,
    out uint rcVertexLength,
    out uint age,
    out bool shouldBoostSpatialSamples)
{
    pathLength = packedMetadata & 0xfffu;
    rcVertexLength = (packedMetadata >> 12) & 0xfffu;
    age = (packedMetadata >> 24) & RTXDI_PTRESERVOIR_AGE_MAX;
    shouldBoostSpatialSamples = (packedMetadata & 0x20000000u) != 0u;
}

uint RTXDI_PTPackPositiveHalfPair(float x, float y)
{
    return (f32tof16(max(y, 0.0)) << 16) | (f32tof16(max(x, 0.0)) & 0xffffu);
}

float2 RTXDI_PTUnpackPositiveHalfPair(uint packedPair)
{
    return float2(f16tof32(packedPair & 0xffffu), f16tof32((packedPair >> 16) & 0xffffu));
}

float2 RTXDI_PTEncodeOctahedralNormal(float3 normal)
{
    normal = normalize(normal);
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 1.0e-6);
    float2 encoded = normal.xy;
    if (normal.z < 0.0)
    {
        const float2 signValue = float2(encoded.x >= 0.0 ? 1.0 : -1.0, encoded.y >= 0.0 ? 1.0 : -1.0);
        encoded = (1.0 - abs(encoded.yx)) * signValue;
    }
    return encoded * 0.5 + 0.5;
}

float3 RTXDI_PTDecodeOctahedralNormal(float2 encoded)
{
    float2 f = encoded * 2.0 - 1.0;
    float3 normal = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (normal.z < 0.0)
    {
        const float2 signValue = float2(normal.x >= 0.0 ? 1.0 : -1.0, normal.y >= 0.0 ? 1.0 : -1.0);
        const float2 folded = (1.0 - abs(normal.yx)) * signValue;
        normal.x = folded.x;
        normal.y = folded.y;
    }
    return normalize(normal);
}

uint RTXDI_PTPackNormal(float3 normal)
{
    const float2 encoded = saturate(RTXDI_PTEncodeOctahedralNormal(normal));
    const uint2 packedUnorm = uint2(round(encoded * 65535.0));
    return (packedUnorm.y << 16) | (packedUnorm.x & 0xffffu);
}

float3 RTXDI_PTUnpackNormal(uint packedNormal)
{
    const float2 encoded = float2(
        (float)(packedNormal & 0xffffu),
        (float)((packedNormal >> 16) & 0xffffu)) / 65535.0;
    return RTXDI_PTDecodeOctahedralNormal(encoded);
}

RTXDI_PTReservoir RTXDI_EmptyPTReservoir()
{
    RTXDI_PTReservoir reservoir = (RTXDI_PTReservoir)0;
    return reservoir;
}

bool RTXDI_IsValidPTReservoir(const RTXDI_PTReservoir reservoir)
{
    return reservoir.M > 0u && reservoir.WeightSum > 0.0;
}

bool RTXDI_ConnectsToNeeLight(RTXDI_PTReservoir reservoir)
{
    return isinf(reservoir.Radiance.x);
}

RTXDI_SampledLightData RTXDI_GetSampledLightData(RTXDI_PTReservoir reservoir)
{
    RTXDI_SampledLightData sampledLightData = RTXDI_SampledLightData_CreateInvalidData();
    if (!RTXDI_ConnectsToNeeLight(reservoir))
    {
        return sampledLightData;
    }

    sampledLightData.lightData = asuint(reservoir.Radiance.y);
    sampledLightData.uvData = asuint(reservoir.Radiance.z);
    return sampledLightData;
}

RTXDI_RandomSamplerState RTXDI_GetRngForShading(RTXDI_PTReservoir reservoir)
{
    return RTXDI_CreateRandomSamplerFromDirectSeed(reservoir.RandomSeed, reservoir.RandomIndex + 6u);
}

RTXDI_PTReservoir RTXDI_MakePTReservoir(
    const float3 targetFunction,
    const uint randomSeed,
    const uint randomIndex,
    const uint rcVertexLength,
    const uint pathLength,
    const float partialJacobian,
    const float rcWiPdf,
    const float3 translatedWorldPosition,
    const float3 worldNormal,
    const float3 radiance,
    const float samplePdf)
{
    RTXDI_PTReservoir reservoir = RTXDI_EmptyPTReservoir();
    reservoir.M = 1u;
    reservoir.WeightSum = samplePdf > 0.0 ? 1.0 / samplePdf : 1.0;
    reservoir.TargetFunction = max(targetFunction, float3(0.0, 0.0, 0.0));
    reservoir.Radiance = radiance;
    reservoir.TranslatedWorldPosition = translatedWorldPosition;
    reservoir.WorldNormal = worldNormal;
    reservoir.PathLength = pathLength;
    reservoir.RcVertexLength = rcVertexLength;
    reservoir.PartialJacobian = partialJacobian;
    reservoir.RcWiPdf = rcWiPdf;
    reservoir.RandomSeed = randomSeed;
    reservoir.RandomIndex = randomIndex;
    reservoir.Age = 0u;
    return reservoir;
}

bool CombineReservoirs(
    inout RTXDI_PTReservoir targetReservoir,
    RTXDI_PTReservoir newReservoir,
    float random,
    float3 newTargetFunction)
{
    if (!RTXDI_IsValidPTReservoir(newReservoir))
    {
        return false;
    }

    const float candidateWeight = max(RTXDI_Luminance(max(newTargetFunction, float3(0.0, 0.0, 0.0))) * max(newReservoir.WeightSum, 0.0), 0.0);
    if (candidateWeight <= 0.0)
    {
        return false;
    }

    const float previousWeight = max(RTXDI_Luminance(max(targetReservoir.TargetFunction, float3(0.0, 0.0, 0.0))) * max(targetReservoir.WeightSum, 0.0), 0.0);
    const float combinedWeight = previousWeight + candidateWeight;
    const bool selectCandidate = !RTXDI_IsValidPTReservoir(targetReservoir) || random * combinedWeight <= candidateWeight;

    if (selectCandidate)
    {
        targetReservoir = newReservoir;
        targetReservoir.TargetFunction = newTargetFunction;
    }

    targetReservoir.M = min(targetReservoir.M + max(newReservoir.M, 1u), RTXDI_PTReservoir::MaxM);
    targetReservoir.WeightSum = combinedWeight;
    return selectCandidate;
}

void RTXDI_FinalizeResampling(inout RTXDI_PTReservoir reservoir, in float numerator, in float denominator)
{
    if (!RTXDI_IsValidPTReservoir(reservoir) || denominator <= 0.0 || numerator <= 0.0)
    {
        reservoir = RTXDI_EmptyPTReservoir();
        return;
    }

    reservoir.WeightSum = numerator / denominator;
}

RTXDI_PackedPTReservoir RTXDI_PackPTReservoir(RTXDI_PTReservoir reservoir)
{
    RTXDI_PackedPTReservoir packedReservoir = (RTXDI_PackedPTReservoir)0;
    const uint packedMetadata = RTXDI_PTPackMetadata(
        reservoir.PathLength,
        reservoir.RcVertexLength,
        reservoir.Age,
        reservoir.ShouldBoostSpatialSamples);
    packedReservoir.Data0 = uint4(
        reservoir.M,
        asuint(reservoir.WeightSum),
        asuint(reservoir.TargetFunction.x),
        asuint(reservoir.TargetFunction.y));
    packedReservoir.Data1 = uint4(
        asuint(reservoir.TargetFunction.z),
        asuint(reservoir.Radiance.x),
        asuint(reservoir.Radiance.y),
        asuint(reservoir.Radiance.z));
    packedReservoir.Data2 = uint4(
        asuint(reservoir.TranslatedWorldPosition.x),
        asuint(reservoir.TranslatedWorldPosition.y),
        asuint(reservoir.TranslatedWorldPosition.z),
        packedMetadata);
    packedReservoir.Data3 = uint4(
        RTXDI_PTPackNormal(reservoir.WorldNormal),
        RTXDI_PTPackPositiveHalfPair(reservoir.RcWiPdf, reservoir.PartialJacobian),
        reservoir.RandomSeed,
        reservoir.RandomIndex);
    return packedReservoir;
}

RTXDI_PTReservoir RTXDI_UnpackPTReservoir(in const RTXDI_PackedPTReservoir packedReservoir)
{
    RTXDI_PTReservoir reservoir = RTXDI_EmptyPTReservoir();
    reservoir.M = packedReservoir.Data0.x;
    reservoir.WeightSum = asfloat(packedReservoir.Data0.y);
    reservoir.TargetFunction = float3(
        asfloat(packedReservoir.Data0.z),
        asfloat(packedReservoir.Data0.w),
        asfloat(packedReservoir.Data1.x));
    reservoir.Radiance = float3(
        asfloat(packedReservoir.Data1.y),
        asfloat(packedReservoir.Data1.z),
        asfloat(packedReservoir.Data1.w));
    reservoir.TranslatedWorldPosition = float3(
        asfloat(packedReservoir.Data2.x),
        asfloat(packedReservoir.Data2.y),
        asfloat(packedReservoir.Data2.z));
    reservoir.WorldNormal = RTXDI_PTUnpackNormal(packedReservoir.Data3.x);
    RTXDI_PTUnpackMetadata(
        packedReservoir.Data2.w,
        reservoir.PathLength,
        reservoir.RcVertexLength,
        reservoir.Age,
        reservoir.ShouldBoostSpatialSamples);
    const float2 rcPdfAndJacobian = RTXDI_PTUnpackPositiveHalfPair(packedReservoir.Data3.y);
    reservoir.RcWiPdf = rcPdfAndJacobian.x;
    reservoir.PartialJacobian = rcPdfAndJacobian.y;
    reservoir.RandomSeed = packedReservoir.Data3.z;
    reservoir.RandomIndex = packedReservoir.Data3.w;
    return reservoir;
}

void RTXDI_StorePackedPTReservoir(
    const RTXDI_PackedPTReservoir packedPTReservoir,
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    const uint pointer = RTXDI_ReservoirPositionToPointer(reservoirParams, reservoirPosition, reservoirArrayIndex);
    RTXDI_PT_RESERVOIR_BUFFER[pointer] = packedPTReservoir;
}

void RTXDI_StorePTReservoir(
    const RTXDI_PTReservoir reservoir,
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    RTXDI_StorePackedPTReservoir(RTXDI_PackPTReservoir(reservoir), reservoirParams, reservoirPosition, reservoirArrayIndex);
}

RTXDI_PackedPTReservoir RTXDI_LoadPackedPTReservoir(
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    const uint pointer = RTXDI_ReservoirPositionToPointer(reservoirParams, reservoirPosition, reservoirArrayIndex);
    return RTXDI_PT_RESERVOIR_BUFFER[pointer];
}

RTXDI_PTReservoir RTXDI_LoadPTReservoir(
    RTXDI_ReservoirBufferParameters reservoirParams,
    uint2 reservoirPosition,
    uint reservoirArrayIndex)
{
    return RTXDI_UnpackPTReservoir(RTXDI_LoadPackedPTReservoir(reservoirParams, reservoirPosition, reservoirArrayIndex));
}

#endif
