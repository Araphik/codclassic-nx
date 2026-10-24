<h1 align=center>Call of Duty Classic — Nintendo Switch port (codclassic-nx)</h1>

An unofficial, work-in-progress native Nintendo Switch port of the single-player campaign of the Xbox 360
(XBLA) release of **Call of Duty Classic** (title ID `58410957`).

It is not an emulator. The PowerPC code of the game's `default.xex` and its modules (`x360shared.xdl`,
`game_sp.xdl`, `cgame_sp.xdl`, `ui_sp.xdl`) is statically recompiled to C++ with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for the Switch, and the game is drawn through
Vulkan on NVK (Mesa). The Switch runtime is the one from [nfsmw-nx](https://github.com/StevensND/nfsmw-nx) by
StevensND, which this repository is a fork of.

> [!IMPORTANT]
> This repository contains **no files, code or assets from the game**, and no builds made from them. You need
> your own copy of the XBLA game, and you build the port yourself from it. Do not open issues or pull requests
> containing game files, generated code, NROs or logs with game data.

## Status

- The campaign starts and plays; missions up to Dawnville have been played. Saving and loading after a death
  were fixed recently and still need more testing.
- Performance is the main open problem (about 20–35 fps in play, lower in heavy scenes). The game thread waits
  on the GPU a large part of each frame.
- Single-player only. The multiplayer executable and modules are not part of the port.
- See [codclassic/NOTES.md](codclassic/NOTES.md) for the fixes made so far and what is known.

## Repository layout

| Folder | Contents |
| --- | --- |
| `codclassic/app/` | The port: CMake project, ReXGlue manifest, function hints and gap lists, hooks and guards (`src/`) |
| `codclassic/scripts/` | Codegen helpers: `post_codegen.py` (run after every codegen), gap tools, `stfs_extract.py` |
| `codclassic/localization/` | Tools to build a Cyrillic font and a translated string pack from **your own** copy of the game; no translation text is included |
| `sdk/` | ReXGlue SDK with the Horizon layer (from nfsmw-nx) and the changes this port needed |
| `mesa/`, `tools/switch/` | The mesa-switch patch and the devkitA64 toolchain file (from nfsmw-nx) |
| everything else | The Need for Speed: Most Wanted port this fork is based on, kept as is ([README.nfsmw-nx.md](README.nfsmw-nx.md)) |

## Building

You need devkitA64 and libnx ([devkitPro](https://devkitpro.org)), CMake 3.25+, Ninja, Python 3, a host clang to
build the `rexglue` code generator, and the NVK Vulkan driver built from
[mesa-switch](https://github.com/danfromtico/mesa-switch) with `mesa/mesa-switch-nfsmw.patch`
(see [docs/building.md](docs/building.md) and [docs/mesa.md](docs/mesa.md) from nfsmw-nx).

1. Fetch the SDK's third-party sources: `python tools/fetch_thirdparty.py`.
2. Build the host `rexglue` tool from `sdk/` (see `docs/building.md`).
3. Put the game files from **your** XBLA package into `codclassic/game/` (`default.xex`, the four `.xdl`,
   `main/`, `shaders/`, `splash/`). `python codclassic/scripts/stfs_extract.py <package>` lists and extracts the
   package contents.
4. Generate the code and post-process it:
   ```sh
   cd codclassic/app
   rexglue codegen --ignore-stamp codclassic_manifest.toml
   python ../scripts/post_codegen.py
   ```
5. Configure and build for the Switch:
   ```sh
   cmake -S . -B out/sw -G Ninja \
     -DCMAKE_TOOLCHAIN_FILE=../../tools/switch/cmake/switch-devkitA64.cmake \
     -DREXSDK_DIR=../../sdk \
     -DREXGLUE_SWITCH_NVK_SDK=<mesa-sdk>/opt/devkitpro/portlibs/switch
   cmake --build out/sw
   ```
   An optional `codclassic/app/icon.jpg` (256×256) becomes the NRO icon. None is included.

A step-by-step build log in Russian is in [codclassic/BUILDING.ru.md](codclassic/BUILDING.ru.md).

## Installing

```text
sdmc:/switch/codclassic/
  codclassic.nro
  codclassic.toml        (optional settings)
  game_root/             (your game files: default.xex, *.xdl, main/, shaders/, splash/)
```

Launch with a title override (hold **R** while starting an installed game) or a forwarder. Album applet mode
does not give the game enough memory.

## Credits

- **Activision and Infinity Ward** — creators of Call of Duty. This is an unofficial fan project with no
  affiliation.
- **[StevensND](https://github.com/StevensND/nfsmw-nx)** — nfsmw-nx: the Switch runtime, the Horizon layer of
  the SDK and the toolchain this port is built on.
- **[Tom Clay](https://github.com/rexglue/rexglue-sdk)** — the ReXGlue SDK, built on the work of the
  **[Xenia](https://xenia.jp)** team.
- **[danfromtico](https://github.com/danfromtico/mesa-switch)** and **[NaGaa95](https://github.com/NaGaa95/mesa-switch)** — mesa-switch.
- **[devkitPro](https://devkitpro.org)** and **[switchbrew](https://github.com/switchbrew/libnx)** — devkitA64 and libnx.
- Everyone listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Legal

No affiliation with Activision, Infinity Ward or Microsoft. "Call of Duty" is a trademark of Activision
Publishing, Inc.; it is used here only to say which game the port is for. This repository contains no assets or
program code from the game: users must provide their own legally obtained copy and build the port from it on
their own machine. Builds made from the game are derived from it and are not distributed here.

Running homebrew requires custom firmware, which violates Nintendo's terms of service and can get a console
banned.

Source code is under the GPL-3.0 license ([LICENSE](LICENSE)), inherited from nfsmw-nx. The SDK changes are
under the SDK's BSD-3-Clause license and the Mesa changes under MIT (see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

---

### Кратко по-русски

Неофициальный порт одиночной кампании Call of Duty Classic (Xbox 360, XBLA) на Nintendo Switch через
статическую рекомпиляцию (ReXGlue) на основе nfsmw-nx. В репозитории нет ни файлов игры, ни сгенерированного из
неё кода, ни готовых NRO: собирать нужно самому из своей копии игры (инструкция выше и в
[codclassic/BUILDING.ru.md](codclassic/BUILDING.ru.md)).
