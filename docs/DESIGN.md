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
- MCM: opacity, size, size hotkeys, a reactive light effect.
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

## VR

The overlay is a HUD-mode client of imgui-vr-helper. Interface 006 (`SetHudPlane`) gives it its own
head-locked plane (default 0.6 m, coverage 1.15 so the edges over-fill the view) instead of the helper's
global HUD plane. The helper composites it into each eye's target with that eye's projection, so the
plane carries normal stereo parallax. Sway is disabled in VR.

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
  `SetCategoryVolume`, restoring the captured value when the helmet comes off. The footsteps category is
  not found by name, so it is skipped.
- **Low-pass** (`bLowPass`): `LowPass.cpp` hooks `BSXAudio2GameSound::UpdateEmitterPosition` (vtable slot
  0x0D) and `Update` (0x0E). After the engine runs, each non-music voice gets a low-pass on its send to
  the mastering voice through `SetOutputFilterParameters`, the call the engine uses for its own distance
  filter. The result is never more open than the engine's value, and voices are released to open when the
  helmet comes off. The engine has no global or underwater low-pass; its underwater effect is category
  mutes plus a reverb preset.

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

`bSuppressOriginal` stops the quests of `ScribeOverlays.esp` on each save load and new game. In VR, with
the ESP enabled, a save load logged "Suppressed 2 quest(s)" and `ScribeHelmet_Quest` reported not running;
with the option off the same quest reported running. The original's widget never showed in VR captures
either way, so the visual effect is untested; flat has not been run against an active ESP. The hook does
not fire for a console `coc` start.

## Open items

0. Opaque-region mask (experimental, off by default): skip shading where the overlay is fully opaque, flat
   and VR, through a depth write this plugin issues in its own world-batch hook. SE shows a saving; VR is
   unmeasured. See [OPAQUE_MASK.md](OPAQUE_MASK.md).
1. Verify VR parallax and the visor plane on a real headset; the null driver reports zero eye separation.
2. Per-armor sound profiles: derive defaults from armor type, weight, material keywords and slot
   coverage, with an optional override file.
3. Head-hit feedback: `HitData::damageLimb == kHead` on the player, played as a material-appropriate
   sound.
4. Overrides for helmets whose model path has no art.
5. The footsteps sound category needs its form found another way.
6. The helper interface 006 must be released and the `imgui-vr-helper-api` package pin raised; until
   then builds need `HONG_VRHELPER_API` pointing at a helper checkout's `api/` folder.
