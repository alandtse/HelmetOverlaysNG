# How Helmet Overlays NG compares

Helmet Overlays NG is a native SKSE plugin. The other approaches draw the overlay with a Scaleform widget
or with a post-process shader. Facts below come from each mod's Nexus page (linked) unless marked
"inferred" or "not verified"; Helmet Overlays NG is unreleased and its VR behavior has been checked only
with SteamVR's null driver, not on a headset.

## The approaches

| Approach                                                                                                      | Mechanism                                                                                              |
| ------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| [Helmet Overlays - Improved 1st Person Experience](https://www.nexusmods.com/skyrimspecialedition/mods/35605) | SkyUI widget (SWF) driven by Papyrus and an ESP; the original mod's overlay engine and art loader      |
| [Helmet Overlays Enhanced Edition](https://www.nexusmods.com/skyrimspecialedition/mods/189092)                | Art pack for the mod above (needs it installed); adds many more helmets, no engine of its own          |
| [Helmet Overlay Addon](https://www.nexusmods.com/skyrimspecialedition/mods/93800)                              | Art pack for the mod above (needs it installed); loose DDS for 76+ armor mods, no plugin or engine     |
| [VR Helmets](https://www.nexusmods.com/skyrimspecialedition/mods/47827)                                       | ReShade shader (`Layer.fx`) that draws a PNG layer over the VR image, using the VR build of ReShade    |
| Helmet Overlays NG                                                                                            | Native SKSE plugin (CommonLibSSE-NG); reads the same art; flat and VR draw paths; no ESP/Papyrus/SkyUI |

## Feature comparison

| Capability                     | Helmet Overlays (SWF)                                                                | Enhanced Edition (art)                            | VR Helmets (ReShade)                                                                    | Helmet Overlays NG                                                                             |
| ------------------------------ | ------------------------------------------------------------------------------------ | ------------------------------------------------- | --------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| Platforms                      | Flat. Page: untested above 1.5.7; no VR support stated                               | Flat (same engine)                                | VR only                                                                                 | SE, AE and VR in one DLL                                                                       |
| Picks overlay from worn helmet | Yes                                                                                  | Yes                                               | No: choose one of 6 layers by hand in the ReShade overlay                               | Yes, from the helmet's world model path, with a cross-fade on swap                             |
| Overlay art                    | About 230 overlays                                                                   | About 330 overlays, covers vanilla and CC helmets | 6 generic layers                                                                        | Reuses both packs; ships no art                                                                |
| Runtime requirements           | SKSE, SkyUI                                                                          | The mod to its left                               | ReShade VR build (replaces `dxgi.dll`), supersampled resolution typed into the ini      | SKSE; in VR, imgui-vr-helper. SKSE Menu Framework optional. No SkyUI, ESP or Papyrus           |
| Setup effort                   | Install, enable ESP                                                                  | Install below the original                        | Install ReShade, overwrite files, enter your resolution, edit an ini                    | Install the plugin and an art pack                                                             |
| Menus                          | Overlay removed while menus are open (per the Enhanced page)                         | Same                                              | Not verified                                                                            | Draws beneath every menu and fades out for menus you select in game                            |
| HUD elements                   | Draw behind the overlay; the Enhanced page recommends HUD mods and a patch to fix it | Optional TrueHUD patch                            | Not verified                                                                            | Not yet tested; flat draws after the HUD                                                       |
| VR depth                       | Not applicable                                                                       | Not applicable                                    | A layer on the rendered image (inferred: no per-eye parallax)                           | Head-locked plane at a set depth, composited per eye with that eye's projection                |
| VR field of view               | Not applicable                                                                       | Not applicable                                    | Templates for 8:9 and 9:10 headsets; you enter the resolution so it fills the view      | Plane sized from the eyes' actual FOV; over-fills so no edge gap shows                         |
| Conflicts                      | SkyHUD (per the page)                                                                | Load order below the original                     | Presets that already use a custom `layer.png`; other ReShade or ENB setups need merging | None known with ReShade or ENB; helper needed in VR                                            |
| Opacity and size               | MCM transparency and size                                                            | Same                                              | Not verified                                                                            | Opacity and scale sliders                                                                      |
| Animation                      | Motion animations and reactive lighting                                              | Same                                              | None stated                                                                             | Fade, settle or slide on and off; head-turn sway on flat only. No reactive lighting yet        |
| Sound                          | None                                                                                 | None                                              | None                                                                                    | Volume attenuation and a separate real low-pass filter                                         |
| In-game settings               | MCM                                                                                  | MCM                                               | ReShade overlay (Home key), needs you to leave the headset view                         | SKSE Menu Framework pages: Overlay, Sound, Menus, Debug                                        |
| Coverage report                | None                                                                                 | None                                              | None                                                                                    | Counts playable helmets with and without art, per plugin; wigs and circlets can be ignored     |
| Debugging                      | Debug menu that can stick on screen (a separate fix mod exists)                      | Same                                              | None                                                                                    | Debug page shows the model path and paths tried, and previews any helmet without equipping it  |
| Original mod coexistence       | Not applicable                                                                       | Not applicable                                    | Not applicable                                                                          | Can stop the original ESP's quests so you never get two overlays (quest-level test in VR only) |
| GPU cost                       | A Scaleform widget over the full screen                                              | Same                                              | Not verified                                                                           | Optional mask skips shading behind fully opaque pixels: about 8-11% less on the world pass on SE, no measurable gain in VR yet |
| Maintenance                    | v2.5.2, Jun 2023; author asked for a successor                                       | v2.8, Oct 2026, active                            | v2.0, Mar 2024                                                                          | v0.1.0, unreleased                                                                             |

## What the native approach buys

- **One engine for flat and VR.** The same worn-helmet logic and art serve both, instead of a Scaleform
  widget for flat and a separate ReShade pack for VR.
- **Automatic in VR.** A ReShade layer cannot see what you wear; VR Helmets needs you to pick a layer.
- **Fewer moving parts.** No SkyUI, MCM, Papyrus or ESP, and nothing to install in the graphics stack.
- **VR placement that follows the headset.** The overlay is a plane with real stereo projection, sized from
  the headset's field of view instead of a resolution you type in.
- **UI-aware.** Menu visibility is configurable, and the overlay sits beneath menus by construction. The
  settings pages show only the options that apply to the running game, flat or VR.
- **Things a widget or shader cannot do.** Sound filtering tied to the worn helmet, and a debug view of
  exactly which art file a helmet resolved to.

## What it does not do (yet)

- Reactive lighting and the original's size hotkeys.
- Per-armor sound profiles and head-hit feedback (designed, not built).
- Verification on a real headset, including parallax, comfort and the ideal plane depth. Close objects make the
  head-locked plane's center double, as any fixed-depth visor does when your eyes converge nearer.
- A measured VR saving from the opaque mask (the SE saving is measured; the VR run was inconclusive).
- It ships no art, so it depends on the Helmet Overlays art packs.
