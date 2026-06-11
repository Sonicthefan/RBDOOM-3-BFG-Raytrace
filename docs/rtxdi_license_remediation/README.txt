RTXDI Proprietary Code Remediation
==================================

Status: audit complete 2026-06-11. No remediation work started.

Problem
-------

The path-tracing stack is not license-clean. Shader and C++ code across the
PT, clean DI, and clean GI lanes #include NVIDIA RTXDI runtime headers from
outside the repo:

    E:/prog/references/RTXDI-main/Libraries/Rtxdi/Include

wired through the CMake cache variable RTXDI_RUNTIME_DIR
(neo/CMakeLists.txt, neo/shaders/CMakeLists.txt). Every header in that tree
is marked:

    SPDX-License-Identifier: LicenseRef-NvidiaProprietary

and is governed by the NVIDIA RTX SDKs License (RTXDI-main/LICENSE.txt and
Libraries/Rtxdi/LICENSE.txt). These are NOT public/open-source headers,
despite docs/restir_remix_gi_cleanroom/README.txt calling them "public
RTXDI headers". The dxvk-remix rtxdi submodule is under the same NVIDIA
license; there is no permissively licensed copy of this code.

Consequences:

    1. All ReSTIR resampling math (reservoir storage, initial sampling RIS,
       temporal reuse, spatial reuse) in the DI, GI, and PT lanes is
       NVIDIA's implementation, not rbdoom-owned code. The "clean-room"
       label on the DI/GI lanes is true only of the wrapper/contract code.
    2. Every compiled shader blob under base/renderprogs2 that came from a
       file listed below is a derivative work of proprietary NVIDIA code.
       Same for RBDoom3BFG.exe (it compiles RtxdiUtils.cpp from the
       references tree and includes Rtxdi C++ headers).
    3. The engine is GPL-3. Distributing builds that contain this code
       conflicts with both licenses. Local development use is permitted by
       the SDK license; distribution is the problem.
    4. Architectural drift: the lanes were supposed to conform to the RTX
       Remix reference design (a shipping implementation). The Rtxdi/PT/*
       (ReSTIR PT) headers instead come from the upstream RTXDI sample,
       which is a basic non-shipping example not intended for direct use.
       Pulling those PT files in wholesale likely degraded performance and
       quality in addition to the license problem, and pulled the lanes
       away from the Remix-shaped contract the docs mandate.

The git source tree itself is clean: no NVIDIA source text is committed,
and the renderprogs2 blobs are untracked. The contamination enters only at
compile time via include paths, and exists in build outputs and in the
deployed game at E:/prog/rbdoom-3-BFG-prebuilt.

Policy/documentation failures to correct alongside the code:

    docs/restir_remix_di_cleanroom forbids copying NVIDIA source and
        requires rbdoom-owned code, but the DI implementation includes the
        proprietary headers anyway, and the docs never disclose it.
    docs/restir_remix_gi_cleanroom explicitly authorizes the includes and
        mislabels the headers as "public".

Issue Locations
---------------

See impacted_files.txt for the complete per-file inventory. Summary:

    build system        neo/CMakeLists.txt (RTXDI_RUNTIME_DIR, RtxdiUtils.cpp)
                        neo/shaders/CMakeLists.txt (-I args, dependency lists)
    clean GI lane       remix_restir_gi shaders + remix_bridge GI bridges
    clean DI lane       cleanroom_rtxdi sentinel + spatial shaders
    remix bridge        RAB_ReservoirBridge, RAB_GIReservoirBridge
    legacy PT/smoke     pathtrace_smoke*, restir PT shaders, RtxdiBridge files
    C++ renderer        PathTraceCleanRestirGi, PathTraceRemixRtxdiResources,
                        PathTraceRestirPT* (+ transitive users)
    vendored (separate) neo/bin/ShaderDynamic/Source (Rtxpt/donut/NVAPI tree,
                        gitignored, predates the ReSTIR lanes)
    artifacts           base/renderprogs2 blobs, build-* outputs,
                        E:/prog/rbdoom-3-BFG-prebuilt deployment

Replacement Plan
----------------

Broad order of work. Each numbered step is one part to replace; no
function-level instructions here. Ground rule for all rewrite steps: derive
the math from the published papers (Bitterli et al. 2020 ReSTIR, Ouyang et
al. 2021 ReSTIR GI, the SIGGRAPH course notes) with the NVIDIA headers
closed. Do not open, paraphrase, or transcribe the proprietary files while
writing replacements.

Operating rule for this remediation thread:

    This coordinator may keep doing non-math cleanup, documentation,
    containment, dependency mapping, build wiring, and mechanical removal only.
    When progress requires replacing one RTXDI header/module surface
    (for example Rtxdi/RtxdiParameters.h, Rtxdi/RtxdiUtils.h,
    Rtxdi/DI/Reservoir.hlsli, Rtxdi/GI/TemporalResampling.hlsli,
    Rtxdi/PT/ReSTIRPT.h, or similar), stop and create a prompt for a
    stand-alone session dedicated to that single replacement. Resume this
    coordinator only after that replacement lands.

Shader validation rule:

    This branch's renderer runtime path is Vulkan. Shader-remediation work is
    accepted only when the Vulkan/SPIR-V path is clean:

        cmake --build --preset win64-pt-dev-release

    from E:/prog/rbdoom-3-bfg-rt/neo must pass, including the SPIR-V shader
    custom-build steps. Direct DXIL compiles may be used as quick syntax/type
    smoke tests, but they are not runtime proof, not deployment proof, and not
    proof that a forbidden RTXDI header is absent from the actual compile
    graph. A valid severance proof must remove the dependency from the SPIR-V
    compile graph and, at Step 9, pass configure+build with
    E:/prog/references renamed away.

Step 0. Decision gate: confirm the goal is GPL-distributable builds. If the
        project will instead accept the NVIDIA RTX SDKs License and never
        distribute under GPL, stop after Step 1 (documentation honesty) and
        Step 2 (containment).

Step 1. Fix the documentation. Correct the "public headers" claim in the GI
        README, add the license status to both cleanroom READMEs, and record
        that DI/GI/PT resampling math is currently NVIDIA code.

Step 2. Containment. Do not distribute current exes or renderprogs2 blobs.
        Add a guard (pre-commit grep or CMake check) that fails on any new
        tracked file containing NVIDIA copyright text, and require any new
        include of Rtxdi/* to be listed in impacted_files.txt.

Step 3. Decide the fate of the legacy PT/smoke lanes (pathtrace_smoke*,
        restir PT reservoir lanes, PDF/NEE verifier, mode-56 era code).
        Preferred: delete them rather than rewrite them; the clean DI/GI
        lanes superseded them, and the Rtxdi/PT/* code they depend on is
        the upstream non-shipping sample, not the Remix-shaped design the
        project targets - it is suspect for performance/quality as well as
        license. Deleting removes the entire Rtxdi/PT/* dependency and most
        of the DI header users in one step.

Step 4. Write rbdoom-owned shared utility headers (replaces Rtxdi/Utils/*):
        random sampler state, reservoir page addressing, small math
        helpers, checkerboard helpers, BRDF ray sample struct. New files
        under neo/shaders/builtin/pathtracing/cleanroom_common/ (or
        similar). These are small, textbook pieces.

Step 5. Write rbdoom-owned parameter structs (replaces RtxdiParameters.h,
        ReSTIRDIParameters.h, ReSTIRGIParameters.h) shared between HLSL and
        C++, plus C++-side context/param types (replaces ReSTIRDI.h,
        ReSTIRGI.h, RtxdiUtils.h/.cpp).

Step 6. DI lane rewrite (replaces Rtxdi/DI/*): reservoir struct +
        pack/unpack + page storage, initial sampling RIS loop, temporal
        resampling, spatial resampling. Rewire the two cleanroom_rtxdi
        shaders and RAB_ReservoirBridge to the new headers. Re-prove with
        the existing DI validation matrix.

Step 7. GI lane rewrite (replaces Rtxdi/GI/*): GI reservoir struct +
        storage, temporal resampling with jacobian validation and BASIC
        bias correction, spatial resampling. Rewire
        pathtrace_clean_restir_gi.rt.hlsl, restir_gi_temporal_reuse.rt.hlsl,
        and RAB_GIReservoirBridge. Re-prove with the GI validation matrix
        steps 1-5 against the current build as a private behavior oracle.

Step 8. C++ cleanup: remove RtxdiUtils.cpp from the build, port
        PathTraceCleanRestirGi / PathTraceRemixRtxdiResources /
        PathTraceRestirPT* (if kept) to the rbdoom-owned types from Step 5.

Step 9. Sever the dependency: delete RTXDI_RUNTIME_DIR and all
        ${RTXDI_RUNTIME_DIR} references from both CMakeLists. A clean
        configure+build with E:/prog/references renamed away is the proof
        that nothing outside the repo is reachable.

Step 10. Rebuild and redeploy: full rebuild of the Vulkan SPIR-V
         renderprogs2 blobs, delete stale blobs in build-*/ and the prebuilt
         dir, redeploy exe + renderprogs2 to
         E:/prog/rbdoom-3-BFG-prebuilt, run the DI and GI human-facing
         accumulation proofs. DXIL artifacts are secondary build outputs for
         this branch and must not be used as the acceptance gate.

Step 11. Separate follow-up: neo/bin/ShaderDynamic/Source (vendored NVIDIA
         Rtxpt sample + donut shaders + NVAPI headers, March 2026,
         gitignored). Delete if the dynamic-shader experiment is dead;
         otherwise document its license status the same way.
