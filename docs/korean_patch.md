**English** · [한국어](korean_patch.ko.md)

# Korean patch (역전맞고, v0.9.2/v0.9.3) compatibility notes

Instead of making its own translation, the PC port aims to run in Korean by reading a Korean-patched disc
image that the user patched themselves. The patch's data, translated text and code are not included in the
repository. This document only records the formats the PC port needs to interpret that data.

## What the patch changes

- Disc: `main.dol` stays as is. The patch changes `foresta/forestd/foresti/foresto.rel`, `forest_msg.arc`
  (msg.bin), `forest_1st_script.arc` (letters, choices, strings), `forest_2nd.arc` (villager names), the
  GBA-link archives and `boot.bin`.
- `forestd/foresti/foresto.rel`: size and layout stay the same; only the data changes in place
  (`FONT_tex_font1`, item and furniture name plates, the title logo, various UI text textures, etc.).
- `foresta.rel`: the existing sections keep their sizes, and two executable sections are added (section 7,
  about 72KB; section 8, about 7KB). In 88 existing functions the first instruction (among other places) is
  replaced with a branch to the new code, and some string-table pointers are redirected to Korean strings in
  the new sections. Strings, catalog sort tables, width tables etc. in `.data` also change in place.

## Character encoding

- 1-byte characters keep the original character table. The newline `0xCD` and control codes `0x80 len ...` are
  unchanged as well.
- One Hangul character is 2 bytes:
  - Lead byte: `0x00–0x1F`, `0x86–0x8F`, `0x91–0x96` (48 values, indexed 0–47 in this order)
  - Second byte: `0xCE–0xFF` (50 values)
  - Glyph number `g = lead_index × 50 + (second − 0xCE)`, valid range 0–2389
- If `g < 2350`, it is the `g`-th character of the 2,350 KS X 1001 precomposed Hangul syllables laid out in
  Unicode order. (The 2,350 KS X 1001 syllables are in the same order as in Unicode, EUC-KR `0xB0A1–0xC8FE`.)
  2350–2368 are the 19 initial consonants (ㄱ ㄲ ㄴ ㄷ ㄸ ㄹ ㅁ ㅂ ㅃ ㅅ ㅆ ㅇ ㅈ ㅉ ㅊ ㅋ ㅌ ㅍ ㅎ), 2369–2389 the 21 medial
  vowels (ㅏ … ㅣ).
- 1-byte characters: the patched font puts Latin letters and digits at their ASCII positions. Most symbols are
  the same as in ASCII, but `~`=0x2A, `+`=0x3D, `\`=0x60, `;`=0x7B, `*`=0x7C, `=`=0x7D, `/`=0x7E
  (`ko_charset_ascii`). In Korean mode Latin text is fixed-width (12).
- The 3-byte sequence `0x96, ≥0xF6, xx` is a dictionary code. Its number is
  `e = b1×255 + b2 − (b2≥0x80 ? 1 : 0) − 0xF6×255` (0–1194); given the u16 offset `o` at `0xD340 + 16 + 2e` in
  section 7, the entry is at `0xD340 + o`, with the length byte at `+1010` and the contents from `+1011`.
  These are mostly long item names that do not fit in the field (10 bytes) (e.g. entry 0, "오렌지편지지" (orange
  stationery)).
- Some of the 1-byte values `0x97–0xC6` are frequently used Hangul syllables (e.g. `ac c0 a8` = "마을로" ("to the
  town")). The u16 table at section 7 `0xA0DE` gives the syllable number + 1 for each 1-byte character (for
  the final-consonant check).

## Glyphs

- In `msg.bin` (forest_msg.arc), starting `0x332780` bytes after the start of the message resource, 2,390
  glyphs of 192 bytes each follow one another. In game code: `Msg_rom_start + 0x332780 + g × 192`.
- A glyph is 2bpp, 12 8×8 tiles (3 across × 4 down) = 24×32 pixels. 4 pixels per byte (high bits first); the
  values 0–3 are expanded to I4 intensities 0, 5, 10, 15.
- The original game's font bank texture is 384×512 I4 (16×16 cells of 24×32). The patch uses the unused second
  bank (`FONT_tex_font2`) as a 255-cell cache: it loads Hangul into those cells and then draws them as bank-2
  characters.

## Character width

- The original e+ font is fixed-width (the table in `mFont_Get_FontOffset` is all 0). The patch puts `6` for
  space, comma and period and `12` for `0xCE–0xFF` into this table, and applies the table regardless of `cut`.

## Functions the patch touches (code)

- Character size, width, drawing: `mFont_CodeSize_get`, `mFontW_CodeSize_get`, `mFont_GetStringWidth(2F)`,
  `mFont_SetLineStrings_AndSpace_new`, `mFontChar_gppDraw`, `mFontCharW_gppDraw`, `mFont_GetCodeWidth(F)B`
- Message assembly (PC implementation: `pc_ko_expand`, `pc_ko_fix_josa`)
  - `mMsg_Set_free_str`/`mMsg_Set_item_str`/`mHandbill_Set_free_str`: expand dictionary codes to at most 66
    bytes, then pass the result to the original function. If only spaces follow the last code, the string ends
    there.
  - `mMsg_CopyString`: decides the final consonant from the last sound of the copied string (Hangul syllables,
    1-byte Hangul, and digits read as 영·일·이…·십·백·천·만 (Sino-Korean numerals: zero, one, two … ten, hundred,
    thousand, ten thousand); Latin letters etc. are unknown → left as is), and rewrites the particle right
    after it (skipping tags), 은/는 (topic), 을/를 (object), 이/가 (subject), 과/와 ("and") or 아/야 (vocative), into
    the form from the table at section 7 `0xA0CA`. If a Hangul syllable follows the particle, it is taken to
    be a word and left alone; for 아/야, when there is a final consonant, the particle is changed only if a
    comma (or 0xC7) follows it.
  - Name color and animalese: see "UI strings, name color, animalese" below. The choice hook is for the
    patch's own aspect-ratio menu (not applicable).
- Catchphrase (PC implementation: `pc_ko_word_ending`): expands the result of `mNpc_GetWordEnding(_forTag)` to
  16 bytes. If it is `0xFE` + 6 bytes, it holds four 12-bit values (below 2400 = glyph, 2400–2655 = 1-byte
  character `v−2400`); if it is a dictionary code, the dictionary entry; otherwise it is used as is. The
  length in `mMsg_CopyTail(Truth)` also goes 4→16. These 7 bytes go into `catchphrase` of `Animal_c` (4 bytes,
  0x595) and the 3 padding bytes after it (0x599–0x59B), so they are also kept as is in the save file.
- Line check: message board, diary and bulletin board `*_strLineCheck` count the width of each slot per
  character (Hangul second byte 0, space/comma/period 6, newline 0, everything else 12). PC implementation:
  `pc_ko_line_char_width`.
- Input method: `mED_*` (name and letter entry), `mLE_*`
  - In the edit buffer a Hangul character takes 2 slots (2 bytes in a u8 buffer; in a u16 buffer, one byte in
    each of 2 slots). When a flag in a static variable is set, the patch's `mED_output_code` inserts the lead
    and second byte at once and moves the cursor by 2.
  - `mED_get_col_line_width`/`mED_set_idxcol_inLineWidth`/`mED_get_single_line_width` count by character, with
    width 6 for space, comma and period and 12 for everything else (the lead-byte check uses the 256-byte
    table in section 7).
  - `mED_set_dl` is replaced by code in section 8 that draws the patch's own input UI (it reuses the original
    `lat_sousa_*` textures, so drawing with the original code comes out garbled).
  - The letter footer (`mBD_set_writing_footer`) has no hook and is right-aligned at 12 pixels per slot, while
    the cursor (`mBD_set_cursol`) is positioned by character width, so the two drift apart when there is
    Hangul. In Korean mode the PC port aligns it by character width.
  - Name-entry editor (`m_ledit_ovl.c`, PC implementation:
    `pc_ko_ledit_buf`/`pc_ko_ledit_match`/`ko_charset_pack_ending`, `TARGET_PC` in afe-decomp): the
    catchphrase (EPHRASE), the reply to Mr. Resetti (RESET) and the K.K. Slider song request (REQUEST) are
    entered into new buffers at section 7 `0xBD68` (+0 8 bytes, +0x18 18 bytes, +0x30 18 bytes). Their lengths
    in `mLE_win_data` in `.data` (52 bytes per entry, `edit_max` at +0x20) were changed 4→8, 10→18, 10→18 (the
    PC port reads the values from the disc).
    - `mLE_ledit_ovl_init+0xdc`: the `mem_clear(str, max, ' ')` call is redirected to `s7+0x59c` — if the type
      (`data0`) is 2, 3 or 4, it switches to the new buffer, clears it and passes that buffer to the editor.
    - `mLE_move_Wait+0xdc` (catchphrase, `s7+0x1424`): instead of `mem_copy(data2, edit_buffer, 4)`, if the 8
      bytes minus trailing spaces are 4 bytes or shorter, it takes the 4 bytes as is + three 0 bytes;
      otherwise, from the front, it turns 2-byte Hangul (lead-byte table `s7+0x6b7c`, second byte 0xCE or
      above) into glyph numbers and other bytes into 2400 + code, makes four 12-bit values (unused values
      0xFFF), and writes them as `0xFE` + 6 bytes to `data2` (the villager's `catchphrase`).
    - `mLE_move_Wait+0x7c` (song request, `s7+0x12cc`): instead of `mem_cmp(name, input, 10)` against the song
      name, it expands the name (10 bytes) if it is a dictionary code (at most 24 bytes, `s7+0xee4`) and
      compares the two while skipping all spaces on both sides. It matches whether or not the words are
      separated by spaces.
    - `aMJN3_check_word` (`s7+0x15ac`): compares Mr. Resetti's phrase (`mString`, 10 bytes) with the reply
      buffer the same way; on a match the result is GOOD, otherwise it copies the first 10 bytes of the buffer
      to `actor->reply` and runs the original banned-word check (`aMJN3_check_ng_word`).
    - `mED_all_space_check` is unchanged, so for the catchphrase it compares `data2` (the packed value) with
      the input buffer (same as the patch).
- Fixed-length constants and moved strings: comparing the original and patched `.text`, 32 instructions apart
  from the hooks are changed directly, mostly string copy lengths (2→4, 5→6, etc.). The patch places those
  strings themselves anew in section 7 and redirects their relocations there (130 places by
  `reldis.py retarget patched.rel original.rel`: `.text` references and pointers in `.data` tables).
  - PC implementation: `pc_ko_patched_str` (a string by section 7 offset), `pc_ko_data_ptr`/`pc_ko_data_u32`
    (pointers and lengths in the disc's `.data`; if a pointer points into `.data`, they return the disc copy,
    because strings lengthened in place exceed the size of the PC object).
  - Handled: the birthday letter and stationery footer ("드림" ("from")), the bulletin board town suffix ("마을"
    (town); the patch writes 4 bytes but counts 2, the PC port counts 4), the fishing record town ("쿠니치"),
    lost item names, the gyroid message table (no "それ" prefix), map labels (one line), the can't-place effect
    strings ("위엔 못 둬!"/"이미 있어!" ("can't put it on top!"/"already there!")).
  - The rest (input screen titles, bulletin board date, warning and photo menu text, greeting animalese, etc.)
    is handled too: see the section below.
- Other: aspect ratio (field of view, orthographic matrix, camera), RTC year limit 2032→2100, area-boundary
  running, the boot villager's kanji menu → aspect-ratio menu

## UI strings, name color, animalese (analysis and PC implementation)

Based on an instruction-by-instruction comparison of the original and patched `.text` (constants changed apart
from hooks) and on inspection with `reldis.py hooks|dis|retarget`. Offsets are for v0.9.2; "s7"/"s8" are
sections 7 and 8 added by the patch. The PC implementation is `TARGET_PC` in afe-decomp and `pc_ko.c`.

- Animalese `mMsg_sound_CodeVoice+0x2c` → `s7+0x6324`: originally `mMsg_sound_voice_get2(data[idx])` (per
  byte, the kana voice sound from the table `voice_array`); the patch passes `&data[idx]` and picks as follows
  (PC: `pc/src/ko/ko_voice.c`).
  - For 2-byte Hangul (lead table `s7+0x6B7C`, second byte 0xCE or above), it gets the syllable number
    (Unicode−0xAC00) of glyph `g` from the u16 table at `s7+0xA2DE` (for 1-byte Hangul, the value at
    `s7+0xA0DE` − 1) and splits it into initial, medial and final. The row from the initial (`s7+0xB53A`, 19
    entries) and the column from the medial (`s7+0xB54D`, 21 entries, low 4 bits) select from the kana voice
    table at `s7+0xA08E` (5 per row: a, i, u, e, o / 아·이·우·에·오). If the initial is ㅇ: 은·음·응 give ん (0x2D),
    medials marked 0x10 in the medial table (ㅑ·ㅕ·ㅛ·ㅠ) give や·ゆ·よ (0x24–0x26) depending on the column, and 0x20
    (ㅘ) gives わ (0x2C).
  - A standalone consonant (glyphs 2350–2368) gives the 'u' (우) of its row; a standalone vowel (2369–2389)
    gives its column in the a (아) row.
  - A second byte right after a lead byte gives -1 (no sound). Other 1-byte characters use the original table.
  - Since the same function does not voice the second byte separately, there is one sound per syllable.
    Checked with `VOICELOG` (below).
- Greeting animalese `voice$3952` (the sound a villager makes while waving): two entries of the table (lazy
  male, snooty female) are redirected to "어이"/"어머" ("hey"/"oh my", 4 bytes) in s7 and their length goes 3→4.
  The rest change in place in `.data`. This table calls `mMsg_sound_voice_get` per byte (no hook), so Hangul
  bytes are also voiced through the original table (same as the patch). The PC port uses the pointers and
  lengths from the disc.
- Name color `mMsg_Set_PlayerNameColor+0x34`, `mMsg_Set_PfColor+0x2c` → `s8+0x1824`: the character count of
  the color control code becomes the number of characters counted one at a time with `mFont_CodeSize_get` (one
  Hangul syllable = 1) instead of `mMsg_Get_Length_String` (bytes). Counting bytes colors the characters after
  the name as well.
- Choices `mMsg_Main_Cursol_SetSelectString_ControlCursol+0x114`, `mMsgTag_Choice_Option+0x74` → `s8+0xC70`:
  for choice numbers 0x7700–0x7705 it uses "4:3 (기본)" ("4:3 (default)"), "16:9" … "32:9" from s8, and for the
  rest the original `mChoice_Load_ChoseStringFromRom`. This is for the patch's own aspect-ratio menu (boot
  villager), so it does not apply to PC (on PC the aspect ratio is in the settings menu).
- Gyroid alarm message (`mHB_set_character+0x5c` → `s7+0x19F4`, `+0x138` → `s7+0x1AE8`): breaks lines by width
  instead of character count (16) (Hangul second byte 0, space/comma/period 6, everything else 12; past 192
  goes to the next line; a finished line wider than 180 counts as full), and puts the end marker at
  `x + width` instead of `x + char_count × 12` (12.0 in `.rodata` changed to 1.0). The `len == 16` check that
  tested whether a line was full is turned off.
- Input screen titles (strings moved to s7, only the length constants changed):
  - Time entry `mTI_set_character+0xbc` → `s7+0xD2FC`: the title "시간을 맞춰보자" ("let's set the time", 15 bytes)
    instead of `chr_table` (9 bytes).
  - Password `mPM_set_character`: "마을" (town) 2→4, "이름" (name) 3→4. Alarm `mAL_set_character`: "시간" (time)
    3→4, "분 뒤에" (minutes from now) 4→7, "분에" (at minute) 3→4.
  - Debug player select `player_select_draw_title`: "플레이어 선택" (player select) 10→13.
- Bulletin board date `mNT_set_day_strings_dl`: `day_str` (10 bytes) is extended in `.data` to 12 bytes of
  spaces, with year, month and day at 0, 6, 10 (originally 0, 5, 8). The digits land between the "/" marks of
  the background texture.
- Price `mTG_price_draw+0xb4`: characters per line 8→12. That line ("돈(벨):" ("money (Bells):")) fits within 8
  bytes, and the patch pads the last 4 bytes with spaces, so it looks the same; the PC port leaves it as is.
- K.K. Slider song request reply `aMKBC_clip_set_itemstr3`: `mMsg_Set_item_str` with the song request editor's
  18-byte buffer (`s7+0xBD98`) instead of `request_str` (10 bytes) in the save area. PC:
  `pc_ko_ledit_buf(mLE_TYPE_REQUEST)`. Verified on the actual screen (`make_days_script.py kk`, Saturday 20:00
  on a save that has finished Nook's part-time job): "있어 있어!" ("I've got one!") → "K.K.보사노바" (K.K. Bossa)
  typed into the "부탁해요" ("please") editor → "오케이! 그럼 들어 주세요. K.K.보사노바" ("OK! Then please listen. K.K. Bossa")
  → the performance (staff roll the first time) → "답례로 K.K.보사노바의 뮤직을 줄게요." ("In return I'll give you the music
  of K.K. Bossa.").
- 24 warnings (`wr_*_line`, pockets menu) and the photo menu (`fl_strb_*`, `fl_selb_*`): the string pointers
  in the line tables are redirected to s7 and the lengths changed (e.g. "편지를 더 못 가져!" ("can't carry any more
  letters!") 12→18). The line tables contain pointers and are therefore left out of the data overlay, so when
  a menu opens the PC port reads each line's pointer and length from the tables in the disc's `.data`
  (`wr_win_data`, `fl_strb_str_data`, `fl_selb_str_data`) and fixes up the PC tables (`pc_ko_data_target`,
  `pc_ko_redirect_strings`). The photo menu's SD failure messages were verified on the actual screen
  (`make_device_script.py film`): "사진을, SD카드에 저장?" ("save the photo to the SD card?") → 네 (yes) → "SD카드에 기록중 /
  잠시만요" → "SD카드 없음." ("no SD card.") → "SD카드가 들어 있는지, SD카드 어댑터 / 슬롯B에 연결하세요. / 준비되면 A버튼 누르세요" → "다시 이 사진을
  SD카드에 저장?", and after closing, "사진을 저장할 수 없었어요." ("the photo could not be saved."). The post office e-Card
  terminal ("게임보이 어드밴스가 제대로 연결되지 않았습니다.") and Mabel's (고순이) talk about the Advance ("게임보이 어드밴스가 제대로 연결이 안 된 것
  같아예.") also come out in Korean all the way through.
- RTC year limit: the patch changes 2030–2032 in `sChk_*`, `mTM_rtcTime_limit_check`, `lbRTC_GetNukegaraTime`,
  `lbRk_*` and `mTI_*` to 2099–2100, and replaces the lunar calendar tables (`l_lbRk_ConvertTable`,
  `l_lbRk_leapdays`) with tables in s7 extended to the year 2100. The PC port already extends this to 2100 in
  all modes under `TARGET_PC` (`GAME_YEAR_MAX`, `mTM_MAX_YEAR`, `mTI_MAX_YEAR`, its own lunar table). At the
  boundary (years 2099–2100) the two differ by about a year.

## v0.9.3 (2026-10-31)

The patch changelog lists two changes: the Japanese on the GBA screen when reading e-Cards with the Card
e-Reader+ (13 gyroid speech balloons and the logo) is now in Hangul, and a fix for the pose briefly changing
when crossing an area boundary by running or dashing with the area-boundary camera on. Comparing the v0.9.2
and v0.9.3 discs file by file (`main.dol`, `boot.bin`, `bi2.bin` identical), only two files changed:

- `foresta.rel`: section 8 (the patch's code) grows 0x1C00 → 0x2234 bytes, and 12 bytes of `.text` (section 1)
  change (hooks into the new code). Section 7 (the particle, animalese, lead-byte, dictionary tables etc. that
  the PC port reads by offset) and `.data` are identical down to the byte.
- `eappli.bin` in `forest_2nd.arc` (9440 → 10912 bytes): the e-Card reading program that is sent to the GBA
  and run there.

The PC port does not run the disc's code (section 8 is not read) and has no GBA ("no device"), so neither
change is visible on PC. Area-boundary running is not implemented separately on PC (the PC camera is the same
as in the original game). Verified: `pc/tools/apply_ko_patch.py` and the in-game installer (Settings → Disc →
Korean patch, the zip in `patch/`) both apply the v0.9.3 zip and produce an ISO matching the SHA-256 in the
manifest (`69dbd858…`), and the new-game opening (65 screenshots), a week at once (99 screenshots),
SD/GBA/e-Card (51 screenshots), New Year countdown (17 screenshots) and loading a real town GCI (13
screenshots) are the same as on the v0.9.2 disc, down to the screenshots and saves.

## Distribution format and in-game patching

- Distribution zip (`Doubutsu-no-Mori-e-plus-Korean-v0.9.2/`): `*.xdelta` (about 8MB), `manifest.json`,
  patching tools (`vcdiff.py`, `apply_gc_xdelta.py`, `패치적용.html` ("apply patch") for browsers), documentation.
  The patch tools are MIT-licensed.
- The `.xdelta` is VCDIFF (RFC 3284) produced by xdelta3: header `D6 C3 C4 00`, no secondary compression and
  no application-defined code table, 58 windows, and each window has an Adler-32 (window indicator `0x04`), an
  xdelta3 extension. Source windows are 4MB or smaller and source positions range over the whole disc
  (1,459,978,240 bytes); the result is a truncated ISO of 36,995,072 bytes.
- `manifest.json`: `patch_sha256`, `target_bytes`, `target_sha256`, `target_filename`, `source_*`
  (CRC-32/MD5/SHA-1/SHA-256 of the original; the original is the Japanese Rev 1 GAEJ01).
- The PC port's installer (`pc/src/pc_ko_patch.c`, pure code in `pc/src/ko/ko_patch.c` +
  `pc/tests/test_ko_patch.c`): for a zip it extracts the `.xdelta` and `manifest.json` inside (its own inflate
  implementation), reads the source window by window from the original image (CISO also works as is), opened
  separately from the disc the settings menu uses, writes to `korean/<target_filename>.part` and computes the
  SHA-256 along the way. If size and hash match the manifest, it renames the file and writes `korean = 1` and
  `ko_image` to `settings.ini`. It does not start if the disc header is not `GAEJ01` version 1, and it stops
  if a window's Adler-32 differs (a different revision or an already-patched image). Applying v0.9.2 takes a
  little over 0.2 seconds on Linux.
- On the next run, if `korean = 1`, `pc_disc_init` opens `ko_image` first (or the original image if it is
  missing or not a disc).

## PC port implementation plan

1. Detect the patched disc → Korean mode.
2. Character size, drawing, width: implemented in C following the formats above (`TARGET_PC`); glyphs are read
   from msg.bin on the disc.
3. REL data overwrite: read data objects from the RELs on the disc and overwrite the same objects in the PC
   build (textures, name plates, strings, width tables, etc.). The same mechanism also solves the runtime
   asset loader (M6).
4. Adjust message assembly, catchphrases, line checks and fixed-length constants for Korean mode.
5. Input method: physical-keyboard QWERTY Dubeolsik + 한/영 toggle (`pc/src/ko/ko_ime.c`), output in the patch
   encoding — implemented (see "Editor hookup" in [README.md](README.md)). An on-screen keyboard for gamepad
   and touch is implemented too (`pc_ko_editor.c`, `ko_osk.c`). Verified: name, town name, letters (header,
   body, footer), catchphrase, reply to Mr. Resetti, K.K. Slider song request
   (`make_opening_script.py --korean`, `make_ko_edit_script.py`). Writing on the bulletin board in front of
   the train station and in the diary is verified too (`make_ko_edit_script.py boards`). Sending a letter (to
   Pelly: "편지를 보내고 싶어" ("I want to send a letter")), the villager's reply that arrives with the 17:00 delivery
   the next day, and K.K. Slider's song request reply are also verified along the actual path
   (`make_days_script.py`). Villagers reply only to short Hangul letters (under 5 characters, a "bad letter"):
   the patch leaves `mNpc_CheckNormalMail_length`, which decides whether to reply, and its Japanese word table
   (`m_mail_check_ovl.c`) unchanged, so a letter with a few words written in Hangul is neither a "good letter"
   nor a "bad letter" and gets no reply (same on GameCube; the bytes of that function and table in foresta.rel
   are identical to the original). Remaining: the request in actual conversation to change the catchphrase
   (random) has only been tested with a debug command.

## REL data overlay scope

`gen_rel_overlay.py` only overwrites objects with a simple element type (byte, u16, u32, Vtx). Function-local
statics (such as `country_str$457`, which is `country_str.1` in the PC build) are included — the suffix `마을로`
("to the town") in town-name entry comes from here. For structs that start with text, only that part is
overwritten (`STRUCT_PREFIXES`: the 8 bytes of the tag menu text `mTG_tag_word_c`). Other pointer tables and
structs (`type:None`, `type:Gfx`, etc.) are left out. When one source file has function-local statics of the
same name with different types (`int category_table[]` and `mActor_name_t category_table[6]` in
`m_police_box.c`), the type of the declaration that matches the object size is used, and if that still does
not settle it, the object is not overwritten (the original value compiled into the build is used). Previously
the type of the last declaration was used, so this int table got byte-swapped in u16 units, and adding a lost
item (`mPB_force_set_keep_item`) crashed when the day changed.
