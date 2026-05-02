#pragma once

// Shared GPU-facing geometry structs and pure geometry helpers.
//
// Keep these definitions small and layout-conscious: PathTraceSmokeVertex is
// consumed by NVRHI buffers and shader code, while the helpers are common math
// utilities used by capture, acceleration, and emissive inventory code.

struct PathTraceSmokeVertex
{
    float position[4];
    float normal[4];
    float texCoord[4];
    float color[4];
    float color2[4];
};

bool SmokeFloatIsFinite(float value);
bool SmokeVec2IsFinite(const idVec2& value);
bool SmokeVec3IsFinite(const idVec3& value);
idVec3 SmokeVertexPosition(const PathTraceSmokeVertex& vertex);
idVec3 SmokeVertexNormal(const PathTraceSmokeVertex& vertex);
idVec2 SmokeVertexTexCoord(const PathTraceSmokeVertex& vertex);
bool SmokeNormalIsUsable(const idVec3& normal);
bool SmokeTexCoordIsUsable(const idVec2& texCoord);
bool IsZeroAreaSmokeTriangle(const idVec3& p0, const idVec3& p1, const idVec3& p2);
bool IntersectRayTriangle(const idVec3& rayOrigin, const idVec3& rayDirection, const idVec3& p0, const idVec3& p1, const idVec3& p2, float& hitDistance);
