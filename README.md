<p align="center"><b>English</b> · <a href="README.ko.md">한국어</a></p>

<p align="center"><img src="pc/res/icon.png" alt="afe-port icon" width="128"></p>

<h1 align="center">afe-port</h1>

<p align="center">
  A native Windows, Linux and Android port of <i>Dōbutsu no Mori e+</i> (どうぶつの森e+, GameCube),<br>
  built on the <a href="https://github.com/cailet0422/afe-decomp">afe-decomp</a> decompilation, with support for the Korean fan translation.
</p>

<p align="center">
  <a href="https://github.com/cailet0422/afe-port/releases"><img alt="Latest release" src="https://img.shields.io/github/v/release/cailet0422/afe-port?include_prereleases&sort=semver&label=latest%20release"></a>
  <a href="https://github.com/cailet0422/afe-port/actions/workflows/tests.yml"><img alt="Tests" src="https://github.com/cailet0422/afe-port/actions/workflows/tests.yml/badge.svg"></a>
</p>

## Download

<!-- When a new release is published, update the version in these links (here and in README.ko.md). -->
Latest release: **[v0.1.6](https://github.com/cailet0422/afe-port/releases/tag/v0.1.6)** (preview)

| Platform | Download | Requirements |
|---|---|---|
| Windows 64-bit | [DobutsuNoMoriEPlus-v0.1.6-Windows-x64.zip](https://github.com/cailet0422/afe-port/releases/download/v0.1.6/DobutsuNoMoriEPlus-v0.1.6-Windows-x64.zip) | OpenGL 3.3 (`SDL2.dll` included) |
| Windows 32-bit | [DobutsuNoMoriEPlus-v0.1.6-Windows-x86.zip](https://github.com/cailet0422/afe-port/releases/download/v0.1.6/DobutsuNoMoriEPlus-v0.1.6-Windows-x86.zip) | OpenGL 3.3 (`SDL2.dll` included); also runs on 64-bit Windows |
| Linux x86_64 | [DobutsuNoMoriEPlus-v0.1.6-Linux-x86_64.zip](https://github.com/cailet0422/afe-port/releases/download/v0.1.6/DobutsuNoMoriEPlus-v0.1.6-Linux-x86_64.zip) | SDL2 (`libsdl2-2.0-0`), OpenGL 3.3 |
| Android | [DobutsuNoMoriEPlus-v0.1.6-Android.apk](https://github.com/cailet0422/afe-port/releases/download/v0.1.6/DobutsuNoMoriEPlus-v0.1.6-Android.apk) | Android 5.0+, ARM 32/64-bit |

Checksums: [SHA256SUMS.txt](https://github.com/cailet0422/afe-port/releases/download/v0.1.6/SHA256SUMS.txt) ·
Older versions and release notes: [all releases](https://github.com/cailet0422/afe-port/releases)

> [!IMPORTANT]
> **No game data is included.** You need your own disc image of the Japanese
> *Dōbutsu no Mori e+* **Rev 1 (GAEJ01)** as `.iso`, `.ciso` or `.gcm`. Disc images are not
> distributed here. The release builds contain no assets from the disc; they read everything from
> your image at start-up.

> [!NOTE]
> Releases are **previews**. They are tested widely on Linux with scripted play-throughs, the
> Windows builds under Wine and the Android build on an emulator, but not yet on many real Windows
> PCs or Android phones. Please report problems in [Issues](https://github.com/cailet0422/afe-port/issues).

## Features

- Runs natively on Windows (32/64-bit), Linux and Android (ARM 32/64-bit) through SDL2 and
  OpenGL 3.3 / OpenGL ES 3.0. No emulator involved.
- Reads the game straight from your disc image; the original image is never modified.
- In-game settings and pause menu: video options, key and gamepad rebinding, disc folder selection.
- Korean fan patch support: install the patch zip from inside the game, switch between Korean and
  Japanese at any time, and type Hangul with a real keyboard (2-set layout) or an on-screen keyboard.
- Android: on-screen touch gamepad, physical gamepads, and a crash report shown on the next launch.
- NES (Famicom) furniture games, saved high scores and the boot jingle all work.
- GameCube memory card saves (GCI) can be imported and exported with the `afe_gci` tool (build from
  source).

## Getting started

### Windows / Linux

1. Unzip the download. Inside the extracted `DobutsuNoMoriEPlus` folder, create a `rom` folder and put
   your disc image in it.
   On Windows you can also skip this: when no image is found, the game asks for the folder that holds
   it (**Choose folder**).
2. Run `DobutsuNoMoriEPlus.exe` (Linux: `./DobutsuNoMoriEPlus`). Saves go to `save/` in the same folder.

### Android

1. Copy your disc image into a folder on the phone, e.g. `Download/AnimalForest`. (Android does not let
   apps pick the root of `Download`, so use a subfolder.)
2. Install the APK (allow installing from unknown sources), open it, and pick that folder with
   **Choose folder**.
3. Play with the on-screen touch gamepad or a physical controller.

The APK is signed with a release key (certificate SHA-256
`db6fc1ad60b6b6d980bee0990f120ef481f195f117729fa194b124d14752e8b7`), so new versions install over the
old one and keep your saves, which live in `Android/data/org.afedecomp.dobutsunomori/files/save/`.
If the app ever closes unexpectedly, the next launch shows what happened before the game starts —
tap **복사** (Copy) and paste it into an issue.

### Playing in Korean

The Korean translation is a separate fan patch, not part of this project.

1. Download the patch zip from [retrodb 한글패치 — 동물의 숲 e+](https://retrodb.info/krpatch/7428/%EB%8F%99%EB%AC%BC%EC%9D%98-%EC%88%B2-e)
   (patch versions 0.9.2 and 0.9.3 are tested).
2. Start the game with your original disc, open the pause menu (**Esc** on PC, **Back** on Android) →
   **Settings** → **Disc** → **Korean patch: Install**.
3. Select the zip as downloaded (file picker on Windows and Android; on Linux, put the zip in the
   game folder's `patch/` folder first).
4. When it finishes (about a second), choose **Restart now**.

The patched image is written to `korean/`; your original image stays untouched. Use
**Korean: On / Off** on the same tab to switch back and forth.

### Default controls (PC)

| GameCube | Keyboard |
|---|---|
| Control stick | W / A / S / D |
| C-stick | Arrow keys |
| D-pad | I / J / K / L |
| A / B | Space / Left Shift |
| X / Y / Z | X / Y / Z |
| L / R | Q / E |
| Start | Enter |
| Pause menu | Esc |

Change them under pause menu → **Settings** → **Controls** → **Buttons**; gamepads can be rebound there
too. On Korean text-entry screens you can type Hangul directly (toggle with the 한/영 key, Right Alt or
Shift+Space).

## Known limitations

- The SD card, GBA link and Card e-Reader are reported as "no device"; the game shows its own message
  for each.
- Villagers do not reply to longer letters written in Korean. The patch still decides replies with the
  Japanese word table; it behaves the same on a GameCube.
- A save made with the Japanese disc and loaded with the Korean-patched disc shows saved names mixed
  with Hangul glyphs (same on a GameCube).

## Building from source

Building needs your own disc as well: the decompilation's matching build and some of the generated
sources come from it. Quick start on Linux (32-bit, the default desktop target):

```sh
sudo apt install gcc-multilib g++-multilib cmake ninja-build python3 libsdl2-dev:i386 libgl-dev:i386
git clone --recursive https://github.com/cailet0422/afe-port.git
cd afe-port
python3 pc/tools/gcdisc.py extract-orig <disc.ciso> decomp/orig/GAEJ01_01
(cd decomp && python3 configure.py --version GAEJ01_01 && ninja)
cmake -S pc -B pc/build32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-multilib32.cmake
ninja -C pc/build32
```

Put your disc image in `pc/build32/bin/rom/` and run `pc/build32/bin/DobutsuNoMoriEPlus`. If the
decompilation lives elsewhere, pass `-DDECOMP_ROOT=<path to afe-decomp>` to CMake.

Other targets — Windows via MinGW cross-compilation, 64-bit builds (`-DPC_ALLOW_64BIT=ON`), Android
(Gradle), asset-free distributable builds (`-DPC_STRIP_ASSETS=ON` and the `dist` target) — are covered
in the developer docs below.

### Documentation

The detailed documentation is currently written in Korean.

| Document | Contents |
|---|---|
| [docs/README.md](docs/README.md) | Architecture, build and run instructions for every platform, milestones, known issues |
| [docs/android.md](docs/android.md) | Android build, signing, touch controls |
| [docs/64bit.md](docs/64bit.md) | Porting the decompilation to 64-bit |
| [docs/korean_patch.md](docs/korean_patch.md) | Korean patch format and how the port supports it |
| [docs/dev_setup.md](docs/dev_setup.md) | Development environment, test scripts, save tools |

## Repository layout

| Path | Contents |
|---|---|
| `pc/` | Platform layer: SDL2, OpenGL / GLES renderer, audio, disc reading, settings and pause menus, Korean patch installer, Korean input method. Also the Android project (`pc/android`), generators and test tools (`pc/tools`), unit tests (`pc/tests`) |
| `decomp/` | The [afe-decomp](https://github.com/cailet0422/afe-decomp) submodule: the game code and its `#ifdef TARGET_PC` hooks |
| `docs/` | Developer documentation (Korean) |

## Contributing

PC-specific changes to the game code go into afe-decomp, inside `#ifdef TARGET_PC` only, so the
matching build keeps producing the original binaries (the hashes are listed in
[docs/README.md](docs/README.md)). After that commit lands, bump the `decomp` submodule here.

Never commit game data: disc images, assets extracted from them, or build outputs that contain them
(executables, APKs built with assets).

## Credits and license

- The platform layer in `pc/` is based on [ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port)
  (MIT); see [pc/LICENSE](pc/LICENSE). It includes [FixNES](https://github.com/FIX94/fixNES) (MIT) for
  the NES games.
- The decompilation (`decomp/`) is under afe-decomp's license (CC0 1.0).
- The Korean fan patch is the work of an independent author (역전맞고). This project and its
  maintainers did not take part in making it, and no patch files, translations or fonts are included
  here. Questions about the patch itself should go to its distribution page.
- Animal Crossing and Dōbutsu no Mori are trademarks of Nintendo. This is an unofficial fan project,
  not affiliated with or endorsed by Nintendo.
