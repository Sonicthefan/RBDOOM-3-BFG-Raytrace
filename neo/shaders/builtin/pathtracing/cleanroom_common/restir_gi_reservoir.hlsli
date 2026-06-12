#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_GI_RESERVOIR_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_GI_RESERVOIR_HLSLI

#include "cleanroom_common/restir_gi_parameters.hlsli"
#include "cleanroom_common/restir_shared_reservoir_addressing.hlsli"

// rbdoom-owned GI reservoir, replacing Rtxdi/GI/Reservoir.hlsli. The public
// surface (field names, function names) comes from this repo's call sites
// and the rbdoom GI spatial replacement's documented contract; the weighted
// reservoir math is Bitterli et al. 2020 / Ouyang et al. 2021 (ReSTIR GI).
//
// GI finalize convention (from the call sites): the caller passes the full
// normalization INCLUDING the selected sample's target function, e.g.
// RTXDI_FinalizeGIResampling(r, 1.0, pNew * r.M), so unlike the DI variant
// no division by the stored target pdf happens here.
//
// Packed ABI: 32 bytes (uint4 x2), matching the buffer stride the C++ side
// currently allocates (verified 2026-06-13 via a sizeof compile probe; the
// header itself was not opened). Layout is rbdoom-defined; it only has to
// round-trip through this header:
//   Data0.xyz  position (raw float bits)
//   Data0.w    radiance RG (2x f16)
//   Data1.x    radiance B (f16) | M (8 bits << 16) | age (8 bits << 24)
//   Data1.y    octahedral normal (2x unorm16)
//   Data1.z    weightSum (raw float bits)
//   Data1.w    rngSeed
// rngSeed identifies the reservoir's initial candidate and is propagated
// unchanged through reuse - it is the storage prerequisite for the staged
// adaptive M-cap (GIAdaptiveMCap.hlsli, ReSTIR PT Enhanced Sec. 5). Producers
// of fresh samples should set it; resampling only copies it with the winner.

struct RTXDI_PackedGIReservoir
{
    uint4 Data0;
    uint4 Data1;
};

struct RTXDI_GIReservoir
{
    float3 position;   // secondary sample point (world space)
    float3 normal;     // sample-point normal
    float3 radiance;   // incoming radiance cached at the sample point
    float weightSum;   // running RIS weight; W after RTXDI_FinalizeGIResampling
    uint M;            // confidence / accumulated sample count
    uint age;          // frames since the sample was generated
    uint rngSeed;      // initial-candidate RNG seed (adaptive M-cap support)
};

RTXDI_GIReservoir RTXDI_EmptyGIReservoir()
{
    RTXDI_GIReservoir reservoir = (RTXDI_GIReservoir)0;
    return reservoir;
}

bool RTXDI_IsValidGIReservoir(RTXDI_GIReservoir reservoir)
{
    return reservoir.M > 0u && reservoir.weightSum >= 0.0;
}

// Fresh single-sample reservoir. weightSum carries 1/samplePdf so that one
// RTXDI_CombineGIReservoirs + RTXDI_FinalizeGIResampling(1, pNew * M) round
// reproduces the canonical single-candidate RIS weight.
RTXDI_GIReservoir RTXDI_MakeGIReservoir(
    float3 position,
    float3 normal,
    float3 radiance,
    float samplePdf)
{
    RTXDI_GIReservoir reservoir = RTXDI_EmptyGIReservoir();
    reservoir.position = position;
    reservoir.normal = normal;
    reservoir.radiance = max(radiance, float3(0.0, 0.0, 0.0));
    reservoir.weightSum = samplePdf > 0.0 ? 1.0 / samplePdf : 0.0;
    reservoir.M = 1u;
    reservoir.age = 0u;
    return reservoir;
}

// Streaming merge (Bitterli 2020 Alg. 4): candidate weight is its target
// function at the destination times its contribution weight W times its
// confidence M. Confidence accumulates even for rejected candidates.
bool RTXDI_CombineGIReservoirs(
    inout RTXDI_GIReservoir reservoir,
    RTXDI_GIReservoir candidate,
    float random,
    float targetPdf)
{
    reservoir.M += candidate.M;

    const float risWeight = max(targetPdf, 0.0) * max(candidate.weightSum, 0.0) * (float)candidate.M;
    if (candidate.M == 0u || risWeight <= 0.0)
    {
        return false;
    }

    reservoir.weightSum += risWeight;
    const bool selectCandidate = random * reservoir.weightSum <= risWeight;
    if (selectCandidate)
    {
        reservoir.position = candidate.position;
        reservoir.normal = candidate.normal;
        reservoir.radiance = candidate.radiance;
        reservoir.age = candidate.age;
        reservoir.rngSeed = candidate.rngSeed;
    }
    return selectCandidate;
}

// W = wSum * numerator / denominator. The denominator from callers already
// contains the selected sample's target function (see header note).
void RTXDI_FinalizeGIResampling(
    inout RTXDI_GIReservoir reservoir,
    float normalizationNumerator,
    float normalizationDenominator)
{
    if (normalizationNumerator <= 0.0 ||
        normalizationDenominator <= 0.0 ||
        reservoir.weightSum <= 0.0)
    {
        reservoir.weightSum = 0.0;
        return;
    }

    reservoir.weightSum = (reservoir.weightSum * normalizationNumerator) / normalizationDenominator;
}

float2 RBPT_GIEncodeOctahedralNormal(float3 normal)
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

float3 RBPT_GIDecodeOctahedralNormal(float2 encoded)
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

// miscFlags is accepted for call-site compatibility (the bridge passes 0u)
// but not stored; the packed layout has no spare bits for it.
RTXDI_PackedGIReservoir RTXDI_PackGIReservoir(RTXDI_GIReservoir reservoir, uint miscFlags)
{
    const float3 radiance = clamp(reservoir.radiance, float3(0.0, 0.0, 0.0), float3(65504.0, 65504.0, 65504.0));
    const float2 encodedNormal = saturate(RBPT_GIEncodeOctahedralNormal(reservoir.normal));
    const uint2 normalUnorm = uint2(round(encodedNormal * 65535.0));

    RTXDI_PackedGIReservoir packedReservoir = (RTXDI_PackedGIReservoir)0;
    packedReservoir.Data0.x = asuint(reservoir.position.x);
    packedReservoir.Data0.y = asuint(reservoir.position.y);
    packedReservoir.Data0.z = asuint(reservoir.position.z);
    packedReservoir.Data0.w = (f32tof16(radiance.x) & 0xffffu) | (f32tof16(radiance.y) << 16);
    packedReservoir.Data1.x =
        (f32tof16(radiance.z) & 0xffffu) |
        (min(reservoir.M, 0xffu) << 16) |
        (min(reservoir.age, 0xffu) << 24);
    packedReservoir.Data1.y = (normalUnorm.x & 0xffffu) | (normalUnorm.y << 16);
    packedReservoir.Data1.z = asuint(reservoir.weightSum);
    packedReservoir.Data1.w = reservoir.rngSeed;
    return packedReservoir;
}

RTXDI_GIReservoir RTXDI_UnpackGIReservoir(RTXDI_PackedGIReservoir packedReservoir)
{
    RTXDI_GIReservoir reservoir = RTXDI_EmptyGIReservoir();
    reservoir.position = float3(
        asfloat(packedReservoir.Data0.x),
        asfloat(packedReservoir.Data0.y),
        asfloat(packedReservoir.Data0.z));
    reservoir.radiance = float3(
        f16tof32(packedReservoir.Data0.w & 0xffffu),
        f16tof32((packedReservoir.Data0.w >> 16) & 0xffffu),
        f16tof32(packedReservoir.Data1.x & 0xffffu));
    reservoir.M = (packedReservoir.Data1.x >> 16) & 0xffu;
    reservoir.age = (packedReservoir.Data1.x >> 24) & 0xffu;
    const float2 encodedNormal = float2(
        (float)(packedReservoir.Data1.y & 0xffffu),
        (float)((packedReservoir.Data1.y >> 16) & 0xffffu)) / 65535.0;
    reservoir.normal = RBPT_GIDecodeOctahedralNormal(encodedNormal);
    reservoir.weightSum = asfloat(packedReservoir.Data1.z);
    reservoir.rngSeed = packedReservoir.Data1.w;
    return reservoir;
}

#endif
