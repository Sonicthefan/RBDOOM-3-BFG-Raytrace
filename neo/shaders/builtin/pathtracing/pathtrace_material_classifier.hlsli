#ifndef RB_PATH_TRACE_MATERIAL_CLASSIFIER_HLSLI
#define RB_PATH_TRACE_MATERIAL_CLASSIFIER_HLSLI

static const uint RT_MATCLASS_ROUTE_REAL_PBR_RMAO = 1u;
static const uint RT_MATCLASS_ROUTE_LEGACY_SPEC_GLOSS = 2u;
static const uint RT_MATCLASS_ROUTE_SURFACE_TYPE_FALLBACK = 3u;
static const uint RT_MATCLASS_ROUTE_SHIFT = 10u;
static const uint RT_MATCLASS_ROUTE_MASK = 0x0fu;
static const uint RT_MATCLASS_SURFACE_CLASS_METAL = 1u;
static const uint RT_MATCLASS_SURFACE_CLASS_RICOCHET = 9u;
static const uint RT_MATCLASS_SURFACE_CLASS_SPECIAL = 10u;
static const uint RT_MATCLASS_EMISSIVE_INTENT = 0x02000000u;
static const uint RT_SMOKE_MATERIAL_CLASSIFIER_DRIVE_LEGACY_SPEC = 0x00000002u;

uint SmokeMatClassRoute(PathTraceSmokeMaterial material)
{
    return (material.padding1 >> RT_MATCLASS_ROUTE_SHIFT) & RT_MATCLASS_ROUTE_MASK;
}

uint SmokeMatClassSurfaceClass(PathTraceSmokeMaterial material)
{
    return material.padding1 & 0x0fu;
}

float SmokeMatClassUnpackUnit8(uint packed, uint shift)
{
    return (float((packed >> shift) & 0xffu)) * (1.0 / 255.0);
}

float SmokeMatClassRoughness(PathTraceSmokeMaterial material)
{
    return SmokeMatClassUnpackUnit8(material.padding2, 0u);
}

float SmokeMatClassMetallic(PathTraceSmokeMaterial material)
{
    return SmokeMatClassUnpackUnit8(material.padding2, 8u);
}

float SmokeMatClassTransmission(PathTraceSmokeMaterial material)
{
    return SmokeMatClassUnpackUnit8(material.padding2, 16u);
}

float SmokeMatClassF0(PathTraceSmokeMaterial material)
{
    return SmokeMatClassUnpackUnit8(material.padding2, 24u);
}

bool SmokeMatClassHasPackedBsdf(PathTraceSmokeMaterial material)
{
    const uint route = SmokeMatClassRoute(material);
    const uint surfaceClass = SmokeMatClassSurfaceClass(material);
    const bool specialEmissiveRouteB =
        route == RT_MATCLASS_ROUTE_LEGACY_SPEC_GLOSS &&
        surfaceClass == RT_MATCLASS_SURFACE_CLASS_SPECIAL &&
        (material.padding1 & RT_MATCLASS_EMISSIVE_INTENT) != 0u;
    return route == RT_MATCLASS_ROUTE_SURFACE_TYPE_FALLBACK || specialEmissiveRouteB;
}

bool SmokeMatClassDrivesLegacySpec(PathTraceSmokeMaterial material)
{
    const uint route = SmokeMatClassRoute(material);
    const uint surfaceClass = SmokeMatClassSurfaceClass(material);
    return
        (material.padding0 & RT_SMOKE_MATERIAL_CLASSIFIER_DRIVE_LEGACY_SPEC) != 0u &&
        route == RT_MATCLASS_ROUTE_LEGACY_SPEC_GLOSS &&
        surfaceClass == RT_MATCLASS_SURFACE_CLASS_RICOCHET;
}

float3 SmokeMatClassMetallicF0(PathTraceSmokeMaterial material, float3 albedo)
{
    const float classifiedMetallic = SmokeMatClassMetallic(material);
    const float classifiedF0 = SmokeMatClassF0(material);
    const float3 dielectricF0 = float3(classifiedF0, classifiedF0, classifiedF0);
    return lerp(dielectricF0, max(saturate(albedo), dielectricF0), saturate(classifiedMetallic));
}

void SmokeApplyMaterialClassifierBsdf(PathTraceSmokeMaterial material, float3 albedo, inout float3 specularF0, inout float roughness)
{
    if (SmokeMatClassDrivesLegacySpec(material))
    {
        specularF0 = max(specularF0, SmokeMatClassMetallicF0(material, albedo));
        return;
    }

    if (!SmokeMatClassHasPackedBsdf(material))
    {
        return;
    }

    const float classifiedRoughness = SmokeMatClassRoughness(material);
    const bool hasIncomingTexturedRoughness =
        material.specularTextureIndex != 0xffffffffu &&
        roughness < 0.999;

    if (!hasIncomingTexturedRoughness)
    {
        roughness = saturate(classifiedRoughness);
    }
    specularF0 = SmokeMatClassMetallicF0(material, albedo);
}

#endif
