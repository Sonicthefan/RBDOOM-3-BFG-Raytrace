#ifndef RB_PATH_TRACE_CLEAN_RTXDI_DI_RR_GEOMETRY_GUIDES_HLSLI
#define RB_PATH_TRACE_CLEAN_RTXDI_DI_RR_GEOMETRY_GUIDES_HLSLI

// RR depth. RRProjectionDepthInfo.x = near, .y = far, .z = depth mode:
//   mode 2 = hyperbolic hardware depth [0,1] (kBufferTypeDepth) -- the form DLSS-RR
//            actually consumes; far/near ratio controls precision distribution.
//   mode 0 = normalized linear view-Z [0,1]; mode 1 = raw linear view-Z (kBufferTypeLinearDepth).
// depthInverted=false for all (near = low value).
float PathTraceCleanRtxdiDiRRLinearDepth(float viewZ)
{
    const float zNear = RRProjectionDepthInfo.x;
    const float zFar = RRProjectionDepthInfo.y;
    const float mode = RRProjectionDepthInfo.z;
    if (mode >= 1.5)
    {
        // hyperbolic finite-far: far/(far-near) * (1 - near/viewZ), reaches 1.0 at far
        const float safeViewZ = max(viewZ, zNear);
        return saturate((zFar / max(zFar - zNear, 1.0e-4)) * (1.0 - zNear / safeViewZ));
    }
    if (mode >= 0.5)
    {
        return max(viewZ, 0.0);                                    // mode 1: raw view Z
    }
    return saturate((viewZ - zNear) / max(zFar - zNear, 1.0e-4));  // mode 0: normalized [0,1]
}

float PathTraceCleanRtxdiDiRRInvalidDepth()
{
    // sky / invalid reads as the far plane: raw linear (mode 1) = far value, else 1.0
    const float mode = RRProjectionDepthInfo.z;
    return (mode >= 0.5 && mode < 1.5) ? RRProjectionDepthInfo.y : 1.0;
}

float4 PathTraceCleanRtxdiDiRRInvalidPosition()
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

// Linear view Z (distance along camera forward). Depth and motion.z share this
// convention; motion.z (PathTraceCleanRtxdiDiRRMotionDepthDelta) is the difference of
// these so it stays coherent automatically.
float PathTraceCleanRtxdiDiRRPrimaryDepthFromSurface(RAB_Surface surface)
{
    return PathTraceCleanRtxdiDiRRLinearDepth(PathTracePrimarySurfaceCurrentViewZ(surface.worldPos));
}

float PathTraceCleanRtxdiDiRRPreviousDepthFromWorldPosition(float3 previousWorldPos)
{
    return PathTraceCleanRtxdiDiRRLinearDepth(PathTracePrimarySurfacePreviousViewZ(previousWorldPos));
}

float PathTraceCleanRtxdiDiRRMotionDepthDelta(RAB_Surface currentSurface, float previousDepth)
{
    return previousDepth - PathTraceCleanRtxdiDiRRPrimaryDepthFromSurface(currentSurface);
}

float4 PathTraceCleanRtxdiDiRRPositionFromSurface(RAB_Surface surface)
{
    return float4(surface.worldPos, 1.0);
}

#endif
