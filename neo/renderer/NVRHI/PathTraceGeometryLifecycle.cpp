#include "precompiled.h"
#pragma hdrstop

#include "PathTraceGeometryLifecycle.h"
#include "PathTraceCVars.h"
#include "../RenderCommon.h"
#include "../RenderWorld_local.h"

#include <unordered_map>
#include <vector>

namespace {

const int PT_GEOMETRY_LIFECYCLE_MAX_EVENT_SAMPLES = 16;

struct PtGeometryLifecycleSlotState
{
    uint32_t generation = 1;
    bool alive = false;
};

struct PtGeometryLifecycleWorldState
{
    std::vector<PtGeometryLifecycleSlotState> entitySlots;
    std::vector<PtGeometryLifecycleSlotState> lightSlots;
};

struct PtGeometryLifecycleEventSample
{
    PtGeometryLifecycleEventKind eventKind = PtGeometryLifecycleEventKind::Add;
    PtGeometryLifecycleDefKind defKind = PtGeometryLifecycleDefKind::Entity;
    PtRenderDefKey key;
    int entityNum = -1;
    int lastModifiedFrameNum = 0;
    PtGeometryLifecycleClass geometryClass = PtGeometryLifecycleClass::Unknown;
    bool modelChanged = false;
    const idRenderModel* oldModel = nullptr;
    const idRenderModel* newModel = nullptr;
};

struct PtGeometryLifecycleStats
{
    int entityAdds = 0;
    int entityUpdates = 0;
    int entityFrees = 0;
    int entityModelSwaps = 0;
    int lightAdds = 0;
    int lightUpdates = 0;
    int lightFrees = 0;
    int classWorld = 0;
    int classRigidAtRest = 0;
    int classRigidMoving = 0;
    int classDeforming = 0;
    int classTransient = 0;
    int classUnknown = 0;
    PtGeometryLifecycleEventSample samples[PT_GEOMETRY_LIFECYCLE_MAX_EVENT_SAMPLES];
    int sampleCount = 0;
};

std::unordered_map<const void*, PtGeometryLifecycleWorldState> g_lifecycleWorlds;
PtGeometryLifecycleStats g_lifecycleStats;

bool LifecycleDiagnosticsEnabled()
{
    return r_pathTracingGeometryLifecycle.GetInteger() != 0 ||
        r_pathTracingGeometryLifecycleStage.GetInteger() != 0 ||
        r_pathTracingGeometryLifecycleDump.GetInteger() != 0;
}

PtGeometryLifecycleWorldState& EnsureWorldState(const void* world)
{
    return g_lifecycleWorlds[world];
}

PtGeometryLifecycleSlotState& EnsureSlot(std::vector<PtGeometryLifecycleSlotState>& slots, int index)
{
    if (index < 0)
    {
        static PtGeometryLifecycleSlotState invalidSlot;
        invalidSlot = PtGeometryLifecycleSlotState();
        return invalidSlot;
    }
    if (index >= static_cast<int>(slots.size()))
    {
        slots.resize(static_cast<size_t>(index + 1));
    }
    return slots[static_cast<size_t>(index)];
}

const PtGeometryLifecycleSlotState* FindSlot(const std::vector<PtGeometryLifecycleSlotState>& slots, int index)
{
    if (index < 0 || index >= static_cast<int>(slots.size()))
    {
        return nullptr;
    }
    return &slots[static_cast<size_t>(index)];
}

PtGeometryLifecycleSlotState* FindSlot(std::vector<PtGeometryLifecycleSlotState>& slots, int index)
{
    if (index < 0 || index >= static_cast<int>(slots.size()))
    {
        return nullptr;
    }
    return &slots[static_cast<size_t>(index)];
}

PtRenderDefKey MakeKey(const void* world, int index, const std::vector<PtGeometryLifecycleSlotState>& slots)
{
    PtRenderDefKey key;
    key.world = world;
    key.index = index;
    const PtGeometryLifecycleSlotState* slot = FindSlot(slots, index);
    key.generation = slot ? slot->generation : 1u;
    return key;
}

void AccumulateClass(PtGeometryLifecycleClass geometryClass)
{
    switch (geometryClass)
    {
        case PtGeometryLifecycleClass::World:
            ++g_lifecycleStats.classWorld;
            break;
        case PtGeometryLifecycleClass::RigidAtRest:
            ++g_lifecycleStats.classRigidAtRest;
            break;
        case PtGeometryLifecycleClass::RigidMoving:
            ++g_lifecycleStats.classRigidMoving;
            break;
        case PtGeometryLifecycleClass::Deforming:
            ++g_lifecycleStats.classDeforming;
            break;
        case PtGeometryLifecycleClass::Transient:
            ++g_lifecycleStats.classTransient;
            break;
        default:
            ++g_lifecycleStats.classUnknown;
            break;
    }
}

void AddEventSample(const PtGeometryLifecycleEventSample& sample)
{
    if (!LifecycleDiagnosticsEnabled() ||
        g_lifecycleStats.sampleCount >= PT_GEOMETRY_LIFECYCLE_MAX_EVENT_SAMPLES)
    {
        return;
    }
    g_lifecycleStats.samples[g_lifecycleStats.sampleCount++] = sample;
}

const char* EventName(PtGeometryLifecycleEventKind kind)
{
    switch (kind)
    {
        case PtGeometryLifecycleEventKind::Add:
            return "add";
        case PtGeometryLifecycleEventKind::Update:
            return "update";
        case PtGeometryLifecycleEventKind::Free:
            return "free";
        default:
            return "unknown";
    }
}

const char* DefKindName(PtGeometryLifecycleDefKind kind)
{
    switch (kind)
    {
        case PtGeometryLifecycleDefKind::Entity:
            return "entity";
        case PtGeometryLifecycleDefKind::Light:
            return "light";
        default:
            return "unknown";
    }
}

void MarkEntityAlive(const idRenderEntityLocal* entity)
{
    if (!entity)
    {
        return;
    }
    PtGeometryLifecycleWorldState& state = EnsureWorldState(entity->world);
    PtGeometryLifecycleSlotState& slot = EnsureSlot(state.entitySlots, entity->index);
    slot.alive = true;
}

void MarkLightAlive(const idRenderLightLocal* light)
{
    if (!light)
    {
        return;
    }
    PtGeometryLifecycleWorldState& state = EnsureWorldState(light->world);
    PtGeometryLifecycleSlotState& slot = EnsureSlot(state.lightSlots, light->index);
    slot.alive = true;
}

}

namespace PtGeometryLifecycle {

PtRenderDefKey MakeEntityKey(const idRenderEntityLocal* entity)
{
    if (!entity)
    {
        return PtRenderDefKey();
    }
    PtGeometryLifecycleWorldState& state = EnsureWorldState(entity->world);
    return MakeKey(entity->world, entity->index, state.entitySlots);
}

PtRenderDefKey MakeLightKey(const idRenderLightLocal* light)
{
    if (!light)
    {
        return PtRenderDefKey();
    }
    PtGeometryLifecycleWorldState& state = EnsureWorldState(light->world);
    return MakeKey(light->world, light->index, state.lightSlots);
}

uint32_t EntityGeneration(const void* world, int index)
{
    PtGeometryLifecycleWorldState& state = EnsureWorldState(world);
    return EnsureSlot(state.entitySlots, index).generation;
}

uint32_t LightGeneration(const void* world, int index)
{
    PtGeometryLifecycleWorldState& state = EnsureWorldState(world);
    return EnsureSlot(state.lightSlots, index).generation;
}

bool IsEntityKeyAlive(const PtRenderDefKey& key)
{
    if (!key.world || key.index < 0 || key.generation == 0)
    {
        return false;
    }
    std::unordered_map<const void*, PtGeometryLifecycleWorldState>::const_iterator worldIt = g_lifecycleWorlds.find(key.world);
    if (worldIt == g_lifecycleWorlds.end())
    {
        return false;
    }
    const PtGeometryLifecycleSlotState* slot = FindSlot(worldIt->second.entitySlots, key.index);
    return slot && slot->alive && slot->generation == key.generation;
}

bool IsLightKeyAlive(const PtRenderDefKey& key)
{
    if (!key.world || key.index < 0 || key.generation == 0)
    {
        return false;
    }
    std::unordered_map<const void*, PtGeometryLifecycleWorldState>::const_iterator worldIt = g_lifecycleWorlds.find(key.world);
    if (worldIt == g_lifecycleWorlds.end())
    {
        return false;
    }
    const PtGeometryLifecycleSlotState* slot = FindSlot(worldIt->second.lightSlots, key.index);
    return slot && slot->alive && slot->generation == key.generation;
}

void ClearWorld(const void* world)
{
    if (!world)
    {
        return;
    }
    g_lifecycleWorlds.erase(world);
}

void ClearAll()
{
    g_lifecycleWorlds.clear();
    g_lifecycleStats = PtGeometryLifecycleStats();
}

PtGeometryLifecycleClass ClassifyEntity(const idRenderEntityLocal* entity)
{
    const renderEntity_t* renderEntity = entity ? &entity->parms : nullptr;
    const idRenderModel* model = renderEntity ? renderEntity->hModel : nullptr;
    if (!entity || !renderEntity || !model)
    {
        if (renderEntity && renderEntity->callback)
        {
            return PtGeometryLifecycleClass::Deforming;
        }
        return PtGeometryLifecycleClass::Unknown;
    }
    if (model->IsStaticWorldModel())
    {
        return PtGeometryLifecycleClass::World;
    }
    const dynamicModel_t dynamicModel = model->IsDynamicModel();
    if (dynamicModel == DM_CONTINUOUS)
    {
        return PtGeometryLifecycleClass::Transient;
    }
    if (dynamicModel == DM_CACHED ||
        renderEntity->callback != nullptr ||
        renderEntity->joints != nullptr ||
        renderEntity->numJoints > 0 ||
        entity->dynamicModel != nullptr ||
        entity->cachedDynamicModel != nullptr)
    {
        return PtGeometryLifecycleClass::Deforming;
    }
    if (dynamicModel == DM_STATIC)
    {
        return entity->lastModifiedFrameNum == tr.frameCount
            ? PtGeometryLifecycleClass::RigidMoving
            : PtGeometryLifecycleClass::RigidAtRest;
    }
    return PtGeometryLifecycleClass::Unknown;
}

const char* ClassName(PtGeometryLifecycleClass geometryClass)
{
    switch (geometryClass)
    {
        case PtGeometryLifecycleClass::World:
            return "world";
        case PtGeometryLifecycleClass::RigidAtRest:
            return "rigid-at-rest";
        case PtGeometryLifecycleClass::RigidMoving:
            return "rigid-moving";
        case PtGeometryLifecycleClass::Deforming:
            return "deforming";
        case PtGeometryLifecycleClass::Transient:
            return "transient";
        default:
            return "unknown";
    }
}

void NotifyEntityAdded(const idRenderEntityLocal* entity)
{
    if (!entity)
    {
        return;
    }
    MarkEntityAlive(entity);
    ++g_lifecycleStats.entityAdds;
    const PtGeometryLifecycleClass geometryClass = ClassifyEntity(entity);
    AccumulateClass(geometryClass);

    PtGeometryLifecycleEventSample sample;
    sample.eventKind = PtGeometryLifecycleEventKind::Add;
    sample.defKind = PtGeometryLifecycleDefKind::Entity;
    sample.key = MakeEntityKey(entity);
    sample.entityNum = entity->parms.entityNum;
    sample.lastModifiedFrameNum = entity->lastModifiedFrameNum;
    sample.geometryClass = geometryClass;
    sample.newModel = entity->parms.hModel;
    AddEventSample(sample);
}

void NotifyEntityUpdated(const idRenderEntityLocal* entity, const idRenderModel* oldModel, bool modelChanged)
{
    if (!entity)
    {
        return;
    }
    MarkEntityAlive(entity);
    ++g_lifecycleStats.entityUpdates;
    if (modelChanged)
    {
        ++g_lifecycleStats.entityModelSwaps;
    }
    const PtGeometryLifecycleClass geometryClass = ClassifyEntity(entity);
    AccumulateClass(geometryClass);

    PtGeometryLifecycleEventSample sample;
    sample.eventKind = PtGeometryLifecycleEventKind::Update;
    sample.defKind = PtGeometryLifecycleDefKind::Entity;
    sample.key = MakeEntityKey(entity);
    sample.entityNum = entity->parms.entityNum;
    sample.lastModifiedFrameNum = entity->lastModifiedFrameNum;
    sample.geometryClass = geometryClass;
    sample.modelChanged = modelChanged;
    sample.oldModel = oldModel;
    sample.newModel = entity->parms.hModel;
    AddEventSample(sample);
}

void NotifyEntityFreed(const idRenderEntityLocal* entity)
{
    if (!entity)
    {
        return;
    }
    PtGeometryLifecycleWorldState& state = EnsureWorldState(entity->world);
    PtGeometryLifecycleSlotState& slot = EnsureSlot(state.entitySlots, entity->index);
    const PtRenderDefKey oldKey = MakeKey(entity->world, entity->index, state.entitySlots);
    slot.alive = false;
    ++slot.generation;
    if (slot.generation == 0)
    {
        slot.generation = 1;
    }

    ++g_lifecycleStats.entityFrees;
    PtGeometryLifecycleEventSample sample;
    sample.eventKind = PtGeometryLifecycleEventKind::Free;
    sample.defKind = PtGeometryLifecycleDefKind::Entity;
    sample.key = oldKey;
    sample.entityNum = entity->parms.entityNum;
    sample.lastModifiedFrameNum = entity->lastModifiedFrameNum;
    sample.geometryClass = ClassifyEntity(entity);
    sample.newModel = entity->parms.hModel;
    AddEventSample(sample);
}

void NotifyLightAdded(const idRenderLightLocal* light)
{
    if (!light)
    {
        return;
    }
    MarkLightAlive(light);
    ++g_lifecycleStats.lightAdds;

    PtGeometryLifecycleEventSample sample;
    sample.eventKind = PtGeometryLifecycleEventKind::Add;
    sample.defKind = PtGeometryLifecycleDefKind::Light;
    sample.key = MakeLightKey(light);
    sample.lastModifiedFrameNum = light->lastModifiedFrameNum;
    AddEventSample(sample);
}

void NotifyLightUpdated(const idRenderLightLocal* light)
{
    if (!light)
    {
        return;
    }
    MarkLightAlive(light);
    ++g_lifecycleStats.lightUpdates;

    PtGeometryLifecycleEventSample sample;
    sample.eventKind = PtGeometryLifecycleEventKind::Update;
    sample.defKind = PtGeometryLifecycleDefKind::Light;
    sample.key = MakeLightKey(light);
    sample.lastModifiedFrameNum = light->lastModifiedFrameNum;
    AddEventSample(sample);
}

void NotifyLightFreed(const idRenderLightLocal* light)
{
    if (!light)
    {
        return;
    }
    PtGeometryLifecycleWorldState& state = EnsureWorldState(light->world);
    PtGeometryLifecycleSlotState& slot = EnsureSlot(state.lightSlots, light->index);
    const PtRenderDefKey oldKey = MakeKey(light->world, light->index, state.lightSlots);
    slot.alive = false;
    ++slot.generation;
    if (slot.generation == 0)
    {
        slot.generation = 1;
    }

    ++g_lifecycleStats.lightFrees;
    PtGeometryLifecycleEventSample sample;
    sample.eventKind = PtGeometryLifecycleEventKind::Free;
    sample.defKind = PtGeometryLifecycleDefKind::Light;
    sample.key = oldKey;
    sample.lastModifiedFrameNum = light->lastModifiedFrameNum;
    AddEventSample(sample);
}

void MaybeDumpLifecycleStats(uint64_t frameIndex)
{
    if (r_pathTracingGeometryLifecycleDump.GetInteger() == 0)
    {
        return;
    }

    common->Printf("PathTracePrimaryPass: geometry lifecycle frame=%llu cvar=%d stage=%d entity add/update/free=%d/%d/%d modelSwaps=%d light add/update/free=%d/%d/%d class world/rigidRest/rigidMoving/deforming/transient/unknown=%d/%d/%d/%d/%d/%d worlds=%d\n",
        static_cast<unsigned long long>(frameIndex),
        r_pathTracingGeometryLifecycle.GetInteger(),
        r_pathTracingGeometryLifecycleStage.GetInteger(),
        g_lifecycleStats.entityAdds,
        g_lifecycleStats.entityUpdates,
        g_lifecycleStats.entityFrees,
        g_lifecycleStats.entityModelSwaps,
        g_lifecycleStats.lightAdds,
        g_lifecycleStats.lightUpdates,
        g_lifecycleStats.lightFrees,
        g_lifecycleStats.classWorld,
        g_lifecycleStats.classRigidAtRest,
        g_lifecycleStats.classRigidMoving,
        g_lifecycleStats.classDeforming,
        g_lifecycleStats.classTransient,
        g_lifecycleStats.classUnknown,
        static_cast<int>(g_lifecycleWorlds.size()));

    for (int sampleIndex = 0; sampleIndex < g_lifecycleStats.sampleCount; ++sampleIndex)
    {
        const PtGeometryLifecycleEventSample& sample = g_lifecycleStats.samples[sampleIndex];
        common->Printf("PathTracePrimaryPass: geometry lifecycle sample %d %s %s world=%p index=%d generation=%u entityNum=%d class=%s modifiedFrame=%d modelChanged=%d oldModel=%p newModel=%p\n",
            sampleIndex,
            DefKindName(sample.defKind),
            EventName(sample.eventKind),
            sample.key.world,
            sample.key.index,
            sample.key.generation,
            sample.entityNum,
            ClassName(sample.geometryClass),
            sample.lastModifiedFrameNum,
            sample.modelChanged ? 1 : 0,
            sample.oldModel,
            sample.newModel);
    }

    g_lifecycleStats = PtGeometryLifecycleStats();
    r_pathTracingGeometryLifecycleDump.SetInteger(0);
}

}
