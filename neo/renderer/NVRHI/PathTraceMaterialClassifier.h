#pragma once

// New path-tracing material classifier.
//
// This stack is intentionally separate from PathTraceDoomMaterialClassifier and
// RtSmokeMaterialUniverseFacts. It owns route/surface/BSDF metadata for the new
// material-classifier path while the legacy renderer remains the default path.

#include "PathTraceTextureRegistry.h"

class idMaterial;

enum class RtMaterialSurfaceClass : uint8_t
{
    Unknown = 0,
    Metal,
    Stone,
    Flesh,
    Wood,
    Cardboard,
    Liquid,
    Glass,
    Plastic,
    Ricochet,
    Special
};

enum class RtMaterialClassConfidence : uint8_t
{
    Authoritative = 0,
    Flag,
    Heuristic,
    FallbackNone
};

enum class RtMaterialBsdfRoute : uint8_t
{
    Unknown = 0,
    RealPbrRmao,
    LegacySpecGloss,
    SurfaceTypeFallback
};

enum class RtMaterialSurfaceClassReason : uint8_t
{
    Unknown = 0,
    MaterialSort,
    SurfaceType,
    StageKind,
    NameToken,
    ImageNameToken,
    FallbackMetal,
    FallbackUnknown
};

enum class RtMaterialBsdfRouteReason : uint8_t
{
    Unknown = 0,
    PbrRmaoStage,
    LegacySpecularStage,
    RmaoDisabled,
    NoCompatibleSpecularStage
};

enum class RtMaterialNormalDecodeMode : uint8_t
{
    None = 0,
    Rgb8Rg,
    CompressedWy
};

struct RtMaterialStageFacts
{
    int stageCount = 0;
    int bumpStages = 0;
    int diffuseStages = 0;
    int specularStages = 0;
    int pbrRmaoStages = 0;
    int legacySpecStages = 0;
    int ambientStages = 0;
    int additiveBlendStages = 0;
    int filterBlendStages = 0;
    int alphaBlendStages = 0;
    int coverageStages = 0;
    int guiOrScreenStages = 0;
    int dynamicImageStages = 0;
    int cinematicStages = 0;
    int cubeMapStages = 0;
    int customProgramStages = 0;
    int effectStages = 0;
    int firstDiffuseStage = -1;
    int firstBumpStage = -1;
    int firstSpecularStage = -1;
    int routeStage = -1;
};

struct RtMaterialBsdfParams
{
    float roughness = 0.7f;
    float metallic = 0.0f;
    float ior = 1.5f;
    float transmission = 0.0f;
    float specularF0 = 0.04f;
    float ao = 1.0f;
    uint8_t subsurfaceHint = 0;
    uint8_t twoSidedBsdf = 0;
};

struct RtMaterialRecord
{
    bool valid = false;
    uint64 signature = 0;
    uint32_t materialId = 0;
    idStr materialName;
    idStr diffuseImageName;
    idStr normalImageName;
    idStr specularImageName;
    idStr emissiveImageName;
    idStr surfaceClassEvidence;
    idStr bsdfEvidence;
    int rawSurfaceType = 0;
    int surfaceFlags = 0;
    float sort = 0.0f;
    materialCoverage_t coverage = MC_BAD;
    RtMaterialSurfaceClass surfaceClass = RtMaterialSurfaceClass::Unknown;
    RtMaterialClassConfidence surfaceClassConfidence = RtMaterialClassConfidence::FallbackNone;
    RtMaterialBsdfRoute route = RtMaterialBsdfRoute::Unknown;
    RtMaterialSurfaceClassReason surfaceClassReason = RtMaterialSurfaceClassReason::Unknown;
    RtMaterialBsdfRouteReason routeReason = RtMaterialBsdfRouteReason::Unknown;
    RtMaterialNormalDecodeMode normalDecodeMode = RtMaterialNormalDecodeMode::None;
    RtMaterialStageFacts stageFacts;
    RtMaterialBsdfParams bsdf;
    textureUsage_t diffuseUsage = TD_DEFAULT;
    textureUsage_t normalUsage = TD_DEFAULT;
    textureUsage_t specularUsage = TD_DEFAULT;
    textureUsage_t emissiveUsage = TD_DEFAULT;
    textureColor_t diffuseColorFormat = CFM_DEFAULT;
    textureColor_t normalColorFormat = CFM_DEFAULT;
    textureColor_t specularColorFormat = CFM_DEFAULT;
    textureColor_t emissiveColorFormat = CFM_DEFAULT;
    float specularRepresentativeRgb[3] = { 0.0f, 0.0f, 0.0f };
    float specularRepresentativeLuma = 0.0f;
    bool hasDiffuseImage = false;
    bool hasNormalImage = false;
    bool hasSpecularImage = false;
    bool hasEmissiveImage = false;
    bool alphaTested = false;
    bool emissiveIntent = false;
};

struct RtMaterialClassifierStats
{
    int records = 0;
    int hits = 0;
    int misses = 0;
    int rebuilds = 0;
    int frameHits = 0;
    int frameMisses = 0;
    int frameRebuilds = 0;
    int routeRealPbr = 0;
    int routeLegacySpec = 0;
    int routeFallback = 0;
    int confidenceAuthoritative = 0;
    int confidenceFlag = 0;
    int confidenceHeuristic = 0;
    int confidenceFallbackNone = 0;
};

const char* RtMaterialSurfaceClassName(RtMaterialSurfaceClass surfaceClass);
const char* RtMaterialClassConfidenceName(RtMaterialClassConfidence confidence);
const char* RtMaterialBsdfRouteName(RtMaterialBsdfRoute route);
const char* RtMaterialSurfaceClassReasonName(RtMaterialSurfaceClassReason reason);
const char* RtMaterialBsdfRouteReasonName(RtMaterialBsdfRouteReason reason);
const char* RtMaterialNormalDecodeModeName(RtMaterialNormalDecodeMode mode);

void BeginPathTraceMaterialClassifierFrame();
const RtMaterialRecord& RegisterPathTraceMaterialRecord(const idMaterial* material, const RtSmokeMaterialTextureInfo& info);
const RtMaterialRecord* FindPathTraceMaterialRecord(uint32_t materialId);
RtMaterialClassifierStats GetPathTraceMaterialClassifierStats();
uint32_t GetPathTraceMaterialClassifierGeneration();
uint32_t PackPathTraceMaterialClassifierFlags(const RtMaterialRecord& record);
uint32_t PackPathTraceMaterialClassifierParams(const RtMaterialRecord& record);
void MaybeDumpPathTraceMaterialDeclSurfaceTypeDistribution();
