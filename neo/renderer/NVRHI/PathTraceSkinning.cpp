#include "precompiled.h"
#pragma hdrstop

#include "PathTraceSkinning.h"
#include "../RenderCommon.h"
#include "../Model_local.h"

const idJointMat* GetSmokeRtCpuSkinningJoints(const srfTriangles_t* tri)
{
    if (!r_useGPUSkinning.GetBool() || !tri || !tri->staticModelWithJoints || !tri->staticModelWithJoints->jointsInverted)
    {
        return nullptr;
    }

    return tri->staticModelWithJoints->jointsInverted;
}

idVec3 TransformSmokeSkinnedVertexPosition(const idDrawVert& base, const idJointMat* joints)
{
    const idJointMat& j0 = joints[base.color[0]];
    const idJointMat& j1 = joints[base.color[1]];
    const idJointMat& j2 = joints[base.color[2]];
    const idJointMat& j3 = joints[base.color[3]];

    const float w0 = base.color2[0] * (1.0f / 255.0f);
    const float w1 = base.color2[1] * (1.0f / 255.0f);
    const float w2 = base.color2[2] * (1.0f / 255.0f);
    const float w3 = base.color2[3] * (1.0f / 255.0f);

    idJointMat accum;
    idJointMat::Mul(accum, j0, w0);
    idJointMat::Mad(accum, j1, w1);
    idJointMat::Mad(accum, j2, w2);
    idJointMat::Mad(accum, j3, w3);

    return accum * idVec4(base.xyz.x, base.xyz.y, base.xyz.z, 1.0f);
}

idVec3 TransformSmokeSkinnedVertexNormal(const idDrawVert& base, const idJointMat* joints)
{
    const idJointMat& j0 = joints[base.color[0]];
    const idJointMat& j1 = joints[base.color[1]];
    const idJointMat& j2 = joints[base.color[2]];
    const idJointMat& j3 = joints[base.color[3]];

    const float w0 = base.color2[0] * (1.0f / 255.0f);
    const float w1 = base.color2[1] * (1.0f / 255.0f);
    const float w2 = base.color2[2] * (1.0f / 255.0f);
    const float w3 = base.color2[3] * (1.0f / 255.0f);

    idJointMat accum;
    idJointMat::Mul(accum, j0, w0);
    idJointMat::Mad(accum, j1, w1);
    idJointMat::Mad(accum, j2, w2);
    idJointMat::Mad(accum, j3, w3);

    idVec3 normal = accum * base.GetNormal();
    normal.Normalize();
    return normal;
}

idVec3 TransformSmokeSkinnedVertexTangent(const idDrawVert& base, const idJointMat* joints)
{
    const idJointMat& j0 = joints[base.color[0]];
    const idJointMat& j1 = joints[base.color[1]];
    const idJointMat& j2 = joints[base.color[2]];
    const idJointMat& j3 = joints[base.color[3]];

    const float w0 = base.color2[0] * (1.0f / 255.0f);
    const float w1 = base.color2[1] * (1.0f / 255.0f);
    const float w2 = base.color2[2] * (1.0f / 255.0f);
    const float w3 = base.color2[3] * (1.0f / 255.0f);

    idJointMat accum;
    idJointMat::Mul(accum, j0, w0);
    idJointMat::Mad(accum, j1, w1);
    idJointMat::Mad(accum, j2, w2);
    idJointMat::Mad(accum, j3, w3);

    idVec3 tangent = accum * base.GetTangent();
    tangent.Normalize();
    return tangent;
}

bool SmokeSkinnedSurfaceLikelyBasePose(const drawSurf_t* drawSurf, const srfTriangles_t* tri)
{
    const viewEntity_t* space = drawSurf ? drawSurf->space : nullptr;
    const idRenderEntityLocal* entityDef = space ? space->entityDef : nullptr;
    const renderEntity_t* renderEntity = entityDef ? &entityDef->parms : nullptr;
    return (drawSurf && drawSurf->jointCache != 0) ||
        (tri && tri->staticModelWithJoints != nullptr) ||
        (renderEntity && renderEntity->joints != nullptr && renderEntity->numJoints > 0);
}
