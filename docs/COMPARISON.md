# Comparison with existing implementations

Facts come from each mod's Nexus page (linked), its shipped scripts and SWF, or Helmet Overlays NG's own
tests. Capability rows answer Yes, No or Not verified (the source does not say and it was not tested), with
a short detail in brackets. Helmet Overlays NG is unreleased.

## Implementations

| Mod                                                                                                           | Mechanism                                                                             |
| ------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| [Helmet Overlays - Improved 1st Person Experience](https://www.nexusmods.com/skyrimspecialedition/mods/35605) | SkyUI widget (`overlaywidget.swf`) driven by Papyrus scripts and `ScribeOverlays.esp` |
| [VR Helmets](https://www.nexusmods.com/skyrimspecialedition/mods/47827)                                       | ReShade shader (`Layer.fx`) drawing a PNG layer over the VR image                     |
| Helmet Overlays NG                                                                                            | Native SKSE plugin (CommonLibSSE-NG); no ESP, Papyrus or SkyUI                        |

## Facts

| Item         | Helmet Overlays (SWF)   | VR Helmets (ReShade)                                                 | Helmet Overlays NG                                        |
| ------------ | ----------------------- | -------------------------------------------------------------------- | --------------------------------------------------------- |
| Platforms    | Flat                    | VR                                                                   | SE, AE and VR (one DLL)                                   |
| Requirements | SKSE, SkyUI             | ReShade VR build (replaces `dxgi.dll`); headset resolution in an ini | SKSE; imgui-vr-helper in VR; SKSE Menu Framework optional |
| Art formats  | swf, dds, png, jpg, gif | png                                                                  | dds                                                       |
| Conflicts    | SkyHUD (per the page)   | Presets with a custom `layer.png`; other ReShade or ENB setups       | None known                                                |
| Version      | 2.5.2, June 2023        | 2.0, March 2024                                                      | 0.1.0, unreleased                                         |

## Capabilities

| Capability                       | Helmet Overlays (SWF)                              | VR Helmets (ReShade)         | Helmet Overlays NG                                               |
| -------------------------------- | -------------------------------------------------- | ---------------------------- | ---------------------------------------------------------------- |
| Overlay follows the worn helmet  | Yes (world model path)                             | No (6 layers chosen by hand) | Yes (world model path, cross-fades)                              |
| Ships its own art                | Yes                                                | Yes (6 layers)               | No (reads the Helmet Overlays path convention)                   |
| Opacity setting                  | Yes                                                | Not verified                 | Yes                                                              |
| Size setting                     | Yes (hotkeys are debug-only)                       | Not verified                 | Yes                                                              |
| Vertical placement setting       | No                                                 | Not verified                 | Yes                                                              |
| Reactive lighting                | Yes                                                | Not verified                 | Yes (on by default, original's values)                           |
| Scene color tint                 | No                                                 | No                           | Yes (flat only, on by default)                                   |
| Jump, strafe, sprint motion      | Yes                                                | Not verified                 | Yes (flat only)                                                  |
| Head-turn sway                   | No                                                 | Not verified                 | Yes (flat only, off by default)                                  |
| Fade, settle or slide on change  | Not verified                                       | Not verified                 | Yes                                                              |
| Sound muffling                   | No                                                 | No                           | Yes (volume and low-pass)                                        |
| Hides in menus                   | Yes (Loading, RaceSex; others not verified)        | Not verified                 | Yes (menus you select)                                           |
| Reacts to camera state           | Yes (first person, third person, horse, furniture) | No                           | Yes (first person only on flat, option)                          |
| Per-eye VR placement             | No                                                 | Not verified                 | Yes (head-locked plane, per-eye projection)                      |
| Fits the headset's field of view | No                                                 | Yes (8:9 and 9:10 templates) | Yes (plane sized from the eyes' field of view)                   |
| GPU saving (opaque mask)         | No                                                 | Not verified                 | Yes, on by default (about 8-11% on SE, 16-18% on VR; world pass) |
| In-game settings                 | Yes (MCM)                                          | Yes (ReShade overlay)        | Yes (ini file, and optional SKSE Menu Framework pages)           |
| Coverage report                  | No                                                 | No                           | Yes (per plugin; can ignore wigs and circlets)                   |
| Debug tools                      | Yes (panel, add-equipment hotkey)                  | No                           | Yes (Debug page, helmet preview)                                 |
| Stops the original mod's widget  | No                                                 | No                           | Yes (optional)                                                   |

## Not implemented in Helmet Overlays NG

- Size and add-equipment hotkeys.
- swf, png, jpg and gif art.
- Per-armor sound profiles and head-hit feedback.

## Known limits

- In VR, the center of the head-locked plane doubles when the eyes converge nearer than the plane depth.
- The opaque mask follows sway and motion with a one-frame prediction and is off during the fade and for a frame
  when the overlay speeds up sharply.
- A hair-slot armor whose name has a hood or hat word is kept as headgear, even if it is a wig.
