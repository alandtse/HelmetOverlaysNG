# Design: skip shading where the overlay is fully opaque

Status: design only, nothing built. Engine facts come from static analysis of the SE, AE and VR
executables and the open-shaders sources (gbrain `reference-overlay-opaque-mask-injection-points` has the
full tables); anything marked "unverified" is inferred and has not been run.

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
- What the saving applies to: from existing Tracy notes, the per-pixel costs are Deferred Geometry (about
  8 to 22 ms in the captured scenes), NR (about 5 ms), Skylighting and the composite passes; shadow
  rendering is geometry-bound and would not shrink. So the expected saving is roughly the masked share of
  Deferred Geometry plus the passes that early-out. No per-pixel profile exists, so the real figure is
  unknown until measured.

## The mask

- **Definition.** A canvas pixel is masked when its alpha is 255 and every pixel within the safety margin
  is also 255. Alpha 254 is left to draw; excluding it costs under half a point.
- **Safety margin.** Erode by at least 4 px at canvas resolution (8 by default). This covers texture
  filtering, TAA and upscaler taps that read across the mask edge, and the one-frame delay below.
- **Storage.** Build it once when an overlay loads, from the alpha already decoded for the see-through
  measure: an `R8_UNORM` texture at 480x270, where a texel is 255 only if all of the 4x4 source pixels (after
  erosion) are opaque (a min-pool, never an average). About 130 KB per overlay. Nearest sampling.
- **When it is active.** All of these must hold, otherwise no mask is drawn:

  - the overlay layer is fully faded in (progress 1) and its target is shown;
  - opacity is 1.0 (anything lower means no pixel is fully occluding);
  - the overlay is not hidden by a menu, by first-person gating, or by being disabled;
  - the active layer is the only layer (no cross-fade in progress).

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
  transform to the same texture coordinates. Sway moves the art by up to 2.5% of the display per frame in
  the extreme; use the previous frame's transform plus a margin of the largest per-frame step, or switch
  the mask off while the sway velocity is high.

## Where the draw goes (same point for VR and flat)

**SE and AE flat have no hidden-area mask**: there is no `DrawHiddenAreaMesh`, no `bUseHiddenAreaMesh`,
and no immediate write of the engine's stencil-skip mode (`0x1d`) on flat, while VR has five. So flat needs
a new draw, and the same draw works on VR.

Frame order, identical on SE, AE 1.6.1170, AE 1.7.104 and VR (`Main::Draw`: SE id 35560 `0x1405b1860`, AE
id 36559 `0x1406444b0`, VR `0x1405b9330`): RenderDepth, RenderShadowmasks, Occlusion,
PrepareMainDepthPrepass, RenderWorld, RenderFirstPersonView. RenderDepth clears kMAIN depth and stencil,
draws the prepass, then copies depth to the post-prepass copy.

**Injection point.** Inside open-shaders' existing `Main_RenderWorld_Start` thunk, after
`Deferred::StartDeferred()` and before the original call, which begins the first opaque batches. The hook
site is `BSShaderAccumulator::FinishAccumulatingCommonPasses` (SE id 99938 `0x1412cacd0`, AE id 106583
`0x1414b2d90`, VR `0x1413093a0`) at +0x8E (SE, VR) or +0x84 (AE). At that moment the prepass is finished,
the post-prepass depth copy is taken, the shadow masks have consumed the stencil, and on VR the stereo
optimization has already written its stencil.

**State for the mask draw:**

- Bind no color target and the kMAIN depth-stencil view; no color write.
- Depth test ALWAYS with depth write, writing the near value for the active convention: 0.0 under standard
  depth, 1.0 under reverse-Z (open-shaders exposes this as `FrameBuffer::NearPlaneDepth()`).
- Fullscreen triangle, pixel shader samples the mask and discards unmasked pixels, then writes depth.
- Restore the previous state and the G-buffer targets afterwards.

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

| Component        | Role                                                                                                        |
| ---------------- | ----------------------------------------------------------------------------------------------------------- |
| HelmetOverlaysNG | Builds the mask texture, decides when it is allowed, publishes mask and transform                           |
| open-shaders     | Issues the mask draw in `Main_RenderWorld_Start`; owns depth convention, state restore and the stereo logic |
| imgui-vr-helper  | Supplies the plane parameters (depth, size) it already derives per eye                                      |

The draw belongs in open-shaders because it already owns that hook and the depth convention. HelmetOverlaysNG
would hand it a small struct (mask SRV, transform or plane constants, active flag) through an exported
function or a messaging interface, the way it talks to the helper today. A build without open-shaders
simply has no mask.

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
- **Runtimes.** AE builders for the depth-stencil state table were not located, so the stencil inventory is
  complete for SE and VR only. Verify on AE before enabling there.

## Settings

`bOpaqueMask` (default off while experimental), `iMaskMarginPx` (default 8), shown on the Overlay page with
the measured saving once measured. No effect without the open-shaders hook.

## Validation

1. Correctness: capture with the mask on and off at fixed head pose (devbench capture, both eyes), over
   several overlays and fade states, on flat and on VR. The visible image must be identical except for
   pixels under fully opaque overlay pixels; compute the difference only inside the visible region.
2. Cost: GPU time per frame with the mask off and on, same scene and pose, using the profiler timers
   open-shaders exposes and per-pass pixel or draw counts (`inspect draws`). Report the change as a share
   of frame time.
3. Edge behavior: sweep head motion and equip, unequip and swap helmets while capturing, looking for dark
   fringes at the mask edge and one-frame flashes at fade start.
4. Interaction: repeat with the upscaler on and off, StereoMode on and off, reverse-Z on and off.

## Risks

- Depth or stencil conventions differ by runtime and by open-shaders feature flags; a wrong near value
  would hide geometry that should draw.
- A mask that drifts from the overlay by a frame or a pixel shows as a dark fringe; the margin and the
  active conditions are what contain it.
- The saving is unproven. Opaque pixels are often at the periphery where the lens already loses detail
  and, in VR, where the engine already masks.
- Flat gains the most per pixel but has the least existing machinery; the SE and AE builders for the
  state table are not yet located.
