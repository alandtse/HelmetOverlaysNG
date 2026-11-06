# Helmet Overlays NG

Native SKSE (CommonLibSSE-NG) replacement for the SWF-based
[Helmet Overlays](https://www.nexusmods.com/skyrimspecialedition/mods/35605) widget, for Skyrim SE/AE
and Skyrim VR.

Source and issues: <https://github.com/alandtse/HelmetOverlaysNG>. Licensed GPL-3.0-or-later with a modding
exception (see [License](#license)).

It reads the existing overlay art (`Data/Interface/overlays/**.dds`, from [Helmet Overlays](https://www.nexusmods.com/skyrimspecialedition/mods/35605) and
[Helmet Overlays Enhanced Edition](https://www.nexusmods.com/skyrimspecialedition/mods/189092)) and picks the overlay from the worn helmet's world model path.
No ESP, no Papyrus, no SkyUI, no Scaleform. This mod ships no art; install the art from at least one of these:

- [Helmet Overlays](https://www.nexusmods.com/skyrimspecialedition/mods/35605): the SE version of the original mod. Its art is used; the SWF plugin is not.
- [Helmet Overlays Enhanced Edition](https://www.nexusmods.com/skyrimspecialedition/mods/189092): more helmets.
- [Helmet Overlay Addon](https://www.nexusmods.com/skyrimspecialedition/mods/93800): art for more armor mods.

If no art is found, the log and the settings page say so.

See [docs/DESIGN.md](docs/DESIGN.md) for how it works and the open items, and
[docs/COMPARISON.md](docs/COMPARISON.md) for how it differs from the other overlay mods.

## Features

- Switches the overlay automatically with the worn helmet (head and hair slots).
- Flat and VR (a head-locked plane through [imgui-vr-helper](https://github.com/alandtse/imgui-vr-helper)).
- Opacity, scale, vertical position and edge extension, and animated on/off (fade, settle, slide).
- Reactive lighting: dims the art in dark places, on by default.
- Screen color tint: the art takes on the color of the scene around the screen edges, such as orange firelight. On by default; the Debug page compares it with the light-level look.
- Flat only: jump, strafe and sprint nudges, and optional head-turn sway.
- Drawn beneath all menus and faded out for the menus you choose.
- A coverage report: how many helmets in your load order have overlay art, per plugin. Wigs and circlets
  can be ignored.
- An opaque-pixel mask (flat only): skips shading the scene behind the overlay's fully opaque pixels. Verified as working in a GPU capture; worth about 8-11% of the world pass on SE where that pass costs several ms, and in a typical interior the pass is about 1 ms, so it is under 0.1 ms of the frame. VR has no equivalent yet — see [OPAQUE_MASK.md](docs/OPAQUE_MASK.md).
- Sound muffling scaled by helmet type, from the armor class and how closed the overlay art is.
- World-sound attenuation and a low-pass filter, on by default, as separate settings.
- In-game settings and a debug page through [SKSE Menu Framework](https://github.com/alandtse/SKSE-Menu-Framework-3) (optional).
- Can stop the original `ScribeOverlays.esp` widget from drawing a second overlay.

## Build

```
git submodule update --init --recursive
xmake f -p windows -a x64 -m releasedbg
xmake build
```

Set `SkyrimVRPath`, `SkyrimVRPluginTargets`, or `SkyrimPluginTargets` to deploy after a build. Close the
game before building if it loaded a build older than the PDB path fix, or the linker cannot write the PDB.

The overlay uses imgui-vr-helper interface 006 (per-client HUD plane). At runtime VR needs a helper
that supports it; an older helper falls back to its global HUD plane. To build against an unreleased
local helper checkout, set `HONG_VRHELPER_API` to its `api` folder.

## Config

Edit `Data/SKSE/Plugins/HelmetOverlaysNG.ini`, or change the same settings in game under "Helmet Overlays NG" with the
optional [SKSE Menu Framework](https://github.com/alandtse/SKSE-Menu-Framework-3).

## Credits

- [scrivener07](https://www.nexusmods.com/profile/scrivener07?gameId=110) and megapolom for the original
  concept, [Helmet Overlays](https://www.nexusmods.com/skyrim/mods/69611) on LE. It defined the art format and the
  model-path convention this mod reads.
- [GSA2011](https://www.nexusmods.com/profile/GSA2011?gameId=1704) for the first SE port,
  [Helmet Overlays - Improved 1st Person Experience](https://www.nexusmods.com/skyrimspecialedition/mods/35605).
- HoodiePledge and rotol for keeping it alive with new overlays
  ([Enhanced Edition](https://www.nexusmods.com/skyrimspecialedition/mods/189092) and
  [Helmet Overlay Addon](https://www.nexusmods.com/skyrimspecialedition/mods/93800)). This mod packages no art; install
  their mods.

## License

GPL-3.0-or-later with the Modding Exception; see `COPYING` and `EXCEPTIONS.md`.
Source: <https://github.com/alandtse/HelmetOverlaysNG>.
