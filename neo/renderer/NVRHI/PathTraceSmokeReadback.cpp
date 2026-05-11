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

}

void PathTracePrimaryPass::ReadBackRayTracingSmokeTest()
{
    const int debugMode = idMath::ClampInt(0, 45, r_pathTracingDebugMode.GetInteger());
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
    int overlapMatch = 0;
    int overlapRigidOnly = 0;
    int overlapRigidInFront = 0;
    int overlapFallbackInFront = 0;
    int overlapFallbackOnly = 0;
    int overlapNeither = 0;
    int overlapUnknown = 0;
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
                const bool rHigh = rgba[0] > 0.75f;
                const bool gHigh = rgba[1] > 0.75f;
                const bool bHigh = rgba[2] > 0.75f;
                const bool rMid = rgba[0] > 0.35f;
                const bool gMid = rgba[1] > 0.30f;
                const bool bMid = rgba[2] > 0.35f;
                const bool dimGray = rgba[0] > 0.08f && rgba[0] < 0.32f && rgba[1] > 0.08f && rgba[1] < 0.32f && rgba[2] > 0.08f && rgba[2] < 0.32f;
                const bool black = rgba[0] < 0.04f && rgba[1] < 0.04f && rgba[2] < 0.04f;

                if (!rHigh && gHigh && !bHigh)
                {
                    ++overlapMatch;
                }
                else if (!rHigh && gHigh && bHigh)
                {
                    ++overlapRigidOnly;
                }
                else if (!rHigh && !gHigh && bHigh)
                {
                    ++overlapRigidInFront;
                }
                else if (rHigh && gMid && !bMid)
                {
                    ++overlapFallbackInFront;
                }
                else if (dimGray)
                {
                    ++overlapFallbackOnly;
                }
                else if (black)
                {
                    ++overlapNeither;
                }
                else
                {
                    ++overlapUnknown;
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
        common->Printf("PathTracePrimaryPass: PT rigid route overlap pixels total=%d match=%d(%.2f%%) rigidOnly=%d(%.2f%%) rigidInFront=%d(%.2f%%) fallbackInFront=%d(%.2f%%) fallbackOnly=%d(%.2f%%) neither=%d(%.2f%%) unknown=%d(%.2f%%) colorCode green/cyan/blue/orange/gray/black tolerance=1.5pxRayT\n",
            totalPixels,
            overlapMatch, 100.0f * static_cast<float>(overlapMatch) / static_cast<float>(totalPixels),
            overlapRigidOnly, 100.0f * static_cast<float>(overlapRigidOnly) / static_cast<float>(totalPixels),
            overlapRigidInFront, 100.0f * static_cast<float>(overlapRigidInFront) / static_cast<float>(totalPixels),
            overlapFallbackInFront, 100.0f * static_cast<float>(overlapFallbackInFront) / static_cast<float>(totalPixels),
            overlapFallbackOnly, 100.0f * static_cast<float>(overlapFallbackOnly) / static_cast<float>(totalPixels),
            overlapNeither, 100.0f * static_cast<float>(overlapNeither) / static_cast<float>(totalPixels),
            overlapUnknown, 100.0f * static_cast<float>(overlapUnknown) / static_cast<float>(totalPixels));
        r_pathTracingRigidRouteOverlapDump.SetInteger(0);
    }

    device->unmapStagingTexture(m_frameResources.readbackTexture);
    m_frameResources.RecordReadbackUnmapped();
    m_frameResources.readbackLogged = true;
    m_frameResources.readbackQueued = false;
    m_frameResources.readbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
}
