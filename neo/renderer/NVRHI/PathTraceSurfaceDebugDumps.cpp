#include "precompiled.h"
#pragma hdrstop

#include "PathTraceSurfaceDebugDumps.h"
#include "PathTraceDebugDumps.h"
#include "PathTraceGuiSurfaces.h"
#include "PathTraceSceneCapture.h"
#include "PathTraceTextureRegistry.h"

#include <algorithm>

extern idCVar r_pathTracingAllowGuiSurfaces;
extern idCVar r_pathTracingAllowGuiTextures;

void LogSmokeGuiSurfaceDump(const viewDef_t* viewDef, const RtSmokeMaterialTableBuild& table)
{
    if (!viewDef)
    {
        common->Printf("PathTracePrimaryPass: RT smoke GUI dump no viewDef\n");
        return;
    }

    const int maxLogged = 24;
    int guiSurfaces = 0;
    int capturedGuiSurfaces = 0;
    int logged = 0;
    common->Printf("PathTracePrimaryPass: RT smoke GUI dump drawSurfs=%d allowGuiSurfaces=%d allowGuiTextures=%d\n",
        viewDef->numDrawSurfs,
        r_pathTracingAllowGuiSurfaces.GetInteger() != 0 ? 1 : 0,
        r_pathTracingAllowGuiTextures.GetInteger() != 0 ? 1 : 0);

    for (int surfaceIndex = 0; surfaceIndex < viewDef->numDrawSurfs; ++surfaceIndex)
    {
        const drawSurf_t* drawSurf = viewDef->drawSurfs[surfaceIndex];
        if (!IsSmokeGuiDrawSurface(drawSurf))
        {
            continue;
        }

        ++guiSurfaces;
        const srfTriangles_t* tri = nullptr;
        const bool captured = ValidateSmokeDrawSurface(viewDef, drawSurf, tri, nullptr);
        if (captured)
        {
            ++capturedGuiSurfaces;
        }

        if (logged >= maxLogged)
        {
            continue;
        }

        const idMaterial* material = drawSurf ? drawSurf->material : nullptr;
        const char* materialName = material ? material->GetName() : "<none>";
        const uint32_t materialId = HashSmokeMaterialName(materialName);
        int tableIndex = -1;
        std::vector<uint32_t>::const_iterator tableIt = std::find(table.materialIds.begin(), table.materialIds.end(), materialId);
        if (tableIt != table.materialIds.end())
        {
            tableIndex = static_cast<int>(tableIt - table.materialIds.begin());
        }

        idVec4 colorMin(1.0f, 1.0f, 1.0f, 1.0f);
        idVec4 colorMax(0.0f, 0.0f, 0.0f, 0.0f);
        idVec2 uvMin(1.0e20f, 1.0e20f);
        idVec2 uvMax(-1.0e20f, -1.0e20f);
        if (tri && tri->verts)
        {
            for (int vertIndex = 0; vertIndex < tri->numVerts; ++vertIndex)
            {
                const idDrawVert& vert = tri->verts[vertIndex];
                for (int component = 0; component < 4; ++component)
                {
                    const float c = vert.color[component] * (1.0f / 255.0f);
                    colorMin[component] = Min(colorMin[component], c);
                    colorMax[component] = Max(colorMax[component], c);
                }
                const idVec2 uv = vert.GetTexCoord();
                uvMin.x = Min(uvMin.x, uv.x);
                uvMin.y = Min(uvMin.y, uv.y);
                uvMax.x = Max(uvMax.x, uv.x);
                uvMax.y = Max(uvMax.y, uv.y);
            }
        }

        const RtSmokeMaterialTextureInfo info = ResolveSmokeMaterialTextureInfo(materialId, tableIndex);
        const PathTraceSmokeMaterial* rtMaterial = tableIndex >= 0 && tableIndex < static_cast<int>(table.materials.size()) ? &table.materials[tableIndex] : nullptr;
        common->Printf("PathTracePrimaryPass: RT smoke GUI surface[%d] captured=%d table=%d id=%u material='%s' verts=%d indexes=%d colorMin=(%.2f %.2f %.2f %.2f) colorMax=(%.2f %.2f %.2f %.2f) uvMin=(%.2f %.2f) uvMax=(%.2f %.2f) diffuse='%s' safe=%d handle=%d slot=%d reason='%s'\n",
            surfaceIndex,
            captured ? 1 : 0,
            tableIndex,
            materialId,
            materialName,
            tri ? tri->numVerts : 0,
            tri ? tri->numIndexes : 0,
            colorMin.x, colorMin.y, colorMin.z, colorMin.w,
            colorMax.x, colorMax.y, colorMax.z, colorMax.w,
            uvMin.x, uvMin.y,
            uvMax.x, uvMax.y,
            info.diffuseImageName.c_str(),
            info.hasSafeTexture ? 1 : 0,
            info.hasTextureHandle ? 1 : 0,
            rtMaterial && rtMaterial->diffuseTextureIndex != UINT32_MAX ? static_cast<int>(rtMaterial->diffuseTextureIndex) : -1,
            info.fallbackReason.c_str());

        if (material)
        {
            const float* regs = drawSurf && drawSurf->shaderRegisters ? drawSurf->shaderRegisters : material->ConstantRegisters();
            const int registerCount = material->GetNumRegisters();
            for (int stageIndex = 0; stageIndex < material->GetNumStages(); ++stageIndex)
            {
                const shaderStage_t* stage = material->GetStage(stageIndex);
                if (!stage)
                {
                    continue;
                }

                idVec4 stageColor(1.0f, 1.0f, 1.0f, 1.0f);
                if (regs)
                {
                    for (int component = 0; component < 4; ++component)
                    {
                        const int colorRegister = stage->color.registers[component];
                        if (colorRegister >= 0 && colorRegister < registerCount)
                        {
                            stageColor[component] = regs[colorRegister];
                        }
                    }
                }
                common->Printf("PathTracePrimaryPass: RT smoke GUI surface[%d] stage[%d] lighting=%s color=(%.3f %.3f %.3f %.3f) texgen=%s dynamic=%d image='%s' safe=%d\n",
                    surfaceIndex,
                    stageIndex,
                    SmokeStageLightingName(stage->lighting),
                    stageColor.x, stageColor.y, stageColor.z, stageColor.w,
                    SmokeTexgenName(stage->texture.texgen),
                    static_cast<int>(stage->texture.dynamic),
                    stage->texture.image ? stage->texture.image->GetName() : "<none>",
                    stage->texture.image && IsSmokeDiffuseImageSafeForRayTracing(stage->texture.image) ? 1 : 0);
            }
        }

        ++logged;
    }

    common->Printf("PathTracePrimaryPass: RT smoke GUI dump summary guiSurfaces=%d captured=%d logged=%d\n",
        guiSurfaces,
        capturedGuiSurfaces,
        logged);
}
