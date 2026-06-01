#pragma once

// CPU-only envelope for future PT worker jobs.
//
// This file deliberately avoids renderer, NVRHI, and game object dependencies.
// Render-thread code owns snapshot capture and GPU submission; worker results
// are accepted only when their immutable generation tokens still match.

#include <cstdint>

struct RtPathTraceCpuWorkGeneration
{
    uint64_t frameIndex = 0;
    uint64_t sceneGeneration = 0;
    uint64_t geometryGeneration = 0;
    uint64_t materialGeneration = 0;
    uint64_t lightGeneration = 0;
};

struct RtPathTraceCpuWorkTiming
{
    double snapshotCaptureMs = 0.0;
    double queueWaitMs = 0.0;
    double workerExecutionMs = 0.0;
    double renderSubmitMs = 0.0;
};

struct RtPathTraceCpuWorkResultEnvelope
{
    RtPathTraceCpuWorkGeneration generation;
    RtPathTraceCpuWorkTiming timing;
    bool completed = false;
};

struct RtPathTraceCpuWorkResultSlot
{
    RtPathTraceCpuWorkResultEnvelope result;
    bool valid = false;
};

struct RtPathTraceCpuWorkFrameDecision
{
    bool accepted = false;
    bool staleRejected = false;
    bool lateFallback = false;
    bool syncFallback = false;
};

struct RtPathTraceCpuWorkState
{
    RtPathTraceCpuWorkGeneration currentGeneration;
    RtPathTraceCpuWorkGeneration pendingGeneration;
    RtPathTraceCpuWorkGeneration acceptedGeneration;
    RtPathTraceCpuWorkResultSlot currentResult;
    RtPathTraceCpuWorkResultSlot previousResult;
    RtPathTraceCpuWorkResultSlot pendingResult;
    RtPathTraceCpuWorkTiming lastAcceptedTiming;
    uint64_t acceptedResultCount = 0;
    uint64_t rejectedStaleResultCount = 0;
    uint64_t lateResultCount = 0;
    uint64_t syncFallbackCount = 0;
    bool hasPending = false;
    bool hasAccepted = false;
};

bool RtPathTraceCpuWorkGenerationEquals(
    const RtPathTraceCpuWorkGeneration& a,
    const RtPathTraceCpuWorkGeneration& b);

void RtPathTraceCpuWorkPublishSnapshot(
    RtPathTraceCpuWorkState& state,
    const RtPathTraceCpuWorkGeneration& generation);

void RtPathTraceCpuWorkPublishCompletedResult(
    RtPathTraceCpuWorkState& state,
    const RtPathTraceCpuWorkResultEnvelope& result);

RtPathTraceCpuWorkFrameDecision RtPathTraceCpuWorkAcceptLatest(
    RtPathTraceCpuWorkState& state,
    const RtPathTraceCpuWorkGeneration& expectedGeneration,
    const RtPathTraceCpuWorkResultEnvelope* latestResult,
    bool synchronousFallbackAllowed);

bool RtPathTraceCpuWorkRecordRenderSubmit(
    RtPathTraceCpuWorkState& state,
    const RtPathTraceCpuWorkGeneration& generation,
    double renderSubmitMs);
