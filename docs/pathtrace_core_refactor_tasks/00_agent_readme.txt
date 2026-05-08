RBDoom3 BFG RT/PT Core Refactor Agent README
============================================

Repository:

    E:\prog\rbdoom-3-bfg-rt

Branch family:

    codex/refactor-pathtrace-smoke-modules

Read first:

    docs/pathtrace_core_refactor_plan.txt
    docs/pathtracing.txt
    docs/pathtrace_modules.txt
    docs/restir_pt_agent_bridge_plan.txt
    docs/pathtrace_geometry_universe.md
    docs/pathtrace_material_universe.md
    docs/pathtrace_interactive_surfaces.md
    docs/doom3_light_reference_for_codex_agent.txt
    docs/doom3_idtech4_material_reference_for_coding_agent.txt

Important working rules:

- Keep changes narrow and reviewable.
- Treat PathTracePrimaryPass and PathTraceSmokeDispatch as legacy orchestration
  to be reduced in controlled slices, not rewritten all at once.
- Preserve source3 as the baseline scene stack unless the user explicitly asks
  for source0 comparison work.
- Leave unrelated dirty work alone.
- Do not stage docs/pathtracing_handoff.txt unless the user explicitly asks.
- Do not touch neo/CMakeLists.txt, neo/extern/RTXPT, or
  neo/renderer/NVRHI/RTXPT unless the task explicitly requires it.
- If adding new neo/renderer/NVRHI/*.cpp files, regenerate the build tree with
  CMake before building.
- If editing pathtrace_smoke.rt.hlsl or included HLSL, rebuild
  PathTracingShaders. The main target alone can leave SPIR-V stale.

Build lanes:

    cmake -S neo -B build
    cmake --build build --config Debug --target PathTracingShaders
    cmake --build build --config Debug --target RBDoom3BFG

For performance-relevant validation, use the repo's Release/PT dev Release lane
instead of Debug or validation-layer-heavy runs. Debug is acceptable for compile
and correctness tracing only.

Prepared game folder used by prior validation:

    E:\prog\rbdoom-3-BFG-prebuilt

General validation expectations:

- Keep mode 18 and mode 20 usable unless the task explicitly changes them.
- Treat samples-per-pixel, max path depth, bounce limits, and first reflection
  support as core path-tracer work, not ReSTIR-only debug settings.
- Treat the current material classifier as functional stopgap logic. New
  material contracts must leave room for the later idTech4-material-document
  interpreter plus RBDoom3/mod PBR texture roles such as metallic/roughness,
  AO, height/parallax, and displacement.
- Treat source3 plus Doom renderer area/portal traversal as the validated BVH
  residency baseline. New geometry contracts must leave room for current and
  previous frame vertex/transform capture for motion vectors.
- Portal-area walks currently exist in several consumers. Before adding another
  one, inspect whether static preload, rigid residency, emissive light selection,
  and Doom analytic lights can consume one shared per-frame selected-area result
  with per-consumer filtering.
- Keep the future visible-portal fallback distinct from the default depth-4
  walk: line-of-sight portal boundary areas may be added as terminal selected
  areas, but should not recursively propagate connections.
- Keep modes 26-33 conservative; mode 32/33 should remain at or below the known
  720p-ish safety envelope until dispatch splitting is complete.
- Treat GPU device removal as a design signal. Average FPS is not enough:
  inspect per-pass dispatch duration, effective ray budget, barriers, resource
  replacement, descriptor rebuilds, and readback/debug work.
- Treat CPU/GPU handoff as a weak point. Doom 3's renderer is not highly
  multithreaded, so CPU staging, full-buffer uploads, descriptor rebuilds,
  readbacks, and unnecessary waits can trip frame pacing even with plenty of
  RAM/VRAM available.
- Use one-shot console diagnostics for scene/light/material validation.
- Prefer smoke validation plus focused log checks over large bespoke gameplay
  plans.
- Document ownership changes in docs/pathtracing.txt and
  docs/pathtrace_modules.txt when code changes land.

When to ask the user:

- You need the correct Doom/idTech4 source of original tangent/sign basis.
- You need to choose the long-term object/entity ID that will survive map
  transitions, entity updates, and save/load behavior.
- You need glass, liquid, GUI, particle, or material-parm animation policy.
- You need the final mapping from idTech4 material stages to PBR-style fields.
- You need to choose how parallax/displacement maps should affect BVH geometry
  versus shading-only normals/heights.
- You need to decide whether geometry and light portal-walk step counts should
  intentionally diverge or become one default frame setting.
- You need to decide whether a visible behavior change is acceptable.
- You are unsure whether a code path is game logic, renderer frontend, or debug
  artifact.
