Texture2D<float4> CleanRtxdiDiBoilingInput : register(t1);
RWTexture2D<float4> CleanRtxdiDiBoilingOutput : register(u2);

cbuffer CleanRtxdiDiBoilingConstants : register(b0)
{
    uint2 CleanRtxdiDiBoilingDimensions;
    float CleanRtxdiDiBoilingThreshold;
    uint CleanRtxdiDiBoilingEnabled;
};

groupshared float CleanRtxdiDiBoilingLuminance[64];
groupshared uint CleanRtxdiDiBoilingCount;

float3 CleanRtxdiDiBoilingSanitizeRgb(float3 color)
{
    if (!all(color == color))
    {
        return float3(0.0, 0.0, 0.0);
    }

    return clamp(color, float3(0.0, 0.0, 0.0), float3(65504.0, 65504.0, 65504.0));
}

float CleanRtxdiDiBoilingLum(float3 color)
{
    return dot(CleanRtxdiDiBoilingSanitizeRgb(color), float3(0.2126, 0.7152, 0.0722));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID)
{
    const uint localIndex = groupThreadId.y * 8u + groupThreadId.x;
    const uint2 pixel = dispatchThreadId.xy;
    const bool validPixel =
        pixel.x < CleanRtxdiDiBoilingDimensions.x &&
        pixel.y < CleanRtxdiDiBoilingDimensions.y;

    if (localIndex == 0u)
    {
        CleanRtxdiDiBoilingCount = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float luminance = 0.0;
    if (validPixel)
    {
        color = CleanRtxdiDiBoilingInput[pixel];
        color.rgb = CleanRtxdiDiBoilingSanitizeRgb(color.rgb);
        color.a = color.a == color.a ? color.a : 1.0;
        luminance = CleanRtxdiDiBoilingLum(color.rgb);
        InterlockedAdd(CleanRtxdiDiBoilingCount, 1u);
    }
    CleanRtxdiDiBoilingLuminance[localIndex] = luminance;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 32u; stride > 0u; stride >>= 1u)
    {
        if (localIndex < stride)
        {
            CleanRtxdiDiBoilingLuminance[localIndex] += CleanRtxdiDiBoilingLuminance[localIndex + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (!validPixel)
    {
        return;
    }

    const float averageLuminance = CleanRtxdiDiBoilingLuminance[0] / max((float)CleanRtxdiDiBoilingCount, 1.0);
    const float threshold = max(CleanRtxdiDiBoilingThreshold, 1.0);
    const float maxLuminance = averageLuminance * threshold;
    if (CleanRtxdiDiBoilingEnabled != 0u && averageLuminance > 0.0 && luminance > maxLuminance)
    {
        color.rgb *= maxLuminance / max(luminance, 1.0e-6);
    }
    color.rgb = CleanRtxdiDiBoilingSanitizeRgb(color.rgb);

    CleanRtxdiDiBoilingOutput[pixel] = color;
}
