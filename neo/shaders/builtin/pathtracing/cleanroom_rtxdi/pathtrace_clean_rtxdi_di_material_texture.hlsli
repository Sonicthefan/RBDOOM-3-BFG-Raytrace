PathTraceSmokeMaterial PathTraceCleanRoomLoadSmokeMaterial(uint materialIndex)
{
    PathTraceSmokeMaterial material;
    material.debugAlbedo = float4(1.0, 0.0, 1.0, 1.0);
    material.emissiveColor = float4(0.0, 0.0, 0.0, 1.0);
    material.diffuseTextureIndex = 0xffffffffu;
    material.alphaTextureIndex = 0xffffffffu;
    material.normalTextureIndex = 0xffffffffu;
    material.specularTextureIndex = 0xffffffffu;
    material.emissiveTextureIndex = 0xffffffffu;
    material.alphaCutoff = 0.0;
    material.flags = 0u;
    material.textureWidth = 1u;
    material.textureHeight = 1u;
    material.alphaTextureWidth = 1u;
    material.alphaTextureHeight = 1u;
    material.normalTextureWidth = 1u;
    material.normalTextureHeight = 1u;
    material.specularTextureWidth = 1u;
    material.specularTextureHeight = 1u;
    material.emissiveTextureWidth = 1u;
    material.emissiveTextureHeight = 1u;
    material.padding0 = 0u;
    material.padding1 = 0u;
    material.padding2 = 0u;

    const uint materialCount = (uint)TextureInfo.z;
    if (materialIndex < materialCount)
    {
        material = SmokeMaterials[materialIndex];
    }

    return material;
}

bool PathTraceCleanRoomLiveMaterialClassifierBsdfActive(uint materialIndex)
{
    const PathTraceSmokeMaterial material = PathTraceCleanRoomLoadSmokeMaterial(materialIndex);
    return SmokeMatClassRoute(material) == RT_MATCLASS_ROUTE_REAL_PBR_RMAO ||
        SmokeMatClassDrivesLegacySpec(material) ||
        SmokeMatClassHasPackedBsdf(material);
}

void PathTraceCleanRoomApplyLiveMaterialClassifierBsdf(
    uint materialIndex,
    float3 albedo,
    inout float3 specularF0,
    inout float roughness)
{
    const PathTraceSmokeMaterial material = PathTraceCleanRoomLoadSmokeMaterial(materialIndex);
    SmokeApplyMaterialClassifierBsdf(material, albedo, specularF0, roughness);
}

float4 PathTraceCleanRoomLoadTextureTexel(uint textureIndex, uint2 texel, bool bindlessEnabled)
{
    return bindlessEnabled
        ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].Load(int3(texel, 0))
        : SmokeFallbackTexture.Load(int3(0, 0, 0));
}

float4 PathTraceCleanRoomSampleTextureLoad(uint textureIndex, uint textureWidth, uint textureHeight, float2 wrappedTexCoord, bool bindlessEnabled, bool bilinearFilter)
{
    const uint width = max(textureWidth, 1u);
    const uint height = max(textureHeight, 1u);
    if (!bilinearFilter || width == 1u || height == 1u)
    {
        const uint2 texel = min(uint2(wrappedTexCoord * float2(width, height)), uint2(width - 1u, height - 1u));
        return PathTraceCleanRoomLoadTextureTexel(textureIndex, texel, bindlessEnabled);
    }

    const float2 texelPosition = wrappedTexCoord * float2(width, height) - 0.5;
    const int2 baseTexel = int2(floor(texelPosition));
    const float2 fraction = frac(texelPosition);
    const uint2 textureSize = uint2(width, height);
    const uint2 texel00 = uint2((baseTexel + int2(0, 0) + int2(width, height)) % int2(textureSize));
    const uint2 texel10 = uint2((baseTexel + int2(1, 0) + int2(width, height)) % int2(textureSize));
    const uint2 texel01 = uint2((baseTexel + int2(0, 1) + int2(width, height)) % int2(textureSize));
    const uint2 texel11 = uint2((baseTexel + int2(1, 1) + int2(width, height)) % int2(textureSize));
    const float4 c00 = PathTraceCleanRoomLoadTextureTexel(textureIndex, texel00, bindlessEnabled);
    const float4 c10 = PathTraceCleanRoomLoadTextureTexel(textureIndex, texel10, bindlessEnabled);
    const float4 c01 = PathTraceCleanRoomLoadTextureTexel(textureIndex, texel01, bindlessEnabled);
    const float4 c11 = PathTraceCleanRoomLoadTextureTexel(textureIndex, texel11, bindlessEnabled);
    return lerp(lerp(c00, c10, fraction.x), lerp(c01, c11, fraction.x), fraction.y);
}

float4 PathTraceCleanRoomSampleTexture(uint textureIndex, uint textureWidth, uint textureHeight, float2 texCoord, float4 fallback)
{
    const uint textureCount = (uint)TextureInfo.x;
    const uint sampleMethod = (uint)TextureInfo.y;
    const uint textureFlags = (uint)TextureInfo.w;
    const bool bindlessEnabled = (textureFlags & 1u) != 0u;
    const bool bilinearFilter = (textureFlags & 2u) != 0u;
    if (sampleMethod == 0u || textureIndex == 0xffffffffu || textureIndex >= textureCount)
    {
        return fallback;
    }
    if (!all(texCoord == texCoord) || any(abs(texCoord) > 65536.0))
    {
        return fallback;
    }

    const float2 wrappedTexCoord = frac(texCoord);
    const float4 sampled = sampleMethod == 2u
        ? PathTraceCleanRoomSampleTextureLoad(textureIndex, textureWidth, textureHeight, wrappedTexCoord, bindlessEnabled, bilinearFilter)
        : (bindlessEnabled
            ? SmokeDiffuseTextures[NonUniformResourceIndex(textureIndex)].SampleLevel(SmokeMaterialSampler, wrappedTexCoord, 0.0)
            : SmokeFallbackTexture.SampleLevel(SmokeMaterialSampler, wrappedTexCoord, 0.0));
    if (!all(sampled == sampled) || any(abs(sampled) > 65504.0))
    {
        return fallback;
    }
    return sampled;
}

float3 PathTraceCleanRoomTexturedEmissiveRadiance(PathTraceUnifiedLightRecord light, bool previousFrame)
{
    const float emissiveScale = max(ToyPathInfo.z, 0.0);
    const float3 fallbackRadiance = max(light.radianceAndLuminance.rgb, float3(0.0, 0.0, 0.0)) * emissiveScale;
    if (emissiveScale <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const uint sourceIndex = light.sourceIndex;
    const uint emissiveCount = previousFrame ? CleanRtxdiDiPreviousEmissiveTriangleCount : CleanRtxdiDiCurrentEmissiveTriangleCount;
    if (sourceIndex >= emissiveCount)
    {
        return fallbackRadiance;
    }

    PathTraceSmokeEmissiveTriangle emissiveTriangle;
    if (previousFrame)
    {
        emissiveTriangle = SmokePreviousEmissiveTriangles[sourceIndex];
    }
    else
    {
        emissiveTriangle = SmokeEmissiveTriangles[sourceIndex];
    }
    if (emissiveTriangle.materialIndex >= (uint)TextureInfo.z)
    {
        return fallbackRadiance;
    }

    const PathTraceSmokeMaterial material = PathTraceCleanRoomLoadSmokeMaterial(emissiveTriangle.materialIndex);
    const bool activeEmissiveStage = (emissiveTriangle.padding0 & RT_SMOKE_TRIANGLE_EMISSIVE_STAGE_OFF) == 0u;
    if ((material.flags & RT_SMOKE_MATERIAL_EMISSIVE) == 0u ||
        !activeEmissiveStage ||
        ((((uint)TextureInfo.w) & RT_SMOKE_TEXTURE_FLAG_USE_EMISSIVE_MAPS) == 0u))
    {
        return fallbackRadiance;
    }

    float3 radiance = max(material.emissiveColor.rgb, float3(0.0, 0.0, 0.0));
    if (material.emissiveTextureIndex != 0xffffffffu)
    {
        radiance *= saturate(PathTraceCleanRoomSampleTexture(
            material.emissiveTextureIndex,
            material.emissiveTextureWidth,
            material.emissiveTextureHeight,
            emissiveTriangle.centroidUvAndWeight.xy,
            float4(1.0, 1.0, 1.0, 1.0)).rgb);
    }
    radiance *= 1.75 * emissiveScale;
    return PathTraceCleanRoomLuminance(radiance) > 0.0 ? radiance : fallbackRadiance;
}
