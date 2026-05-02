#include "precompiled.h"
#pragma hdrstop

#include "PathTraceGeometry.h"

#include <cmath>

bool SmokeFloatIsFinite(float value)
{
    return std::isfinite(value) != 0;
}

bool SmokeVec2IsFinite(const idVec2& value)
{
    return SmokeFloatIsFinite(value.x) && SmokeFloatIsFinite(value.y);
}

bool SmokeVec3IsFinite(const idVec3& value)
{
    return SmokeFloatIsFinite(value.x) && SmokeFloatIsFinite(value.y) && SmokeFloatIsFinite(value.z);
}

idVec3 SmokeVertexPosition(const PathTraceSmokeVertex& vertex)
{
    return idVec3(vertex.position[0], vertex.position[1], vertex.position[2]);
}

idVec3 SmokeVertexNormal(const PathTraceSmokeVertex& vertex)
{
    return idVec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
}

idVec2 SmokeVertexTexCoord(const PathTraceSmokeVertex& vertex)
{
    return idVec2(vertex.texCoord[0], vertex.texCoord[1]);
}

bool SmokeNormalIsUsable(const idVec3& normal)
{
    return SmokeVec3IsFinite(normal) && normal.LengthSqr() > 1.0e-8f;
}

bool SmokeTexCoordIsUsable(const idVec2& texCoord)
{
    return SmokeVec2IsFinite(texCoord) && idMath::Fabs(texCoord.x) < 1.0e6f && idMath::Fabs(texCoord.y) < 1.0e6f;
}

bool IsZeroAreaSmokeTriangle(const idVec3& p0, const idVec3& p1, const idVec3& p2)
{
    const idVec3 edge1 = p1 - p0;
    const idVec3 edge2 = p2 - p0;
    const float areaSqr = edge1.Cross(edge2).LengthSqr();
    return areaSqr <= 1.0e-8f;
}

bool IntersectRayTriangle(const idVec3& rayOrigin, const idVec3& rayDirection, const idVec3& p0, const idVec3& p1, const idVec3& p2, float& hitDistance)
{
    const idVec3 edge1 = p1 - p0;
    const idVec3 edge2 = p2 - p0;
    const idVec3 pvec = rayDirection.Cross(edge2);
    const float det = edge1 * pvec;
    if (idMath::Fabs(det) < 1.0e-8f)
    {
        return false;
    }

    const float invDet = 1.0f / det;
    const idVec3 tvec = rayOrigin - p0;
    const float u = (tvec * pvec) * invDet;
    if (u < 0.0f || u > 1.0f)
    {
        return false;
    }

    const idVec3 qvec = tvec.Cross(edge1);
    const float v = (rayDirection * qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f)
    {
        return false;
    }

    const float t = (edge2 * qvec) * invDet;
    if (t <= 0.1f)
    {
        return false;
    }

    hitDistance = t;
    return true;
}
