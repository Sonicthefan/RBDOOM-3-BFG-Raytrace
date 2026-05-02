#pragma once

// GUI surface admission helper for RT scene capture.
//
// Doom GUI surfaces are special-case material/visibility inputs. This helper
// keeps the CVar gate and surface test out of the broader capture loop.

struct drawSurf_t;

bool IsSmokeGuiDrawSurface(const drawSurf_t* drawSurf);
