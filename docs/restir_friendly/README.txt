ReSTIR-Friendly Light Universe Rebuild
======================================

This folder is now the instruction set for rebuilding rbdoom's light universe
into an RTXDI-friendly shape. The previous extraction work is considered done;
do not treat old extraction notes as preserved scope.

Goal
----

Build a unified local-light universe that can feed RTXDI-style RAB callbacks
without destroying the existing split outputs. The first accepted result is not
a ReSTIR visual improvement. The first accepted result is a stable debug view
showing that emissive and Doom analytic lights can be represented through one
unified light domain without flicker, invalid records, or random type changes.

Read order
----------

1. docs/restir_friendly/light_universe_rebuild_protocol.txt
2. docs/restir_friendly/unified_light_record_contract.txt
3. docs/restir_friendly/layered_debug_gates.txt
4. docs/restir_friendly/mechanical_light_universe_slices.txt
5. docs/restir_friendly/new_agent_prompt.txt
6. docs/ReSTIR/restir_pt_chain_after_light_universe.txt
7. docs/ReSTIR/restir_pt_rab_io_supply_map.txt
8. docs/ReSTIR/restir_pt_smoke_to_rab_crossings.txt

Reference root
--------------

Use the local NVIDIA RTXDI checkout as a structure and contract reference:

    E:\prog\references\RTXDI-main

Most relevant reference files:

    Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_Buffers.hlsli
    Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightInfo.hlsli
    Samples\FullSample\Shaders\LightingPasses\RtxdiApplicationBridge\RAB_LightSampling.hlsli
    Samples\FullSample\Source\RenderingPasses\PrepareLightsPass.cpp
    Samples\FullSample\Source\RenderingPasses\LightingPasses.cpp
    Libraries\Rtxdi\Include\Rtxdi\DI\InitialSampling.hlsli
    Libraries\Rtxdi\Include\Rtxdi\DI\SpatialResampling.hlsli
    Libraries\Rtxdi\Include\Rtxdi\RtxdiParameters.h
    Libraries\Rtxdi\Include\Rtxdi\ReSTIRDIParameters.h

rbdoom source anchors
--------------------

Existing split light outputs:

    neo\renderer\NVRHI\PathTraceEmissiveCandidates.h
    neo\renderer\NVRHI\PathTraceEmissiveCandidates.cpp
    neo\renderer\NVRHI\PathTraceDoomLights.h
    neo\renderer\NVRHI\PathTraceDoomLights.cpp
    neo\renderer\NVRHI\PathTraceSmokeSceneBuild.cpp
    neo\renderer\NVRHI\PathTraceSmokeResources.cpp
    neo\renderer\NVRHI\PathTraceSmokeDispatch.cpp

Existing RAB light bridge:

    neo\shaders\builtin\pathtracing\RtxdiBridge\RAB_LightDomainCounts.hlsli
    neo\shaders\builtin\pathtracing\RtxdiBridge\RAB_LightLoadRuntime.hlsli
    neo\shaders\builtin\pathtracing\RtxdiBridge\RAB_LightSamplingCore.hlsli
    neo\shaders\builtin\pathtracing\RtxdiBridge\RAB_LocalLightPdf.hlsli
    neo\shaders\builtin\pathtracing\RtxdiBridge\RAB_HitToLightIndex.hlsli
    neo\shaders\builtin\pathtracing\RtxdiBridge\RAB_EnvironmentSamplingStub.hlsli

Existing debug surface:

    neo\shaders\builtin\pathtracing\pathtrace_restir_combined_resolve.rt.hlsl
    neo\shaders\builtin\pathtracing\RtxdiBridge\Debug\
    neo\renderer\NVRHI\PathTraceCVars.cpp

Build and deploy
----------------

Build from:

    E:\prog\rbdoom-3-bfg-rt\neo

Command:

    cmake --build --preset win64-pt-dev-release

Deploy exe:

    E:\prog\rbdoom-3-bfg-rt\build-win64-pt-dev\Release\RBDoom3BFG.exe
    to
    E:\prog\rbdoom-3-BFG-prebuilt\RBDoom3BFG.exe

Deploy shaders after shader edits:

    E:\prog\rbdoom-3-bfg-rt\base\renderprogs2
    to
    E:\prog\rbdoom-3-BFG-prebuilt\base\renderprogs2

Do not create a nested renderprogs2 directory inside the destination.
