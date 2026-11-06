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
Latest release: **[v1.5.3](https://github.com/cailet0422/afe-port/releases/tag/v1.5.3)**

| Platform | Download | Requirements |
|---|---|---|
| Windows 64-bit | [DobutsuNoMoriEPlus-v1.5.3-Windows-x64.zip](https://github.com/cailet0422/afe-port/releases/download/v1.5.3/DobutsuNoMoriEPlus-v1.5.3-Windows-x64.zip) | OpenGL 3.3, Vulkan 1.0 or Direct3D 12 (`SDL2.dll` included) |
| Windows 32-bit | [DobutsuNoMoriEPlus-v1.5.3-Windows-x86.zip](https://github.com/cailet0422/afe-port/releases/download/v1.5.3/DobutsuNoMoriEPlus-v1.5.3-Windows-x86.zip) | OpenGL 3.3, Vulkan 1.0 or Direct3D 12 (`SDL2.dll` included); also runs on 64-bit Windows |
| Linux x86_64 | [DobutsuNoMoriEPlus-v1.5.3-Linux-x86_64.zip](https://github.com/cailet0422/afe-port/releases/download/v1.5.3/DobutsuNoMoriEPlus-v1.5.3-Linux-x86_64.zip) | SDL2 (`libsdl2-2.0-0`), OpenGL 3.3 or Vulkan 1.0 |
| Android | [DobutsuNoMoriEPlus-v1.5.3-Android.apk](https://github.com/cailet0422/afe-port/releases/download/v1.5.3/DobutsuNoMoriEPlus-v1.5.3-Android.apk) | Android 5.0+, ARM 32/64-bit; OpenGL ES 3.0, or Vulkan on Android 7.0+ |

Checksums: [SHA256SUMS.txt](https://github.com/cailet0422/afe-port/releases/download/v1.5.3/SHA256SUMS.txt) ·
Older versions and release notes: [all releases](https://github.com/cailet0422/afe-port/releases)

> [!IMPORTANT]
> **No game data is included.** You need your own disc image of the Japanese
> *Dōbutsu no Mori e+* **Rev 1 (GAEJ01)** as `.iso`, `.ciso` or `.gcm`. Disc images are not
> distributed here. The release builds contain no assets from the disc; they read everything from
> your image at start-up.

> [!NOTE]
> Releases are tested widely on Linux with scripted play-throughs, the Windows builds under Wine
> and the Android build on an emulator, but not yet on many real Windows PCs or Android phones. Please report problems in [Issues](https://github.com/cailet0422/afe-port/issues).

## Features

- Runs natively on Windows (32/64-bit), Linux and Android (ARM 32/64-bit) through SDL2, drawing with
  OpenGL 3.3 / OpenGL ES 3.0, Vulkan or (on Windows) Direct3D 12 ([Graphics API](#graphics-api)). No emulator
  involved.
- Reads the game straight from your disc image; the original image is never modified.
- The port's own pause and settings menus: windows over the game you use with the mouse and keyboard
  (or a gamepad) on PC and with your fingers on Android, in Korean or English. Video options, volume,
  key and gamepad rebinding, disc folder selection, the Korean patch, HD textures and multiplayer.
- **HD textures**: install a Dolphin HD texture pack zip from the menu (made for the
  [Animal Crossing HD Texture Pack](https://forums.dolphin-emu.org/Thread-animal-crossing-hd-texture-pack-version-25-october-11th-2026)
  v25, the work of TechieAndroid and contributors, not of this repository; downloaded separately); it is read
  straight from the zip, loads while you play and leaves the Korean patch's own textures alone
  ([HD textures](#hd-textures)).
- **Multiplayer without a server**: host a room in your town (public, or private with a room code and
  password) and friends on the same network or over the internet take the train to visit; you see
  each other (with your own custom designs), chat, and drop, pick up, plant and dig in the same town, with
  the host's villagers, furniture, weather, shops and museum. The host can send players out
  ([Multiplayer](#multiplayer)).
- **Autosave**: the game saves by itself every minute outdoors, between scenes (going in or out of a
  building) and when you quit, so closing it or a crash loses about a minute at most. A new town is saved too, as
  soon as the gyroid could save it: on the first day, right after the talk with the first villager you meet. Mr. Resetti therefore stays
  away by default ([Saving](#saving)).
- Korean fan patch support: install the patch zip from inside the game, switch between Korean and
  Japanese at any time, and type Hangul with a real keyboard (2-set layout) or an on-screen keyboard.
- Android: on-screen touch gamepad (its buttons can be moved and resized), a **full touch mode** played with the
  fingers on the world (tap to walk, tap villagers and things, icons for the pockets and the map, a tool strip, a
  furniture mode in your house; [Touch controls](#touch-controls)), physical gamepads, and a crash report shown on
  the next launch.
- Minimap: outside in town, a small map of the town sits in the bottom left corner (you, facing the
  way you walk, and the villagers' houses). **M** opens the game's full map at any time, even before
  you have the map from the police station; on Android, tap the minimap. Turn it off under
  **Settings** → **Gameplay** → **Minimap**.
- Seamless acres: outside in town you walk straight on from one acre into the next, with no stop at the edge, and the
  camera follows you over it. The acres around you are already alive: their houses, villagers out walking, insects,
  fish and the like are there before you arrive. Turn it off (to cross as on the GameCube) under **Settings** →
  **Gameplay** → **Seamless acres**.
- Updates: from v1.0.0 on, the game looks for a new release when it starts and offers to install it
  (saves, settings, the disc and the Korean patch stay). Turn it off under **Settings** → **Gameplay**
  → **Updates**.
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
3. Play with the on-screen touch gamepad, the full touch mode ([Touch controls](#touch-controls)) or a physical
   controller. **SAVE**, left of START on the touch gamepad (the arrow-into-tray icon in the full touch mode), saves
   right away.

The APK is signed with a release key (certificate SHA-256
`db6fc1ad60b6b6d980bee0990f120ef481f195f117729fa194b124d14752e8b7`), so new versions install over the
old one and keep your saves, which live in `Android/data/org.afedecomp.dobutsunomori/files/save/`.
If the app ever closes unexpectedly, the next launch shows what happened before the game starts —
tap **복사** (Copy) and paste it into an issue.

### Updating

From v1.0.0 on, the downloads above update themselves. When the game starts it checks the
[releases](https://github.com/cailet0422/afe-port/releases); if there is a newer one, the pause menu opens
with **Update now**, **Later** or **Skip this version**.

- Windows / Linux: the new zip is downloaded and checked against the release's `SHA256SUMS.txt`, its
  files replace the old ones in the game folder, and **Restart now** starts the new version. Your
  `rom/`, `save/`, `korean/` and settings are not touched. The game folder must be writable (not
  `Program Files`). Linux needs `curl` or `wget`.
- Android: the new APK is downloaded and checked, then the system installer opens (allow installing
  from this app if Android asks). It installs over the old version and keeps your saves and the Korean
  patch.

Updates come only from this repository's releases, and a build made from source never updates itself.
**Settings** → **Gameplay** → **Updates** turns the check off (`auto_update = 0` in `settings.ini`).
Version v0.1.6 and earlier can't update themselves: download v1.0.0 once by hand.

### Graphics API

**Settings** → **Video** → **Graphics API** chooses what the game draws with, from the next start:

- **OpenGL** (the default): OpenGL 3.3 on Windows and Linux, OpenGL ES 3.0 on Android.
- **Vulkan**: Windows, Linux and Android 7.0+ with a Vulkan driver.
- **Direct3D 12**: Windows 10 and later.

All three draw the same picture. Vulkan and Direct3D 12 leave the CPU more time (the driver does less work for each
thing drawn, and shaders are prepared on another thread), which helps most on phones and slower PCs; whether a GPU
is faster with one or the other depends on its driver, so try them. The shaders made for your GPU are kept in
`shader_cache/`: the first start with an API prepares them in the background as you play, later starts reuse them.

If the chosen API cannot start on your device, the game uses OpenGL and the menu says so. If the game crashes as it
starts with Vulkan or Direct3D 12 (a driver problem), it starts with OpenGL the next time and sets it in the menu.
`renderer` in `settings.ini` is the same choice (0 OpenGL, 1 Vulkan, 2 Direct3D 12), and `--renderer vulkan` (or
`opengl`, `d3d12`) uses an API for one run.

### Saving

The game saves your town as the gyroid's "save and continue" would, without asking:

- every minute while you are outdoors in your town and not doing anything special (not in a menu, a
  conversation or holding up an item; it waits for such a moment);
- when you go in or out of a building (at most once every 20 seconds);
- when you quit from the menu or close the window.

A new town is saved the same way: its first save creates the town file on the memory card, as a first save at the
gyroid would. On the first day the gyroid only saves once you have met a villager, so the first save is made right
after the talk with the first villager you meet, a few seconds later, wherever you are (outdoors or in a building);
a notice says it was saved for the first time. A player who moves into a town that already has a file is saved the
same way on their first day.

To save right away, press **Ctrl+S** (PC) or **SAVE** left of START on the touch gamepad. This also works in your
house's rooms and with the autosave turned off; a notice says it saved, or why it cannot yet (in a conversation or a
menu, in the middle of an action, on the island, or on the first day before you have met a villager).

You can still save at the gyroid as usual. Since every town is saved, quitting without saving no longer starts you
over: to start a new game, use **Settings** → **Gameplay** → **Town data** → **Delete town...**. It shows the town on
each memory card (with its players), asks once more, deletes the chosen one and restarts the game, which then
begins with a new game. This cannot be undone. Turning the game off without saving no longer loses your play, so
**Mr. Resetti does not come by default**. **Settings** → **Gameplay** → **Autosave** turns the autosave off, and
**Mr. Resetti** brings him back (`autosave = 0`, `autosave_every = 1` (minutes, 1-60) and `resetti = 1` in
`settings.ini`). While you visit another town in multiplayer, your own town's file gets your pockets, bells and
letters of the moment, as the train home would.

### Playing in Korean

The Korean translation is a separate fan patch, not part of this project.

1. Download the patch zip from [retrodb 한글패치 — 동물의 숲 e+](https://retrodb.info/krpatch/7428/%EB%8F%99%EB%AC%BC%EC%9D%98-%EC%88%B2-e)
   (patch versions 0.9.2 and 0.9.3 are tested).
2. Start the game with your original disc, open the pause menu (**Esc** on PC, **Back** on Android) →
   **Settings** → **Disc** → **Korean patch** → **Install...**.
3. Select the zip as downloaded (file picker on Windows and Android; on Linux, put the zip in the
   game folder's `patch/` folder first).
4. When it finishes (about a second), choose **Restart now**.

The patched image is written to `korean/`; your original image stays untouched. Use
**Play in Korean** on the same tab to switch back and forth.

### HD textures

> [!NOTE]
> **The HD texture pack is not made by the maintainer of this repository.** The Animal Crossing HD Texture
> Pack is the work of TechieAndroid and the contributors of the
> [Dolphin forums project](https://forums.dolphin-emu.org/Thread-animal-crossing-hd-texture-pack-version-25-october-11th-2026);
> all credit and rights for the textures belong to them. The pack is not included in this repository or its
> releases, and it is not redistributed here: the port only reads the file you download yourself from the
> original source. The pack's terms of use are those of its original source; questions and requests about the
> pack itself go to the original thread.

- Original source: [Dolphin forums — Animal Crossing HD Texture Pack](https://forums.dolphin-emu.org/Thread-animal-crossing-hd-texture-pack-version-25-october-11th-2026)
- Download: [Google Drive](https://drive.google.com/file/d/1Z8-pMx20xO8q3caiqy4NOrSNVT1veDSk/view) (v25, `ACHD V25.zip`, 160 MB)

**Installing**

1. Download the zip from the link above. There is no need to unpack it.
2. Pause menu → **Settings** → **HD textures** → **Install a texture pack (zip)...** and pick the zip as
   downloaded. It is copied to the game folder's `texture_pack/` (the app's folder on Android) and turned on.
   On Linux without a file picker, put the zip in `texture_pack/` yourself and press **Read the folder again**.
3. Textures change as you play, no restart needed.

**Good to know**

- It works with the Korean patch: the patch's own textures (logo, name plates, Hangul font and text) are never
  replaced. In Korean mode the pack's textures with text in other languages (`Localisation/`) are not used either.
- The pack is made for the North American Animal Crossing. Textures that e+ has in exactly the same form are
  replaced; textures only e+ has, or with different text, stay as they are.
- Textures load the first time they are drawn, so in a place you visit for the first time the original texture may
  show for a moment before the HD one.
- It takes a lot of memory. Most phones cannot use the pack's compressed format (BC7) directly and get the textures
  decoded, so on Android **Size** is **Half** by default. If the game gets slow or closes, keep **Size: Half** or turn
  the pack off (size and loading mode take effect after a restart).
- The **HD textures** switch turns it off and on at once; installed packs are removed with **Delete** on the same
  page. The zip needs its 160 MB of storage.
- Details: [docs/texture_pack.md](docs/texture_pack.md)

### Multiplayer

Up to 8 players in one town, connected directly to each other's games without any server. One player (the
host) opens a room in their town; the others (guests) take the train from their own town's station to visit.
How it works: [docs/multiplayer.md](docs/multiplayer.md).

**Hosting**

1. Save in your town once before opening a room: the game needs the town file to send to guests.
2. Pause menu → **Multiplayer**: set the room name, public or not, a password (optional) and the number of
   players (2–8), then **Open the room**.
3. A public room shows in the room list on the same network and over the internet. For a private room, give
   your friends the **Room code** shown on the page (with a Copy button): private rooms are joined with the
   code only.
4. **Send out** in the player list removes a guest, who cannot come back into that room. **Close the room**
   makes everyone leave.

**Joining**

1. Pause menu → **Multiplayer**: pick a room in the list and **Join**, or enter the code you were given (or an
   address such as `IP:47070`) under **Room code** and **Join**. Enter the password if the room has one.
2. Wait until "Receiving the host's town..." is done.
3. At **your own town's train station**, talk to the station master and ask for a trip. The game saves your
   town and takes you to the host's town by train.
4. To come home, take the train at the host's station as usual. What you got there comes home with you.

In the same town you see each other with name tags, and chat lines show over heads for a few seconds. The items
outside change the same way for everyone: dropping, picking up, placing, digging and filling holes, burying,
planting, shaking fruit down, chopping trees, pulling weeds. The name others see is set under **Your name**
(empty = your character's name).

**Chat**: while in a room, press **T** on PC, or the **speech bubble button** at the top of the screen on Android, to
open the chat box. Type and press **Enter** (**Send** on Android) to send, **Esc** (**Close**, or a tap outside the
box, on Android) to close it. The game does not stop while the box is open, but your character stands still. New
lines show at the top of the screen for a few seconds, and on Android the button gets a red dot while there are
unread ones. The key can be changed in Settings → Controls → **Chat (multiplayer)** (a gamepad button too). On PC,
Hangul is typed with your computer's Korean input method (Han/Eng key).

**Things to keep in mind**

- **Everyone should use the same version** of the game; with different versions joining may fail.
- Guests also need **a saved town of their own**, with **a different name** from the host's town, and their
  character must have **finished Tom Nook's part-time job**: before that the station master says no train will come
  for a while (the original game's rule).
- Only the host's town is shared.
- **Villagers follow the host's.** Villagers wandering near the host walk or run to the same places on guests'
  screens, and villagers away from the host are in the same acres too. Talking to or greeting a villager is still your
  own: during a conversation that villager stands still on your screen and follows again afterwards. They go home,
  come out and sleep when the host's do. **Inside a villager's house** the villager walks about the room where the
  host's does when the host is in the same house (and comes in when the host's does). The island villager, insects
  and fish move on each machine by themselves.
- **The host's furniture is shared live.** When the host places, moves, turns or removes furniture, or changes the
  wallpaper or carpet, guests see it at once. A guest standing in that room sees the furniture rebuilt when it is not
  in a menu or a conversation and is standing or walking. Lamps, TVs and radios on or off, the record in a stereo,
  what storage furniture holds and the gyroids' steps follow the host's too. As in the original game, nobody can move
  furniture in someone else's house.
- **Custom designs show as drawn.** A design worn as clothing shows to everyone as its owner drew it (changed
  designs too), and the designs of the host's houses (wallpaper, carpet, the house door) show to guests.
- **Tools in hand show.** Others see the net, axe, rod, shovel, umbrella (custom-design umbrellas as drawn),
  balloon, pinwheel, fan, flower or party popper in your hand, with your arms holding it and the net or rod moving as
  you swing or cast, and the item you hold up after picking something up or digging it up.
- **Also shared, following the host**: what each player is doing (picking up, shaking a tree, fishing... with the
  same animation), the weather, the town tune, the museum, the noticeboard, the police station's lost and found,
  Nook's shop, the Able Sisters' display, the post office, the turnip prices, the visitors' events and the snowmen. A
  guest's change at one of these places (a post on the board, a lost item taken, something bought or sold, the town
  tune) goes to the host and from the host to everyone. Also the outside of the houses (size, roof colour, basement,
  the mailbox's flag), the gyroid at a house's door (what a guest buys from it goes into the guest's pockets, and the
  gyroid keeps the bells for the house's owner, on the host's side too), the villagers' own data (their clothes and
  umbrellas, moving in or out), the extra bridge, the lighthouse, the fishing tourney, Blanca, the town's assessment
  and the other town events.
- A guest's own town is saved with the guest's pockets and bells while visiting (see [Saving](#saving)).
- Two players taking the same item at the same moment: the first one gets it, and the other's pocket gives it back.
- While a guest is in the host's town, the guest's game clock follows the host's.
- The room ends when the host closes it or quits. A guest already in the host's town stays there and takes the
  train home.
- It is still experimental: back up saves you care about first (copy the game folder's `save/`).
- **Privacy**: the room code contains the host's internet IP address, and a public room's address is published in
  the BitTorrent DHT. To play only with people you know, use a private room with a password and give the code only
  to people you trust.

**Playing across different networks (internet)**

- On the same Wi-Fi or router it works at once.
- Across networks, UPnP turned on in the host's router works best: the game asks the router to open UDP port
  47070 and the multiplayer page shows "(router port open)". Without UPnP, forward UDP 47070 to the host's device
  in the router's settings. Even with no port open, the game tries to connect by opening a path from both sides
  (hole punching).
- If the page shows "(the router is behind another router: its open port is not used)", there is another router
  in front of yours (such as the carrier's modem-router). Forward the port on that outer router or put it in
  bridge mode.
- Joining over the internet can take about half a minute while each side finds the other.
- Public Wi-Fi (cafés, schools, offices), guest Wi-Fi networks, mobile data and hotspots may block it or fail to
  connect; with both sides on mobile data it most likely will not connect. Host from a home connection.
- On Windows, allow the game if a firewall prompt appears when you first open or join a room, for **both private
  and public networks** (Windows often counts a home connection as public, and its prompt then also adds a rule
  blocking the game there). If guests from other networks cannot get into a room on your PC, press **Allow in
  Windows Firewall** on the multiplayer page while hosting: Windows asks for an administrator's consent, then the
  game's old rules are replaced by one allowing it on every network.
- On Android, keep the game on screen while playing: switching apps or turning the screen off can drop the
  connection.
- The port can be changed with `mp_port` in `settings.ini` (default 47070).
- It has not been tried much between real, different networks yet. If it does not work, please open an
  [issue](https://github.com/cailet0422/afe-port/issues) with the "Internet address" line from the multiplayer page.

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
| Town map (open / close) | M |
| Chat (in multiplayer) | T |
| Save now | Ctrl+S |
| Pause menu | Esc |

In the menus, use the mouse (click, wheel), the arrow keys with Enter and Esc, or a gamepad (D-pad, A,
B). Change the controls under pause menu → **Settings** → **Controls**: click a cell and press the key
or button; gamepads can be rebound there too. On Korean text-entry screens you can type Hangul directly (toggle with the 한/영 key, Right Alt or
Shift+Space). While typing, **Shift+Enter** (or Ctrl+Enter) finishes the text, as Start does. Enter starts a new
line; where there is no room for one it goes to the next line, in a letter on to the closing, and in the closing
it finishes too. If Start is on a key that types nothing, such as Home, that key finishes the text as well.

### Touch controls

Pause menu → **Settings** → **Gameplay** → **Touch controls** (`touch_controls` in `settings.ini`; the on-screen
pad is the default on Android):

- **On-screen pad**: a GameCube pad drawn over the game. **Edit pad layout...** under it shows the pad: drag a
  button, the D-pad or the stick to move it, pick one and make it bigger or smaller (50-200%); **Reset all** puts
  everything back.
- **Full touch**: no pad; you play with your fingers on the world.
  - **Tap** the ground to walk there; keep a finger **held** to run to where it is; **drag** to run that way.
  - **Tap a villager** to walk up and talk; while a message is up, a tap goes on; tap a choice to pick it.
  - **Tap a thing** to walk up and use it: items, flowers and weeds are picked up, trees shaken (or chopped with the
    axe), signs, mailboxes and furniture used; with the shovel a tap on the ground digs there (or fills the hole);
    with the net or the rod a tap nearby swings or casts that way. A tap on yourself is A.
  - Icons at the top: the pause menu, save and the gamepad (top left), the map and the pockets (top right). In the
    pockets, tap a slot for its commands (or to put down what the hand holds), tap a command, and **drag an item** to
    another slot or onto yourself to wear or hold it; in the map, tap an acre. The X closes them.
  - **Tools**: the strip in the middle shows the tool in hand with the ones before and after it in your pockets;
    **swipe** it left or right (or tap a side) to take another one out.
  - **Furniture mode** (the chair icon, in your own house): tap a piece to pick it, drag it to move it (green where it
    fits), turn it with the arrows, put it in your pockets with the satchel, or drag a piece from the pockets at the
    bottom onto the floor. The check mark ends the mode.
  - Everything else that wants buttons (other menus, the NES games, the title) brings the GameCube pad up by itself;
    the gamepad icon shows it at any time.

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

Each document is also available in Korean (`*.ko.md`).

| Document | Contents |
|---|---|
| [docs/README.md](docs/README.md) | Architecture, build and run instructions for every platform, milestones, known issues |
| [docs/android.md](docs/android.md) | Android build, signing, touch controls |
| [docs/64bit.md](docs/64bit.md) | Porting the decompilation to 64-bit |
| [docs/korean_patch.md](docs/korean_patch.md) | Korean patch format and how the port supports it |
| [docs/texture_pack.md](docs/texture_pack.md) | HD texture packs: installing, loading, the Korean patch |
| [docs/multiplayer.md](docs/multiplayer.md) | Multiplayer without a server: rooms, the internet (DHT, STUN, UPnP), the shared town |
| [docs/dev_setup.md](docs/dev_setup.md) | Development environment, test scripts, save tools |

## Repository layout

| Path | Contents |
|---|---|
| `pc/` | Platform layer: SDL2, OpenGL / GLES renderer, audio, disc reading, the menus (`pc/src/gui`, Dear ImGui), HD texture packs, multiplayer (`pc/src/mp`), Korean patch installer, Korean input method. Also the Android project (`pc/android`), generators and test tools (`pc/tools`), unit tests (`pc/tests`) |
| `decomp/` | The [afe-decomp](https://github.com/cailet0422/afe-decomp) submodule: the game code and its `#ifdef TARGET_PC` hooks |
| `docs/` | Developer documentation (English, with Korean versions in `*.ko.md`) |

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
- The menus use [Dear ImGui](https://github.com/ocornut/imgui) (MIT) and a subset of NanumGothic by NHN
  (SIL Open Font License 1.1, renamed "AFE Menu Gothic" as the license asks; `pc/res/gui_font_OFL.txt`).
  HD textures are decoded with [bcdec](https://github.com/iOrange/bcdec) (MIT) and
  [stb_image](https://github.com/nothings/stb) (public domain).
- The Animal Crossing HD Texture Pack is the work of TechieAndroid and its contributors
  ([Dolphin forums](https://forums.dolphin-emu.org/Thread-animal-crossing-hd-texture-pack-version-25-october-11th-2026)).
  The maintainer of this repository is not its original author and did not take part in making it; the pack is
  not included in this repository or its releases and is not redistributed here. The port only reads a pack you
  download yourself.
- The Korean fan patch is the work of an independent author (역전맞고). This project and its
  maintainers did not take part in making it, and no patch files, translations or fonts are included
  here. Questions about the patch itself should go to its distribution page.
- Animal Crossing and Dōbutsu no Mori are trademarks of Nintendo. This is an unofficial fan project,
  not affiliated with or endorsed by Nintendo.
