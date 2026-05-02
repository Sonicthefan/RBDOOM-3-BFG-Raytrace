#pragma once

class idDrawVert;
class idJointMat;
class idVec3;
struct drawSurf_t;
struct srfTriangles_t;

const idJointMat* GetSmokeRtCpuSkinningJoints(const srfTriangles_t* tri);
idVec3 TransformSmokeSkinnedVertexPosition(const idDrawVert& base, const idJointMat* joints);
idVec3 TransformSmokeSkinnedVertexNormal(const idDrawVert& base, const idJointMat* joints);
bool SmokeSkinnedSurfaceLikelyBasePose(const drawSurf_t* drawSurf, const srfTriangles_t* tri);
