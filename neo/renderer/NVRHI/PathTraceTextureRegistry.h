#pragma once

#include <nvrhi/nvrhi.h>
#include <vector>

class idImage;

bool IsSmokeDiffuseTextureSafeForRayTracing(nvrhi::ITexture* texture);
bool IsSmokeImageNameSafeForRayTracing(const char* imageName);
bool IsSmokeImageNameGuiLike(const char* imageName);
bool IsSmokeDiffuseImageSafeForRayTracing(idImage* image);
bool IsSmokeTextureHandleSafeForDescriptor(nvrhi::TextureHandle texture);
bool SmokeTextureHandleListsEqual(const std::vector<nvrhi::TextureHandle>& lhs, const std::vector<nvrhi::TextureHandle>& rhs);
