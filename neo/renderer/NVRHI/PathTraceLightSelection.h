#pragma once

// Debug light selection for smoke ray generation constants.
//
// Dispatch uses this module to choose a small, stable set of Doom lights for
// modes that exercise direct lighting before full path tracing light sampling
// exists.

#include "../RenderCommon.h"

const int RT_SMOKE_MAX_DEBUG_LIGHTS = 32;

struct RtSmokeSelectedLight
{
    idVec3 origin = vec3_zero;
    float radius = 0.0f;
    idVec4 color = idVec4(1.0f, 1.0f, 1.0f, 1.0f);
    bool spriteProxy = false;
    int index = -1;
    float distanceSquared = idMath::INFINITUM;
    float score = 0.0f;
    idStr shaderName;
};

int CollectSelectedSmokePointLights(const viewDef_t* viewDef, const idVec3& cameraOrigin, RtSmokeSelectedLight* selectedLights, int maxSelectedLights, int selectionMode);
