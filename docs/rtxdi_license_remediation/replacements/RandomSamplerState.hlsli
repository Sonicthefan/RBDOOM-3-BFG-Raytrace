#ifndef RBPT_RANDOM_SAMPLER_STATE_HLSLI
#define RBPT_RANDOM_SAMPLER_STATE_HLSLI

// rbdoom-owned replacement for Rtxdi/Utils/RandomSamplerState.hlsli.
// Clean-room: written against the call sites in this repo only
// (RAB_RandomSamplerState.hlsli and the *_InitRandomSampler users), not
// against the NVIDIA header. Output sequences intentionally differ from
// upstream; nothing in the codebase depends on exact sequences.
//
// White-noise core: PCG output hash, Jarzynski & Olano, "Hash Functions
// for GPU Rendering", JCGT 9(3), 2020 (public algorithm).
// Blue-noise path: scalar spatiotemporal blue-noise mask array, one mask
// layer per frame slice, per-dimension toroidal R2 shift plus
// Cranley-Patterson rotation for decorrelation. Both operations preserve
// the per-dimension blue spatial spectrum and the uniform value histogram.
//
// Integration (see replacements/README.txt):
//   1. Drop this file in as the include target currently satisfied by
//      Rtxdi/Utils/RandomSamplerState.hlsli (keep the RTXDI_ names below,
//      or rename repo-wide to RBPT_).
//   2. Without RBPT_ENABLE_BLUE_NOISE defined this is a pure drop-in:
//      same two-uint state round-trip RAB_RandomSamplerState performs.
//   3. With RBPT_ENABLE_BLUE_NOISE defined, the shader must bind
//      t_RbptBlueNoise / declared below, and init sites get blue noise
//      automatically for the first RBPT_BLUE_NOISE_DIMS draws per state.
//      The direct-seed constructor cannot carry a pixel coordinate, so
//      states created through RAB_CreateRandomSamplerFromDirectSeed fall
//      back to white noise unless RAB_RandomSamplerState is widened to
//      carry the extra fields (mechanical follow-up task).

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

// Number of leading draws per sampler state that use the blue-noise path.
// Draws beyond this budget fall through to the white-noise hash. Keep this
// small: only the decision-critical first draws (candidate pick, neighbor
// offset, bounce direction) benefit; deep RIS-loop draws gain nothing.
#ifndef RBPT_BLUE_NOISE_DIMS
#define RBPT_BLUE_NOISE_DIMS 16
#endif

// Mask layout. Defaults match common scalar STBN sets (128x128, 64 slices).
// Both must be powers of two (the code masks rather than mods).
#ifndef RBPT_BLUE_NOISE_SIZE
#define RBPT_BLUE_NOISE_SIZE 128
#endif
#ifndef RBPT_BLUE_NOISE_LAYERS
#define RBPT_BLUE_NOISE_LAYERS 64
#endif

#ifdef RBPT_ENABLE_BLUE_NOISE
// Scalar mask array: RBPT_BLUE_NOISE_LAYERS layers of
// RBPT_BLUE_NOISE_SIZE^2, single channel, values uniform in [0,1).
// Register/space chosen at integration time; placeholder binding here.
#ifndef RBPT_BLUE_NOISE_TEXTURE_DECLARED
Texture2DArray<float> t_RbptBlueNoise : register(t127);
#define RBPT_BLUE_NOISE_TEXTURE_DECLARED 1
#endif
#endif

// ---------------------------------------------------------------------------
// White-noise hash core
// ---------------------------------------------------------------------------

uint RBPT_PcgHash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Two-round combiner for seeding from multiple fields.
uint RBPT_HashCombine(uint a, uint b)
{
    return RBPT_PcgHash(a ^ (b + 0x9e3779b9u + (a << 6u) + (a >> 2u)));
}

// Exact [0,1): top 24 bits scaled by 2^-24, never returns 1.0.
float RBPT_UintToUnitFloat(uint v)
{
    return float(v >> 8u) * (1.0 / 16777216.0);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// First two fields keep the exact layout/meaning the RAB bridge round-trips
// (seed, index). The remaining fields exist only for the blue-noise path
// and are zero / inert in white-noise-only states.
struct RTXDI_RandomSamplerState
{
    uint seed;       // hashed (pixel, frame, pass) stream identity
    uint index;      // dimension counter, advances by 1 per draw
    uint2 pixel;     // raw pixel for blue-noise lookups
    uint frameIndex; // raw frame for blue-noise layer selection
    uint passSalt;   // raw pass salt for blue-noise decorrelation
    uint useBlueNoise; // 0 = white only; nonzero = blue for first dims
};

RTXDI_RandomSamplerState RTXDI_CreateRandomSamplerFromDirectSeed(uint seed, uint index)
{
    RTXDI_RandomSamplerState rng;
    rng.seed = seed;
    rng.index = index;
    rng.pixel = uint2(0u, 0u);
    rng.frameIndex = 0u;
    rng.passSalt = 0u;
    rng.useBlueNoise = 0u; // pixel identity lost; blue noise impossible
    return rng;
}

RTXDI_RandomSamplerState RTXDI_InitRandomSampler(uint2 pixel, uint frameIndex, uint pass)
{
    RTXDI_RandomSamplerState rng;
    rng.seed = RBPT_HashCombine(RBPT_HashCombine(pixel.x, pixel.y),
                                RBPT_HashCombine(frameIndex, pass));
    rng.index = 0u;
    rng.pixel = pixel;
    rng.frameIndex = frameIndex;
    rng.passSalt = pass;
#ifdef RBPT_ENABLE_BLUE_NOISE
    rng.useBlueNoise = 1u;
#else
    rng.useBlueNoise = 0u;
#endif
    return rng;
}

// ---------------------------------------------------------------------------
// Draws
// ---------------------------------------------------------------------------

float RBPT_GetNextWhite(inout RTXDI_RandomSamplerState rng)
{
    // Counter-based: value_i = hash(seed ^ hash(index)). Stateless in the
    // stream, so direct-seed round trips through (seed, index) reproduce
    // the same continuation, which the RAB bridge relies on.
    const uint value = RBPT_PcgHash(rng.seed ^ RBPT_PcgHash(rng.index ^ 0xa511e9b3u));
    rng.index += 1u;
    return RBPT_UintToUnitFloat(value);
}

#ifdef RBPT_ENABLE_BLUE_NOISE
float RBPT_GetNextBlue(inout RTXDI_RandomSamplerState rng)
{
    const uint dimension = rng.index;
    rng.index += 1u;

    // Temporal axis: the mask array's layer axis is the spatiotemporal
    // time axis; advance one layer per frame so each pixel's value
    // sequence over frames is blue in time.
    const uint layer = rng.frameIndex & (RBPT_BLUE_NOISE_LAYERS - 1u);

    // Per-dimension decorrelation 1: toroidal shift of the lookup by the
    // additive-recurrence R2 sequence (Roberts, 2018). A pure translation
    // of the mask, so each dimension keeps an exact blue spectrum while
    // different dimensions read effectively independent masks.
    // Plastic-constant reciprocals 1/phi2, 1/phi2^2 in 0.32 fixed point:
    //   floor(2^32 * 0.7548776662) = 0xC13FA9A9
    //   floor(2^32 * 0.5698402909) = 0x91E10DA5
    const uint2 r2 = uint2(0xC13FA9A9u * dimension, 0x91E10DA5u * dimension);
    const uint2 shift = r2 >> (32u - firstbithigh(RBPT_BLUE_NOISE_SIZE));
    const uint2 texel = (rng.pixel + shift) & (RBPT_BLUE_NOISE_SIZE - 1u);

    const float mask = t_RbptBlueNoise.Load(int4(int2(texel), int(layer), 0)).x;

    // Per-dimension decorrelation 2: Cranley-Patterson rotation keyed on
    // (pass, dimension, frame epoch). frac(mask + rot) keeps the uniform
    // histogram and the spatial spectrum; the frame-epoch term re-keys the
    // rotation each time the layer counter wraps so sequences do not
    // repeat with period RBPT_BLUE_NOISE_LAYERS.
    const uint epoch = rng.frameIndex / RBPT_BLUE_NOISE_LAYERS;
    const uint rotBits = RBPT_HashCombine(rng.passSalt,
                          RBPT_HashCombine(dimension, epoch));
    const float rot = RBPT_UintToUnitFloat(rotBits);

    return frac(mask + rot);
}
#endif

float RTXDI_GetNextRandom(inout RTXDI_RandomSamplerState rng)
{
#ifdef RBPT_ENABLE_BLUE_NOISE
    if (rng.useBlueNoise != 0u && rng.index < RBPT_BLUE_NOISE_DIMS)
    {
        return RBPT_GetNextBlue(rng);
    }
#endif
    return RBPT_GetNextWhite(rng);
}

// Some call sites in older lanes use this alias name.
float RTXDI_SampleUniformRng(inout RTXDI_RandomSamplerState rng)
{
    return RTXDI_GetNextRandom(rng);
}

#endif // RBPT_RANDOM_SAMPLER_STATE_HLSLI
