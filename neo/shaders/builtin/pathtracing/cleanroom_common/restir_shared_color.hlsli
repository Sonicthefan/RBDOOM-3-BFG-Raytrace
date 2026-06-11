#ifndef RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_COLOR_HLSLI
#define RB_PATH_TRACING_CLEANROOM_COMMON_RESTIR_SHARED_COLOR_HLSLI

float RTXDI_Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

uint RTXDI_EncodeRGBToLogLuv(float3 color)
{
    const float3 rgb = max(color, float3(0.0, 0.0, 0.0));
    const float X = dot(rgb, float3(0.4124564, 0.3575761, 0.1804375));
    const float Y = dot(rgb, float3(0.2126729, 0.7151522, 0.0721750));
    const float Z = dot(rgb, float3(0.0193339, 0.1191920, 0.9503041));

    const float logY = 409.6 * (log2(max(Y, 1.0e-30)) + 20.0);
    const uint encodedLuminance = (uint)clamp(logY, 0.0, 16383.0);
    if (encodedLuminance == 0u)
    {
        return 0u;
    }

    const float xyzSum = X + Y + Z;
    const float denominator = max(-2.0 * X + 12.0 * Y + 3.0 * xyzSum, 1.0e-8);
    const float2 chroma = float2(4.0 * X, 9.0 * Y) / denominator;
    const uint2 encodedChroma = uint2(clamp(820.0 * chroma, 0.0, 511.0));

    return (encodedLuminance << 18u) | (encodedChroma.x << 9u) | encodedChroma.y;
}

float3 RTXDI_DecodeLogLuvToRGB(uint packedColor)
{
    if (packedColor == 0u)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const uint encodedLuminance = packedColor >> 18u;
    if (encodedLuminance == 0u)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float Y = exp2(((float)encodedLuminance + 0.5) / 409.6 - 20.0);
    const uint2 encodedChroma = uint2((packedColor >> 9u) & 0x1ffu, packedColor & 0x1ffu);
    const float2 chroma = ((float2)encodedChroma + 0.5) / 820.0;

    const float invDenominator = 1.0 / max(6.0 * chroma.x - 16.0 * chroma.y + 12.0, 1.0e-8);
    const float2 xy = float2(9.0 * chroma.x, 4.0 * chroma.y) * invDenominator;
    const float scale = Y / max(xy.y, 1.0e-8);
    const float X = scale * xy.x;
    const float Z = scale * (1.0 - xy.x - xy.y);

    return max(float3(
        3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z,
       -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z,
        0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z), float3(0.0, 0.0, 0.0));
}

#endif
