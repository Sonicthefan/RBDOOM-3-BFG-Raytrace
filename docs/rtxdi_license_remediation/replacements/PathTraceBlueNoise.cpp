// STUB implementation for PathTraceBlueNoise.h — see header for
// integration tasks. File I/O calls are sketched with idlib-style usage;
// the integrating agent should match whatever the neighboring PT resource
// files actually use (idLib::fileSystem vs fopen) and the engine's
// logging/error conventions.

#include "PathTraceBlueNoise.h"

#include <cstring>
#include <vector>

bool PathTraceBlueNoise::Init( nvrhi::IDevice* device, nvrhi::ICommandList* commandList )
{
    const size_t layerBytes = size_t( PT_BLUE_NOISE_SIZE ) * PT_BLUE_NOISE_SIZE;
    const size_t totalBytes = layerBytes * PT_BLUE_NOISE_LAYERS;

    std::vector<unsigned char> blob( totalBytes );

    // TODO(integration): resolve path through the engine VFS, e.g.
    // base/textures/bluenoise/stbn_scalar_128x128x64.raw
    if( !LoadMaskBlob( "textures/bluenoise/stbn_scalar_128x128x64.raw", blob.data(), totalBytes ) )
    {
        return false;
    }

    nvrhi::TextureDesc desc;
    desc.width = PT_BLUE_NOISE_SIZE;
    desc.height = PT_BLUE_NOISE_SIZE;
    desc.arraySize = PT_BLUE_NOISE_LAYERS;
    desc.dimension = nvrhi::TextureDimension::Texture2DArray;
    desc.format = nvrhi::Format::R8_UNORM; // shader reads .x as [0,1)
    desc.mipLevels = 1;
    desc.debugName = "PathTraceBlueNoiseMasks";
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    desc.keepInitialState = true;

    texture = device->createTexture( desc );
    if( !texture )
    {
        return false;
    }

    commandList->open();
    for( int layer = 0; layer < PT_BLUE_NOISE_LAYERS; layer++ )
    {
        // Row pitch = width for tightly packed R8 source data.
        commandList->writeTexture( texture,
                                   /* arraySlice = */ layer,
                                   /* mipLevel   = */ 0,
                                   blob.data() + size_t( layer ) * layerBytes,
                                   /* rowPitch   = */ PT_BLUE_NOISE_SIZE );
    }
    commandList->close();
    device->executeCommandList( commandList );

    return true;
}

void PathTraceBlueNoise::Shutdown()
{
    texture = nullptr;
}

bool PathTraceBlueNoise::LoadMaskBlob( const char* path, void* dest, size_t destBytes )
{
    // TODO(integration): replace with the engine's file API, e.g.:
    //   idFile* f = fileSystem->OpenFileRead( path );
    //   if not f or f->Length() != destBytes: return false
    //   f->Read( dest, destBytes ); fileSystem->CloseFile( f );
    // Reject short/oversized files outright — a truncated mask silently
    // degrades the noise spectrum, which is painful to diagnose visually.
    ( void )path;
    ( void )dest;
    ( void )destBytes;
    return false; // stub: always fail until wired to the VFS
}
