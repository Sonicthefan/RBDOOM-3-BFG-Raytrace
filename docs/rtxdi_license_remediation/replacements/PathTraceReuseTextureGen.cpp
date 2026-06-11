// Reciprocal spatial-reuse texture generator.
// Implements ReSTIR PT Enhanced (Lin, Kettunen, Wyman 2026, CC BY 4.0),
// Section 3.1: Gaussian-permuted self-inverting neighbor link textures.
//
// STUB status: the algorithm is complete and self-contained (CPU only).
// Integration tasks: move next to the other PT C++, create one
// R16G16_SINT texture per neighbor slot from Generate()'s output (use
// different sizes per slot, e.g. 254/230/210/186, to avoid tiling beats),
// re-roll MakeFrameTransformBits() each frame and pass it to the shader
// constant consumed by RBPT_GIReuseNeighborDelta() in
// GISpatialResampling.hlsli.
//
// How it works (paper Sec. 3.1): fill the texture with link indices so
// each index occupies two adjacent pixels. Repeatedly shuffle tiled 2x2
// blocks with random permutations, offsetting every other iteration
// diagonally by one so information crosses block borders. Each copy of an
// index performs a random walk; after enough iterations the offset between
// the two copies of each index follows a 2D normal distribution. Pixels
// holding the same index become reuse partners; the texture is
// self-inverting by construction (a links to b <=> b links to a).
//
// Iteration count: paper Eq. 3 (verified against the published PDF),
//   n_sigma = floor( sigma^2/2 + 1.46 sigma^-1 + 1.76 sigma^-2
//                    + 0.656 sigma^-3 + 0.5 )
// used as the primary mechanism. The measured stddev of the link deltas is
// still computed and returned so integrators can assert the fit holds
// (achievedSigma should land near targetSigma; a large mismatch means the
// shuffle pass was altered).

#include <cstdint>
#include <cmath>
#include <random>
#include <vector>

struct ReuseTextureResult
{
    int size = 0;                  // texture is size x size
    std::vector<int16_t> deltas;   // size*size*2, (dx,dy) per pixel,
                                   // each in [-size/2, size/2)
    int iterations = 0;            // shuffle iterations performed
    float achievedSigma = 0.0f;    // measured stddev of link distance
};

class PathTraceReuseTextureGen
{
public:
    // size: even, <= 254 recommended (deltas must fit int16 after wrap;
    //   254 also avoids the +/-128 edge case the paper footnotes for 256).
    // targetSigma: desired per-axis stddev of neighbor distance in pixels.
    //   Smallest supported is 0.8 (one shuffle iteration, paper footnote 1).
    static ReuseTextureResult Generate(
        int size, float targetSigma, uint32_t seed )
    {
        ReuseTextureResult result;
        result.size = size;

        const int pixelCount = size * size;
        std::mt19937 rng( seed );

        // Initial layout: horizontally adjacent pairs share a link index,
        // index = pixelLinearIndex / 2 ("consecutive link indices").
        std::vector<int32_t> link( pixelCount );
        for( int i = 0; i < pixelCount; i++ )
        {
            link[i] = i / 2;
        }

        // Paper Eq. 3; the negative-power terms are fit corrections for
        // small sigma.
        const double s = double( targetSigma > 0.8f ? targetSigma : 0.8f );
        const int nSigma = int( std::floor(
            s * s / 2.0 + 1.46 / s + 1.76 / ( s * s )
            + 0.656 / ( s * s * s ) + 0.5 ) );
        result.iterations = nSigma > 1 ? nSigma : 1;

        for( int iter = 0; iter < result.iterations; iter++ )
        {
            ShufflePass( link, size, ( iter & 1 ) != 0, rng );
        }

        // Cross-check only: should land near targetSigma. A large mismatch
        // means the shuffle pass was altered; assert/log at call site.
        result.achievedSigma = MeasureSigma( link, size );

        result.deltas = BuildDeltas( link, size );
        return result;
    }

    // Per-frame transform for the shader-side lookup
    // (RBPT_GIReuseNeighborDelta): random mirror/transpose/offset re-keys
    // the pairing every frame, so one texture per slot suffices
    // (paper Sec. 3.2). Layout of the returned bits:
    //   bit 0 mirror x, bit 1 mirror y, bit 2 transpose,
    //   bits 8..15 x offset, bits 16..23 y offset.
    static uint32_t MakeFrameTransformBits( uint32_t frameSeed, int size )
    {
        std::mt19937 rng( frameSeed );
        const uint32_t flips = rng() & 7u;
        const uint32_t ox = rng() % uint32_t( size );
        const uint32_t oy = rng() % uint32_t( size );
        return flips | ( ( ox & 0xffu ) << 8 ) | ( ( oy & 0xffu ) << 16 );
    }

private:
    // One tiled 2x2 shuffle pass. Every other pass starts the tiling at
    // (1,1) (wrapping toroidally) so blocks straddle the previous pass's
    // block borders - without this, indices never leave their 2x2 cell.
    static void ShufflePass(
        std::vector<int32_t>& link, int size, bool diagonalOffset,
        std::mt19937& rng )
    {
        const int base = diagonalOffset ? 1 : 0;
        for( int by = 0; by < size; by += 2 )
        {
            for( int bx = 0; bx < size; bx += 2 )
            {
                // The four cells of this block, toroidal.
                int idx[4];
                int n = 0;
                for( int cy = 0; cy < 2; cy++ )
                {
                    for( int cx = 0; cx < 2; cx++ )
                    {
                        const int x = ( bx + base + cx ) % size;
                        const int y = ( by + base + cy ) % size;
                        idx[n++] = y * size + x;
                    }
                }
                // Fisher-Yates over the 4 cells.
                for( int i = 3; i > 0; i-- )
                {
                    const int j = int( rng() % uint32_t( i + 1 ) );
                    const int32_t tmp = link[idx[i]];
                    link[idx[i]] = link[idx[j]];
                    link[idx[j]] = tmp;
                }
            }
        }
    }

    // Toroidal wrap of a coordinate delta into [-size/2, size/2)
    // (paper: "break long links" to make the texture tileable).
    static int WrapDelta( int d, int size )
    {
        if( d >= size / 2 )
        {
            d -= size;
        }
        if( d < -size / 2 )
        {
            d += size;
        }
        return d;
    }

    // Locate both carriers of every link index. CPU replacement for the
    // paper's racy two-slot GPU index table: same result, deterministic.
    static void FindPairs(
        const std::vector<int32_t>& link, int size,
        std::vector<int32_t>& first, std::vector<int32_t>& second )
    {
        const int pixelCount = size * size;
        first.assign( pixelCount / 2, -1 );
        second.assign( pixelCount / 2, -1 );
        for( int p = 0; p < pixelCount; p++ )
        {
            const int32_t li = link[p];
            if( first[li] < 0 )
            {
                first[li] = p;
            }
            else
            {
                second[li] = p;
            }
        }
    }

    static float MeasureSigma( const std::vector<int32_t>& link, int size )
    {
        std::vector<int32_t> first, second;
        FindPairs( link, size, first, second );

        // Per-axis variance of the wrapped deltas; reported sigma is the
        // per-axis stddev (the paper's sigma), i.e. sqrt of the mean of
        // the two axis variances. Means are ~0 by symmetry.
        double sumSq = 0.0;
        int count = 0;
        for( size_t li = 0; li < first.size(); li++ )
        {
            if( second[li] < 0 )
            {
                continue;
            }
            const int ax = first[li] % size, ay = first[li] / size;
            const int bx = second[li] % size, by = second[li] / size;
            const int dx = WrapDelta( bx - ax, size );
            const int dy = WrapDelta( by - ay, size );
            sumSq += double( dx ) * dx + double( dy ) * dy;
            count += 2; // two axis samples per pair
        }
        return count > 0 ? float( std::sqrt( sumSq / count ) ) : 0.0f;
    }

    static std::vector<int16_t> BuildDeltas(
        const std::vector<int32_t>& link, int size )
    {
        std::vector<int32_t> first, second;
        FindPairs( link, size, first, second );

        std::vector<int16_t> deltas( size_t( size ) * size * 2, 0 );
        for( size_t li = 0; li < first.size(); li++ )
        {
            const int a = first[li];
            const int b = second[li];
            if( b < 0 )
            {
                continue; // cannot happen with a permutation; belt+braces
            }
            const int ax = a % size, ay = a / size;
            const int bx = b % size, by = b / size;
            const int dx = WrapDelta( bx - ax, size );
            const int dy = WrapDelta( by - ay, size );
            // Self-inverting: a stores delta to b, b stores delta to a.
            deltas[size_t( a ) * 2 + 0] = int16_t( dx );
            deltas[size_t( a ) * 2 + 1] = int16_t( dy );
            deltas[size_t( b ) * 2 + 0] = int16_t( -dx );
            deltas[size_t( b ) * 2 + 1] = int16_t( -dy );
        }
        return deltas;
    }
};
