RBDoom3 BFG RT/PT Raster Interaction Leak Investigation
=======================================================

Purpose
-------

This note captures the current theory for the location-specific FPS drops that
show up in raster mode and appear to leak into path tracing mode. It is meant to
survive context loss and give the next session a concrete, low-risk plan.

Observed Behavior
-----------------

Symptoms seen during testing:

- Certain level locations drop heavily while nearby areas do not.
- The drop is not uniform with resolution, which points away from pure RT pixel
  cost.
- The same location pattern appears in raster mode and can still affect PT mode.
- In raster mode, disabling interactions and shadows is decisive:

      r_skipInteractions 1
      r_skipShadows 1

  The combination recovers performance in the bad areas.
- Disabling only one side helps partially. Shadows and interactions stack.
- PT mode is much faster after r_pathTracingSkipRaster3D was added, because the
  full backend 3D raster tail no longer draws under the RT output. However, the
  location-specific interaction/shadow problem can still be felt.
- A local source build of the public v1.6.0 retail tag was created and tested.
  It runs the bad area rock-solid at 120 FPS, proving the machine, driver,
  Chrome, and external desktop state are not the primary cause.
- A clean latest upstream/master source build was then bisected from v1.6.0 to
  upstream tip. Every tested candidate was clear, and the final "first bad"
  commit was README-only. A direct tip test was also clear. Clean upstream is
  therefore not the source of the observed slowdown.
- The decisive difference was build configuration, specifically Debug versus
  Release. A fresh RT branch Release retail-like build fixed the raster stall,
  but a later fresh generic Release non-retail build also ran at the 120 FPS
  cap. The old huge Debug executable reproduced the broken performance.
- Disabling validation with `+set r_useValidationLayers 0` restored raster
  Debug performance to the 120 FPS cap. PT improved only partially in
  RelWithDebInfo and stayed far below Release in worst spots, which separates
  the raster validation cost from the PT CPU scene-build cost.
- Treat the old slowdown as a Debug-build/performance-testing artifact unless a
  Release build starts reproducing again. Do not resume upstream bisect unless
  a clean upstream Release build starts reproducing again.

Important profiler interpretation:

- Optick often shows large "Wait for Frame" regions during these bad frames.
  That block is the engine pacing/synchronization loop in common_frame.cpp, not
  a render pass. It can grow because a previous frame or another thread/GPU path
  consumed the budget.
- If profiling while the game is backgrounded, RBDoom forces the engine to
  roughly 15 Hz when com_activeApp is false:

      common_frame.cpp: backgroundEngineHz = 15.0f

  That produces about 66.7 ms in "Wait for Frame" and can masquerade as the
  same symptom. For profiling, keep the game focused or use:

      com_activeApp 1
      com_pause 0
      com_noSleep 1
      r_swapInterval 0

  A future profiling convenience CVar around the background throttle may be
  useful.
- Nsight Graphics capture replay is not reliable for end-to-end frame pacing.
  Replay can report the isolated GPU command stream as cheap even when the real
  game frame is CPU/present/synchronization limited.
- In one Nsight Graphics capture replay, the captured GPU work replayed around
  100 FPS / roughly 2 ms GPU time, while the live game was slow. Do not treat
  Nsight Graphics replay as proof that the live frame is healthy; use Nsight
  Systems, Optick, PresentMon, or GPUView-style timelines for the live wait.
- Optick showed SwapCommandBuffers_FinishRendering and BlockingSwapBuffers
  immediately before the large Wait for Frame region. In this code path:

      common_frame.cpp
          renderSystem->SwapCommandBuffers_FinishRendering()
      RenderSystem.cpp
          SwapCommandBuffers_FinishRendering()
              backEnd.GL_BlockingSwapBuffers()
      RenderBackend_NVRHI.cpp
          GL_BlockingSwapBuffers()
              deviceManager->Present()

  That means the visible long wait may be present/frame-queue synchronization
  or waiting for previous queued work, not necessarily the exact CPU function
  that originally caused the bad frame.
- Nsight Systems "Blocked State" means a CPU thread is parked waiting on an OS,
  API, driver, or synchronization object. A Resource wait on the RBDoom main
  thread near Present/BlockingSwapBuffers points toward swapchain, frame latency,
  command queue, or resource lifetime synchronization. It is a symptom row, not
  by itself the root cause.
- Chromium/CrGpuMain User Request waits can line up with the same display
  cadence and should not be treated as the cause unless its GPU queue is active
  during the RBDoom stall. The local v1.6.0 retail-source build being flawless
  under the same desktop conditions makes Chrome or general desktop state an
  unlikely root cause.

Known-Good Regression Anchors
-----------------------------

A local build matching the public v1.6.0 retail source was created specifically
to anchor this bug hunt:

    checkout: E:\prog\rbdoom3_retail
    tag:      v1.6.0
    commit:   ba39ba67df049dcf16c7b31f6613f72959be2c21
    exe:      E:\prog\rbdoom3_retail\build\Release\RBDoom3BFG.exe

Build configuration used:

    cmake -S neo -B build -G "Visual Studio 17 2022" -A x64 ^
      -DFFMPEG=ON -DBINKDEC=OFF -DRETAIL=ON ^
      -DISPC_EXECUTABLE=E:\prog\rbdoom-3-bfg-rt\tools\ispc\bin\ispc.exe

Submodules were initialized at the commits recorded by v1.6.0:

    neo/extern/ShaderMake 13867771f6142f35690a5e2103c1e1efdd90cb0e
    neo/extern/nvrhi     fc4cfe69b9f65c7a0ba6509a52303efaba0a0e8c

Result:

- The local v1.6.0 retail-source build runs the bad area at a stable 120 FPS.
- Rebuilding the same checkout with standard win64 no-ffmpeg flags
  (FFMPEG=OFF, BINKDEC=ON, RETAIL=OFF) did not change the result; codec choice
  was ruled out for this performance issue.

Clean upstream bisect result:

    bisect checkout: E:\prog\rbdoom3_bisect
    game files:      E:\prog\rbdoom-3-BFG-bisect-gamefiles
    good anchor:     v1.6.0
    tip tested:      babd04b3e4469c7b59f8a2e590d1f2775b0f116f

- All midpoint builds tested clear.
- The reported "first bad" commit only changed README.md and the direct tip
  build was also clear.
- Clean upstream/master between v1.6.0 and babd04b3 is effectively exonerated
  for this bug.

RT branch Release-build result:

    preset/build family: win64-pt-retail / win64-pt-dev
    generic control:     build-nonretail-generic
    key factor:          Release config
    PT requirement:      r_pathTracing must be set before device creation

- Fresh Release RT branch builds remove the raster/frame-pacing stall.
- A generic Release non-retail build with RETAIL=OFF, OPTICK=ON,
  FFMPEG=ON, and BINKDEC=OFF also tested clear at the 120 FPS cap.
- The legacy Debug RT branch executable reproduces the broken performance.
- The same Debug executable with `r_useValidationLayers 0` brings raster back to
  the 120 FPS cap, but PT remains much slower than Release in complex spots.
- RelWithDebInfo partially improves PT lows, but this project file still uses
  `MultiThreadedDebugDLL` for that configuration and weaker inlining than
  Release, so it is not a final performance lane.
- PT also runs at the 120 FPS cap when launched with r_pathTracing enabled from
  startup. Setting r_pathTracing after device creation can leave RT feature
  support unavailable, because Vulkan/DX12 RT capabilities are latched during
  device creation.

Historical bisect shape used:

    git worktree add E:\prog\rbdoom3_bisect upstream/master
    cd E:\prog\rbdoom3_bisect
    git bisect start upstream/master v1.6.0

For each bisect step, rebuild with the same retail-style flags above, replace
the game folder renderprogs2 with the candidate's generated renderprogs2, and
test only the known bad location. Mark:

    git bisect good
    git bisect bad

This is now complete. The clean-upstream path did not reproduce. Future work
should focus on avoiding Debug builds for performance conclusions, and on
checking stale build artifacts/configuration if a Release build ever reproduces.

Build Mode Lessons
------------------

Do not compare Debug and Release builds as if they are the same performance
experiment.

- Visual Studio Debug executables are around 30 MB here and use /Od, /Zi, /RTC1,
  debug CRT, and incremental debug linking. Release executables are around
  14-15 MB. The size difference is expected and is not evidence of source-code
  bloat. The Debug executable reproduced the broken frame-pacing/performance
  collapse.
- `r_useValidationLayers` defaults to 1, which enables NVRHI validation. Setting
  it to 0 at startup restored raster Debug performance. Use validation only for
  targeted resource/state/binding investigations, not FPS comparisons.
- PT remains Debug/RelWithDebInfo-sensitive even with validation off because the
  current prototype performs substantial CPU-side staging every frame: Doom
  surface capture, vertex transforms, material metadata/table work, emissive
  inventory, buffer upload setup, and dynamic BLAS/TLAS input preparation. Debug
  CRT allocation checks, disabled or reduced inlining, `/Od`, `/RTC1`, and
  debug-friendly codegen hit that path much harder than raster.
- RETAIL=ON is not an optimization flag. It defines ID_RETAIL, which strips or
  gates non-shipping commands, menus, and developer behavior. It was an early
  suspect because the first fresh clean build was retail-like, but a later
  generic Release non-retail control also tested clear. Do not treat ID_RETAIL
  as the proven culprit for this stall.
- OPTICK=ON is useful for profiling, but keep it out of final performance
  sanity checks unless the question explicitly concerns profiling overhead or
  instrumentation behavior.
- FFMPEG/BINKDEC choice was tested and did not explain the stall.

The new intended Windows build lanes are:

    cmake --preset win64-pt-retail
    cmake --build --preset win64-pt-retail-release

    cmake --preset win64-pt-dev
    cmake --build --preset win64-pt-dev-release

    cmake --preset win64-pt-dev-optick
    cmake --build --preset win64-pt-dev-optick-release

Equivalent human-friendly wrappers live in neo:

    cmake-vs2022-win64-pt-retail.bat
    cmake-vs2022-win64-pt-dev.bat
    cmake-vs2022-win64-pt-dev-optick.bat
    cmake-build-vs2022-win64-pt-retail-release.bat
    cmake-build-vs2022-win64-pt-dev-release.bat
    cmake-build-vs2022-win64-pt-dev-optick-release.bat

PATH_TRACING_DEV defines ID_PATH_TRACING_DEV while preserving the stable PT
Release build lane. Use it for surgical PT diagnostics in Release builds without
falling back to the broad legacy Debug workflow for performance testing.

Current Theory
--------------

Resolved build-mode theory:

- The major live performance collapse was not upstream master and not the PT GPU
  ray tracing cost. It was tied to Debug-build performance.
- Raster Debug loss was mostly NVRHI validation (`r_useValidationLayers 1`).
- PT Debug loss was mostly the debug/runtime/codegen cost of the prototype's
  CPU-heavy per-frame PT scene build. Release optimization and the release CRT
  make the same path fast enough to hit the 120 FPS cap.
- Fresh Release builds, including generic non-retail Release, fix the raster
  stall and let the current toy PT renderer run at the 120 FPS cap.
- The earlier retail-like conclusion was a useful false lead: it identified that
  a fresh Release-family build was clean, but ID_RETAIL itself is not proven to
  be the switch.
- The PT path still depends on startup configuration: r_pathTracing must be set
  before the renderer/device is created so RT device features are enabled.

Older frontend-leak theory retained for reference:

r_pathTracingSkipRaster3D skips backend 3D raster drawing after the PT output is
produced, but it does not stop earlier frontend work for the 3D view.

Current high-level frame shape:

1. Frontend builds the 3D view:

       FindViewLightsAndEntities()
       R_AddLights()
       R_AddModels()
       R_AddInGameGuis()
       R_OptimizeViewLightsList()

2. Backend receives a prepared view.

3. DrawViewInternal enters the PT path:

       PathTracePrimaryPass::Execute()

4. With r_pathTracingSkipRaster3D enabled, backend returns before:

       ShadowAtlasPass()
       DrawInteractions()
       Render_GenericShaderPasses()
       other normal 3D raster passes

That means backend raster shadows/interactions are skipped, but frontend
light/interaction preparation may already have happened.

Likely leak path:

- R_AddLights walks visible viewLights and builds entityInteractionState.
- R_AddModels walks visible entities, finds contacted lights, and may create
  interaction drawSurfs and shadow drawSurfs.
- This can allocate frame memory, touch vertex/index caches, derive tangents,
  set up perforated/alpha-test shadow shaders, and chain light/shadow surfaces.
- PT still needs the normal visible ambient/world draw surfaces for scene
  capture, but it does not need raster-only interaction/shadow drawSurf lists if
  the RT output owns the 3D scene.

Relevant files and areas:

- neo/framework/common_frame.cpp
  "Wait for Frame" Optick region and background 15 Hz throttle.
- neo/renderer/tr_frontend_main.cpp
  Main frontend view build order. Calls FindViewLightsAndEntities, R_AddLights,
  R_AddModels, R_AddInGameGuis, R_OptimizeViewLightsList.
- neo/renderer/tr_frontend_addlights.cpp
  R_AddLights and R_AddSingleLight. Builds view-light interaction state and
  shadow-only entity lists.
- neo/renderer/tr_frontend_addmodels.cpp
  R_AddSingleModel. Builds ambient surfaces, light interaction surfaces, and
  shadow drawSurfs.
- neo/renderer/RenderBackend.cpp
  DrawViewInternal. The current r_pathTracingSkipRaster3D early return happens
  here, after frontend work has already completed.
- neo/renderer/NVRHI/PathTracePrimaryPass.cpp
  PT entry/present shell.
- neo/renderer/NVRHI/PathTraceSceneCapture.cpp
  Captures visible drawSurf geometry for the RT smoke scene.
- neo/renderer/NVRHI/PathTraceLightSelection.cpp
  Uses viewDef->viewLights for selected debug lights.

What Not To Do
--------------

- Do not globally leave r_skipInteractions or r_skipShadows enabled as the fix.
  They are useful diagnostics but they change the raster renderer globally and
  can hide side effects.
- Do not skip all of R_AddModels for PT. PT still needs normal visible ambient
  drawSurfs, material info, dynamic surfaces, and GUI/HUD/menu handling.
- Do not remove viewLights entirely without checking PT selected-light modes.
  Mode 14/15/18 use viewDef->viewLights for debug light selection.
- Do not re-enable full backend raster under PT to recover side effects. That
  was a large performance loss and made raster hitches look like PT hitches.

Investigation Plan
------------------

Step 1: Confirm the leak with frontend counters.

Add lightweight, opt-in diagnostics around the frontend paths:

- In R_AddLights:
  - visible viewLights count,
  - culled/removed lights,
  - time in R_AddLights,
  - optional serial/parallel timing if useful.
- In R_AddSingleModel / R_AddModels:
  - contacted light count totals,
  - created light interaction drawSurf count,
  - created shadow drawSurf count,
  - perforated/alpha-test shadow drawSurf count,
  - time spent in "Find lights",
  - time spent creating interaction drawSurfs,
  - time spent creating shadow drawSurfs.

Keep this behind an explicit diagnostic CVar or Optick scopes. Do not spam the
console every frame in normal play.

Suggested temporary CVar names:

    r_pathTracingRasterLeakLog
    r_pathTracingRasterLeakThreshold

or use Optick-only scopes first if enough.

Step 2: Add a PT-scoped experimental frontend skip.

Prototype a CVar:

    r_pathTracingSkipRasterLightPrep

Intended behavior when:

    r_pathTracing 1
    r_pathTracingSkipRaster3D 1
    r_pathTracingSkipRasterLightPrep 1
    view is the main 3D world view

The frontend should still:

- build visible view entities,
- build normal ambient drawSurfs needed by PT scene capture,
- build in-game GUI drawSurfs as the existing path requires,
- preserve viewLights enough for selected-light PT debug modes if needed,
- preserve later GUI/HUD/menu raster.

The frontend should avoid:

- raster-only light interaction drawSurf generation,
- raster-only shadow drawSurf generation,
- expensive shadow-only entity work that exists solely for raster shadows,
- shadow/interaction cache prep that the PT backend will not consume.

Step 3: Choose the least invasive gate.

Likely safer first gate:

- In tr_frontend_addmodels.cpp, keep ambient surface creation but bypass the
  "add all light interactions" section when PT owns the 3D view.
- This should avoid creating vLight->localInteractions,
  vLight->globalInteractions, vLight->translucentInteractions, and
  vLight->globalShadows/localShadows drawSurfs while leaving ambient drawSurfs
  intact.

Riskier gate:

- Skip or shrink parts of R_AddLights. This may affect viewLights, shadow-only
  entities, and selected-light debug modes. Try only after the AddModels gate is
  understood.

Step 4: Validate behavior.

Test matrix:

1. Raster baseline:

       r_pathTracing 0
       r_skipInteractions 0
       r_skipShadows 0

2. Raster diagnostic:

       r_pathTracing 0
       r_skipInteractions 1
       r_skipShadows 1

3. PT current:

       r_pathTracing 1
       r_pathTracingDebugMode 18
       r_pathTracingSkipRaster3D 1
       r_pathTracingSkipRasterLightPrep 0

4. PT experimental:

       r_pathTracing 1
       r_pathTracingDebugMode 18
       r_pathTracingSkipRaster3D 1
       r_pathTracingSkipRasterLightPrep 1

Check:

- bad location FPS,
- Optick frontend timing,
- mode 18 visual stability,
- mode 14/15 selected-light behavior,
- mode 19 emissive inventory,
- crosshair material dump,
- GUI/HUD/menu still visible,
- no new crashes or material/geometry weirdness.

Expected result if the theory is right:

- Raster mode remains unchanged unless diagnostic cvars are manually used.
- PT mode keeps the same visible RT scene but avoids location-specific frontend
  shadow/interaction prep cost.
- Optick should show less time in R_AddModels/R_AddLights or less downstream
  sync/wait caused by those paths.

Step 5: Decide whether to keep the gate.

If the experimental gate works:

- Fold it into the PT-owns-3D contract or keep it as a clearly named CVar.
- Document that PT mode skips both backend 3D raster drawing and raster-only
  frontend shadow/interaction preparation.
- Keep the default conservative only if a missing side effect appears.

If it does not work:

- Keep the counters/scopes long enough to identify the actual bad path.
- Check whether the cost is in light list construction, model dynamic
  instantiation, vertex cache uploads, alpha/perforated shadow shader setup, or
  present/GPU queue backpressure.

Notes for Future Work
---------------------

This is separate from the geometry/material universe work. Do not mix the
frontend raster-leak experiment with persistent geometry-history changes unless
profiling proves they interact.

The intended PT architecture remains:

- PT owns the 3D world output.
- Raster owns GUI/HUD/menu composition.
- Doom frontend should still provide the stable visible scene data PT needs.
- Raster-only shadow/interaction work should not run just because PT needs
  ambient drawSurfs.
