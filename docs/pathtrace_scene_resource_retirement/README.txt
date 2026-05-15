PT Scene Resource Retirement Guide
==================================

Purpose
-------

This folder is for focused work on replacing the diagnostic portal-transition
`waitForIdle()` with proper frame-lagged lifetime retention for committed
path-tracing scene resources.

The confirmed crash class is CPU/GPU resource lifetime around PT scene package
replacement:

    - texture-like corruption appears when the GPU is far enough behind
    - device removal occurs when crossing portal boundaries under load
    - a blunt `device->waitForIdle()` before replacing committed PT scene
      resources prevents the crash

This strongly indicates that old PT scene resources can be released, rewritten,
or replaced while previous GPU work still references them.

Confirmed Diagnostic
--------------------

The diagnostic path lives in:

    neo/renderer/NVRHI/PathTraceSmokeResources.cpp

Function:

    PathTracePrimaryPass::CommitRayTracingSmokeSceneResources

The current diagnostic CVar is:

    r_pathTracingWaitForIdleOnPortalChange

When enabled, the code waits for the whole device before swapping committed PT
scene resources if the scene transition signature changed. That is correct as a
diagnostic, but it is not acceptable as the default runtime solution.

Target Fix
----------

Replace the need for `waitForIdle()` with a retired scene package queue:

    when committing a new PT scene package:
        capture the previously committed package
        push it into a retirement queue
        keep all handles alive for N frames
        assign the new package immediately

    each frame or each commit:
        release retired packages whose retire frame has passed

Because this project has ample RAM/VRAM headroom, frame-lagged retention is the
preferred first real fix. Event queries or timeline-semaphore retirement can be
added later if exact retirement becomes necessary.

Do Not Start With Event Queries
-------------------------------

NVRHI exposes event queries:

    device->createEventQuery()
    device->setEventQuery(query, nvrhi::CommandQueue::Graphics)
    device->pollEventQuery(query)
    device->waitEventQuery(query)
    device->resetEventQuery(query)

The engine already uses them for frame pacing in `DeviceManager_VK.cpp` and
`DeviceManager_DX12.cpp`.

However, do not replace `waitForIdle()` with a just-in-time
`setEventQuery()`/`waitEventQuery()` inside `CommitRayTracingSmokeSceneResources`.
If the query is created and waited immediately before replacement, it is still a
stall. The correct use is "swap now, retire later".

Frame-lagged retention is simpler and safer for the first fix:

    retireFrame = currentFrame + r_pathTracingSceneRetireFrames

Recommended default:

    r_pathTracingSceneRetireFrames 6

Important Files
---------------

Primary implementation files:

    neo/renderer/NVRHI/PathTracePrimaryPass.h
    neo/renderer/NVRHI/PathTraceSmokeResources.cpp
    neo/renderer/NVRHI/PathTraceSmokeResources.h
    neo/renderer/NVRHI/PathTraceCVars.cpp
    neo/renderer/NVRHI/PathTraceCVars.h

Important existing types:

    RtSmokeSceneBufferHandles
    RtSmokeSceneResourceCommitDesc
    RtPathTraceSceneInputs

Important existing function:

    PathTracePrimaryPass::CommitRayTracingSmokeSceneResources

Important committed resource members:

    m_sceneInputs
    m_smokeStaticVertexBuffer
    m_smokeStaticIndexBuffer
    m_smokeStaticTriangleClassBuffer
    m_smokeStaticTriangleMaterialBuffer
    m_smokeStaticTriangleMaterialIndexBuffer
    m_smokeDynamicVertexBuffer
    m_smokeDynamicIndexBuffer
    m_smokeDynamicTriangleClassBuffer
    m_smokeDynamicTriangleMaterialBuffer
    m_smokeDynamicTriangleMaterialIndexBuffer
    m_smokeMaterialTableBuffer
    m_smokeEmissiveTriangleBuffer
    m_smokeLightCandidateBuffer
    m_smokeDoomAnalyticLightBuffer
    m_smokeRigidRouteVertexBuffer
    m_smokeRigidRouteIndexBuffer
    m_smokeRigidRouteTriangleMaterialBuffer
    m_smokeRigidRouteTriangleMaterialIndexBuffer
    m_smokeRigidRouteInstanceBuffer
    m_smokeStaticBlas
    m_smokeDynamicBlas
    m_smokeTlas
    m_smokeBindingSet
    m_smokeTextureDescriptorTable
    m_smokeActiveTextureTable

Retain strong NVRHI handles, not raw pointers.

Recommended Task Order
----------------------

1. Add a retired scene package struct.

   It must hold strong handles for the previous committed package.

2. Add a retirement queue to `PathTracePrimaryPass`.

   A `std::deque` or `std::vector` is fine. Keep it private.

3. Add CVars:

       r_pathTracingSceneRetireFrames 6
       r_pathTracingSceneRetireDump 0

4. On scene commit, capture the previous committed package before overwriting
   `m_smoke*` members.

5. Push the previous package into the retirement queue only when it contains
   resources worth retaining.

6. Release old retired packages after their retire frame passes.

7. Keep `r_pathTracingWaitForIdleOnPortalChange` as a diagnostic override, but
   do not rely on it for normal behavior.

8. Validate by running with:

       r_pathTracingWaitForIdleOnPortalChange 0
       r_pathTracingSceneRetireFrames 6

   Then force high GPU load and cross portal boundaries.

Non-Goals
---------

Do not do these in the first retirement task:

- do not redesign portal walking
- do not change geometry selection
- do not change light selection
- do not change shader logic
- do not add event-query retirement yet
- do not add a new frame graph
- do not globally wait for the device every frame
- do not remove the diagnostic wait CVar
- do not rewrite unrelated PT resource ownership

Acceptance Criteria
-------------------

- Old committed PT scene packages are retained for a configurable number of
  frames.
- Normal portal transitions no longer require `device->waitForIdle()`.
- `r_pathTracingWaitForIdleOnPortalChange 1` still works as a diagnostic
  override.
- The retirement queue logs enough data to confirm packages are retained and
  released.
- High-load portal crossing should be tested with the wait disabled.
- The implementation builds cleanly.

