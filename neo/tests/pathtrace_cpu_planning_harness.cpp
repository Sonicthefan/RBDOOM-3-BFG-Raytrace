#include "../renderer/NVRHI/PathTraceAccelerationPlan.h"
#include "../renderer/NVRHI/PathTraceCpuWork.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct HarnessSmokeVertex
{
    float position[4];
    float normal[4];
    float texCoord[4];
    float color[4];
    float color2[4];
};

int g_failures = 0;

void Check(bool condition, const char* name)
{
    if (condition)
    {
        std::cout << "[PASS] " << name << "\n";
    }
    else
    {
        std::cout << "[FAIL] " << name << "\n";
        ++g_failures;
    }
}

std::vector<HarnessSmokeVertex> BuildTriangleVertices(float materialMarker)
{
    std::vector<HarnessSmokeVertex> vertices(3);
    vertices[0].position[0] = 0.0f;
    vertices[0].position[1] = 0.0f;
    vertices[0].position[2] = 0.0f;
    vertices[1].position[0] = 1.0f;
    vertices[1].position[1] = 0.0f;
    vertices[1].position[2] = 0.0f;
    vertices[2].position[0] = 0.0f;
    vertices[2].position[1] = 1.0f;
    vertices[2].position[2] = 0.0f;
    for (HarnessSmokeVertex& vertex : vertices)
    {
        vertex.position[3] = 1.0f;
        vertex.normal[2] = 1.0f;
        vertex.texCoord[0] = materialMarker;
        vertex.color[0] = 1.0f;
        vertex.color[1] = 1.0f;
        vertex.color[2] = 1.0f;
        vertex.color[3] = 1.0f;
    }
    return vertices;
}

RtSmokePlanStaticBlasSignatureDesc BuildSignatureDesc(
    const std::vector<HarnessSmokeVertex>& vertices,
    const std::vector<uint32_t>& indexes,
    const std::vector<uint32_t>& classes,
    const std::vector<uint32_t>& materials)
{
    RtSmokePlanStaticBlasSignatureDesc desc;
    desc.vertices = vertices.data();
    desc.vertexStride = sizeof(vertices[0]);
    desc.totalVertexCount = static_cast<int>(vertices.size());
    desc.indexes = indexes.data();
    desc.totalIndexCount = static_cast<int>(indexes.size());
    desc.triangleClasses = classes.data();
    desc.triangleMaterials = materials.data();
    desc.totalTriangleCount = static_cast<int>(classes.size());
    desc.staticRange.vertexCount = static_cast<int>(vertices.size());
    desc.staticRange.indexCount = static_cast<int>(indexes.size());
    desc.staticRange.triangleCount = static_cast<int>(classes.size());
    return desc;
}

RtSmokeAccelerationPlan BuildPlan(uint64_t previousHash, bool cacheValid, bool resourcesReady, bool cacheChanged)
{
    std::vector<HarnessSmokeVertex> vertices = BuildTriangleVertices(0.25f);
    std::vector<uint32_t> indexes = { 0, 1, 2 };
    std::vector<uint32_t> classes = { 7 };
    std::vector<uint32_t> materials = { 11 };

    RtSmokeAccelerationPlanInput input;
    input.staticSignature = BuildSignatureDesc(vertices, indexes, classes, materials);
    input.staticCache.cacheValid = cacheValid;
    input.staticCache.cacheResourcesReady = resourcesReady;
    input.staticCache.staticCacheChanged = cacheChanged;
    input.staticCache.previousSignatureHash = previousHash;
    input.staticVertexCount = static_cast<int>(vertices.size());
    input.staticIndexCount = static_cast<int>(indexes.size());
    input.dynamicVertexCount = 6;
    input.dynamicIndexCount = 6;
    return BuildSmokeAccelerationPlan(input);
}

RtSmokeAccelerationPlanInput BuildPlanInput(
    const std::vector<HarnessSmokeVertex>& vertices,
    const std::vector<uint32_t>& indexes,
    const std::vector<uint32_t>& classes,
    const std::vector<uint32_t>& materials)
{
    RtSmokeAccelerationPlanInput input;
    input.staticSignature = BuildSignatureDesc(vertices, indexes, classes, materials);
    input.staticCache.staticCacheChanged = true;
    input.staticVertexCount = static_cast<int>(vertices.size());
    input.staticIndexCount = static_cast<int>(indexes.size());
    input.dynamicVertexCount = 6;
    input.dynamicIndexCount = 6;
    return input;
}

void TestStaticSignature()
{
    std::vector<HarnessSmokeVertex> vertices = BuildTriangleVertices(0.25f);
    std::vector<uint32_t> indexes = { 0, 1, 2 };
    std::vector<uint32_t> classes = { 7 };
    std::vector<uint32_t> materials = { 11 };

    const RtSmokePlanStaticBlasSignature a =
        ComputeSmokeStaticBlasSignaturePlan(BuildSignatureDesc(vertices, indexes, classes, materials));
    const RtSmokePlanStaticBlasSignature b =
        ComputeSmokeStaticBlasSignaturePlan(BuildSignatureDesc(vertices, indexes, classes, materials));
    Check(a.hash == b.hash, "identical static inputs produce identical BLAS signatures");

    vertices[1].position[0] = 2.0f;
    const RtSmokePlanStaticBlasSignature vertexChanged =
        ComputeSmokeStaticBlasSignaturePlan(BuildSignatureDesc(vertices, indexes, classes, materials));
    Check(a.hash != vertexChanged.hash, "vertex changes alter the BLAS signature");

    vertices = BuildTriangleVertices(0.25f);
    indexes[2] = 1;
    const RtSmokePlanStaticBlasSignature indexChanged =
        ComputeSmokeStaticBlasSignaturePlan(BuildSignatureDesc(vertices, indexes, classes, materials));
    Check(a.hash != indexChanged.hash, "index changes alter the BLAS signature");

    indexes = { 0, 1, 2 };
    classes[0] = 9;
    const RtSmokePlanStaticBlasSignature classChanged =
        ComputeSmokeStaticBlasSignaturePlan(BuildSignatureDesc(vertices, indexes, classes, materials));
    Check(a.hash != classChanged.hash, "class changes alter the BLAS signature");

    classes[0] = 7;
    materials[0] = 12;
    const RtSmokePlanStaticBlasSignature materialChanged =
        ComputeSmokeStaticBlasSignaturePlan(BuildSignatureDesc(vertices, indexes, classes, materials));
    Check(a.hash != materialChanged.hash, "material changes alter the BLAS signature");
}

void TestStaticSignatureRanges()
{
    std::vector<HarnessSmokeVertex> vertices(6);
    for (int vertexIndex = 0; vertexIndex < static_cast<int>(vertices.size()); ++vertexIndex)
    {
        vertices[vertexIndex].position[0] = static_cast<float>(vertexIndex);
        vertices[vertexIndex].position[1] = static_cast<float>(vertexIndex * 2);
        vertices[vertexIndex].position[2] = static_cast<float>(vertexIndex * 3);
        vertices[vertexIndex].position[3] = 1.0f;
        vertices[vertexIndex].normal[2] = 1.0f;
    }
    std::vector<uint32_t> indexes = { 0, 1, 2, 3, 4, 5 };
    std::vector<uint32_t> classes = { 7, 8 };
    std::vector<uint32_t> materials = { 11, 12 };

    RtSmokePlanStaticBlasSignatureDesc rangedDesc =
        BuildSignatureDesc(vertices, indexes, classes, materials);
    rangedDesc.staticRange.vertexOffset = 3;
    rangedDesc.staticRange.vertexCount = 3;
    rangedDesc.staticRange.indexOffset = 3;
    rangedDesc.staticRange.indexCount = 3;
    rangedDesc.staticRange.triangleOffset = 1;
    rangedDesc.staticRange.triangleCount = 1;
    const RtSmokePlanStaticBlasSignature rangedSignature =
        ComputeSmokeStaticBlasSignaturePlan(rangedDesc);

    std::vector<HarnessSmokeVertex> outsideChangedVertices = vertices;
    outsideChangedVertices[0].position[0] = 99.0f;
    RtSmokePlanStaticBlasSignatureDesc outsideChangedDesc =
        BuildSignatureDesc(outsideChangedVertices, indexes, classes, materials);
    outsideChangedDesc.staticRange = rangedDesc.staticRange;
    const RtSmokePlanStaticBlasSignature outsideChangedSignature =
        ComputeSmokeStaticBlasSignaturePlan(outsideChangedDesc);
    Check(rangedSignature.hash == outsideChangedSignature.hash, "static BLAS ranged signature ignores vertices outside range");

    std::vector<HarnessSmokeVertex> insideChangedVertices = vertices;
    insideChangedVertices[4].position[0] = 99.0f;
    RtSmokePlanStaticBlasSignatureDesc insideChangedDesc =
        BuildSignatureDesc(insideChangedVertices, indexes, classes, materials);
    insideChangedDesc.staticRange = rangedDesc.staticRange;
    const RtSmokePlanStaticBlasSignature insideChangedSignature =
        ComputeSmokeStaticBlasSignaturePlan(insideChangedDesc);
    Check(rangedSignature.hash != insideChangedSignature.hash, "static BLAS ranged signature includes vertices inside range");

    std::vector<uint32_t> outsideChangedMaterials = materials;
    outsideChangedMaterials[0] = 99;
    RtSmokePlanStaticBlasSignatureDesc outsideMaterialChangedDesc =
        BuildSignatureDesc(vertices, indexes, classes, outsideChangedMaterials);
    outsideMaterialChangedDesc.staticRange = rangedDesc.staticRange;
    const RtSmokePlanStaticBlasSignature outsideMaterialChangedSignature =
        ComputeSmokeStaticBlasSignaturePlan(outsideMaterialChangedDesc);
    Check(rangedSignature.hash == outsideMaterialChangedSignature.hash, "static BLAS ranged signature ignores triangle metadata outside range");

    rangedDesc.sceneOrigin.x = 1.0f;
    const RtSmokePlanStaticBlasSignature originChangedSignature =
        ComputeSmokeStaticBlasSignaturePlan(rangedDesc);
    Check(rangedSignature.hash != originChangedSignature.hash, "static BLAS signature includes scene origin");
}

void TestCacheAndBaseTlas()
{
    const RtSmokeAccelerationPlan coldPlan = BuildPlan(0, false, false, false);
    const RtSmokeAccelerationPlan hitPlan = BuildPlan(coldPlan.staticSignature.hash, true, true, false);
    const RtSmokeAccelerationPlan changedPlan = BuildPlan(coldPlan.staticSignature.hash, true, true, true);
    Check(!coldPlan.staticCacheHit, "static cache miss when no previous cache exists");
    Check(hitPlan.staticCacheHit, "static cache hit matches unchanged signature and ready resources");
    Check(!changedPlan.staticCacheHit, "static cache miss when static cache changed");

    const RtSmokeBaseTlasPlan tlasPlan = BuildSmokeBaseTlasPlan(true, true);
    Check(tlasPlan.instanceCount == 2, "TLAS plan includes base static and dynamic instances");
    Check(tlasPlan.instances[0].instanceId == 0 && tlasPlan.instances[1].instanceId == 1, "TLAS base instance IDs are deterministic");

    const RtSmokeBaseTlasPlan dynamicOnlyTlasPlan = BuildSmokeBaseTlasPlan(false, true);
    Check(dynamicOnlyTlasPlan.instanceCount == 1 && dynamicOnlyTlasPlan.instances[0].instanceId == 1, "TLAS plan supports dynamic-only scenes");

    RtSmokeAccelerationSubmitPlanInput submitPlanInput;
    submitPlanInput.hasStaticBlas = true;
    submitPlanInput.hasDynamicBlas = true;
    const RtSmokeAccelerationSubmitPlan fullSubmitPlan = BuildSmokeAccelerationSubmitPlan(submitPlanInput);
    Check(fullSubmitPlan.buildStaticBlas && fullSubmitPlan.buildDynamicBlas && fullSubmitPlan.submitTlas, "acceleration submit plan builds uncached static and dynamic BLAS");
    Check(fullSubmitPlan.baseTlasPlan.instanceCount == 2, "acceleration submit plan carries base TLAS instances");

    submitPlanInput.staticBlasCacheHit = true;
    const RtSmokeAccelerationSubmitPlan cachedSubmitPlan = BuildSmokeAccelerationSubmitPlan(submitPlanInput);
    Check(!cachedSubmitPlan.buildStaticBlas && cachedSubmitPlan.buildDynamicBlas && cachedSubmitPlan.submitTlas, "acceleration submit plan skips cached static BLAS build");

    submitPlanInput.hasStaticBlas = false;
    submitPlanInput.hasDynamicBlas = false;
    const RtSmokeAccelerationSubmitPlan emptySubmitPlan = BuildSmokeAccelerationSubmitPlan(submitPlanInput);
    Check(!emptySubmitPlan.buildStaticBlas && !emptySubmitPlan.buildDynamicBlas && !emptySubmitPlan.submitTlas, "acceleration submit plan rejects empty BLAS set");

    RtSmokeAccelerationPlanInput emptyInput;
    const RtSmokeAccelerationPlan emptyPlan = BuildSmokeAccelerationPlan(emptyInput);
    Check(!emptyPlan.hasStaticBlas && !emptyPlan.hasDynamicBlas, "acceleration plan handles empty BLAS ranges");

    RtSmokeAccelerationPlanInput reusedInput;
    reusedInput.staticSignature.staticRange.vertexCount = 3;
    reusedInput.staticSignature.staticRange.indexCount = 3;
    reusedInput.staticSignature.staticRange.triangleCount = 1;
    reusedInput.staticCache.cacheValid = true;
    reusedInput.staticCache.cacheResourcesReady = true;
    reusedInput.staticCache.previousSignatureHash = 0x12345678ull;
    reusedInput.staticVertexCount = 3;
    reusedInput.staticIndexCount = 3;
    const RtSmokeAccelerationPlan reusedPlan = BuildSmokeAccelerationPlan(reusedInput);
    Check(reusedPlan.staticSignatureReused && reusedPlan.staticSignature.hash == 0x12345678ull, "static cache reuse avoids source-array signature work");
    Check(reusedPlan.staticCacheHit, "static cache reuse can hit with previous signature and ready resources");
}

void TestOwnedSnapshot()
{
    std::vector<HarnessSmokeVertex> vertices = BuildTriangleVertices(0.25f);
    std::vector<uint32_t> indexes = { 0, 1, 2 };
    std::vector<uint32_t> classes = { 7 };
    std::vector<uint32_t> materials = { 11 };

    RtSmokeAccelerationPlanInput input = BuildPlanInput(vertices, indexes, classes, materials);
    const RtSmokeAccelerationPlan directPlan = BuildSmokeAccelerationPlan(input);
    const RtSmokeAccelerationPlanSnapshot snapshot = CaptureSmokeAccelerationPlanSnapshot(input);

    vertices[1].position[0] = 99.0f;
    indexes[2] = 0;
    classes[0] = 99;
    materials[0] = 99;

    const RtSmokeAccelerationPlanResult snapshotResult = BuildSmokeAccelerationPlanResult(snapshot);
    Check(snapshotResult.valid, "owned acceleration snapshot produces a valid result");
    Check(snapshotResult.plan.staticSignature.hash == directPlan.staticSignature.hash, "owned acceleration snapshot is immutable after source mutation");
    Check(snapshotResult.plan.staticBlas.vertexCount == 3 && snapshotResult.plan.dynamicBlas.indexCount == 6, "owned acceleration snapshot preserves BLAS plan counts");
}

void TestAccelerationPlanInputToken()
{
    std::vector<HarnessSmokeVertex> vertices = BuildTriangleVertices(0.25f);
    std::vector<uint32_t> indexes = { 0, 1, 2 };
    std::vector<uint32_t> classes = { 7 };
    std::vector<uint32_t> materials = { 11 };

    RtSmokeAccelerationPlanInput input = BuildPlanInput(vertices, indexes, classes, materials);
    input.staticCache.cacheValid = true;
    input.staticCache.cacheResourcesReady = true;
    input.staticCache.previousSignatureHash = 0x1234ull;
    const uint64_t baseToken = BuildSmokeAccelerationPlanInputToken(input);
    Check(baseToken == BuildSmokeAccelerationPlanInputToken(input), "acceleration plan input token is deterministic");

    RtSmokeAccelerationPlanInput cacheChangedInput = input;
    cacheChangedInput.staticCache.cacheResourcesReady = false;
    Check(baseToken != BuildSmokeAccelerationPlanInputToken(cacheChangedInput), "acceleration plan input token tracks cache readiness");

    RtSmokeAccelerationPlanInput rangeChangedInput = input;
    rangeChangedInput.staticSignature.staticRange.indexCount = 0;
    Check(baseToken != BuildSmokeAccelerationPlanInputToken(rangeChangedInput), "acceleration plan input token tracks BLAS ranges");

    RtSmokeAccelerationPlanInput dynamicChangedInput = input;
    dynamicChangedInput.dynamicIndexCount = 0;
    Check(baseToken != BuildSmokeAccelerationPlanInputToken(dynamicChangedInput), "acceleration plan input token tracks dynamic BLAS counts");
}

void TestAsyncSnapshotPlanning()
{
    std::vector<HarnessSmokeVertex> vertices = BuildTriangleVertices(0.25f);
    std::vector<uint32_t> indexes = { 0, 1, 2 };
    std::vector<uint32_t> classes = { 7 };
    std::vector<uint32_t> materials = { 11 };

    RtSmokeAccelerationPlanInput input = BuildPlanInput(vertices, indexes, classes, materials);
    const RtSmokeAccelerationPlan directPlan = BuildSmokeAccelerationPlan(input);
    const RtSmokeAccelerationPlanSnapshot snapshot = CaptureSmokeAccelerationPlanSnapshot(input);

    RtPathTraceCpuWorkState state;
    RtPathTraceCpuWorkGeneration generation;
    generation.sceneGeneration = 1;
    generation.geometryGeneration = 2;
    generation.materialGeneration = 3;
    RtPathTraceCpuWorkPublishSnapshot(state, generation);

    std::future<RtSmokeAccelerationPlanTimedResult> future = std::async(
        std::launch::async,
        [snapshot]() {
            return BuildSmokeAccelerationPlanTimedResult(snapshot);
        });

    vertices[2].position[0] = 42.0f;
    const RtPathTraceCpuWorkFrameDecision lateDecision =
        RtPathTraceCpuWorkAcceptLatest(state, generation, nullptr, true);
    Check(lateDecision.lateFallback && lateDecision.syncFallback, "late async acceleration plan requests synchronous fallback");

    RtPathTraceCpuWorkResultEnvelope envelope;
    const RtSmokeAccelerationPlanTimedResult timedResult = future.get();
    const RtSmokeAccelerationPlanResult& asyncResult = timedResult.result;
    envelope.completed = asyncResult.valid;
    envelope.generation = generation;
    envelope.timing.workerExecutionMs = timedResult.workerExecutionMs;
    RtPathTraceCpuWorkPublishCompletedResult(state, envelope);
    const RtPathTraceCpuWorkFrameDecision acceptDecision =
        RtPathTraceCpuWorkAcceptLatest(state, generation, nullptr, true);
    Check(acceptDecision.accepted, "matching async acceleration plan generation is accepted");
    Check(asyncResult.plan.staticSignature.hash == directPlan.staticSignature.hash, "async acceleration plan uses owned immutable snapshot data");
    Check(timedResult.workerExecutionMs >= 0.0, "timed acceleration worker result records execution time");

    RtPathTraceCpuWorkGeneration staleGeneration = generation;
    staleGeneration.geometryGeneration = 99;
    RtPathTraceCpuWorkResultEnvelope staleEnvelope = envelope;
    staleEnvelope.generation = staleGeneration;
    const RtPathTraceCpuWorkFrameDecision staleDecision =
        RtPathTraceCpuWorkAcceptLatest(state, generation, &staleEnvelope, true);
    Check(staleDecision.staleRejected && staleDecision.syncFallback, "stale async acceleration plan generation is rejected");
}

void TestRigidPlan()
{
    RtSmokeRigidTlasObservation observations[4];
    observations[0].meshHash = 100;
    observations[0].instanceId = 10;
    observations[0].sourceFlags = 0x2;
    observations[0].hasMeshRecord = true;
    observations[0].meshSeenThisFrame = true;
    observations[0].hasBlas = true;
    observations[0].objectToWorld[0] = 1.0f;
    observations[0].objectToWorld[5] = 1.0f;
    observations[0].objectToWorld[10] = 1.0f;
    observations[0].objectToWorld[15] = 1.0f;
    observations[0].previousObjectToWorld[0] = 1.0f;
    observations[0].previousObjectToWorld[5] = 1.0f;
    observations[0].previousObjectToWorld[10] = 1.0f;
    observations[0].previousObjectToWorld[15] = 1.0f;
    observations[0].previousObjectToWorld[12] = -2.0f;
    observations[0].hasPreviousObjectToWorld = true;
    observations[0].transformContinuous = true;
    observations[0].seenThisFrame = false;

    observations[1] = observations[0];
    observations[1].meshHash = 200;
    observations[1].hasMeshRecord = false;

    observations[2] = observations[0];
    observations[2].meshHash = 300;
    observations[2].meshSeenThisFrame = false;
    observations[2].residencyEnabled = false;

    observations[3] = observations[0];
    observations[3].meshHash = 400;
    observations[3].hasBlas = false;

    RtSmokeRigidTlasPlanDesc desc;
    desc.observations = observations;
    desc.observationCount = 4;
    desc.rigidSourceMask = 0x2;
    desc.firstInstanceId = 2;
    desc.instanceMask = 0x02;

    const RtSmokeRigidTlasPlan plan = BuildSmokeRigidTlasPlan(desc);
    Check(plan.emittedInstances == 1, "rigid TLAS plan accepts valid observations");
    Check(plan.rejectedMissingMesh == 1, "rigid TLAS plan rejects missing mesh records");
    Check(plan.rejectedStaleMesh == 1, "rigid TLAS plan rejects stale observations");
    Check(plan.rejectedMissingBlas == 1, "rigid TLAS plan rejects missing BLAS handles before render submit");
    Check(plan.instances[0].instanceId == 2 &&
        plan.instances[0].meshHash == 100 &&
        plan.instances[0].hasPreviousTransform &&
        plan.instances[0].transformContinuous &&
        !plan.instances[0].sourceSeenThisFrame &&
        plan.instances[0].previousTransform[12] == -2.0f,
        "rigid TLAS emitted instance metadata is deterministic");

    const uint64_t baseRigidToken = BuildSmokeRigidTlasPlanInputToken(desc);
    Check(baseRigidToken == BuildSmokeRigidTlasPlanInputToken(desc),
        "rigid TLAS plan input token is deterministic");
    RtSmokeRigidTlasPlanDesc changedTransformDesc = desc;
    observations[0].previousObjectToWorld[12] = -4.0f;
    Check(baseRigidToken != BuildSmokeRigidTlasPlanInputToken(changedTransformDesc),
        "rigid TLAS plan input token tracks route transform metadata");
    observations[0].previousObjectToWorld[12] = -2.0f;

    const RtSmokeRigidTlasPlanSnapshot snapshot = CaptureSmokeRigidTlasPlanSnapshot(desc);
    const uint64_t snapshotRigidToken = BuildSmokeRigidTlasPlanInputToken(snapshot);
    observations[0].hasBlas = false;
    observations[0].meshHash = 999;
    const RtSmokeRigidTlasPlan snapshotPlan = BuildSmokeRigidTlasPlan(snapshot);
    Check(snapshotPlan.emittedInstances == 1 &&
        snapshotPlan.instances[0].meshHash == 100 &&
        snapshotRigidToken == baseRigidToken &&
        BuildSmokeRigidTlasPlanInputToken(desc) != snapshotRigidToken,
        "owned rigid TLAS snapshot is immutable after source mutation");

    observations[0].hasBlas = true;
    observations[0].meshHash = 100;
    observations[2].residencyEnabled = true;
    observations[2].hasBlas = true;
    const RtSmokeRigidTlasPlan residentPlan = BuildSmokeRigidTlasPlan(desc);
    Check(residentPlan.emittedInstances == 2, "rigid TLAS plan accepts stale mesh when residency keeps it valid");

    desc.maxInstances = 1;
    const RtSmokeRigidTlasPlan cappedPlan = BuildSmokeRigidTlasPlan(desc);
    Check(cappedPlan.emittedInstances == 1 && cappedPlan.instances.size() == 1, "rigid TLAS plan honors max instance cap");

    RtSmokeRigidTlasObservation nonRigid = observations[0];
    nonRigid.sourceFlags = 0;
    RtSmokeRigidTlasPlan streamingPlan;
    RtSmokeRigidTlasPlanDesc streamingDesc;
    streamingDesc.rigidSourceMask = 0x2;
    streamingDesc.firstInstanceId = 8;
    streamingDesc.instanceMask = 0x02;
    Check(AppendSmokeRigidTlasPlanObservation(streamingPlan, streamingDesc, nonRigid), "streaming rigid TLAS planner accepts non-rigid observation without stopping");
    Check(streamingPlan.rejectedNonRigid == 1 && streamingPlan.emittedInstances == 0, "streaming rigid TLAS planner counts non-rigid rejection");
    Check(AppendSmokeRigidTlasPlanObservation(streamingPlan, streamingDesc, observations[0]), "streaming rigid TLAS planner accepts valid observation");
    Check(streamingPlan.emittedInstances == 1 && streamingPlan.instances[0].instanceId == 8, "streaming rigid TLAS planner emits deterministic instance id");
}

void TestAsyncRigidTlasPlanning()
{
    RtSmokeRigidTlasObservation observation;
    observation.meshHash = 500;
    observation.instanceId = 42;
    observation.sourceFlags = 2;
    observation.hasMeshRecord = true;
    observation.meshSeenThisFrame = true;
    observation.hasBlas = true;
    observation.routeRecordIndex = 7;
    observation.seenThisFrame = true;
    for (int transformIndex = 0; transformIndex < 16; ++transformIndex)
    {
        observation.objectToWorld[transformIndex] = 0.0f;
        observation.previousObjectToWorld[transformIndex] = 0.0f;
    }
    observation.objectToWorld[0] = 1.0f;
    observation.objectToWorld[5] = 1.0f;
    observation.objectToWorld[10] = 1.0f;
    observation.objectToWorld[15] = 1.0f;
    observation.previousObjectToWorld[0] = 1.0f;
    observation.previousObjectToWorld[5] = 1.0f;
    observation.previousObjectToWorld[10] = 1.0f;
    observation.previousObjectToWorld[15] = 1.0f;

    RtSmokeRigidTlasPlanDesc desc;
    desc.observations = &observation;
    desc.observationCount = 1;
    desc.rigidSourceMask = 2;
    desc.firstInstanceId = 12;
    desc.instanceMask = 0x02;
    desc.maxInstances = 8;
    const RtSmokeRigidTlasPlanSnapshot snapshot =
        CaptureSmokeRigidTlasPlanSnapshot(desc);

    RtPathTraceCpuWorkState state;
    RtPathTraceCpuWorkGeneration generation;
    generation.frameIndex = 77;
    generation.sceneGeneration = 3;
    generation.geometryGeneration = 900;
    generation.materialGeneration = 901;
    generation.lightGeneration = BuildSmokeRigidTlasPlanInputToken(snapshot);
    RtPathTraceCpuWorkPublishSnapshot(state, generation);

    std::future<RtSmokeRigidTlasPlanTimedResult> future = std::async(
        std::launch::async,
        [snapshot]() {
            return BuildSmokeRigidTlasPlanTimedResult(snapshot);
        });

    const RtPathTraceCpuWorkFrameDecision lateDecision =
        RtPathTraceCpuWorkAcceptLatest(state, generation, nullptr, true);
    Check(lateDecision.lateFallback &&
        lateDecision.syncFallback &&
        state.lateResultCount == 1,
        "late async rigid TLAS work requests synchronous fallback");

    const RtSmokeRigidTlasPlanTimedResult timedResult = future.get();
    RtPathTraceCpuWorkResultEnvelope envelope;
    envelope.completed = true;
    envelope.generation = generation;
    envelope.timing.workerExecutionMs = static_cast<double>(timedResult.planningTimeMicros) / 1000.0;
    RtPathTraceCpuWorkPublishCompletedResult(state, envelope);
    const RtPathTraceCpuWorkFrameDecision acceptDecision =
        RtPathTraceCpuWorkAcceptLatest(state, generation, nullptr, true);
    Check(acceptDecision.accepted &&
        timedResult.plan.emittedInstances == 1 &&
        timedResult.plan.instances[0].instanceId == 12 &&
        state.acceptedResultCount == 1,
        "matching async rigid TLAS generation is accepted");

    RtPathTraceCpuWorkResultEnvelope staleEnvelope = envelope;
    staleEnvelope.generation.lightGeneration ^= 0x1000ull;
    const RtPathTraceCpuWorkFrameDecision staleDecision =
        RtPathTraceCpuWorkAcceptLatest(state, generation, &staleEnvelope, true);
    Check(staleDecision.staleRejected && staleDecision.syncFallback,
        "stale async rigid TLAS generation is rejected");
}

void TestRigidBlasBuildPlan()
{
    RtSmokeRigidBlasBuildPlanInput input;
    input.submitBuilds = true;
    input.hasBlas = true;
    input.blasInputsCompatible = true;
    const RtSmokeRigidBlasBuildPlan unchangedPlan = BuildSmokeRigidBlasBuildPlan(input);
    Check(!unchangedPlan.createBlas && !unchangedPlan.submitBuild && unchangedPlan.skipBuild, "unchanged rigid BLAS skips build submission");

    input.uploadRequired = true;
    const RtSmokeRigidBlasBuildPlan uploadChangedPlan = BuildSmokeRigidBlasBuildPlan(input);
    Check(!uploadChangedPlan.createBlas && uploadChangedPlan.submitBuild && !uploadChangedPlan.skipBuild, "changed rigid BLAS upload requests build submission");

    input.uploadRequired = false;
    input.forceRebuild = true;
    const RtSmokeRigidBlasBuildPlan forcedPlan = BuildSmokeRigidBlasBuildPlan(input);
    Check(!forcedPlan.createBlas && forcedPlan.submitBuild && !forcedPlan.skipBuild, "rigid BLAS force rebuild requests build submission");

    input.forceRebuild = false;
    input.blasInputsCompatible = false;
    const RtSmokeRigidBlasBuildPlan incompatiblePlan = BuildSmokeRigidBlasBuildPlan(input);
    Check(incompatiblePlan.createBlas && incompatiblePlan.submitBuild, "rigid BLAS descriptor incompatibility requests BLAS recreate and build");

    input.hasBlas = false;
    input.blasInputsCompatible = false;
    const RtSmokeRigidBlasBuildPlan missingPlan = BuildSmokeRigidBlasBuildPlan(input);
    Check(missingPlan.createBlas && missingPlan.submitBuild, "missing rigid BLAS requests create and build");

    input.submitBuilds = false;
    const RtSmokeRigidBlasBuildPlan gateOffPlan = BuildSmokeRigidBlasBuildPlan(input);
    Check(!gateOffPlan.createBlas && !gateOffPlan.submitBuild && gateOffPlan.skipBuild, "rigid BLAS build gate skips build submission");
}

void TestStaticBucketBlasBuildPlan()
{
    RtSmokeStaticBucketBlasBuildPlanInput input;
    input.submitBuilds = true;
    input.hasBlas = true;
    input.blasInputsCompatible = true;
    input.signatureValid = true;
    input.previousBlasInputSignature = 100;
    input.currentBlasInputSignature = 100;
    const RtSmokeStaticBucketBlasBuildPlan unchangedPlan =
        BuildSmokeStaticBucketBlasBuildPlan(input);
    Check(!unchangedPlan.createBlas &&
        !unchangedPlan.submitBuild &&
        unchangedPlan.skipBuild &&
        !unchangedPlan.signatureChanged,
        "static bucket BLAS build plan skips unchanged compatible bucket BLAS");

    input.currentBlasInputSignature = 101;
    const RtSmokeStaticBucketBlasBuildPlan changedSignaturePlan =
        BuildSmokeStaticBucketBlasBuildPlan(input);
    Check(!changedSignaturePlan.createBlas &&
        changedSignaturePlan.submitBuild &&
        changedSignaturePlan.signatureChanged,
        "static bucket BLAS build plan rebuilds changed bucket signature");

    input.currentBlasInputSignature = 100;
    input.uploadRequired = true;
    const RtSmokeStaticBucketBlasBuildPlan uploadPlan =
        BuildSmokeStaticBucketBlasBuildPlan(input);
    Check(uploadPlan.submitBuild && !uploadPlan.createBlas, "static bucket BLAS build plan rebuilds after required upload");

    input.uploadRequired = false;
    input.forceRebuild = true;
    const RtSmokeStaticBucketBlasBuildPlan forcedPlan =
        BuildSmokeStaticBucketBlasBuildPlan(input);
    Check(forcedPlan.submitBuild && !forcedPlan.createBlas, "static bucket BLAS build plan honors force rebuild");

    input.forceRebuild = false;
    input.blasInputsCompatible = false;
    const RtSmokeStaticBucketBlasBuildPlan incompatiblePlan =
        BuildSmokeStaticBucketBlasBuildPlan(input);
    Check(incompatiblePlan.createBlas && incompatiblePlan.submitBuild, "static bucket BLAS build plan recreates incompatible BLAS");

    input.hasBlas = false;
    const RtSmokeStaticBucketBlasBuildPlan missingPlan =
        BuildSmokeStaticBucketBlasBuildPlan(input);
    Check(missingPlan.createBlas && missingPlan.submitBuild, "static bucket BLAS build plan creates missing BLAS");

    input.submitBuilds = false;
    const RtSmokeStaticBucketBlasBuildPlan gateOffPlan =
        BuildSmokeStaticBucketBlasBuildPlan(input);
    Check(!gateOffPlan.createBlas &&
        !gateOffPlan.submitBuild &&
        gateOffPlan.skipBuild,
        "static bucket BLAS build gate skips build submission");
}

void TestStaticBucketBlasBuildBatchPlan()
{
    RtSmokeStaticBucketBlasBuildObservation observations[4];
    observations[0].bucketKey = 10;
    observations[0].hasBlas = true;
    observations[0].blasInputsCompatible = true;
    observations[0].signatureValid = true;
    observations[0].previousBlasInputSignature = 100;
    observations[0].currentBlasInputSignature = 100;

    observations[1] = observations[0];
    observations[1].bucketKey = 20;
    observations[1].currentBlasInputSignature = 101;

    observations[2] = observations[0];
    observations[2].bucketKey = 30;
    observations[2].hasBlas = false;

    observations[3] = observations[0];
    observations[3].bucketKey = 40;
    observations[3].uploadRequired = true;
    observations[3].blasInputsCompatible = false;

    RtSmokeStaticBucketBlasBuildBatchPlanInput input;
    input.observations = observations;
    input.observationCount = 4;
    input.submitBuilds = true;
    const RtSmokeStaticBucketBlasBuildBatchPlan mixedPlan =
        BuildSmokeStaticBucketBlasBuildBatchPlan(input);
    Check(mixedPlan.emittedRecords == 4 &&
        mixedPlan.submitBuildRecords == 3 &&
        mixedPlan.skippedBuildRecords == 1 &&
        mixedPlan.createBlasRecords == 2 &&
        mixedPlan.signatureChangedRecords == 1 &&
        mixedPlan.uploadRequiredRecords == 1 &&
        mixedPlan.incompatibleRecords == 1 &&
        mixedPlan.missingBlasRecords == 1 &&
        mixedPlan.records[0].buildPlan.skipBuild &&
        mixedPlan.records[1].buildPlan.submitBuild &&
        mixedPlan.records[2].buildPlan.createBlas &&
        mixedPlan.planSignature != 0,
        "static bucket BLAS batch plan aggregates mixed bucket build decisions");

    input.submitBuilds = false;
    const RtSmokeStaticBucketBlasBuildBatchPlan gatedPlan =
        BuildSmokeStaticBucketBlasBuildBatchPlan(input);
    Check(gatedPlan.emittedRecords == 4 &&
        gatedPlan.submitBuildRecords == 0 &&
        gatedPlan.skippedBuildRecords == 4 &&
        gatedPlan.createBlasRecords == 0,
        "static bucket BLAS batch plan respects disabled build gate");

    input.submitBuilds = true;
    input.forceRebuild = true;
    const RtSmokeStaticBucketBlasBuildBatchPlan forcePlan =
        BuildSmokeStaticBucketBlasBuildBatchPlan(input);
    Check(forcePlan.submitBuildRecords == 4 &&
        forcePlan.skippedBuildRecords == 0 &&
        forcePlan.records[0].buildPlan.submitBuild,
        "static bucket BLAS batch plan applies force rebuild to every emitted bucket");

    input.forceRebuild = false;
    input.maxRecords = 2;
    const RtSmokeStaticBucketBlasBuildBatchPlan cappedPlan =
        BuildSmokeStaticBucketBlasBuildBatchPlan(input);
    Check(cappedPlan.emittedRecords == 2 &&
        cappedPlan.overflow &&
        cappedPlan.records[1].bucketKey == 20,
        "static bucket BLAS batch plan reports record cap overflow");
}

void TestStaticBucketBlasBuildObservationPlan()
{
    RtSmokeStaticBvhBucketSignature currentBuckets[4];
    currentBuckets[0].bucketKey = 10;
    currentBuckets[0].blasInputSignature = 100;
    currentBuckets[0].active = true;
    currentBuckets[0].resident = true;

    currentBuckets[1] = currentBuckets[0];
    currentBuckets[1].bucketKey = 20;
    currentBuckets[1].blasInputSignature = 200;

    currentBuckets[2] = currentBuckets[0];
    currentBuckets[2].bucketKey = 30;
    currentBuckets[2].blasInputSignature = 300;
    currentBuckets[2].active = false;

    currentBuckets[3] = currentBuckets[0];
    currentBuckets[3].bucketKey = 40;
    currentBuckets[3].blasInputSignature = 400;

    RtSmokeStaticBucketBlasCacheState previousBuckets[3];
    previousBuckets[0].bucketKey = 10;
    previousBuckets[0].blasInputSignature = 100;
    previousBuckets[0].hasBlas = true;
    previousBuckets[0].blasInputsCompatible = true;

    previousBuckets[1] = previousBuckets[0];
    previousBuckets[1].bucketKey = 20;
    previousBuckets[1].blasInputSignature = 201;

    previousBuckets[2] = previousBuckets[0];
    previousBuckets[2].bucketKey = 40;
    previousBuckets[2].blasInputSignature = 400;
    previousBuckets[2].hasBlas = false;

    RtSmokeStaticBucketBlasBuildObservationPlanInput input;
    input.currentBuckets = currentBuckets;
    input.currentBucketCount = 4;
    input.previousBuckets = previousBuckets;
    input.previousBucketCount = 3;
    const RtSmokeStaticBucketBlasBuildObservationPlan plan =
        BuildSmokeStaticBucketBlasBuildObservationPlan(input);
    Check(plan.emittedObservations == 3 &&
        plan.cacheHits == 1 &&
        plan.cacheMisses == 2 &&
        plan.signatureChanged == 1 &&
        plan.uploadRequired == 2 &&
        plan.skippedInactive == 1 &&
        plan.observations[0].bucketKey == 10 &&
        !plan.observations[0].uploadRequired &&
        plan.observations[1].bucketKey == 20 &&
        plan.observations[1].uploadRequired &&
        plan.observations[2].bucketKey == 40 &&
        !plan.observations[2].hasBlas &&
        plan.planSignature != 0,
        "static bucket BLAS observation plan derives active cache hit and miss observations");

    input.maxRecords = 1;
    const RtSmokeStaticBucketBlasBuildObservationPlan cappedPlan =
        BuildSmokeStaticBucketBlasBuildObservationPlan(input);
    Check(cappedPlan.emittedObservations == 1 &&
        cappedPlan.overflow &&
        cappedPlan.observations[0].bucketKey == 10,
        "static bucket BLAS observation plan reports active record cap overflow");
}

void TestStaticBucketWorkPlan()
{
    RtSmokeStaticTlasBucketObservation buckets[3];
    buckets[0].bucketKey = 10;
    buckets[0].resident = true;
    buckets[0].active = true;
    buckets[0].hasBlas = true;
    buckets[0].routeRecordIndex = 0;
    buckets[0].activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_VISIBLE;
    buckets[0].residentVertexOffset = 0;
    buckets[0].residentVertexCount = 12;
    buckets[0].residentIndexOffset = 0;
    buckets[0].residentIndexCount = 36;
    buckets[0].residentTriangleOffset = 0;
    buckets[0].residentTriangleCount = 12;
    buckets[0].activeVertexCount = 12;
    buckets[0].activeIndexCount = 36;
    buckets[0].activeTriangleCount = 12;

    buckets[1] = buckets[0];
    buckets[1].bucketKey = 20;
    buckets[1].active = false;
    buckets[1].routeRecordIndex = 1;
    buckets[1].residentVertexOffset = 12;
    buckets[1].residentIndexOffset = 36;
    buckets[1].residentTriangleOffset = 12;

    buckets[2] = buckets[0];
    buckets[2].bucketKey = 30;
    buckets[2].routeRecordIndex = 2;
    buckets[2].residentVertexOffset = 24;
    buckets[2].residentIndexOffset = 72;
    buckets[2].residentTriangleOffset = 24;

    RtSmokeStaticBucketBlasCacheState previousBuckets[2];
    previousBuckets[0].bucketKey = 10;
    previousBuckets[0].hasBlas = true;
    previousBuckets[0].blasInputsCompatible = true;

    RtSmokeStaticBvhBucketSignatureInput signatureInput;
    signatureInput.bucket = buckets[0];
    signatureInput.geometryContentSignature = 1000;
    signatureInput.materialGeneration = 2000;
    previousBuckets[0].blasInputSignature =
        BuildSmokeStaticBvhBucketSignature(signatureInput).blasInputSignature;

    previousBuckets[1].bucketKey = 30;
    previousBuckets[1].hasBlas = true;
    previousBuckets[1].blasInputsCompatible = true;
    previousBuckets[1].blasInputSignature = previousBuckets[0].blasInputSignature + 1;

    RtSmokeStaticBucketWorkPlanInput input;
    input.buckets = buckets;
    input.bucketCount = 3;
    input.previousBuckets = previousBuckets;
    input.previousBucketCount = 2;
    input.geometryContentSignature = 1000;
    input.materialGeneration = 2000;
    input.totalVertexCount = 36;
    input.totalIndexCount = 108;
    input.totalTriangleCount = 36;
    input.hasStaticBlas = true;
    input.submitBuilds = true;
    input.enableStaticRoutes = true;
    input.shaderSupportsStaticBucketRoutes = false;
    input.rigidRouteRecordCount = 5;
    const RtSmokeStaticBucketWorkPlan blockedPlan =
        BuildSmokeStaticBucketWorkPlan(input);
    Check(blockedPlan.bucketSignatures.size() == 3 &&
        blockedPlan.activeSetPlan.activeBuckets == 2 &&
        blockedPlan.activeSetPlan.inactiveResidentBuckets == 1 &&
        blockedPlan.activeSetPlan.inactiveResidentGeometryIncluded &&
        blockedPlan.bucketBlasPlan.emittedRecords == 2 &&
        blockedPlan.bucketBlasPlan.skippedInactive == 1 &&
        blockedPlan.traversalCompatibility.requiresShaderRouteMetadata &&
        blockedPlan.routeNamespace.staticRoutesBlocked &&
        blockedPlan.routeTablePlan.emittedRecords == 0 &&
        blockedPlan.routeTablePlan.blocked &&
        blockedPlan.buildObservationPlan.emittedObservations == 2 &&
        blockedPlan.buildObservationPlan.cacheHits == 1 &&
        blockedPlan.buildObservationPlan.cacheMisses == 1 &&
        blockedPlan.buildBatchPlan.submitBuildRecords == 1 &&
        blockedPlan.buildBatchPlan.skippedBuildRecords == 1 &&
        blockedPlan.planSignature != 0,
        "static bucket work plan composes bucket cache, build, route, and shader compatibility decisions");

    input.shaderSupportsStaticBucketRoutes = true;
    const RtSmokeStaticBucketWorkPlan routedPlan =
        BuildSmokeStaticBucketWorkPlan(input);
    Check(routedPlan.routeNamespace.staticRoutesEnabled &&
        !routedPlan.routeNamespace.staticRoutesBlocked &&
        routedPlan.routeTablePlan.emittedRecords == 2 &&
        routedPlan.routeTablePlan.records[0].instanceId == 2 &&
        routedPlan.routeNamespace.rigidFirstInstanceId == 4,
        "static bucket work plan emits route table records when shader route support is available");

    input.maxRouteRecords = 1;
    const RtSmokeStaticBucketWorkPlan routeCappedPlan =
        BuildSmokeStaticBucketWorkPlan(input);
    Check(routeCappedPlan.bucketBlasPlan.emittedRecords == 2 &&
        !routeCappedPlan.bucketBlasPlan.overflow &&
        routeCappedPlan.routeTablePlan.emittedRecords == 1 &&
        routeCappedPlan.routeTablePlan.overflow,
        "static bucket work plan reports route table cap overflow independently");

    input.maxRouteRecords = 0;
    input.maxBuildRecords = 1;
    const RtSmokeStaticBucketWorkPlan buildCappedPlan =
        BuildSmokeStaticBucketWorkPlan(input);
    Check(buildCappedPlan.buildObservationPlan.emittedObservations == 1 &&
        buildCappedPlan.buildObservationPlan.overflow &&
        buildCappedPlan.buildBatchPlan.emittedRecords == 1 &&
        !buildCappedPlan.buildBatchPlan.overflow,
        "static bucket work plan reports build observation cap overflow independently");

    input.maxBuildRecords = 0;
    input.maxBucketRecords = 1;
    const RtSmokeStaticBucketWorkPlan bucketCappedPlan =
        BuildSmokeStaticBucketWorkPlan(input);
    Check(bucketCappedPlan.bucketBlasPlan.emittedRecords == 1 &&
        bucketCappedPlan.bucketBlasPlan.overflow &&
        bucketCappedPlan.routeTablePlan.emittedRecords == 1 &&
        !bucketCappedPlan.routeTablePlan.overflow,
        "static bucket work plan reports bucket BLAS cap overflow independently");

    RtSmokeStaticBucketWorkPlanInput monolithicInput;
    monolithicInput.buckets = buckets;
    monolithicInput.bucketCount = 1;
    monolithicInput.geometryContentSignature = 1000;
    monolithicInput.materialGeneration = 2000;
    monolithicInput.totalVertexCount = buckets[0].residentVertexCount;
    monolithicInput.totalIndexCount = buckets[0].residentIndexCount;
    monolithicInput.totalTriangleCount = buckets[0].residentTriangleCount;
    monolithicInput.hasStaticBlas = true;
    monolithicInput.enableStaticRoutes = true;
    monolithicInput.shaderSupportsStaticBucketRoutes = false;
    monolithicInput.rigidRouteRecordCount = 3;
    const RtSmokeStaticBucketWorkPlan monolithicPlan =
        BuildSmokeStaticBucketWorkPlan(monolithicInput);
    Check(monolithicPlan.traversalCompatibility.exactMonolithicRecord &&
        !monolithicPlan.routeNamespace.staticRoutesBlocked &&
        monolithicPlan.routeTablePlan.emittedRecords == 0 &&
        monolithicPlan.routeNamespace.rigidFirstInstanceId == 2,
        "static bucket work plan keeps exact monolithic static BLAS on the current route namespace");
}

void TestStaticBucketRigidRouteNamespaceComposition()
{
    RtSmokeStaticTlasBucketObservation buckets[2];
    buckets[0].bucketKey = 10;
    buckets[0].resident = true;
    buckets[0].active = true;
    buckets[0].hasBlas = true;
    buckets[0].routeRecordIndex = 0;
    buckets[0].residentVertexCount = 3;
    buckets[0].residentIndexCount = 3;
    buckets[0].residentTriangleCount = 1;
    buckets[0].activeVertexCount = 3;
    buckets[0].activeIndexCount = 3;
    buckets[0].activeTriangleCount = 1;

    buckets[1] = buckets[0];
    buckets[1].bucketKey = 20;
    buckets[1].routeRecordIndex = 1;
    buckets[1].residentVertexOffset = 3;
    buckets[1].residentIndexOffset = 3;
    buckets[1].residentTriangleOffset = 1;

    RtSmokeStaticBucketWorkPlanInput staticInput;
    staticInput.buckets = buckets;
    staticInput.bucketCount = 2;
    staticInput.geometryContentSignature = 500;
    staticInput.materialGeneration = 600;
    staticInput.totalVertexCount = 6;
    staticInput.totalIndexCount = 6;
    staticInput.totalTriangleCount = 2;
    staticInput.hasStaticBlas = true;
    staticInput.enableStaticRoutes = true;
    staticInput.shaderSupportsStaticBucketRoutes = true;
    staticInput.rigidRouteRecordCount = 2;
    const RtSmokeStaticBucketWorkPlan staticPlan =
        BuildSmokeStaticBucketWorkPlan(staticInput);

    RtSmokeRigidTlasObservation rigidObservations[2];
    rigidObservations[0].meshHash = 1000;
    rigidObservations[0].instanceId = 100;
    rigidObservations[0].sourceFlags = 0x2;
    rigidObservations[0].hasMeshRecord = true;
    rigidObservations[0].meshSeenThisFrame = true;
    rigidObservations[0].hasBlas = true;
    rigidObservations[0].objectToWorld[0] = 1.0f;
    rigidObservations[0].objectToWorld[5] = 1.0f;
    rigidObservations[0].objectToWorld[10] = 1.0f;
    rigidObservations[0].objectToWorld[15] = 1.0f;
    rigidObservations[1] = rigidObservations[0];
    rigidObservations[1].meshHash = 2000;
    rigidObservations[1].instanceId = 200;

    RtSmokeRigidTlasPlanDesc rigidDesc;
    rigidDesc.observations = rigidObservations;
    rigidDesc.observationCount = 2;
    rigidDesc.rigidSourceMask = 0x2;
    rigidDesc.firstInstanceId = staticPlan.routeNamespace.rigidFirstInstanceId;
    rigidDesc.instanceMask = 0x02;
    const RtSmokeRigidTlasPlan rigidPlan = BuildSmokeRigidTlasPlan(rigidDesc);

    Check(staticPlan.routeTablePlan.emittedRecords == 2 &&
        rigidPlan.emittedInstances == 2 &&
        staticPlan.routeTablePlan.records[0].instanceId == 2 &&
        staticPlan.routeTablePlan.records[1].instanceId == 3 &&
        rigidPlan.instances[0].instanceId == 4 &&
        rigidPlan.instances[1].instanceId == 5,
        "static bucket and rigid route planners compose non-overlapping instance IDs");

    staticInput.shaderSupportsStaticBucketRoutes = false;
    const RtSmokeStaticBucketWorkPlan blockedStaticPlan =
        BuildSmokeStaticBucketWorkPlan(staticInput);
    rigidDesc.firstInstanceId = blockedStaticPlan.routeNamespace.rigidFirstInstanceId;
    const RtSmokeRigidTlasPlan blockedRigidPlan = BuildSmokeRigidTlasPlan(rigidDesc);
    Check(blockedStaticPlan.routeNamespace.staticRoutesBlocked &&
        blockedStaticPlan.routeTablePlan.emittedRecords == 0 &&
        blockedRigidPlan.instances[0].instanceId == 2,
        "blocked static routes keep current rigid route instance base");
}

void TestStaticBucketWorkPlanSnapshot()
{
    RtSmokeStaticTlasBucketObservation buckets[2];
    buckets[0].bucketKey = 10;
    buckets[0].resident = true;
    buckets[0].active = true;
    buckets[0].hasBlas = true;
    buckets[0].routeRecordIndex = 0;
    buckets[0].residentVertexCount = 12;
    buckets[0].residentIndexCount = 36;
    buckets[0].residentTriangleCount = 12;
    buckets[0].activeVertexCount = 12;
    buckets[0].activeIndexCount = 36;
    buckets[0].activeTriangleCount = 12;

    buckets[1] = buckets[0];
    buckets[1].bucketKey = 20;
    buckets[1].routeRecordIndex = 1;
    buckets[1].residentVertexOffset = 12;
    buckets[1].residentIndexOffset = 36;
    buckets[1].residentTriangleOffset = 12;

    RtSmokeStaticBucketBlasCacheState previousBuckets[1];
    previousBuckets[0].bucketKey = 10;
    previousBuckets[0].hasBlas = true;
    previousBuckets[0].blasInputsCompatible = true;

    RtSmokeStaticBvhBucketSignatureInput signatureInput;
    signatureInput.bucket = buckets[0];
    signatureInput.geometryContentSignature = 3000;
    signatureInput.materialGeneration = 4000;
    previousBuckets[0].blasInputSignature =
        BuildSmokeStaticBvhBucketSignature(signatureInput).blasInputSignature;

    RtSmokeStaticBucketWorkPlanInput input;
    input.buckets = buckets;
    input.bucketCount = 2;
    input.previousBuckets = previousBuckets;
    input.previousBucketCount = 1;
    input.geometryContentSignature = 3000;
    input.materialGeneration = 4000;
    input.totalVertexCount = 24;
    input.totalIndexCount = 72;
    input.totalTriangleCount = 24;
    input.submitBuilds = true;
    input.enableStaticRoutes = true;
    input.shaderSupportsStaticBucketRoutes = true;
    const RtSmokeStaticBucketWorkPlanSnapshot snapshot =
        CaptureSmokeStaticBucketWorkPlanSnapshot(input);
    const RtSmokeStaticBucketWorkPlan originalPlan =
        BuildSmokeStaticBucketWorkPlan(snapshot);

    buckets[0].bucketKey = 99;
    buckets[0].active = false;
    buckets[1].residentIndexCount = 0;
    previousBuckets[0].hasBlas = false;
    previousBuckets[0].blasInputSignature = 0;
    const RtSmokeStaticBucketWorkPlan snapshotPlan =
        BuildSmokeStaticBucketWorkPlan(snapshot);
    const RtSmokeStaticBucketWorkPlan mutatedSourcePlan =
        BuildSmokeStaticBucketWorkPlan(input);

    Check(snapshot.buckets.size() == 2 &&
        snapshot.previousBuckets.size() == 1 &&
        snapshotPlan.planSignature == originalPlan.planSignature &&
        snapshotPlan.bucketBlasPlan.emittedRecords == 2 &&
        snapshotPlan.buildObservationPlan.cacheHits == 1 &&
        mutatedSourcePlan.planSignature != snapshotPlan.planSignature,
        "static bucket work snapshot owns immutable bucket and cache input data");
}

void TestStaticBucketWorkPlanTimedResult()
{
    RtSmokeStaticTlasBucketObservation bucket;
    bucket.bucketKey = 77;
    bucket.resident = true;
    bucket.active = true;
    bucket.hasBlas = true;
    bucket.routeRecordIndex = 0;
    bucket.residentVertexCount = 3;
    bucket.residentIndexCount = 3;
    bucket.residentTriangleCount = 1;
    bucket.activeVertexCount = 3;
    bucket.activeIndexCount = 3;
    bucket.activeTriangleCount = 1;

    RtSmokeStaticBucketWorkPlanInput input;
    input.buckets = &bucket;
    input.bucketCount = 1;
    input.geometryContentSignature = 55;
    input.materialGeneration = 66;
    input.totalVertexCount = 3;
    input.totalIndexCount = 3;
    input.totalTriangleCount = 1;
    input.submitBuilds = true;
    const RtSmokeStaticBucketWorkPlanSnapshot snapshot =
        CaptureSmokeStaticBucketWorkPlanSnapshot(input);
    const RtSmokeStaticBucketWorkPlan expectedPlan =
        BuildSmokeStaticBucketWorkPlan(snapshot);
    const RtSmokeStaticBucketWorkPlanTimedResult timedResult =
        BuildSmokeStaticBucketWorkPlanTimedResult(snapshot);
    Check(timedResult.plan.planSignature == expectedPlan.planSignature &&
        timedResult.plan.bucketBlasPlan.emittedRecords == 1,
        "timed static bucket work result preserves snapshot planning output");
}

void TestStaticBucketWorkPlanInputToken()
{
    RtSmokeStaticTlasBucketObservation buckets[2];
    buckets[0].bucketKey = 10;
    buckets[0].resident = true;
    buckets[0].active = true;
    buckets[0].hasBlas = true;
    buckets[0].routeRecordIndex = 0;
    buckets[0].residentVertexCount = 12;
    buckets[0].residentIndexCount = 36;
    buckets[0].residentTriangleCount = 12;
    buckets[0].activeVertexCount = 12;
    buckets[0].activeIndexCount = 36;
    buckets[0].activeTriangleCount = 12;

    buckets[1] = buckets[0];
    buckets[1].bucketKey = 20;
    buckets[1].routeRecordIndex = 1;
    buckets[1].residentVertexOffset = 12;
    buckets[1].residentIndexOffset = 36;
    buckets[1].residentTriangleOffset = 12;

    RtSmokeStaticBucketBlasCacheState previousBuckets[1];
    previousBuckets[0].bucketKey = 10;
    previousBuckets[0].blasInputSignature = 100;
    previousBuckets[0].hasBlas = true;
    previousBuckets[0].blasInputsCompatible = true;

    RtSmokeStaticBucketWorkPlanInput input;
    input.buckets = buckets;
    input.bucketCount = 2;
    input.previousBuckets = previousBuckets;
    input.previousBucketCount = 1;
    input.geometryContentSignature = 1000;
    input.materialGeneration = 2000;
    input.totalVertexCount = 24;
    input.totalIndexCount = 72;
    input.totalTriangleCount = 24;
    input.submitBuilds = true;
    input.enableStaticRoutes = true;
    input.shaderSupportsStaticBucketRoutes = false;
    input.rigidRouteRecordCount = 4;

    const uint64_t baseToken = BuildSmokeStaticBucketWorkPlanInputToken(input);
    Check(baseToken == BuildSmokeStaticBucketWorkPlanInputToken(input),
        "static bucket work input token is deterministic");

    RtSmokeStaticBucketWorkPlanInput changedInput = input;
    buckets[1].active = false;
    Check(baseToken != BuildSmokeStaticBucketWorkPlanInputToken(changedInput),
        "static bucket work input token tracks active bucket membership");

    buckets[1].active = true;
    buckets[1].residentIndexCount = 33;
    Check(baseToken != BuildSmokeStaticBucketWorkPlanInputToken(input),
        "static bucket work input token tracks bucket geometry ranges");

    buckets[1].residentIndexCount = 36;
    previousBuckets[0].blasInputSignature = 101;
    Check(baseToken != BuildSmokeStaticBucketWorkPlanInputToken(input),
        "static bucket work input token tracks previous bucket cache signatures");

    previousBuckets[0].blasInputSignature = 100;
    input.shaderSupportsStaticBucketRoutes = true;
    Check(baseToken != BuildSmokeStaticBucketWorkPlanInputToken(input),
        "static bucket work input token tracks route support flags");

    input.shaderSupportsStaticBucketRoutes = false;
    const RtSmokeStaticBucketWorkPlanSnapshot snapshot =
        CaptureSmokeStaticBucketWorkPlanSnapshot(input);
    const uint64_t snapshotToken = BuildSmokeStaticBucketWorkPlanInputToken(snapshot);
    buckets[0].bucketKey = 99;
    previousBuckets[0].hasBlas = false;
    Check(snapshotToken == BuildSmokeStaticBucketWorkPlanInputToken(snapshot) &&
        snapshotToken == baseToken &&
        BuildSmokeStaticBucketWorkPlanInputToken(input) != snapshotToken,
        "static bucket work snapshot token is immutable after source mutation");
}

void TestAsyncStaticBucketWorkPlanning()
{
    RtSmokeStaticTlasBucketObservation bucket;
    bucket.bucketKey = 88;
    bucket.resident = true;
    bucket.active = true;
    bucket.hasBlas = true;
    bucket.routeRecordIndex = 0;
    bucket.residentVertexCount = 3;
    bucket.residentIndexCount = 3;
    bucket.residentTriangleCount = 1;
    bucket.activeVertexCount = 3;
    bucket.activeIndexCount = 3;
    bucket.activeTriangleCount = 1;

    RtSmokeStaticBucketWorkPlanInput input;
    input.buckets = &bucket;
    input.bucketCount = 1;
    input.geometryContentSignature = 700;
    input.materialGeneration = 800;
    input.totalVertexCount = 3;
    input.totalIndexCount = 3;
    input.totalTriangleCount = 1;
    input.submitBuilds = true;
    const RtSmokeStaticBucketWorkPlanSnapshot snapshot =
        CaptureSmokeStaticBucketWorkPlanSnapshot(input);

    RtPathTraceCpuWorkState state;
    RtPathTraceCpuWorkGeneration generation;
    generation.frameIndex = 42;
    generation.sceneGeneration = 1;
    generation.geometryGeneration = input.geometryContentSignature;
    generation.materialGeneration = input.materialGeneration;
    generation.lightGeneration = BuildSmokeStaticBucketWorkPlanInputToken(snapshot);
    RtPathTraceCpuWorkPublishSnapshot(state, generation);

    std::future<RtSmokeStaticBucketWorkPlanTimedResult> future = std::async(
        std::launch::async,
        [snapshot]() {
            return BuildSmokeStaticBucketWorkPlanTimedResult(snapshot);
        });

    const RtPathTraceCpuWorkFrameDecision lateDecision =
        RtPathTraceCpuWorkAcceptLatest(state, generation, nullptr, true);
    Check(lateDecision.lateFallback && lateDecision.syncFallback,
        "late async static bucket work requests synchronous fallback");

    const RtSmokeStaticBucketWorkPlanTimedResult timedResult = future.get();
    RtPathTraceCpuWorkResultEnvelope envelope;
    envelope.completed = true;
    envelope.generation = generation;
    envelope.timing.workerExecutionMs = static_cast<double>(timedResult.planningTimeMicros) / 1000.0;
    RtPathTraceCpuWorkPublishCompletedResult(state, envelope);
    const RtPathTraceCpuWorkFrameDecision acceptDecision =
        RtPathTraceCpuWorkAcceptLatest(state, generation, nullptr, true);
    Check(acceptDecision.accepted &&
        timedResult.plan.bucketBlasPlan.emittedRecords == 1 &&
        state.acceptedResultCount == 1,
        "matching async static bucket work generation is accepted");

    RtPathTraceCpuWorkResultEnvelope staleEnvelope = envelope;
    staleEnvelope.generation.lightGeneration ^= 0x1000ull;
    const RtPathTraceCpuWorkFrameDecision staleDecision =
        RtPathTraceCpuWorkAcceptLatest(state, generation, &staleEnvelope, true);
    Check(staleDecision.staleRejected && staleDecision.syncFallback,
        "stale async static bucket work generation is rejected");
}

void TestStaticActiveSetPlan()
{
    RtSmokeStaticTlasBucketObservation buckets[3];
    buckets[0].bucketKey = 100;
    buckets[0].resident = true;
    buckets[0].active = true;
    buckets[0].hasBlas = true;
    buckets[0].activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_VISIBLE | RT_SMOKE_STATIC_ACTIVE_SELECTED_AREA;
    buckets[0].routeRecordIndex = 0;
    buckets[0].residentSurfaceCount = 1;
    buckets[0].residentVertexCount = 10;
    buckets[0].residentIndexCount = 30;
    buckets[0].residentTriangleCount = 10;
    buckets[0].activeSurfaceCount = 1;
    buckets[0].activeVertexCount = 10;
    buckets[0].activeIndexCount = 30;
    buckets[0].activeTriangleCount = 10;

    buckets[1].bucketKey = 200;
    buckets[1].resident = true;
    buckets[1].active = false;
    buckets[1].hasBlas = true;
    buckets[1].routeRecordIndex = 1;
    buckets[1].residentSurfaceCount = 1;
    buckets[1].residentVertexCount = 20;
    buckets[1].residentIndexCount = 60;
    buckets[1].residentTriangleCount = 20;

    buckets[2].bucketKey = 300;
    buckets[2].resident = true;
    buckets[2].active = true;
    buckets[2].hasBlas = true;
    buckets[2].activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_FORCE_INCLUDE;
    buckets[2].routeRecordIndex = 2;
    buckets[2].residentSurfaceCount = 1;
    buckets[2].residentVertexCount = 5;
    buckets[2].residentIndexCount = 15;
    buckets[2].residentTriangleCount = 5;
    buckets[2].activeSurfaceCount = 1;
    buckets[2].activeVertexCount = 5;
    buckets[2].activeIndexCount = 15;
    buckets[2].activeTriangleCount = 5;

    RtSmokeStaticTlasActiveSetPlanDesc desc;
    desc.buckets = buckets;
    desc.bucketCount = 3;
    desc.hasStaticBlas = true;
    desc.firstInstanceId = 4;
    desc.instanceMask = 0x01;

    const RtSmokeStaticTlasActiveSetPlan monolithicPlan = BuildSmokeStaticTlasActiveSetPlan(desc);
    Check(monolithicPlan.emittedInstances == 1, "monolithic static active-set plan emits one static TLAS instance");
    Check(monolithicPlan.activeBuckets == 2 && monolithicPlan.inactiveResidentBuckets == 1, "static active-set plan separates active and resident buckets");
    Check(monolithicPlan.inactiveResidentGeometryIncluded && monolithicPlan.requiresBucketedStaticBlas, "monolithic static active-set plan reports inactive resident geometry included");

    desc.monolithicStaticBlas = false;
    const RtSmokeStaticTlasActiveSetPlan bucketPlan = BuildSmokeStaticTlasActiveSetPlan(desc);
    Check(bucketPlan.emittedInstances == 2 && bucketPlan.instances.size() == 2, "bucketed static active-set plan omits resident inactive geometry from TLAS");
    Check(bucketPlan.instances[0].kind == RT_SMOKE_PLAN_TLAS_STATIC_BUCKET_BLAS && bucketPlan.instances[0].meshHash == 100, "bucketed static active-set plan preserves first active bucket identity");
    Check(bucketPlan.instances[1].instanceId == 5 && bucketPlan.instances[1].flags == RT_SMOKE_STATIC_ACTIVE_FORCE_INCLUDE, "bucketed static active-set plan emits deterministic instance ids and reason flags");
    Check(!bucketPlan.inactiveResidentGeometryIncluded && !bucketPlan.requiresBucketedStaticBlas, "bucketed static active-set plan does not include inactive resident geometry");

    buckets[2].hasBlas = false;
    const RtSmokeStaticTlasActiveSetPlan missingBucketBlasPlan = BuildSmokeStaticTlasActiveSetPlan(desc);
    Check(missingBucketBlasPlan.activeBuckets == 2 && missingBucketBlasPlan.emittedInstances == 1, "bucketed static active-set plan waits for active bucket BLAS readiness");

    RtSmokeStaticTlasBucketObservation monolithicSurfaceDeltaBucket = buckets[0];
    monolithicSurfaceDeltaBucket.residentSurfaceCount = 3;
    monolithicSurfaceDeltaBucket.activeSurfaceCount = 2;
    monolithicSurfaceDeltaBucket.residentVertexCount = monolithicSurfaceDeltaBucket.activeVertexCount;
    monolithicSurfaceDeltaBucket.residentIndexCount = monolithicSurfaceDeltaBucket.activeIndexCount;
    monolithicSurfaceDeltaBucket.residentTriangleCount = monolithicSurfaceDeltaBucket.activeTriangleCount;
    RtSmokeStaticTlasActiveSetPlanDesc surfaceDeltaDesc;
    surfaceDeltaDesc.buckets = &monolithicSurfaceDeltaBucket;
    surfaceDeltaDesc.bucketCount = 1;
    surfaceDeltaDesc.monolithicStaticBlas = true;
    surfaceDeltaDesc.hasStaticBlas = true;
    const RtSmokeStaticTlasActiveSetPlan surfaceDeltaPlan = BuildSmokeStaticTlasActiveSetPlan(surfaceDeltaDesc);
    Check(surfaceDeltaPlan.inactiveResidentGeometryIncluded && surfaceDeltaPlan.requiresBucketedStaticBlas, "monolithic static active-set plan detects inactive retained surfaces even when vertex counts match");
}

void TestStaticBucketObservation()
{
    RtSmokeStaticTlasBucketObservationInput input;
    input.bucketKey = 900;
    input.routeRecordIndex = 7;
    input.activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_VISIBLE | RT_SMOKE_STATIC_ACTIVE_SELECTED_AREA;
    input.vertexOffset = 3;
    input.indexOffset = 9;
    input.triangleOffset = 3;
    input.vertexCount = 12;
    input.indexCount = 36;
    input.triangleCount = 12;
    input.valid = true;
    input.seenThisFrame = true;
    input.hasBlas = true;

    RtSmokeStaticTlasBucketObservation observation;
    Check(BuildSmokeStaticTlasBucketObservation(input, observation), "static bucket observation accepts valid resident surface range");
    Check(observation.bucketKey == 900 && observation.routeRecordIndex == 7 &&
        observation.resident && observation.active && observation.hasBlas,
        "static bucket observation preserves resident active identity");
    Check(observation.residentSurfaceCount == 1 && observation.residentVertexCount == 12 &&
        observation.residentIndexCount == 36 && observation.residentTriangleCount == 12 &&
        observation.residentVertexOffset == 3 && observation.residentIndexOffset == 9 &&
        observation.residentTriangleOffset == 3,
        "static bucket observation records resident geometry ranges");
    Check(observation.activeSurfaceCount == 1 && observation.activeVertexCount == 12 &&
        observation.activeIndexCount == 36 && observation.activeTriangleCount == 12 &&
        observation.activeReasonFlags == input.activeReasonFlags,
        "static bucket observation records active counts and reason flags");

    input.seenThisFrame = false;
    RtSmokeStaticTlasBucketObservation inactiveObservation;
    Check(BuildSmokeStaticTlasBucketObservation(input, inactiveObservation), "static bucket observation accepts inactive resident surface range");
    Check(inactiveObservation.resident && !inactiveObservation.active &&
        inactiveObservation.activeSurfaceCount == 0 && inactiveObservation.activeReasonFlags == 0,
        "static bucket observation separates inactive resident geometry from active membership");

    input.valid = false;
    RtSmokeStaticTlasBucketObservation invalidObservation;
    Check(!BuildSmokeStaticTlasBucketObservation(input, invalidObservation), "static bucket observation rejects invalid records");

    input.valid = true;
    input.indexCount = 0;
    Check(!BuildSmokeStaticTlasBucketObservation(input, invalidObservation), "static bucket observation rejects empty ranges");
}

void TestStaticBucketBlasPlan()
{
    RtSmokeStaticTlasBucketObservation buckets[3];
    buckets[0].bucketKey = 100;
    buckets[0].resident = true;
    buckets[0].active = true;
    buckets[0].activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_VISIBLE;
    buckets[0].routeRecordIndex = 0;
    buckets[0].residentVertexOffset = 0;
    buckets[0].residentVertexCount = 10;
    buckets[0].residentIndexOffset = 0;
    buckets[0].residentIndexCount = 30;
    buckets[0].residentTriangleOffset = 0;
    buckets[0].residentTriangleCount = 10;

    buckets[1] = buckets[0];
    buckets[1].bucketKey = 200;
    buckets[1].active = false;
    buckets[1].activeReasonFlags = 0;
    buckets[1].routeRecordIndex = 1;
    buckets[1].residentVertexOffset = 10;
    buckets[1].residentVertexCount = 20;
    buckets[1].residentIndexOffset = 30;
    buckets[1].residentIndexCount = 60;
    buckets[1].residentTriangleOffset = 10;
    buckets[1].residentTriangleCount = 20;

    buckets[2] = buckets[0];
    buckets[2].bucketKey = 300;
    buckets[2].residentIndexCount = 0;

    RtSmokeStaticBucketBlasPlanDesc desc;
    desc.buckets = buckets;
    desc.bucketCount = 3;
    desc.activeOnly = true;
    const RtSmokeStaticBucketBlasPlan activePlan = BuildSmokeStaticBucketBlasPlan(desc);
    Check(activePlan.emittedRecords == 1 && activePlan.activeBuckets == 2 &&
        activePlan.residentBuckets == 3 && activePlan.skippedInactive == 1 &&
        activePlan.skippedInvalid == 1,
        "static bucket BLAS plan emits active valid bucket ranges");
    Check(activePlan.records[0].bucketKey == 100 &&
        activePlan.records[0].range.vertexOffset == 0 &&
        activePlan.records[0].range.indexCount == 30 &&
        activePlan.planSignature != 0,
        "static bucket BLAS plan preserves bucket range identity");

    desc.activeOnly = false;
    const RtSmokeStaticBucketBlasPlan residentPlan = BuildSmokeStaticBucketBlasPlan(desc);
    Check(residentPlan.emittedRecords == 2 && residentPlan.skippedInactive == 0 &&
        residentPlan.records[1].bucketKey == 200 && !residentPlan.records[1].active,
        "static bucket BLAS plan can retain inactive resident bucket ranges");

    desc.activeOnly = false;
    desc.maxRecords = 1;
    const RtSmokeStaticBucketBlasPlan cappedPlan = BuildSmokeStaticBucketBlasPlan(desc);
    Check(cappedPlan.emittedRecords == 1 && cappedPlan.overflow, "static bucket BLAS plan reports record cap overflow");
}

void TestStaticBucketTraversalCompatibility()
{
    RtSmokeStaticBucketBlasRecord monolithicRecord;
    monolithicRecord.bucketKey = 100;
    monolithicRecord.range.vertexOffset = 0;
    monolithicRecord.range.vertexCount = 30;
    monolithicRecord.range.indexOffset = 0;
    monolithicRecord.range.indexCount = 90;
    monolithicRecord.range.triangleOffset = 0;
    monolithicRecord.range.triangleCount = 30;

    RtSmokeStaticBucketTraversalCompatibilityInput input;
    input.records = &monolithicRecord;
    input.recordCount = 1;
    input.totalVertexCount = 30;
    input.totalIndexCount = 90;
    input.totalTriangleCount = 30;
    const RtSmokeStaticBucketTraversalCompatibility monolithicCompatibility =
        BuildSmokeStaticBucketTraversalCompatibility(input);
    Check(monolithicCompatibility.exactMonolithicRecord &&
        monolithicCompatibility.currentStaticShaderCompatible &&
        !monolithicCompatibility.requiresShaderRouteMetadata,
        "static bucket traversal compatibility accepts exact monolithic static record");

    RtSmokeStaticBucketBlasRecord bucketRecords[2];
    bucketRecords[0] = monolithicRecord;
    bucketRecords[0].range.vertexCount = 10;
    bucketRecords[0].range.indexCount = 30;
    bucketRecords[0].range.triangleCount = 10;
    bucketRecords[1] = bucketRecords[0];
    bucketRecords[1].bucketKey = 200;
    bucketRecords[1].range.vertexOffset = 10;
    bucketRecords[1].range.indexOffset = 30;
    bucketRecords[1].range.triangleOffset = 10;
    input.records = bucketRecords;
    input.recordCount = 2;
    const RtSmokeStaticBucketTraversalCompatibility bucketCompatibility =
        BuildSmokeStaticBucketTraversalCompatibility(input);
    Check(!bucketCompatibility.exactMonolithicRecord &&
        !bucketCompatibility.currentStaticShaderCompatible &&
        bucketCompatibility.requiresShaderRouteMetadata &&
        bucketCompatibility.nonZeroOffsetRecords == 1,
        "static bucket traversal compatibility blocks bucketed static records without shader routes");

    input.shaderSupportsStaticBucketRoutes = true;
    const RtSmokeStaticBucketTraversalCompatibility routedCompatibility =
        BuildSmokeStaticBucketTraversalCompatibility(input);
    Check(routedCompatibility.currentStaticShaderCompatible &&
        !routedCompatibility.requiresShaderRouteMetadata,
        "static bucket traversal compatibility allows bucketed records when shader routes are available");
}

void TestRouteInstanceNamespacePlan()
{
    RtSmokeRouteInstanceNamespacePlanInput input;
    input.staticRouteRecordCount = 4;
    input.rigidRouteRecordCount = 7;
    input.firstRouteInstanceId = 2;

    const RtSmokeRouteInstanceNamespacePlan defaultPlan =
        BuildSmokeRouteInstanceNamespacePlan(input);
    Check(!defaultPlan.staticRoutesEnabled &&
        !defaultPlan.staticRoutesBlocked &&
        defaultPlan.staticRouteInstanceCount == 0 &&
        defaultPlan.rigidRouteInstanceCount == 7 &&
        defaultPlan.rigidFirstInstanceId == 2 &&
        !defaultPlan.rigidRouteBaseShifted,
        "route namespace keeps current rigid base when static routes are disabled");

    input.enableStaticRoutes = true;
    const RtSmokeRouteInstanceNamespacePlan blockedPlan =
        BuildSmokeRouteInstanceNamespacePlan(input);
    Check(!blockedPlan.staticRoutesEnabled &&
        blockedPlan.staticRoutesRequireShaderSupport &&
        blockedPlan.staticRoutesBlocked &&
        blockedPlan.staticRouteInstanceCount == 0 &&
        blockedPlan.rigidFirstInstanceId == 2 &&
        !blockedPlan.rigidRouteBaseShifted,
        "route namespace blocks static routes without shader route support");

    input.shaderSupportsStaticBucketRoutes = true;
    const RtSmokeRouteInstanceNamespacePlan routedPlan =
        BuildSmokeRouteInstanceNamespacePlan(input);
    Check(routedPlan.staticRoutesEnabled &&
        !routedPlan.staticRoutesBlocked &&
        routedPlan.staticFirstInstanceId == 2 &&
        routedPlan.staticRouteInstanceCount == 4 &&
        routedPlan.rigidFirstInstanceId == 6 &&
        routedPlan.rigidRouteBaseShifted,
        "route namespace reserves static route IDs before shifted rigid route IDs");

    input.staticRouteRecordCount = 0;
    const RtSmokeRouteInstanceNamespacePlan zeroStaticPlan =
        BuildSmokeRouteInstanceNamespacePlan(input);
    Check(!zeroStaticPlan.staticRoutesEnabled &&
        zeroStaticPlan.staticRouteInstanceCount == 0 &&
        zeroStaticPlan.rigidFirstInstanceId == 2 &&
        !zeroStaticPlan.rigidRouteBaseShifted,
        "route namespace keeps rigid base when no static route records exist");
}

void TestStaticRouteTablePlan()
{
    RtSmokeStaticBucketBlasRecord bucketRecords[3];
    bucketRecords[0].bucketKey = 10;
    bucketRecords[0].routeRecordIndex = 3;
    bucketRecords[0].activeReasonFlags = 0x1;
    bucketRecords[0].range.vertexOffset = 0;
    bucketRecords[0].range.vertexCount = 12;
    bucketRecords[0].range.indexOffset = 0;
    bucketRecords[0].range.indexCount = 36;
    bucketRecords[0].range.triangleOffset = 0;
    bucketRecords[0].range.triangleCount = 12;
    bucketRecords[1] = bucketRecords[0];
    bucketRecords[1].bucketKey = 20;
    bucketRecords[1].routeRecordIndex = 4;
    bucketRecords[1].activeReasonFlags = 0x2;
    bucketRecords[1].range.vertexOffset = 12;
    bucketRecords[1].range.indexOffset = 36;
    bucketRecords[1].range.triangleOffset = 12;
    bucketRecords[2] = bucketRecords[0];
    bucketRecords[2].bucketKey = 30;
    bucketRecords[2].range.indexCount = 0;

    RtSmokeStaticRouteTablePlanInput input;
    input.records = bucketRecords;
    input.recordCount = 3;

    RtSmokeRouteInstanceNamespacePlanInput namespaceInput;
    namespaceInput.staticRouteRecordCount = 2;
    namespaceInput.rigidRouteRecordCount = 5;
    namespaceInput.firstRouteInstanceId = 2;
    namespaceInput.enableStaticRoutes = false;
    input.routeNamespace = BuildSmokeRouteInstanceNamespacePlan(namespaceInput);
    const RtSmokeStaticRouteTablePlan disabledPlan = BuildSmokeStaticRouteTablePlan(input);
    Check(disabledPlan.emittedRecords == 0 &&
        disabledPlan.skippedDisabled == 3 &&
        !disabledPlan.blocked,
        "static route table emits no records while static routes are disabled");

    namespaceInput.enableStaticRoutes = true;
    input.routeNamespace = BuildSmokeRouteInstanceNamespacePlan(namespaceInput);
    const RtSmokeStaticRouteTablePlan blockedPlan = BuildSmokeStaticRouteTablePlan(input);
    Check(blockedPlan.emittedRecords == 0 &&
        blockedPlan.skippedDisabled == 3 &&
        blockedPlan.blocked,
        "static route table emits no records when namespace blocks static routes");

    namespaceInput.shaderSupportsStaticBucketRoutes = true;
    input.routeNamespace = BuildSmokeRouteInstanceNamespacePlan(namespaceInput);
    const RtSmokeStaticRouteTablePlan routedPlan = BuildSmokeStaticRouteTablePlan(input);
    Check(routedPlan.emittedRecords == 2 &&
        routedPlan.skippedInvalid == 1 &&
        routedPlan.records[0].instanceId == 2 &&
        routedPlan.records[1].instanceId == 3 &&
        routedPlan.records[1].bucketKey == 20 &&
        routedPlan.records[1].range.vertexOffset == 12 &&
        routedPlan.tableSignature != 0,
        "static route table assigns deterministic instance IDs and preserves bucket ranges");

    input.maxRecords = 1;
    const RtSmokeStaticRouteTablePlan cappedPlan = BuildSmokeStaticRouteTablePlan(input);
    Check(cappedPlan.emittedRecords == 1 &&
        cappedPlan.overflow,
        "static route table reports record cap overflow");
}

void TestBvhDirtyPlan()
{
    RtSmokeBvhDirtyTokenState base;
    base.geometryContentSignature = 10;
    base.materialGeneration = 20;
    base.activeSetSignature = 30;
    base.tlasInstanceSignature = 40;

    RtSmokeBvhDirtyPlanInput input;
    input.previousValid = true;
    input.previous = base;
    input.current = base;
    const RtSmokeBvhDirtyPlan unchangedPlan = BuildSmokeBvhDirtyPlan(input);
    Check(!unchangedPlan.geometryContentChanged && !unchangedPlan.materialChanged &&
        !unchangedPlan.activeMembershipChanged && !unchangedPlan.tlasInstanceChanged &&
        !unchangedPlan.blasInputDirty && !unchangedPlan.tlasDirty,
        "BVH dirty plan keeps unchanged tokens clean");

    input.previousValid = false;
    const RtSmokeBvhDirtyPlan firstFramePlan = BuildSmokeBvhDirtyPlan(input);
    Check(firstFramePlan.geometryContentChanged && firstFramePlan.materialChanged &&
        firstFramePlan.activeMembershipChanged && firstFramePlan.tlasInstanceChanged &&
        firstFramePlan.blasInputDirty && firstFramePlan.tlasDirty,
        "BVH dirty plan treats invalid previous state as dirty");

    input.previousValid = true;
    input.current = base;
    input.current.geometryContentSignature = 11;
    const RtSmokeBvhDirtyPlan geometryPlan = BuildSmokeBvhDirtyPlan(input);
    Check(geometryPlan.geometryContentChanged && geometryPlan.blasInputDirty &&
        geometryPlan.tlasDirty && !geometryPlan.activeMembershipChanged,
        "BVH dirty plan maps geometry content changes to BLAS and TLAS work");

    input.current = base;
    input.current.materialGeneration = 21;
    const RtSmokeBvhDirtyPlan materialPlan = BuildSmokeBvhDirtyPlan(input);
    Check(materialPlan.materialChanged && materialPlan.blasInputDirty &&
        materialPlan.tlasDirty && !materialPlan.activeMembershipChanged,
        "BVH dirty plan maps material generation changes to BLAS and TLAS work");

    input.current = base;
    input.current.activeSetSignature = 31;
    const RtSmokeBvhDirtyPlan activePlan = BuildSmokeBvhDirtyPlan(input);
    Check(activePlan.activeMembershipChanged && activePlan.tlasDirty &&
        !activePlan.blasInputDirty && !activePlan.geometryContentChanged,
        "BVH dirty plan maps active-set changes to TLAS-only work");

    input.current = base;
    input.current.tlasInstanceSignature = 41;
    const RtSmokeBvhDirtyPlan tlasPlan = BuildSmokeBvhDirtyPlan(input);
    Check(tlasPlan.tlasInstanceChanged && tlasPlan.tlasDirty &&
        !tlasPlan.blasInputDirty && !tlasPlan.geometryContentChanged,
        "BVH dirty plan maps TLAS instance changes to TLAS-only work");

    RtSmokeStaticTlasBucketObservation buckets[2];
    buckets[0].bucketKey = 100;
    buckets[0].resident = true;
    buckets[0].active = true;
    buckets[0].hasBlas = true;
    buckets[0].activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_VISIBLE;
    buckets[0].residentSurfaceCount = 1;
    buckets[0].residentVertexCount = 10;
    buckets[0].residentIndexCount = 30;
    buckets[0].residentTriangleCount = 10;
    buckets[0].activeSurfaceCount = 1;
    buckets[0].activeVertexCount = 10;
    buckets[0].activeIndexCount = 30;
    buckets[0].activeTriangleCount = 10;

    buckets[1].bucketKey = 200;
    buckets[1].resident = true;
    buckets[1].active = false;
    buckets[1].hasBlas = true;
    buckets[1].residentSurfaceCount = 1;
    buckets[1].residentVertexCount = 20;
    buckets[1].residentIndexCount = 60;
    buckets[1].residentTriangleCount = 20;

    RtSmokeStaticTlasActiveSetPlanDesc activeSetDesc;
    activeSetDesc.buckets = buckets;
    activeSetDesc.bucketCount = 2;
    activeSetDesc.hasStaticBlas = true;
    activeSetDesc.monolithicStaticBlas = false;
    const RtSmokeStaticTlasActiveSetPlan baseActiveSetPlan = BuildSmokeStaticTlasActiveSetPlan(activeSetDesc);

    buckets[0].activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_VISIBLE | RT_SMOKE_STATIC_ACTIVE_SELECTED_AREA;
    const RtSmokeStaticTlasActiveSetPlan reasonChangedPlan = BuildSmokeStaticTlasActiveSetPlan(activeSetDesc);
    Check(baseActiveSetPlan.activeSetSignature != reasonChangedPlan.activeSetSignature &&
        baseActiveSetPlan.residentSetSignature == reasonChangedPlan.residentSetSignature,
        "static active-set signature tracks active reasons without changing resident signature");

    buckets[0].activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_VISIBLE;
    buckets[1].active = true;
    buckets[1].activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_RESIDENCY;
    buckets[1].activeSurfaceCount = buckets[1].residentSurfaceCount;
    buckets[1].activeVertexCount = buckets[1].residentVertexCount;
    buckets[1].activeIndexCount = buckets[1].residentIndexCount;
    buckets[1].activeTriangleCount = buckets[1].residentTriangleCount;
    const RtSmokeStaticTlasActiveSetPlan membershipChangedPlan = BuildSmokeStaticTlasActiveSetPlan(activeSetDesc);
    Check(baseActiveSetPlan.activeSetSignature != membershipChangedPlan.activeSetSignature &&
        baseActiveSetPlan.residentSetSignature == membershipChangedPlan.residentSetSignature,
        "static active-set signature tracks active membership without changing resident signature");
}

void TestStaticBucketWorkDirtyToken()
{
    RtSmokeStaticTlasBucketObservation buckets[2];
    buckets[0].bucketKey = 10;
    buckets[0].resident = true;
    buckets[0].active = true;
    buckets[0].hasBlas = true;
    buckets[0].activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_VISIBLE;
    buckets[0].routeRecordIndex = 0;
    buckets[0].residentVertexCount = 3;
    buckets[0].residentIndexCount = 3;
    buckets[0].residentTriangleCount = 1;
    buckets[0].activeVertexCount = 3;
    buckets[0].activeIndexCount = 3;
    buckets[0].activeTriangleCount = 1;

    buckets[1] = buckets[0];
    buckets[1].bucketKey = 20;
    buckets[1].active = false;
    buckets[1].routeRecordIndex = 1;
    buckets[1].residentVertexOffset = 3;
    buckets[1].residentIndexOffset = 3;
    buckets[1].residentTriangleOffset = 1;

    RtSmokeStaticBucketWorkPlanInput input;
    input.buckets = buckets;
    input.bucketCount = 2;
    input.geometryContentSignature = 100;
    input.materialGeneration = 200;
    input.totalVertexCount = 6;
    input.totalIndexCount = 6;
    input.totalTriangleCount = 2;
    input.enableStaticRoutes = true;
    input.shaderSupportsStaticBucketRoutes = true;
    const RtSmokeStaticBucketWorkPlan basePlan = BuildSmokeStaticBucketWorkPlan(input);

    RtSmokeStaticBucketWorkDirtyTokenInput tokenInput;
    tokenInput.plan = &basePlan;
    tokenInput.materialGeneration = input.materialGeneration;
    const RtSmokeBvhDirtyTokenState baseToken = BuildSmokeStaticBucketWorkDirtyToken(tokenInput);

    RtSmokeBvhDirtyPlanInput dirtyInput;
    dirtyInput.previousValid = true;
    dirtyInput.previous = baseToken;
    dirtyInput.current = baseToken;
    const RtSmokeBvhDirtyPlan unchangedPlan = BuildSmokeBvhDirtyPlan(dirtyInput);
    Check(!unchangedPlan.geometryContentChanged &&
        !unchangedPlan.activeGeometryContentChanged &&
        !unchangedPlan.residentSetChanged &&
        !unchangedPlan.blasInputDirty &&
        !unchangedPlan.tlasDirty,
        "static bucket work dirty token keeps unchanged plan clean");

    buckets[1].residentIndexCount = 6;
    const RtSmokeStaticBucketWorkPlan inactiveResidentChangedPlan = BuildSmokeStaticBucketWorkPlan(input);
    tokenInput.plan = &inactiveResidentChangedPlan;
    dirtyInput.current = BuildSmokeStaticBucketWorkDirtyToken(tokenInput);
    const RtSmokeBvhDirtyPlan inactiveResidentDirtyPlan = BuildSmokeBvhDirtyPlan(dirtyInput);
    Check(inactiveResidentDirtyPlan.geometryContentChanged &&
        inactiveResidentDirtyPlan.residentSetChanged &&
        !inactiveResidentDirtyPlan.activeGeometryContentChanged &&
        !inactiveResidentDirtyPlan.activeMembershipChanged &&
        !inactiveResidentDirtyPlan.blasInputDirty &&
        !inactiveResidentDirtyPlan.tlasDirty,
        "static bucket work dirty token keeps inactive resident geometry out of active BLAS and TLAS work");

    buckets[1].residentIndexCount = 3;
    buckets[1].active = true;
    buckets[1].activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_SELECTED_AREA;
    buckets[1].activeVertexCount = buckets[1].residentVertexCount;
    buckets[1].activeIndexCount = buckets[1].residentIndexCount;
    buckets[1].activeTriangleCount = buckets[1].residentTriangleCount;
    const RtSmokeStaticBucketWorkPlan activeChangedPlan = BuildSmokeStaticBucketWorkPlan(input);
    tokenInput.plan = &activeChangedPlan;
    dirtyInput.current = BuildSmokeStaticBucketWorkDirtyToken(tokenInput);
    const RtSmokeBvhDirtyPlan activeDirtyPlan = BuildSmokeBvhDirtyPlan(dirtyInput);
    Check(activeDirtyPlan.activeMembershipChanged &&
        activeDirtyPlan.tlasInstanceChanged &&
        activeDirtyPlan.tlasDirty &&
        !activeDirtyPlan.blasInputDirty &&
        !activeDirtyPlan.activeGeometryContentChanged &&
        !activeDirtyPlan.residentSetChanged &&
        !activeDirtyPlan.geometryContentChanged,
        "static bucket work dirty token maps active bucket changes to TLAS-only work");

    buckets[1].residentIndexCount = 6;
    buckets[1].activeIndexCount = 6;
    const RtSmokeStaticBucketWorkPlan geometryChangedPlan = BuildSmokeStaticBucketWorkPlan(input);
    tokenInput.plan = &geometryChangedPlan;
    dirtyInput.current = BuildSmokeStaticBucketWorkDirtyToken(tokenInput);
    const RtSmokeBvhDirtyPlan geometryDirtyPlan = BuildSmokeBvhDirtyPlan(dirtyInput);
    Check(geometryDirtyPlan.geometryContentChanged &&
        geometryDirtyPlan.activeGeometryContentChanged &&
        geometryDirtyPlan.blasInputDirty &&
        geometryDirtyPlan.tlasDirty,
        "static bucket work dirty token maps active resident geometry changes to BLAS work");

    input.materialGeneration = 201;
    const RtSmokeStaticBucketWorkPlan materialChangedPlan = BuildSmokeStaticBucketWorkPlan(input);
    tokenInput.plan = &materialChangedPlan;
    tokenInput.materialGeneration = input.materialGeneration;
    dirtyInput.current = BuildSmokeStaticBucketWorkDirtyToken(tokenInput);
    const RtSmokeBvhDirtyPlan materialDirtyPlan = BuildSmokeBvhDirtyPlan(dirtyInput);
    Check(materialDirtyPlan.materialChanged &&
        materialDirtyPlan.blasInputDirty &&
        materialDirtyPlan.tlasDirty,
        "static bucket work dirty token maps material changes to BLAS work");

    input.materialGeneration = 200;
    buckets[1].residentIndexCount = 3;
    buckets[1].activeIndexCount = 3;
    input.shaderSupportsStaticBucketRoutes = false;
    const RtSmokeStaticBucketWorkPlan routeChangedPlan = BuildSmokeStaticBucketWorkPlan(input);
    tokenInput.plan = &routeChangedPlan;
    tokenInput.materialGeneration = input.materialGeneration;
    dirtyInput.current = BuildSmokeStaticBucketWorkDirtyToken(tokenInput);
    const RtSmokeBvhDirtyPlan routeDirtyPlan = BuildSmokeBvhDirtyPlan(dirtyInput);
    Check(routeDirtyPlan.tlasInstanceChanged &&
        routeDirtyPlan.tlasDirty &&
        !routeDirtyPlan.blasInputDirty,
        "static bucket work dirty token maps route compatibility changes to TLAS-only work");
}

void TestBvhFrameToken()
{
    RtSmokeBvhFrameTokenInput input;
    input.staticBlasSignature = 100;
    input.geometryGeneration = 200;
    input.materialGeneration = 300;
    input.staticActiveSetSignature = 400;
    input.staticResidentSetSignature = 500;
    input.dynamicVertexCount = 10;
    input.dynamicIndexCount = 30;
    input.rigidRouteVertexCount = 20;
    input.rigidRouteIndexCount = 60;
    input.rigidRouteTriangleCount = 20;
    input.rigidRouteInstanceCount = 3;
    input.rigidRouteSeenThisFrameCount = 2;
    input.rigidRouteCachedInstanceCount = 1;
    input.baseTlasInstanceCount = 2;
    input.rigidTlasInstanceCount = 3;
    input.hasStaticBlas = true;
    input.hasDynamicBlas = true;

    const RtSmokeBvhFrameToken baseToken = BuildSmokeBvhFrameToken(input);
    Check(baseToken.dirtyToken.geometryContentSignature != 0 &&
        baseToken.dirtyToken.activeBlasInputSignature != 0 &&
        baseToken.dirtyToken.materialGeneration == 300 &&
        baseToken.dirtyToken.activeSetSignature != 0 &&
        baseToken.dirtyToken.tlasInstanceSignature != 0 &&
        baseToken.dirtyToken.residentSetSignature == 500 &&
        baseToken.residentSetSignature == 500,
        "BVH frame token emits geometry, active-BLAS, material, active, TLAS, and resident signatures");

    RtSmokeBvhFrameTokenInput activeChangedInput = input;
    activeChangedInput.rigidRouteInstanceCount = 4;
    const RtSmokeBvhFrameToken activeChangedToken = BuildSmokeBvhFrameToken(activeChangedInput);
    Check(baseToken.dirtyToken.geometryContentSignature == activeChangedToken.dirtyToken.geometryContentSignature &&
        baseToken.dirtyToken.activeSetSignature != activeChangedToken.dirtyToken.activeSetSignature &&
        baseToken.dirtyToken.tlasInstanceSignature != activeChangedToken.dirtyToken.tlasInstanceSignature,
        "BVH frame token maps rigid active membership changes to active and TLAS signatures");

    RtSmokeBvhFrameTokenInput geometryChangedInput = input;
    geometryChangedInput.rigidRouteIndexCount = 63;
    const RtSmokeBvhFrameToken geometryChangedToken = BuildSmokeBvhFrameToken(geometryChangedInput);
    Check(baseToken.dirtyToken.geometryContentSignature != geometryChangedToken.dirtyToken.geometryContentSignature &&
        baseToken.dirtyToken.activeSetSignature == geometryChangedToken.dirtyToken.activeSetSignature,
        "BVH frame token maps geometry count changes to geometry signature only");

    RtSmokeBvhFrameTokenInput tlasChangedInput = input;
    tlasChangedInput.rigidTlasInstanceCount = 4;
    const RtSmokeBvhFrameToken tlasChangedToken = BuildSmokeBvhFrameToken(tlasChangedInput);
    Check(baseToken.dirtyToken.geometryContentSignature == tlasChangedToken.dirtyToken.geometryContentSignature &&
        baseToken.dirtyToken.activeSetSignature == tlasChangedToken.dirtyToken.activeSetSignature &&
        baseToken.dirtyToken.tlasInstanceSignature != tlasChangedToken.dirtyToken.tlasInstanceSignature,
        "BVH frame token maps submitted TLAS count changes to TLAS signature only");
}

void TestBvhFramePlanningResult()
{
    RtSmokeStaticTlasBucketObservation buckets[2];
    buckets[0].bucketKey = 100;
    buckets[0].resident = true;
    buckets[0].active = true;
    buckets[0].hasBlas = true;
    buckets[0].activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_SELECTED_AREA;
    buckets[0].residentVertexCount = 10;
    buckets[0].residentIndexCount = 30;
    buckets[0].residentTriangleCount = 10;
    buckets[0].activeVertexCount = 10;
    buckets[0].activeIndexCount = 30;
    buckets[0].activeTriangleCount = 10;
    buckets[1] = buckets[0];
    buckets[1].bucketKey = 200;
    buckets[1].active = false;

    RtSmokeBvhFramePlanningInput input;
    input.staticBucketWorkInput.buckets = buckets;
    input.staticBucketWorkInput.bucketCount = 2;
    input.staticBucketWorkInput.geometryContentSignature = 900;
    input.staticBucketWorkInput.materialGeneration = 901;
    input.staticBucketWorkInput.totalVertexCount = 20;
    input.staticBucketWorkInput.totalIndexCount = 60;
    input.staticBucketWorkInput.totalTriangleCount = 20;
    input.staticBucketWorkInput.monolithicStaticBlas = true;
    input.staticBucketWorkInput.hasStaticBlas = true;
    input.frameTokenInput.staticBlasSignature = 902;
    input.frameTokenInput.geometryGeneration = 903;
    input.frameTokenInput.materialGeneration = 904;
    input.frameTokenInput.dynamicVertexCount = 3;
    input.frameTokenInput.dynamicIndexCount = 3;
    input.frameTokenInput.rigidRouteInstanceCount = 1;
    input.frameTokenInput.baseTlasInstanceCount = 2;
    input.frameTokenInput.rigidTlasInstanceCount = 1;
    input.frameTokenInput.hasStaticBlas = true;
    input.frameTokenInput.hasDynamicBlas = true;

    const RtSmokeBvhFramePlanningResult firstResult =
        BuildSmokeBvhFramePlanningResult(input);
    RtSmokeBvhFrameTokenInput expectedFrameTokenInput = input.frameTokenInput;
    expectedFrameTokenInput.staticActiveSetSignature =
        firstResult.staticBucketWorkPlan.activeSetPlan.activeSetSignature;
    expectedFrameTokenInput.staticResidentSetSignature =
        firstResult.staticBucketWorkPlan.activeSetPlan.residentSetSignature;
    const RtSmokeBvhFrameToken expectedFrameToken =
        BuildSmokeBvhFrameToken(expectedFrameTokenInput);
    Check(firstResult.staticBucketWorkPlan.activeSetPlan.activeBuckets == 1,
        "BVH frame planning result preserves active bucket count");
    Check(firstResult.staticBucketWorkPlan.activeSetPlan.inactiveResidentBuckets == 1,
        "BVH frame planning result preserves inactive resident bucket count");
    Check(firstResult.frameToken.dirtyToken.activeSetSignature ==
            expectedFrameToken.dirtyToken.activeSetSignature &&
        firstResult.frameToken.dirtyToken.residentSetSignature ==
            expectedFrameToken.dirtyToken.residentSetSignature,
        "BVH frame planning result composes static bucket work into frame token");
    Check(firstResult.dirtyPlan.blasInputDirty && firstResult.dirtyPlan.tlasDirty,
        "BVH frame planning result treats missing previous token as dirty");

    input.previousDirtyTokenValid = true;
    input.previousDirtyToken = firstResult.frameToken.dirtyToken;
    const RtSmokeBvhFramePlanningResult unchangedResult =
        BuildSmokeBvhFramePlanningResult(input);
    Check(!unchangedResult.dirtyPlan.blasInputDirty &&
        !unchangedResult.dirtyPlan.tlasDirty,
        "BVH frame planning result keeps unchanged inputs clean");

    buckets[1].active = true;
    const RtSmokeBvhFramePlanningResult activeChangedResult =
        BuildSmokeBvhFramePlanningResult(input);
    Check(!activeChangedResult.dirtyPlan.blasInputDirty &&
        activeChangedResult.dirtyPlan.tlasDirty,
        "BVH frame planning result maps active bucket changes to TLAS work");
}

void TestBvhBucketableSignatures()
{
    RtSmokeStaticBvhBucketSignatureInput staticInput;
    staticInput.geometryContentSignature = 1000;
    staticInput.materialGeneration = 2000;
    staticInput.bucket.bucketKey = 77;
    staticInput.bucket.resident = true;
    staticInput.bucket.active = true;
    staticInput.bucket.activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_VISIBLE;
    staticInput.bucket.routeRecordIndex = 3;
    staticInput.bucket.residentSurfaceCount = 2;
    staticInput.bucket.residentVertexCount = 12;
    staticInput.bucket.residentIndexCount = 36;
    staticInput.bucket.residentTriangleCount = 12;
    staticInput.bucket.activeSurfaceCount = 1;
    staticInput.bucket.activeVertexCount = 6;
    staticInput.bucket.activeIndexCount = 18;
    staticInput.bucket.activeTriangleCount = 6;
    const RtSmokeStaticBvhBucketSignature baseStaticSignature =
        BuildSmokeStaticBvhBucketSignature(staticInput);
    Check(baseStaticSignature.bucketKey == 77 && baseStaticSignature.resident &&
        baseStaticSignature.active && baseStaticSignature.residentSignature != 0 &&
        baseStaticSignature.activeSignature != 0 && baseStaticSignature.blasInputSignature != 0,
        "static BVH bucket signature preserves bucket identity and state");

    RtSmokeStaticBvhBucketSignatureInput activeChangedInput = staticInput;
    activeChangedInput.bucket.activeReasonFlags = RT_SMOKE_STATIC_ACTIVE_VISIBLE | RT_SMOKE_STATIC_ACTIVE_FORCE_INCLUDE;
    const RtSmokeStaticBvhBucketSignature activeChangedSignature =
        BuildSmokeStaticBvhBucketSignature(activeChangedInput);
    Check(baseStaticSignature.activeSignature != activeChangedSignature.activeSignature &&
        baseStaticSignature.residentSignature == activeChangedSignature.residentSignature &&
        baseStaticSignature.blasInputSignature == activeChangedSignature.blasInputSignature,
        "static BVH bucket active changes do not dirty resident or BLAS signatures");

    RtSmokeStaticBvhBucketSignatureInput geometryChangedInput = staticInput;
    geometryChangedInput.geometryContentSignature = 1001;
    const RtSmokeStaticBvhBucketSignature geometryChangedSignature =
        BuildSmokeStaticBvhBucketSignature(geometryChangedInput);
    Check(baseStaticSignature.blasInputSignature != geometryChangedSignature.blasInputSignature &&
        baseStaticSignature.activeSignature == geometryChangedSignature.activeSignature &&
        baseStaticSignature.residentSignature == geometryChangedSignature.residentSignature,
        "static BVH bucket geometry changes dirty BLAS signature without changing active membership");

    RtSmokeRigidBvhObjectSignatureInput rigidInput;
    rigidInput.meshHash = 500;
    rigidInput.instanceId = 600;
    rigidInput.geometryContentSignature = 700;
    rigidInput.materialGeneration = 800;
    rigidInput.sourceFlags = 0x2;
    rigidInput.rigidSourceMask = 0x2;
    rigidInput.routeRecordIndex = 9;
    rigidInput.vertexCount = 24;
    rigidInput.indexCount = 72;
    rigidInput.hasMeshRecord = true;
    rigidInput.meshSeenThisFrame = true;
    const RtSmokeRigidBvhObjectSignature baseRigidSignature =
        BuildSmokeRigidBvhObjectSignature(rigidInput);
    Check(baseRigidSignature.resident && baseRigidSignature.activeCandidate &&
        baseRigidSignature.objectKey != 0 && baseRigidSignature.blasInputSignature != 0 &&
        baseRigidSignature.tlasMembershipSignature != 0,
        "rigid BVH object signature preserves object identity and active state");

    RtSmokeRigidBvhObjectSignatureInput secondRigidInstanceInput = rigidInput;
    secondRigidInstanceInput.instanceId = 601;
    const RtSmokeRigidBvhObjectSignature secondRigidInstanceSignature =
        BuildSmokeRigidBvhObjectSignature(secondRigidInstanceInput);
    Check(baseRigidSignature.objectKey != secondRigidInstanceSignature.objectKey &&
        baseRigidSignature.blasInputSignature == secondRigidInstanceSignature.blasInputSignature &&
        baseRigidSignature.tlasMembershipSignature != secondRigidInstanceSignature.tlasMembershipSignature,
        "rigid BVH object signature shares mesh BLAS inputs across distinct instances");

    RtSmokeRigidBvhObjectSignatureInput staleResidentInput = rigidInput;
    staleResidentInput.meshSeenThisFrame = false;
    staleResidentInput.residencyEnabled = true;
    const RtSmokeRigidBvhObjectSignature staleResidentSignature =
        BuildSmokeRigidBvhObjectSignature(staleResidentInput);
    Check(staleResidentSignature.resident && staleResidentSignature.activeCandidate &&
        baseRigidSignature.blasInputSignature == staleResidentSignature.blasInputSignature,
        "rigid BVH object signature keeps residency separate from BLAS input identity");

    RtSmokeRigidBvhObjectSignatureInput inactiveRigidInput = rigidInput;
    inactiveRigidInput.sourceFlags = 0x4;
    const RtSmokeRigidBvhObjectSignature inactiveRigidSignature =
        BuildSmokeRigidBvhObjectSignature(inactiveRigidInput);
    Check(inactiveRigidSignature.resident && !inactiveRigidSignature.activeCandidate &&
        baseRigidSignature.blasInputSignature == inactiveRigidSignature.blasInputSignature,
        "rigid BVH object signature separates resident objects from active TLAS candidates");
}

void TestUploadPlan()
{
    const RtSmokeUploadPlanMetadata fullUpload = BuildSmokeVectorUploadPlanMetadata(10, 4, false, -1, 0);
    Check(!fullUpload.skip && fullUpload.byteSize == 40 && fullUpload.sourceOffsetBytes == 0, "upload plan emits full upload metadata");

    const RtSmokeUploadPlanMetadata dirtyUpload = BuildSmokeVectorUploadPlanMetadata(10, 4, false, 2, 3);
    Check(!dirtyUpload.skip && dirtyUpload.byteSize == 12 && dirtyUpload.sourceOffsetBytes == 8 && dirtyUpload.destOffsetBytes == 8, "upload plan emits dirty-range metadata");

    const RtSmokeUploadPlanMetadata invalidDirtyUpload = BuildSmokeVectorUploadPlanMetadata(10, 4, false, 8, 8);
    Check(!invalidDirtyUpload.skip && invalidDirtyUpload.byteSize == 40 && invalidDirtyUpload.sourceOffsetBytes == 0, "upload plan falls back to full upload for invalid dirty ranges");

    const RtSmokeUploadPlanMetadata skippedUpload = BuildSmokeVectorUploadPlanMetadata(10, 4, true, 2, 3);
    Check(skippedUpload.skip && skippedUpload.byteSize == 0, "upload plan preserves explicit skip");

    const RtSmokeUploadPlanMetadata zeroUpload = BuildSmokeVectorUploadPlanMetadata(0, 4, false, -1, 0);
    Check(!zeroUpload.skip && zeroUpload.byteSize == 0, "upload plan preserves zero-byte non-skip barriers");

    RtSmokeStaticDirtyUploadPlanInput dirtyPlanInput;
    dirtyPlanInput.staticCacheChanged = true;
    dirtyPlanInput.staticGeometryBuffersReused = true;
    dirtyPlanInput.staticDirtyCount = 1;
    dirtyPlanInput.dirtyVertexOffset = 2;
    dirtyPlanInput.dirtyVertexCount = 3;
    dirtyPlanInput.totalVertexCount = 10;
    dirtyPlanInput.dirtyIndexOffset = 3;
    dirtyPlanInput.dirtyIndexCount = 6;
    dirtyPlanInput.totalIndexCount = 18;
    dirtyPlanInput.dirtyTriangleOffset = 1;
    dirtyPlanInput.dirtyTriangleCount = 2;
    dirtyPlanInput.totalTriangleClassCount = 6;
    dirtyPlanInput.totalTriangleMaterialCount = 6;
    const RtSmokeStaticDirtyUploadPlan dirtyPlan = BuildSmokeStaticDirtyUploadPlan(dirtyPlanInput);
    Check(dirtyPlan.dirtyRangesValid && dirtyPlan.useDirtyRangeUploads, "static dirty upload plan accepts valid reused dirty ranges");

    RtSmokeStaticDirtyUploadPlanInput cacheHitDirtyPlanInput = dirtyPlanInput;
    cacheHitDirtyPlanInput.staticBlasCacheHit = true;
    const RtSmokeStaticDirtyUploadPlan cacheHitDirtyPlan = BuildSmokeStaticDirtyUploadPlan(cacheHitDirtyPlanInput);
    Check(cacheHitDirtyPlan.dirtyRangesValid && !cacheHitDirtyPlan.useDirtyRangeUploads, "static dirty upload plan skips dirty ranges on static BLAS cache hit");

    RtSmokeStaticDirtyUploadPlanInput invalidDirtyPlanInput = dirtyPlanInput;
    invalidDirtyPlanInput.dirtyTriangleCount = 99;
    const RtSmokeStaticDirtyUploadPlan invalidDirtyPlan = BuildSmokeStaticDirtyUploadPlan(invalidDirtyPlanInput);
    Check(!invalidDirtyPlan.dirtyRangesValid && !invalidDirtyPlan.useDirtyRangeUploads, "static dirty upload plan rejects invalid dirty ranges");

    const uint32_t spanA[] = { 1, 2, 3, 4 };
    const uint32_t spanB[] = { 5, 6 };
    const RtSmokePlanDataSpan spans[] = {
        { spanA, sizeof(spanA[0]), 4 },
        { spanB, sizeof(spanB[0]), 2 }
    };
    const uint64_t spanSignature = BuildSmokePlanDataSpanSignature(spans, 2);
    Check(spanSignature == BuildSmokePlanDataSpanSignature(spans, 2), "data span upload signature is deterministic");
    const RtSmokePlanDataSpan truncatedSpans[] = {
        { spanA, sizeof(spanA[0]), 3 },
        { spanB, sizeof(spanB[0]), 2 }
    };
    Check(spanSignature != BuildSmokePlanDataSpanSignature(truncatedSpans, 2), "data span upload signature tracks element counts");

    RtSmokePreviousStaticSnapshotUploadPlanInput previousStaticInput;
    previousStaticInput.dataAvailable = true;
    previousStaticInput.buffersReused = true;
    previousStaticInput.previousUploadSignature = spanSignature;
    previousStaticInput.currentUploadSignature = spanSignature;
    Check(BuildSmokePreviousStaticSnapshotUploadPlan(previousStaticInput).skipUpload, "previous static snapshot upload plan skips matching reused snapshot");
    previousStaticInput.currentUploadSignature = spanSignature + 1;
    Check(!BuildSmokePreviousStaticSnapshotUploadPlan(previousStaticInput).skipUpload, "previous static snapshot upload plan uploads changed snapshot");
    previousStaticInput.currentUploadSignature = spanSignature;
    previousStaticInput.buffersReused = false;
    Check(!BuildSmokePreviousStaticSnapshotUploadPlan(previousStaticInput).skipUpload, "previous static snapshot upload plan requires reused buffers");
}

void TestGenerationAcceptance()
{
    RtPathTraceCpuWorkState state;
    RtPathTraceCpuWorkGeneration expected;
    expected.frameIndex = 5;
    expected.sceneGeneration = 1;
    expected.geometryGeneration = 2;
    expected.materialGeneration = 3;
    expected.lightGeneration = 4;
    RtPathTraceCpuWorkPublishSnapshot(state, expected);

    RtPathTraceCpuWorkResultEnvelope stale;
    stale.completed = true;
    stale.generation = expected;
    stale.generation.geometryGeneration = 99;
    const RtPathTraceCpuWorkFrameDecision staleDecision =
        RtPathTraceCpuWorkAcceptLatest(state, expected, &stale, true);
    Check(staleDecision.staleRejected && staleDecision.syncFallback, "stale generation tokens are rejected");

    const RtPathTraceCpuWorkFrameDecision lateDecision =
        RtPathTraceCpuWorkAcceptLatest(state, expected, nullptr, true);
    Check(lateDecision.lateFallback && lateDecision.syncFallback, "late results fall back without blocking");

    RtPathTraceCpuWorkResultEnvelope current;
    current.completed = true;
    current.generation = expected;
    RtPathTraceCpuWorkPublishCompletedResult(state, current);
    const RtPathTraceCpuWorkFrameDecision acceptedDecision =
        RtPathTraceCpuWorkAcceptLatest(state, expected, nullptr, true);
    Check(acceptedDecision.accepted && state.acceptedResultCount == 1, "matching generation result is accepted");
    Check(state.currentResult.valid && !state.pendingResult.valid, "accepted CPU work result becomes current and clears pending");

    const RtPathTraceCpuWorkFrameDecision reusedCurrentDecision =
        RtPathTraceCpuWorkAcceptLatest(state, expected, nullptr, true);
    Check(reusedCurrentDecision.accepted && reusedCurrentDecision.reusedCurrent && !reusedCurrentDecision.lateFallback, "matching current CPU work result is reused without late fallback");

    RtPathTraceCpuWorkGeneration nextExpected = expected;
    nextExpected.frameIndex = 6;
    RtPathTraceCpuWorkPublishSnapshot(state, nextExpected);
    RtPathTraceCpuWorkResultEnvelope nextCurrent;
    nextCurrent.completed = true;
    nextCurrent.generation = nextExpected;
    RtPathTraceCpuWorkPublishCompletedResult(state, nextCurrent);
    const RtPathTraceCpuWorkFrameDecision nextAcceptedDecision =
        RtPathTraceCpuWorkAcceptLatest(state, nextExpected, nullptr, true);
    Check(nextAcceptedDecision.accepted && state.previousResult.valid, "accepted CPU work result preserves previous result slot");
    Check(RtPathTraceCpuWorkRecordRenderSubmit(state, nextExpected, 3.5), "render submit timing records against current generation");
    Check(state.lastAcceptedTiming.renderSubmitMs == 3.5, "render submit timing is retained on accepted result");
    Check(!RtPathTraceCpuWorkRecordRenderSubmit(state, expected, 9.0), "render submit timing rejects stale generation");
    Check(state.rejectedStaleResultCount == 1 && state.lateResultCount == 1 && state.syncFallbackCount == 2, "CPU work ownership counters track stale and late fallbacks");

    RtPathTraceCpuWorkGeneration stalePendingExpected = nextExpected;
    stalePendingExpected.frameIndex = 7;
    RtPathTraceCpuWorkPublishSnapshot(state, stalePendingExpected);
    RtPathTraceCpuWorkResultEnvelope stalePending;
    stalePending.completed = true;
    stalePending.generation = nextExpected;
    RtPathTraceCpuWorkPublishCompletedResult(state, stalePending);
    const RtPathTraceCpuWorkFrameDecision stalePendingDecision =
        RtPathTraceCpuWorkAcceptLatest(state, stalePendingExpected, nullptr, true);
    Check(stalePendingDecision.staleRejected && !state.pendingResult.valid && !state.hasPending, "stale pending CPU work result is discarded after rejection");
}

void RunStressMode(int iterations)
{
    const int safeIterations = iterations > 0 ? iterations : 100;
    const int triangleCount = 8192;
    const int vertexCount = triangleCount * 3;
    const int indexCount = triangleCount * 3;

    std::vector<HarnessSmokeVertex> vertices(vertexCount);
    std::vector<uint32_t> indexes(indexCount);
    std::vector<uint32_t> classes(triangleCount);
    std::vector<uint32_t> materials(triangleCount);
    for (int vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
    {
        vertices[vertexIndex].position[0] = static_cast<float>(vertexIndex % 127) * 0.125f;
        vertices[vertexIndex].position[1] = static_cast<float>((vertexIndex / 3) % 113) * 0.25f;
        vertices[vertexIndex].position[2] = static_cast<float>(vertexIndex % 17) * 0.5f;
        vertices[vertexIndex].position[3] = 1.0f;
        vertices[vertexIndex].normal[2] = 1.0f;
        vertices[vertexIndex].texCoord[0] = static_cast<float>(vertexIndex % 31) * 0.01f;
        indexes[vertexIndex] = static_cast<uint32_t>(vertexIndex);
    }
    for (int triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex)
    {
        classes[triangleIndex] = static_cast<uint32_t>(triangleIndex & 7);
        materials[triangleIndex] = static_cast<uint32_t>((triangleIndex % 257) + 1);
    }

    RtSmokeAccelerationPlanInput input = BuildPlanInput(vertices, indexes, classes, materials);
    input.staticCache.staticCacheChanged = true;
    input.staticCache.cacheValid = true;
    input.staticCache.cacheResourcesReady = true;

    const auto planStart = std::chrono::steady_clock::now();
    uint64_t hashAccumulator = 0;
    for (int iteration = 0; iteration < safeIterations; ++iteration)
    {
        RtSmokeAccelerationPlan plan = BuildSmokeAccelerationPlan(input);
        hashAccumulator ^= plan.staticSignature.hash + static_cast<uint64_t>(iteration);
    }
    const auto planEnd = std::chrono::steady_clock::now();

    const int rigidObservationCount = 4096;
    RtSmokeRigidTlasPlanDesc rigidDesc;
    rigidDesc.rigidSourceMask = 0x2;
    rigidDesc.firstInstanceId = 2;
    rigidDesc.instanceMask = 0x02;
    rigidDesc.maxInstances = rigidObservationCount;

    const auto rigidStart = std::chrono::steady_clock::now();
    int emittedAccumulator = 0;
    for (int iteration = 0; iteration < safeIterations; ++iteration)
    {
        RtSmokeRigidTlasPlan rigidPlan;
        rigidPlan.instances.reserve(rigidObservationCount);
        for (int observationIndex = 0; observationIndex < rigidObservationCount; ++observationIndex)
        {
            RtSmokeRigidTlasObservation observation;
            observation.meshHash = static_cast<uint64_t>(observationIndex + 1);
            observation.instanceId = static_cast<uint64_t>(iteration * rigidObservationCount + observationIndex);
            observation.sourceFlags = observationIndex % 8 == 0 ? 0u : 0x2u;
            observation.hasMeshRecord = observationIndex % 11 != 0;
            observation.meshSeenThisFrame = observationIndex % 13 != 0;
            observation.residencyEnabled = observationIndex % 17 == 0;
            observation.hasBlas = observationIndex % 19 != 0;
            observation.routeRecordIndex = static_cast<uint32_t>(observationIndex);
            observation.objectToWorld[0] = 1.0f;
            observation.objectToWorld[5] = 1.0f;
            observation.objectToWorld[10] = 1.0f;
            observation.objectToWorld[15] = 1.0f;
            if (!AppendSmokeRigidTlasPlanObservation(rigidPlan, rigidDesc, observation))
            {
                break;
            }
        }
        emittedAccumulator += rigidPlan.emittedInstances;
    }
    const auto rigidEnd = std::chrono::steady_clock::now();

    const double planMs = std::chrono::duration<double, std::milli>(planEnd - planStart).count();
    const double rigidMs = std::chrono::duration<double, std::milli>(rigidEnd - rigidStart).count();
    std::cout << "[STRESS] iterations=" << safeIterations
        << " staticTriangles=" << triangleCount
        << " staticPlanMs=" << planMs
        << " staticPlanAvgMs=" << (planMs / static_cast<double>(safeIterations))
        << " rigidObservations=" << rigidObservationCount
        << " rigidPlanMs=" << rigidMs
        << " rigidPlanAvgMs=" << (rigidMs / static_cast<double>(safeIterations))
        << " hashAccumulator=" << hashAccumulator
        << " emittedAccumulator=" << emittedAccumulator
        << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    TestStaticSignature();
    TestStaticSignatureRanges();
    TestCacheAndBaseTlas();
    TestOwnedSnapshot();
    TestAccelerationPlanInputToken();
    TestAsyncSnapshotPlanning();
    TestRigidPlan();
    TestAsyncRigidTlasPlanning();
    TestRigidBlasBuildPlan();
    TestStaticBucketBlasBuildPlan();
    TestStaticBucketBlasBuildBatchPlan();
    TestStaticBucketBlasBuildObservationPlan();
    TestStaticBucketWorkPlan();
    TestStaticBucketRigidRouteNamespaceComposition();
    TestStaticBucketWorkPlanSnapshot();
    TestStaticBucketWorkPlanTimedResult();
    TestStaticBucketWorkPlanInputToken();
    TestAsyncStaticBucketWorkPlanning();
    TestStaticActiveSetPlan();
    TestStaticBucketObservation();
    TestStaticBucketBlasPlan();
    TestStaticBucketTraversalCompatibility();
    TestRouteInstanceNamespacePlan();
    TestStaticRouteTablePlan();
    TestBvhDirtyPlan();
    TestStaticBucketWorkDirtyToken();
    TestBvhFrameToken();
    TestBvhFramePlanningResult();
    TestBvhBucketableSignatures();
    TestUploadPlan();
    TestGenerationAcceptance();

    if (g_failures != 0)
    {
        std::cout << "PT CPU planning harness failed: " << g_failures << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "PT CPU planning harness passed\n";
    if (argc >= 2 && std::string(argv[1]) == "--stress")
    {
        const int iterations = argc >= 3 ? std::atoi(argv[2]) : 100;
        RunStressMode(iterations);
    }
    return EXIT_SUCCESS;
}
