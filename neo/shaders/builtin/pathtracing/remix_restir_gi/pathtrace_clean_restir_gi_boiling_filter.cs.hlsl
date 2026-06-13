// Clean-room ReSTIR GI boiling filter + resolve consumer (RGI-08).
//
// Remix final-shading-shaped boiling filter for the shaded GI output:
// compute the threadgroup's average nonzero shaded luminance and clamp
// outliers down to that average.
// This pass also owns the resolve add so the combined outputs receive the
// FILTERED contribution: SmokeOutput/RRInputColor += indirect.
//
// io_whitelist: reads GI-O-05 (shaded indirect GI) + GI-I-01 (validity only);
// writes GI-O-05 (filtered in place) and the combined beauty outputs under
// r_pathTracingCleanRestirGiResolve.

#include "../../../vulkan.hlsli"
#include "../PathTracePrimarySurface.hlsli"

cbuffer PathTraceCleanRestirGiBoilingFilterConstants : register(b0)
{
    uint CleanGiBfWidth;
    uint CleanGiBfHeight;
    float CleanGiBfThreshold;     // 0 disables clamping
    uint CleanGiBfResolveEnabled; // adds shaded indirect GI into the outputs
};

VK_IMAGE_FORMAT("rgba16f") RWTexture2D<float4> CleanGiBfIndirectDiffuse : register(u1);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> CleanGiBfSmokeOutput : register(u2);
VK_IMAGE_FORMAT("rgba32f") RWTexture2D<float4> CleanGiBfRRInputColor : register(u3);
StructuredBuffer<PathTracePrimarySurfaceRecord> CleanGiBfPrimarySurfaces : register(t4);

static const uint CLEAN_GI_BF_GROUP_SIZE = 8u;

groupshared float CleanGiBfLuminanceSum[CLEAN_GI_BF_GROUP_SIZE * CLEAN_GI_BF_GROUP_SIZE];
groupshared uint CleanGiBfNonzeroCount[CLEAN_GI_BF_GROUP_SIZE * CLEAN_GI_BF_GROUP_SIZE];

float CleanGiBfLuminance(float3 value)
{
    return dot(max(value, float3(0.0, 0.0, 0.0)), float3(0.2126, 0.7152, 0.0722));
}

[numthreads(CLEAN_GI_BF_GROUP_SIZE, CLEAN_GI_BF_GROUP_SIZE, 1)]
void main(uint2 pixel : SV_DispatchThreadID, uint2 localIndex : SV_GroupThreadID)
{
    const bool inBounds = pixel.x < CleanGiBfWidth && pixel.y < CleanGiBfHeight;

    float3 indirect = float3(0.0, 0.0, 0.0);
    if (inBounds)
    {
        indirect = max(CleanGiBfIndirectDiffuse[pixel].rgb, float3(0.0, 0.0, 0.0));
        if (!all(indirect == indirect))
        {
            indirect = float3(0.0, 0.0, 0.0);
        }
    }
    const float luminance = CleanGiBfLuminance(indirect);

    // Group reduction of the nonzero shaded luminance (portable groupshared
    // tree instead of wave intrinsics).
    const uint linearIndex = localIndex.x + localIndex.y * CLEAN_GI_BF_GROUP_SIZE;
    CleanGiBfLuminanceSum[linearIndex] = luminance;
    CleanGiBfNonzeroCount[linearIndex] = luminance > 0.0 ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = (CLEAN_GI_BF_GROUP_SIZE * CLEAN_GI_BF_GROUP_SIZE) / 2u; stride > 0u; stride >>= 1u)
    {
        if (linearIndex < stride)
        {
            CleanGiBfLuminanceSum[linearIndex] += CleanGiBfLuminanceSum[linearIndex + stride];
            CleanGiBfNonzeroCount[linearIndex] += CleanGiBfNonzeroCount[linearIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    const uint nonzeroCount = CleanGiBfNonzeroCount[0];
    const float averageNonzeroLuminance = nonzeroCount > 0u
        ? CleanGiBfLuminanceSum[0] / (float)nonzeroCount
        : 0.0;

    if (!inBounds)
    {
        return;
    }

    if (CleanGiBfThreshold > 0.0 &&
        averageNonzeroLuminance > 0.0 &&
        luminance > averageNonzeroLuminance * CleanGiBfThreshold)
    {
        indirect *= averageNonzeroLuminance / luminance;
        CleanGiBfIndirectDiffuse[pixel] = float4(indirect, 1.0);
    }

    if (CleanGiBfResolveEnabled != 0u)
    {
        const PathTracePrimarySurfaceRecord record = CleanGiBfPrimarySurfaces[pixel.y * CleanGiBfWidth + pixel.x];
        if (record.header.x == RT_PATH_TRACE_PRIMARY_SURFACE_RECORD_VERSION &&
            (record.header.y & RT_PRIMARY_SURFACE_VALID) != 0u)
        {
            CleanGiBfSmokeOutput[pixel] += float4(indirect, 0.0);
            CleanGiBfRRInputColor[pixel] += float4(indirect, 0.0);
        }
    }
}
