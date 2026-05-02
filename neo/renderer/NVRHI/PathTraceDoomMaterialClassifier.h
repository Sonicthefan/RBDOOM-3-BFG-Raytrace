#pragma once

#include "../Material.h"

struct RtSmokeTranslucentClassifierInfo
{
    bool sortIsGuiOrSubview = false;
    bool sortIsDecal = false;
    bool sortIsPostProcess = false;
    bool polygonOffsetDecal = false;
    bool hasScreenTexgen = false;
    bool hasAdditiveBlend = false;
    bool hasAmbientStage = false;
    bool hasAmbientBlendStage = false;
    bool hasDiffuseStage = false;
    bool nameLooksGui = false;
    bool nameLooksParticle = false;
    bool nameLooksDecal = false;
    bool nameLooksGlass = false;
    bool nameLooksGlow = false;
    bool nameLooksSignage = false;
};

bool SmokeNameContainsAny(const idStr& name, const char* const* tokens, int tokenCount);
bool SmokeStageBlendUsesSourceAlpha(const shaderStage_t* stage);
bool SmokeStageIsAdditiveBlend(const shaderStage_t* stage);
bool SmokeStageIsFilterBlend(const shaderStage_t* stage, bool& blackKey);
const char* SmokeStageAlphaSemanticName(const shaderStage_t* stage);
RtSmokeTranslucentClassifierInfo BuildSmokeTranslucentClassifierInfo(const idMaterial* material);
void ResolveSmokeMaterialAlphaInfo(const idMaterial* material, bool& hasAlphaTest, float& alphaCutoff);
bool IsSmokeAdditiveDecalMaterial(const idMaterial* material);
bool IsSmokeAdditiveWhiteKeyMaterial(const idMaterial* material, const RtSmokeTranslucentClassifierInfo& classifier);
bool IsSmokeRgbKeyedBlendDecalMaterial(const idMaterial* material, const RtSmokeTranslucentClassifierInfo& classifier);
bool IsSmokeYCoCgDiffuseMapDecalMaterial(const idMaterial* material, const RtSmokeTranslucentClassifierInfo& classifier);
