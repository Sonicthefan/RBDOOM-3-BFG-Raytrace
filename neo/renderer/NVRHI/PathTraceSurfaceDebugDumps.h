#pragma once

// Surface-targeted diagnostic dumps.
//
// Prints crosshair material and GUI surface details using the already-built
// material table. Trigger/reset policy lives in PathTraceDebugDumps.

#include "PathTraceDynamicMaterialState.h"

struct viewDef_t;
class RtSmokeGeometryUniverse;

void ProcessSmokeCrosshairZeroRoughnessToggle(const viewDef_t* viewDef);
void ProcessSmokeCrosshairFullMetalToggle(const viewDef_t* viewDef);
void LogSmokeCrosshairMaterialDump(const viewDef_t* viewDef, const RtSmokeMaterialTableBuild& table, const RtSmokeGeometryUniverse* geometryUniverse = nullptr);
void LogSmokeGuiSurfaceDump(const viewDef_t* viewDef, const RtSmokeMaterialTableBuild& table);
