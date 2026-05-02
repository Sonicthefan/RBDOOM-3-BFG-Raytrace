#pragma once

#include "PathTraceGeometry.h"

#include <vector>

struct drawSurf_t;
struct srfTriangles_t;
struct viewDef_t;
class idJointMat;

struct RtSmokeSurfaceSkipStats
{
    int nullSurface = 0;
    int missingGeometry = 0;
    int nullMaterial = 0;
    int nullSpace = 0;
    int nullModel = 0;
    int invalidIndexCount = 0;
    int nonCurrentCache = 0;
    int limitExceeded = 0;
    int zeroAreaOnly = 0;
    int emptyClassBuffer = 0;
    int guiSurface = 0;
    int callbackEntity = 0;
};

struct RtSmokeAttributeClassStats
{
    int invalidNormalVerts = 0;
    int invalidNormalTriangles = 0;
    int invalidUvVerts = 0;
    int invalidUvTriangles = 0;
    int forcedGeometricNormalTriangles = 0;
};

struct RtSmokeAttributeStats
{
    RtSmokeAttributeClassStats classes[5];
};

void TransformSurfacePointToWorld(const drawSurf_t* drawSurf, const idVec3& localPoint, idVec3& worldPoint);
void TransformSurfaceVectorToWorld(const drawSurf_t* drawSurf, const idVec3& localVector, idVec3& worldVector);
bool ValidateSmokeDrawSurface(const viewDef_t* viewDef, const drawSurf_t* drawSurf, const srfTriangles_t*& tri, RtSmokeSurfaceSkipStats* skipStats);
bool FindCenterCameraRayAnchor(const viewDef_t* viewDef, idVec3& anchorPoint, int& anchorSurface, int& anchorTriangle);
PathTraceSmokeVertex BuildSmokeSurfaceVertex(const drawSurf_t* drawSurf, const srfTriangles_t* tri, int vertexIndex, const idJointMat* rtCpuSkinningJoints);
void TransformSmokeSurfaceVertexToWorld(const drawSurf_t* drawSurf, const srfTriangles_t* tri, int vertexIndex, const idJointMat* rtCpuSkinningJoints, idVec3& worldPosition);
int AppendSmokeSurfaceGeometry(
    const drawSurf_t* drawSurf,
    const srfTriangles_t* tri,
    uint32_t surfaceClassId,
    uint32_t materialId,
    int classCount,
    uint32_t triangleClassMask,
    uint32_t particleAlphaClassId,
    uint32_t forceGeometricNormalFlag,
    std::vector<PathTraceSmokeVertex>& vertices,
    std::vector<uint32_t>& indexes,
    std::vector<uint32_t>& triangleClasses,
    std::vector<uint32_t>& triangleMaterials,
    RtSmokeSurfaceSkipStats& skipStats,
    RtSmokeAttributeStats& attributeStats);
