#ifndef RB_PATH_TRACING_RTXDI_GI_RESTIR_GI_PARAMETERS_SHADOW_H
#define RB_PATH_TRACING_RTXDI_GI_RESTIR_GI_PARAMETERS_SHADOW_H

#include "Rtxdi/RtxdiParameters.h"
// The reservoir header also pulls restir_gi_parameters.hlsli; call sites
// declare RTXDI_PackedGIReservoir buffers right after including this file,
// so the packed type must be visible from here.
#include "../../cleanroom_common/restir_gi_reservoir.hlsli"

#endif
