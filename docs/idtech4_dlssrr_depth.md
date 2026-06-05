# idTech4 Depth and the DLSS Ray Reconstruction Depth Contract

This note documents why the path-traced (RTXDI/ReSTIR) DLSS Ray Reconstruction (DLSS-RR)
depth input in rbdoom-3-bfg-rt needed special handling, what makes idTech4's depth
unusual relative to other DLSS-RR titles, and the configuration that finally produced a
depth buffer whose NVIDIA DLSS-RR debug overlay reaches parity with shipping DLSS-RR
games.

It also records, honestly, what this fixed and what it did **not** fix.


## TL;DR

- idTech4 (Doom 3 BFG / rbdoom) renders with an **infinite-far** projection. Every other
  DLSS-RR title uses a **finite-far** frustum, which is what DLSS-RR's depth contract
  assumes.
- DLSS-RR derives its working depth from the **frustum description**
  (`cameraViewToClip` + `cameraNear`/`cameraFar` + `depthInverted`), so the depth buffer
  and that description must be mutually consistent and finite.
- The clean DI path now feeds **hyperbolic hardware depth** from a dedicated **finite-far**
  projection, tagged `kBufferTypeDepth`, with a tunable near/far.
- The depth **distribution** is governed by the **far/near ratio**. idTech's default
  `r_znear = 3` against a large far gives a ratio so high (~1300+) that the whole scene
  crams into the last fraction of a percent of the range. A small near (~0.2) with a
  scene-scale far (~2048) matches the distribution shipping games use.
- Validated defaults for Doom 3 BFG:
  `r_pathTracingDLSSRRDepthMode 2`, `r_pathTracingDLSSRRCameraNear 0.2`,
  `r_pathTracingDLSSRRCameraFar 2048`, `depthInverted = 0`.


## Why idTech4 depth is the odd one out

idTech4's perspective projection (`R_SetupProjectionMatrix`, `neo/renderer/GLMatrix.cpp`)
is an **infinite far plane** projection (column-major, `m[col*4+row]`):

```
m[2*4+2] = -0.999        // z scale  (not -far/(far-near); ~ -1 as far -> inf)
m[3*4+2] = -zNear        // z translate (not -near*far/(far-near); ~ -near as far -> inf)
m[2*4+3] = -1.0          // perspective divide: clip.w = viewZ
m[3*4+3] =  0.0          // <- infinite-far signature
```

After the perspective divide this yields NDC `z = 0.999 - zNear/viewZ`: it starts at 0 at
the near plane and **asymptotes to 0.999** at infinity, never reaching a real far plane.

DLSS-RR (like most temporal techniques) assumes a conventional **finite** frustum: depth
that reaches 1.0 (or 0.0 for reverse-Z) at a real far plane, described by a matching
`cameraViewToClip` and `cameraNear`/`cameraFar`. Handing DLSS-RR idTech's infinite-far
matrix means it cannot recover a sane near/far, so it mis-normalizes the depth and the
debug overlay collapses the scene into a thin sliver.


## What DLSS-RR actually consumes

Empirically (via the per-frame `PathTraceDLSSRR: CONTRACT ...` verbose dump and the
DLSS-RR debug depth overlay, `Ctrl+Alt+F12` in a dev build):

- **`kBufferTypeDepth` (hyperbolic hardware depth) is the form this integration actually
  uses.** It is the only depth form that the overlay rendered meaningfully.
- **`kBufferTypeLinearDepth` did not work here.** Normalized `[0,1]` linear produced a
  black overlay (DLSS interpreted the small values as "0–1 world units" = everything at
  the near plane); raw view-space-Z linear produced a flat, no-dynamic-range overlay.
  Whatever the SDK documents, in this Streamline/NGX build the RR path expects hardware
  depth plus a finite-far `cameraViewToClip`.
- The scalar `cameraFar` only affected the result once it actually drove the depth
  **values** (the finite-far projection's far plane). As a standalone constant against an
  infinite-far matrix it did nothing — further evidence DLSS-RR reconstructs depth from
  the matrix/frustum, not from the texture alone.

The DLSS-RR debug depth overlay is a near→far false-colour ramp of the (DLSS-reconstructed)
depth: black at the near plane through blue/purple/pink to white at the far plane. It is
**not** a validity signal; a colour ramp that matches a reference game simply means the
depth distribution matches.


## The far/near ratio is the distribution knob

Hyperbolic depth places most of its precision near the camera. The spread across the scene
is controlled by the **far/near ratio**, dominated by `near`:

```
NDC z (finite-far) = far/(far-near) * (1 - near/viewZ)
```

- `near = 3`, `far = 4096`  -> ratio ~1365: ~99% of the range is used by the first ~30
  units; the rest of the room crams into the last < 1%. This was the overlay "outlier".
- `near = 0.2`, `far = 2048` -> ratio ~10000 numerically but, matching the small-near
  convention shipping games use, the **overlay distribution matches reference games**.

(Note: shipping DLSS-RR titles also use small near planes; "parity" means matching their
near/far, not minimising the ratio. A larger near spreads precision differently but no
longer looks like the reference.)


## Final configuration and CVars

The clean DI path (`pathtrace_primary_surface_producer.rt.hlsl` via
`cleanroom_rtxdi/pathtrace_clean_rtxdi_di_rr_geometry_guides.hlsli`) writes
`PathTraceRRGuideDepth` using `(near, far, mode)` carried in `RRProjectionDepthInfo`
(packed C++-side in `PathTraceSmokeDispatch.cpp`). `PathTraceDLSSRRBridge.cpp` builds a
matching finite-far `cameraViewToClip` (overriding only the two depth/z-row terms of
idTech's projection so X/Y/offsets/Y-flip are preserved), tags the buffer per mode, and
sets `cameraNear`/`cameraFar`/`depthInverted` consistently. `motion.z` is derived from the
same depth helper so it stays coherent.

CVars (all `r_pathTracingDLSSRR*`):

| CVar | Default | Meaning |
|------|---------|---------|
| `DepthMode` | `2` | `2` = hyperbolic hardware depth (`kBufferTypeDepth`, used); `0` = normalized linear `[0,1]`; `1` = raw linear view-Z (`0`/`1` tagged `kBufferTypeLinearDepth`). |
| `CameraNear` | `0.2` | RR depth frustum near (world units). `<=0` uses `r_znear`. |
| `CameraFar` | `2048` | RR depth frustum far (world units). `<=near` falls back to `100000`. |
| `ReverseZ` | `1` | Legacy/hyperbolic-matrix only; inactive for the linear modes. |
| `Verbose` | `0` | `1` prints the one-time `PathTraceDLSSRR: CONTRACT ...` dump (tag, near/far, FOV, `depthInverted`, mvecScale, jitter, `cameraViewToClip`, `clipToCameraView`). |

The CONTRACT dump exists so this integration's depth/camera contract can be diffed
field-by-field against a known-good DLSS-RR game captured with an `sl.interposer.json`
(`logLevel: 2`) dropped next to that game's executable.


## Scope: what this fixed, and what it did not

**Fixed.** The DLSS-RR debug depth overlay now reaches parity with reference DLSS-RR games:
the depth is finite-far hardware depth with a self-consistent frustum and a distribution
that matches shipping titles. rbdoom is no longer identifiable as feeding a fundamentally
different depth source.

**Not fixed (still open).** Achieving depth parity produced only a **minor** improvement to
the residual temporal "boiling" seen under camera/player motion (worst on floors and other
surfaces with a steep on-screen depth gradient; a near-flat wall is stable). Because
matching the depth exactly did not resolve the motion boiling, **depth was not its root
cause** — the depth overlay was a visible-but-coincidental outlier. With depth, motion-vector
sign, clip-history matrices and depth distribution now all eliminated, the remaining
suspects are depth-independent: jitter convention reported to DLSS vs the actual ray jitter,
a periodic (~1–2 s) history reset under motion, and the camera-basis/`cameraMotionIncluded`
contract. The recommended next step is to capture a reference game's **full** contract via
`sl.interposer.json` logging and diff every motion-related field (not just depth) against the
`PathTraceDLSSRR: CONTRACT` dump.
