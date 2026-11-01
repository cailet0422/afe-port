**English** · [한국어](README.ko.md)

# Dōbutsu no Mori e+ PC port / Korean patch

This document collects the plan and progress of the PC (Windows/Linux) and Android port based on afe-decomp and of
Korean patch support. This repository holds only the port layer (`pc/`) and documentation; the decomp (the game code
and the `TARGET_PC` hooks in it) is the `decomp/` submodule ([cailet0422/afe-decomp](https://github.com/cailet0422/afe-decomp)).

## Principles

- **The matching build never breaks.** All PC-related changes to the decomp sources (`decomp/src/`, `decomp/include/`)
  go only inside `#ifdef TARGET_PC` (some in `BUGFIXES`, `PC_ENHANCEMENTS`). Those changes are committed to afe-decomp,
  and this repository bumps the submodule commit. After changing the sources, rerun the matching build with `ninja` in
  `decomp/` and check that the SHA-1s below are unchanged.

  | File | SHA-1 |
  |---|---|
  | static.dol | `7574bec2692efc921c0975c52af41038f3fb7093` |
  | foresta.rel | `a3b987c9a0cc9764494ac1e6ec64dc6c5c16ead2` |
  | forestd.rel | `49fd5f046eb4d9ca9be98ab97c731c3553d9343c` |
  | foresti.rel | `98246b73d5ad3d14f2b61c4d8369d0ae52f14753` |
  | foresto.rel | `73ddcf0d1e979720c575f7ab0c5df2e9ff04eee0` |

- **No game data goes into the repository.** Disc images, assets extracted from the disc, and build outputs that
  contain them (`decomp/orig/*`, `decomp/build/`, `pc/build*/`) are not committed or distributed. Only code and patches
  (diffs) are distributed; users provide their own disc image.
- The PC platform layer (`pc/`) is taken from [ACGC-PC-Port](https://github.com/flyngmt/ACGC-PC-Port)
  (MIT) and adapted to e+. The license notice is in `pc/LICENSE`.

## Architecture

```
game code (decomp/src/, decomp as is)  ── N64 display lists ──▶ emu64 ──▶ GX ──▶ pc_gx (OpenGL 3.3)
        │                                                                        SDL2: window/input/audio
        ├─ Dolphin SDK / OS calls ─▶ pc_os, pc_dvd (reads the disc image directly), pc_card, pc_aram, pc_sdk_extra ...
        └─ Famicom ─▶ FixNES
```

- The 4 REL modules (foresta/forestd/foresti/foresto) are statically linked into a single executable on PC.
  foresti/foresto have no C sources yet, so `pc/tools/gen_module_data.py` generates C sources and assets from the dtk
  assembly using the decomp's `decomp/tools/converters/batch_assetdis.py`.
- By default, assets are included at compile time from the `.inc` files extracted by the matching build. Building with
  `-DPC_STRIP_ASSETS=ON` replaces the `.inc` files (18,070 of them) and the system font with zeros of the same shape,
  and fills them all at startup from main.dol and the RELs on the disc (`pc_rel_overlay.c`, "Asset-free build" below).
  The structure of the C sources made from the dtk assembly for foresti/foresto (display lists, tables) goes in
  unchanged either way.
- Sound is normally produced on a separate thread (the producer thread in `pc_audio.c`). On the GameCube, sound ran
  from the audio interrupt and could not cut into sections where the game code had blocked it with
  `OSDisableInterrupts`, so on PC "disabling interrupts" is implemented as a lock shared with the producer thread
  (`pc_os.c`). Each thread keeps its own enabled/disabled state, so nested calls lock only at the outermost level, and
  the producer thread holds the same lock while it produces one frame of sound. Nothing waits for sound progress with
  interrupts disabled (the corresponding sections of the game code are all short list/variable updates). With
  `--deterministic`, sound is produced on the game thread every frame.
- Sound path: the game's music, sound effects and animalese are produced on the CPU by an N64-style synthesizer (Nas,
  "NEOS") (`rspsim.c`), and JAudio's DSP channels (boot jingle) are mixed by `pc_dsp.c` instead of the GameCube DSP
  every subframe (80 samples) (AFC/PCM waveforms in ARAM, 4-tap resampling, volume ramps, front bus only; effect sends
  left out). `Jac_VframeWork` combines the two in chunks of 560 samples (32kHz stereo) and hands them to `AIInitDMA`.
  The work queue of the JAudio DVD thread (ARAM loads and callbacks) is processed by the sound thread every sound frame.
  During Famicom play, as with the original `EmuSound`, NES sound (FixNES APU, about 224kHz resampled to 1/60 s per game
  frame) replaces the mixer callback (N64-style music) (`pc_nes_fixnes.c`).
- Some data that is stored as big-endian raw bytes (`u8[]`) is read elsewhere as `u16` or as structs, so
  `pc/tools/gen_data_fixups.py` finds such blobs and generates a table (`pc_data_fixups.c`) that swaps their byte
  order once at boot. Blobs declared `static` (the Reset Center room and chair, 6 palettes of the diary furniture) cannot
  be referenced by name from outside, so a header force-included into that source file with `-include`
  (`gen/static_fixups/`) declares the blob in advance and defines the table entry inside that file (source and object
  file names stay the same).
- The decomp handles pointers as `u32` in many places, so the desktop default is a **32-bit (x86) build**. 64-bit
  (for Android arm64) has been fixed inside `TARGET_PC` to follow the pointer size and verified through new-game
  progress on Linux x86_64 ([64bit.md](64bit.md), CMake `-DPC_ALLOW_64BIT=ON`).

## Building (Linux, 32-bit)

Requires: `gcc-multilib g++-multilib cmake ninja-build python3 libsdl2-dev:i386 libgl-dev:i386`

0. Fetch the submodules too: `git clone --recursive <this repository>` (if already cloned, `git submodule update --init --recursive`).
   If the decomp lives somewhere else, pass `-DDECOMP_ROOT=<path to afe-decomp>` to cmake.
1. Prepare the matching build (needed for asset extraction and the dtk split)
   ```sh
   # put sys/main.dol and files/*.rel extracted from the disc in decomp/orig/GAEJ01_01/
   # (python3 pc/tools/gcdisc.py extract-orig disc.ciso decomp/orig/GAEJ01_01)
   cd decomp && python3 configure.py --version GAEJ01_01 && ninja && cd ..
   ```
2. PC build
   ```sh
   cmake -S pc -B pc/build32 -G Ninja \
         -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-multilib32.cmake
   ninja -C pc/build32
   ```
   The first configure takes a few minutes because of the foresti/foresto generation. 32-bit x86 builds with
   `-msse2 -mfpmath=sse`: with x87, storing an out-of-range float into an `s16` gives -32768, whereas the GameCube
   converts to 32 bits and keeps the low 16 bits, and the game code relies on that (angle interpolation
   `cKF_SkeletonInfo_subRotInterpolation` — the problem where the camera looked horizontally from ground height while
   crossing an acre boundary).
3. Run: put the disc image (`.ciso`/`.iso`/`.gcm`) in `pc/build32/bin/rom/`, or set `rom_dir` in `settings.ini` to
   the folder containing the image (or to the image file), then
   ```sh
   cd pc/build32/bin && ./DobutsuNoMoriEPlus --verbose
   ```

### Windows (32-bit)

- Cross-build on Linux (verified): get `gcc-mingw-w64-i686 g++-mingw-w64-i686` and the SDL2 MinGW development package
  (`SDL2-devel-2.30.x-mingw.tar.gz`, from the libsdl-org/SDL releases), then
  ```sh
  cmake -S pc -B pc/buildwin -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-mingw32.cmake \
        -DSDL2_DIR=<SDL2>/i686-w64-mingw32/lib/cmake/SDL2 -DPC_BUILD_TESTS=OFF
  ninja -C pc/buildwin
  ```
  `pc/buildwin/bin/` gets the exe, `SDL2.dll` (copied automatically), `shaders/` and the `.relmap`. Move this folder
  as a whole to Windows and put the disc image in `rom/`, or pick the folder containing the image with "Choose folder"
  (폴더 선택) in the prompt that appears on launch. The only DLL needed is `SDL2.dll` (everything else ships with
  Windows).
- Disc image location: `rom_dir` in `[Paths]` of `settings.ini` (a folder or an image file, UTF-8, Korean folder names
  allowed) is checked first; otherwise the game folder, `orig/` and `rom/` are searched. On Windows it can be
  changed with a File Explorer folder picker under pause menu (Esc) → Settings → Disc → Folder (takes effect on the
  next launch, `pc_rom_dir.c`).
- MSYS2 **MINGW32** shell: `pacman -S mingw-w64-i686-{gcc,cmake,ninja,SDL2,python}`, then the same steps as on Linux
  (untested).
- Run verified under Wine: the opening, Korean name entry and the new game → town path give the same results as the
  Linux build.
- Windows-only issue: emu64 took the end of the N64-format texture conversion buffer to be the start of the next array
  (an assumption about the GC linker layout), so with the MinGW layout every conversion failed (a different texture in
  place of "Press Start Button!" on the title screen). The buffer end is now taken from the array size. The exe is
  linked at `0x10000000` so that an image over 120MB does not overlap the N64 segment addresses
  (`0x03000000`–`0x0FFFFFFF`).

### Windows (64-bit)

- Cross-build on Linux: `gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64` and the `x86_64-w64-mingw32` part of the same SDL2
  MinGW package
  ```sh
  cmake -S pc -B pc/buildwin64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-mingw64.cmake \
        -DSDL2_DIR=<SDL2>/x86_64-w64-mingw32/lib/cmake/SDL2 -DPC_BUILD_TESTS=OFF -DPC_ALLOW_64BIT=ON
  ninja -C pc/buildwin64
  ```
- Windows is LLP64 (`long` is 4 bytes), unlike Linux and Android (LP64), but the type definitions change only under
  `__LP64__`, so `u32`/`s32` stay 4 bytes. Windows-only issue: in the table copies of the boot jingle (JAudio) the
  pointers are already addresses, but `PTconvert` compared them with the table address truncated to 32 bits and added the
  base again to pointers below that. On Linux the copies are above 4GB, so it happened to work; on Windows they are below
  4GB, so it crashed during boot (`Jac_InitHeap`). On 64-bit no conversion is done (afe-decomp `TARGET_PC`).
- Verified under Wine (64-bit): the original disc opening (8 screenshots), a new game on the Korean-patched disc (65), a
  week in one run (99) and the New Year's countdown (17) match the 32-bit Windows build down to screenshots and saves.
  The sound from `--audio-dump` differs slightly in waveform from the Linux build in some sections (the continuous sound
  by the pond, firecrackers) (per-section levels are the same; the 32-bit Windows build behaves the same way, so it looks
  like a floating-point difference in the math library).

Icons: `icon.ico` (7 sizes, Windows executable resource) made from `pc/res/icon.png` (512px original with the white
background removed), `icon64.rgba` (64×64 RGBA, embedded at build time with `bin2c.py` and shown on the window and
taskbar via `SDL_SetWindowIcon`), and Android `res/mipmap-*` (launcher icon and Android 8+ adaptive icon, background is
the border green `#5AAB49`).

Building also produces `DobutsuNoMoriEPlus.relmap` in `bin/`. The executable uses this table to re-read main.dol and REL
data from the running disc (this is also how the changed data of the Korean-patched disc gets in), so keep the two files
in the same folder.

### Asset-free build (PC_STRIP_ASSETS)

```sh
cmake -S pc -B pc/build32strip -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-multilib32.cmake \
      -DPC_STRIP_ASSETS=ON
ninja -C pc/build32strip
```

- The executable keeps only zeroed space for the assets (it is `.bss`, so the file is that much smaller too), and at
  startup it fills 17,693 assets from the disc following the relmap table (some, such as fonts, are used before the game
  starts, so `pc_main.c` does this right after opening the disc). If the relmap is missing or the assets cannot all be
  filled from the disc, it shows an error window and exits.
- At build time `gen_rel_overlay.py --require-assets` checks that every asset in the executable is in the table, and the
  build fails if even one is missing. When an asset with the same name exists in several places, the match is decided by
  `extract` (disc location) in `config.yml` and the per-section symbols of foresti/foresto.
- The result is the same as the regular build: compared with `--deterministic` scripts, Korean disc new game → save
  (202 screenshots, save file) and the original disc opening (52 screenshots) all match. Of 400 byte snippets picked
  from the assets, the 261 that are found in the regular executable are not found at all in this executable.
- Building still requires your own disc (the matching build and the foresti/foresto sources are made from the disc).
  Android: `gradle assembleDebug -PstripAssets`.
- `ninja -C pc/build32strip dist` packs the game folder into `dist/DobutsuNoMoriEPlus-Linux.zip` (`-Windows.zip` for
  the Windows cross build, including `SDL2.dll`): it contains only the executable (without debug info, keeping symbol
  names for crash logs), the relmap, `shaders/` and `LICENSE.txt` (the `rom/`, `save/` and settings created by running
  from `bin/` are not included). About 4MB for Linux, about 6MB for Windows. Recipients unzip it and put their own disc
  image in `rom/`, or run it and pick the folder. Both the unzipped Linux build and the Windows build run under Wine
  showed the same original disc opening screens as the regular build.

### Running in Korean (Korean patch)

Get the Korean patch from the page that links to the original distribution site ([retrodb Korean patch — Dōbutsu no Mori e+](https://retrodb.info/krpatch/7428/%EB%8F%99%EB%AC%BC%EC%9D%98-%EC%88%B2-e)).
The Korean patch is the work of a separate author (역전맞고) unrelated to this repository; this repository contains no
patch files, translated text or font data.

**Apply in game** (same on Windows, Android and Linux, `pc/src/pc_ko_patch.c`):

1. Launch the game with the original disc image (Japanese Rev 1, `.iso`/`.ciso`/`.gcm`).
2. Pause menu (Esc; the Back button on Android) → Settings → **Disc** tab → press Enter on **Korean patch: Install**.
   - Windows: a File Explorer file picker opens. Select the downloaded **zip as is** (or the `.xdelta` inside it).
   - Android: pick the zip in the system file picker (Downloads folder etc.).
   - Others (Linux): put the zip in `patch/` in the game folder and it is used.
3. When the progress bar finishes (about a second), choose "Restart now". After the restart the game is in Korean.
   - The patch does not modify the original image; it creates a separate Korean ISO (about 37MB) in `korean/` in the
     game folder. `korean = 1` and `ko_image` in `[Korean]` of `settings.ini` point to it.
   - The result is saved only if it matches the SHA-256 in the patch's `manifest.json` (if you pick only the `.xdelta`
     without the zip, v0.9.2 is checked against a built-in hash, other versions against the per-window Adler-32 in the
     patch). If the source image is not Rev 1, it stops with "not the one the patch is for".
   - **Korean: < On / Off >** in the same tab (restart after Apply) switches between the Korean version and the
     original disc.
   - v0.9.2 and v0.9.3 are verified. What changed in v0.9.3 (GBA e-Card screen, running across acre boundaries) is in
     the GBA program on the disc and in patch code, so it is not visible on PC; the data the PC reads is the same
     ([korean_patch.md](korean_patch.md)).

**Apply manually**: you can also apply the patch to a Rev 1 ISO with the tool included in the patch (or
`pc/tools/apply_ko_patch.py`) and put that ISO in `rom/`. Either way, if the run log shows `Korean patch detected`, the
game is in Korean mode (character widths, Hangul glyphs, logo, name plates etc. are read from the disc). See
[korean_patch.md](korean_patch.md) for the format.

### Rebinding controls

Change them under pause menu → Settings → **Controls** → **Buttons** (`pc_settings_menu.c`, `pc_keybindings.c`).

- Each of the 20 GameCube buttons (A B X Y Start Z L R, and four directions each of the stick, C-stick and D-pad) has a
  **Keyboard** and a **Gamepad** column. Pick a row with up/down and a column with left/right, press Enter, then press
  the key you want (mouse buttons included) or a pad button (LT/RT triggers included). Esc or 6 seconds without input
  cancels.
- Choosing a key already used by another button swaps the keys of the two buttons (so no key is duplicated or lost).
- The pad's left/right sticks are always the main stick/C-stick. Pad buttons can also be added to stick directions.
- **Reset to defaults** restores the initial values. Changes are saved to `keybindings.ini` immediately (`[Keyboard]`,
  `[Gamepad]`; up to two per line for the pad: `L = leftshoulder, lefttrigger`). An old `keybindings.ini` (keyboard
  only) is still read as is.
- While paused, the gamepad also navigates the menu (D-pad, A = confirm, B/Start = back).

### Scripted test input

`--input-script file` replays per-frame input, screenshots and exit
(format in `pc/include/pc_input_script.h`). Combined with `--no-framelimit` it runs fast.
It can also send key and pad events such as `KEY Escape` and `PAD dpdown`, so the pause and settings menus (key
settings, Korean patch install) can be tested. `POS` prints player and villager positions, and `SEEK` walks to a
villager or a point.
With `--deterministic` the same script produces the same screens every time (`--days N` starts N days later,
`--time H[:M]` at that time of day; 2026-11-01 is a Sunday, so `--days 6 --time 20` is Saturday night).
`--audio-dump file.wav` records the sound output as WAV (with `--deterministic` the same script gives the same WAV;
analysis with `pc/tools/audio_stats.py`, [dev_setup.md](dev_setup.md)).
Debug commands for scenes that come up only occasionally: `GIVE` (item into pockets), `LEDIT` (open the name-entry
editor: villager catchphrase etc.), `RESETS` (number of times the game was turned off without saving — Mr. Resetti
comes), `CATCH` (print the villager's catchphrase bytes), `ITEM` (print the item the menu selected), `JOBDONE` (finish
the part-time job the way the game does: clear the errand, make it wait for the HRA notice and restart the clock),
`NOJOB` (turn off only the part-time job event), `CLOCK h[:m]` (advance the clock to that time: past midnight into the
next day without relaunching), `FG x,z item` (place an item on that ground tile: walking into a `008D` Mr. Resetti hole
drops you into the Reset Center).

## Milestones

| Stage | Description | Status |
|---|---|---|
| M0 | Merge `TARGET_PC` hooks, verify matching is preserved, link the PC build | Done |
| M1 | Boot to the title screen on Linux/Windows | Done: Linux, Windows (MinGW cross build on Linux, run verified under Wine) |
| M2 | Town play, saves (memory card files), bypass the e+-only features (SD/e-Reader/GBA) | In progress: on the Korean disc, no crashes through train station → Tom Nook → house viewing and choice → free movement → pockets screen (`pc/tools/make_newgame_script.py`). Save/load: first save at the gyroid (haniwa) (on the first day it accepts only after you greet one villager), continue, overwrite save, and Mr. Resetti after quitting without saving verified (`make_newgame_script.py --save`, memory card layer test `pc/tests/test_pc_card.c`). Several days from the end of the part-time job (`JOBDONE`): sending a letter and the reply the next day, K.K. Slider song request on Saturday night, paying off the loan and house expansions (middle, big house, basement, second floor), buying the island and the round trip to the island on Kapp'n's boat, the photo menu, e-Card, GBA and train station SD checks ending with the "not present" messages (`make_days_script.py`, `make_device_script.py`). The part-time job taken from Tom Nook and done to the end (work clothes, planting, greetings, three deliveries, letter, bulletin board: `make_job_script.py`). A week in one run (`week`: crossing midnight with the input-script `CLOCK`, Monday letter → save/continue → Tuesday reply → Saturday K.K. Slider; about 12 minutes at normal speed, sound identical to the fast run down to the samples). Events: Redd's tent (`broker`, tent on an empty lot, Crazy Redd, inside the tent), Harvest Festival (`harvest`, announcement, Franklin), New Year's countdown (`countdown`, crossing midnight by the pond, HAPPY NEW YEAR, firecrackers); Reset Center (`FG` to the Mr. Resetti hole). Remaining: buying inside the tent, other seasonal events (Fishing Tourney, fireworks, Halloween etc.) |
| M3 | Audio fixes, Famicom (FixNES) | In progress: tools for checking sound (`--audio-dump` WAV, `pc/tools/audio_stats.py`, same script → same WAV). Fixed: mono output (the console setting was reported as mono, so town sound was identical left and right → stereo), the hang on the Z button (the SD check waited forever on "checking"), Famicom sound (through the mixer callback as in the original; game sound production used to stop), the sound system running twice per frame during Famicom play, the boot jingle (software DSP, DVD work queue, endianness of the JAudio tables, `SOUNDID_` and `MixConfig`, notes cutting out with the deterministic clock). Famicom: the path of getting the furniture, placing it and launching → playing → returning to the room with L+R+Z (`make_famicom_script.py`, dialogue checked on the Korean disc), no stutter at normal speed either, 64-bit gives the same screens and sound as clang 32-bit. All 24 hourly music tracks play (`BGM` debug command). 64-bit boot jingle (the JAudio wave and bank tables are rebuilt as host structs on 64-bit, same sound as 32-bit). All 15 Famicom games (`--game 0`–`14`; 15–18 are empty furniture that cannot be obtained according to the game data, so they are not placed) launch, play and return, with NES sound (Golf has no original background music during play). Famicom high scores are saved (previously the new `DobutsunomoriP_F_SAVE.sav` was filled with zeros, so the game did not save, treating it as "common save area corrupted"; the high score from the save shows on the title and remains after playing). DSP effects (reverb): the game does not enable effect lines (`DFX_SetFxLine` is only called with `NULL` at initialization) — no effect is sent on the GameCube either, so not mixing them is the original behavior. High score updated through actual play: Pinball (`--game 4 --keys A,SL,A,SR --every 50 --hold 20 --play 6000`) gets TOP 016860, which is saved, and Pinball on the next run starts with TOP 016860 and 1UP 000000 (64-bit gives the same screens and save). Remaining: checking by ear (the pitch of a few early-morning tracks) |
| M4 | 64-bit cleanup (pointer↔u32), Android (arm64, SDL2 + GLES3, touch input) | In progress: OpenGL ES 3.0 support in the renderer (`-DPC_GLES=ON`, opening and town verified with Mesa GLES on Linux), Android (armeabi-v7a) APK build, on-screen gamepad (touch) and Korean input with the Android keyboard ([android.md](android.md)). 64-bit: in `--deterministic` comparisons with the clang 32-bit build, the opening (52 screenshots) and the Korean new-game path (201 of 202 screenshots) give the same screens; name-entry window and editor button fixes; the Korean editor scenario (letter, catchphrase, save, continue; 134 screenshots) also gives the same screens; arm64-v8a included in the APK ([64bit.md](64bit.md)). Inside buildings (shop, museum, post office, Able Sisters, police station, house, villager houses; 71 screenshots), saving after furniture, purchases and donations (44 screenshots, identical down to the save bytes) and 32↔64-bit cross-loading of saves, the title demo, night/snow/rain, Famicom, and GLES 64-bit also give the same screens (fixes for items handed to NPCs on 64-bit, garbage bytes in saves, a stack overflow in the shop menu). The island round trip is verified on 32-bit (the boat is placed when you walk in from north of the dock). The whole part-time job, several days (letter, reply, K.K. Slider), house expansion, island round trip, second floor/basement, the no-device paths and the boot jingle also give the same screens and saves on 64-bit as on clang 32-bit. Remaining: checking on a real device |
| M5 | Korean patch | Support for the user's Korean-patched disc: display of dialogue, logo, name plates and character widths, Korean keyboard input (names, town name, English name, letter header/body/footer, catchphrase, answer to Mr. Resetti and K.K. Slider song request verified), automatic selection of dictionary codes, catchphrases and particles, strings and tables moved by the patch, line widths of the message board/diary/bulletin board, choosing the patch zip in the settings menu and applying it right away (creates the Korean image in `korean/` and restarts). On-screen keyboard for gamepad and touch (QWERTY Dubeolsik + 한/영, cursor with the D-pad). The patch's UI strings and constants (time, alarm and password entry titles, line breaks of the gyroid alarm message, bulletin board dates, the 24 pockets warnings and the photo menu text), name colors (per syllable), Korean animalese (kana voice chosen from the initial and medial, checked with `VOICELOG`), Korean input on the bulletin board and diary verified (`make_ko_edit_script.py ui\|warning\|boards\|voice`), SD and GBA failure messages (`make_device_script.py`), sending letters, villager replies and the answer to the K.K. Slider song request (`make_days_script.py`). Remaining: Korean letters get a reply only when short (the patch left unchanged the code that decides whether to reply using a Japanese word table, same as on the GameCube), comparing animalese as sound files ([korean_patch.md](korean_patch.md)) |
| M6 | Runtime asset loader, distribution packaging | In progress: asset-free build (`-DPC_STRIP_ASSETS=ON`) — the executable contains no assets taken from the disc and reads them from the disc at startup. Screenshots and saves are the same as the regular build. Linux and Windows zips through the `dist` target (Windows verified under Wine). Android APK `-PstripAssets` build (both ABIs, no assets in the library; fixed the asset-free build failing to link because the window icon was mistaken for a disc asset; fixed a crash during boot because 7774 generated-source assets of the Android build were missing from the disc table, [android.md](android.md)). Android verified through disc reading, opening and title on an x86_64 emulator. Remaining: checking on real Windows and Android devices |

## Korean patch design

### Character encoding and fonts

- The original has a 1-byte character table + 3 font banks (about 768 characters), so it cannot hold the 2,350
  precomposed Hangul syllables.
- A **2-byte encoding** is added for the PC build only: a lead byte is reserved in an unused range and the following
  byte gives the Hangul syllable index. Existing 1-byte characters (English/symbols/Japanese) stay as they are.
- Glyphs are rendered at boot from a TTF (a freely licensed Hangul font) into a texture atlas, and when the game's font
  drawing path (`mFont`) meets a 2-byte code, it draws from the atlas.
- Text width calculation and line breaking count a 2-byte code as one character.

### Text handling

- **Automatic particle selection**: for places where a name is inserted into a message (player/villager/item names),
  selection control codes for `은/는`, `이/가`, `을/를`, `과/와`, `으로/로` are added. They are decided at runtime by
  whether the preceding character has a final consonant (batchim) (including the ㄹ final consonant exception for
  `으로/로`).
- **Word-level line breaking**: Japanese breaks lines per character, but Korean breaks at spaces.
- **Name length**: the byte count of the name fields in the save data stays as is, and input is allowed only up to the
  number of characters that fit as 2-byte characters (save compatibility preserved).
- **Message data (BMG etc.)**: tools for extraction → text for translation (control codes written as tags) → repacking
  are built in `tools/`. Translations are managed as diffs that do not contain the original text.

### Input method (QWERTY, 한/영 switching)

- Core: `pc/src/ko/ko_ime.c`, `pc/include/ko_ime.h`, test `pc/tests/test_ko_ime.c` (87 passing).
- Layout: **standard Dubeolsik (2-set)**, on the US QWERTY key positions as they are (`r`=ㄱ, `R`=ㄲ, `k`=ㅏ, `o`=ㅐ,
  `O`=ㅒ ...).
- Composition rules: compound vowels (ㅘ ㅙ ㅚ ㅝ ㅞ ㅟ ㅢ), double final consonants (ㄳ ㄵ ㄶ ㄺ ㄻ ㄼ ㄽ ㄾ ㄿ ㅀ ㅄ),
  and when a vowel follows a final consonant, the last final consonant moves over as the initial of the next syllable
  (`각`+`ㅏ` → `가가`, `값`+`ㅏ` → `갑사`).
- Backspace deletes per jamo (`값` → `갑` → `가` → `ㄱ` → nothing).
- **한/영 switching**: the composing syllable is committed, then the mode switches. In English mode input passes
  through unchanged.
  - Physical keyboard: any of the `한/영` key, right Alt, `Shift+Space` (to be made changeable in the key settings).
  - On-screen keyboard: a QWERTY layout and a `한/영` key are drawn in place of the wheel on the text entry screen (see
    "Editor hookup" below). On the gamepad, X switches 한/영.
- Output is Unicode (precomposed U+AC00–U+D7A3, compatibility jamo U+3131–U+3163); conversion to the Korean patch
  encoding is done by `pc/src/ko/ko_charset.c` (KS X 1001 order → 2 bytes, test `pc/tests/test_ko_charset.c`).
  Single jamo being composed are shown with the patch's jamo glyphs (19 initials + 21 medials). Syllables not in KS X 1001
  (e.g. 똠) show as their initial while composing and are dropped on commit (the patch font has no glyph for them).

#### Editor hookup (`pc/src/pc_typing.c`, TARGET_PC in `decomp/src/game/m_editor_ovl.c`)

- When the editor opens, key input is turned into a queue of edit actions (1-byte character, 2-byte Hangul character,
  replace/commit of the composing syllable, cursor, backspace, Enter), which the editor runs on the next frame through
  its original command path (`mED_edit_func_*`). The composing syllable is also in the buffer, so it shows as is
  anywhere: names, letters, message board.
- Hangul takes 2 buffer slots (2 bytes or 2 u16s), as in the patch. Cursor movement and backspace work per character;
  line width is 6 for space, comma and period and 12 for everything else (same as the patch's
  `mED_get_col_line_width`). The name position in the letter header (`header_back_start`) also moves by 2 slots.
- On the Korean-patched disc, keyboard input turns on as soon as the editor opens (the English town name starts in
  English mode). `Tab` turns it off and on (when it is off, the keyboard acts as the gamepad). 한/영: `한/영` key,
  right Alt, `Shift+Space`. `Enter` inserts a line break when multi-line and confirms when single-line (in the letter
  header and footer, moves to the next field); `Ctrl+Enter`/`Shift+Enter` finishes.
- The patch puts its own Hangul input UI in place of the kana wheel and replaced the wheel's textures for that UI; this
  UI is not reproduced. In Korean mode, wheel input is turned off and an **on-screen keyboard** (`pc/src/pc_ko_editor.c`,
  layout and cursor in `pc/src/ko/ko_osk.c`, test `pc/tests/test_ko_osk.c`) is drawn in place of the wheel.
  - Layout: a number row + three QWERTY rows (showing Dubeolsik jamo in Hangul mode and letters in English mode) +
    `한/영`, space, `<-` (delete) and `확정` (commit). The keys compose through the same built-in input method
    (`ko_ime.c`) as the physical keyboard, so final consonant carry-over, double final consonants and per-jamo deletion
    behave exactly the same. `^` (Shift) applies only to the next single character (ㄲ ㄸ ㅃ ㅆ ㅉ ㅒ ㅖ, capitals,
    `!@#…`).
  - Gamepad (while the editor takes input): stick = move between keys, D-pad = move the text cursor (up/down lines; in
    letters, into the header and footer too; as on the game's original input screen, where the stick drives the wheel
    and the D-pad the cursor), holding either repeats. A = press, B = delete, X = 한/영, Y = Shift, Z = commit (line
    break when multi-line), L/R = cursor left/right, START = finish. Buttons the keyboard uses are not passed to the
    game (`pc_ko_osk_pad`, `pc_pad.c`). The matching button letter is written in the corner of the special keys.
  - Touch: touching a key with a finger presses that key (`pc_ko_osk_touch`; `pc_touch.c` asks it before the on-screen
    pad). Window coordinates are converted to the game's 2D coordinates through the widescreen correction
    (`pc_gx_window_to_ui`).
  - With the system IME setting (`text_input = 1`) as well, the on-screen keyboard composes with the built-in input
    method. Physical keyboard input works as before.
- **Letters** (`mED_TYPE_LETTER`, message board `m_board_ovl.c`): the header (before and after the recipient's name),
  the multi-line body and the footer are written with the keyboard or the on-screen keyboard (stick, touch). Text past
  the line width wraps to the next line, deletion is per jamo, the cursor moves per character, and at the header length
  limit a character is never cut in half. The footer is right-aligned, but the original code placed it at 12 pixels per
  slot, so with Hangul (one character per 2 slots) it was out of line with the cursor — in Korean mode it is aligned by
  glyph width (`mBD_set_writing_footer`). Reopening the letter via confirm → letter slot in pockets → "다시쓰기"
  (rewrite) shows it unchanged, and it is also the same after save → continue. The input screen covering the lower part
  of the letter is the same as with the original wheel, and when the cursor moves down the letter scrolls up with it.
- **Name-entry editor** (`m_ledit_ovl.c`): the catchphrase, the answer to Mr. Resetti and the song request to K.K.
  Slider are typed into longer buffers, as in the patch (8, 18 and 18 bytes, `pc_ko_ledit_buf`). A catchphrase that fits
  in 4 bytes goes in as is; otherwise it is stored as `0xFE` + four 12-bit values in the villager's 4-byte field and the
  3 bytes of padding after it (`ko_charset_pack_ending`; it stays that way in the save file too). For the song request
  and the answer, dictionary codes are expanded and all spaces removed before comparing with the song names and Mr.
  Resetti's phrase (`pc_ko_ledit_match`). The "SP" mark attached to typed spaces is drawn at half size because spaces in
  Korean mode are half width. Details in [korean_patch.md](korean_patch.md).
- On the original disc it is turned on with `Tab` as in ACGC, and accepts only English letters, digits, space and
  `!?,.`, which are certain to exist in the Japanese font.
- **System IME** (`text_input = 1` in `[Input]` of `settings.ini`, the Android default): uses the OS input method
  (Android on-screen keyboard, Windows IME etc.) instead of the built-in Dubeolsik. When the editor opens,
  `SDL_StartTextInput` brings up the on-screen keyboard; committed characters (`SDL_TEXTINPUT`) go in as is, and the
  composing syllable (`SDL_TEXTEDITING`) is put into the buffer with a "composing" mark, then removed and re-inserted
  when it changes (`PC_TYPING_CMD_PREEDIT_CLEAR`). Even when the field is full and only part of the composing text fits,
  exactly the part that went in is removed. SDL 2.30's Android input connection sends composition as "backspace + new
  character", and that path gives the same result too. When a gamepad command commits the composition,
  `SDL_ClearComposition` also clears the composition on the IME side so the same character does not go in twice.
- Automated tests: the `--input-script` commands `TYPE` (built-in input method keys) and `EDIT`/`TEXT` (system IME
  composing/committed text) (`pc/src/pc_input_script.c`), `make_opening_script.py --korean`. The on-screen keyboard is
  tested with script buttons (`DOWN`, `RIGHT`, `A`, `X`, `Y`, `B`, `Z`) and `TOUCH`: verified that in name entry,
  typing "한국" (Korea) with the pad, then Shift, 한/영 and delete, and `?` by touch gives "한국?".
  `make_opening_script.py --korean` does not press A while the editor is open (it would press keys of the on-screen
  keyboard). Letters, catchphrase, Mr. Resetti and song request are tested with `make_ko_edit_script.py` (starts from a
  saved town, [dev_setup.md](dev_setup.md)).

## Known issues

- Saves are stored as files in `save/card_a/` in the PC byte order (little-endian) as is. Saves taken from a GameCube
  memory card (GCI) are imported and exported with the `afe_gci` tool (town `DobutsunomoriE_MURA`, Famicom common save
  `DobutsunomoriE_F_SAVE`; usage in [dev_setup.md](dev_setup.md)). The field table is generated from the e+ headers
  (`pc/tools/gen_save_swap.py`), and checksums are recomputed in the swapped byte order. Verified: round trip, per-field
  values and checksums on synthetic data (`pc/tests/test_save_swap.c`); one real town GCI (not in the repository)
  converted to the PC format has a matching checksum and plausible values (town id, villagers, wallet, house size), and
  converted back to GCI the data is byte-identical to the original (same on 32- and 64-bit).
  Also verified in game: importing a real town GCI made on the Japanese version and continuing (villager's question →
  choose player → town, with house size, island, pockets and furniture in the house intact) → saving at the gyroid →
  continuing the next day (the changed pockets and Bells remain) → exporting to GCI (checksum matches) → importing that
  GCI again gives a save byte-identical to the PC save and the same screen on continue. The 64-bit build also gives the
  same screens and saves, and the `afe_gci` conversion results are the same as on 32-bit. Loading a save made on the
  Japanese version with the Korean-patched disc shows the names stored in the save (villager "エックスエル" etc.) mixed
  with Hangul glyphs (the patch put Hangul into those character codes, so the same happens on the GameCube; with the
  original disc they show as is).
  **Not verified**: loading an exported GCI on a GameCube (or Dolphin), a real Famicom save GCI (tested only with
  synthetic data). Exporting a Famicom save without a template gives only the comment and no banner or icon (the PC does
  not have those images; `--template` can use the images of an original GCI). Other files, such as the player file
  (`DobutsunomoriE_PL_*`, while traveling) and letter and design files, are not converted yet.
- SD card, e-Reader and GBA link respond as "no device" (`EXIProbeEx` -1, GBA calls fail). The game ends with its own
  "not present" message in each case: SD saving in the photo menu (previously `EXIProbeEx` was 0, "checking", so
  `mCsd_sdcard_slot_chk` looped forever and pressing A hung — the same cause as the Z button hang, fixed together in
  M3), the e-Card terminal at the post office (reading, preparing the reader), sending GBA designs at the Able Sisters,
  and the SD check when leaving town at the train station (it moves on to memory card slot B, rides the train and
  returns to the title). The check path is `pc/tools/make_device_script.py`. The SD worker thread (`osCreateThread2`)
  runs synchronously to completion when started on PC, and the `JUTGba` thread does not run, but
  `mGcgba_ConnectEnabled` returns an error after a fixed number of tries.
- Sound: effect (reverb) sends of the JAudio DSP are not mixed. The game does not enable effect lines (only
  `DFX_SetFxLine(i, NULL, NULL)`), so no sound goes to effects on the GameCube either, and this matches the original. A
  few of the early-morning hourly tracks (2, 3 and 4 o'clock) measure ±30–40 cents off equal temperament, but these
  tracks have sparse notes, so it has not been checked by ear whether this is a measurement artifact or really off.
