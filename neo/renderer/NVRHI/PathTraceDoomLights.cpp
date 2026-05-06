#include "precompiled.h"
#pragma hdrstop

#include "PathTraceDoomLights.h"
#include "PathTraceCVars.h"
#include "../RenderCommon.h"
#include "../RenderWorld_local.h"
#include "../../d3xp/Game_local.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

extern idCVar r_lightScale;

const uint32_t RT_DOOM_ANALYTIC_LIGHT_CASTS_SHADOWS = 0x00000001u;
const uint32_t RT_DOOM_ANALYTIC_LIGHT_GAME_LINKED = 0x00000002u;
const uint32_t RT_DOOM_ANALYTIC_LIGHT_BEHIND_CAMERA = 0x00000004u;

namespace {

struct DoomLightPortalSelection
{
    int currentArea = -1;
    int portalSteps = 0;
    int selectedAreaCount = 0;
    int portalEdges = 0;
    int blockedPortalEdges = 0;
    std::vector<int> depthByArea;
};

struct DoomLightRecord
{
    int index = -1;
    int area = -1;
    int originArea = -1;
    int portalDepth = -1;
    bool selectedArea = false;
    bool connectedArea = false;
    bool active = false;
    bool suppressed = false;
    bool pointLight = false;
    bool parallel = false;
    bool castsShadows = false;
    bool visibleInView = false;
    bool spriteProxy = false;
    idVec3 origin = vec3_zero;
    idVec3 radius = vec3_zero;
    idBounds bounds;
    idVec4 color = idVec4(0.0f, 0.0f, 0.0f, 1.0f);
    float radiusMax = 0.0f;
    float distance = 0.0f;
    float crosshairT = -1.0f;
    float crosshairDistance = -1.0f;
    float crosshairScore = idMath::INFINITUM;
    bool crosshairBehind = false;
    float sphereRadius = 0.0f;
    const char* shaderName = "<none>";
    const char* entityName = "<unavailable>";
    const char* entityClassname = "<unavailable>";
    const char* entityDefName = "<unavailable>";
    const char* spawnTexture = "<unavailable>";
    const char* spawnModel = "<unavailable>";
    const char* spawnBind = "<none>";
    const char* spawnTarget = "<none>";
    const char* spawnBroken = "<none>";
    bool gameLinked = false;
    bool gameHidden = false;
    bool spawnStartOff = false;
    bool spawnBreak = false;
    bool spawnNoShadows = false;
    bool spawnNoSpecular = false;
    int entityNumber = -1;
    int levels = 0;
    int currentLevel = 0;
    int spawnCount = 0;
    int health = 0;
    idVec3 baseColor = vec3_zero;
    idVec3 currentGameColor = vec3_zero;
};

struct DoomLightGameMetadata
{
    const idLight* light = nullptr;
    const char* entityName = "<unavailable>";
    const char* entityClassname = "<unavailable>";
    const char* entityDefName = "<unavailable>";
    const char* spawnTexture = "<unavailable>";
    const char* spawnModel = "<unavailable>";
    const char* spawnBind = "<none>";
    const char* spawnTarget = "<none>";
    const char* spawnBroken = "<none>";
    bool hidden = false;
    bool startOff = false;
    bool breakOnTrigger = false;
    bool noShadows = false;
    bool noSpecular = false;
    int entityNumber = -1;
    int levels = 0;
    int currentLevel = 0;
    int count = 0;
    int health = 0;
    idVec3 baseColor = vec3_zero;
    idVec3 currentColor = vec3_zero;
};

bool IsDoomLightGameStateActive()
{
    return gameLocal.GameState() == GAMESTATE_ACTIVE;
}

std::unordered_map<int, DoomLightGameMetadata> BuildDoomLightGameMetadataByHandle()
{
    std::unordered_map<int, DoomLightGameMetadata> metadataByHandle;
    if (!IsDoomLightGameStateActive())
    {
        return metadataByHandle;
    }

    metadataByHandle.reserve(128);
    for (int entityIndex = 0; entityIndex < gameLocal.num_entities; ++entityIndex)
    {
        idEntity* entity = gameLocal.entities[entityIndex];
        if (!entity || !entity->IsType(idLight::Type))
        {
            continue;
        }

        const idLight* light = static_cast<const idLight*>(entity);
        const qhandle_t lightHandle = light->GetLightDefHandle();
        if (lightHandle < 0)
        {
            continue;
        }

        DoomLightGameMetadata metadata;
        metadata.light = light;
        metadata.entityName = light->GetName();
        metadata.entityClassname = light->spawnArgs.GetString("classname", "<none>");
        metadata.entityDefName = light->GetEntityDefName();
        metadata.spawnTexture = light->spawnArgs.GetString("texture", "lights/squarelight1");
        metadata.spawnModel = light->spawnArgs.GetString("model", "<none>");
        metadata.spawnBind = light->spawnArgs.GetString("bind", "<none>");
        metadata.spawnTarget = light->spawnArgs.GetString("target", "<none>");
        metadata.spawnBroken = light->spawnArgs.GetString("broken", "<none>");
        metadata.hidden = light->fl.hidden;
        metadata.startOff = light->spawnArgs.GetBool("start_off", "0");
        metadata.breakOnTrigger = light->spawnArgs.GetBool("break", "0");
        metadata.noShadows = light->spawnArgs.GetBool("noshadows", "0");
        metadata.noSpecular = light->spawnArgs.GetBool("nospecular", "0");
        metadata.entityNumber = light->GetEntityNumber();
        metadata.levels = light->GetLightLevels();
        metadata.currentLevel = light->GetCurrentLightLevel();
        metadata.count = light->spawnArgs.GetInt("count", "1");
        metadata.health = light->health;
        metadata.baseColor = light->GetBaseColor();
        light->GetColor(metadata.currentColor);
        metadataByHandle[lightHandle] = metadata;
    }
    return metadataByHandle;
}

int ResolveCurrentDoomLightArea(const viewDef_t* viewDef)
{
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld)
    {
        return -1;
    }

    int area = viewDef->areaNum;
    if (area < 0)
    {
        area = renderWorld->PointInArea(viewDef->initialViewAreaOrigin);
    }
    if (area < 0)
    {
        area = renderWorld->PointInArea(viewDef->renderView.vieworg);
    }
    return area;
}

DoomLightPortalSelection BuildDoomLightPortalSelection(const viewDef_t* viewDef, int portalSteps)
{
    DoomLightPortalSelection selection;
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    selection.currentArea = ResolveCurrentDoomLightArea(viewDef);
    selection.portalSteps = idMath::ClampInt(0, 8, portalSteps);
    if (!renderWorld || selection.currentArea < 0)
    {
        return selection;
    }

    const int areaCount = renderWorld->NumAreas();
    if (selection.currentArea >= areaCount)
    {
        return selection;
    }

    selection.depthByArea.assign(areaCount, -1);
    std::vector<int> queue;
    queue.reserve(areaCount);
    selection.depthByArea[selection.currentArea] = 0;
    queue.push_back(selection.currentArea);

    for (size_t queueIndex = 0; queueIndex < queue.size(); ++queueIndex)
    {
        const int area = queue[queueIndex];
        const int depth = selection.depthByArea[area];
        if (depth >= selection.portalSteps)
        {
            continue;
        }

        const int portalCount = renderWorld->NumPortalsInArea(area);
        for (int portalIndex = 0; portalIndex < portalCount; ++portalIndex)
        {
            const exitPortal_t portal = renderWorld->GetPortal(area, portalIndex);
            if ((portal.blockingBits & PS_BLOCK_VIEW) != 0)
            {
                ++selection.blockedPortalEdges;
            }

            int nextArea = -1;
            if (portal.areas[0] == area)
            {
                nextArea = portal.areas[1];
            }
            else if (portal.areas[1] == area)
            {
                nextArea = portal.areas[0];
            }
            if (nextArea < 0 || nextArea >= areaCount)
            {
                continue;
            }

            ++selection.portalEdges;
            if (selection.depthByArea[nextArea] < 0)
            {
                selection.depthByArea[nextArea] = depth + 1;
                queue.push_back(nextArea);
            }
        }
    }

    for (int depth : selection.depthByArea)
    {
        if (depth >= 0)
        {
            ++selection.selectedAreaCount;
        }
    }
    return selection;
}

idVec4 EvaluateDoomLightColor(const viewDef_t* viewDef, const idRenderLightLocal* light, bool& active)
{
    active = false;
    if (!viewDef || !light || !light->lightShader)
    {
        return idVec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const idMaterial* lightShader = light->lightShader;
    std::vector<float> registers;
    const float* lightRegs = lightShader->ConstantRegisters();
    if (!lightRegs)
    {
        registers.resize(lightShader->GetNumRegisters());
        lightShader->EvaluateRegisters(
            registers.data(),
            light->parms.shaderParms,
            viewDef->renderView.shaderParms,
            viewDef->renderView.time[0] * 0.001f,
            light->parms.referenceSound);
        lightRegs = registers.data();
    }

    const float lightScale = r_lightScale.GetFloat();
    idVec4 bestColor(0.0f, 0.0f, 0.0f, 0.0f);
    float bestIntensity = 0.0f;
    for (int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); ++lightStageNum)
    {
        const shaderStage_t* lightStage = lightShader->GetStage(lightStageNum);
        if (!lightStage || !lightRegs[lightStage->conditionRegister])
        {
            continue;
        }

        const int* colorRegisters = lightStage->color.registers;
        const idVec4 color(
            lightScale * lightRegs[colorRegisters[0]],
            lightScale * lightRegs[colorRegisters[1]],
            lightScale * lightRegs[colorRegisters[2]],
            lightRegs[colorRegisters[3]]);
        const float intensity = Max(color.x, Max(color.y, color.z));
        if (intensity > bestIntensity)
        {
            bestIntensity = intensity;
            bestColor = color;
            bestColor.w = intensity;
        }
    }

    active = bestIntensity > 0.0f;
    return bestColor;
}

bool IsDoomLightSuppressedForView(const viewDef_t* viewDef, const idRenderLightLocal* light)
{
    if (!viewDef || !light)
    {
        return true;
    }
    return (light->parms.suppressLightInViewID != 0 && light->parms.suppressLightInViewID == viewDef->renderView.viewID) ||
        (light->parms.allowLightInViewID != 0 && light->parms.allowLightInViewID != viewDef->renderView.viewID);
}

void FillDoomLightCrosshairMetrics(const viewDef_t* viewDef, DoomLightRecord& record)
{
    if (!viewDef)
    {
        return;
    }

    idVec3 forward = viewDef->renderView.viewaxis[0];
    forward.Normalize();
    const idVec3 toLight = record.origin - viewDef->renderView.vieworg;
    record.crosshairT = toLight * forward;
    if (record.crosshairT < 0.0f)
    {
        record.crosshairBehind = true;
        return;
    }

    const idVec3 closestPoint = viewDef->renderView.vieworg + forward * record.crosshairT;
    record.crosshairDistance = (record.origin - closestPoint).Length();
    record.crosshairScore = record.crosshairDistance / Max(record.radiusMax, 1.0f);
}

DoomLightRecord BuildDoomLightRecord(
    const viewDef_t* viewDef,
    const idRenderLightLocal* light,
    const DoomLightPortalSelection& selection,
    const std::unordered_map<int, DoomLightGameMetadata>& gameMetadataByHandle)
{
    DoomLightRecord record;
    if (!viewDef || !light)
    {
        return record;
    }

    idRenderWorldLocal* renderWorld = viewDef->renderWorld;
    record.index = light->index;
    record.origin = light->globalLightOrigin;
    record.radius = light->parms.lightRadius;
    record.radiusMax = Max(record.radius.x, Max(record.radius.y, record.radius.z));
    record.bounds = light->globalLightBounds;
    record.area = light->areaNum;
    record.originArea = renderWorld ? renderWorld->PointInArea(record.origin) : -1;
    if (record.area < 0)
    {
        record.area = record.originArea;
    }
    if (record.area >= 0 && record.area < static_cast<int>(selection.depthByArea.size()))
    {
        record.portalDepth = selection.depthByArea[record.area];
        record.selectedArea = record.portalDepth >= 0;
    }
    record.connectedArea = viewDef->connectedAreas && record.area >= 0 && renderWorld && record.area < renderWorld->NumAreas() && viewDef->connectedAreas[record.area];
    record.suppressed = IsDoomLightSuppressedForView(viewDef, light);
    record.color = EvaluateDoomLightColor(viewDef, light, record.active);
    record.active = record.active && !record.suppressed;
    record.pointLight = light->parms.pointLight;
    record.parallel = light->parms.parallel;
    record.castsShadows = light->lightShader ? light->LightCastsShadows() : false;
    record.visibleInView = light->viewCount == tr.viewCount && light->viewLight && !light->viewLight->removeFromList;
    record.shaderName = light->lightShader ? light->lightShader->GetName() : "<none>";
    record.distance = (record.origin - viewDef->renderView.vieworg).Length();
    const auto gameMetadataIt = gameMetadataByHandle.find(light->index);
    if (gameMetadataIt != gameMetadataByHandle.end())
    {
        const DoomLightGameMetadata& metadata = gameMetadataIt->second;
        record.gameLinked = true;
        record.entityName = metadata.entityName;
        record.entityClassname = metadata.entityClassname;
        record.entityDefName = metadata.entityDefName;
        record.spawnTexture = metadata.spawnTexture;
        record.spawnModel = metadata.spawnModel;
        record.spawnBind = metadata.spawnBind;
        record.spawnTarget = metadata.spawnTarget;
        record.spawnBroken = metadata.spawnBroken;
        record.gameHidden = metadata.hidden;
        record.spawnStartOff = metadata.startOff;
        record.spawnBreak = metadata.breakOnTrigger;
        record.spawnNoShadows = metadata.noShadows;
        record.spawnNoSpecular = metadata.noSpecular;
        record.entityNumber = metadata.entityNumber;
        record.levels = metadata.levels;
        record.currentLevel = metadata.currentLevel;
        record.spawnCount = metadata.count;
        record.health = metadata.health;
        record.baseColor = metadata.baseColor;
        record.currentGameColor = metadata.currentColor;
    }
    FillDoomLightCrosshairMetrics(viewDef, record);
    return record;
}

std::vector<DoomLightRecord> CollectDoomLightRecords(
    const viewDef_t* viewDef,
    const DoomLightPortalSelection& selection,
    const std::unordered_map<int, DoomLightGameMetadata>& gameMetadataByHandle)
{
    std::vector<DoomLightRecord> records;
    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (!renderWorld)
    {
        return records;
    }

    records.reserve(renderWorld->lightDefs.Num());
    for (int lightIndex = 0; lightIndex < renderWorld->lightDefs.Num(); ++lightIndex)
    {
        const idRenderLightLocal* light = renderWorld->lightDefs[lightIndex];
        if (!light)
        {
            continue;
        }
        records.push_back(BuildDoomLightRecord(viewDef, light, selection, gameMetadataByHandle));
    }
    return records;
}

void PrintDoomLightRecord(const char* prefix, int sampleIndex, const DoomLightRecord& light, const char* mapName)
{
    common->Printf("%s[%d]: map='%s' renderLight=%d linked=%d entity='%s' entNum=%d classname='%s' entityDef='%s' spawnTexture='%s' shader='%s' type=%s origin=(%.2f %.2f %.2f) radius=(%.2f %.2f %.2f) radiusMax=%.2f bounds=(%.2f %.2f %.2f)-(%.2f %.2f %.2f) color=(%.3f %.3f %.3f) intensity=%.3f gameColor=(%.3f %.3f %.3f) baseColor=(%.3f %.3f %.3f) level=%d/%d hidden=%d startOff=%d break=%d count=%d health=%d model='%s' bind='%s' target='%s' broken='%s' area=%d originArea=%d portalDepth=%d selected=%d connected=%d active=%d suppressed=%d shadows=%d visible=%d distance=%.2f crosshairBehind=%d crosshairT=%.2f crosshairDist=%.2f\n",
        prefix,
        sampleIndex,
        mapName ? mapName : "<unknown>",
        light.index,
        light.gameLinked ? 1 : 0,
        light.entityName,
        light.entityNumber,
        light.entityClassname,
        light.entityDefName,
        light.spawnTexture,
        light.shaderName,
        light.parallel ? "parallel" : (light.pointLight ? "point" : "projected"),
        light.origin.x,
        light.origin.y,
        light.origin.z,
        light.radius.x,
        light.radius.y,
        light.radius.z,
        light.radiusMax,
        light.bounds[0].x,
        light.bounds[0].y,
        light.bounds[0].z,
        light.bounds[1].x,
        light.bounds[1].y,
        light.bounds[1].z,
        light.color.x,
        light.color.y,
        light.color.z,
        light.color.w,
        light.currentGameColor.x,
        light.currentGameColor.y,
        light.currentGameColor.z,
        light.baseColor.x,
        light.baseColor.y,
        light.baseColor.z,
        light.currentLevel,
        light.levels,
        light.gameHidden ? 1 : 0,
        light.spawnStartOff ? 1 : 0,
        light.spawnBreak ? 1 : 0,
        light.spawnCount,
        light.health,
        light.spawnModel,
        light.spawnBind,
        light.spawnTarget,
        light.spawnBroken,
        light.area,
        light.originArea,
        light.portalDepth,
        light.selectedArea ? 1 : 0,
        light.connectedArea ? 1 : 0,
        light.active ? 1 : 0,
        light.suppressed ? 1 : 0,
        light.castsShadows ? 1 : 0,
        light.visibleInView ? 1 : 0,
        light.distance,
        light.crosshairBehind ? 1 : 0,
        light.crosshairT,
        light.crosshairDistance);
}

void RunDoomLightIdentityDump(const viewDef_t* viewDef, const DoomLightPortalSelection& selection, const std::vector<DoomLightRecord>& records)
{
    const int dumpMode = r_pathTracingDoomLightDump.GetInteger();
    if (dumpMode == 0)
    {
        return;
    }

    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    const char* mapName = renderWorld ? renderWorld->mapName.c_str() : "<unknown>";
    int active = 0;
    int selected = 0;
    int visible = 0;
    int point = 0;
    int projected = 0;
    int parallel = 0;
    int unknownArea = 0;
    int gameLinked = 0;
    int gameHidden = 0;
    int gameLevelOff = 0;
    int connectedUnselected = 0;
    int depthBins[6] = {};

    for (const DoomLightRecord& record : records)
    {
        active += record.active ? 1 : 0;
        selected += record.selectedArea ? 1 : 0;
        visible += record.visibleInView ? 1 : 0;
        point += record.pointLight ? 1 : 0;
        projected += (!record.pointLight && !record.parallel) ? 1 : 0;
        parallel += record.parallel ? 1 : 0;
        unknownArea += record.area < 0 ? 1 : 0;
        gameLinked += record.gameLinked ? 1 : 0;
        gameHidden += record.gameHidden ? 1 : 0;
        gameLevelOff += record.gameLinked && record.currentLevel <= 0 ? 1 : 0;
        connectedUnselected += (record.connectedArea && !record.selectedArea) ? 1 : 0;
        if (record.portalDepth >= 0 && record.portalDepth < 5)
        {
            ++depthBins[record.portalDepth];
        }
        else
        {
            ++depthBins[5];
        }
    }

    common->Printf("PathTracePrimaryPass: Doom light dump map='%s' lights=%d active=%d visibleView=%d point/projected/parallel=%d/%d/%d gameLinked=%d hidden=%d levelOff=%d currentArea=%d portalSteps=%d selectedAreas=%d portalEdges/blocked=%d/%d selectedLights=%d connectedUnselected=%d unknownArea=%d depthBins 0/1/2/3/4/other=%d/%d/%d/%d/%d/%d entityNameStatus=linked-by-idLight-lightDefHandle\n",
        mapName,
        static_cast<int>(records.size()),
        active,
        visible,
        point,
        projected,
        parallel,
        gameLinked,
        gameHidden,
        gameLevelOff,
        selection.currentArea,
        selection.portalSteps,
        selection.selectedAreaCount,
        selection.portalEdges,
        selection.blockedPortalEdges,
        selected,
        connectedUnselected,
        unknownArea,
        depthBins[0],
        depthBins[1],
        depthBins[2],
        depthBins[3],
        depthBins[4],
        depthBins[5]);

    std::vector<DoomLightRecord> samples = records;
    std::stable_sort(samples.begin(), samples.end(), [](const DoomLightRecord& a, const DoomLightRecord& b) {
        if (a.selectedArea != b.selectedArea)
        {
            return a.selectedArea && !b.selectedArea;
        }
        if (a.active != b.active)
        {
            return a.active && !b.active;
        }
        if (a.portalDepth != b.portalDepth)
        {
            return a.portalDepth >= 0 && (b.portalDepth < 0 || a.portalDepth < b.portalDepth);
        }
        if (a.distance != b.distance)
        {
            return a.distance < b.distance;
        }
        return a.index < b.index;
    });

    const int maxSamples = idMath::ClampInt(0, 1024, r_pathTracingDoomLightDumpMax.GetInteger());
    const int printCount = dumpMode >= 2 ? Min(maxSamples, static_cast<int>(samples.size())) : Min(maxSamples, Min(16, static_cast<int>(samples.size())));
    for (int i = 0; i < printCount; ++i)
    {
        PrintDoomLightRecord("PathTracePrimaryPass: Doom light", i, samples[i], mapName);
    }

    r_pathTracingDoomLightDump.SetInteger(0);
}

void RunDoomLightProbeDump(const viewDef_t* viewDef, const std::vector<DoomLightRecord>& records)
{
    if (r_pathTracingDoomLightProbeDump.GetInteger() == 0)
    {
        return;
    }

    idRenderWorldLocal* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    const char* mapName = renderWorld ? renderWorld->mapName.c_str() : "<unknown>";
    const int maxSamples = idMath::ClampInt(1, 64, r_pathTracingDoomLightProbeMax.GetInteger());

    std::vector<DoomLightRecord> nearest = records;
    std::stable_sort(nearest.begin(), nearest.end(), [](const DoomLightRecord& a, const DoomLightRecord& b) {
        if (a.distance != b.distance)
        {
            return a.distance < b.distance;
        }
        return a.index < b.index;
    });

    std::vector<DoomLightRecord> crosshair;
    crosshair.reserve(records.size());
    for (const DoomLightRecord& record : records)
    {
        if (record.crosshairT >= 0.0f)
        {
            crosshair.push_back(record);
        }
    }
    std::stable_sort(crosshair.begin(), crosshair.end(), [](const DoomLightRecord& a, const DoomLightRecord& b) {
        if (a.crosshairScore != b.crosshairScore)
        {
            return a.crosshairScore < b.crosshairScore;
        }
        if (a.crosshairT != b.crosshairT)
        {
            return a.crosshairT < b.crosshairT;
        }
        return a.index < b.index;
    });

    common->Printf("PathTracePrimaryPass: Doom light probe map='%s' playerOrigin=(%.2f %.2f %.2f) forward=(%.3f %.3f %.3f) nearest=%d crosshair=%d max=%d\n",
        mapName,
        viewDef->renderView.vieworg.x,
        viewDef->renderView.vieworg.y,
        viewDef->renderView.vieworg.z,
        viewDef->renderView.viewaxis[0].x,
        viewDef->renderView.viewaxis[0].y,
        viewDef->renderView.viewaxis[0].z,
        static_cast<int>(nearest.size()),
        static_cast<int>(crosshair.size()),
        maxSamples);

    for (int i = 0; i < Min(maxSamples, static_cast<int>(nearest.size())); ++i)
    {
        PrintDoomLightRecord("PathTracePrimaryPass: Doom nearest light", i, nearest[i], mapName);
    }
    for (int i = 0; i < Min(maxSamples, static_cast<int>(crosshair.size())); ++i)
    {
        PrintDoomLightRecord("PathTracePrimaryPass: Doom crosshair light", i, crosshair[i], mapName);
    }

    r_pathTracingDoomLightProbeDump.SetInteger(0);
}

std::vector<DoomLightRecord> BuildAnalyticDoomLightRecords(const std::vector<DoomLightRecord>& records)
{
    std::vector<DoomLightRecord> candidates;
    candidates.reserve(records.size());
    const float radiusScale = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingAnalyticSphereLightRadiusScale.GetFloat());
    const float radiusMin = Max(0.0f, r_pathTracingAnalyticSphereLightRadiusMin.GetFloat());
    const float radiusMax = Max(radiusMin, r_pathTracingAnalyticSphereLightRadiusMax.GetFloat());
    for (DoomLightRecord record : records)
    {
        if (!record.active || !record.pointLight || record.parallel || !record.selectedArea || record.radiusMax <= 1.0f)
        {
            continue;
        }
        record.sphereRadius = idMath::ClampFloat(radiusMin, radiusMax, record.radiusMax * radiusScale);
        candidates.push_back(record);
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const DoomLightRecord& a, const DoomLightRecord& b) {
        if (a.portalDepth != b.portalDepth)
        {
            return a.portalDepth < b.portalDepth;
        }
        if (a.distance != b.distance)
        {
            return a.distance < b.distance;
        }
        return a.index < b.index;
    });

    return candidates;
}

void RunAnalyticLightCandidateDump(const DoomLightPortalSelection& selection, const std::vector<DoomLightRecord>& records)
{
    if (r_pathTracingAnalyticLightCandidates.GetInteger() == 0)
    {
        if (r_pathTracingAnalyticLightCandidateDump.GetInteger() != 0)
        {
            common->Printf("PathTracePrimaryPass: Doom analytic light candidates disabled; set r_pathTracingAnalyticLightCandidates 1 to build and shade analytic sphere records\n");
            r_pathTracingAnalyticLightCandidateDump.SetInteger(0);
        }
        return;
    }

    std::vector<DoomLightRecord> candidates = BuildAnalyticDoomLightRecords(records);

    if (r_pathTracingAnalyticLightCandidateDump.GetInteger() == 0)
    {
        return;
    }

    const float radiusScale = idMath::ClampFloat(0.0f, 1.0f, r_pathTracingAnalyticSphereLightRadiusScale.GetFloat());
    const float radiusMin = Max(0.0f, r_pathTracingAnalyticSphereLightRadiusMin.GetFloat());
    const float radiusMax = Max(radiusMin, r_pathTracingAnalyticSphereLightRadiusMax.GetFloat());

    int linkedCandidates = 0;
    for (const DoomLightRecord& candidate : candidates)
    {
        linkedCandidates += candidate.gameLinked ? 1 : 0;
    }

    int behindCandidates = 0;
    for (const DoomLightRecord& candidate : candidates)
    {
        behindCandidates += candidate.crosshairBehind ? 1 : 0;
    }

    common->Printf("PathTracePrimaryPass: Doom analytic sphere-light candidates count=%d linked=%d behindCamera=%d sourceLights=%d currentArea=%d portalSteps=%d gpuMax=%d intensityScale=%.3f radius scale/min/max=%.4f/%.2f/%.2f selection=area-active-point-not-view-facing outputChange=1 gpuBuffer=separate\n",
        static_cast<int>(candidates.size()),
        linkedCandidates,
        behindCandidates,
        static_cast<int>(records.size()),
        selection.currentArea,
        selection.portalSteps,
        idMath::ClampInt(0, 1024, r_pathTracingAnalyticLightMaxGpu.GetInteger()),
        idMath::ClampFloat(0.0f, 16.0f, r_pathTracingAnalyticLightIntensityScale.GetFloat()),
        radiusScale,
        radiusMin,
        radiusMax);

    const int maxSamples = idMath::ClampInt(0, 1024, r_pathTracingDoomLightDumpMax.GetInteger());
    for (int i = 0; i < Min(maxSamples, static_cast<int>(candidates.size())); ++i)
    {
        const DoomLightRecord& light = candidates[i];
        common->Printf("PathTracePrimaryPass: Doom analytic sphere candidate[%d] renderLight=%d linked=%d entity='%s' entNum=%d classname='%s' spawnTexture='%s' shader='%s' level=%d/%d hidden=%d behindCamera=%d gameColor=(%.3f %.3f %.3f) baseColor=(%.3f %.3f %.3f) area=%d portalDepth=%d origin=(%.2f %.2f %.2f) doomRadius=%.2f sphereRadius=%.2f color=(%.3f %.3f %.3f) intensity=%.3f\n",
            i,
            light.index,
            light.gameLinked ? 1 : 0,
            light.entityName,
            light.entityNumber,
            light.entityClassname,
            light.spawnTexture,
            light.shaderName,
            light.currentLevel,
            light.levels,
            light.gameHidden ? 1 : 0,
            light.crosshairBehind ? 1 : 0,
            light.currentGameColor.x,
            light.currentGameColor.y,
            light.currentGameColor.z,
            light.baseColor.x,
            light.baseColor.y,
            light.baseColor.z,
            light.area,
            light.portalDepth,
            light.origin.x,
            light.origin.y,
            light.origin.z,
            light.radiusMax,
            light.sphereRadius,
            light.color.x,
            light.color.y,
            light.color.z,
            light.color.w);
    }

    r_pathTracingAnalyticLightCandidateDump.SetInteger(0);
}

}

std::vector<PathTraceDoomAnalyticLightCandidate> BuildPathTraceDoomAnalyticLightCandidates(const viewDef_t* viewDef)
{
    std::vector<PathTraceDoomAnalyticLightCandidate> gpuCandidates;
    if (!viewDef || !viewDef->renderWorld || !IsDoomLightGameStateActive() || r_pathTracingAnalyticLightCandidates.GetInteger() == 0)
    {
        return gpuCandidates;
    }

    const DoomLightPortalSelection selection = BuildDoomLightPortalSelection(
        viewDef,
        idMath::ClampInt(0, 8, r_pathTracingLightAreaPortalSteps.GetInteger()));
    const std::unordered_map<int, DoomLightGameMetadata> gameMetadataByHandle = BuildDoomLightGameMetadataByHandle();
    const std::vector<DoomLightRecord> records = CollectDoomLightRecords(viewDef, selection, gameMetadataByHandle);
    const std::vector<DoomLightRecord> candidates = BuildAnalyticDoomLightRecords(records);
    const int maxGpuCandidates = idMath::ClampInt(0, 1024, r_pathTracingAnalyticLightMaxGpu.GetInteger());
    gpuCandidates.reserve(Min(maxGpuCandidates, static_cast<int>(candidates.size())));

    for (int i = 0; i < maxGpuCandidates && i < static_cast<int>(candidates.size()); ++i)
    {
        const DoomLightRecord& light = candidates[i];
        PathTraceDoomAnalyticLightCandidate gpuLight = {};
        gpuLight.originAndRadius[0] = light.origin.x;
        gpuLight.originAndRadius[1] = light.origin.y;
        gpuLight.originAndRadius[2] = light.origin.z;
        gpuLight.originAndRadius[3] = Max(light.sphereRadius, 0.01f);
        gpuLight.colorAndIntensity[0] = Max(light.color.x, 0.0f);
        gpuLight.colorAndIntensity[1] = Max(light.color.y, 0.0f);
        gpuLight.colorAndIntensity[2] = Max(light.color.z, 0.0f);
        gpuLight.colorAndIntensity[3] = Max(light.color.w, 0.0f);
        gpuLight.doomRadiusAndArea[0] = Max(light.radiusMax, 1.0f);
        gpuLight.doomRadiusAndArea[1] = 12.56637061f * gpuLight.originAndRadius[3] * gpuLight.originAndRadius[3];
        gpuLight.doomRadiusAndArea[2] = static_cast<float>(light.portalDepth);
        gpuLight.doomRadiusAndArea[3] = static_cast<float>(light.area);
        if (light.castsShadows)
        {
            gpuLight.flags |= RT_DOOM_ANALYTIC_LIGHT_CASTS_SHADOWS;
        }
        if (light.gameLinked)
        {
            gpuLight.flags |= RT_DOOM_ANALYTIC_LIGHT_GAME_LINKED;
        }
        if (light.crosshairBehind)
        {
            gpuLight.flags |= RT_DOOM_ANALYTIC_LIGHT_BEHIND_CAMERA;
        }
        gpuLight.renderLightIndex = static_cast<uint32_t>(Max(light.index, 0));
        gpuLight.entityNumber = static_cast<uint32_t>(Max(light.entityNumber, 0));
        gpuCandidates.push_back(gpuLight);
    }

    return gpuCandidates;
}

void RunPathTraceDoomLightDiagnostics(const viewDef_t* viewDef)
{
    if (!viewDef || !viewDef->renderWorld || !IsDoomLightGameStateActive())
    {
        return;
    }

    const bool wantsDump =
        r_pathTracingDoomLightDump.GetInteger() != 0 ||
        r_pathTracingDoomLightProbeDump.GetInteger() != 0 ||
        r_pathTracingAnalyticLightCandidateDump.GetInteger() != 0;
    if (!wantsDump)
    {
        return;
    }

    const DoomLightPortalSelection selection = BuildDoomLightPortalSelection(
        viewDef,
        idMath::ClampInt(0, 8, r_pathTracingLightAreaPortalSteps.GetInteger()));
    const std::unordered_map<int, DoomLightGameMetadata> gameMetadataByHandle = BuildDoomLightGameMetadataByHandle();
    const std::vector<DoomLightRecord> records = CollectDoomLightRecords(viewDef, selection, gameMetadataByHandle);
    RunDoomLightIdentityDump(viewDef, selection, records);
    RunDoomLightProbeDump(viewDef, records);
    RunAnalyticLightCandidateDump(selection, records);
}
