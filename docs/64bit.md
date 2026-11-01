**English** · [한국어](64bit.ko.md)

# 64-bit build (in progress)

Many recent Android devices (e.g. Pixel 7 and later, flagships from 2024 on) cannot run 32-bit apps, so an
arm64-v8a build is needed. The decomp code stores pointers in `u32` in many places, so the 32-bit build and the
matching build are left as they are, and the code is fixed inside `#ifdef TARGET_PC` to follow the pointer size.

## Build

Test on Linux x86_64 first (the same problems show up on arm64 unchanged, and they are easy to look at with gdb).

```
CC=clang CXX=clang++ cmake -G Ninja -S pc -B pc/build64 -DPC_ALLOW_64BIT=ON \
    -DSDL2_DIR=<64-bit SDL2>/lib/cmake/SDL2 -DPC_BUILD_TESTS=OFF
```

Without `PC_ALLOW_64BIT`, CMake still refuses a 64-bit configuration (not every scene has been checked yet).

## Approach

- **Types**: on LP64 (Linux and Android 64-bit) `long` is 8 bytes, so `u32`/`s32` (originally `unsigned long`/
  `long`) are defined as `int` only under `__LP64__` (`types.h`, `dolphin/types.h`, `PR/ultratypes.h`,
  `msl_types.h`, `pc_types.h`). For SDK functions declared with `long` that clash with `s32` declarations (CARD,
  SI, the `long` arguments of JKR*), the PC-side declarations are changed to `s32`.
- **Display lists**: the second word of `Gfx`, `w1`, is widened to `uintptr_t` (on 64-bit `Gfx` is 16 bytes).
  GBI macros that hold addresses (`gDma*p`, `gSPVertex`, `gImmp1/21`, `gSetImage`, `gSPLoadUcodeEx`, the
  TLUT/texture image macros of the Dolphin extensions) are redefined, with only the casts widened, in a `TARGET_PC`
  block at the end of `gbi.h`/`gbi_extensions.h`. Bitfield structs (`Gdma`, `Gsetimg2`, `Gmovemem` …) get
  `GBI_PC_W1_PAD` after the first word so the second word lands at the `w1` position (8 bytes in), and their
  address fields are `uintptr_t`. emu64's segment table, DL stack and `seg2k0` are also `uintptr_t`.
- **Pointers inside disc structs**: if a struct that is overlaid directly on disc data has a runtime pointer slot
  (0 on disc), its size cannot change. The `mData` of RARC file entries was changed to `PCPtr32`
  (`pc/include/pc_ptr32.h`; on 64-bit an index into a pointer table, on 32-bit the pointer itself).
- **ARAM, DMA, callbacks**: `ARStartDMA`/`ARQPostRequest`/`JKRAramPiece`/`JKRAramPcs`, which took main-memory
  addresses as `u32`, and the callbacks that passed pointers as `u32` (`ARQCallback`, the JKR command callbacks)
  were changed to `uintptr_t`.
- **PC layer**: `DVDFileInfo` is read through a struct with the same layout instead of fixed offsets, pointers
  inside `GXTexObj`/`GXTlutObj` are stored across two slots with memcpy, and the keys of the texture cache and
  the EFB capture are `uintptr_t`.
- **Pointer-to-integer casts**: handled in bulk (about 250 lines) with a tool that changes only the cast at the
  column a clang warning points to (`-Wpointer-to-int-cast` etc.; decomp sources normally build with `-w`) to
  `uintptr_t`/`intptr_t` and keeps the original line under `#else`. Where the result goes back into a `u32`
  variable, field or argument, `-Wshorten-64-to-32` finds it and that declaration is widened (the menu arguments
  `data3`/`param3`, the player's clothing texture address, the work slots of structure and insect actors, etc.).
- **`long`**: libultra's `Mtx_t` (`long[4][4]`) doubles in size on LP64, which breaks the matrices emu64 reads,
  so it is made `s32`.
- **Places that treat `Gfx` as a `u64`**: where emu64 copied and compared combiner (`G_SETCOMBINE*`),
  `G_TEXTURE` and triangle commands as a single `u64`, it would lose `w1` on 64-bit, so it copies and compares
  the whole `Gfx`.
- **Message queues**: `OSMesg` is pointer-sized, so receiving it into a 4-byte variable overwrites the stack.
  Receivers take it as an `OSMesg` and then narrow it to `u32` (audio thread, initial menu, NES sound events).
- **Audio**: on 64-bit, the bank tables of the N64-style audio (Nas) are not patched in place in the disc data but
  rebuilt as structs in host layout (`Nas_BankOfsToAddr64`). The address slots of audio commands (Acmd), pointer
  arguments such as callback and melody data (`Nap_SetPtr`), and the callback copy size in the DVD work queue were
  made pointer-sized. The wave and bank tables of the boot jingle (JAudio) store 32-bit pointers in place, so they
  are not registered on 64-bit (on 32-bit `pc_dsp.c` plays the sound).
- **Actor labels**: the `u32` labels that point to actors (the player's net, door, snowball, mosquito request
  and so on) and the `arg1`/`arg3` of structures were widened to `uintptr_t`/`intptr_t`. IDs that are only
  compared (sound effect position, shutter label) are left as they are.
- **Arena address**: the game stores addresses in the arena (24MB of main memory, `OSInit`) in 32-bit variables
  (the `ARENA` output in `boot.c`, the memory size check, etc.). So the arena is mapped below 4GB, and at
  `0x10000000` or above so it does not overlap N64 segment addresses, with `MAP_FIXED_NOREPLACE`. It used to
  search only `0x10000000`–`0x50000000` and fall back to `malloc` on failure; on 64-bit that address is above
  4GB, so the game crashed right at boot (reproduced on Linux by blocking that range with `LD_PRELOAD`). On 64-bit
  Android, ART's Java heap and boot image sit in the low 4GB (often from `0x12c00000`), so it now searches the
  whole `0x10000000`–`0xF0000000` range in 16MB steps. Kernels before 4.17 do not know this flag and treat the
  address only as a hint, so if a different address comes back it is discarded and the next address is tried.
  With the arena pushed to `0x52000000` or `0x80000000`, the screenshots and saves of the opening and of the
  week-in-one-run playthrough are byte-identical to those at `0x10000000`. It is logged as `[PC] arena at …`.
- **ARAM address**: ARAM is a `malloc`ed buffer, and the game is given offsets into it (`ARGetBaseAddress` is 0).
  To also accept `aram_base + offset`, the `ARStartDMA` of the imported PC port compared the offset with the low
  32 bits of the buffer address and subtracted them if the offset was in that range. On 64-bit, ASLR sometimes
  makes those low 32 bits small (`0x0023c010` etc.), and then every larger offset read from the wrong place.
  Audio banks got corrupted, and the game crashed in `Nas_BankOfsToAddr64` or hung right after start (about 1 run
  in 70). There are no such callers (in a week-long playthrough on both 32-bit and 64-bit that branch is never
  entered once), so on 64-bit it is removed, and on 32-bit it is used only when the buffer lies above the offset
  range. Reproduced with an `LD_PRELOAD` that places the ARAM buffer at `0x500000100000`. Before the fix it hung
  in the opening; after the fix the opening and the week-long playthrough are byte-identical to a normal run.
- **Fixed-size pools**: the NPC actor pool slot (0xBA0) is made 0xE00, since the NPC struct grows on 64-bit.
- **Raw-byte display lists**: 11 lists extracted as `u8 x[] = { #include "....inc" }` and called through
  `gsSPDisplayList` (the name-entry window's `nam_win_modeT`, the editor combiners `lat_sousa_*_combine`,
  calendar, diary, letters, etc.) are the disc's 8-byte commands as they are, so reading them as 64-bit `Gfx` (16
  bytes) mixes two commands at a time (the name-entry window and pencil, and the A/B button letters of the editor,
  came out as white blobs). `gen_data_fixups.py` marks them with `PC_FIX_GFX`; at boot, after byte-swapping, a
  host-layout copy is made on 64-bit and emu64's `G_DL` executes that copy (`pc_raw_dl_host`).
- **Display lists called by byte offset into a segment**: the 14 places that call into the middle of a `Gfx` list
  a segment points to, like `gsSPDisplayList(anime_4_txt + 0x18)` (map guide board `annai`, window borders
  `kan_win_waku4`/`kan_win_moji1`, choice tab `ctl_sentaku`, card letter `ctl_win_nuno8`), use offsets in units
  of GameCube 8-byte commands, so on 64-bit they read from the middle of a command (the block and lot numbers and
  the building names on the left panel of the map vanished entirely, with `予期しないコマンド` ("unexpected
  command") in the log). Under `TARGET_PC` they become `PC_GFX_OFS(0x18)` (number of 8-byte commands ×
  `sizeof(Gfx)`).
- **Slot sizes of Korean patch tables**: the tables in the patched foresta `.data` are in GameCube layout, so
  multiplying by the host `sizeof` as the slot size goes wrong on 64-bit. That is why the map building names
  (`mMP_label_data`, slot 0x1C, 40 bytes as a 64-bit struct) vanished or were garbled. Every place that reads the
  tables (`pc_ko_data_*`) writes the disc slot size as a number.

- **Unions written through one member and read through another**: the request to hand something from the pockets
  to an NPC (`mPlib_request_main_give_from_submenu`) writes the union's `give` and reads `give_from_submenu`. On
  32-bit the leading pointer+int pair and `xyz_t` are both 12 bytes, so they lined up, but on 64-bit the item read
  as 0 and the handed-over item was not drawn in the NPC's hand. It now writes through the member that is read.
- **Uninitialized buffers that end up in saves**: message board auto posts (`mNtc_set_auto_nwrite`) save a post
  struct on the stack that is only partly filled. It is invisible in the game but makes 32-bit and 64-bit saves
  differ, so the struct is zeroed before it is written.

## How to check

The default 32-bit build (gcc) and the 64-bit build (clang) give different screens even with the same
`--deterministic` script. The two compilers evaluate function arguments in different orders (gcc right to left,
clang left to right), so where two arguments call the RNG
(`aNPC_set_tex_anime(..., 32 + RANDOM(16), 1 + RANDOM_F(3))` etc.), the random numbers end up in different places,
and town layout, clothes and villager movement diverge. So 64-bit is compared **against a clang 32-bit build**.

```
sed 's/gcc)/clang)/; s/g++)/clang++)/' pc/cmake/Toolchain-multilib32.cmake > /tmp/Toolchain-clang32.cmake
cmake -S pc -B pc/build32clang -G Ninja -DCMAKE_TOOLCHAIN_FILE=/tmp/Toolchain-clang32.cmake -DPC_BUILD_TESTS=OFF \
      -DCMAKE_CXX_FLAGS="-m32 -mlong-double-64 -fno-aligned-new"
```

(Flags that avoid `-malign-double`, libstdc++'s `long double` tables and 32-bit clang's aligned `new`. Used only
for the comparison build.) Put each build folder's relmap next to its executable, run both, then compare the
screenshots with `compare -metric AE`.

Paths that need a save start from the first-day save that the gcc 32-bit build makes with
`make_newgame_script.py --korean --save` (in the clang build's town, that path does not get as far as saving).
Building interiors use `make_interior_script.py` (run it with `--days 1`, [dev_setup.md](dev_setup.md)), and saves
are compared byte for byte with `cmp` (the file holds two copies of the same save, starting at 0x10000 and
0x3E000).

## Current status

Results of comparing against clang 32-bit with the same `--deterministic` script on Linux x86_64 (counts are
screenshots):

| Path | Result |
|---|---|
| Original disc opening | 52 identical |
| Korean new game → free roam → villagers in the east area | 201 of 202 identical (1 differs by one frame in the player's arm pose, identical again from the next scene; looks like 32/64-bit libm floating-point error) |
| Korean editor, 6 scenarios (`make_ko_edit_script.py`: letter/save/reopen, catchphrase/save, song request, Mr. Resetti) | 134 identical |
| Building interiors (`make_interior_script.py all`: Tom Nook's shop, museum hall/Blathers' room/fish room, post office, Able Sisters, police station, player house, two villager houses) | 71 identical |
| Save path (`state`: placing furniture in the house, museum donation, buying at the shop, saving at a gyroid) | 44 identical, save file byte-identical |
| Loading (`state-check`: 64-bit loads a 32-bit save and 32-bit loads a 64-bit save, both walk the same rooms and save again) | 35 identical, re-saved files also identical. Furniture, bought items (SOLD OUT, wallet) and donations (museum display bits) persist |
| Title demo (original disc and Korean patch, `make_opening_script.py --idle 12000`) | 38 identical each |
| Other days and times (`town`: first night, 11/4 morning, snowy 12/21, 2/9 evening; `town-rain`: heavy rain) | 13 identical each |
| Famicom (`famicom`: placing it in the house and turning it on, Clu Clu Land) | 14 identical |
| Full part-time job (`make_job_script.py` plant/hello/furniture/letter/carpet/axe/notice, each step from the previous step's save) | 13/17/8/10/8/8/9 identical, save byte-identical at every step |
| Multiple days (`make_days_script.py` letter/reply/kk: a letter and the reply the next day, K.K. Slider on Saturday night) | 24/14/42 identical, saves identical |
| House expansion and island (`house`/`house-next`/`island-buy`/`island`/`rooms`: paying off the loan and expanding, island round trip, second floor and basement) | 16/16/21/59/12 identical, saves identical |
| No device (`make_device_script.py all`/`station`: photo menu SD, Card e-Reader terminal at the post office, GBA at Able Sisters, going out from the train station) | 51/25 identical, saves identical |
| Boot jingle (`--audio-dump`) | Same sound as 32-bit (below) |
| Famicom sound (`make_famicom_script.py --game 0·1·2·13` + `--audio-dump`, Clu Clu Land/Balloon Fight/Donkey Kong/Excitebike) | WAVs identical sample for sample for all 4 |
| Famicom high score (`--game 4`, 6000 frames of Pinball play) | 31 identical, the Famicom save holding the high score (016860) also identical |
| A week in one run (`make_days_script.py week`: five midnights via `CLOCK`, save/continue, reply, K.K. Slider) | 99 identical, saves identical |
| Events (`broker`/`harvest`/`countdown`: broker tent (Crazy Redd), Franklin at the Harvest Festival, New Year countdown) | 7/10/17 identical, saves identical |
| `static` palettes (Reset Center `FG 2140,1540 008D`, pink diary) | 17/4 identical |
| GLES 64-bit (`-DPC_GLES=ON`, Mesa GLES 3.2) | 71 interior and 38 title demo screenshots identical to desktop GL 64-bit |
| Korean-patch map (`MENU 5`, all 30 areas) and about 30 submenus (`MENU 1`–`31`, including inner menus such as the mailbox and address book) | 30 map screenshots and 4 per submenu identical to 32-bit; arm64 (qemu, GLES) also differs by at most 3 per channel |

The gcc 32-bit build also runs all of the paths above without crashing (its town differs, so its screens are not
compared). No 64-bit crashes.

- Fixed: the name-entry window and editor buttons (raw-byte display lists, above). The item menu words in the
  pockets (`mTG_tag_word_c`, a struct of which only the first 8 bytes are overwritten from disc) were left out of
  the REL overlay on 64-bit because the struct is larger, and came out in Japanese. Fish donated to the museum were
  not drawn in Blathers' hands (union, above). 32-bit and 64-bit saves differed by the garbage bytes of message
  board auto posts (above). The left panel of the Korean-patch map (block and lot numbers, building names) was
  empty (segment offsets and table slot sizes, above).
- Also present on 32-bit: in Tom Nook's shop's six-choice menu (after the part-time job is done), the gcc 32-bit
  build died from stack protection. The original code reads 72 bytes of choice strings into a 66-byte slot, so the
  last slot overwrote memory past the caller's array (`mChoice_Load_ChoseStringFromRom`; under `TARGET_PC` it
  reads into a 72-byte buffer and copies as much as fits).
- Famicom (`make_famicom_script.py`, from the gcc build's `--save` save): placing the furniture → launching and
  playing Clu Clu Land → returning with L+R+Z; all 14 screenshots are identical to clang 32-bit, and the sound
  during the NES part is also identical sample for sample. No crashes.
- Boot jingle: JAudio's WSYS (wave) and IBNK (instrument bank) tables work by rewriting disc bytes in place into
  32-bit pointers, so they cannot be used on 64-bit; on 64-bit only, the same content is rebuilt as host structs
  (`pc_jaudio_host_wsys`/`pc_jaudio_host_ibnk`, `pc_jaudio_bswap.c`; on 32-bit the original tables are returned
  as they are). In addition, places that hardcoded GameCube struct offsets as numbers (`seqsetup.c` track address,
  `jammain_2.c` track fields and child list, `oneshot_getchannel.c_inc` wave address, `waveread.c`) and the place
  that passed the DSP channel owner as a `u32` (`dspdriver.c`) were fixed under `TARGET_PC`. The jingle sound is
  identical to clang 32-bit.
- The Android APK bundles the arm64-v8a library as well ([android.md](android.md)). Running on arm64 has not been
  verified for lack of a device (verified on x86_64, which uses the same LP64 model). This round of checks did not
  change the Android build settings.
- Windows 64-bit (MinGW x86_64, LLP64) also builds: after fixing the boot jingle's `PTconvert` corrupting pointers
  in the table copies below 4GB, it gives the same screens and saves as the 32-bit Windows build under Wine
  (README.md, "Windows (64-bit)").
- Remaining:
  - Buying inside the tent and the other seasonal events (Fishing Tourney, fireworks, Halloween, etc.) have no
    path.
  - arm64 real device.
