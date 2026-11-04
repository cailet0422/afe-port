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

## Audit (v1.4.7)

A pass over everything that could differ on 64-bit, after the 키다리팀바 gyroid crash of v1.4.6 (JAudio's sequence
script call stack had room for 4 calls, that gyroid's melody calls 5 deep, and on 64-bit the extra entry
overwrote the stack depth; it has room for 8).

How:

- **Warnings**: every compile command of the 64-bit configuration run again with clang's pointer/integer size
  warnings (`-Wpointer-to-int-cast`, `-Wint-to-pointer-cast`, `-Wint-conversion`, `-Wvoid-pointer-to-int-cast`,
  `-Wpointer-to-enum-cast`, `-Wincompatible-pointer-types`, `-Wshorten-64-to-32`), for Linux (LP64) and Windows
  (LLP64, MinGW), and each one looked at.
- **AddressSanitizer and UBSan bounds checks**: a gcc 64-bit build with
  `-fsanitize=address,bounds -fsanitize-recover=address,bounds --param=asan-globals=0 -fno-omit-frame-pointer`
  (without `asan-globals=0`, the REL overlay map made after linking cannot find the assets) run through every test
  scenario below, the sound sweep, and a walk through all 30 blocks of a later town by day and by night (the
  first-day scenarios do not pass rivers, waterfalls, gyroids or mailboxes).
- **Sound**: every SE the game has (346), every BGM (384), the gyroid melodies and the instrument melodies, one
  after another with the input-script command `SOUND` and `--audio-dump`, on 32-bit and 64-bit: the WAVs are
  identical sample for sample.
- **Screens**: every scenario on the gcc 32-bit and the gcc 64-bit build (gcc evaluates arguments in the same order
  on both, so the random numbers match), comparing the screenshots pixel for pixel, the logged positions line for
  line and the saves byte for byte, and the same with clang, the Android build's compiler, 32-bit against 64-bit.
  The counts first given here (35 scenarios and 815 screenshots; 14 and 491 with clang) came from runs whose
  starting save was empty, so most of those scenarios stopped at the new-game prompt: see
  [Further checks (v1.4.10)](#further-checks-v1410) for the runs done again.
- **REL overlay map**: the disc objects each build overlays (`*.relmap`) are the same, except one table of
  animation pointers (`m_player_lib.c`) that only the 32-bit build pairs with a disc object of the same size. The
  copy keeps every pointer field, so nothing changes.
- **Multiplayer**: packets are written field by field. The only structs copied whole are save structs
  (`PersonalID_c`, `lbRTC_time_c`), which have the same layout on every build (`Save` is 140412 bytes on 32-bit and
  64-bit), so 32-bit and 64-bit players can be in one room.
- **Android**: the arm64-v8a libraries are aligned for 16 KB pages, and `char` is signed as on x86
  (`-fsigned-char`).
- **Windows x64** (Wine): placing the 키다리팀바 gyroid gives the same 5 screenshots as on Linux. The sound of the
  town walk differed from Linux's before the `.bss` fix below (from the first fade, 68 seconds in) and is identical
  to it, and to 32-bit, after.

Fixed (all under `TARGET_PC`; the matching build is unchanged):

- **emu64 combiners**: `replace_combine_to_tev` and `combine_manual` read a TEV combiner struct (12 bytes on 64-bit,
  its second word at `w1`'s place) as a whole `Gfx` (16 bytes), reading 4 bytes past it (ASan). In
  `replace_combine_to_tev` those stack bytes went into the top half of `w1`, which whole-`Gfx` comparisons of
  combiners then saw. Both now copy a whole `Gfx`.
- **Audio**: the sequence callback command checked `<= 4` on `seq_callbacks[4]` (index 4 is past the array: NULL
  on the GameCube, other fields read as a function pointer on 64-bit; no disc sequence uses it). `Bank_GetInstVmap`
  returned a pointer as an `int`, cut on 64-bit (no bank reaches it). The resampler saved its 8-entry history
  starting 4 back, reading 3 past the array. `zelda_GetFreeArena` passed `size_t` pointers to a function that
  writes `u32`s.
- **SE priorities (every PC build)**: `Sou_TrgStart` looks up `TRGPRIO[]` (128 entries) with the low byte of the SE
  id, and the game plays SEs whose low byte is 0x80 or more (0x8D, 0x97, 0x98, 0x9A, 0x9B, 0xA8, 0xB0, 0xCF,
  0x42E6, ...). The GameCube reads the init tables that follow the table in rodata; the PC builds read what their
  linker put there (on Linux, a `printf` format string; 0x8D got 82 instead of 128, 0x97 got 37 instead of 0).
  The priority decides which SE replaces which when the 6 SE slots are full, so with these SEs that went
  differently from the GameCube. The table now carries the DOL's 128 following bytes.
- **JAudio's `.bss` arrays (every desktop build)**: the game writes past `sou_lev_se` (`Sou_SeFadeoutRoutine` and
  `Sou_SeFadeinRoutine` set `sou_lev_se[8..14].volumeScale` when a level sound's fade ends) and reads past
  `sou_lev_ongen_data` (`Na_OngenPos` looks through 50 entries of 4 when a river sounds near a waterfall). On the
  GameCube both land in the arrays that follow in `.bss`. gcc pads each array to 32 bytes, so on Linux they hit
  other fields; the 64-bit Windows build orders these globals by name, so there the writes landed in other sound
  variables (an entry of the level-sound stack, and `sou_nobasu_count`, which stretches the vowels of the villagers'
  voices). On Android, clang keeps them together. The eight arrays are now one struct in the
  GameCube's order, checked at compile time, and the writes store the float's bytes field by field as the
  big-endian GameCube does (1.0 puts 0x3F in a sound slot's id there, not 0).
- **Photos saved to the SD card (every build)**: `JUTTiff::createIFD0` fills the strip offsets and sizes (4 bytes
  per strip) from a 2-byte local, reading past it on the stack (ASan). They are placeholders that
  `createMainImage` writes over, so they now come from a zeroed buffer; the file is the same.

Original code that reads past an array, the same way on every build (left as is):

- `Actor_ct` counts actors without a bank in `banks[-1]`, which is `current_scene_data` on 32-bit and 64-bit alike
  (the high half on the GameCube); it is not read again before the next scene sets it.
- `Player_actor_init_value` writes `item_shape_type[2]` and `[3]` of a 2-entry array, which are
  `item_animation_idx[0]` and `[1]` on every build.
- `mEv_check_schedule`/`mEv_check_status`/`mEv_check_run_today` take the address of `event_today[0xFF]` but test
  the index before reading.
- `decode_date` reads `dates[15]` of 8; the save layout is the same on every build.
- `Kabu_manager` subtracts a week from the stalk market's date before it was ever set (month 0), reading
  `days_month[][255]`; the year (0) already decides the comparison that follows.
- `aHNW_actor_ct` and `aMBX_actor_ct` (gyroids, mailboxes) read `banks[-1].ram_start` for actors without a bank:
  another field on each build, but the value is never used.
- `mBGMPsComp_main_remove` makes a pointer to `ps[-1]` when the list is empty, without reading it.
- `Nas_SubSeq` reads a sequence's table through a 2-entry array type (`dyn_tbl[0][97]`): bytes of the sequence data,
  the same on every build.
- With the save's weekly date still empty (0xFFFF), `decode_date` gets month 255 and reads `last_day[254]` and
  `weekday1st[255]`. Both ends of that event's dates come from the same value, so they come out equal and the event
  stays off.

Not fixed: SE ids the game never plays (0x351 and others between the defined ones) make the sequence player read
unrelated bank data and can crash or hang any build.

## Further checks (v1.4.10)

The save most scenarios start from (`make_newgame_script.py --save`) had been made with too few presses at the end:
the game never wrote it, and the runs of v1.4.7 that load it stayed at the "출발할까?" prompt of the train. The
script now presses through to the end of the save, and everything was run again on the save it leaves. The
scenarios were also checked for steps that miss what they are for (the logs' positions, jobs and loans, and the
screenshots of each).

- **gcc 32-bit against gcc 64-bit**: 36 scenarios, 845 screenshots, the logs and the saves identical (the saves
  after the notice fix below).
- **clang 32-bit against clang 64-bit** (the Android build's compiler): the same 36 scenarios and 845 screenshots
  identical.
- **The part-time job**: the test script's `hello` step missed a villager who is in his house at that hour in this
  town, so Nook never gave the next job, and the steps after it ran without doing their errands (the same on every
  build, so the comparison did not show it). The script now knows who is indoors, talks to the mayor again when he
  has walked on, goes round a table, and addresses the letter to the job's villager; the job runs to its end, Nook
  greeting the player as a shopkeeper: 76 screenshots on gcc and 75 on clang (whose town gives the errands to other
  villagers), all identical, and the saves identical at every step.
- **House expansion and the island** (`house-next` on days 4, 5 and 6, `island-buy`, `rooms`, `island`: the loan
  paid each time, the bigger house, the basement, the upper floor, buying the island, the rooms upstairs and
  downstairs, the boat to the island and back): 141 screenshots and the saves identical. The way back to Kapp'n on
  the island's pier was set for another town's island, so the player stayed on the island; the script now stands
  where the boat left the player.
- **The town through the year** (the walk through all 30 blocks on Christmas Eve, New Year's Day, in January snow,
  on Valentine's Day, in cherry-blossom time, in May, July, on the fireworks night, in September, at Halloween and
  on a rainy night): 330 screenshots identical.
- **Famicom**: 19 games, 304 screenshots and the sound of each identical.
- **HD texture pack** (ACHD V25, the interiors and the town): 84 screenshots identical (the pack's 18858 textures
  loaded; 65 of the 71 interior screenshots differ from those without it).
- **Windows x64** (Wine) against Linux 64-bit: the save path (furniture, the museum, the shop, saving),
  a letter and the house loan with the treasure notice: 84 screenshots and the saves identical, and the GBA files
  are found at boot.
- **Multiplayer**: a 32-bit host and a 64-bit guest: joining, the town sent to the guest (466944 bytes) and chat.
- **AddressSanitizer and UBSan** through all of it (50 runs): no memory errors. Out-of-range reads in the original
  code that these runs reach and the first did not, the same on every build:
  - `aFSN_actor_ct` (the balloon) reads `banks[-1].ram_start` for an actor without a bank and never uses it;
  - `mPr_GetMotherMailMonthlyNotSendNum` reads `monthly[2]` of 2 for September to December, which is the next byte
    of the struct (`_pad`) on every build, as on the GameCube;
  - `mEv_get_end_time` makes a pointer to `event_today[0xFF]` and tests the index before reading it;
  - the mail carrier (`aPMAN_set_move_idx`) reads `move_idx[-1]` when there is nothing to deliver, and then flies
    off without using it.
- **Valgrind** (memcheck, the 64-bit build): the save path, the house loan on the day of the treasure notice and
  the Korean letter: no use of uninitialised memory other than the harvest moon's year (below), and no uninitialised
  byte written into a save.
- **More warnings** (gcc's `-Wreturn-type`, `-Wimplicit-function-declaration`, `-Wuninitialized`, `-Warray-bounds`,
  `-Wformat` and others over every source): the functions that end without returning a value are unused or their
  callers ignore it, and the functions called without a declaration return `int` or nothing.
- **Mail and notices built on the stack**: every `Mail_c` filled in a local before it goes into the save is cleared
  first; one notice was not (below).

Fixed:

- **A villager's treasure notice** (every build): `mNtc_check_treasure` builds the notice in a post on the stack, and
  the handbill fills only part of its message; the other 14 bytes went into the save as whatever the stack held (a
  32-bit and a 64-bit build that played the same days wrote saves that differed in `noticeboard[5]`). Under
  `TARGET_PC` the post is zeroed first, as `mNtc_set_auto_nwrite` already does for the automatic notices.
- **Archive volume names on Windows and Android**: `JKRFileLoader::fetchVolumeName` lowercased the volume of a path
  with `_tolower`, which the C libraries define for upper-case letters only: MinGW's subtracts `'A' - 'a'` from any
  character and bionic's ORs in 0x20, which turns `_` into DEL. So `/agb_special/...` named no volume there, and at
  boot the GBA design editor's files were not found in `agb_special.arc` ("AGB data file not found"; there is no GBA
  to send them to on PC, so nothing showed). It uses `tolower` now, which leaves other characters alone.
- **The harvest moon's year** (Valgrind): for the years of its table, `lbRk_HarvestMoonDay` fills in the month and
  day only, and `update_event_rumor` copies the year too (an uninitialised stack value) into the date it steps back
  with `lbRTC_Sub_DD`, whose leap-year test reads it at every town load. The month before a September or October
  date never depends on the year, so the rumor's dates were right on every build; under `TARGET_PC` the year is set.

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
