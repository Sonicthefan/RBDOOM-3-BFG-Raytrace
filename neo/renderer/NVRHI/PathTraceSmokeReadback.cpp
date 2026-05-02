#include "precompiled.h"
#pragma hdrstop

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
    if (r_pathTracingReadbackEnable.GetInteger() == 0)
    {
        m_smokeReadbackQueued = false;
        m_smokeReadbackDelayFrames = 0;
        m_smokeReadbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
        return;
    }

    if (!m_smokeReadbackQueued || !m_smokeReadbackTexture)
    {
        if (m_smokeReadbackCooldownFrames > 0)
        {
            --m_smokeReadbackCooldownFrames;
        }
        return;
    }

    if (m_smokeReadbackDelayFrames > 0)
    {
        --m_smokeReadbackDelayFrames;
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
    void* readbackData = device->mapStagingTexture(m_smokeReadbackTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);
    if (!readbackData)
    {
        common->Printf("PathTracePrimaryPass: RT smoke UAV readback map failed\n");
        m_smokeReadbackQueued = false;
        m_smokeReadbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
        return;
    }

    const int sampleX = m_smokeOutputWidth / 2;
    const int sampleY = m_smokeOutputHeight / 2;
    const byte* readbackBytes = static_cast<const byte*>(readbackData);
    const float* centerRgba = reinterpret_cast<const float*>(readbackBytes + rowPitch * sampleY + sizeof(float) * 4 * sampleX);

    int greenHits = 0;
    int redMisses = 0;
    for (int y = 0; y < m_smokeOutputHeight; ++y)
    {
        const float* row = reinterpret_cast<const float*>(readbackBytes + rowPitch * y);
        for (int x = 0; x < m_smokeOutputWidth; ++x)
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
        }
    }

    const int readbackMs = Sys_Milliseconds() - readbackStartMs;
    if (r_pathTracingSmokeLog.GetInteger() != 0 || ShouldLogSmokeTiming(readbackMs, Sys_Milliseconds(), g_smokeLastReadbackTimingLogMs))
    {
        common->Printf("PathTracePrimaryPass: RT smoke UAV readback %dx%d center rgba=(%.3f, %.3f, %.3f, %.3f), hits=%d, misses=%d, rowPitch=%u, total=%d ms, waitForIdle=%d ms\n",
            m_smokeOutputWidth, m_smokeOutputHeight,
            centerRgba[0], centerRgba[1], centerRgba[2], centerRgba[3],
            greenHits, redMisses, static_cast<unsigned int>(rowPitch),
            readbackMs, waitForIdleMs);
    }

    device->unmapStagingTexture(m_smokeReadbackTexture);
    m_smokeReadbackLogged = true;
    m_smokeReadbackQueued = false;
    m_smokeReadbackCooldownFrames = RT_SMOKE_READBACK_INTERVAL_FRAMES;
}
