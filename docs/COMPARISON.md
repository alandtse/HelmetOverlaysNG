# Comparison with existing implementations

Facts come from each mod's Nexus page (linked), its shipped scripts and SWF, or Helmet Overlays NG's own
tests. "Not verified" means the source does not say and it has not been tested. Helmet Overlays NG is
unreleased; its VR behavior has been checked only with SteamVR's null driver, not on a headset.

## Implementations

| Mod                                                                                                      | Mechanism                                                                          |
| -------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| [Helmet Overlays - Improved 1st Person Experience](https://www.nexusmods.com/skyrimspecialedition/mods/35605) | SkyUI widget (`overlaywidget.swf`) driven by Papyrus scripts and `ScribeOverlays.esp` |
| [VR Helmets](https://www.nexusmods.com/skyrimspecialedition/mods/47827)                                  | ReShade shader (`Layer.fx`) drawing a PNG layer over the VR image                  |
| Helmet Overlays NG                                                                                       | Native SKSE plugin (CommonLibSSE-NG); no ESP, Papyrus or SkyUI                     |

## Features

| Capability                    | Helmet Overlays (SWF)                                                                                | VR Helmets (ReShade)                                                   | Helmet Overlays NG                                                                                          |
| ----------------------------- | ---------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| Platforms                     | Flat                                                                                                 | VR only                                                                | SE, AE and VR (one DLL)                                                                                     |
| Requirements                  | SKSE, SkyUI                                                                                          | ReShade VR build (replaces `dxgi.dll`); headset resolution typed in ini | SKSE; imgui-vr-helper in VR; SKSE Menu Framework optional                                                   |
| Overlay selection             | Worn head or hair armor's world model path                                                           | One of 6 layers chosen by hand in the ReShade overlay                  | Worn head or hair armor's world model path; cross-fade on change                                            |
| Art formats                   | swf, dds, png, jpg, gif                                                                              | png                                                                    | dds                                                                                                         |
| Art included                  | Yes                                                                                                  | 6 layers                                                               | No; reads the Helmet Overlays path convention (`Interface\overlays\...`)                                    |
| Opacity                       | MCM percent                                                                                          | Not verified                                                           | Slider                                                                                                      |
| Size                          | MCM modifier; debug-only hotkeys                                                                     | Not verified                                                           | Scale slider                                                                                                |
| Jump, strafe, sprint motion   | Yes (Idle, Jump, MoveLeft, MoveRight, Sprint clips)                                                  | None stated                                                            | Yes, flat only; strength slider                                                                             |
| Head-turn sway                | No                                                                                                   | None stated                                                            | Flat only; strength slider                                                                                  |
| Fade, settle, slide on change | No                                                                                                   | None stated                                                            | Yes                                                                                                         |
| Reactive lighting             | Yes: enable, strength, tween duration, update interval                                               | None stated                                                            | Yes, on by default with the original's defaults: strength, fade time, check interval                                                    |
| Sound                         | None                                                                                                 | None                                                                   | Volume attenuation and low-pass filter, each optional; strength scaled by helmet type                        |
| Menus                         | Hidden during Loading Menu and RaceSex Menu; other menus not verified                                | Not verified                                                           | Hidden for the menus selected in settings; drawn beneath every menu                                         |
| Camera                        | Reacts to first person, third person, horse and furniture camera states                              | VR only                                                                | First person only on flat (option); always on in VR                                                         |
| VR placement                  | Not applicable                                                                                       | Layer on the rendered image (inferred: no per-eye parallax)            | Head-locked plane at a set depth, composited per eye; sized from the eyes' field of view; coverage setting  |
| VR field of view              | Not applicable                                                                                       | Templates for 8:9 and 9:10 headsets                                    | Plane over-fills the view (coverage above 1)                                                                |
| GPU cost                      | Scaleform widget over the full screen                                                                | Not verified                                                           | Optional mask skips shading behind opaque pixels; about 8-11% less on the world pass on SE and about 16-18% less on VR (null driver, one run each) |
| In-game settings              | MCM                                                                                                  | ReShade overlay (Home key)                                             | SKSE Menu Framework pages: Overlay, Sound, Menus, Debug; options shown only for the running game's runtime  |
| Coverage report               | None                                                                                                 | None                                                                   | Counts playable helmets with and without art, per plugin; wigs and circlets can be ignored                  |
| Debugging                     | Debug panel, add-equipment hotkey                                                                    | None                                                                   | Debug page: model path, paths tried, light level, helmet preview                                            |
| Original mod coexistence      | Not applicable                                                                                       | Not applicable                                                         | Optionally stops `ScribeOverlays.esp` quests (tested in SE and VR)                                          |
| Known conflicts               | SkyHUD (per the page)                                                                                | Presets that use a custom `layer.png`; other ReShade or ENB setups     | None known                                                                                                  |
| Version                       | 2.5.2, June 2023                                                                                     | 2.0, March 2024                                                        | 0.1.0, unreleased                                                                                           |

## Not implemented in Helmet Overlays NG

- Size hotkeys and the add-equipment hotkey.
- swf, png, jpg and gif art.
- Per-armor sound profiles and head-hit feedback (designed, not built).
- Verification on a headset: parallax, comfort and plane depth.
- A mask saving measured on a headset (the SE and VR numbers come from single runs, VR on the null driver).

## Known limits

- In VR, the center of the head-locked plane doubles when the eyes converge on an object nearer than the
  plane depth.
- The opaque mask is off while the overlay moves (sway, motion, fade).
- Wig detection uses slots and name words; a hair-slot armor named like a wig but with a hood or hat word is
  kept as headgear.
- Builds need an unreleased imgui-vr-helper interface (006) until it is released.
