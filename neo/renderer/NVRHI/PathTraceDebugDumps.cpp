#include "precompiled.h"
#pragma hdrstop

#include "PathTraceDebugDumps.h"

const char* SmokeCoverageName(materialCoverage_t coverage)
{
    switch (coverage)
    {
        case MC_OPAQUE:
            return "opaque";
        case MC_PERFORATED:
            return "perforated";
        case MC_TRANSLUCENT:
            return "translucent";
        default:
            return "bad";
    }
}

const char* SmokeStageLightingName(stageLighting_t lighting)
{
    switch (lighting)
    {
        case SL_AMBIENT:
            return "ambient";
        case SL_BUMP:
            return "bump";
        case SL_DIFFUSE:
            return "diffuse";
        case SL_SPECULAR:
            return "specular";
        case SL_COVERAGE:
            return "coverage";
        default:
            return "unknown";
    }
}

const char* SmokeTexgenName(texgen_t texgen)
{
    switch (texgen)
    {
        case TG_EXPLICIT:
            return "explicit";
        case TG_DIFFUSE_CUBE:
            return "diffuse-cube";
        case TG_REFLECT_CUBE:
            return "reflect-cube";
        case TG_REFLECT_CUBE2:
            return "reflect-cube2";
        case TG_SKYBOX_CUBE:
            return "skybox-cube";
        case TG_WOBBLESKY_CUBE:
            return "wobblesky-cube";
        case TG_SCREEN:
            return "screen";
        case TG_SCREEN2:
            return "screen2";
        case TG_GLASSWARP:
            return "glasswarp";
        default:
            return "unknown";
    }
}

const char* SmokeTextureUsageName(textureUsage_t usage)
{
    switch (usage)
    {
        case TD_SPECULAR:
            return "TD_SPECULAR";
        case TD_DIFFUSE:
            return "TD_DIFFUSE";
        case TD_DEFAULT:
            return "TD_DEFAULT";
        case TD_BUMP:
            return "TD_BUMP";
        case TD_COVERAGE:
            return "TD_COVERAGE";
        default:
            return va("TD_%d", static_cast<int>(usage));
    }
}

const char* SmokeTextureColorFormatName(textureColor_t colorFormat)
{
    switch (colorFormat)
    {
        case CFM_DEFAULT:
            return "CFM_DEFAULT";
        case CFM_NORMAL_DXT5:
            return "CFM_NORMAL_DXT5";
        case CFM_YCOCG_DXT5:
            return "CFM_YCOCG_DXT5";
        case CFM_GREEN_ALPHA:
            return "CFM_GREEN_ALPHA";
        default:
            return va("CFM_%d", static_cast<int>(colorFormat));
    }
}

const char* SmokeDeformName(deform_t deform)
{
    switch (deform)
    {
        case DFRM_NONE:
            return "none";
        case DFRM_SPRITE:
            return "sprite";
        case DFRM_TUBE:
            return "tube";
        case DFRM_FLARE:
            return "flare";
        case DFRM_EXPAND:
            return "expand";
        case DFRM_MOVE:
            return "move";
        case DFRM_EYEBALL:
            return "eyeball";
        case DFRM_PARTICLE:
            return "particle";
        case DFRM_PARTICLE2:
            return "particle2";
        case DFRM_TURB:
            return "turb";
        default:
            return "unknown";
    }
}

const char* SmokeDynamicModelName(dynamicModel_t dynamicModel)
{
    switch (dynamicModel)
    {
        case DM_STATIC:
            return "static";
        case DM_CACHED:
            return "cached";
        case DM_CONTINUOUS:
            return "continuous";
        default:
            return "unknown";
    }
}
