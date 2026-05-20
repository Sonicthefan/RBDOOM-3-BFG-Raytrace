#ifndef RB_PATH_TRACING_REMIX_RAB_SURFACE_BRIDGE_HLSLI
#define RB_PATH_TRACING_REMIX_RAB_SURFACE_BRIDGE_HLSLI

// RTX Remix-shaped surface bridge contract for future DI temporal reuse.
//
// The active dispatch slice must provide current and previous primary-surface
// loading before including RTXDI temporal reuse. The fallback below is invalid
// by design so this inactive contract cannot accidentally reuse history without
// a real surface bridge.

#include "../RtxdiBridge/RAB_SurfaceCore.hlsli"

#ifndef REMIX_RAB_SURFACE_BRIDGE_EXTERNAL_CALLBACKS
RAB_Surface RemixRAB_LoadSurface(int2 pixel, bool previousFrame)
{
    return RAB_EmptySurface();
}
#endif

RAB_Surface RAB_GetGBufferSurface(int2 pixel, bool previousFrame)
{
    return RemixRAB_LoadSurface(pixel, previousFrame);
}

#endif
