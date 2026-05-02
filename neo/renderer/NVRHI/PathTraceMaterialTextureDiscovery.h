#pragma once

class idMaterial;

struct RtSmokeMaterialMetadataFrameStats
{
    int registrations = 0;
    int cacheRefreshes = 0;
    int fullDiscovers = 0;
    int newEntries = 0;
    int duplicateSkips = 0;
};

extern RtSmokeMaterialMetadataFrameStats g_smokeMaterialMetadataFrameStats;

void RegisterSmokeMaterialTextureInfo(const idMaterial* material);
