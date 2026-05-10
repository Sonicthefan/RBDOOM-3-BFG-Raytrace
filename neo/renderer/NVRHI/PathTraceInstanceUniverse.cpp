#include "precompiled.h"
#pragma hdrstop

#include "PathTraceCVars.h"
#include "PathTraceInstanceUniverse.h"
#include "../RenderCommon.h"

namespace {

bool PtInstanceMatricesMatch(const float lhs[16], const float rhs[16])
{
    for (int element = 0; element < 16; ++element)
    {
        if (idMath::Fabs(lhs[element] - rhs[element]) > 1.0e-4f)
        {
            return false;
        }
    }
    return true;
}

float PtInstanceMaxMatrixDelta(const float lhs[16], const float rhs[16])
{
    float maxDelta = 0.0f;
    for (int element = 0; element < 16; ++element)
    {
        maxDelta = Max(maxDelta, idMath::Fabs(lhs[element] - rhs[element]));
    }
    return maxDelta;
}

idVec3 PtInstanceMatrixOrigin(const float matrix[16])
{
    return idVec3(matrix[12], matrix[13], matrix[14]);
}

float PtInstanceOriginDelta(const float lhs[16], const float rhs[16])
{
    return (PtInstanceMatrixOrigin(lhs) - PtInstanceMatrixOrigin(rhs)).Length();
}

const char* PtInstanceSourceFlagSummary(uint32_t flags)
{
    if ((flags & RT_PT_INSTANCE_SOURCE_STATIC_WORLD) != 0)
    {
        return "static";
    }
    if ((flags & RT_PT_INSTANCE_SOURCE_RIGID) != 0)
    {
        return "rigid";
    }
    if ((flags & RT_PT_INSTANCE_SOURCE_SKINNED_OR_DEFORMING) != 0)
    {
        return "skinned/deforming";
    }
    if ((flags & RT_PT_INSTANCE_SOURCE_PARTICLE_OR_TRANSIENT) != 0)
    {
        return "particle/transient";
    }
    return "unknown";
}

}

void RtPathTraceInstanceUniverse::Clear()
{
    m_renderWorld = nullptr;
    m_frameIndex = 0;
    m_frameActive = false;
    m_meshRecords.clear();
    m_meshLookup.clear();
    m_frameMeshHashes.clear();
    m_instanceHistories.clear();
    m_instanceHistoryLookup.clear();
    m_frameInstances.clear();
    ResetFrameStats();
    ++m_generation;
}

void RtPathTraceInstanceUniverse::BeginFrame(uint64 frameIndex, const viewDef_t* viewDef)
{
    const void* renderWorld = viewDef ? viewDef->renderWorld : nullptr;
    if (renderWorld != m_renderWorld)
    {
        Clear();
        m_renderWorld = renderWorld;
    }

    m_frameIndex = frameIndex;
    m_frameActive = true;
    ResetFrameStats();
    m_frameStats.frameIndex = frameIndex;
    m_frameStats.generation = m_generation;
    m_frameMeshHashes.clear();
    m_frameInstances.clear();
}

void RtPathTraceInstanceUniverse::EndFrame()
{
    if (!m_frameActive)
    {
        return;
    }

    m_frameStats.uniqueMeshCount = static_cast<int>(m_frameMeshHashes.size());
    m_frameStats.instanceCount = m_frameStats.usableDrawSurfs;
    m_frameActive = false;
}

void RtPathTraceInstanceUniverse::SetObservedDrawSurfCount(int drawSurfCount)
{
    m_frameStats.drawSurfCount = drawSurfCount;
}

void RtPathTraceInstanceUniverse::RecordSkippedDrawSurf(const RtSmokeSurfaceSkipStats& skipStats)
{
    ++m_frameStats.skippedDrawSurfs;
    m_frameStats.nullSurfaceSkips += skipStats.nullSurface;
    m_frameStats.missingGeometrySkips += skipStats.missingGeometry;
    m_frameStats.nullMaterialSkips += skipStats.nullMaterial;
    m_frameStats.nullSpaceSkips += skipStats.nullSpace;
    m_frameStats.nullModelSkips += skipStats.nullModel;
    m_frameStats.invalidIndexSkips += skipStats.invalidIndexCount;
    m_frameStats.nonCurrentCacheSkips += skipStats.nonCurrentCache;
    m_frameStats.guiSurfaceSkips += skipStats.guiSurface;
    m_frameStats.callbackEntitySkips += skipStats.callbackEntity;
}

void RtPathTraceInstanceUniverse::RecordObservation(
    const RtPathTraceMeshObservation& meshObservation,
    const RtPathTraceInstanceObservation& instanceObservation,
    RtSmokeSurfaceClass surfaceClass,
    int numVerts,
    int numIndexes)
{
    bool meshCacheHit = false;
    MeshRecord* meshRecord = FindOrCreateMeshRecord(meshObservation, meshCacheHit);
    if (meshCacheHit)
    {
        ++m_frameStats.meshCacheHits;
    }
    else
    {
        ++m_frameStats.meshCacheMisses;
    }
    if (meshRecord)
    {
        meshRecord->lastSeenFrame = static_cast<int>(m_frameIndex);
        ++meshRecord->seenCount;
    }
    m_frameMeshHashes.insert(meshObservation.stableHash);
    RtPathTraceInstanceObservation frameInstance = instanceObservation;

    ++m_frameStats.usableDrawSurfs;
    switch (surfaceClass)
    {
        case RtSmokeSurfaceClass::StaticWorld:
            ++m_frameStats.staticWorldSurfaces;
            break;
        case RtSmokeSurfaceClass::RigidEntity:
            ++m_frameStats.rigidSurfaces;
            break;
        case RtSmokeSurfaceClass::SkinnedDeformed:
            ++m_frameStats.skinnedOrDeformingSurfaces;
            break;
        case RtSmokeSurfaceClass::ParticleAlpha:
            ++m_frameStats.particleOrTransientSurfaces;
            break;
        default:
            ++m_frameStats.unknownSurfaces;
            break;
    }

    if ((frameInstance.sourceFlags & RT_PT_INSTANCE_SOURCE_STATIC_UNIVERSE_MATCH) != 0)
    {
        ++m_frameStats.staticUniverseMatches;
    }
    if ((frameInstance.sourceFlags & RT_PT_INSTANCE_SOURCE_STATIC_CACHE_MATCH) != 0)
    {
        ++m_frameStats.staticGeometryCacheMatches;
    }
    if ((frameInstance.sourceFlags & RT_PT_INSTANCE_SOURCE_MATERIAL_OVERRIDE) != 0)
    {
        ++m_frameStats.materialOverrideObservations;
    }
    if ((frameInstance.sourceFlags & RT_PT_INSTANCE_SOURCE_SKINNED_OR_DEFORMING) != 0)
    {
        ++m_frameStats.dynamicSkinnedDeformingCandidates;
    }
    if ((frameInstance.sourceFlags & RT_PT_INSTANCE_SOURCE_CALLBACK_OR_GENERATED) != 0)
    {
        ++m_frameStats.callbackOrGeneratedCandidates;
    }
    if ((frameInstance.sourceFlags & RT_PT_INSTANCE_SOURCE_GUI) != 0)
    {
        ++m_frameStats.guiCandidates;
    }
    if ((frameInstance.sourceFlags & RT_PT_INSTANCE_SOURCE_PARTICLE_OR_TRANSIENT) != 0)
    {
        ++m_frameStats.particlesOrTransientCandidates;
    }
    if (frameInstance.materialName.IsEmpty() || frameInstance.materialName.Icmp("<none>") == 0)
    {
        ++m_frameStats.missingMaterialOrSkinOverrideMetadata;
    }

    InstanceHistory* history = FindOrCreateInstanceHistory(frameInstance.instanceId);
    if (history)
    {
        const bool hasAnyPrevious = history->lastSeenFrame > 0;
        const bool hasConsecutivePrevious = hasAnyPrevious && history->lastSeenFrame + 1 == m_frameIndex;
        if (hasConsecutivePrevious)
        {
            frameInstance.hasPreviousObjectToWorld = true;
            frameInstance.transformContinuous = true;
            memcpy(frameInstance.previousObjectToWorld, history->lastObjectToWorld, sizeof(frameInstance.previousObjectToWorld));
        }
        if (!hasAnyPrevious)
        {
            memcpy(history->firstObjectToWorld, frameInstance.objectToWorld, sizeof(history->firstObjectToWorld));
        }
        else
        {
            history->maxObservedMatrixDelta = Max(history->maxObservedMatrixDelta, PtInstanceMaxMatrixDelta(history->firstObjectToWorld, frameInstance.objectToWorld));
            history->maxObservedOriginDelta = Max(history->maxObservedOriginDelta, PtInstanceOriginDelta(history->firstObjectToWorld, frameInstance.objectToWorld));
            if (PtInstanceMatricesMatch(history->lastObjectToWorld, frameInstance.objectToWorld))
            {
                if (hasConsecutivePrevious)
                {
                    ++history->sameTransformCount;
                    ++m_frameStats.sameTransformObservations;
                }
            }
            else
            {
                ++history->changedTransformCount;
                if (hasConsecutivePrevious)
                {
                    ++m_frameStats.changedTransformObservations;
                    if (surfaceClass == RtSmokeSurfaceClass::RigidEntity)
                    {
                        ++m_frameStats.changingTransformRigidObservations;
                    }
                }
            }
        }
        if (history->changedTransformCount > 0)
        {
            ++m_frameStats.everChangedTransformObservations;
            if (surfaceClass == RtSmokeSurfaceClass::RigidEntity)
            {
                ++m_frameStats.everChangedRigidTransformObservations;
                AddMovedRigidSample(meshObservation, frameInstance, *history);
            }
        }
        memcpy(history->lastObjectToWorld, frameInstance.objectToWorld, sizeof(history->lastObjectToWorld));
        history->lastSeenFrame = m_frameIndex;
    }

    m_frameInstances.push_back(frameInstance);
    AddSample(meshObservation, frameInstance, surfaceClass, numVerts, numIndexes);
}

const RtPathTraceInstanceUniverseStats& RtPathTraceInstanceUniverse::GetFrameStats() const
{
    return m_frameStats;
}

const std::vector<RtPathTraceInstanceObservation>& RtPathTraceInstanceUniverse::FrameInstances() const
{
    return m_frameInstances;
}

void RtPathTraceInstanceUniverse::RunDiagnostics(const RtPathTraceInstanceUniverseDiagnosticDesc& desc)
{
    if (r_pathTracingSmokeLog.GetInteger() != 0 && (m_frameIndex % 120ull) == 1ull)
    {
        common->Printf("PathTracePrimaryPass: PT drawSurf mirror source=%d drawSurfs=%d usable=%d skipped=%d meshes=%d instances=%d oldSmokeSurfaces=%d static/rigid/skinned/particle/unknown=%d/%d/%d/%d/%d matches(scene/staticCache)=%d/%d transforms(same/changed/rigidChanged)=%d/%d/%d lifetimeChanged(all/rigid)=%d/%d overrides=%d missingMaterial=%d candidates(skinned/callback/gui/transient)=%d/%d/%d/%d\n",
            desc.sceneSource,
            m_frameStats.drawSurfCount,
            m_frameStats.usableDrawSurfs,
            m_frameStats.skippedDrawSurfs,
            m_frameStats.uniqueMeshCount,
            m_frameStats.instanceCount,
            desc.legacySourceSurfaces,
            m_frameStats.staticWorldSurfaces,
            m_frameStats.rigidSurfaces,
            m_frameStats.skinnedOrDeformingSurfaces,
            m_frameStats.particleOrTransientSurfaces,
            m_frameStats.unknownSurfaces,
            m_frameStats.staticUniverseMatches,
            m_frameStats.staticGeometryCacheMatches,
            m_frameStats.sameTransformObservations,
            m_frameStats.changedTransformObservations,
            m_frameStats.changingTransformRigidObservations,
            m_frameStats.everChangedTransformObservations,
            m_frameStats.everChangedRigidTransformObservations,
            m_frameStats.materialOverrideObservations,
            m_frameStats.missingMaterialOrSkinOverrideMetadata,
            m_frameStats.dynamicSkinnedDeformingCandidates,
            m_frameStats.callbackOrGeneratedCandidates,
            m_frameStats.guiCandidates,
            m_frameStats.particlesOrTransientCandidates);
    }

    if (!desc.dumpRequested)
    {
        return;
    }

    const RtSmokeSurfaceClassStats* legacyClassStats = desc.legacyClassStats;
    const RtSmokeSurfaceSkipStats* legacySkipStats = desc.legacySkipStats;
    common->Printf("PathTracePrimaryPass: PT instance universe dump source=%d frame=%llu generation=%llu drawSurfs=%d usable=%d skipped=%d uniqueMeshes=%d instances=%d meshCache(hit/miss)=%d/%d\n",
        desc.sceneSource,
        static_cast<unsigned long long>(m_frameStats.frameIndex),
        static_cast<unsigned long long>(m_frameStats.generation),
        m_frameStats.drawSurfCount,
        m_frameStats.usableDrawSurfs,
        m_frameStats.skippedDrawSurfs,
        m_frameStats.uniqueMeshCount,
        m_frameStats.instanceCount,
        m_frameStats.meshCacheHits,
        m_frameStats.meshCacheMisses);
    common->Printf("PathTracePrimaryPass: PT instance universe classes mirror static/rigid/skinned/particle/unknown=%d/%d/%d/%d/%d oldSmoke=%d/%d/%d/%d/%d sourceSurfaces=%d\n",
        m_frameStats.staticWorldSurfaces,
        m_frameStats.rigidSurfaces,
        m_frameStats.skinnedOrDeformingSurfaces,
        m_frameStats.particleOrTransientSurfaces,
        m_frameStats.unknownSurfaces,
        legacyClassStats ? legacyClassStats->staticWorldSurfaces : 0,
        legacyClassStats ? legacyClassStats->rigidEntitySurfaces : 0,
        legacyClassStats ? legacyClassStats->skinnedDeformedSurfaces : 0,
        legacyClassStats ? legacyClassStats->particleAlphaSurfaces : 0,
        legacyClassStats ? legacyClassStats->unknownSurfaces : 0,
        desc.legacySourceSurfaces);
    common->Printf("PathTracePrimaryPass: PT instance universe identity matches(scene/staticCache)=%d/%d transforms(same/changed/rigidChanged)=%d/%d/%d lifetimeChanged(all/rigid)=%d/%d overrides=%d missingMaterialOrSkin=%d dynamicCandidates(skinned/callback/gui/transient)=%d/%d/%d/%d\n",
        m_frameStats.staticUniverseMatches,
        m_frameStats.staticGeometryCacheMatches,
        m_frameStats.sameTransformObservations,
        m_frameStats.changedTransformObservations,
        m_frameStats.changingTransformRigidObservations,
        m_frameStats.everChangedTransformObservations,
        m_frameStats.everChangedRigidTransformObservations,
        m_frameStats.materialOverrideObservations,
        m_frameStats.missingMaterialOrSkinOverrideMetadata,
        m_frameStats.dynamicSkinnedDeformingCandidates,
        m_frameStats.callbackOrGeneratedCandidates,
        m_frameStats.guiCandidates,
        m_frameStats.particlesOrTransientCandidates);
    common->Printf("PathTracePrimaryPass: PT instance universe skips mirror null/missingGeo/nullMat/nullSpace/nullModel/invalid/nonCurrent/gui/callback=%d/%d/%d/%d/%d/%d/%d/%d/%d oldSmoke=%d/%d/%d/%d/%d/%d/%d/%d/%d\n",
        m_frameStats.nullSurfaceSkips,
        m_frameStats.missingGeometrySkips,
        m_frameStats.nullMaterialSkips,
        m_frameStats.nullSpaceSkips,
        m_frameStats.nullModelSkips,
        m_frameStats.invalidIndexSkips,
        m_frameStats.nonCurrentCacheSkips,
        m_frameStats.guiSurfaceSkips,
        m_frameStats.callbackEntitySkips,
        legacySkipStats ? legacySkipStats->nullSurface : 0,
        legacySkipStats ? legacySkipStats->missingGeometry : 0,
        legacySkipStats ? legacySkipStats->nullMaterial : 0,
        legacySkipStats ? legacySkipStats->nullSpace : 0,
        legacySkipStats ? legacySkipStats->nullModel : 0,
        legacySkipStats ? legacySkipStats->invalidIndexCount : 0,
        legacySkipStats ? legacySkipStats->nonCurrentCache : 0,
        legacySkipStats ? legacySkipStats->guiSurface : 0,
        legacySkipStats ? legacySkipStats->callbackEntity : 0);

    for (int sampleIndex = 0; sampleIndex < m_frameStats.sampleCount; ++sampleIndex)
    {
        const RtPathTraceInstanceUniverseSample& sample = m_frameStats.samples[sampleIndex];
        if (!sample.valid)
        {
            continue;
        }

        common->Printf("PathTracePrimaryPass: PT instance sample %d surf=%d entity=%d renderEntity=%d class=%s flags=%s mesh=%llu instance=%llu origin=(%.2f %.2f %.2f) verts=%d indexes=%d material='%s' model='%s'\n",
            sampleIndex,
            sample.drawSurfIndex,
            sample.entityIndex,
            sample.renderEntityNum,
            SmokeSurfaceClassName(sample.surfaceClass),
            PtInstanceSourceFlagSummary(sample.sourceFlags),
            static_cast<unsigned long long>(sample.meshHash),
            static_cast<unsigned long long>(sample.instanceId),
            sample.origin.x,
            sample.origin.y,
            sample.origin.z,
            sample.verts,
            sample.indexes,
            sample.materialName.c_str(),
            sample.modelName.c_str());
    }

    for (int sampleIndex = 0; sampleIndex < m_frameStats.movedRigidSampleCount; ++sampleIndex)
    {
        const RtPathTraceMovedRigidInstanceSample& sample = m_frameStats.movedRigidSamples[sampleIndex];
        if (!sample.valid)
        {
            continue;
        }

        common->Printf("PathTracePrimaryPass: PT moved rigid sample %d surf=%d entity=%d renderEntity=%d mesh=%llu instance=%llu changes=%d firstOrigin=(%.2f %.2f %.2f) currentOrigin=(%.2f %.2f %.2f) currentMatrixDelta=%.6f maxObservedMatrixDelta=%.6f maxObservedOriginDelta=%.3f material='%s' model='%s'\n",
            sampleIndex,
            sample.drawSurfIndex,
            sample.entityIndex,
            sample.renderEntityNum,
            static_cast<unsigned long long>(sample.meshHash),
            static_cast<unsigned long long>(sample.instanceId),
            sample.transformChangeCount,
            sample.firstOrigin.x,
            sample.firstOrigin.y,
            sample.firstOrigin.z,
            sample.currentOrigin.x,
            sample.currentOrigin.y,
            sample.currentOrigin.z,
            sample.maxFirstToCurrentMatrixDelta,
            sample.maxObservedMatrixDelta,
            sample.maxObservedOriginDelta,
            sample.materialName.c_str(),
            sample.modelName.c_str());
    }

    r_pathTracingInstanceUniverseDump.SetInteger(0);
}

void RtPathTraceInstanceUniverse::ResetFrameStats()
{
    m_frameStats = RtPathTraceInstanceUniverseStats();
}

RtPathTraceInstanceUniverse::MeshRecord* RtPathTraceInstanceUniverse::FindOrCreateMeshRecord(const RtPathTraceMeshObservation& observation, bool& cacheHit)
{
    cacheHit = false;
    const std::unordered_map<uint64, size_t>::iterator it = m_meshLookup.find(observation.stableHash);
    if (it != m_meshLookup.end() && it->second < m_meshRecords.size())
    {
        MeshRecord& record = m_meshRecords[it->second];
        cacheHit = true;
        return &record;
    }

    MeshRecord record;
    record.key = observation.key;
    record.stableHash = observation.stableHash;
    record.baseMaterial = observation.baseMaterial;
    record.materialName = observation.materialName;
    record.modelName = observation.modelName;
    record.firstSeenFrame = static_cast<int>(m_frameIndex);
    record.lastSeenFrame = static_cast<int>(m_frameIndex);
    record.seenCount = 0;
    record.localSpaceValid = observation.localSpaceValid;
    const size_t recordIndex = m_meshRecords.size();
    m_meshRecords.push_back(record);
    m_meshLookup[observation.stableHash] = recordIndex;
    ++m_generation;
    return &m_meshRecords.back();
}

RtPathTraceInstanceUniverse::InstanceHistory* RtPathTraceInstanceUniverse::FindOrCreateInstanceHistory(uint64 instanceId)
{
    const std::unordered_map<uint64, size_t>::iterator it = m_instanceHistoryLookup.find(instanceId);
    if (it != m_instanceHistoryLookup.end() && it->second < m_instanceHistories.size())
    {
        return &m_instanceHistories[it->second];
    }

    InstanceHistory history;
    history.instanceId = instanceId;
    const size_t historyIndex = m_instanceHistories.size();
    m_instanceHistories.push_back(history);
    m_instanceHistoryLookup[instanceId] = historyIndex;
    return &m_instanceHistories.back();
}

void RtPathTraceInstanceUniverse::AddSample(
    const RtPathTraceMeshObservation& meshObservation,
    const RtPathTraceInstanceObservation& instanceObservation,
    RtSmokeSurfaceClass surfaceClass,
    int numVerts,
    int numIndexes)
{
    if (m_frameStats.sampleCount >= RT_PT_INSTANCE_UNIVERSE_SAMPLES)
    {
        return;
    }

    RtPathTraceInstanceUniverseSample& sample = m_frameStats.samples[m_frameStats.sampleCount++];
    sample.valid = true;
    sample.drawSurfIndex = instanceObservation.drawSurfIndex;
    sample.entityIndex = instanceObservation.entityIndex;
    sample.renderEntityNum = instanceObservation.renderEntityNum;
    sample.verts = numVerts;
    sample.indexes = numIndexes;
    sample.meshHash = meshObservation.stableHash;
    sample.instanceId = instanceObservation.instanceId;
    sample.surfaceClass = surfaceClass;
    sample.sourceFlags = instanceObservation.sourceFlags;
    sample.origin.Set(
        instanceObservation.objectToWorld[12],
        instanceObservation.objectToWorld[13],
        instanceObservation.objectToWorld[14]);
    sample.materialName = instanceObservation.materialName;
    sample.modelName = instanceObservation.modelName;
}

void RtPathTraceInstanceUniverse::AddMovedRigidSample(
    const RtPathTraceMeshObservation& meshObservation,
    const RtPathTraceInstanceObservation& instanceObservation,
    const InstanceHistory& history)
{
    if (m_frameStats.movedRigidSampleCount >= RT_PT_INSTANCE_UNIVERSE_MOVED_RIGID_SAMPLES)
    {
        return;
    }

    RtPathTraceMovedRigidInstanceSample& sample = m_frameStats.movedRigidSamples[m_frameStats.movedRigidSampleCount++];
    sample.valid = true;
    sample.drawSurfIndex = instanceObservation.drawSurfIndex;
    sample.entityIndex = instanceObservation.entityIndex;
    sample.renderEntityNum = instanceObservation.renderEntityNum;
    sample.meshHash = meshObservation.stableHash;
    sample.instanceId = instanceObservation.instanceId;
    sample.transformChangeCount = history.changedTransformCount;
    sample.maxFirstToCurrentMatrixDelta = PtInstanceMaxMatrixDelta(history.firstObjectToWorld, instanceObservation.objectToWorld);
    sample.maxObservedMatrixDelta = history.maxObservedMatrixDelta;
    sample.maxObservedOriginDelta = history.maxObservedOriginDelta;
    sample.firstOrigin = PtInstanceMatrixOrigin(history.firstObjectToWorld);
    sample.currentOrigin = PtInstanceMatrixOrigin(instanceObservation.objectToWorld);
    sample.materialName = instanceObservation.materialName;
    sample.modelName = instanceObservation.modelName;
}
