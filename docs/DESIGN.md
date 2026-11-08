# Helmet Overlays NG design

Replaces the SWF widget of [Helmet Overlays](https://www.nexusmods.com/skyrimspecialedition/mods/35605)
with a native SKSE plugin for flat SE/AE and VR. Overlay art is not bundled: the plugin reads the
existing `Interface/overlays/**.dds` packs (Helmet Overlays 2.0 and Enhanced Edition) from the VFS.

## How the originals work

- `ScribeOverlays.esp` and its Papyrus (`ScribeHelmet_Monitor`, on `OnObjectEquipped` and
  `OnObjectUnequipped`) watch the head and hair slots, take the armor's world model path
  (`Armor.GetModelPath()`, e.g. `Armor\Iron\Male\HelmetGND.nif`), swap `.nif` for `.dds`, prefix
  `overlays/`, and have `overlaywidget.swf` (a SkyUI widget) load it.
- Art: 1920x1080 DXT5 with alpha, transparent where the player sees through, with bleed past the screen
  edge for the sway animation.
- MCM: alpha, size, reactive light (enable, strength, tween duration, update interval), debug hotkeys; the
  widget timeline animates jump, strafe and sprint.
- Enhanced Edition is an art pack on the same path convention, with no scripts.

Any armor with art resolves without configuration, and a new helmet works as soon as a DDS exists at its
model path.

## Pipeline

```
TESEquipEvent (player)  -> dirty flag
HUDMenu::PostDisplay    -> OverlayController::Update(dt)
                             Refresh: worn head/hair armor (or the debug preview) -> Resolver
                                      (world model, own sex then other) -> TextureCache (DDS -> SRV)
                             gate: enabled, 3D loaded, first person (flat), no hidden menu open
                             per-layer progress -> alpha / scale / offset (+ sway on flat)
                          -> Muffle::Update(worn amount) -> LowPass::SetTarget + category attenuation
                          -> Renderer: flat = ImGui background draw list; VR = helper HUD plane
```

Hook points: the `BSGraphics::InitD3D` call site (`RELOCATION_ID(75595, 77226)`) to capture the device,
and `HUDMenu::PostDisplay` (vfunc 6). HUDMenu draws beneath every other menu, so the overlay cannot cover
a menu; the controller also fades it out for the menus selected in the settings.

On flat, the first `PostDisplay` patches the HUD movie's `GFxMovieView::Display` (vfunc 0x26, filtered to the
HUD's own movie) and the frame then runs from there, before the HUD draws, so the compass, bars and crosshair
render on top of the overlay (`bHudOnTop`, default on; off draws from `PostDisplay` and the art covers the HUD). VR keeps the `PostDisplay` path, where the helper composites the plane.

## VR

The overlay is a HUD-mode client of imgui-vr-helper. Interface 006 (`SetHudPlane`) gives it its own
head-locked plane (default 0.1 m, overfill 1.25 so the edges over-fill the view) instead of the helper's
global HUD plane. The helper composites it into each eye's target with that eye's projection, so the
plane carries normal stereo parallax. Sway is disabled in VR.

The helper sizes its plane from the eye's field of view times the overfill, so the art is divided by the
same overfill (`ArtFit::ArtFitFor`, `src/Renderer.cpp`). Raising the overfill then only pushes the frame
further past the edges; it does not enlarge the painted eye openings. Art stretch resizes the art inside
the plane without touching it.

This plugin writes no depth in VR. Culling the world behind the visor is a separate problem that has no
working answer yet: writing depth from outside the renderer fixes nothing, because screen-space effects
have already sampled that depth by the time the overlay draws. Doing it properly needs a coverage mask the
renderer's own shading passes read. See [OPAQUE_MASK.md](OPAQUE_MASK.md).

## Sound

Two independent effects while a helmet is worn (`[Sound]`), scaled per helmet when `bAutoStrength` is on:

- **Per-helmet strength:** `HelmetProfile` combines how see-through the overlay is (the fraction of
  pixels with alpha below 32, read from the DXT3/DXT5 file by `ArtMetrics` when the overlay loads) with
  the armor class (heavy 1.0, light 0.7, clothing 0.4). `fAutoArtWeight` (default 0.65) sets the blend, and
  the art term runs from 1.0 at 45% see-through or less to 0.0 at 90% or more, the span measured over the
  Enhanced Edition overlays. Measured: Daedric 0.99, Iron 0.84, Steel 0.60, Elven 0.48, Hide 0.43, Mage
  Hood 0.16. The strength multiplies the worn amount for both effects. The constants are starting values
  that have not been tuned by ear.

- **Attenuation** (`bAttenuate`): lowers the volume of the SFX sound category through
  `SetCategoryVolume`, restoring the captured raw `volumeMult` when the helmet comes off (`GetCategoryVolume`
  returns the value scaled by the menu volume, so it cannot be used to restore). The footsteps category is
  not found by name, so it is skipped.
- **Low-pass** (`bLowPass`): `LowPass.cpp` hooks `BSXAudio2GameSound::UpdateEmitterPosition` (vtable slot
  0x0D) and `Update` (0x0E). After the engine runs, each non-music voice gets a low-pass on its send to
  the mastering voice through `SetOutputFilterParameters`, the call the engine uses for its own distance
  filter, so while the helmet is worn the plugin's cutoff replaces the engine's on those voices. The plugin
  never reads the filter back: XAudio2 waits without a timeout for its engine thread when a voice is read
  right after a write, and a stalled device hangs the audio thread. Voices are released to open when the
  helmet comes off. The engine has no global or underwater low-pass; its underwater effect is category
  mutes plus a reverb preset. The effect is audible in game.

## Coverage

A scan of every playable head or hair armor that has a world model resolves each one against the art on
disk, in batches of 48 per frame once the player is loaded, and logs the result, for example
"695 of 851 playable head/hair armors have art (82%)" with the plugins missing the most. The Overlay page
shows the summary; the Debug page adds a per-plugin breakdown, a rescan button, and a filter for helmets
without art.

## In-game settings

SKSE Menu Framework page "Helmet Overlays NG": Overlay, Sound, Menus (which registered menus hide the
overlay) and Debug (the resolved model path and paths tried; preview any head or hair armor's overlay
without equipping it). The page lives in its own PCH-free static library because the SMF header cannot
share a translation unit with `imgui.h`.

## Original mod coexistence

`bSuppressOriginal` stops the quests of `ScribeOverlays.esp` on each save load and new game, so its widget
cannot draw a second overlay. The ESP stays in the load order for its art.

## Open items

1. Opaque-region mask (on by default): skip shading where the overlay is fully opaque, through a depth
   write this plugin issues in its own world-batch hook on flat screens. Verified in a GPU capture to write
   the near plane where the visor is opaque and to make the world draws behind it fail the depth test. SE
   saved about 8-11% of the world
   pass in a scene where that pass costs 7.4 ms; an ordinary interior's pass is about 1 ms, so the same
   percentage is under 0.1 ms of the frame. See [OPAQUE_MASK.md](OPAQUE_MASK.md), including a measured null
   result; there is no VR equivalent.
2. Per-armor sound profiles: derive defaults from armor type, weight, material keywords and slot
   coverage, with an optional override file.
3. Head-hit feedback: `HitData::damageLimb == kHead` on the player, played as a material-appropriate
   sound.
4. Overrides for helmets whose model path has no art.
5. The footsteps sound category needs its form found another way.
