ReSTIR-Friendly Motion Vector Guides
====================================

Purpose
-------

This folder is the instruction set for bringing rbdoom's path-traced motion
vectors back toward the NVIDIA RTXDI and RTX Remix reference shape.

The goal is not a new rbdoom-specific temporal policy. The goal is to make the
motion-vector producer and consumers obey the same contract expected by RTXDI
PT temporal resampling, with RTX Remix used only as the hostile-game and portal
reference.

Read order
----------

1. docs/pathtracing.txt
2. docs/pathtrace_modules.txt
3. docs/restir_friendly/motion_vectors/motion_vector_reference_guide.txt
4. docs/restir_friendly/motion_vectors/worker_launch_protocol.txt
5. docs/restir_friendly/motion_vectors/exported_rrx_motion_alignment_tasks.txt
6. docs/restir_friendly/geometry_universe_rtxdi_remix_audit.txt
7. docs/restir_friendly/layered_debug_gates.txt

Reference roots
---------------

RTXDI:

    E:/prog/references/RTXDI-main

RTX Remix:

    E:/prog/references/dxvk-remix-git

rbdoom source anchors
--------------------

Motion-vector producer and projection:

    neo/shaders/builtin/pathtracing/pathtrace_smoke_rab_motion_supplier.hlsli
    neo/shaders/builtin/pathtracing/PathTracePrimarySurface.hlsli
    neo/shaders/builtin/pathtracing/pathtrace_primary_surface_producer.rt.hlsl
    neo/shaders/builtin/pathtracing/pathtrace_smoke.rt.hlsl
    docs/restir_friendly/motion_vectors/exported_rrx_motion_alignment_tasks.txt

Motion-vector consumers:

    neo/shaders/builtin/pathtracing/pathtrace_restir_reference_temporal_debug.hlsli
    neo/shaders/builtin/pathtracing/pathtrace_restir_local_debug_reservoirs.hlsli
    neo/shaders/builtin/pathtracing/pathtrace_restir_combined_resolve.rt.hlsl

CPU/resource path:

    neo/renderer/NVRHI/PathTraceFrameResources.cpp
    neo/renderer/NVRHI/PathTraceSmokeDispatch.cpp
    neo/renderer/NVRHI/PathTraceDLSSRRBridge.cpp

Non-negotiable rules
--------------------

- Do not invent a new motion-vector convention.
- Do not smooth, blur, clamp, or otherwise hide bad vectors as a fix.
- Do not change ReSTIR reservoir math, finalization, or sampling policy in a
  motion-vector slice.
- Do not paste NVIDIA RTXDI or RTX Remix source into rbdoom.
- Keep each implementation slice narrow, build/deploy after code changes, then
  stop for user visual testing.
- If using worker agents, use worker_launch_protocol.txt. The coordinator must
  choose the slice and give the worker a bounded owned write set; workers must
  not be asked to design strategy while coding.
