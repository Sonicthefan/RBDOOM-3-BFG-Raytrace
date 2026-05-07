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

bool RAB_AreMaterialsSimilar(RAB_Material a, RAB_Material b)
{
    return a.materialId == b.materialId && a.flags == b.flags;
}

#endif
