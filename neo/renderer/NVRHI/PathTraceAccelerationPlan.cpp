#include "PathTraceAccelerationPlan.h"

#include <cstring>

namespace {

bool PlanRangeValid(int offset, int count, int totalCount)
{
    return offset >= 0 && count >= 0 && offset <= totalCount && count <= totalCount - offset;
}

bool PlanElementRangeValid(int elementOffset, int elementCount, size_t elementTotal)
{
    return elementOffset >= 0 &&
        elementCount > 0 &&
        static_cast<size_t>(elementOffset) <= elementTotal &&
        static_cast<size_t>(elementCount) <= elementTotal - static_cast<size_t>(elementOffset);
}

void CopyIdentityTransform(float transform[16])
{
    for (int index = 0; index < 16; ++index)
    {
        transform[index] = 0.0f;
    }
    transform[0] = 1.0f;
    transform[5] = 1.0f;
    transform[10] = 1.0f;
    transform[15] = 1.0f;
}

RtSmokePlanStaticBlasSignatureDesc MakeSignatureDescFromSnapshot(
    const RtSmokeStaticBlasSignatureSnapshot& snapshot)
{
    RtSmokePlanStaticBlasSignatureDesc desc;
    desc.vertices = snapshot.vertexBytes.empty() ? nullptr : snapshot.vertexBytes.data();
    desc.vertexStride = snapshot.vertexStride;
    desc.totalVertexCount = snapshot.totalVertexCount;
    desc.indexes = snapshot.indexes.empty() ? nullptr : snapshot.indexes.data();
    desc.totalIndexCount = static_cast<int>(snapshot.indexes.size());
    desc.triangleClasses = snapshot.triangleClasses.empty() ? nullptr : snapshot.triangleClasses.data();
    desc.triangleMaterials = snapshot.triangleMaterials.empty() ? nullptr : snapshot.triangleMaterials.data();
    desc.totalTriangleCount = static_cast<int>(
        snapshot.triangleClasses.size() < snapshot.triangleMaterials.size()
            ? snapshot.triangleClasses.size()
            : snapshot.triangleMaterials.size());
    desc.staticRange = snapshot.staticRange;
    desc.sceneOrigin = snapshot.sceneOrigin;
    return desc;
}

RtSmokeRigidTlasPlanDesc MakeRigidTlasPlanDescFromSnapshot(
    const RtSmokeRigidTlasPlanSnapshot& snapshot)
{
    RtSmokeRigidTlasPlanDesc desc;
    desc.observations = snapshot.observations.empty() ? nullptr : snapshot.observations.data();
    desc.observationCount = static_cast<int>(snapshot.observations.size());
    desc.rigidSourceMask = snapshot.rigidSourceMask;
    desc.firstInstanceId = snapshot.firstInstanceId;
    desc.instanceMask = snapshot.instanceMask;
    desc.maxInstances = snapshot.maxInstances;
    return desc;
}

} // namespace

uint64_t HashSmokePlanBytes(uint64_t hash, const void* data, size_t size)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < size; ++index)
    {
        hash ^= static_cast<uint64_t>(bytes[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

RtSmokeStaticBlasSignatureSnapshot CaptureSmokeStaticBlasSignatureSnapshot(
    const RtSmokePlanStaticBlasSignatureDesc& desc)
{
    RtSmokeStaticBlasSignatureSnapshot snapshot;
    snapshot.vertexStride = desc.vertexStride;
    snapshot.totalVertexCount = desc.totalVertexCount;
    snapshot.staticRange = desc.staticRange;
    snapshot.sceneOrigin = desc.sceneOrigin;

    if (desc.vertices && desc.vertexStride > 0 && desc.totalVertexCount > 0)
    {
        const uint8_t* vertexBytes = static_cast<const uint8_t*>(desc.vertices);
        snapshot.vertexBytes.assign(
            vertexBytes,
            vertexBytes + static_cast<size_t>(desc.totalVertexCount) * desc.vertexStride);
    }
    if (desc.indexes && desc.totalIndexCount > 0)
    {
        snapshot.indexes.assign(desc.indexes, desc.indexes + desc.totalIndexCount);
    }
    if (desc.triangleClasses && desc.totalTriangleCount > 0)
    {
        snapshot.triangleClasses.assign(desc.triangleClasses, desc.triangleClasses + desc.totalTriangleCount);
    }
    if (desc.triangleMaterials && desc.totalTriangleCount > 0)
    {
        snapshot.triangleMaterials.assign(desc.triangleMaterials, desc.triangleMaterials + desc.totalTriangleCount);
    }
    return snapshot;
}

RtSmokeAccelerationPlanSnapshot CaptureSmokeAccelerationPlanSnapshot(
    const RtSmokeAccelerationPlanInput& input)
{
    RtSmokeAccelerationPlanSnapshot snapshot;
    snapshot.staticSignature = CaptureSmokeStaticBlasSignatureSnapshot(input.staticSignature);
    snapshot.staticCache = input.staticCache;
    snapshot.staticVertexCount = input.staticVertexCount;
    snapshot.staticIndexCount = input.staticIndexCount;
    snapshot.dynamicVertexCount = input.dynamicVertexCount;
    snapshot.dynamicIndexCount = input.dynamicIndexCount;
    return snapshot;
}

uint64_t BuildSmokeAccelerationPlanInputToken(const RtSmokeAccelerationPlanInput& input)
{
    uint64_t hash = 1469598103934665603ull;
    const uint32_t cacheBits =
        (input.staticCache.hasStaticBlas ? 1u : 0u) |
        (input.staticCache.cacheValid ? 2u : 0u) |
        (input.staticCache.cacheResourcesReady ? 4u : 0u) |
        (input.staticCache.staticCacheChanged ? 8u : 0u);
    hash = HashSmokePlanBytes(hash, &cacheBits, sizeof(cacheBits));
    hash = HashSmokePlanBytes(hash, &input.staticCache.previousSignatureHash, sizeof(input.staticCache.previousSignatureHash));
    hash = HashSmokePlanBytes(hash, &input.staticSignature.vertexStride, sizeof(input.staticSignature.vertexStride));
    hash = HashSmokePlanBytes(hash, &input.staticSignature.totalVertexCount, sizeof(input.staticSignature.totalVertexCount));
    hash = HashSmokePlanBytes(hash, &input.staticSignature.totalIndexCount, sizeof(input.staticSignature.totalIndexCount));
    hash = HashSmokePlanBytes(hash, &input.staticSignature.totalTriangleCount, sizeof(input.staticSignature.totalTriangleCount));
    hash = HashSmokePlanBytes(hash, &input.staticSignature.staticRange, sizeof(input.staticSignature.staticRange));
    hash = HashSmokePlanBytes(hash, &input.staticSignature.sceneOrigin, sizeof(input.staticSignature.sceneOrigin));
    hash = HashSmokePlanBytes(hash, &input.staticVertexCount, sizeof(input.staticVertexCount));
    hash = HashSmokePlanBytes(hash, &input.staticIndexCount, sizeof(input.staticIndexCount));
    hash = HashSmokePlanBytes(hash, &input.dynamicVertexCount, sizeof(input.dynamicVertexCount));
    hash = HashSmokePlanBytes(hash, &input.dynamicIndexCount, sizeof(input.dynamicIndexCount));
    return hash;
}

RtSmokePlanStaticBlasSignature ComputeSmokeStaticBlasSignaturePlan(
    const RtSmokePlanStaticBlasSignatureDesc& desc)
{
    RtSmokePlanStaticBlasSignature signature;
    signature.vertexCount = desc.staticRange.vertexCount;
    signature.indexCount = desc.staticRange.indexCount;
    signature.triangleCount = desc.staticRange.triangleCount;

    uint64_t hash = 14695981039346656037ull;
    hash = HashSmokePlanBytes(hash, &desc.sceneOrigin.x, sizeof(desc.sceneOrigin.x));
    hash = HashSmokePlanBytes(hash, &desc.sceneOrigin.y, sizeof(desc.sceneOrigin.y));
    hash = HashSmokePlanBytes(hash, &desc.sceneOrigin.z, sizeof(desc.sceneOrigin.z));
    hash = HashSmokePlanBytes(hash, &signature.vertexCount, sizeof(signature.vertexCount));
    hash = HashSmokePlanBytes(hash, &signature.indexCount, sizeof(signature.indexCount));
    hash = HashSmokePlanBytes(hash, &signature.triangleCount, sizeof(signature.triangleCount));

    if (desc.vertices && desc.vertexStride > 0 &&
        PlanRangeValid(desc.staticRange.vertexOffset, desc.staticRange.vertexCount, desc.totalVertexCount))
    {
        const uint8_t* vertexBytes = static_cast<const uint8_t*>(desc.vertices) +
            static_cast<size_t>(desc.staticRange.vertexOffset) * desc.vertexStride;
        hash = HashSmokePlanBytes(hash, vertexBytes, static_cast<size_t>(desc.staticRange.vertexCount) * desc.vertexStride);
    }

    if (desc.indexes &&
        PlanRangeValid(desc.staticRange.indexOffset, desc.staticRange.indexCount, desc.totalIndexCount))
    {
        hash = HashSmokePlanBytes(
            hash,
            desc.indexes + desc.staticRange.indexOffset,
            static_cast<size_t>(desc.staticRange.indexCount) * sizeof(desc.indexes[0]));
    }

    if (desc.triangleClasses && desc.triangleMaterials &&
        PlanRangeValid(desc.staticRange.triangleOffset, desc.staticRange.triangleCount, desc.totalTriangleCount))
    {
        hash = HashSmokePlanBytes(
            hash,
            desc.triangleClasses + desc.staticRange.triangleOffset,
            static_cast<size_t>(desc.staticRange.triangleCount) * sizeof(desc.triangleClasses[0]));
        hash = HashSmokePlanBytes(
            hash,
            desc.triangleMaterials + desc.staticRange.triangleOffset,
            static_cast<size_t>(desc.staticRange.triangleCount) * sizeof(desc.triangleMaterials[0]));
    }

    signature.hash = hash;
    return signature;
}

RtSmokeAccelerationPlan BuildSmokeAccelerationPlan(const RtSmokeAccelerationPlanInput& input)
{
    RtSmokeAccelerationPlan plan;
    plan.hasStaticBlas = input.staticIndexCount > 0;
    plan.hasDynamicBlas = input.dynamicIndexCount > 0;

    if (plan.hasStaticBlas &&
        !input.staticCache.staticCacheChanged &&
        input.staticCache.cacheValid &&
        input.staticCache.previousSignatureHash != 0)
    {
        plan.staticSignature.vertexCount = input.staticSignature.staticRange.vertexCount;
        plan.staticSignature.indexCount = input.staticSignature.staticRange.indexCount;
        plan.staticSignature.triangleCount = input.staticSignature.staticRange.triangleCount;
        plan.staticSignature.hash = input.staticCache.previousSignatureHash;
        plan.staticSignatureReused = true;
    }
    else
    {
        plan.staticSignature = ComputeSmokeStaticBlasSignaturePlan(input.staticSignature);
    }

    plan.staticCacheHit =
        plan.hasStaticBlas &&
        input.staticCache.cacheValid &&
        input.staticCache.cacheResourcesReady &&
        !input.staticCache.staticCacheChanged &&
        input.staticCache.previousSignatureHash != 0 &&
        input.staticCache.previousSignatureHash == plan.staticSignature.hash;

    plan.staticBlas.enabled = plan.hasStaticBlas;
    plan.staticBlas.cacheHit = plan.staticCacheHit;
    plan.staticBlas.vertexCount = input.staticVertexCount;
    plan.staticBlas.indexCount = input.staticIndexCount;
    plan.staticBlas.debugName = "PathTraceSmokeStaticWorldBLAS";

    plan.dynamicBlas.enabled = plan.hasDynamicBlas;
    plan.dynamicBlas.cacheHit = false;
    plan.dynamicBlas.vertexCount = input.dynamicVertexCount;
    plan.dynamicBlas.indexCount = input.dynamicIndexCount;
    plan.dynamicBlas.debugName = "PathTraceSmokeDynamicCandidateBLAS";
    return plan;
}

RtSmokeAccelerationPlanResult BuildSmokeAccelerationPlanResult(
    const RtSmokeAccelerationPlanSnapshot& snapshot)
{
    RtSmokeAccelerationPlanInput input;
    input.staticSignature = MakeSignatureDescFromSnapshot(snapshot.staticSignature);
    input.staticCache = snapshot.staticCache;
    input.staticVertexCount = snapshot.staticVertexCount;
    input.staticIndexCount = snapshot.staticIndexCount;
    input.dynamicVertexCount = snapshot.dynamicVertexCount;
    input.dynamicIndexCount = snapshot.dynamicIndexCount;

    RtSmokeAccelerationPlanResult result;
    result.plan = BuildSmokeAccelerationPlan(input);
    result.valid = result.plan.hasStaticBlas || result.plan.hasDynamicBlas;
    return result;
}

RtSmokeBaseTlasPlan BuildSmokeBaseTlasPlan(bool hasStaticBlas, bool hasDynamicBlas)
{
    RtSmokeBaseTlasPlan plan;
    if (hasStaticBlas)
    {
        RtSmokePlanTlasInstance instance;
        instance.kind = RT_SMOKE_PLAN_TLAS_STATIC_BLAS;
        instance.instanceId = 0;
        instance.instanceMask = 0x01;
        CopyIdentityTransform(instance.transform);
        plan.instances[plan.instanceCount++] = instance;
    }
    if (hasDynamicBlas)
    {
        RtSmokePlanTlasInstance instance;
        instance.kind = RT_SMOKE_PLAN_TLAS_DYNAMIC_BLAS;
        instance.instanceId = 1;
        instance.instanceMask = 0x01;
        CopyIdentityTransform(instance.transform);
        plan.instances[plan.instanceCount++] = instance;
    }
    return plan;
}

RtSmokeAccelerationSubmitPlan BuildSmokeAccelerationSubmitPlan(
    const RtSmokeAccelerationSubmitPlanInput& input)
{
    RtSmokeAccelerationSubmitPlan plan;
    plan.buildStaticBlas = input.hasStaticBlas && !input.staticBlasCacheHit;
    plan.buildDynamicBlas = input.hasDynamicBlas;
    plan.submitTlas = input.hasStaticBlas || input.hasDynamicBlas;
    plan.baseTlasPlan = BuildSmokeBaseTlasPlan(input.hasStaticBlas, input.hasDynamicBlas);
    return plan;
}

bool AppendSmokeRigidTlasPlanObservation(
    RtSmokeRigidTlasPlan& plan,
    const RtSmokeRigidTlasPlanDesc& desc,
    const RtSmokeRigidTlasObservation& observation)
{
    if (desc.maxInstances > 0 && plan.emittedInstances >= desc.maxInstances)
    {
        return false;
    }

    ++plan.visibleInstances;
    if ((observation.sourceFlags & desc.rigidSourceMask) == 0)
    {
        ++plan.rejectedNonRigid;
        return true;
    }

    ++plan.rigidInstances;
    if (!observation.hasMeshRecord)
    {
        ++plan.rejectedMissingMesh;
        return true;
    }
    if (!observation.meshSeenThisFrame && !observation.residencyEnabled)
    {
        ++plan.rejectedStaleMesh;
        return true;
    }
    if (!observation.hasBlas)
    {
        ++plan.rejectedMissingBlas;
        return true;
    }

    RtSmokePlanTlasInstance instance;
    instance.kind = RT_SMOKE_PLAN_TLAS_RIGID_BLAS;
    instance.instanceId = desc.firstInstanceId + static_cast<uint32_t>(plan.emittedInstances);
    instance.instanceMask = desc.instanceMask;
    instance.meshHash = observation.meshHash;
    instance.sourceInstanceId = observation.instanceId;
    instance.routeRecordIndex = observation.routeRecordIndex;
    std::memcpy(instance.transform, observation.objectToWorld, sizeof(instance.transform));
    plan.instances.push_back(instance);
    ++plan.emittedInstances;
    return true;
}

RtSmokeRigidTlasPlanSnapshot CaptureSmokeRigidTlasPlanSnapshot(
    const RtSmokeRigidTlasPlanDesc& desc)
{
    RtSmokeRigidTlasPlanSnapshot snapshot;
    snapshot.rigidSourceMask = desc.rigidSourceMask;
    snapshot.firstInstanceId = desc.firstInstanceId;
    snapshot.instanceMask = desc.instanceMask;
    snapshot.maxInstances = desc.maxInstances;
    if (desc.observations && desc.observationCount > 0)
    {
        snapshot.observations.assign(desc.observations, desc.observations + desc.observationCount);
    }
    return snapshot;
}

RtSmokeRigidTlasPlan BuildSmokeRigidTlasPlan(const RtSmokeRigidTlasPlanDesc& desc)
{
    RtSmokeRigidTlasPlan plan;
    if (!desc.observations || desc.observationCount <= 0)
    {
        return plan;
    }

    const int reserveCount = desc.maxInstances > 0 && desc.maxInstances < desc.observationCount
        ? desc.maxInstances
        : desc.observationCount;
    plan.instances.reserve(reserveCount);
    for (int observationIndex = 0; observationIndex < desc.observationCount; ++observationIndex)
    {
        if (!AppendSmokeRigidTlasPlanObservation(plan, desc, desc.observations[observationIndex]))
        {
            break;
        }
    }

    return plan;
}

RtSmokeRigidTlasPlan BuildSmokeRigidTlasPlan(
    const RtSmokeRigidTlasPlanSnapshot& snapshot)
{
    return BuildSmokeRigidTlasPlan(MakeRigidTlasPlanDescFromSnapshot(snapshot));
}

RtSmokeUploadPlanMetadata BuildSmokeVectorUploadPlanMetadata(
    size_t elementCount,
    size_t elementSize,
    bool skip,
    int dirtyElementOffset,
    int dirtyElementCount)
{
    RtSmokeUploadPlanMetadata metadata;
    metadata.skip = skip;
    if (metadata.skip)
    {
        return metadata;
    }

    size_t uploadElementOffset = 0;
    size_t uploadElementCount = elementCount;
    if (PlanElementRangeValid(dirtyElementOffset, dirtyElementCount, elementCount))
    {
        uploadElementOffset = static_cast<size_t>(dirtyElementOffset);
        uploadElementCount = static_cast<size_t>(dirtyElementCount);
    }

    metadata.byteSize = uploadElementCount * elementSize;
    metadata.sourceOffsetBytes = uploadElementOffset * elementSize;
    metadata.destOffsetBytes = static_cast<uint64_t>(metadata.sourceOffsetBytes);
    return metadata;
}

RtSmokeStaticDirtyUploadPlan BuildSmokeStaticDirtyUploadPlan(
    const RtSmokeStaticDirtyUploadPlanInput& input)
{
    RtSmokeStaticDirtyUploadPlan plan;
    plan.dirtyRangesValid =
        PlanElementRangeValid(input.dirtyVertexOffset, input.dirtyVertexCount, input.totalVertexCount) &&
        PlanElementRangeValid(input.dirtyIndexOffset, input.dirtyIndexCount, input.totalIndexCount) &&
        PlanElementRangeValid(input.dirtyTriangleOffset, input.dirtyTriangleCount, input.totalTriangleClassCount) &&
        PlanElementRangeValid(input.dirtyTriangleOffset, input.dirtyTriangleCount, input.totalTriangleMaterialCount);
    plan.useDirtyRangeUploads =
        !input.staticBlasCacheHit &&
        input.staticCacheChanged &&
        input.staticDirtyCount > 0 &&
        input.staticGeometryBuffersReused &&
        plan.dirtyRangesValid;
    return plan;
}

uint64_t BuildSmokePlanDataSpanSignature(
    const RtSmokePlanDataSpan* spans,
    int spanCount)
{
    uint64_t hash = 14695981039346656037ull;
    if (!spans || spanCount <= 0)
    {
        return hash;
    }

    for (int spanIndex = 0; spanIndex < spanCount; ++spanIndex)
    {
        const RtSmokePlanDataSpan& span = spans[spanIndex];
        const uint64_t count = static_cast<uint64_t>(span.elementCount);
        hash = HashSmokePlanBytes(hash, &count, sizeof(count));
        if (span.data && span.elementCount > 0 && span.elementSize > 0)
        {
            hash = HashSmokePlanBytes(hash, span.data, span.elementCount * span.elementSize);
        }
    }
    return hash;
}

RtSmokePreviousStaticSnapshotUploadPlan BuildSmokePreviousStaticSnapshotUploadPlan(
    const RtSmokePreviousStaticSnapshotUploadPlanInput& input)
{
    RtSmokePreviousStaticSnapshotUploadPlan plan;
    plan.skipUpload =
        input.dataAvailable &&
        input.buffersReused &&
        input.previousUploadSignature != 0 &&
        input.previousUploadSignature == input.currentUploadSignature;
    return plan;
}
