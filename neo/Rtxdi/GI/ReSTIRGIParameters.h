#pragma once

// rbdoom-owned host GI reservoir ABI. The packed layout mirrors
// cleanroom_common/restir_gi_reservoir.hlsli.

#include <Rtxdi/RtxdiUtils.h>

#include <cstdint>

struct RTXDI_PackedGIReservoir
{
	uint32_t Data0[4] = {};
	uint32_t Data1[4] = {};
};

namespace rtxdi {

constexpr uint32_t c_NumReSTIRGIReservoirBuffers = 4u;

} // namespace rtxdi

static_assert(sizeof(RTXDI_PackedGIReservoir) == 32, "GI packed reservoir stride must match HLSL layout");
static_assert((sizeof(RTXDI_PackedGIReservoir) % 16) == 0, "GI packed reservoir stride must stay 16-byte aligned");
