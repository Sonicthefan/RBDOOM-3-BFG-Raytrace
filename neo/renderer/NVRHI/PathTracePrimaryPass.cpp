#include "precompiled.h"
#pragma hdrstop

#include "PathTracePrimaryPass.h"
#include "../RenderCommon.h"
#include "../RenderBackend.h"
#include "../../framework/Common_local.h"

extern idCVar r_pathTracing;   // ← this makes the cvar visible

PathTracePrimaryPass::PathTracePrimaryPass(idRenderBackend* backend)
    : m_backend(backend)
{
    common->Printf("PathTracePrimaryPass: stub initialized ✅\n");
}

PathTracePrimaryPass::~PathTracePrimaryPass()
{
}

void PathTracePrimaryPass::Execute(const viewDef_t* viewDef)
{
    int mode = r_pathTracing.GetInteger();   // ← no "const" to avoid weird follow-on error
    common->Printf("=== PathTracePrimaryPass (mode %d) ===\n", mode);

    if (mode == 2) {
        common->Printf("  → Pure primary-ray mode (TLAS + RTXPT coming)\n");
    }
    else {
        common->Printf("  → Hybrid mode (G-buffer ready for RTXPT)\n");
    }
}