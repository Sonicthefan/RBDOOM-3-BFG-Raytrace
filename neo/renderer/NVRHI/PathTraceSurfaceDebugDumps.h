#pragma once

#include "PathTraceDynamicMaterialState.h"

struct viewDef_t;

void LogSmokeCrosshairMaterialDump(const viewDef_t* viewDef, const RtSmokeMaterialTableBuild& table);
void LogSmokeGuiSurfaceDump(const viewDef_t* viewDef, const RtSmokeMaterialTableBuild& table);
