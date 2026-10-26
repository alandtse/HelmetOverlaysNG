# Helmet Overlays NG

Native SKSE (CommonLibSSE-NG) replacement for the SWF-based
[Helmet Overlays](https://www.nexusmods.com/skyrimspecialedition/mods/35605) widget, for Skyrim SE/AE
and Skyrim VR.

It reads the existing overlay art (`Data/Interface/overlays/**.dds`, from Helmet Overlays 2.0 and
Helmet Overlays Enhanced Edition) and picks the overlay from the worn helmet's world model path.
No ESP, no Papyrus, no SkyUI, no Scaleform. This mod ships no art; install the art packs.

See [docs/DESIGN.md](docs/DESIGN.md) for how it works and the open items, and
[docs/COMPARISON.md](docs/COMPARISON.md) for how it differs from the other overlay mods.

## Features

- Switches the overlay automatically with the worn helmet (head and hair slots).
- Flat and VR (a head-locked plane through [imgui-vr-helper](https://github.com/alandtse/imgui-vr-helper)).
- Opacity, scale, vertical position and edge extension, and animated on/off (fade, settle, slide).
- Reactive lighting: dims the art in dark places, on by default with the original mod's values.
- Flat only: jump, strafe and sprint nudges like the original, and optional head-turn sway.
- Drawn beneath all menus and faded out for the menus you choose.
- A coverage report: how many helmets in your load order have overlay art, per plugin. Wigs and circlets
  can be ignored.
- An experimental mask that skips shading behind the overlay's fully opaque pixels to save GPU time.
- Sound muffling scaled by helmet type, from the armor class and how closed the overlay art is.
- Optional world-sound attenuation and an experimental low-pass filter, as separate settings.
- In-game settings and a debug page through SKSE Menu Framework (optional).
- Can stop the original `ScribeOverlays.esp` widget from drawing a second overlay.

## Build

```
git submodule update --init --recursive
xmake f -p windows -a x64 -m releasedbg
xmake build
```

Set `SkyrimVRPath`, `SkyrimVRPluginTargets`, or `SkyrimPluginTargets` to deploy after a build. Close the
game before building if it loaded a build older than the PDB path fix, or the linker cannot write the PDB.

The overlay uses imgui-vr-helper interface 006 (per-client HUD plane), pinned to the v1.10.0 release. At
runtime, VR needs that helper version; an older helper falls back to its global HUD plane. To build against
an unreleased local helper checkout, set `HONG_VRHELPER_API` to its `api` folder.

## Config

Edit `Data/SKSE/Plugins/HelmetOverlaysNG.ini`, or change the same settings in game under "Helmet Overlays NG" with the
optional SKSE Menu Framework.

## License

GPL-3.0-or-later with the Modding Exception; see `COPYING` and `EXCEPTIONS.md`.
