#include "precompiled.h"
#pragma hdrstop

#include "PathTraceSurfaceClassification.h"
#include "PathTraceDoomMaterialClassifier.h"
#include "PathTraceGuiSurfaces.h"
#include "../RenderCommon.h"

uint32_t SmokeSurfaceClassId(RtSmokeSurfaceClass surfaceClass)
{
    switch (surfaceClass)
    {
        case RtSmokeSurfaceClass::StaticWorld:
            return 0;
        case RtSmokeSurfaceClass::RigidEntity:
            return 1;
        case RtSmokeSurfaceClass::SkinnedDeformed:
            return 2;
        case RtSmokeSurfaceClass::ParticleAlpha:
            return 3;
        default:
            return 4;
    }
}

const char* SmokeSurfaceClassName(RtSmokeSurfaceClass surfaceClass)
{
    switch (surfaceClass)
    {
        case RtSmokeSurfaceClass::StaticWorld:
            return "static";
        case RtSmokeSurfaceClass::RigidEntity:
            return "rigid-entity";
        case RtSmokeSurfaceClass::SkinnedDeformed:
            return "skinned";
        case RtSmokeSurfaceClass::ParticleAlpha:
            return "particle/alpha";
        default:
            return "unknown";
    }
}

const char* SmokeSurfaceClassNameByIndex(int classIndex)
{
    if (classIndex < 0 || classIndex >= RT_SMOKE_CLASS_COUNT)
    {
        return "invalid";
    }

    return SmokeSurfaceClassName(static_cast<RtSmokeSurfaceClass>(classIndex));
}

uint32_t SmokeTranslucentSubtypeId(RtSmokeTranslucentSubtype subtype)
{
    switch (subtype)
    {
        case RtSmokeTranslucentSubtype::DecalGrime:
            return 0;
        case RtSmokeTranslucentSubtype::ObjectGlass:
            return 1;
        case RtSmokeTranslucentSubtype::SmokeParticle:
            return 2;
        case RtSmokeTranslucentSubtype::SignageGlow:
            return 3;
        case RtSmokeTranslucentSubtype::GuiScreen:
            return 5;
        case RtSmokeTranslucentSubtype::PortalWindow:
            return 4;
        default:
            return 6;
    }
}

const char* SmokeTranslucentSubtypeName(RtSmokeTranslucentSubtype subtype)
{
    switch (subtype)
    {
        case RtSmokeTranslucentSubtype::DecalGrime:
            return "decal/grime";
        case RtSmokeTranslucentSubtype::ObjectGlass:
            return "object-glass";
        case RtSmokeTranslucentSubtype::SmokeParticle:
            return "smoke/particle";
        case RtSmokeTranslucentSubtype::SignageGlow:
            return "signage/glow";
        case RtSmokeTranslucentSubtype::PortalWindow:
            return "portal/window";
        case RtSmokeTranslucentSubtype::GuiScreen:
            return "gui/screen";
        default:
            return "unknown";
    }
}

const char* SmokeTranslucentSubtypeNameByIndex(int subtypeIndex)
{
    if (subtypeIndex < 0 || subtypeIndex >= RT_SMOKE_TRANSLUCENT_SUBTYPE_COUNT)
    {
        return "invalid";
    }

    return SmokeTranslucentSubtypeName(static_cast<RtSmokeTranslucentSubtype>(subtypeIndex));
}

RtSmokeSurfaceClass ClassifySmokeSurface(const viewDef_t* viewDef, const drawSurf_t* drawSurf, const srfTriangles_t* tri)
{
    const viewEntity_t* space = drawSurf ? drawSurf->space : nullptr;
    const idRenderEntityLocal* entityDef = space ? space->entityDef : nullptr;
    const renderEntity_t* renderEntity = entityDef ? &entityDef->parms : nullptr;
    const idMaterial* material = drawSurf ? drawSurf->material : nullptr;

    if ((drawSurf && drawSurf->jointCache != 0) ||
        (tri && tri->staticModelWithJoints != nullptr) ||
        (renderEntity && renderEntity->joints != nullptr && renderEntity->numJoints > 0))
    {
        return RtSmokeSurfaceClass::SkinnedDeformed;
    }

    if (material)
    {
        const deform_t deform = material->Deform();
        if (IsSmokeGuiDrawSurface(drawSurf) ||
            material->Coverage() == MC_TRANSLUCENT ||
            deform == DFRM_SPRITE ||
            deform == DFRM_TUBE ||
            deform == DFRM_FLARE ||
            deform == DFRM_PARTICLE ||
            deform == DFRM_PARTICLE2 ||
            material->GetSort() >= SS_MEDIUM ||
            (space && space->modelDepthHack != 0.0f))
        {
            return RtSmokeSurfaceClass::ParticleAlpha;
        }
    }

    const idRenderModel* model = renderEntity ? renderEntity->hModel : nullptr;
    if ((viewDef && space == &viewDef->worldSpace) ||
        !entityDef ||
        (model && model->IsStaticWorldModel()) ||
        (drawSurf && idVertexCache::CacheIsStatic(drawSurf->ambientCache) && idVertexCache::CacheIsStatic(drawSurf->indexCache) && !entityDef))
    {
        return RtSmokeSurfaceClass::StaticWorld;
    }

    if (entityDef)
    {
        return RtSmokeSurfaceClass::RigidEntity;
    }

    return RtSmokeSurfaceClass::Unknown;
}

RtSmokeTranslucentSubtype ClassifySmokeTranslucentSubtype(const drawSurf_t* drawSurf)
{
    const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
    if (!material)
    {
        return RtSmokeTranslucentSubtype::Unknown;
    }

    const deform_t deform = material->Deform();
    const float sort = material->GetSort();
    const RtSmokeTranslucentClassifierInfo info = BuildSmokeTranslucentClassifierInfo(material);

    if (IsSmokeGuiDrawSurface(drawSurf) || info.hasScreenTexgen || info.nameLooksGui)
    {
        return RtSmokeTranslucentSubtype::GuiScreen;
    }

    if (info.sortIsPostProcess || info.sortIsGuiOrSubview)
    {
        return RtSmokeTranslucentSubtype::PortalWindow;
    }

    if (deform == DFRM_PARTICLE ||
        deform == DFRM_PARTICLE2 ||
        deform == DFRM_SPRITE ||
        deform == DFRM_TUBE ||
        deform == DFRM_FLARE ||
        sort >= SS_ALMOST_NEAREST ||
        info.nameLooksParticle)
    {
        return RtSmokeTranslucentSubtype::SmokeParticle;
    }

    if (info.nameLooksGlass)
    {
        return RtSmokeTranslucentSubtype::ObjectGlass;
    }

    if (info.hasAdditiveBlend ||
        (info.hasAmbientStage && !info.hasDiffuseStage && info.nameLooksGlow) ||
        (info.hasAmbientBlendStage && info.nameLooksGlow) ||
        (info.nameLooksGlow && !info.nameLooksDecal) ||
        info.nameLooksSignage)
    {
        return RtSmokeTranslucentSubtype::SignageGlow;
    }

    if (info.sortIsDecal || info.polygonOffsetDecal || info.nameLooksDecal)
    {
        return RtSmokeTranslucentSubtype::DecalGrime;
    }

    return RtSmokeTranslucentSubtype::Unknown;
}

uint32_t SmokeSurfaceClassAndSubtypeId(RtSmokeSurfaceClass surfaceClass, RtSmokeTranslucentSubtype subtype)
{
    uint32_t id = SmokeSurfaceClassId(surfaceClass);
    if (surfaceClass == RtSmokeSurfaceClass::ParticleAlpha)
    {
        id |= (SmokeTranslucentSubtypeId(subtype) << RT_SMOKE_TRANSLUCENT_SUBTYPE_SHIFT) & RT_SMOKE_TRANSLUCENT_SUBTYPE_MASK;
    }
    return id;
}
