#include "precompiled.h"
#pragma hdrstop

// Optional center-pixel/full-frame debug readback for the smoke output texture.
//
// Readback is deliberately delayed and throttled so diagnostic logging can sample
// GPU output without becoming part of the normal frame path.

#include "PathTraceCVars.h"
#include "PathTracePrimaryPass.h"
#include "PathTraceDebugDumps.h"
#include "../../sys/DeviceManager.h"

extern DeviceManager* deviceManager;

namespace {

const int RT_SMOKE_READBACK_INTERVAL_FRAMES = 120;
int g_smokeLastReadbackTimingLogMs = -1000000;

enum class RigidRouteOverlapBucket
{
    Match,
    MaterialMismatch,
    ClassMismatch,
    RigidOnly,
    RigidInFront,
    FallbackInFront,
    FallbackOnly,
    Neither,
    Unknown
};

struct RigidRouteOverlapCounts
{
    int match = 0;
    int materialMismatch = 0;
    int classMismatch = 0;
    int rigidOnly = 0;
    int rigidInFront = 0;
    int fallbackInFront = 0;
    int fallbackOnly = 0;
    int neither = 0;
    int unknown = 0;
};

RigidRouteOverlapBucket ClassifyRigidRouteOverlapColor(const float* rgba)
{
    const bool rHigh = rgba[0] > 0.75f;
    const bool gHigh = rgba[1] > 0.75f;
    const bool bHigh = rgba[2] > 0.75f;
    const bool gMid = rgba[1] > 0.30f;
    const bool bMid = rgba[2] > 0.35f;
    const bool dimGray = rgba[0] > 0.08f && rgba[0] < 0.32f && rgba[1] > 0.08f && rgba[1] < 0.32f && rgba[2] > 0.08f && rgba[2] < 0.32f;
    const bool black = rgba[0] < 0.04f && rgba[1] < 0.04f && rgba[2] < 0.04f;

    if (!rHigh && gHigh && !bHigh)
    {
        return RigidRouteOverlapBucket::Match;
    }
    if (rHigh && gHigh && !bHigh)
    {
        return RigidRouteOverlapBucket::MaterialMismatch;
    }
    if (rHigh && !gHigh && bHigh)
    {
        return RigidRouteOverlapBucket::ClassMismatch;
    }
    if (!rHigh && gHigh && bHigh)
    {
        return RigidRouteOverlapBucket::RigidOnly;
    }
    if (!rHigh && !gHigh && bHigh)
    {
        return RigidRouteOverlapBucket::RigidInFront;
    }
    if (rHigh && gMid && !bMid)
    {
        return RigidRouteOverlapBucket::FallbackInFront;
    }
    if (dimGray)
    {
        return RigidRouteOverlapBucket::FallbackOnly;
    }
    if (black)
    {
        return RigidRouteOverlapBucket::Neither;
    }
    return RigidRouteOverlapBucket::Unknown;
}

const char* RigidRouteOverlapBucketName(RigidRouteOverlapBucket bucket)
{
    switch (bucket)
    {
        case RigidRouteOverlapBucket::Match: return "match/green";
        case RigidRouteOverlapBucket::MaterialMismatch: return "materialMismatch/yellow";
        case RigidRouteOverlapBucket::ClassMismatch: return "classMismatch/magenta";
        case RigidRouteOverlapBucket::RigidOnly: return "rigidOnly/cyan";
        case RigidRouteOverlapBucket::RigidInFront: return "rigidInFront/blue";
        case RigidRouteOverlapBucket::FallbackInFront: return "fallbackInFront/orange";
        case RigidRouteOverlapBucket::FallbackOnly: return "fallbackOnly/gray";
        case RigidRouteOverlapBucket::Neither: return "neither/black";
        default: return "unknown";
    }
}

void AccumulateRigidRouteOverlapBucket(RigidRouteOverlapCounts& counts, RigidRouteOverlapBucket bucket)
{
    switch (bucket)
    {
        case RigidRouteOverlapBucket::Match: ++counts.match; break;
        case RigidRouteOverlapBucket::MaterialMismatch: ++counts.materialMismatch; break;
        case RigidRouteOverlapBucket::ClassMismatch: ++counts.classMismatch; break;
        case RigidRouteOverlapBucket::RigidOnly: ++counts.rigidOnly; break;
        case RigidRouteOverlapBucket::RigidInFront: ++counts.rigidInFront; break;
        case RigidRouteOverlapBucket::FallbackInFront: ++counts.fallbackInFront; break;
        case RigidRouteOverlapBucket::FallbackOnly: ++counts.fallbackOnly; break;
        case RigidRouteOverlapBucket::Neither: ++counts.neither; break;
        default: ++counts.unknown; break;
    }
}

}

void PathTracePrimaryPass::ReadBackRayTracingSmokeTest()
{
    const int debugMode = idMath::ClampInt(0, 52, r_pathTracingDebugMode.GetInteger());
    const bool overlapDumpRequested = debugMode == 24 && r_pathTracingRigidRouteOverlapDump.GetInteger() != 0;
    if (r_pathTracingReadbackEnable.GetInteger() == 0 && !overlapDumpRequested)
    {
        m_frameResources.readbackQueued = false;
        m_frameResources.readbackDelayFrames = 0;
        m_frameResources.readbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
        return;
    }

    if (!m_frameResources.readbackQueued || !m_frameResources.readbackTexture)
    {
        if (m_frameResources.readbackCooldownFrames > 0)
        {
            --m_frameResources.readbackCooldownFrames;
        }
        return;
    }

    if (m_frameResources.readbackDelayFrames > 0)
    {
        --m_frameResources.readbackDelayFrames;
        return;
    }

    nvrhi::IDevice* device = deviceManager ? deviceManager->GetDevice() : nullptr;
    if (!device)
    {
        return;
    }

    const int readbackStartMs = Sys_Milliseconds();
    device->waitForIdle();
    const int waitForIdleMs = Sys_Milliseconds() - readbackStartMs;

    size_t rowPitch = 0;
    void* readbackData = device->mapStagingTexture(m_frameResources.readbackTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);
    if (!readbackData)
    {
        common->Printf("PathTracePrimaryPass: RT smoke UAV readback map failed\n");
        m_frameResources.readbackQueued = false;
        m_frameResources.readbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
        return;
    }
    m_frameResources.RecordReadbackMapped();

    const int sampleX = m_frameResources.width / 2;
    const int sampleY = m_frameResources.height / 2;
    const byte* readbackBytes = static_cast<const byte*>(readbackData);
    const float* centerRgba = reinterpret_cast<const float*>(readbackBytes + rowPitch * sampleY + sizeof(float) * 4 * sampleX);

    int greenHits = 0;
    int redMisses = 0;
    RigidRouteOverlapCounts fullFrameOverlap;
    RigidRouteOverlapCounts centerRegionOverlap;
    int centerRegionPixels = 0;
    const int centerRegionRadius = 16;
    const int centerRegionMinX = idMath::ClampInt(0, Max(0, m_frameResources.width - 1), sampleX - centerRegionRadius);
    const int centerRegionMaxX = idMath::ClampInt(0, Max(0, m_frameResources.width - 1), sampleX + centerRegionRadius);
    const int centerRegionMinY = idMath::ClampInt(0, Max(0, m_frameResources.height - 1), sampleY - centerRegionRadius);
    const int centerRegionMaxY = idMath::ClampInt(0, Max(0, m_frameResources.height - 1), sampleY + centerRegionRadius);
    for (int y = 0; y < m_frameResources.height; ++y)
    {
        const float* row = reinterpret_cast<const float*>(readbackBytes + rowPitch * y);
        for (int x = 0; x < m_frameResources.width; ++x)
        {
            const float* rgba = row + x * 4;
            if (rgba[1] > 0.5f)
            {
                ++greenHits;
            }
            else if (rgba[0] > 0.5f)
            {
                ++redMisses;
            }

            if (debugMode == 24)
            {
                const RigidRouteOverlapBucket bucket = ClassifyRigidRouteOverlapColor(rgba);
                AccumulateRigidRouteOverlapBucket(fullFrameOverlap, bucket);
                if (x >= centerRegionMinX && x <= centerRegionMaxX && y >= centerRegionMinY && y <= centerRegionMaxY)
                {
                    AccumulateRigidRouteOverlapBucket(centerRegionOverlap, bucket);
                    ++centerRegionPixels;
                }
            }
        }
    }

    const int readbackMs = Sys_Milliseconds() - readbackStartMs;
    if (r_pathTracingSmokeLog.GetInteger() != 0 || ShouldLogSmokeTiming(readbackMs, Sys_Milliseconds(), g_smokeLastReadbackTimingLogMs))
    {
        common->Printf("PathTracePrimaryPass: RT smoke UAV readback %dx%d center rgba=(%.3f, %.3f, %.3f, %.3f), hits=%d, misses=%d, rowPitch=%u, total=%d ms, waitForIdle=%d ms\n",
            m_frameResources.width, m_frameResources.height,
            centerRgba[0], centerRgba[1], centerRgba[2], centerRgba[3],
            greenHits, redMisses, static_cast<unsigned int>(rowPitch),
            readbackMs, waitForIdleMs);
    }
    if (debugMode == 24 && (overlapDumpRequested || r_pathTracingSmokeLog.GetInteger() != 0))
    {
        const int totalPixels = Max(1, m_frameResources.width * m_frameResources.height);
        common->Printf("PathTracePrimaryPass: PT rigid route overlap pixels total=%d match=%d(%.2f%%) materialMismatch=%d(%.2f%%) classMismatch=%d(%.2f%%) rigidOnly=%d(%.2f%%) rigidInFront=%d(%.2f%%) fallbackInFront=%d(%.2f%%) fallbackOnly=%d(%.2f%%) neither=%d(%.2f%%) unknown=%d(%.2f%%) colorCode green/yellow/magenta/cyan/blue/orange/gray/black tolerance=1.5pxRayT\n",
            totalPixels,
            fullFrameOverlap.match, 100.0f * static_cast<float>(fullFrameOverlap.match) / static_cast<float>(totalPixels),
            fullFrameOverlap.materialMismatch, 100.0f * static_cast<float>(fullFrameOverlap.materialMismatch) / static_cast<float>(totalPixels),
            fullFrameOverlap.classMismatch, 100.0f * static_cast<float>(fullFrameOverlap.classMismatch) / static_cast<float>(totalPixels),
            fullFrameOverlap.rigidOnly, 100.0f * static_cast<float>(fullFrameOverlap.rigidOnly) / static_cast<float>(totalPixels),
            fullFrameOverlap.rigidInFront, 100.0f * static_cast<float>(fullFrameOverlap.rigidInFront) / static_cast<float>(totalPixels),
            fullFrameOverlap.fallbackInFront, 100.0f * static_cast<float>(fullFrameOverlap.fallbackInFront) / static_cast<float>(totalPixels),
            fullFrameOverlap.fallbackOnly, 100.0f * static_cast<float>(fullFrameOverlap.fallbackOnly) / static_cast<float>(totalPixels),
            fullFrameOverlap.neither, 100.0f * static_cast<float>(fullFrameOverlap.neither) / static_cast<float>(totalPixels),
            fullFrameOverlap.unknown, 100.0f * static_cast<float>(fullFrameOverlap.unknown) / static_cast<float>(totalPixels));
        const int regionPixels = Max(1, centerRegionPixels);
        const RigidRouteOverlapBucket centerBucket = ClassifyRigidRouteOverlapColor(centerRgba);
        common->Printf("PathTracePrimaryPass: PT rigid route center bucket=%s rgba=(%.3f, %.3f, %.3f, %.3f) roi=%dx%d match=%d(%.2f%%) materialMismatch=%d(%.2f%%) classMismatch=%d(%.2f%%) rigidOnly=%d(%.2f%%) rigidInFront=%d(%.2f%%) fallbackInFront=%d(%.2f%%) fallbackOnly=%d(%.2f%%) neither=%d(%.2f%%) unknown=%d(%.2f%%)\n",
            RigidRouteOverlapBucketName(centerBucket),
            centerRgba[0], centerRgba[1], centerRgba[2], centerRgba[3],
            centerRegionMaxX - centerRegionMinX + 1,
            centerRegionMaxY - centerRegionMinY + 1,
            centerRegionOverlap.match, 100.0f * static_cast<float>(centerRegionOverlap.match) / static_cast<float>(regionPixels),
            centerRegionOverlap.materialMismatch, 100.0f * static_cast<float>(centerRegionOverlap.materialMismatch) / static_cast<float>(regionPixels),
            centerRegionOverlap.classMismatch, 100.0f * static_cast<float>(centerRegionOverlap.classMismatch) / static_cast<float>(regionPixels),
            centerRegionOverlap.rigidOnly, 100.0f * static_cast<float>(centerRegionOverlap.rigidOnly) / static_cast<float>(regionPixels),
            centerRegionOverlap.rigidInFront, 100.0f * static_cast<float>(centerRegionOverlap.rigidInFront) / static_cast<float>(regionPixels),
            centerRegionOverlap.fallbackInFront, 100.0f * static_cast<float>(centerRegionOverlap.fallbackInFront) / static_cast<float>(regionPixels),
            centerRegionOverlap.fallbackOnly, 100.0f * static_cast<float>(centerRegionOverlap.fallbackOnly) / static_cast<float>(regionPixels),
            centerRegionOverlap.neither, 100.0f * static_cast<float>(centerRegionOverlap.neither) / static_cast<float>(regionPixels),
            centerRegionOverlap.unknown, 100.0f * static_cast<float>(centerRegionOverlap.unknown) / static_cast<float>(regionPixels));
        r_pathTracingRigidRouteOverlapDump.SetInteger(0);
    }

    device->unmapStagingTexture(m_frameResources.readbackTexture);
    m_frameResources.RecordReadbackUnmapped();
    m_frameResources.readbackLogged = true;
    m_frameResources.readbackQueued = false;
    m_frameResources.readbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
}
