#ifndef RBPT_GI_ADAPTIVE_MCAP_HLSLI
#define RBPT_GI_ADAPTIVE_MCAP_HLSLI

// Adaptive temporal confidence cap via sample duplication maps.
// Clean-room source: Lin, Kettunen, Wyman 2026, "ReSTIR PT Enhanced"
// (CC BY 4.0), Section 5. The NVIDIA headers were not opened.
//
// Idea: temporal M-cap controls how far fireflies/correlation blobs can
// spread and persist. Measure last frame's correlation around each pixel
// (how many nearby reservoirs are shifted copies of the SAME initial
// candidate) and shrink the cap only where correlation is high. Trades a
// small, localized bias (paper: ~3.25% in a stress scene) for removal of
// persistent correlation artifacts. Strict upgrade over a globally low
// cap (the deployed-Remix "cap=4 everywhere" approach).
//
// Two pieces:
//   A) a small compute pass run AFTER each frame's GI passes, writing a
//      single-channel duplication score texture (this file provides the
//      kernel body as a function; the integrating agent wraps it in a
//      .cs.hlsl with the lane's bindings),
//   B) a helper called inside temporal resampling to turn the score at
//      the BACKPROJECTED pixel into the frame's effective M-cap.
//
// Storage prerequisite (the only non-mechanical integration point):
// each GI reservoir must carry the RNG seed that identifies its initial
// candidate, copied UNCHANGED through temporal and spatial reuse (it
// identifies the originating sample, so reuse propagates it; only a new
// initial sample writes a new seed). If the packed reservoir has no free
// field, the paper notes this seed is the one already stored for random
// replay; in this lane the producer can write
// RTXDI_InitRandomSampler(pixel, frame, PRODUCER_PASS).seed. Define
// RBPT_GI_RESERVOIR_SEED(r) to read it.

// ---------------------------------------------------------------------------
// Tunables (paper-tested defaults)
// ---------------------------------------------------------------------------

#ifndef RBPT_GI_DUP_WINDOW
#define RBPT_GI_DUP_WINDOW 17          // odd; 17x17 neighborhood
#endif
#ifndef RBPT_GI_DUP_NORMALIZER
#define RBPT_GI_DUP_NORMALIZER 288.0   // paper divides the count by 288
#endif
#ifndef RBPT_GI_MCAP_MIN
#define RBPT_GI_MCAP_MIN 1.0           // cap when score = 1
#endif
#ifndef RBPT_GI_MCAP_GAMMA
#define RBPT_GI_MCAP_GAMMA 0.1         // sensitivity; 1 = linear,
                                       // ->0 = aggressive early reduction
#endif

// ---------------------------------------------------------------------------
// A) Duplication-map kernel body
// ---------------------------------------------------------------------------
// Wrap in a compute shader dispatched over the full GI resolution, after
// spatial reuse has written the frame's final reservoir page. Provide:
//   RBPT_GI_DUP_LOAD_SEED(pixel)   -> uint   (seed of final reservoir,
//                                             0 = invalid/empty pixel)
//   width/height                   -> lane dimensions
// Output score is in [0,1]; store R8_UNORM.
//
// Cost note: 17x17 = 289 taps/pixel is heavy as written. The paper does
// not specify an optimization; a separable-style approximation is NOT
// valid (equality counting does not factor), but running at half
// resolution and/or sampling a sparse stratified subset of the window
// (e.g. 64 taps, rescaling the normalizer) preserves behavior well -
// acceptable integration liberty, validate against the full version once.

float RBPT_GIComputeDuplicationScore(uint2 pixel, uint2 dimensions)
{
    const uint ownSeed = RBPT_GI_DUP_LOAD_SEED(pixel);
    if (ownSeed == 0u)
    {
        return 0.0;
    }

    const int r = RBPT_GI_DUP_WINDOW / 2;
    uint duplicates = 0u;

    [loop]
    for (int dy = -r; dy <= r; ++dy)
    {
        [loop]
        for (int dx = -r; dx <= r; ++dx)
        {
            if (dx == 0 && dy == 0)
            {
                continue;
            }
            const int2 q = int2(pixel) + int2(dx, dy);
            if (any(q < 0) || q.x >= int(dimensions.x) || q.y >= int(dimensions.y))
            {
                continue;
            }
            if (RBPT_GI_DUP_LOAD_SEED(uint2(q)) == ownSeed)
            {
                duplicates++;
            }
        }
    }

    return saturate(float(duplicates) / RBPT_GI_DUP_NORMALIZER);
}

// ---------------------------------------------------------------------------
// B) Temporal-side cap helper
// ---------------------------------------------------------------------------
// Call inside temporal resampling. `reprojectedPixel` is the PREVIOUS-frame
// pixel the temporal reservoir is fetched from (the score must describe the
// history actually being reused, not the current pixel). Provide:
//   RBPT_GI_DUP_LOAD_SCORE(pixel) -> float  (last frame's score texture)
//
// Replaces the constant history cap at exactly one point: where the
// temporal reservoir's M is clamped (maxHistoryLength /
// CleanRestirGiMaxHistoryLength). Everything else is unchanged - this is
// a parameter controller, not a math change. Known, accepted property:
// modulating confidence per-sample slightly violates the MIS partition of
// unity; the bias is confined to high-correlation regions (paper Sec. 5).

float RBPT_GIAdaptiveMCap(float defaultMCap, uint2 reprojectedPixel)
{
    const float score = RBPT_GI_DUP_LOAD_SCORE(reprojectedPixel);
    const float t = pow(saturate(score), RBPT_GI_MCAP_GAMMA);
    return lerp(defaultMCap, RBPT_GI_MCAP_MIN, t);
}

// Integration order of operations per frame:
//   1. GI producer/temporal/spatial passes run, reservoirs carry seeds.
//   2. Duplication pass writes score texture from the FINAL reservoir page.
//   3. Next frame's temporal pass calls RBPT_GIAdaptiveMCap with the
//      backprojected pixel against the (now one-frame-old) score texture,
//      which is exactly the paper's "prior frame's correlations".
// Double-buffering the score texture is unnecessary: it is written after
// all reads in a frame and read only by the following frame.
//
// Validation: render a scene with a hot emissive sliver + camera strafe.
// Fixed cap (e.g. 30): firefly blobs streak and persist. Adaptive:
// blobs collapse within a few frames while clean regions keep long
// history (verify via an M debug view - M should stay high on flat lit
// walls and drop only around the artifacts). Converged static image must
// match the fixed-cap image away from correlated regions; large global
// dimming means the seed is being rewritten during reuse instead of
// propagated.

#endif // RBPT_GI_ADAPTIVE_MCAP_HLSLI
