#pragma once

// Per-frame Doom material texture discovery.
//
// Inspects visible Doom materials and registers texture metadata for later
// material table construction. Classification decides what kind of material we
// have; discovery records usable texture handles and fallback reasons.

#include <cstdint>
#include <vector>

class idMaterial;
struct viewDef_t;

struct RtSmokeMaterialMetadataFrameStats
{
    int registrations = 0;
    int cacheRefreshes = 0;
    int fullDiscovers = 0;
    int newEntries = 0;
    int duplicateSkips = 0;
    int idHydrationVisited = 0;
    int idHydrationDerived = 0;
    int idHydrationCacheHits = 0;
    int idHydrationCacheMisses = 0;
};

struct RtSmokeMaterialMetadataRegistrationTiming
{
    int metadataMs = 0;
    int validationMs = 0;
    int registrationMs = 0;
};

extern RtSmokeMaterialMetadataFrameStats g_smokeMaterialMetadataFrameStats;

bool RegisterSmokeMaterialTextureInfo(const idMaterial* material);
RtSmokeMaterialMetadataRegistrationTiming RegisterSmokeMaterialTextureInfoForFrame(const viewDef_t* viewDef, bool enabled);
RtSmokeMaterialMetadataRegistrationTiming RegisterSmokeWorldStaticMaterialTextureInfo(const viewDef_t* viewDef, bool enabled);
RtSmokeMaterialMetadataRegistrationTiming RegisterSmokeMaterialTextureInfoForMaterialIds(const std::vector<uint32_t>& materialIds, bool enabled);
