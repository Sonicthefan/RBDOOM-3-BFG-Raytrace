#pragma once

// Live drawSurf mirror for the future PT scene producer.
//
// The mirror observes the final raster-submitted surfaces as mesh identities
// plus per-frame instances. Source mode 3 can also append non-static mirror
// records into the existing shader-compatible dynamic frame bucket.

#include "PathTraceInstanceUniverse.h"

#include <vector>

class RtSmokeGeometryUniverse;
class RtPathTraceSceneUniverse;
struct PathTraceSmokeVertex;
struct RtSmokeSkinnedSurfaceRecord;
struct viewDef_t;

const int RT_PT_BOUNDS_OVERLAY_MAX_LINES = 4096;

struct RtPathTraceBoundsOverlayLine
{
    idVec4 startAndPad = idVec4(0.0f, 0.0f, 0.0f, 0.0f);
    idVec4 endAndPad = idVec4(0.0f, 0.0f, 0.0f, 0.0f);
    idVec4 color = idVec4(1.0f, 1.0f, 1.0f, 1.0f);
};

void CapturePathTraceDrawSurfMirror(
    const viewDef_t* viewDef,
    const RtPathTraceSceneUniverse* sceneUniverse,
    RtSmokeGeometryUniverse* geometryUniverse,
    RtPathTraceInstanceUniverse& instanceUniverse,
    std::vector<RtPathTraceBoundsOverlayLine>* boundsOverlayLines = nullptr);

bool CapturePathTraceDynamicFrameFromDrawSurfMirror(
    const viewDef_t* viewDef,
    const RtPathTraceSceneUniverse* sceneUniverse,
    const RtSmokeGeometryUniverse* geometryUniverse,
    std::vector<PathTraceSmokeVertex>& vertexData,
    std::vector<uint32_t>& indexData,
    std::vector<uint32_t>& triangleClassData,
    std::vector<uint32_t>& triangleMaterialData,
    int& sourceSurfaces,
    int& sourceVerts,
    int& sourceIndexes,
    RtSmokeSurfaceClassStats& classStats,
    RtSmokeSurfaceSkipStats& skipStats,
    RtSmokeDynamicGeometryStats& dynamicStats,
    RtSmokeAttributeStats& attributeStats,
    RtSmokeMaterialStats& materialStats,
    RtSmokeBucketRanges& bucketRanges,
    RtSmokeSceneCaptureTiming& captureTiming,
    RtSmokeSurfaceClassReasonSamples* reasonSamples,
    std::vector<RtSmokeSkinnedSurfaceRecord>* skinnedSurfaceRecords = nullptr);
