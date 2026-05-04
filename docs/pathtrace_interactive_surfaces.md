Interactive Surfaces and Runtime Materials
==========================================

This note captures how Doom 3 communicates game-driven material state to the
renderer, and how the RT/PT path should reason about it.

Why this matters
----------------

Many Doom surfaces are not described by their material declaration alone. A
material can say "this stage can glow", while the current render entity says
"this instance is red", "this instance is green", "this instance is dimming",
or "this stage is off".

For PT/ReSTIR this is an important contract:

- Material-universe records are stable facts and capabilities.
- Per-frame scene capture owns evaluated runtime facts.
- Light candidate/reservoir data must eventually consume the evaluated runtime
  surface state, not only the stable material record.

The switchable fluorescent lights and red/green door panels are examples of
this split.

The data flow
-------------

1. A material stage declares expressions in `.mtr` files.
2. Game code, scripts, map spawnargs, lights, or entities set
   `renderEntity.shaderParms[]`, `renderLight.shaderParms[]`, or global shader
   parms.
3. The frontend evaluates the material expressions with `EvaluateRegisters`.
4. Each `drawSurf_t` receives evaluated `shaderRegisters`.
5. Raster rendering uses those evaluated registers for stage conditions and
   stage colors.
6. The RT/PT capture path can inspect the same registers from the draw surface.

The relevant renderer structure is not the raw material alone; it is:

    drawSurf->material
    drawSurf->shaderRegisters
    material->GetStage(stageIndex)->conditionRegister
    material->GetStage(stageIndex)->color.registers[0..3]

`conditionRegister` decides whether a stage is active. If the evaluated value is
zero, raster skips that stage. `color.registers[]` point at evaluated red,
green, blue, and alpha/intensity values for that stage.

Common shader parms
-------------------

Doom uses named shader parms for the low slots:

- `parm0` / `SHADERPARM_RED`
- `parm1` / `SHADERPARM_GREEN`
- `parm2` / `SHADERPARM_BLUE`
- `parm3` / `SHADERPARM_ALPHA`
- `parm4` / `SHADERPARM_TIMESCALE`
- `parm5` / `SHADERPARM_TIMEOFFSET`
- `parm6` / `SHADERPARM_DIVERSITY`
- `parm7` / `SHADERPARM_MODE`

These are only conventions, not a full semantic guarantee. Content can use
custom expressions freely. Still, the common meanings are useful for diagnostics:

- `colored` usually maps stage color to `parm0..parm3`.
- `parm7` is often a mode switch.
- `red parm0`, `green parm1`, `blue parm2` means the stage color follows the
  entity/light color.
- `red parm0 * global0` means both entity/light state and a global value affect
  the result.
- `rgb 5` is a literal intensity expression, not `parm5`.

Examples
--------

Door panels can use mutually exclusive additive stages:

    {
        if ( parm7 == 0 )
        blend add
        map textures/base_door/doorlight_red.tga
        colored
    }
    {
        if ( parm7 == 1 )
        blend add
        map textures/base_door/doorlight_grn.tga
        colored
    }

The material declares both red and green possibilities. The current entity
state selects one by changing `parm7`. The `colored` keyword means the selected
stage also depends on the evaluated color parms.

Fluorescent strip lights often use direct RGB expressions:

    {
        blend add
        map textures/base_light/striplight3_add.tga
        red   parm0
        green parm1
        blue  parm2
    }

Switchable/broken variants may gate whole sets of stages:

    { if ( parm7 == 0 ) ... }
    { if ( parm7 != 0 ) ... }

This can swap normal/diffuse/specular/emissive stages as well as the glow
stage. A surface may therefore keep the same material name while its evaluated
appearance changes substantially.

Why some lights dim instead of snapping off
------------------------------------------

The material expression does not have to be binary. Game code can animate
`shaderParms`, and material expressions can include time/global terms. If
`parm0..2`, `parm3`, or a global multiplier ramps toward zero, the evaluated
stage color ramps down even if the stage condition remains active. Raster sees
that as dimming. PT should treat this as a per-frame evaluated emissive color,
not as a change to the material's stable emissive capability.

How to detect special game-driven surfaces
------------------------------------------

At capture/debug time, a surface is suspicious if any stage has:

- a non-constant `conditionRegister`
- color registers that evaluate differently from `material->ConstantRegisters()`
- expressions referencing `parm0..parm11`
- expressions referencing global registers
- expressions referencing time/table functions
- multiple stages gated by opposite `parm7` conditions
- additive/glow-looking stages whose evaluated color changes frame to frame

Practical diagnostics should compare, per draw surface:

- material name and material id
- surface class/subtype
- stage index
- stage texture
- stage blend mode
- condition register index and evaluated value
- color register indexes and evaluated RGBA
- raw entity/light shader parms when available
- whether evaluated values changed since the previous frame for a stable-ish
  surface identity

For the current branch, the crosshair material dump is already a useful entry
point because it prints material stages, condition values, color values, blend
state, and the RT metadata chosen for the surface.

Future scoping plan
-------------------

Before adding broad PT support for these systems, build diagnostics that map the
runtime material space instead of discovering cases one surface at a time.

Add two complementary scans:

- Static material dependency scan. Walk parsed material declarations and record
  which stages can depend on `parm*`, `global*`, `time`, tables, stage
  conditions, alpha-test expressions, texture transforms, generated images, or
  deforms.
- Runtime visible-surface scan. Walk current draw surfaces and record the
  evaluated values that raster is actually using this frame:
  `drawSurf->shaderRegisters`, condition values, stage colors, alpha thresholds,
  texture-transform state, material id/name, surface class, model/entity hints,
  and stable-ish surface identity.

Suggested CVars:

- `r_pathTracingRuntimeMaterialScan 1`
  One-shot scan of visible draw surfaces with compact grouped output.
- `r_pathTracingRuntimeMaterialScanMode`
  Filter by category, for example all, emissive, alpha coverage, texture
  transform, stage selection, character/skinned effects, or generated textures.
- `r_pathTracingRuntimeMaterialWatch`
  Optional material-name substring or material id filter for focused repeated
  testing.

Useful output categories:

- `stable-material`
- `runtime-emissive-condition`
- `runtime-emissive-color`
- `runtime-alpha-coverage`
- `runtime-texture-transform`
- `runtime-stage-selection`
- `generated-or-animated-texture`
- `skinned-effect-overlay`

Example runtime dump shape:

    material='textures/base_light/striplight3'
    class=rigid-entity stage=3 blend=add image='...striplight3_add'
    depends=parm0,parm1,parm2 condition=1.000 color=(0.80 0.70 0.60 1.00)
    transform=none category=runtime-emissive-color

    material='monster/.../burnaway'
    class=skinned stage=alphaTest image='...mask'
    depends=time,parm7 alphaTest=0.420 transform=rotate/scale
    category=runtime-alpha-coverage,skinned-effect-overlay

The static scan answers "what can this material do?" The runtime scan answers
"what is this specific visible surface doing right now?" PT/ReSTIR work should
use both: static facts for capability and runtime facts for current-frame light,
coverage, and texture-coordinate behavior.

PT/ReSTIR guidance
------------------

Do not store these runtime values in the material universe as stable facts.
They are frame-local surface facts.

A future-safe contract should look like:

- Material universe: material can emit, texture slots/facts, alpha/cutout facts,
  roughness/specular approximations, stable material identity.
- Scene capture: this draw surface's evaluated stage state, including active
  emissive stage, RGB/intensity, selected texture/stage when possible, and
  whether the state appears game-driven.
- Emissive candidates: area/normal/UV/identity plus evaluated radiance proxy for
  this frame.
- Light universe: persistence only where identity and runtime state are stable
  enough; do not persist missing dynamic lights as if their occluder geometry
  also persisted.
- Reservoirs: consume evaluated candidate radiance, and reset/reproject when
  material table, candidate identity, geometry identity, or runtime light state
  invalidates history.

Q2RTX reference note
--------------------

The Q2RTX Ray Reconstruction reference fork in
`E:\prog\references\Q2RTX-rayreconstruction-master` is useful for off-camera
emissive design. Its important lesson is that emissive lighting is not driven by
the set of emissive surfaces visible in the current frame.

Q2RTX promotes static emissive BSP surfaces into persistent light polygon
records, assigns those records to BSP clusters, and builds per-cluster light
lists using PVS plus a cluster influence test. Shading/ReSTIR then samples the
light list for the cluster containing the shaded point. An emissive can
therefore affect the current pixel even when the emitting polygon is off camera,
as long as the map cluster/PVS/light-influence contract says it belongs in that
cluster's list.

Dynamic/model emissive lights are transformed each frame and injected into the
same cluster light lists. Their temporal history is handled by explicit
previous-to-current light identity remapping; if a previous dynamic light cannot
be matched to a current light, the shader treats that history as invalid. This
is the opposite of blindly keeping stale dynamic emissive candidates alive.

Implications for this branch:

- Static world emissives should become persistent light-universe records that
  can be sampled independently of current camera visibility.
- Stable rigid/entity emissives, such as object-like panels, need persistent
  identity plus current transform/area membership before temporal reuse.
- Game-logic emissives, such as switchable or dimming fluorescent lights, need
  persistent identity but per-frame evaluated active/color/intensity state.
- Truly transient or currently unmatchable dynamic emissives should remain
  current-frame injections until previous-to-current identity and matching
  occluder geometry are trustworthy.
- Future Doom-side work needs some equivalent of area/portal/cluster membership
  for light candidates. Persisting a light alone is not enough if the matching
  geometry or runtime material state can disappear.

Known pitfalls
--------------

- A material name alone cannot tell whether a surface is currently emitting.
- A material's stable emissive color may be white even when the current surface
  should be red, green, blue, dimmed, or off.
- Dynamic/rigid entities may be culled by Doom when off screen. Retaining only
  their light candidates without retaining matching occluder geometry creates
  ghost lighting.
- Switchable surfaces can swap more than emission. They may swap diffuse,
  normal, specular, and additive stages together.
- Adding GPU buffers or changing ray payload layout is high risk in this branch;
  prefer CPU-side diagnostics and candidate-only experiments before changing
  shader resource contracts.
