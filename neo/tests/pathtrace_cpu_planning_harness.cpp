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

    std::future<RtSmokeAccelerationPlanResult> future = std::async(
        std::launch::async,
        [snapshot]() {
            return BuildSmokeAccelerationPlanResult(snapshot);
        });

    vertices[2].position[0] = 42.0f;
    const RtPathTraceCpuWorkFrameDecision lateDecision =
        RtPathTraceCpuWorkAcceptLatest(state, generation, nullptr, true);
    Check(lateDecision.lateFallback && lateDecision.syncFallback, "late async acceleration plan requests synchronous fallback");

    RtPathTraceCpuWorkResultEnvelope envelope;
    const RtSmokeAccelerationPlanResult asyncResult = future.get();
    envelope.completed = asyncResult.valid;
    envelope.generation = generation;
    envelope.timing.workerExecutionMs = 1.0;
    RtPathTraceCpuWorkPublishCompletedResult(state, envelope);
    const RtPathTraceCpuWorkFrameDecision acceptDecision =
        RtPathTraceCpuWorkAcceptLatest(state, generation, nullptr, true);
    Check(acceptDecision.accepted, "matching async acceleration plan generation is accepted");
    Check(asyncResult.plan.staticSignature.hash == directPlan.staticSignature.hash, "async acceleration plan uses owned immutable snapshot data");

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
    Check(plan.instances[0].instanceId == 2 && plan.instances[0].meshHash == 100, "rigid TLAS emitted instance metadata is deterministic");

    const RtSmokeRigidTlasPlanSnapshot snapshot = CaptureSmokeRigidTlasPlanSnapshot(desc);
    observations[0].hasBlas = false;
    observations[0].meshHash = 999;
    const RtSmokeRigidTlasPlan snapshotPlan = BuildSmokeRigidTlasPlan(snapshot);
    Check(snapshotPlan.emittedInstances == 1 && snapshotPlan.instances[0].meshHash == 100, "owned rigid TLAS snapshot is immutable after source mutation");

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
