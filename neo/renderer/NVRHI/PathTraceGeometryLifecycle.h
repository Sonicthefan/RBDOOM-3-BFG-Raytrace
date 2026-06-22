#pragma once

#include <stdint.h>

class idRenderEntityLocal;
class idRenderLightLocal;
class idRenderModel;

struct PtRenderDefKey
{
    const void* world = nullptr;
    int index = -1;
    uint32_t generation = 0;
};

enum class PtGeometryLifecycleEventKind : uint32_t
{
    Add = 0,
    Update,
    Free
};

enum class PtGeometryLifecycleDefKind : uint32_t
{
    Entity = 0,
    Light
};

enum class PtGeometryLifecycleClass : uint32_t
{
    Unknown = 0,
    World,
    RigidAtRest,
    RigidMoving,
    Deforming,
    Transient
};

namespace PtGeometryLifecycle
{
    PtRenderDefKey MakeEntityKey(const idRenderEntityLocal* entity);
    PtRenderDefKey MakeLightKey(const idRenderLightLocal* light);

    uint32_t EntityGeneration(const void* world, int index);
    uint32_t LightGeneration(const void* world, int index);

    bool IsEntityKeyAlive(const PtRenderDefKey& key);
    bool IsLightKeyAlive(const PtRenderDefKey& key);
    void ClearWorld(const void* world);
    void ClearAll();

    PtGeometryLifecycleClass ClassifyEntity(const idRenderEntityLocal* entity);
    const char* ClassName(PtGeometryLifecycleClass geometryClass);

    void NotifyEntityAdded(const idRenderEntityLocal* entity);
    void NotifyEntityUpdated(const idRenderEntityLocal* entity, const idRenderModel* oldModel, bool modelChanged);
    void NotifyEntityFreed(const idRenderEntityLocal* entity);

    void NotifyLightAdded(const idRenderLightLocal* light);
    void NotifyLightUpdated(const idRenderLightLocal* light);
    void NotifyLightFreed(const idRenderLightLocal* light);

    void MaybeDumpLifecycleStats(uint64_t frameIndex);
}
