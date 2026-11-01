**English** · [한국어](android.ko.md)

# Android build

The PC port runs on Android with SDL2 + OpenGL ES 3.0. One APK carries both **32-bit ARM (armeabi-v7a) and
64-bit ARM (arm64-v8a)** libraries, and the device picks the one that fits. Some recent devices (Snapdragon
8 Gen 3 and later, Pixel 7 and later, etc.) cannot run 32-bit apps, so the 64-bit one is used there (64-bit porting:
`64bit.md`).

The APK has the **assets the matching build extracted from your own disc** compiled in, so build it yourself and
install it only on your own device (do not distribute it). Building with `-PstripAssets` leaves the assets out and
reads them from the disc at start-up (see "Asset-free build" in [README.md](README.md)).

## Prerequisites

- JDK 17 or later, Android SDK (platform 34, build-tools 34), NDK `27.2.12479018`, CMake `3.22.1`
  (Android Studio's SDK Manager or `sdkmanager`), Gradle 8.7 or later (or Android Studio)
- Python 3 (the native build runs the same generator scripts as the PC build)
- Do the matching build once (`python configure.py --version GAEJ01_01 && ninja` in `decomp/`, see README)
- SDL2 source (2.30.x): unpack `SDL2-2.30.9.tar.gz` from `https://github.com/libsdl-org/SDL/releases` into
  `pc/android/SDL`, or point the build at it with `-PsdlDir=path`. SDL's Java code (`SDLActivity`) and
  `libSDL2.so` come from here.

## Build

```sh
cd pc/android
echo "sdk.dir=/path/to/android-sdk" > local.properties
gradle assembleDebug -PsdlDir=/path/to/SDL2-2.30.9
# → app/build/outputs/apk/debug/app-debug.apk
```

In Android Studio, just open `pc/android`. The first native build takes a few minutes because of the foresti/foresto
generation (once per ABI). The native build puts the shaders and the REL overlay table into the APK's assets. The
overlay table depends on the library's layout, so it is generated separately for each ABI
(`DobutsuNoMoriEPlus-arm64-v8a.relmap` etc.). If you need only one ABI, trim `abiFilters` in `app/build.gradle`.

`-PstripAssets` check (Linux, SDK via `sdkmanager "platforms;android-34" "build-tools;34.0.0" "ndk;27.2.12479018"
"cmake;3.22.1"`, SDL2 2.30.9, Gradle 8.14, JDK 21): `gradle assembleDebug -PsdlDir=… -PstripAssets` finishes in about
7 minutes and the APK is 12MB (libraries: armeabi-v7a 9.6MB, arm64-v8a 15.5MB). Of 400 samples of 32 bytes each, picked
from the assets the matching build extracted, 399 are found in the regular desktop executable, and only 1 in the
`libmain.so` of both ABIs (a list of consecutive item numbers that happens to match another table in the code; the
desktop asset-free build has the same 1). Before that, the window icon (`pc_window_icon_rgba`) was wrongly taken for a
disc asset and `--require-assets` failed the link (fixed with `bin2c.py --own`). It has not been run on a device.

## Release signing (release APK)

`gradle assembleRelease` signs with the key from `pc/android/release-key/keystore.properties` if that file exists
(otherwise with the debug key, as before). Give a file located elsewhere with
`-PreleaseKeyProps=path/keystore.properties`. `storeFile` is relative to the folder that file is in.

```properties
storeFile=afe-port-release.jks
storePassword=…
keyAlias=afe-port
keyPassword=…
```

Creating the key (once): `keytool -genkeypair -keystore afe-port-release.jks -storetype PKCS12 -alias afe-port
-keyalg RSA -keysize 4096 -validity 36500`. `release-key/`, `keystore.properties` and `*.jks` are in `.gitignore`
and are not committed. **Keep the key and the passwords safe outside the repository**: the next version's APK must be
signed with the same key to update over an existing install, and if the key is lost, users have to uninstall the app
(after moving their saves) and install it again.

```sh
cd pc/android
gradle assembleRelease -PsdlDir=/path/to/SDL2-2.30.9 -PstripAssets
# → app/build/outputs/apk/release/app-release.apk (not debuggable, v1/v2 signatures)
apksigner verify --print-certs app/build/outputs/apk/release/app-release.apk
```

The release build also compiles the decomp code at `-O0`, like the desktop. The Gradle plugin builds release as CMake
`RelWithDebInfo` (`-O2`), and optimization exposes undefined behavior in the decomp code and also turns on bionic's
FORTIFY checks (enabled only when optimizing). So `pc/CMakeLists.txt` removes the build type's `-O` flags (SDL2 stays
at `-O2`). The v0.1.1 APK was such an `-O2` build. The libraries are aligned to 16KB so that they also load on
16KB-page devices (Android 15 and later) (`ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES`).

## Testing on an emulator

Building with the ABI changed via `-Pabis=x86_64` lets it run on an x86_64 emulator (the default is
`armeabi-v7a,arm64-v8a`). The method used on Linux without KVM (this work environment):

```sh
sdkmanager "emulator" "system-images;android-34;default;x86_64"
avdmanager create avd -n x64 -k "system-images;android-34;default;x86_64" -d pixel_6
# config.ini: disk.dataPartition.size = 2147483648 (if disk space runs short, put the AVD in /dev/shm and set ANDROID_AVD_HOME)
E=$ANDROID_SDK_ROOT/emulator
LD_LIBRARY_PATH=$E/lib64:$E/lib64/qt/lib:$E/lib64/gles_swiftshader \
  $E/qemu/linux-x86_64/qemu-system-x86_64-headless -avd x64 -no-window -no-boot-anim \
  -gpu swiftshader_indirect -feature -Vulkan -no-snapshot -accel off
```

The `emulator` launcher refuses x86_64 images without KVM, so the qemu inside it is started directly (boot takes about
5.5 minutes, the game is very slow). The ARM64 image did not boot because the qemu for x86 hosts has no PCI bus. After
`adb root`, put the disc image in `/data/media/0/Android/data/org.afedecomp.dobutsunomori/files/rom/` and change its
owner to the app. Check with `adb logcat` (SDL, `F libc`, `F DEBUG`), `log.txt`/`crash.txt` in the data folder, and
`adb exec-out screencap -p`.

The lint check at the end of the release build downloads files from Maven. If that server returns 429 (Too Many
Requests), run it again a little later (the native build is already done, so it finishes quickly).

## Running

1. Put the disc image (`.iso`/`.ciso`/`.gcm`; a Korean-patched one works too) in any folder on the phone
   (e.g. `Download/DobutsuNoMori`).
2. Install and launch the APK; a notice says the disc image was not found. Tap "Choose folder" (폴더 선택), pick
   the folder that holds the image in the system folder picker, then tap "Use this folder" (이 폴더 사용) and allow
   it (허용). The chosen folder is remembered, and later launches start right away. Android does not let you pick
   the storage root or `Download` itself, so make a folder inside it. It must be a folder on the device, not in
   the cloud (Google Drive etc.).
3. Change the folder in the pause menu (Back button) → Settings → Disc → Folder (applies after a relaunch).
4. As before, you can also put the image in the app data folder
   `Internal storage/Android/data/org.afedecomp.dobutsunomori/files/rom/` (searched when the chosen folder has no
   image). `settings.ini`, `save/` and `log.txt` (run log) are created in this data folder.

The app reads the chosen folder with the permission granted through the system folder picker (SAF)
(`MainActivity.java`, `pc_rom_dir.c`). It requests no other storage permission.

### Applying the Korean patch (in-game)

1. Launch with the original disc image (Japanese Rev 1), then Back button → Settings → Disc →
   **Korean patch: Install**.
2. In the system file picker, pick the downloaded Korean patch **zip** (or the `.xdelta` inside it). It can stay in the
   Downloads folder as it is.
3. When it finishes, **Restart now**. The app restarts itself and comes up in Korean.

The Korean image (about 37MB) is created in `korean/` in the app data folder; the original image is left as it is.
**Korean < On / Off >** on the Disc tab switches between it and the original disc (restart after Apply). The file is
read once with the permission granted through the system file picker (SAF) (`MainActivity.pickPatchFile`). The
restart is done by `RestartActivity` in a separate process (`android:process=":restart"`), which ends the game process
and opens it again — because the native library's static variables are not reinitialized within the same process.

### Controls

Settings → Controls → Buttons remaps the Bluetooth gamepad's buttons (and physical keyboard keys)
(see "Rebinding controls" in [README.md](README.md)). While paused, the gamepad's D-pad, A and B move through the menu.

## Current status

- App icon: `res/mipmap-*/ic_launcher.png` (48–192px) and an adaptive icon (`mipmap-anydpi-v26`; the foreground is the
  central 80dp of a 108dp canvas, background color `#5AAB49`). The source image is `pc/res/icon.png`.
- Run check: on the x86_64 emulator (Android 14, see "Testing on an emulator" above), confirmed through the disc folder
  notice, disc reading (17736 assets), the opening and the title logo. Not yet confirmed on a real device. The same
  renderer was confirmed on Linux with an OpenGL ES 3.0 context (dev_setup.md, section 7).
- The v0.1.0–v0.1.2 APKs closed as soon as they were tapped. Two problems overlapped.
  - Because of `SDL_MAIN_HANDLED` in `pc_platform.h`, `main()` is not renamed to `SDL_main`. So SDL could not find
    `SDL_main` in `libmain.so` (`nativeRunMain(): Couldn't find function SDL_main`) and exited without starting the
    game. It was not a crash, so nothing was recorded. `MainActivity.getMainFunction()` returns `"main"`.
  - After that, it died during boot in `JUTResFont::getWidth` (the system font was empty). In the Android build the
    generated sources (the foresti/foresto data in `gen/`, the system font) are outside the target's (`afe/`) build
    folder, so CMake names their objects by paths relative to `pc/`; `gen_rel_overlay.py` could not find those sources
    and put only 9962 of the 17736 assets into the table. It now also looks up `pc/`-relative paths (the desktop
    table stays identical down to the byte). If an object whose source could not be found has zero-filled data,
    `--require-assets` stops the build.
- Input: gamepads (Bluetooth etc.) and physical keyboards are handled by SDL. When a text entry screen (name, letter,
  etc.) opens, the Android on-screen keyboard comes up and Hangul is typed with it (`text_input = 1` in `settings.ini`,
  see "System IME" in [README.md](README.md)). With the Korean-patched disc you can also press the on-screen
  keyboard below the game screen (QWERTY Dubeolsik) with a finger or the gamepad (see "Editor hookup" in
  [README.md](README.md)).
- Touch controls (`pc/src/pc_touch.c`, `touch_controls = 1` in `settings.ini`, the Android default): an on-screen pad
  shaped like a GameCube controller is drawn over the game screen. Putting a thumb on the lower-left half makes that
  spot the stick center (floating stick); A (large)/B/X/Y are on the right, L/START/R/Z along the top, and a small
  D-pad at the upper left. Sliding between buttons presses the button under the finger. Sizes are based on the
  screen's short side, so the pad sits against the edges on both 4:3 and 20:9. Pressing a real gamepad button hides
  it, and touching the screen again brings it back. It is drawn with its own shader after the frame ends, and the GX
  state is marked to be reapplied (`pc_gx_restore_after_nes`).
- Crash report: the launcher icon opens `LaunchActivity`. If the last run ended abnormally, it shows the report before
  starting the game (`MainActivity`, which loads the native library) ("복사" (Copy), "게임 시작" (Start game),
  "닫기" (Close)); otherwise it starts the game right away. This is because a window shown in the same process as the
  game disappears with it when the game dies again right at start-up (v0.1.2). Contents (`CrashReport.java`):
  - `crash.txt`: a native crash of the game (SIGSEGV, SIGABRT etc., `pc_android.c`; the signal, the address and a call
    stack in `libmain.so+offset (function)` form; after writing, it passes on to the system's handler) or a Java
    exception (`CrashReport.installHandler`).
  - The system's exit records (`ApplicationExitInfo`, Android 11+): crashes, ANRs, `exit()` with an error status.
    Crashes before the game's handler is installed (e.g. while the library is loading) are recorded too. For native
    crashes on Android 12+, it reads the tombstone (protobuf) and attaches the signal, the call stack of the thread
    that died and the last 40 lines of logcat from that process (`Tombstone.java`).
  - App version, device, Android version and the last 40 lines of `log.txt`. The full report is kept as
    `crash_prev.txt`, and the same crash is shown only once.
  `log.txt` is written immediately, line by line.
- Graphics settings: the Video tab has only VSync, MSAA and Textures. Android always uses the full screen, so the PC
  window mode and resolution (Display, Resolution) are hidden and not used even if they are in `settings.ini`. If the
  GPU does not support the chosen MSAA, window creation is retried with 8x→4x→2x→off
  (`[PC] MSAA 8x is not available, using 4x`). Before, choosing MSAA 8x on a Galaxy Z Flip6 failed to create the window
  with `Couldn't find matching EGL config`, and since the setting was saved, the app quit right at start-up every time.
- Exit: on Android the app exits with `_exit()` instead of `exit()` (`pc_exit`; Quit Game, the restart after the Korean
  patch, start-up failure). `exit()` runs C++ static destructors, and in the meantime the system's render thread
  (libhwui) used an already destroyed mutex and aborted with "FORTIFY: pthread_mutex_lock called on a destroyed mutex".
  A start-up failure (`pc_fatal`) shows the reason in a message box and also writes it to `log.txt`.
- Crash protection (`pc_crash_set_jmpbuf`; when emu64 reads a bad address from a display list, that frame is skipped)
  does a `longjmp` only for faults on the thread that set the `jmp_buf`. Before, a SIGSEGV on another thread (the
  Android UI thread, audio) also jumped onto the game thread's stack, and the two threads ran game code together until
  it died with "stack corruption detected" (reproduced on the emulator with `kill -SEGV` while running). Such faults
  now go straight to the crash report.
- The Back button opens the pause menu instead of closing the app (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`, same as Esc).
  The pause menu only takes key input, so the on-screen pad's D-pad/A/B (START) are sent as arrow keys/Enter/Esc.
  If you dismissed the on-screen keyboard during text entry, tap a spot without the pad to bring it back up. Touches
  are not turned into mouse clicks (`SDL_HINT_TOUCH_MOUSE_EVENTS`), and the accelerometer is not picked up as a
  joystick.
- Automated tests: with `TOUCH x,y[:x2,y2] [frames]` in `--input-script` (% of the window size; moves to x2,y2 on the
  next frame), `touch_controls = 1` is turned on and tested on the desktop (walking with the stick, START for pockets,
  B to close confirmed).
- Fixed because of differences between Clang (NDK) and GCC: `-fsigned-char` (char is unsigned on ARM), `size_t` in
  `operator new`, enumerators named `EOF`/`SEEK_*` that get replaced by stdio macros (int → enum conversion), C++
  linkage of libc++ `<cmath>`, bionic's `bcmp`, the lld link map and Clang's names for function-local statics
  (`function.variable`).
