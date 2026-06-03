float3 PathTraceCleanRoomMotionVectorDiagnosticColor(float2 motionPixels)
{
    if (any(!(motionPixels == motionPixels)))
    {
        return float3(1.0, 0.0, 1.0);
    }

    const float maxVisualizedPixels = 32.0;
    const float magnitude = length(motionPixels);
    const float2 signedDirection = saturate(motionPixels / (maxVisualizedPixels * 2.0) + 0.5);
    return float3(signedDirection, saturate(magnitude / maxVisualizedPixels));
}

float3 PathTraceCleanRoomMotionVectorInvalidColor(uint motionMask)
{
    const uint reason = (motionMask >> PT_MOTION_VECTOR_MASK_INVALID_REASON_SHIFT) & 0xffu;
    if (reason == 1u)
    {
        return float3(1.0, 0.05, 0.05);
    }
    if (reason == 2u || reason == 13u)
    {
        return float3(1.0, 0.85, 0.05);
    }
    if (reason == 3u || reason == 15u)
    {
        return float3(1.0, 0.35, 0.05);
    }
    if (reason == 4u || reason == 17u)
    {
        return float3(0.9, 0.05, 1.0);
    }
    if (reason == 7u || reason == 18u)
    {
        return float3(0.05, 0.35, 1.0);
    }
    return float3(0.6, 0.6, 0.6);
}

float3 PathTraceCleanRoomRRMotionVectorColor(uint2 pixel, uint2 dimensions)
{
    const uint motionMask = PathTraceMotionVectorMask[pixel];
    const bool valid = (motionMask & PT_MOTION_VECTOR_MASK_VALID) != 0u;
    const bool rrHalf = pixel.x >= (dimensions.x >> 1u);
    const float2 motionPixels = rrHalf ? PathTraceRRMotionVectors[pixel] : PathTraceMotionVectors[pixel].xy;
    float3 color = PathTraceCleanRoomMotionVectorDiagnosticColor(motionPixels);
    if (!valid)
    {
        return color * 0.2 + PathTraceCleanRoomMotionVectorInvalidColor(motionMask) * 0.8;
    }

    const uint sourceKind = (motionMask >> PT_MOTION_VECTOR_MASK_SOURCE_SHIFT) & 0x0fu;
    const float3 sourceTint =
        sourceKind == 1u ? float3(0.05, 0.25, 0.05) :
        sourceKind == 2u ? float3(0.25, 0.05, 0.25) :
        sourceKind == 3u ? float3(0.05, 0.20, 0.25) :
        float3(0.08, 0.08, 0.08);
    color = saturate(color + sourceTint);
    if (abs((int)pixel.x - (int)(dimensions.x >> 1u)) <= 1)
    {
        return float3(1.0, 1.0, 1.0);
    }
    return color;
}

float3 PathTraceCleanRoomToneMapRRInput(float3 hdrColor)
{
    hdrColor = max(hdrColor, float3(0.0, 0.0, 0.0));
    return hdrColor / (hdrColor + float3(1.0, 1.0, 1.0));
}

float3 PathTraceCleanRoomRRResetMaskColor(uint resetMask)
{
    if (resetMask == 0u)
    {
        return float3(0.03, 0.45, 0.12);
    }
    if ((resetMask & RT_RR_RESET_STOCHASTIC_TRANSLUCENT) != 0u)
    {
        return float3(0.75, 0.15, 0.85);
    }
    if ((resetMask & RT_RR_RESET_MATERIAL_MISMATCH) != 0u)
    {
        return float3(1.0, 0.6, 0.05);
    }
    if ((resetMask & RT_RR_RESET_REJECTED_PREVIOUS) != 0u)
    {
        return float3(0.95, 0.05, 0.05);
    }
    if ((resetMask & RT_RR_RESET_MISSING_PREVIOUS) != 0u)
    {
        return float3(0.6, 0.1, 0.1);
    }
    if ((resetMask & RT_RR_RESET_OBJECT_MOTION_UNAVAILABLE) != 0u)
    {
        return float3(0.8, 0.8, 0.15);
    }
    if ((resetMask & RT_RR_RESET_INVALID_SURFACE) != 0u)
    {
        return float3(0.02, 0.02, 0.02);
    }
    return float3(0.8, 0.3, 0.1);
}

float3 PathTraceCleanRoomRRInputMosaicColor(uint2 pixel, uint2 dimensions)
{
    const uint columns = 3u;
    const uint rows = 2u;
    const uint cellWidth = max(dimensions.x / columns, 1u);
    const uint cellHeight = max(dimensions.y / rows, 1u);
    const uint column = min(pixel.x / cellWidth, columns - 1u);
    const uint row = min(pixel.y / cellHeight, rows - 1u);
    const uint tile = row * columns + column;
    const uint2 cellOrigin = uint2(column * cellWidth, row * cellHeight);
    const uint2 cellExtent = max(uint2(
        column == columns - 1u ? dimensions.x - cellOrigin.x : cellWidth,
        row == rows - 1u ? dimensions.y - cellOrigin.y : cellHeight), uint2(1u, 1u));
    const uint2 localPixel = min(pixel - cellOrigin, cellExtent - 1u);
    const uint2 sourcePixel = min((localPixel * dimensions) / cellExtent, dimensions - 1u);

    if (localPixel.x == 0u || localPixel.y == 0u)
    {
        return float3(1.0, 1.0, 1.0);
    }

    if (tile == 0u)
    {
        return saturate(PathTraceRRGuideAlbedo[sourcePixel].rgb);
    }
    if (tile == 1u)
    {
        const float4 normalRoughness = PathTraceRRGuideNormalRoughness[sourcePixel];
        return saturate(float3(normalRoughness.rg, normalRoughness.a));
    }
    if (tile == 2u)
    {
        return saturate(PathTraceRRGuideSpecularAlbedo[sourcePixel].rgb);
    }
    if (tile == 3u)
    {
        return PathTraceCleanRoomToneMapRRInput(PathTraceRRInputColor[sourcePixel].rgb);
    }
    if (tile == 4u)
    {
        const float depth = PathTraceRRGuideDepth[sourcePixel];
        const float hitDistance = PathTraceRRGuideHitDistance[sourcePixel];
        return float3(saturate(depth / 4096.0), saturate(hitDistance / 4096.0), 0.0);
    }

    const bool validMotion = (PathTraceMotionVectorMask[sourcePixel] & PT_MOTION_VECTOR_MASK_VALID) != 0u;
    const float3 motionColor = PathTraceCleanRoomMotionVectorDiagnosticColor(PathTraceRRMotionVectors[sourcePixel]);
    const float3 resetColor = PathTraceCleanRoomRRResetMaskColor(PathTraceRRGuideResetMask[sourcePixel]);
    return validMotion ? saturate(motionColor) : saturate(resetColor);
}
