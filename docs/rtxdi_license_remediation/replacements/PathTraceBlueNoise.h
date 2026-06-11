// rbdoom-owned blue-noise mask resource for the RandomSamplerState
// replacement (see replacements/RandomSamplerState.hlsli).
//
// STUB: not wired into the build. Integration tasks:
//   - move to neo/renderer/NVRHI/, add to CMake
//   - call Init() where other PT global resources are created
//     (PathTraceFrameResources pattern), Shutdown() with them
//   - bind GetTexture() to the t_RbptBlueNoise slot of every PT binding
//     layout whose shader defines RBPT_ENABLE_BLUE_NOISE
//   - register the cvar alongside the other r_pathTracing* cvars

#ifndef PATH_TRACE_BLUE_NOISE_H
#define PATH_TRACE_BLUE_NOISE_H

#include <nvrhi/nvrhi.h>

// Must match RBPT_BLUE_NOISE_SIZE / RBPT_BLUE_NOISE_LAYERS in the shader.
static const int PT_BLUE_NOISE_SIZE = 128;
static const int PT_BLUE_NOISE_LAYERS = 64;

class PathTraceBlueNoise
{
public:
    // Loads the mask set and creates the texture array. Returns false if
    // the mask file is missing or malformed; callers must treat blue noise
    // as unavailable (do not define RBPT_ENABLE_BLUE_NOISE shader-side or
    // bind a 1x1 dummy and force the cvar off).
    bool Init( nvrhi::IDevice* device, nvrhi::ICommandList* commandList );
    void Shutdown();

    bool IsValid() const { return texture != nullptr; }
    nvrhi::TextureHandle GetTexture() const { return texture; }

private:
    // Mask file format (deliberately trivial so any generator can emit it):
    //   raw unsigned bytes, layer-major:
    //   layers * size * size bytes, uploaded as R8_UNORM (decodes v / 255).
    // Short term the NVIDIA STBN scalar set converted to this raw layout is
    // acceptable for local testing ONLY (license: swap before any release;
    // tracked in impacted_files.txt). Long term: own void-and-cluster /
    // STBN generator writing the same blob.
    bool LoadMaskBlob( const char* path, /*out*/ void* dest, size_t destBytes );

    nvrhi::TextureHandle texture;
};

// Suggested cvar (register in PathTraceCVars.cpp):
//   r_pathTracingBlueNoise  0 = white-noise hash only (default until proven)
//                           1 = blue-noise masks for leading dimensions
// The toggle selects which shader permutation / define is used; the RNG
// falls back to white noise automatically wherever pixel identity is lost.

#endif
