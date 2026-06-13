#pragma once

// rbdoom-owned host DI reservoir ABI. The packed layout mirrors
// cleanroom_common/restir_di_reservoir.hlsli.

#include <Rtxdi/RtxdiUtils.h>

#include <cstdint>

struct RTXDI_PackedDIReservoir
{
	uint32_t lightData = 0;
	uint32_t uvData = 0;
	uint32_t mVisibility = 0;
	uint32_t distanceAge = 0;
	float targetPdf = 0.0f;
	float weight = 0.0f;
};

namespace rtxdi {

constexpr uint32_t c_NumReSTIRDIReservoirBuffers = 3u;

} // namespace rtxdi

static_assert(sizeof(RTXDI_PackedDIReservoir) == 24, "DI packed reservoir stride must match HLSL layout");
static_assert((sizeof(RTXDI_PackedDIReservoir) % 4) == 0, "DI packed reservoir stride must be structured-buffer aligned");
