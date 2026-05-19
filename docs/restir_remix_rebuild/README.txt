ReSTIR Remix-Shaped Rebuild
===========================

Purpose
-------

This folder is the commissioning package for the clean-room RTX Remix-shaped
ReSTIR rebuild.

The old rbdoom pathtrace smoke reservoir path is considered legacy/debug only
for this work. Do not build new ReSTIR behavior on:

    smokeReservoirDispatchSignature
    smokeReservoirSceneSignature as a light-payload reset gate
    m_smokeDoomAnalyticLightCount as temporal validity
    old mode-18 smoke reservoir semantics

The target architecture is to recreate the RTX Remix file and contract graph
with rbdoom-native code, one file-equivalent at a time. RTX Remix is the
authoritative file/contract graph. RTXDI is the authoritative reservoir math
contract. This is a clean-room direct port, not inspiration/reference work. Do
not copy/paste licensed source.


Critical Diagnosis
------------------

rbdoom currently lets Doom analytic light count, membership, or payload churn
reach broad reservoir and accumulation reset gates. This destroys history before
RTXDI temporal reuse can perform per-light/per-pixel rejection.

RTX Remix does not clear all RTXDI reservoirs because a light animates. It
updates current/previous light buffers and light mapping, then RAB/RTXDI
translation rejects only the affected selected-light path when mapping fails.

Therefore the first rebuild target is not final image quality. The target is:

    stable current light buffer
    stable previous light buffer
    current-to-previous light map
    previous-to-current light map
    light type ranges
    per-type sample counts
    RAB light translation/load
    reservoir ownership that is not tied to old smoke reset signatures


Read Order
----------

1. docs/restir_remix_rebuild/README.txt
2. docs/restir_remix_rebuild/remix_file_graph.txt
3. docs/restir_remix_rebuild/implementation_slices.txt
4. docs/restir_remix_rebuild/worker_protocol.txt
5. docs/restir_remix_rebuild/worker_prompt_rrx_00.txt
6. docs/restir_light_manager/session_recovery_dynamic_analytic_bug_2026_05_19.txt

Use older docs/restir_light_manager material only as evidence of the failed
path and current scaffolding. Do not continue old LM/LU prompt sequences unless
the user explicitly asks.


Authoritative Source Roots
--------------------------

RTX Remix:

    E:/prog/references/dxvk-remix-git

RTXDI:

    E:/prog/references/RTXDI-main

rbdoom:

    E:/prog/rbdoom-3-bfg-rt


Non-Negotiable Rules
--------------------

1. Clean-room port only.

   Mirror one exact Remix file's responsibility in rbdoom-native code. Preserve
   the same inputs, outputs, dataflow, invalidation semantics, and ownership
   boundaries. Do not paste Remix code into rbdoom.

2. No ReSTIR math changes.

   Do not alter reservoir weights, target PDFs, MIS, visibility policy,
   temporal thresholds, spatial thresholds, finalization, or RTXDI helper math.

3. One file-equivalent slice at a time.

   Workers must not "make it like Remix" broadly. Each worker owns one
   file-equivalent contract and a narrow write set.

   If a worker cannot identify the exact Remix file-equivalent before editing,
   it must stop and report the missing contract. Do not substitute an old rbdoom
   smoke path because it looks similar.

4. Old smoke reservoir path is not the foundation.

   Keep legacy paths available for comparison if needed, but new ReSTIR
   reservoir ownership must not depend on smoke reservoir clear semantics.

5. Dynamic light animation is not structural history invalidation.

   Color, intensity, radius, position, and on/off payload changes update light
   payload/mapping state. They do not clear the whole reservoir domain.

6. User visual testing remains required.

   Renderer-facing slices stop after build/deploy and report exact test CVars.
