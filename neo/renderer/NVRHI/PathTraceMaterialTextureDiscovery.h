#pragma once

class idMaterial;
struct viewDef_t;

struct RtSmokeMaterialMetadataFrameStats
{
    int registrations = 0;
    int cacheRefreshes = 0;
    int fullDiscovers = 0;
    int newEntries = 0;
    int duplicateSkips = 0;
};

struct RtSmokeMaterialMetadataRegistrationTiming
{
    int metadataMs = 0;
    int validationMs = 0;
    int registrationMs = 0;
};

extern RtSmokeMaterialMetadataFrameStats g_smokeMaterialMetadataFrameStats;

void RegisterSmokeMaterialTextureInfo(const idMaterial* material);
RtSmokeMaterialMetadataRegistrationTiming RegisterSmokeMaterialTextureInfoForFrame(const viewDef_t* viewDef, bool enabled);
