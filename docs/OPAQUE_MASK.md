# Design: skip shading where the overlay is fully opaque

Status: built for flat screens only. There is no VR occlusion in this plugin or in imgui-vr-helper: a
depth write from outside the renderer cannot work, because the screen-space effects have already sampled
the depth by the time the overlay draws, so the world behind the visor is shaded incorrectly rather than
skipped. The VR numbers below are kept as analysis for a future coverage-mask design; they describe what a
correct implementation would save, not what anything does today.
Engine facts come from static analysis of the SE, AE and VR executables and the open-shaders sources (gbrain `reference-overlay-opaque-mask-injection-points` has the
full tables); anything marked "unverified" is inferred and has not been run.

**What has and has not been verified.** Three things are proven on real hardware and one is not. The depth
reject itself was confirmed in a GPU capture (RenderDoc, SE, frozen pose): the mask stamps the near value into
kMAIN, and world batches behind it log `depthTestFailed` instead of shading. The screen-space effect path was
traced in the same capture: the shading consumers read a depth copy taken _before_ the stamp, which is why the
world is not mis-shaded. The 3 px inset's presence is pinned by unit tests (mutation-tested against `floor` and
`round`). **What is not verified at all is moving art**: every capture above was taken at a fixed head pose with
the overlay still, so the one-frame prediction, the inset at a moving edge, and the behaviour while a fade is in
progress have only ever been reasoned from the code. Treat those as untested rather than working.

Two limits on what the inset tests prove, worth stating because the test names suggest more. They pin the
ARITHMETIC of the step, not the shader's behaviour at an edge that is moving between the frame the mask was built
and the frame it is stamped; those differ, and only a capture resolves it. And they pin the step the shader is
given, not that the shader turns it into the pixels the CPU predicted.

That second gap is now partly closed: tests/test_mask_gpu.cpp rasterises the shipped vertex and pixel shaders on
a WARP device and compares the stamped coverage against the CPU's own prediction, so the conventions the two sides
must agree on - the NDC-to-pixel factor, the viewport origin, the UV mapping - are checked on every build rather
than in a manual capture. It does not replace the capture: it covers the mapping on a synthetic mask, not the
engine's real kMAIN bindings, DRS viewport, or a mask built from actual art.

The reason this was mistaken for impossible is worth recording, because the same reasoning produced a wrong
conclusion here. A WARP harness appears not to rasterise - six independent arms (explicit viewport, CULL_NONE, a
real vertex buffer with a POSITION input layout, a blend state, depth test off, and ImGui's own DX11 backend) all
returned the clear value. All six shared a null render target, from binding `winrt::com_ptr::put()` (which
releases the pointer and returns the address of the empty slot) instead of `get()`. Agreement across arms that
share the constant under suspicion is one observation repeated, and repetition feels like corroboration precisely
because it costs effort. An ablation is only evidence if its arms differ in the suspected cause.

## Idea

Where the helmet overlay is fully opaque the scene behind it can never be seen, so the engine does not
need to shade those pixels. The mask must cover only those pixels. Anything the overlay blends (alpha 1 to 254) must still render, because the player sees it through the overlay.

## Why it can pay off

Measured on the Enhanced Edition overlays at the 1920x1080 canvas:

| Overlay       | alpha = 255 | alpha >= 254 | after 8 px erosion | blended (1..254) |
| ------------- | ----------- | ------------ | ------------------ | ---------------- |
| Daedric       | 47.4%       | 47.9%        | 45.6%              | 9.4%             |
| Iron          | 36.2%       | 36.6%        | 34.3%              | 11.4%            |
| Elven         | 21.5%       | 21.8%        | 19.8%              | 7.2%             |
| Steel (light) | 18.7%       | 19.0%        | 17.0%              | 19.7%            |
| Hide (light)  | 17.3%       | 17.7%        | 15.7%              | 7.4%             |
| Mage Hood     | 7.9%        | 8.1%         | 7.0%               | 5.1%             |
| Dragon priest | 42.8%       | 43.5%        | 40.7%              | 57.2%            |

- The opaque areas are large solid blocks, so a safety margin costs little: 8 px of erosion removes under
  two points of coverage.
- Dragon-priest style overlays are mostly translucent. The rule "alpha 255 only" gives them a mask of
  about 43% and still draws their 57% blended area.
- These are shares of the overlay canvas, not of rendered pixels. Part of the canvas lies outside the lens
  view, and in VR the engine's own hidden-area mesh already masks the lens corners, so the new saving is
  the opaque region minus what is already masked.
- **Share of the visible image.** In VR the plane over-fills the view (coverage 1.25), so only the central
  87% of the canvas is on screen, and the lens shape cuts the corners where a closed helmet's frame tends
  to be. Flat shows the whole 16:9 canvas. Masked share after 8 px erosion (the lens column is a rough
  ellipse inscribed in the visible rectangle, a proxy for the real lens cut and hidden-area mesh):

  | Overlay       | Flat (full canvas) | VR visible rectangle | VR lens-shaped |
  | ------------- | ------------------ | -------------------- | -------------- |
  | Daedric       | 45.6%              | 38.9%                | 32.2%          |
  | Iron          | 34.3%              | 21.7%                | 11.6%          |
  | Elven         | 19.8%              | 8.7%                 | 3.6%           |
  | Steel (light) | 17.0%              | 4.4%                 | 0.9%           |
  | Hide (light)  | 15.7%              | 1.5%                 | 0.0%           |
  | Mage Hood     | 7.0%               | 0.6%                 | 0.0%           |
  | Dragon priest | 40.7%              | 31.0%                | 24.0%          |

  So the mask is worth most on flat and for heavy closed helms in VR; for open helmets and hoods in VR it
  is close to nothing.

- What the saving applies to: from existing Tracy notes, the per-pixel costs are Deferred Geometry (about
  8 to 22 ms in the captured scenes), NR (about 5 ms), Skylighting and the composite passes; shadow
  rendering is geometry-bound and would not shrink. So the expected saving is roughly the masked share of
  Deferred Geometry plus the passes that early-out. No per-pixel profile exists, so the real figure is
  unknown until measured.
- **Expected saving (an estimate, not a measurement).** The saving is the masked share of visible pixels
  times the share of GPU frame time spent in per-pixel passes that the early depth reject skips. The
  z-prepass runs before the injection point, so its vertex and raster work is not saved; only pixel work in
  the main passes and anything that early-outs on near depth is. If per-pixel passes are about half of a
  GPU-bound frame, a closed helmet on flat could save on the order of 10 to 20% of GPU frame time (Iron
  34% masked, Daedric 46%), a heavy closed helm in VR roughly 5 to 15%, and open helmets and hoods in VR
  about 0 to 3%. A CPU-bound frame saves nothing. The mask draw itself (a fullscreen triangle, one texture
  sample, a depth write) should cost well under a tenth of a millisecond.

## The mask

- **Definition.** A canvas pixel is masked when its alpha is 255 and every pixel within the safety margin
  is also 255. Alpha 254 is left to draw; excluding it costs under half a point. The test is on the byte the
  renderer actually draws (`OverlayAlpha::Byte`, which truncates), not on the opacity setting: an opacity of
  0.999 draws 254 and is therefore not maskable, even though the UI prints it as "100%".
- **Safety margin.** Erode by at least 4 px at canvas resolution (8 by default). This covers texture
  filtering, TAA and upscaler taps that read across the mask edge, and the one-frame delay below. The
  margin is measured in _art_ pixels, so it shrinks on screen when the art is scaled down; the pixel
  shader adds a fixed 3 px _screen_ inset on top of it, which cannot shrink. See "The fringe" below.
- **Storage.** Build it once when an overlay loads, from the alpha already decoded for the see-through
  measure: an `R8_UNORM` texture at 480x270, where a texel is 255 only if all of the 4x4 source pixels (after
  erosion) are opaque (a min-pool, never an average). About 130 KB per overlay. Nearest sampling.
- **When it is active.** All of these must hold, otherwise no mask is drawn:

  - the overlay layer is fully faded in (progress 1) and its target is shown;
  - the opacity draws at alpha 255, i.e. `OverlayAlpha::IsFullyOpaque`; anything that truncates to
    254 is left to draw;
  - the overlay is not hidden by a menu, by first-person gating, or by being disabled;
  - the active layer is the only layer (no cross-fade in progress).

  These conditions live in `OverlayAlpha::PermitMask` (src/OverlayAlpha.h), which returns a `MaskPermit`
  that cannot be constructed any other way: a default-constructed permit is never allowed. That is
  deliberate. When the gate returned a plain `bool`, replacing the call site with its own inline opacity
  comparison still compiled and still passed every test, because the tests covered the decision function
  while nothing checked that the shipped code called it. With the permit as a type, the call site cannot
  substitute its own comparison and still obtain permission to stamp.

  The mask state is computed at the end of frame N (where the controller already updates) and consumed
  while rendering frame N+1. A fade can therefore start with the mask still on for one frame. At the fade
  rates in use that leaves under about 6% of black under pixels that are about 94% opaque, which does not
  show over art that is nearly black. If it does, switch the mask off one frame ahead of a fade.

## Mapping the mask into the scene

- **VR.** The overlay is a head-locked plane at depth d with width and height from the larger of the two
  eyes' half-FOV tangents times the coverage setting. For an eye with offset e (half the IPD, signed) and
  raw projection tangents, a pixel with tangent coordinates (tx, ty) hits the plane at head-space
  x = e + tx _ d and y = ty _ d, so the mask coordinate is u = x / planeWidth + 0.5,
  v = 0.5 - y / planeHeight. The pixel shader needs only d, e, the plane size and the eye's tangent
  bounds, all of which the helper already computes for its own composite, so the mask carries the same
  per-eye parallax as the visible overlay. How the eye viewport rectangle is laid out in the engine's
  stereo-instanced mode is unknown and must be read from the bound viewport.
- **Flat.** The overlay is drawn with a cover-fit, a scale, and a sway offset. The mask applies the same
  transform to the same texture coordinates. The mask is used by the next frame's world render, so it is
  published with the overlay's offset extrapolated one frame ahead (offset plus last frame's change). It
  drops out for a frame when the change in speed exceeds `kMaskMaxAccel`, which the safety margin covers.

## Where the draw goes (same point for VR and flat)

**SE and AE flat have no hidden-area mask**: there is no `DrawHiddenAreaMesh`, no `bUseHiddenAreaMesh`,
and no immediate write of the engine's stencil-skip mode (`0x1d`) on flat, while VR has five. So flat needs
a new draw, and the same draw works on VR.

Frame order, identical on SE, AE 1.6.1170, AE 1.7.104 and VR (`Main::Draw`: SE id 35560 `0x1405b1860`, AE
id 36559 `0x1406444b0`, VR `0x1405b9330`): RenderDepth, RenderShadowmasks, Occlusion,
PrepareMainDepthPrepass, RenderWorld, RenderFirstPersonView. RenderDepth clears kMAIN depth and stencil,
draws the prepass, then copies depth to the post-prepass copy.

**Injection point.** A call-site thunk that HelmetOverlaysNG installs itself, before the original call that
begins the first opaque batches. Plugins load alphabetically, so ours is installed after Community Shaders'
thunk on the same call and runs first; Community Shaders' `Deferred::StartDeferred()` only swaps color
targets and does not clear depth, so our depth write survives it. The hook site is `BSShaderAccumulator::FinishAccumulatingCommonPasses` (SE id 99938 `0x1412cacd0`, AE id 106583
`0x1414b2d90`, VR `0x1413093a0`) at +0x8E (SE, VR) or +0x84 (AE). At that moment the prepass is finished,
the post-prepass depth copy is taken, the shadow masks have consumed the stencil, and on VR the stereo
optimization has already written its stencil.

**State for the mask draw:**

- Bind no color target and the kMAIN depth-stencil view; no color write.
- Depth test ALWAYS with depth write, writing the near value for the active convention: 0.0 under standard
  depth, 1.0 under reverse-Z. The convention is read from the engine camera's projection matrix: reversed
  when `m[10]` times the perspective term is not positive. SE with Community Shaders and VR both report
  reversed depth.
- Fullscreen triangle, pixel shader samples the mask and discards unmasked pixels, then writes depth.
- Draw only when the shadow-state depth target is kMAIN. The hook also fires for shadow, reflection and
  cubemap passes, and the bound view lags behind the shadow state in VR.
- Flat uses a viewport of the display size times the dynamic-resolution ratio. VR renders one side-by-side
  depth buffer, so the same mask is drawn into the left and right halves; the stencil is written as `0xFF`.
- Restore the previous state and the G-buffer targets afterwards.

**AE (verified in both AE 1.6.1170 and AE 1.7.104).**

| Item                                              | AE 1.6.1170   | AE 1.7.104                |
| ------------------------------------------------- | ------------- | ------------------------- |
| `FinishAccumulatingCommonPasses` (id 106583)      | `0x1414b2d90` | `0x14151ef40`             |
| Hook at +0x84 reaches `RenderBatches` (id 106609) | `0x1414b4fe0` | `0x141521190`             |
| `Renderer::CreateDepthStencilStates` (id 77650)   | `0x140e657b0` | `0x14102b120`             |
| `BSGraphics::SetDirtyStates` (id 77386)           | `0x140e4b5b0` | `0x141010de0`             |
| `RenderWorld` (id 107142)                         | `0x1414cdaa0` | `0x141539f10`             |
| `RenderFirstPersonView`, after `RenderWorld`      | `0x1414cb6d0` | `0x141537b10` (id 107129) |

The state table matches SE, no stencil-skip mode (`0x1d`) is written anywhere on AE, and open-shaders'
existing hook offset (+0x84 on AE, +0x8E on SE and VR) lands on the first `RenderBatches` call in both
builds. `FinishAccumulatingCommonPasses` switches the depth test to EQUAL when the accumulator asks for
it and early-Z is off; with early-Z on the test stays LEQUAL, and a near-depth overwrite still rejects
every draw because nothing is nearer than the near value. The stencil values the AE z-prepass, utility,
lighting, water and particle code use were read from the AE decompiles and match the SE inventory; what
each water and particle mode means is taken from the (verified) state-table builder.

**Why depth alone is enough.** On flat, `RenderWorld` runs with equal-depth testing, so a pixel overwritten
with near depth fails the early depth test for every EQUAL and LEQUAL draw and none of its geometry is
shaded. On VR the main lighting batches also do not test stencil, so depth is the early-out there too.

**Stencil.** Optional. On VR write `0xFF` so the passes that already skip hidden-area pixels (sun glare,
shadow lights with the utility shader, SSAO, the water/TAA imagespace pass, the HDR imagespace pass) skip
these too. Flat has no such consumers, so skip stencil there. Collisions to avoid: values 1..N are
focus-shadow indices on flat; `0xFF` is also written by the normal-clear utility technique; bits 0 to 7
belong to water; and with VR stereo culling on, the stereo pass clears kMAIN stencil to 0, marks Eye 1
with 1 and swaps every depth-stencil state for a not-equal-1 clone, so under stereo culling the mask
should write 1 rather than `0xFF` (unverified).

**Optional second draw.** To let the VR imagespace consumers (SSAO, HDR) see the mask, draw it again into
kMAIN_COPY when the overlay or the dynamic-resolution ratio changes, as the engine does for its own mask.

## Savings beyond the early depth reject

- `DeferredCompositeCS` only returns early for far-depth pixels, so it still shades near-masked pixels.
  Adding a near-depth early-out in open-shaders would save that compute pass.
- `ClearHMDMaskCS`, SSGI depth prefilter (VR) and bend SSS already treat depth equal to the near value as
  masked, so they skip these pixels without changes.

## Who owns what

| Component        | Role                                                                                                              |
| ---------------- | ----------------------------------------------------------------------------------------------------------------- |
| HelmetOverlaysNG | Flat screen: builds the mask texture, decides when it is allowed, hooks the world batch and draws it              |
| open-shaders     | Nothing required; its thunk on the same call runs after ours and does not touch depth                             |
| imgui-vr-helper  | VR: nothing today. Its depth stamp exists behind an option but is idle under `useRuntimeOverlay=true` (see below) |

The draw lives in this plugin so it works with or without open-shaders and needs no exported function.

### Why the VR stamp is idle when SteamVR hosts the overlay

The helper's stamp can only cover a quad it drew into the eye image. When `useRuntimeOverlay=true` SteamVR
composites the overlay itself, `PanelQuadsForEye` returns zero, and the option does nothing - silently,
because a gate that refuses to stamp a quad it never drew is behaving correctly. Reconstructing the quad
from `ResolveAnchorWorld` plus `widthMeters` and aspect is not a missing-line problem: the shared geometry
is easy (`Overlay::Config::CreateScaleMatrix(s.menuScale)` and the same transform copied into
`in.anchor[kHmdAnchor].transform`), but the stamp must project through the game's `ComputeEyeMatrices`
while the compositor projects through SteamVR's own matrices, which are not queryable. A size error would
save less and stay safe; a POSITION error punches a hole in visible world, and position is the term that
cannot be checked. Eroding to dominate any plausible pose or projection mismatch shrinks the stamp toward
useless while still not bounding a systematic offset, so a conservative margin does not rescue it. There is
also no configuration that exercises both derivations on one machine, so the mismatch cannot be falsified
here. Left unimplemented deliberately rather than half-done.

**A detector exists for absence and it is a trap for the case that matters.** The helper reports
`cull_scheduled` / `cull_applied` per client, and under hosting both read 0, so a client can query and learn
that no stamp ran. That is a genuine detector for a stamp that never happened. It cannot detect a stamp that
ran in the wrong place: `cull_applied` becomes 1 the moment the draw executes, whether or not the quad sits
where the panel does. So the field reports success in exactly the failure mode this section is about, and a
future implementer who finds `cull_applied == 1` will read it as confirmation. Treat a non-zero
`cull_applied` as "the draw ran", never as "the right pixels were covered".

**Why trying it is not cheap.** A wrong anchor is not silent to the wearer — they see a hole in the world
immediately. The silence is in the instrument: the report is "something is wrong" with no way to separate
anchor error from projection error from pose error from a stale anchor, because every detector in this
document and in the helper checks presence, depth value or reject, none of which move. So a session buys one
bit, and the bit is already known. That is the actual cost, and it is why this is a decision rather than a
todo.

## Measured

Same first-person replay, head sway off, GPU time of the world batch call summed over three sampling
windows of 1500 calls each:

- **SE (Community Shaders loaded):** 7.41 ms and 7.14 ms with the mask off, 6.60 ms on, about 8-11% less.
  Two off runs differ by about 4%, so treat this as a likely high-single-digit to 10% saving of that pass.
  Those runs are on a scene whose world batch costs 7.4 ms, which is what makes the saving visible at all;
  see the null result below for why a typical interior cannot reproduce it.
- **VR (null driver, 2160x1200 side-by-side target):** 2.83 ms off, 2.32 ms on (-18%) from the withdrawn
  depth-stamp experiment. Kept as an upper bound for what a renderer-safe backend could recover, not as a
  current result: the VR mask no longer exists.
- **Sway and motion:** the mask follows them with the one-frame prediction above. An earlier version
  switched it off whenever the overlay moved (466 toggles in one sway replay) and kept little of the saving.
  The prediction is not verified on moving art: every capture that proves the depth reject was taken at a
  fixed head pose with the overlay still, and the 3 px screen inset that stops a fringe at the edges is
  reasoned from the mask's texel grid rather than seen to work in motion. Treat moving overlay edges as
  untested until a capture lands on it.

## Interactions to check

- **ClearHMDMaskCS and the upscaler.** It zeroes the upscaler's input color wherever depth equals the
  near value. If the overlay were composited into scene color before the upscaler it would be erased
  there. The overlay is composited by the helper into the eye image at submit in VR, and drawn in the HUD
  menu after the scene in flat, so it is after the upscaler in both; confirm in a capture.
- **Masked pixel color.** Masked pixels keep the engine's clear color. Bloom, DoF or TAA can bleed it into
  visible neighbours; consider a black clear, and rely on the margin otherwise.
- **VR Stereo Optimizations.** See the stencil note above; test with StereoMode on and off.
- **SSAO and SSGI.** Depth cliffs at the mask edge can darken neighbours. Drawing the mask after the
  post-prepass copy limits this to readers of kMAIN. The margin keeps it inside the opaque region.
- **Grass Hi-Z.** Fully near-masked tiles read as near occluders, which is correct.
- **Occlusion culling.** The engine's depth-buffer culling reads a downscaled depth; make sure it does not
  cull geometry visible through the blended area.
- **HUD and menus.** The overlay yields to the menus chosen in settings; the mask must switch off with it.
  HUD elements are drawn after the scene and are unaffected.
- **Runtimes.** The stencil inventory is verified for SE, AE 1.6.1170, AE 1.7.104 and VR. Still unknown:
  whether anything clears stencil between the prepass and the water pass.

## Settings

`bOpaqueMask` (default on), `iMaskMarginPx` (default 8), shown on the Overlay page with
the measured saving once measured.

## Validation

### Queued run: cost of the overlay, and of the flat perf option

Four arms. The arm names are what goes in the results table; nothing is inferred from an earlier run.

| Arm                | Overlay           | Opaque mask          | What it answers                                       |
| ------------------ | ----------------- | -------------------- | ----------------------------------------------------- |
| `se-off`           | disabled          | n/a                  | baseline: the scene with no overlay work at all       |
| `se-maskoff`       | worn, opacity 1.0 | off                  | what drawing the overlay costs                        |
| `se-maskon`        | worn, opacity 1.0 | on                   | the perf option, and the difference from `se-maskoff` |
| `vr-off` / `vr-on` | disabled / worn   | n/a (VR has no mask) | what the helper's composite costs in VR               |

`se-maskon` minus `se-maskoff` is the perf option's saving. `se-maskoff` minus `se-off` is the overlay's own cost,
which no run has measured yet. VR has no mask to toggle: the depth-stamp experiment was withdrawn, so the VR arms
only measure the overlay's cost and give the helper a baseline for whatever backend it lands.

Each arm needs its own launch, because a second Tracy connection to the same process returns no GPU zones. Order the
SE arms `off, maskoff, maskon, maskon, maskoff, off` (a bracketed pair, three launches of the option-off build state) so
a drift between the first and last leg is visible rather than being read as the effect.

Before the first measured leg:

1. Toggle "Skip shading behind opaque overlay" once in-window and confirm the log shows `Overlay mask: drawing`
   afterwards. The mask is rebuilt on a settings change; without that line a `se-maskon` leg is really `se-maskoff`
   and will read as a false null.
2. `fOpacity=1.0` (the mask requires the drawn alpha to be exactly 255, checked with `OverlayAlpha::IsFullyOpaque` in src/OverlayAlpha.h; anything that truncates to 254 is left to draw) and one overlay worn, so
   `_layers.size() == 1` holds. Sway and motion off, head still, so the one-frame prediction does not gate the mask
   off mid-leg.
3. Warm-up replay with Tracy detached, then one measured leg per arm (the standing protocol).

Record per arm: GPU `Deferred Geometry`, `Deferred::World`, `Effects` and `Skylighting::OcclusionMask` in ms/frame
over the same replay slice, plus frame p50/p90 for context only. Report the difference as a share of the pass it
applies to and as a share of a 16.67 ms flat budget / 11.11 ms VR budget.

### Result: replay-aligned A/B on SE (2026-10-31) - null, and why

Run as specified above (`off`, `maskoff`, `maskon`, one launch each, warm-up replay with Tracy detached,
connect on `replay starting`, `save_trace` then load). Route `DragonsreachHelmetFirstPerson.json`,
6814 steps, ~67.6 s, Iron Helmet, `fOpacity=1.0`. The mask was confirmed live: the log shows 69
`Overlay mask: drawing, reversed depth` intervals covering 94% of the session.

This measurement is the plugin's own draw and nothing else. `Connect()` returns before registering
unless the runtime is VR (`Renderer::Connect`, which returns early on `!REL::Module::IsVR()`), the helper is not loaded in SE, and the flat mask is
a fullscreen triangle in src/OverlayMask.cpp that writes `SV_Depth` itself. So no helper client mode,
registration flag or HUD/panel path is on this path, and a change to any of them cannot invalidate these
numbers. (Recorded because the question was raised: imgui-vr-helper registers HUD-mode clients with
`kClientFlag_HUDMode`, and its stamp skips clients that register focus/cursor flags without HUDMode. That
gap is real but is about the helper's own menu, not about this measurement.)

| `Deferred::World` (ms) | value                                                              |
| ---------------------- | ------------------------------------------------------------------ |
| overlay off            | 1.03 - 1.09                                                        |
| overlay on, mask off   | 0.97 - 1.04                                                        |
| overlay on, mask on    | 1.09                                                               |
| maskon - off           | +0.086                                                             |
| maskon - maskoff       | +0.140 (and -1.455 ms on total frame time, i.e. the opposite sign) |

**The effect is below the noise floor in this scene, and the sign is not even stable.** Split the _same_
maskoff capture into halves and `Deferred::World` differs by 0.64 ms between them: replay phase (moving
through rooms of different lighting) dominates any residual misalignment between launches, and the
between-launch drift alone (0.07 ms) is the size of the effect being hunted.

The reason is the scene, not the mask. This world batch costs about 1 ms, so 8-11% of it is 0.08-0.11 ms on
a ~10 ms frame, roughly 1%. The earlier 8-11% was measured where the batch costs 7.4 ms, so the same
percentage is ~0.8 ms and visible. **To measure this option you need a scene whose world batch dominates the
frame**; a Dragonsreach interior with one helmet is not one. The mask also only pays off in proportion to the
screen area it covers, which for a normal visor is a fraction of the view.

What this run does establish: the option is not a lever in ordinary interior play, so it should not be
described as one in the UI or the readme. The absolute numbers above are an upper bound on its value here.

Still not done: a heavy-scene leg that could confirm or refute the 7.4 ms result, and any VR leg (VR has no
mask to toggle, so only the overlay's own cost is measurable there).

### Correctness: verified in a GPU capture on SE (2026-10-31) - it works

A frame-time A/B cannot settle whether the mask works, so this was checked mechanically instead: two
RenderDoc-injected launches of the same save, loaded at the same free camera pose (`-448, -128, -318`, yaw
15.28, pitch 358.14) with `fOpacity=1.0`, one worn Iron Helmet, sway/motion/tint/reactive off, identical
settings except `bOpaqueMask`. Capture 1 (mask off): 47028 events, `frame2517`. Capture 2 (mask on): 45615
events, `frame2785`. Community Shaders HDR and frame generation off, upscale method `kTAA`, so the trace is
the real D3D11 frame rather than a D3D12 present.

The mask draw is unambiguous in the trace. It is the only draw in either capture that is `Draw()` with 3
indexed vertices, `numInstances == 0`, a `D3D11_COMPARISON_ALWAYS` depth test, `DepthWriteMask = ALL`, and a
**zero colour write mask**, bound to the main depth target (`D32S8_TYPELESS`, 2560x1440) with no render
targets at all. Its pixel shader declares `SV_Depth` and nothing else. In the mask-on capture that is event
**42634**, the 51st of 138 draws that touch the main depth target. It reads a 480x270 `R8_UNORM` texture - the
mask - in PS slot 0. No such draw exists in the mask-off capture.

| check                                                                      | result                                                                                                                   |
| -------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| the mask draw exists in mask-on only                                       | **pass** - eid 42634, absent from capture 1                                                                              |
| it targets the same depth target the world batches use                     | **pass** - resource 426, the `kMAIN` D32S8                                                                               |
| depth after the mask is the near plane inside the visor, untouched outside | **pass** - see below                                                                                                     |
| world draws behind the mask are rejected                                   | **pass** - `depthTestFailed` on every draw that would have shaded them, before their pixel shader runs                   |
| the final colour image is unchanged                                        | **pass** - 0.84% of visor pixels differ by more than 8/255, against 1.28% _outside_ the visor, i.e. no systematic change |

The depth write itself: exporting resource 426 before the mask (after eid 42559), immediately after it
(42634), and at the end of the deferred passes shows 525,160 pixels going to 255 - the reversed-Z near plane

- and 16,464 pixels going to 0/15. That is 32.1% of the 1706x960 viewport, matching the mask texture's 32.0%
  opaque coverage, and the shape is the visor: a solid band across the top, the lower corners, and a gap in the
  middle where the eyes are. 99.9% of those pixels are still at the near plane at the end of the deferred
  passes, so nothing overwrites them.

`PixelHistory` on resource 426 at a masked pixel reads, in event order: scene geometry writes 0.0608 -> the
mask writes 1.0 -> the G-buffer passes 42793/42825 test `CompareFunction.Equal` and are `backfaceCulled` ->
later world geometry is `depthTestFailed`. At (1105,50) four draws between 43023 and 43115 fail the depth test
where in the mask-off capture they shaded. Comparing the _same_ screen pixels between captures makes the
contrast explicit: (369,82) is `depthTestFailed` from 43023 onward in mask-on, and shades normally in
mask-off; the same holds at (1500,150) and (1105,50).

What is skipped is the pixel shader. The mask's pixel shader declares only `SV_Depth` and no `SV_Target`,
while every draw it rejects writes colour: the `Equal` G-buffer passes at 42793/43023/43095/43867 and the
later `GreaterEqual` passes at 43867/44083 declare 3 to 8 `SV_Target` outputs. Those are the draws that no
longer run per pixel inside the visor.

The colour equivalence in the fifth row is a coarse check (the two captures are different frames of a moving
scene, so per-pixel comparison is noisy), and it is meant as a guard against the mask painting anything
rather than as a pixel-exact diff. It shows no systematic darkening under the visor.

**Conclusion: the depth reject works as designed.** The mechanism is verified; only its _value_ is unmeasured,
and the frame-time result above says that value is under 0.1 ms in an ordinary interior. Stencil was not used
instead for a narrower reason than "stencil is rarely used": writing a stencil value does not by itself skip
anything - every world batch would have to be re-bound to a stencil-testing depth-stencil state, and any later
rebind silently wins. Depth is already tested by every batch with no cooperation. The "3 of 4068 draws" figure
is about this one frame, not about stencil being rare in general: the engine's own **hidden-area mask** uses
near-depth writes too (VR relies on them, and the GI shader's `#ifdef VR` guard above exists for them), and
Open Shaders' VRStereoOptimizations stamps stencil to cull eye 1 - but only when stereo reprojection is
actually on (`StereoMode != Off`, plus the G-buffer-fill and shader gates in `CanDispatchStencil()`), and it
**clears `kMAIN` stencil** to 0 before stamping its own classification, so a stencil the mask left behind would
be wiped whenever that feature engages. That is an argument against depending on stencil, not for it.

### The fringe, and the fix for it

A wrong edge shows as a thin un-stamped strip of scene right at the visor silhouette, where the overlay's
bilinear-filtered edge blends the art towards clear but the scene behind it is still being shaded. Two
mechanisms caused it, and both are fixed:

- **The margin was measured in art pixels.** `ArtMetrics::BuildOpaqueMask` erodes by whole 4x4 DDS blocks,
  and `TextureCache::CreateMask` converts `iMaskMarginPx` to blocks. When the art is scaled to the display
  (a 1920-wide canvas on a 1280-wide display is 1:0.67), 8 art px of margin becomes ~5 screen px, and the
  art's own bilinear edge blends over a whole screen pixel. At a small enough scale the margin rounds to
  nothing. `src/MaskInset.h` now computes a UV step that is at least `kScreenInsetPx` (3) screen pixels
  in **every direction**, not only along the axes: the shader takes five point samples (centre, +/-x,
  +/-y), so a step sized for 3 px along an axis delivers only `3/sqrt(2)` = 2.12 px on a 45-degree edge.
  The request is multiplied by `sqrt(2)` for that reason, and the worst case is swept over the edge
  normal in the tests rather than assumed,
  quantised _up_ to whole mask texels because the shader point-samples the mask. The pixel shader samples
  five taps - centre, +/-U, +/-V - and takes the minimum, eroding by that step. Because it is defined in
  screen pixels it cannot shrink with the art.
- **The prediction ran at unbounded speed.** The mask is published for the _next_ frame with the overlay's
  offset extrapolated by one frame of motion. With only an acceleration gate and no magnitude cap, art that
  stops dead - a fade completing, the helmet coming off, sway settling - left the prediction a full frame of
  travel ahead of the art it was hiding, and prediction error is not covered by the art's own bleed margin
  because the mask moves _further_ than the art. `kMaskMaxPrediction` now caps the per-frame extrapolation at
  0.002 of the display (about 2-3 px), which is inside the margin.

The stamp is also dropped entirely when the art covers less than `kMinStampSize` (0.01) of the display on
either axis, so a degenerate scale cannot ask for an unbounded inset.

Verified in a second capture of the fixed build (573 MB, `frame 12818`; mask draw now eid **42374**, its
constant buffer up from 32 to 48 bytes to carry `InsetUV`, still `SV_Depth`-only and still the only
always-true depth draw bound to resource 426):

| measurement                               | before the fix   | after the fix    |
| ----------------------------------------- | ---------------- | ---------------- |
| pixels stamped to the near plane          | 524,642 (14.23%) | 494,842 (13.42%) |
| eye-opening clear width at the centre row | 697 px           | **705 px**       |
| outer visor edge                          | starts at x=7    | starts at x=11   |

The inset removed 29,800 px, 5.7% of the stamp, and 0 pixels changed outside the stamped set. The eye
openings grew 4 px per side and the outer edge moved in 4 px - the 3 px inset plus texel quantisation,
where the old margin left a 1-2 px band. `PixelHistory` on the fixed capture confirms the reject still
happens: (845,720) and (860,720) see the mask at 42374 and are then `depthTestFailed` by 43759, while
(137,720) and (880,720) - the widened clear opening and the pulled-in outer edge - now shade normally.

### Screen-space effects read this depth as data

They do, and this is the part worth being precise about, because the obvious worry - "a ray march will hit the
near plane instead of the wall and light everything wrong" - is not what happens, while a related hazard is
real. The mechanics:

- The stamp is applied to `kMAIN` depth at, in the audited frame, event 42374. **A copy of `kMAIN` is taken
  before it** (event 42295, into the post-z-prepass copy - resource 436) and that pre-stamp copy is what the
  deferred world shader, the screen-space shadows, the skylighting and the eye-reprojection passes read. Their
  input is the true scene depth: exporting resource 436 at that point shows 5,118 near-plane pixels (0.14%,
  just the existing sky/hidden-area case), versus 499,960 (13.56%) after the post-stamp refresh at 43840.
  So the _shading_ of the visible scene is not fed stamped values at all.
- Later passes do sample the stamped `kMAIN` depth (`PS_Resource` of `DepthTex`/`AvgDepthTex` at 44024 and
  45019, the compute AO family at 44980/44994). Those are the ones whose input now contains near-plane
  values inside the visor.

The reason that does not corrupt anything visible is a specific guard in the GI shader, not luck. Intel's
XeGTAO prefilter (`ScreenSpaceGI/prefilterDepths.cs.hlsl`) begins with:

```hlsl
float ClampDepth(float depth)
{
#ifdef VR
    if (depth == FrameBuffer::NearPlaneDepth())  // VR hidden-area mask
        return 0.0;
#endif
    depth = ScreenToViewDepth(depth);
    return clamp(depth, 0.0, 3.402823466e+38);
}
```

That test exists because VR already hides areas by writing near depth, so the GI path was taught to treat
near-plane depth as "no geometry, no visibility". The flat mask therefore lands in a case the shader already
handles correctly. It is guarded by `#ifdef VR` only - on flat the same values are converted to view depth 0
and clamped, which happens to give the same "nothing here" result, but this is a coincidence of the clamp
rather than a designed path. **That is the real fragility of the flat mask**, not the depth test itself: it
depends on every depth consumer continuing to treat near-plane depth as empty.

This is also the class of hazard that made the VR depth stamp unsafe. There the hidden region was replaced by
a synthetic near value and then _showed up in visible reflections_, because that consumer wanted depth for
reprojection rather than for culling and had no such guard. The flat mask differs in the one way that matters:
the stamped pixels are precisely the pixels the overlay paints with fully opaque art at opacity 1, so a
consumer that mishandles them is mishandling a region the player cannot see. It is safe _because_ of the
opacity and single-layer conditions, not because nothing reads the depth. Loosening those conditions - or a
new depth consumer that does not treat near depth as empty - is what would break it.

Still outstanding: several overlays, fade states, head motion and helmet swaps (the two mechanisms above are
the known causes, but the moving case is not captured), and the upscaler / StereoMode / reverse-Z
permutations.

## Risks

- Depth or stencil conventions differ by runtime and by open-shaders feature flags; a wrong near value
  would hide geometry that should draw.
- A mask that drifts from the overlay by a frame or a pixel shows as a dark fringe; the screen-pixel
  inset and the capped prediction are what contain it. (Both fixes are measured at a fixed pose; motion is
  not captured.)
- The saving is unproven. Opaque pixels are often at the periphery where the lens already loses detail
  and, in VR, where the engine already masks. The depth reject is verified; what it saves is measured at
  under 0.1 ms in a 1 ms world pass.
- Flat gains the most per pixel but has the least existing machinery: it needs the new draw and has no
  stencil consumers, so everything rests on the depth reject.
