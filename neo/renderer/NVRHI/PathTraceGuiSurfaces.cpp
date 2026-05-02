#include "precompiled.h"
#pragma hdrstop

#include "PathTraceGuiSurfaces.h"
#include "../RenderCommon.h"

bool IsSmokeGuiDrawSurface(const drawSurf_t* drawSurf)
{
    if (!drawSurf)
    {
        return false;
    }

    if (drawSurf->space && drawSurf->space->isGuiSurface)
    {
        return true;
    }

    const idMaterial* material = drawSurf->material;
    return material && material->HasGui();
}
