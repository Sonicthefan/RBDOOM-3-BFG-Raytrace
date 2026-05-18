#ifndef RB_PATH_TRACING_RAB_MATERIAL_HLSLI
#define RB_PATH_TRACING_RAB_MATERIAL_HLSLI

struct RAB_Material
{
    uint materialId;
    uint materialIndex;
    uint flags;
    float alphaCutoff;
    float3 diffuseAlbedo;
    float roughness;
    float3 specularF0;
    float opacity;
    float3 emissiveRadiance;
    uint emissiveTextureIndex;
};

RAB_Material RAB_EmptyMaterial()
{
    RAB_Material material = (RAB_Material)0;
    material.roughness = 1.0;
    material.opacity = 1.0;
    return material;
}

float3 GetDiffuseAlbedo(RAB_Material material)
{
    return material.diffuseAlbedo;
}

float3 GetSpecularF0(RAB_Material material)
{
    return material.specularF0;
}

float GetRoughness(RAB_Material material)
{
    return material.roughness;
}

float RAB_MaterialLuminance(float3 value)
{
    return dot(max(value, float3(0.0, 0.0, 0.0)), float3(0.2126, 0.7152, 0.0722));
}

bool RAB_MaterialCompareRelativeDifference(float reference, float candidate, float threshold)
{
    return threshold <= 0.0 || abs(reference - candidate) <= threshold * max(reference, candidate);
}

uint RAB_MaterialSimilarityMode()
{
    return clamp((uint)max(RestirPTSurfaceInfo.x, 0.0), 0u, 5u);
}

bool RAB_AreMaterialsSimilarRTXDIWithMode(RAB_Material a, RAB_Material b, uint mode)
{
    const float roughnessThreshold = 0.5;
    const float reflectivityThreshold = 0.25;
    const float albedoThreshold = 0.25;

    if (mode == 5u)
    {
        return true;
    }

    if (mode != 4u && !RAB_MaterialCompareRelativeDifference(a.roughness, b.roughness, roughnessThreshold))
    {
        return false;
    }

    if (mode != 3u && abs(RAB_MaterialLuminance(a.specularF0) - RAB_MaterialLuminance(b.specularF0)) > reflectivityThreshold)
    {
        return false;
    }

    if (mode != 2u && abs(RAB_MaterialLuminance(a.diffuseAlbedo) - RAB_MaterialLuminance(b.diffuseAlbedo)) > albedoThreshold)
    {
        return false;
    }

    return true;
}

bool RAB_AreMaterialsSimilarRTXDI(RAB_Material a, RAB_Material b)
{
    return RAB_AreMaterialsSimilarRTXDIWithMode(a, b, 1u);
}

uint RAB_MaterialSimilarityFailureReasonRTXDI(RAB_Material a, RAB_Material b)
{
    const float roughnessThreshold = 0.5;
    const float reflectivityThreshold = 0.25;
    const float albedoThreshold = 0.25;

    if (!RAB_MaterialCompareRelativeDifference(a.roughness, b.roughness, roughnessThreshold))
    {
        return 1u;
    }

    if (abs(RAB_MaterialLuminance(a.specularF0) - RAB_MaterialLuminance(b.specularF0)) > reflectivityThreshold)
    {
        return 2u;
    }

    if (abs(RAB_MaterialLuminance(a.diffuseAlbedo) - RAB_MaterialLuminance(b.diffuseAlbedo)) > albedoThreshold)
    {
        return 3u;
    }

    return 0u;
}

bool RAB_AreMaterialsSimilar(RAB_Material a, RAB_Material b)
{
    const uint materialSimilarityMode = RAB_MaterialSimilarityMode();
    if (materialSimilarityMode == 0u)
    {
        return a.materialId == b.materialId && a.flags == b.flags;
    }
    return RAB_AreMaterialsSimilarRTXDIWithMode(a, b, materialSimilarityMode);
}

#endif
