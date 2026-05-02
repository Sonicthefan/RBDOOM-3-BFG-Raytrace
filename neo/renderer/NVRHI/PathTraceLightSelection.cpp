#include "precompiled.h"
#pragma hdrstop

#include "PathTraceLightSelection.h"
#include "PathTraceDoomMaterialClassifier.h"

#include <algorithm>

extern idCVar r_lightScale;

namespace {

idVec4 EvaluateSmokeLightColor(const viewLight_t* vLight)
{
    if (!vLight || !vLight->lightShader || !vLight->shaderRegisters)
    {
        return idVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const float lightScale = r_lightScale.GetFloat();
    const idMaterial* lightShader = vLight->lightShader;
    const float* lightRegs = vLight->shaderRegisters;
    for (int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++)
    {
        const shaderStage_t* lightStage = lightShader->GetStage(lightStageNum);
        if (!lightStage || !lightRegs[lightStage->conditionRegister])
        {
            continue;
        }

        const int* registers = lightStage->color.registers;
        idVec4 color(
            lightScale * lightRegs[registers[0]],
            lightScale * lightRegs[registers[1]],
            lightScale * lightRegs[registers[2]],
            lightRegs[registers[3]]);
        if (color.x > 0.0f || color.y > 0.0f || color.z > 0.0f)
        {
            color.w = Max(color.x, Max(color.y, color.z));
            return color;
        }
    }

    return idVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

float ScoreSmokePointLight(const idVec3& cameraOrigin, const idVec3& lightOrigin, float radius, const idVec4& color, int selectionMode)
{
    const float distanceSquared = (lightOrigin - cameraOrigin).LengthSqr();
    if (selectionMode <= 0)
    {
        return 1.0f / Max(distanceSquared, 1.0f);
    }

    const float distance = idMath::Sqrt(distanceSquared);
    const float normalizedDistance = distance / Max(radius, 1.0f);
    const float attenuation = idMath::ClampFloat(0.0f, 1.0f, 1.0f - normalizedDistance);
    const float intensity = Max(0.0f, color.w);
    return intensity * (0.001f + attenuation * attenuation);
}

bool IsSmokeLightSpriteProxyCandidate(const idMaterial* lightShader)
{
    if (!lightShader)
    {
        return false;
    }

    idStr shaderName = lightShader->GetName();
    static const char* spriteTokens[] = { "strobe", "beacon", "blink", "warning", "alarm", "flare", "sprite", "glow" };
    return SmokeNameContainsAny(shaderName, spriteTokens, sizeof(spriteTokens) / sizeof(spriteTokens[0]));
}

}

int CollectSelectedSmokePointLights(const viewDef_t* viewDef, const idVec3& cameraOrigin, RtSmokeSelectedLight* selectedLights, int maxSelectedLights, int selectionMode)
{
    if (!viewDef || !selectedLights || maxSelectedLights <= 0)
    {
        return 0;
    }

    int selectedLightCount = 0;
    for (const viewLight_t* vLight = viewDef->viewLights; vLight != NULL; vLight = vLight->next)
    {
        if (!vLight->pointLight || vLight->parallel || vLight->removeFromList)
        {
            continue;
        }

        float radius = 512.0f;
        if (vLight->lightDef)
        {
            const idVec3& lightRadiusVec = vLight->lightDef->parms.lightRadius;
            radius = Max(lightRadiusVec.x, Max(lightRadiusVec.y, lightRadiusVec.z));
        }

        if (radius <= 1.0f)
        {
            continue;
        }

        const idVec4 color = EvaluateSmokeLightColor(vLight);
        const float distanceSquared = (vLight->globalLightOrigin - cameraOrigin).LengthSqr();
        const float score = ScoreSmokePointLight(cameraOrigin, vLight->globalLightOrigin, radius, color, selectionMode);
        if (score <= 0.0f)
        {
            continue;
        }

        int insertIndex = selectedLightCount;
        if (selectedLightCount == maxSelectedLights)
        {
            insertIndex = -1;
            float worstScore = score;
            for (int i = 0; i < selectedLightCount; i++)
            {
                if (selectedLights[i].score < worstScore)
                {
                    worstScore = selectedLights[i].score;
                    insertIndex = i;
                }
            }

            if (insertIndex < 0)
            {
                continue;
            }
        }
        else
        {
            selectedLightCount++;
        }

        selectedLights[insertIndex].origin = vLight->globalLightOrigin;
        selectedLights[insertIndex].radius = radius;
        selectedLights[insertIndex].color = color;
        selectedLights[insertIndex].index = vLight->lightDef ? vLight->lightDef->index : -1;
        selectedLights[insertIndex].distanceSquared = distanceSquared;
        selectedLights[insertIndex].score = score;
        selectedLights[insertIndex].shaderName = vLight->lightShader ? vLight->lightShader->GetName() : "<none>";
        selectedLights[insertIndex].spriteProxy = IsSmokeLightSpriteProxyCandidate(vLight->lightShader);
    }

    std::sort(selectedLights, selectedLights + selectedLightCount, [](const RtSmokeSelectedLight& a, const RtSmokeSelectedLight& b) {
        if (a.score != b.score)
        {
            return a.score > b.score;
        }
        return a.distanceSquared < b.distanceSquared;
    });
    return selectedLightCount;
}
