#include "PathTraceAccelerationPlan.h"

#include <chrono>
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

RtSmokeAccelerationPlanTimedResult BuildSmokeAccelerationPlanTimedResult(
    const RtSmokeAccelerationPlanSnapshot& snapshot)
{
    const auto start = std::chrono::steady_clock::now();
    RtSmokeAccelerationPlanTimedResult timedResult;
    timedResult.result = BuildSmokeAccelerationPlanResult(snapshot);
    const auto end = std::chrono::steady_clock::now();
    timedResult.workerExecutionMs = std::chrono::duration<double, std::milli>(end - start).count();
    return timedResult;
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

RtSmokeStaticTlasActiveSetPlan BuildSmokeStaticTlasActiveSetPlan(
    const RtSmokeStaticTlasActiveSetPlanDesc& desc)
{
    RtSmokeStaticTlasActiveSetPlan plan;
    plan.monolithicStaticBlas = desc.monolithicStaticBlas;
    plan.activeSetSignature = 1469598103934665603ull;
    plan.residentSetSignature = 1469598103934665603ull;
    if (!desc.buckets || desc.bucketCount <= 0)
    {
        return plan;
    }

    plan.instances.reserve(desc.monolithicStaticBlas ? 1 : desc.bucketCount);
    bool monolithicInstanceEmitted = false;
    for (int bucketIndex = 0; bucketIndex < desc.bucketCount; ++bucketIndex)
    {
        const RtSmokeStaticTlasBucketObservation& bucket = desc.buckets[bucketIndex];
        if (!bucket.resident)
        {
            continue;
        }

        plan.residentSetSignature = HashSmokePlanBytes(plan.residentSetSignature, &bucket.bucketKey, sizeof(bucket.bucketKey));
        plan.residentSetSignature = HashSmokePlanBytes(plan.residentSetSignature, &bucket.residentSurfaceCount, sizeof(bucket.residentSurfaceCount));
        plan.residentSetSignature = HashSmokePlanBytes(plan.residentSetSignature, &bucket.residentVertexCount, sizeof(bucket.residentVertexCount));
        plan.residentSetSignature = HashSmokePlanBytes(plan.residentSetSignature, &bucket.residentIndexCount, sizeof(bucket.residentIndexCount));
        plan.residentSetSignature = HashSmokePlanBytes(plan.residentSetSignature, &bucket.residentTriangleCount, sizeof(bucket.residentTriangleCount));
        ++plan.residentBuckets;
        plan.residentSurfaceCount += bucket.residentSurfaceCount;
        plan.residentVertexCount += bucket.residentVertexCount;
        plan.residentIndexCount += bucket.residentIndexCount;
        plan.residentTriangleCount += bucket.residentTriangleCount;
        if (!bucket.active)
        {
            ++plan.inactiveResidentBuckets;
            continue;
        }

        plan.activeSetSignature = HashSmokePlanBytes(plan.activeSetSignature, &bucket.bucketKey, sizeof(bucket.bucketKey));
        plan.activeSetSignature = HashSmokePlanBytes(plan.activeSetSignature, &bucket.activeReasonFlags, sizeof(bucket.activeReasonFlags));
        plan.activeSetSignature = HashSmokePlanBytes(plan.activeSetSignature, &bucket.activeSurfaceCount, sizeof(bucket.activeSurfaceCount));
        plan.activeSetSignature = HashSmokePlanBytes(plan.activeSetSignature, &bucket.activeVertexCount, sizeof(bucket.activeVertexCount));
        plan.activeSetSignature = HashSmokePlanBytes(plan.activeSetSignature, &bucket.activeIndexCount, sizeof(bucket.activeIndexCount));
        plan.activeSetSignature = HashSmokePlanBytes(plan.activeSetSignature, &bucket.activeTriangleCount, sizeof(bucket.activeTriangleCount));
        ++plan.activeBuckets;
        plan.activeSurfaceCount += bucket.activeSurfaceCount;
        plan.activeVertexCount += bucket.activeVertexCount;
        plan.activeIndexCount += bucket.activeIndexCount;
        plan.activeTriangleCount += bucket.activeTriangleCount;
        if (!bucket.hasBlas || !desc.hasStaticBlas)
        {
            continue;
        }

        if (desc.monolithicStaticBlas)
        {
            if (!monolithicInstanceEmitted)
            {
                RtSmokePlanTlasInstance instance;
                instance.kind = RT_SMOKE_PLAN_TLAS_STATIC_BLAS;
                instance.instanceId = desc.firstInstanceId;
                instance.instanceMask = desc.instanceMask;
                CopyIdentityTransform(instance.transform);
                plan.instances.push_back(instance);
                ++plan.emittedInstances;
                monolithicInstanceEmitted = true;
            }
            continue;
        }

        RtSmokePlanTlasInstance instance;
        instance.kind = RT_SMOKE_PLAN_TLAS_STATIC_BUCKET_BLAS;
        instance.instanceId = desc.firstInstanceId + static_cast<uint32_t>(plan.emittedInstances);
        instance.instanceMask = desc.instanceMask;
        instance.meshHash = bucket.bucketKey;
        instance.routeRecordIndex = bucket.routeRecordIndex;
        instance.flags = bucket.activeReasonFlags;
        CopyIdentityTransform(instance.transform);
        plan.instances.push_back(instance);
        ++plan.emittedInstances;
    }

    plan.inactiveResidentGeometryIncluded =
        desc.monolithicStaticBlas &&
        plan.emittedInstances > 0 &&
        (plan.inactiveResidentBuckets > 0 ||
            plan.activeSurfaceCount < plan.residentSurfaceCount ||
            plan.activeVertexCount < plan.residentVertexCount ||
            plan.activeIndexCount < plan.residentIndexCount ||
            plan.activeTriangleCount < plan.residentTriangleCount);
    plan.requiresBucketedStaticBlas = plan.inactiveResidentGeometryIncluded;
    return plan;
}

bool BuildSmokeStaticTlasBucketObservation(
    const RtSmokeStaticTlasBucketObservationInput& input,
    RtSmokeStaticTlasBucketObservation& observation)
{
    observation = RtSmokeStaticTlasBucketObservation();
    if (!input.valid || input.vertexCount <= 0 || input.indexCount <= 0 || input.triangleCount <= 0)
    {
        return false;
    }

    observation.bucketKey = input.bucketKey;
    observation.resident = true;
    observation.active = input.seenThisFrame;
    observation.hasBlas = input.hasBlas;
    observation.activeReasonFlags = input.seenThisFrame ? input.activeReasonFlags : 0u;
    observation.routeRecordIndex = input.routeRecordIndex;
    observation.residentVertexOffset = input.vertexOffset;
    observation.residentIndexOffset = input.indexOffset;
    observation.residentTriangleOffset = input.triangleOffset;
    observation.residentSurfaceCount = 1;
    observation.residentVertexCount = input.vertexCount;
    observation.residentIndexCount = input.indexCount;
    observation.residentTriangleCount = input.triangleCount;
    if (input.seenThisFrame)
    {
        observation.activeSurfaceCount = 1;
        observation.activeVertexCount = input.vertexCount;
        observation.activeIndexCount = input.indexCount;
        observation.activeTriangleCount = input.triangleCount;
    }
    return true;
}

RtSmokeStaticBucketBlasPlan BuildSmokeStaticBucketBlasPlan(
    const RtSmokeStaticBucketBlasPlanDesc& desc)
{
    RtSmokeStaticBucketBlasPlan plan;
    plan.planSignature = 14695981039346656037ull;
    if (!desc.buckets || desc.bucketCount <= 0)
    {
        return plan;
    }

    const int reserveCount = desc.maxRecords > 0 && desc.maxRecords < desc.bucketCount
        ? desc.maxRecords
        : desc.bucketCount;
    plan.records.reserve(reserveCount);
    for (int bucketIndex = 0; bucketIndex < desc.bucketCount; ++bucketIndex)
    {
        const RtSmokeStaticTlasBucketObservation& bucket = desc.buckets[bucketIndex];
        if (!bucket.resident)
        {
            ++plan.skippedInvalid;
            continue;
        }

        ++plan.residentBuckets;
        if (bucket.active)
        {
            ++plan.activeBuckets;
        }
        else if (desc.activeOnly)
        {
            ++plan.skippedInactive;
            continue;
        }

        if (bucket.residentVertexOffset < 0 ||
            bucket.residentIndexOffset < 0 ||
            bucket.residentTriangleOffset < 0 ||
            bucket.residentVertexCount <= 0 ||
            bucket.residentIndexCount <= 0 ||
            bucket.residentTriangleCount <= 0)
        {
            ++plan.skippedInvalid;
            continue;
        }
        if (desc.maxRecords > 0 && plan.emittedRecords >= desc.maxRecords)
        {
            plan.overflow = true;
            break;
        }

        RtSmokeStaticBucketBlasRecord record;
        record.bucketKey = bucket.bucketKey;
        record.routeRecordIndex = bucket.routeRecordIndex;
        record.activeReasonFlags = bucket.activeReasonFlags;
        record.active = bucket.active;
        record.range.vertexOffset = bucket.residentVertexOffset;
        record.range.vertexCount = bucket.residentVertexCount;
        record.range.indexOffset = bucket.residentIndexOffset;
        record.range.indexCount = bucket.residentIndexCount;
        record.range.triangleOffset = bucket.residentTriangleOffset;
        record.range.triangleCount = bucket.residentTriangleCount;
        plan.records.push_back(record);
        ++plan.emittedRecords;

        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &record.bucketKey, sizeof(record.bucketKey));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &record.routeRecordIndex, sizeof(record.routeRecordIndex));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &record.activeReasonFlags, sizeof(record.activeReasonFlags));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &record.range, sizeof(record.range));
    }
    return plan;
}

RtSmokeStaticBucketTraversalCompatibility BuildSmokeStaticBucketTraversalCompatibility(
    const RtSmokeStaticBucketTraversalCompatibilityInput& input)
{
    RtSmokeStaticBucketTraversalCompatibility plan;
    plan.recordCount = input.recordCount;
    if (!input.records || input.recordCount <= 0)
    {
        return plan;
    }

    for (int recordIndex = 0; recordIndex < input.recordCount; ++recordIndex)
    {
        const RtSmokeStaticBucketBlasRecord& record = input.records[recordIndex];
        if (record.range.vertexOffset != 0 ||
            record.range.indexOffset != 0 ||
            record.range.triangleOffset != 0)
        {
            ++plan.nonZeroOffsetRecords;
        }
    }

    const RtSmokeStaticBucketBlasRecord& firstRecord = input.records[0];
    plan.exactMonolithicRecord =
        input.recordCount == 1 &&
        firstRecord.range.vertexOffset == 0 &&
        firstRecord.range.indexOffset == 0 &&
        firstRecord.range.triangleOffset == 0 &&
        firstRecord.range.vertexCount == input.totalVertexCount &&
        firstRecord.range.indexCount == input.totalIndexCount &&
        firstRecord.range.triangleCount == input.totalTriangleCount;
    plan.currentStaticShaderCompatible =
        input.shaderSupportsStaticBucketRoutes ||
        plan.exactMonolithicRecord;
    plan.requiresShaderRouteMetadata =
        !input.shaderSupportsStaticBucketRoutes &&
        !plan.exactMonolithicRecord;
    return plan;
}

RtSmokeRouteInstanceNamespacePlan BuildSmokeRouteInstanceNamespacePlan(
    const RtSmokeRouteInstanceNamespacePlanInput& input)
{
    RtSmokeRouteInstanceNamespacePlan plan;
    const int staticRouteCount = input.staticRouteRecordCount > 0 ? input.staticRouteRecordCount : 0;
    const int rigidRouteCount = input.rigidRouteRecordCount > 0 ? input.rigidRouteRecordCount : 0;
    plan.staticFirstInstanceId = input.firstRouteInstanceId;
    plan.rigidFirstInstanceId = input.firstRouteInstanceId;
    plan.rigidRouteInstanceCount = rigidRouteCount;

    const bool staticRoutesRequested = input.enableStaticRoutes && staticRouteCount > 0;
    plan.staticRoutesRequireShaderSupport =
        staticRoutesRequested &&
        !input.shaderSupportsStaticBucketRoutes;
    plan.staticRoutesBlocked = plan.staticRoutesRequireShaderSupport;
    plan.staticRoutesEnabled = staticRoutesRequested && !plan.staticRoutesBlocked;
    if (plan.staticRoutesEnabled)
    {
        plan.staticRouteInstanceCount = staticRouteCount;
        plan.rigidFirstInstanceId = input.firstRouteInstanceId + static_cast<uint32_t>(staticRouteCount);
    }
    else
    {
        plan.staticFirstInstanceId = 0;
    }
    plan.rigidRouteBaseShifted = plan.rigidFirstInstanceId != input.firstRouteInstanceId;
    return plan;
}

RtSmokeStaticRouteTablePlan BuildSmokeStaticRouteTablePlan(
    const RtSmokeStaticRouteTablePlanInput& input)
{
    RtSmokeStaticRouteTablePlan plan;
    plan.tableSignature = 14695981039346656037ull;
    plan.inputRecords = input.recordCount > 0 ? input.recordCount : 0;
    plan.blocked = input.routeNamespace.staticRoutesBlocked;
    if (!input.records || input.recordCount <= 0)
    {
        return plan;
    }
    if (!input.routeNamespace.staticRoutesEnabled)
    {
        plan.skippedDisabled = input.recordCount;
        return plan;
    }

    const int reserveCount = input.maxRecords > 0 && input.maxRecords < input.recordCount
        ? input.maxRecords
        : input.recordCount;
    plan.records.reserve(reserveCount);
    for (int recordIndex = 0; recordIndex < input.recordCount; ++recordIndex)
    {
        if (input.maxRecords > 0 && plan.emittedRecords >= input.maxRecords)
        {
            plan.overflow = true;
            break;
        }

        const RtSmokeStaticBucketBlasRecord& sourceRecord = input.records[recordIndex];
        if (sourceRecord.range.vertexOffset < 0 ||
            sourceRecord.range.vertexCount <= 0 ||
            sourceRecord.range.indexOffset < 0 ||
            sourceRecord.range.indexCount <= 0 ||
            sourceRecord.range.triangleOffset < 0 ||
            sourceRecord.range.triangleCount <= 0)
        {
            ++plan.skippedInvalid;
            continue;
        }

        RtSmokeStaticRouteTableRecord routeRecord;
        routeRecord.bucketKey = sourceRecord.bucketKey;
        routeRecord.instanceId =
            input.routeNamespace.staticFirstInstanceId + static_cast<uint32_t>(plan.emittedRecords);
        routeRecord.routeRecordIndex = sourceRecord.routeRecordIndex;
        routeRecord.activeReasonFlags = sourceRecord.activeReasonFlags;
        routeRecord.range = sourceRecord.range;
        plan.records.push_back(routeRecord);
        ++plan.emittedRecords;

        plan.tableSignature = HashSmokePlanBytes(plan.tableSignature, &routeRecord.bucketKey, sizeof(routeRecord.bucketKey));
        plan.tableSignature = HashSmokePlanBytes(plan.tableSignature, &routeRecord.instanceId, sizeof(routeRecord.instanceId));
        plan.tableSignature = HashSmokePlanBytes(plan.tableSignature, &routeRecord.routeRecordIndex, sizeof(routeRecord.routeRecordIndex));
        plan.tableSignature = HashSmokePlanBytes(plan.tableSignature, &routeRecord.activeReasonFlags, sizeof(routeRecord.activeReasonFlags));
        plan.tableSignature = HashSmokePlanBytes(plan.tableSignature, &routeRecord.range, sizeof(routeRecord.range));
    }
    return plan;
}

RtSmokeStaticBucketBlasBuildPlan BuildSmokeStaticBucketBlasBuildPlan(
    const RtSmokeStaticBucketBlasBuildPlanInput& input)
{
    RtSmokeStaticBucketBlasBuildPlan plan;
    if (!input.submitBuilds)
    {
        plan.skipBuild = true;
        return plan;
    }

    plan.signatureChanged =
        !input.signatureValid ||
        input.previousBlasInputSignature != input.currentBlasInputSignature;
    plan.createBlas = !input.hasBlas || !input.blasInputsCompatible;
    plan.submitBuild =
        input.forceRebuild ||
        input.uploadRequired ||
        plan.createBlas ||
        plan.signatureChanged;
    plan.skipBuild = !plan.submitBuild;
    return plan;
}

RtSmokeStaticBucketBlasBuildBatchPlan BuildSmokeStaticBucketBlasBuildBatchPlan(
    const RtSmokeStaticBucketBlasBuildBatchPlanInput& input)
{
    RtSmokeStaticBucketBlasBuildBatchPlan plan;
    plan.planSignature = 14695981039346656037ull;
    plan.inputRecords = input.observationCount > 0 ? input.observationCount : 0;
    if (!input.observations || input.observationCount <= 0)
    {
        return plan;
    }

    const int reserveCount = input.maxRecords > 0 && input.maxRecords < input.observationCount
        ? input.maxRecords
        : input.observationCount;
    plan.records.reserve(reserveCount);
    for (int observationIndex = 0; observationIndex < input.observationCount; ++observationIndex)
    {
        if (input.maxRecords > 0 && plan.emittedRecords >= input.maxRecords)
        {
            plan.overflow = true;
            break;
        }

        const RtSmokeStaticBucketBlasBuildObservation& observation = input.observations[observationIndex];
        RtSmokeStaticBucketBlasBuildPlanInput buildInput;
        buildInput.submitBuilds = input.submitBuilds;
        buildInput.forceRebuild = input.forceRebuild;
        buildInput.hasBlas = observation.hasBlas;
        buildInput.uploadRequired = observation.uploadRequired;
        buildInput.blasInputsCompatible = observation.blasInputsCompatible;
        buildInput.signatureValid = observation.signatureValid;
        buildInput.previousBlasInputSignature = observation.previousBlasInputSignature;
        buildInput.currentBlasInputSignature = observation.currentBlasInputSignature;
        const RtSmokeStaticBucketBlasBuildPlan buildPlan =
            BuildSmokeStaticBucketBlasBuildPlan(buildInput);

        RtSmokeStaticBucketBlasBuildBatchRecord record;
        record.bucketKey = observation.bucketKey;
        record.buildPlan = buildPlan;
        plan.records.push_back(record);
        ++plan.emittedRecords;

        if (buildPlan.createBlas)
        {
            ++plan.createBlasRecords;
        }
        if (buildPlan.submitBuild)
        {
            ++plan.submitBuildRecords;
        }
        if (buildPlan.skipBuild)
        {
            ++plan.skippedBuildRecords;
        }
        if (buildPlan.signatureChanged)
        {
            ++plan.signatureChangedRecords;
        }
        if (observation.uploadRequired)
        {
            ++plan.uploadRequiredRecords;
        }
        if (!observation.blasInputsCompatible)
        {
            ++plan.incompatibleRecords;
        }
        if (!observation.hasBlas)
        {
            ++plan.missingBlasRecords;
        }

        const uint32_t createBit = buildPlan.createBlas ? 1u : 0u;
        const uint32_t submitBit = buildPlan.submitBuild ? 1u : 0u;
        const uint32_t skipBit = buildPlan.skipBuild ? 1u : 0u;
        const uint32_t signatureChangedBit = buildPlan.signatureChanged ? 1u : 0u;
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &record.bucketKey, sizeof(record.bucketKey));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &createBit, sizeof(createBit));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &submitBit, sizeof(submitBit));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &skipBit, sizeof(skipBit));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &signatureChangedBit, sizeof(signatureChangedBit));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &observation.previousBlasInputSignature, sizeof(observation.previousBlasInputSignature));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &observation.currentBlasInputSignature, sizeof(observation.currentBlasInputSignature));
    }
    return plan;
}

RtSmokeStaticBvhBucketSignature BuildSmokeStaticBvhBucketSignature(
    const RtSmokeStaticBvhBucketSignatureInput& input)
{
    RtSmokeStaticBvhBucketSignature signature;
    const RtSmokeStaticTlasBucketObservation& bucket = input.bucket;
    signature.bucketKey = bucket.bucketKey;
    signature.resident = bucket.resident;
    signature.active = bucket.active;

    uint64_t residentHash = 14695981039346656037ull;
    const uint32_t residentBit = bucket.resident ? 1u : 0u;
    residentHash = HashSmokePlanBytes(residentHash, &bucket.bucketKey, sizeof(bucket.bucketKey));
    residentHash = HashSmokePlanBytes(residentHash, &residentBit, sizeof(residentBit));
    residentHash = HashSmokePlanBytes(residentHash, &bucket.residentSurfaceCount, sizeof(bucket.residentSurfaceCount));
    residentHash = HashSmokePlanBytes(residentHash, &bucket.residentVertexCount, sizeof(bucket.residentVertexCount));
    residentHash = HashSmokePlanBytes(residentHash, &bucket.residentIndexCount, sizeof(bucket.residentIndexCount));
    residentHash = HashSmokePlanBytes(residentHash, &bucket.residentTriangleCount, sizeof(bucket.residentTriangleCount));
    signature.residentSignature = residentHash;

    uint64_t activeHash = 14695981039346656037ull;
    const uint32_t activeBit = bucket.active ? 1u : 0u;
    activeHash = HashSmokePlanBytes(activeHash, &bucket.bucketKey, sizeof(bucket.bucketKey));
    activeHash = HashSmokePlanBytes(activeHash, &activeBit, sizeof(activeBit));
    activeHash = HashSmokePlanBytes(activeHash, &bucket.activeReasonFlags, sizeof(bucket.activeReasonFlags));
    activeHash = HashSmokePlanBytes(activeHash, &bucket.routeRecordIndex, sizeof(bucket.routeRecordIndex));
    activeHash = HashSmokePlanBytes(activeHash, &bucket.activeSurfaceCount, sizeof(bucket.activeSurfaceCount));
    activeHash = HashSmokePlanBytes(activeHash, &bucket.activeVertexCount, sizeof(bucket.activeVertexCount));
    activeHash = HashSmokePlanBytes(activeHash, &bucket.activeIndexCount, sizeof(bucket.activeIndexCount));
    activeHash = HashSmokePlanBytes(activeHash, &bucket.activeTriangleCount, sizeof(bucket.activeTriangleCount));
    signature.activeSignature = activeHash;

    uint64_t blasHash = 14695981039346656037ull;
    blasHash = HashSmokePlanBytes(blasHash, &bucket.bucketKey, sizeof(bucket.bucketKey));
    blasHash = HashSmokePlanBytes(blasHash, &input.geometryContentSignature, sizeof(input.geometryContentSignature));
    blasHash = HashSmokePlanBytes(blasHash, &input.materialGeneration, sizeof(input.materialGeneration));
    blasHash = HashSmokePlanBytes(blasHash, &bucket.residentVertexCount, sizeof(bucket.residentVertexCount));
    blasHash = HashSmokePlanBytes(blasHash, &bucket.residentIndexCount, sizeof(bucket.residentIndexCount));
    blasHash = HashSmokePlanBytes(blasHash, &bucket.residentTriangleCount, sizeof(bucket.residentTriangleCount));
    signature.blasInputSignature = blasHash;
    return signature;
}

RtSmokeStaticBucketBlasBuildObservationPlan BuildSmokeStaticBucketBlasBuildObservationPlan(
    const RtSmokeStaticBucketBlasBuildObservationPlanInput& input)
{
    RtSmokeStaticBucketBlasBuildObservationPlan plan;
    plan.planSignature = 14695981039346656037ull;
    plan.inputBuckets = input.currentBucketCount > 0 ? input.currentBucketCount : 0;
    if (!input.currentBuckets || input.currentBucketCount <= 0)
    {
        return plan;
    }

    const int reserveCount = input.maxRecords > 0 && input.maxRecords < input.currentBucketCount
        ? input.maxRecords
        : input.currentBucketCount;
    plan.observations.reserve(reserveCount);
    for (int bucketIndex = 0; bucketIndex < input.currentBucketCount; ++bucketIndex)
    {
        const RtSmokeStaticBvhBucketSignature& currentBucket = input.currentBuckets[bucketIndex];
        if (!currentBucket.active)
        {
            ++plan.skippedInactive;
            continue;
        }
        if (input.maxRecords > 0 && plan.emittedObservations >= input.maxRecords)
        {
            plan.overflow = true;
            break;
        }

        const RtSmokeStaticBucketBlasCacheState* previousBucket = nullptr;
        if (input.previousBuckets && input.previousBucketCount > 0)
        {
            for (int previousIndex = 0; previousIndex < input.previousBucketCount; ++previousIndex)
            {
                if (input.previousBuckets[previousIndex].bucketKey == currentBucket.bucketKey)
                {
                    previousBucket = &input.previousBuckets[previousIndex];
                    break;
                }
            }
        }

        RtSmokeStaticBucketBlasBuildObservation observation;
        observation.bucketKey = currentBucket.bucketKey;
        observation.currentBlasInputSignature = currentBucket.blasInputSignature;
        if (previousBucket)
        {
            observation.hasBlas = previousBucket->hasBlas;
            observation.blasInputsCompatible = previousBucket->blasInputsCompatible;
            observation.signatureValid = true;
            observation.previousBlasInputSignature = previousBucket->blasInputSignature;
        }

        const bool changedSignature =
            !previousBucket ||
            previousBucket->blasInputSignature != currentBucket.blasInputSignature;
        observation.uploadRequired =
            changedSignature ||
            !observation.hasBlas;
        plan.observations.push_back(observation);
        ++plan.emittedObservations;

        const bool cacheHit =
            previousBucket &&
            observation.hasBlas &&
            observation.blasInputsCompatible &&
            !changedSignature;
        if (cacheHit)
        {
            ++plan.cacheHits;
        }
        else
        {
            ++plan.cacheMisses;
        }
        if (changedSignature)
        {
            ++plan.signatureChanged;
        }
        if (observation.uploadRequired)
        {
            ++plan.uploadRequired;
        }

        const uint32_t hasPreviousBit = previousBucket ? 1u : 0u;
        const uint32_t uploadBit = observation.uploadRequired ? 1u : 0u;
        const uint32_t hitBit = cacheHit ? 1u : 0u;
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &observation.bucketKey, sizeof(observation.bucketKey));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &hasPreviousBit, sizeof(hasPreviousBit));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &hitBit, sizeof(hitBit));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &uploadBit, sizeof(uploadBit));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &observation.previousBlasInputSignature, sizeof(observation.previousBlasInputSignature));
        plan.planSignature = HashSmokePlanBytes(plan.planSignature, &observation.currentBlasInputSignature, sizeof(observation.currentBlasInputSignature));
    }
    return plan;
}

RtSmokeBvhDirtyPlan BuildSmokeBvhDirtyPlan(
    const RtSmokeBvhDirtyPlanInput& input)
{
    RtSmokeBvhDirtyPlan plan;
    if (!input.previousValid)
    {
        plan.geometryContentChanged = true;
        plan.materialChanged = true;
        plan.activeMembershipChanged = true;
        plan.tlasInstanceChanged = true;
    }
    else
    {
        plan.geometryContentChanged =
            input.previous.geometryContentSignature != input.current.geometryContentSignature;
        plan.materialChanged =
            input.previous.materialGeneration != input.current.materialGeneration;
        plan.activeMembershipChanged =
            input.previous.activeSetSignature != input.current.activeSetSignature;
        plan.tlasInstanceChanged =
            input.previous.tlasInstanceSignature != input.current.tlasInstanceSignature;
    }

    plan.blasInputDirty = plan.geometryContentChanged || plan.materialChanged;
    plan.tlasDirty = plan.blasInputDirty || plan.activeMembershipChanged || plan.tlasInstanceChanged;
    return plan;
}

RtSmokeBvhFrameToken BuildSmokeBvhFrameToken(
    const RtSmokeBvhFrameTokenInput& input)
{
    RtSmokeBvhFrameToken token;

    uint64_t geometryContentSignature = 14695981039346656037ull;
    geometryContentSignature = HashSmokePlanBytes(geometryContentSignature, &input.staticBlasSignature, sizeof(input.staticBlasSignature));
    geometryContentSignature = HashSmokePlanBytes(geometryContentSignature, &input.geometryGeneration, sizeof(input.geometryGeneration));
    geometryContentSignature = HashSmokePlanBytes(geometryContentSignature, &input.dynamicVertexCount, sizeof(input.dynamicVertexCount));
    geometryContentSignature = HashSmokePlanBytes(geometryContentSignature, &input.dynamicIndexCount, sizeof(input.dynamicIndexCount));
    geometryContentSignature = HashSmokePlanBytes(geometryContentSignature, &input.rigidRouteVertexCount, sizeof(input.rigidRouteVertexCount));
    geometryContentSignature = HashSmokePlanBytes(geometryContentSignature, &input.rigidRouteIndexCount, sizeof(input.rigidRouteIndexCount));
    geometryContentSignature = HashSmokePlanBytes(geometryContentSignature, &input.rigidRouteTriangleCount, sizeof(input.rigidRouteTriangleCount));

    uint64_t activeSetSignature = input.staticActiveSetSignature;
    activeSetSignature = HashSmokePlanBytes(activeSetSignature, &input.rigidRouteInstanceCount, sizeof(input.rigidRouteInstanceCount));
    activeSetSignature = HashSmokePlanBytes(activeSetSignature, &input.rigidRouteSeenThisFrameCount, sizeof(input.rigidRouteSeenThisFrameCount));
    activeSetSignature = HashSmokePlanBytes(activeSetSignature, &input.rigidRouteCachedInstanceCount, sizeof(input.rigidRouteCachedInstanceCount));

    uint64_t tlasInstanceSignature = 14695981039346656037ull;
    const uint32_t hasStaticBlasBit = input.hasStaticBlas ? 1u : 0u;
    const uint32_t hasDynamicBlasBit = input.hasDynamicBlas ? 1u : 0u;
    tlasInstanceSignature = HashSmokePlanBytes(tlasInstanceSignature, &activeSetSignature, sizeof(activeSetSignature));
    tlasInstanceSignature = HashSmokePlanBytes(tlasInstanceSignature, &hasStaticBlasBit, sizeof(hasStaticBlasBit));
    tlasInstanceSignature = HashSmokePlanBytes(tlasInstanceSignature, &hasDynamicBlasBit, sizeof(hasDynamicBlasBit));
    tlasInstanceSignature = HashSmokePlanBytes(tlasInstanceSignature, &input.baseTlasInstanceCount, sizeof(input.baseTlasInstanceCount));
    tlasInstanceSignature = HashSmokePlanBytes(tlasInstanceSignature, &input.rigidTlasInstanceCount, sizeof(input.rigidTlasInstanceCount));

    token.dirtyToken.geometryContentSignature = geometryContentSignature;
    token.dirtyToken.materialGeneration = input.materialGeneration;
    token.dirtyToken.activeSetSignature = activeSetSignature;
    token.dirtyToken.tlasInstanceSignature = tlasInstanceSignature;
    token.residentSetSignature = input.staticResidentSetSignature;
    return token;
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

RtSmokeRigidBlasBuildPlan BuildSmokeRigidBlasBuildPlan(
    const RtSmokeRigidBlasBuildPlanInput& input)
{
    RtSmokeRigidBlasBuildPlan plan;
    if (!input.submitBuilds)
    {
        plan.skipBuild = true;
        return plan;
    }

    plan.createBlas = !input.hasBlas || !input.blasInputsCompatible;
    plan.submitBuild =
        input.forceRebuild ||
        input.uploadRequired ||
        plan.createBlas;
    plan.skipBuild = !plan.submitBuild;
    return plan;
}

RtSmokeRigidBvhObjectSignature BuildSmokeRigidBvhObjectSignature(
    const RtSmokeRigidBvhObjectSignatureInput& input)
{
    RtSmokeRigidBvhObjectSignature signature;
    signature.resident = input.hasMeshRecord && (input.meshSeenThisFrame || input.residencyEnabled);
    signature.activeCandidate = signature.resident && ((input.sourceFlags & input.rigidSourceMask) != 0);

    uint64_t objectHash = 14695981039346656037ull;
    objectHash = HashSmokePlanBytes(objectHash, &input.meshHash, sizeof(input.meshHash));
    objectHash = HashSmokePlanBytes(objectHash, &input.instanceId, sizeof(input.instanceId));
    signature.objectKey = objectHash;

    uint64_t blasHash = 14695981039346656037ull;
    blasHash = HashSmokePlanBytes(blasHash, &input.meshHash, sizeof(input.meshHash));
    blasHash = HashSmokePlanBytes(blasHash, &input.geometryContentSignature, sizeof(input.geometryContentSignature));
    blasHash = HashSmokePlanBytes(blasHash, &input.materialGeneration, sizeof(input.materialGeneration));
    blasHash = HashSmokePlanBytes(blasHash, &input.vertexCount, sizeof(input.vertexCount));
    blasHash = HashSmokePlanBytes(blasHash, &input.indexCount, sizeof(input.indexCount));
    signature.blasInputSignature = blasHash;

    uint64_t tlasHash = 14695981039346656037ull;
    const uint32_t residentBit = signature.resident ? 1u : 0u;
    const uint32_t activeBit = signature.activeCandidate ? 1u : 0u;
    tlasHash = HashSmokePlanBytes(tlasHash, &signature.objectKey, sizeof(signature.objectKey));
    tlasHash = HashSmokePlanBytes(tlasHash, &input.sourceFlags, sizeof(input.sourceFlags));
    tlasHash = HashSmokePlanBytes(tlasHash, &input.routeRecordIndex, sizeof(input.routeRecordIndex));
    tlasHash = HashSmokePlanBytes(tlasHash, &residentBit, sizeof(residentBit));
    tlasHash = HashSmokePlanBytes(tlasHash, &activeBit, sizeof(activeBit));
    signature.tlasMembershipSignature = tlasHash;
    return signature;
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
