**English** · [한국어](dev_setup.ko.md)

# PC port development environment (Linux container)

The steps for picking up work in a fresh container. Game data is not in the repository, so it has to be prepared
every time.

## 1. Packages

```sh
dpkg --add-architecture i386 && apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y gcc-multilib g++-multilib libsdl2-dev:i386 \
    libgl-dev:i386 libgl1-mesa-dri:i386 xvfb mesa-utils x11-apps x11-utils imagemagick xdotool gdb unifdef \
    binutils-powerpc-linux-gnu ninja-build cmake
```

The unit tests (Hangul input method, on-screen keyboard, character table, patch applier, memory card, GCI save
conversion) run without game data (same as CI; only the memory card and GCI conversion tests need the headers from
the `decomp/` submodule):

```sh
cmake -S pc -B pc/build-tests -G Ninja -DPC_TESTS_ONLY=ON && ninja -C pc/build-tests && ctest --test-dir pc/build-tests
```

## 2. Disc image

The user uploads the disc image (Japanese Rev 1, CISO, about 33MB) to the session directly. No download links go in
the repository or the docs. Files uploaded to a Claude Code session appear under `/root/.claude/uploads/<session>/`
with a prefix added to the name (for an `.iso`, copy it to `game.iso`; if it was uploaded as a 7z archive, run
`apt-get install -y 7zip`, then extract it with `7z x` in an empty folder and use the `.ciso` inside):

```sh
mkdir -p /tmp/disc && cp /root/.claude/uploads/*/*.ciso /tmp/disc/game.ciso
python3 pc/tools/gcdisc.py extract-orig /tmp/disc/game.ciso decomp/orig/GAEJ01_01
```

Do not commit the disc image or extracted files (`decomp/orig/*/*` and `pc/build*/` are ignored). If the submodule
is empty, run `git submodule update --init --recursive` first.

## 3. Matching build (asset extraction and verification)

```sh
cd decomp && python3 configure.py --version GAEJ01_01 && ninja
sha1sum build/GAEJ01_01/static.dol build/GAEJ01_01/*/*.rel && cd ..
```

The hashes must be exactly these (check every time the decomp source is changed):
`7574bec2…` static.dol, `a3b987c9…` foresta, `49fd5f04…` forestd, `98246b73…` foresti, `73ddcf0d…` foresto
(full values in [README.md](README.md)). For edited files, also check with `unifdef -UTARGET_PC -UBUGFIXES` that
they match the original text.

## 4. PC build and running

```sh
cmake -S pc -B pc/build32 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-multilib32.cmake
ninja -C pc/build32          # the first configure takes a few minutes generating foresti/foresto
mkdir -p pc/build32/bin/rom && cp /tmp/disc/game.ciso pc/build32/bin/rom/
(Xvfb :99 -screen 0 1280x720x24 &) ; export DISPLAY=:99 SDL_AUDIODRIVER=dummy
cd pc/build32/bin && ./DobutsuNoMoriEPlus --verbose
```

- `settings.ini` needs `msaa = 0` for Xvfb to find a GLX visual (edit it after the first run).
- To investigate crashes: `gdb -batch -ex run -ex bt --args ./DobutsuNoMoriEPlus --verbose`.
- Scripted input: `python3 pc/tools/make_opening_script.py 16000 > /tmp/opening.txt`, then
  `./DobutsuNoMoriEPlus --no-framelimit --input-script /tmp/opening.txt` → `shots/*.ppm`
  (combining them with `montage` makes them easy to look at). About 30–60fps on llvmpipe.
- Adding `--script-fast` renders only the frames that take screenshots, so it runs at about 400fps (the 6,000-frame
  opening in 16 seconds). The scene copies that menus keep in the background come out black.
- `--deterministic`: starts the game clock at noon (UTC) on 2026-11-01 and advances it only by frame count, and
  generates audio 1/60 s at a time per video frame instead of on a separate thread. Running the same script gives
  the same screens every time (town layout, clothes, villager positions, down to the screenshots). Combined with
  `--time H`, it is hour H of that day.
- New game → town: `python3 pc/tools/make_newgame_script.py --korean [steps...] > /tmp/ng.txt`, then
  `./DobutsuNoMoriEPlus --no-framelimit --script-fast --deterministic --input-script /tmp/ng.txt`
  (about a minute and a half). See the script's description for the step format. Free movement starts at about
  frame 29,000. When building a route, `--pos` prints for every screenshot the player and villager positions, the
  camera (mode, center, eye, angles), the blocks that contain the shop, museum, post office etc., and the positions
  of nearby building actors (`POS`, shown with `--verbose`; one block is 640),
  and `SEEK:E038:600` makes the player walk to a villager or a point (sidestepping when blocked).
  Screens the game only opens occasionally are opened with `MENU <name> [arg0] [arg1]` (`ALARMIN`, `TIMEIN`,
  `HBOARD`, `PASSWORDMAKE`, `PASSWORDCHK`, `NOTICE`, `WARNING`, `CALENDAR`, `FILM`, `MAP` …,
  `pc/include/pc_input_script.h`).
- Normal-speed check: running without `--no-framelimit` (with only `--script-fast` if needed) runs audio on its
  separate thread. Without `--deterministic` the town differs every time, so the `--save` route may not get as far
  as saving, but it serves to see whether the run reaches the end without crashes or hangs (drawing every frame on
  llvmpipe runs at around 30fps, so it takes about twice as long).
- Audio check: adding `--audio-dump file.wav` records the sound output (32kHz stereo 16-bit, before the volume
  setting is applied) as a WAV. It also works with `SDL_AUDIODRIVER=dummy`. With `--deterministic` audio is
  generated 1/60 s per frame, so the same script produces a WAV that is identical down to the sample (frame F is
  roughly sample F×32000/60). `python3 pc/tools/audio_stats.py file.wav --seg name:F1-F2 ... --gaps 200` shows, for
  each segment (in frames), the length, loudness (RMS/peak dBFS), silence ratio, longest silence, pops, clipping,
  DC, left/right correlation and strongest frequency; `--gaps` looks for dropouts between sounds, `--every <sec>`
  shows loudness over time, and `--compare other.wav` checks whether two dumps are identical.
- Findings from the audio checks (Korean disc, `--deterministic`): the first 0.7 seconds of the audio dump are the
  boot jingle (JAudio, `pc_dsp.c`); the peaks of the opening music average about −1 cent relative to A440 equal
  temperament. On the first-day save (during Nook's part-time job) the town music is `BGM_INTRO_FIND_SHOP`, which
  does not depend on the time; that is the original behavior, so the hourly music is played with `BGM <id>`
  (input-script debug command, numbers from `audio_defs.h`: 01–18 are hours 0–23). At normal speed, too,
  `--audio-dump` produces a dump of real-time length, and `audio_stats.py --gaps` shows dropouts.
- Famicom: running `python3 pc/tools/make_famicom_script.py > /tmp/fc.txt` (options `--game N`: the Famicom
  furniture 1DE8+4N, `--play F`) on a `--save` save receives the furniture, enters the house and places it
  (pockets → 배치하기 (Place)), faces it and presses A → "놀자!" ("Let's play!") → how-to-quit notice → START on
  the Clu Clu Land title → play → L+R+Z → back in the room, with `shots/fc_<n>_<step>.ppm` at every step (about 1
  minute). NES audio runs from roughly frame 5400 until quitting (`audio_stats.py --seg nes:5600-6700`). The
  furniture is placed in the tile in front of the player (up/right), so the player steps half a tile to the right
  and faces it from below (when touching it but not squarely in front of it, A becomes push/pull).
  `--game 0`–`14` are the disc's 15 games (0 Clu Clu Land, 1 Balloon Fight, 2 Donkey Kong … 13 Excitebike,
  14 Wario's Woods); 15–18 are empty furniture that cannot be obtained, and give "이 방향엔 못 놓아!" ("Can't place
  it facing this way!"). High scores are kept in `save/card_a/DobutsunomoriP_F_SAVE.sav`
  (0x40-byte header + 0x660 bytes per player; on quitting a game `nesinfo_tag_process3` copies the scores from WRAM).
  "WRAMのハイスコアが更新されました" (WRAM high score updated) means the in-game score was copied to the save data;
  "共通セーブ領域が壊れているのでセーブしません" (common save area is broken, not saving) means it was not saved (the
  bytes printed under `nesinfo_tag_process3` on quitting a game and "初期値" (initial value) are the tag's initial
  values, not the saved score). To score through actual play, use `--keys` (buttons to press in turn, default
  `SR,SD,SL,SU`), `--every` and `--hold`: for Pinball, `--game 4 --keys A,SL,A,SR --every 50 --hold 20 --play 6000`
  lets the ball score on its own and TOP reaches the 01xxxx (10,000-point) range. To change a saved score, edit
  the bytes of the player data and adjust the third byte so that the sum of those 0x660 bytes is 0 (mod 256) (the
  Balloon Fight TOP is 5 bytes from 0x4C of player 0, lowest digit first).
- `static` palettes (byte-order fixup at boot; README.md, "Architecture"): for the diary furniture, change
  `GIVE 1DE8` in `make_famicom_script.py` to `GIVE 3150` (pink diary, `FTR_IID_DIARY`) and place it in the room; it
  must be pink (light green without the fixup). For the Reset Center, on a `--save` save put
  `3000 FG 2140,1540 008D` (Mr. Resetti's hole) in the tile south of the spot in front of the house (2128,1488) and
  walk in with `3020 SD 30`; the player falls in (scene 52, about 1000 frames later). The walls, floor and chairs
  must be wood-colored (purple and green blotches without the fixup).
  During the build, `gen_data_fixups.py` prints "N data fixups, 6 of static blobs".
- Save/load check: `make_newgame_script.py --korean --save` greets the next-door villager (밀크, Milk) and saves at
  the gyroid (on the first day the gyroid refuses to save if the player has not met a single villager). Once
  `save/card_a/DobutsunomoriE_MURA` exists, run again with `make_opening_script.py 5200` to continue — Milk greets
  the player and play starts in front of the house.
  Loading a save that was quit without saving brings out Mr. Resetti (도루묵씨/리셋씨) (original behavior).
- Korean editor checks (Korean-patched disc, starting from a save made with `--save`):
  `python3 pc/tools/make_ko_edit_script.py <scenario> > /tmp/k.txt`, then
  `./DobutsuNoMoriEPlus --no-framelimit --script-fast --deterministic --verbose --input-script /tmp/k.txt`
  (1–3 minutes each, screenshots in `shots/k_<scenario>_<n>_<step>.ppm`).
  - `letter`: receives stationery (`GIVE 2000`) and writes a letter to Milk — the body with `TYPE`, the on-screen
    keyboard with the stick, A and `TOUCH`, long-line wrapping, deleting and inserting, and the header (before/after
    the name) and the closing with the D-pad. After confirming, it reopens the letter from the pockets with
    "다시쓰기" (Rewrite) and saves at the gyroid. Then `letter-check` loads the save and opens that letter again.
  - `ephrase`: changes Milk's catchphrase to "하나둘이" (`LEDIT 2 E038`; compressed to 7 bytes: `CATCH` shows
    `FE 8A 01 23 20 66 0B`), talks to Milk to see whether it appears at the end of the lines, then saves.
    `ephrase-check` loads the save and checks again.
  - `request`: enters "K.K.보사노바" (K.K. Bossa; dictionary codes on the disc), "아일랜드의 노래" ("Island song";
    with and without the space) and a song that does not exist into the song-request editor (`LEDIT 4`), and checks
    that `ITEM` prints 2A0A, 2A13, 2A13, 2A8C (none). Does not save.
  - `reset`: `RESETS 6` brings up Mr. Resetti's third visit; wrong answer (몰라, "Don't know") → he asks again → enter
    the phrase he demands, typed with spaces → "참말이제?!" ("For real?!") (accepted). The phrase differs per save
    ("정말죄송합니다" ("I'm really sorry") on the `--save` save).
  - `ui`: with the debug command `MENU`, opens in turn the time entry ("시간을 맞춰보자", "Let's set the time"), the
    two gyroid alarms ("분 뒤에"/"분에", "minutes from now"/"at minute"), the password screens ("마을"/"이름",
    "town"/"name"), the gyroid alarm message (with Hangul entered, to see whether lines break by width) and the
    bulletin board (date "2026/11/01").
  - `warning`: shows the 24 pockets-menu warnings (`MENU WARNING n`) in turn (long texts that the patch relocated).
  - `boards`: writes Hangul on the bulletin board and in the diary (calendar → today → write diary) and posts it,
    and opens the password check screen ("암호 입력", "Enter password") and the photo menu
    ("사진을, SD카드에 저장?", "Save the photo to the SD card?") (closes them with B; trying to save with A is
    `make_device_script.py film` below).
  - `voice`: turns on `VOICELOG 1` and talks to Milk. In the log, `[VOICE] msg 055C 1BDC U+C557 voice 1` is the
    message number, the bytes, the character and the chosen animalese voice number (index in the kana voice table;
    the second byte is not voiced).
  - Each scenario uses a fresh copy of the save made with `--save` (`letter` and `ephrase` save, so the `*-check`
    runs come after them).
- Missing-device checks (Korean-patched disc, save made with `--save`, `--days 1`):
  `python3 pc/tools/make_device_script.py <scenario> > /tmp/d.txt`, then run it as for the interior checks (about
  1 minute each, screenshots `shots/d_<scenario>_<n>_<step>.ppm`). None of them may hang; each must end with the
  game's "not present" message.
  - `film`: in the photo menu (`MENU FILM`), save to SD → "SD카드 없음." ("No SD card.") → adapter notice → save
    again? → once more → "사진을 저장할 수 없었어요." ("The photo could not be saved.").
  - `terminal`, `terminal-send`: at the post office Card-e terminal (60,220; inside x>90, 43, face west and press A),
    "카드e로 읽기" (Read with Card-e) / "리더 준비하기" (Prepare the reader) → "게임보이 어드밴스가 제대로 연결되지
    않았습니다." ("The Game Boy Advance is not connected properly."). Because of the message speed, choosing again at
    the same spot after one conversation had unstable timing, so the scenarios were split.
  - `tailors`: to Mabel (고순이), "어드밴스 얘기" ("About the Advance") → writes the design tool to the GBA →
    "제대로 연결이 안 된 것 같아예." ("Looks like it's not connected properly.").
  - `station`: runs `NOJOB` before the town loads (the station attendant checks the part-time job state when it is
    spawned), then tells the station attendant "외출할 거야" ("I'm going out") → SD check → player written to the
    memory card in slot B and the town to slot A → train → title. The save changes (`save/card_b/`).
  - `all`: `film`, `terminal`, `tailors`.
- Nook's part-time job (Korean-patched disc, from the first-day save made with `--save`, without `--days`):
  `python3 pc/tools/make_job_script.py <step> > /tmp/j.txt`, run it and copy `save/` for use in the next step (1–2
  minutes per step; each saves at the gyroid and returns to the title). `plant` (work uniform: pick it up from the
  pockets → drop it on the player / plant 10 flower seeds and saplings — on the z 1260 row southeast of the shop;
  the first two spots are rejected, so it plants again on the row below), `hello` (greets six villagers and the
  mayor D07F next to the shrine; villagers who are indoors at that time are visited in their houses), `furniture`
  (furniture to E08C), `letter` (letter on Tom Nook's stationery to 루시 (Lucy) → Pelly), `carpet` (E09D),
  `axe` (E09E), `notice` (posts on the bulletin board north of the houses, 2220,1580) → Tom Nook welcomes the player
  as the shopkeeper (job finished). The receiving villager is shown by `first job N progress P to XXXX` in `POS`.
  Conversations are waited on with `TALK`/`CHOOSE`/`TALKEND`.
- Multiple days (Korean-patched disc, save made with `--save`):
  `python3 pc/tools/make_days_script.py <scenario> > /tmp/n.txt`, then run as above (screenshots
  `shots/n_<scenario>_<n>_<step>.ppm`).
  - `letter` (`--days 1`): `JOBDONE` → "안녕" ("Hi") to Milk on stationery (`GIVE 2000`) → at the post office, to
    Pelly "편지를 보내고 싶어" ("I want to send a letter") → "네, 확실히 받았습니다." ("Yes, received.") → "또 다른
    편지도?" ("Another letter?") "이제 됐어" ("That's all") → save at the gyroid (about 1 minute). Keep a separate copy
    of this save.
  - `reply` (`--days 2 --time 16:59`, the `letter` save): on load, Milk's reply is generated and left at the post
    office (`mNpc_Remail`), and at the 17:00 delivery the mail carrier puts it in the mailbox (2140,1420). Facing
    northwest in front of the mailbox, open it and read the second letter ("밀크님 보냄", "From Milk"). A reply only
    comes when the letter is judged "good" or "bad" (a letter of a few words in Hangul gets no judgment, so there is
    no reply; korean_patch.md).
  - `kk` (`--days 6 --time 20`, the `letter` save): Saturday night, to K.K. Slider in front of the train station
    (2220,940) "들려 줘!" ("Play for me!") → "있어 있어!" ("I've got one!") → "K.K.보사노바" in the editor →
    "오케이! 그럼 들어 주세요. K.K.보사노바" ("Okay! Then have a listen. K.K. Bossa") → performance (the first
    performance comes with the staff roll, about 2 minutes) → the record he gives in return (at about 4 minutes).
    The choices only appear after pressing A once more on the question; the stick is moved after that.
  - House expansion and the island (one day at a time from the `letter` save, 1–2 minutes each; at the end of each
    day, copy the save and use it for the next day):
    `house` (`--days 3`): pay back the loan at the post office (`BELLS 99999` → "입금하고 싶어" ("I want to deposit")
    → in the amount entry, up once = as much as can be paid → START) → Tom Nook's HRA talk ("나한테 맡겨!", "Leave it
    to me!") → go back in for the expansion offer (first roof color) → save.
    `house-next <payments> <choices>` (`--days 4, 5, …`): Tom Nook announces the expansion and a new loan → pay that
    many times → next offer. With this save: `house-next 2` (day 4, loan 125000 → offer of a bigger house),
    `house-next 3 9` (day 5, 298000 → orders the basement with the last choice),
    `house-next 3` (day 6, 249000 → second-floor offer), `island-buy 6` (day 7, 598000 → buys the island "선리섬",
    loan 998000).
    `rooms` (day 8): from the house's first floor (scene 40), the front-left stairs (south from 100,370) lead to the
    second floor (41), and the right-hand ones (300,370) to the basement (45).
    `island 0` (day 8): the boat and Kapp'n are only placed when the player `WARP`s to the block north of the dock and
    walks in (a `WARP` straight into the dock block leaves both missing; `ac_birth_control` places the boat on
    entering block E-5/F-4). Next to Kapp'n (3438,4230), at (3470,4240): "섬에 갈래" ("I want to go to the island")
    → to the island while he sings → walk around the island → to Kapp'n at the island dock (south from 3660,5262)
    → back to the town (about 5 minutes).
  - `week` (`--days 1`, the `--save` save): a whole week in one run instead of a new run for each day. The
    input-script command `CLOCK h[:m[:s]]` moves the clock forward to that time (tomorrow if that time has already
    passed today), and since the game reads the clock every frame, it crosses midnight by itself
    (date and weekday, turnip prices, daily-update flags). Monday letter → `CLOCK 23:59:40`, then 40 seconds → at
    0:00 on Tuesday, save at the gyroid and quit → continue by answering Milk's question with A (the reply is
    generated when the town loads) → `CLOCK 16:59` → Milk's reply in the mailbox → four midnights → Saturday
    `CLOCK 20:00` → K.K. Slider song request → save. About 41000 frames (a few minutes with `--no-framelimit`, about
    12 minutes at normal speed: 727 seconds with `--audio-dump` and without `--no-framelimit --script-fast`, and the
    audio matches the fast run's WAV for 23.05 million samples, so there are no dropouts).
    `POS` prints the game's date and time (`time 2026-11-03 16:59:00 weekday 2`). Spending a day in real time would
    take 5.18 million frames (24 hours at normal speed), so `CLOCK` skips ahead.
  - Events (from the `--save` save; it is mid-job, so `NOJOB` first; locations are those of that town):
    `broker` (`--days 26`, 11/27): this week's visitor is decided a day or two in advance
    (`special event 75 day 0B1B state …` in `POS`; 75 is Crazy Redd's sale, 3 means in progress) and opens shop
    at 18:00. The tent is placed on an empty lot out of view, so `WARP` to the house and then
    `WARP` south of the lot (sign 1420,1580) → Crazy Redd at the entrance "이야~, 어이쿠 이거 참! 손님, 어서옵쇼~~!"
    ("Well, well, well! Welcome, customer~~!") → inside the tent (scene 12) "자, 어서어서, 손님~!" ("Come on, come on,
    customer~!"). There is no route yet for buying items inside the tent (A in front of an item got no reaction).
    `harvest` (`--days 25 --time 18:30`, 11/26): on the event day, at the first scene change, the announcement at
    the train station ("여러분~! 추수감사절이에요!", "Everyone~! It's the Harvest Festival!") → walk up to Franklin
    (D08D) and talk to him ("으아아아! 뭐, 뭐, 뭐야? …", "Aaaah! Wh-wh-what? …").
    `countdown` (`--days 60 --time 23:45`, 12/31): announcement ("새해맞이 카운트다운에 와 주세요~!!", "Come to the
    New Year's countdown~!!") → walk into the pond block (`pool block 3,3` in `POS`) from the north (event villagers
    are placed while the block is out of view) → `CLOCK 23:59:40` →
    Milk "1분 전이야!!!" ("One minute to go!!!") → 2027-01-01 0:00 "HAPPY NEW YEAR!!" and fireworks → villager lines.
    `POS` prints today's event numbers (`events today 56* 108*@5,4 …`: `*` means in progress at the current time,
    `@` is the block where it opens) and this week's visitor's location
    (`special event place block 2,2 unit 3,7 actor 5826`) (the numbers are `mEv_EVENT_*` in `m_event.h`).
  - Where conversations cannot be timed by frame count, the input script waits with `TALK` (A until a choice
    appears), `CHOOSE n` (the n-th choice, 9 = the last), `WAITMENU` (A until a menu opens) and `TALKEND [n [quiet]]`
    (A until the dialog closes and stays closed for `quiet` frames).
    While waiting, script time stops, and the following lines happen that much later. `POS` also prints the house
    size, next size, loan and bells on hand.
- Interior checks (Korean-patched disc, starting from a save made with `--save`, advanced to the next day):
  `python3 pc/tools/make_interior_script.py <building> > /tmp/i.txt`, then
  `./DobutsuNoMoriEPlus --no-framelimit --script-fast --deterministic --days 1 --verbose --input-script /tmp/i.txt`
  (1–3 minutes per building, screenshots in `shots/i_<building>_<n>_<step>.ppm`; `POS` prints the scene number).
  - The route goes to the door with `WARP x,z [dir]` (reloads the town at that spot, as when leaving a building),
    runs `NOJOB` (ends Nook's part-time job: the first-day save is mid-job, so the shop and house differ from usual)
    and enters through the door the way the game does.
    Door positions and directions are in the script's `DOORS` (for that save's town).
  - `shop`, `museum` (hall, Blathers's room, fish room), `post`, `tailors`, `police`, `myhouse`,
    `house<villager id>` (e.g. `houseE015`), `all` (the above plus two villager houses in one run).
  - `state`: places a box in the house (pockets → 배치하기 (Place); facing placed furniture and pressing B picks it up
    again), donates a fish to Blathers (`GIVE`), buys an item from the west wall of Nook's shop (`BELLS`) and saves
    at the gyroid. `state-check` loads that save, goes through the same rooms and saves again (also with saves from
    another build).
  - `town` (a few spots in the town, together with `--days`/`--time`: night, snow), `town-rain` (heavy rain with
    `WEATHER 1 3`; during the part-time job the game turns rain into clear weather), `famicom` (places a Famicom in
    the house and turns it on; does not come back out).
- Title demo: `make_opening_script.py --idle 12000` (presses nothing, a screenshot every 300 frames).
- For memory problems, build in a separate build directory with `-fsanitize=address -fsanitize-recover=address`
  (`ASAN_OPTIONS=detect_leaks=0:halt_on_error=0`). Some of the original game's out-of-bounds reads
  (`mFM_LoadBGCommonTex` etc.) are harmless.

## 5. Korean-patched disc

The user uploads the patch zip (v0.9.2) to the session (it is only available from the original distribution site; do
not put it in the repository).

```sh
unzip -q patch.zip -d /tmp/kopatch
python3 pc/tools/apply_ko_patch.py /tmp/disc/game.ciso /tmp/kopatch/Doubutsu-no-Mori-e-plus-Korean-v0.9.2 /tmp/disc/ko.iso
mkdir -p pc/build32/bin_ko/rom && cd pc/build32/bin_ko
ln -sfn ../bin/DobutsuNoMoriEPlus . && ln -sfn ../bin/shaders . && ln -sfn ../bin/DobutsuNoMoriEPlus.relmap .
cp ../bin/settings.ini ../bin/keybindings.ini . && ln -sfn /tmp/disc/ko.iso rom/game.iso
```

It works if the log shows `Korean patch detected` and `loaded N data objects from the disc`.
Hangul input check: running the script made with
`python3 pc/tools/make_opening_script.py --korean 9000 > /tmp/op_ko.txt` leaves the screens of each step in `shots/name_*.ppm`, `town_*.ppm` and `eng_*.ppm`.
For analyzing the patch's code, see `pc/tools/reldis.py hooks|dis` (extract the patched foresta.rel with
`gcdisc.py extract`) and [korean_patch.md](korean_patch.md).

## 6. Windows build and Wine check

```sh
apt-get install -y gcc-mingw-w64-i686 g++-mingw-w64-i686 wine32:i386 wine
mkdir -p /tmp/sdl2mingw && curl -sSL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-mingw.tar.gz | tar xz -C /tmp/sdl2mingw
cmake -S pc -B pc/buildwin -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/pc/cmake/Toolchain-mingw32.cmake \
      -DSDL2_DIR=/tmp/sdl2mingw/SDL2-2.30.9/i686-w64-mingw32/lib/cmake/SDL2 -DPC_BUILD_TESTS=OFF
ninja -C pc/buildwin
cd pc/buildwin/bin && mkdir -p rom && ln -sfn /tmp/disc/ko.iso rom/game.iso && cp ../../build32/bin/settings.ini .
WINEPREFIX=/tmp/wineprefix WINEDEBUG=-all wine DobutsuNoMoriEPlus.exe --no-framelimit --script-fast \
      --input-script 'Z:\tmp\op_fast.txt'
```

- MinGW differences: bitfield layout (matched with `-mno-ms-bitfields`); the PE linker also treats undefined
  references in functions that would be discarded as errors (`pc_stubs.c`); the link map format (`.data$name`, sizes
  in 4-byte units; `gen_rel_overlay.py` reads both).
- Wine has no sound device, so audio falls back to SDL `dummy` (the same happens on real Windows when there is no
  output device).
- If Xvfb is down, Wine cannot create a window and reports "no driver could be loaded".

## 7. OpenGL ES path (for Android) desktop check

After `apt-get install -y libgles-dev:i386 libegl-dev:i386`, build separately with `-DPC_GLES=ON`
(`pc/build32gles`). SDL creates an ES 3.0 context, and `pc_gl.h` rewrites the shaders' `#version 330 core` to
`#version 300 es`. It works if the log shows `OpenGL OpenGL ES 3.x Mesa`.

## 8. Importing and exporting GameCube saves (GCI)

Imports saves from a GameCube memory card (`.gci` files taken out with Dolphin etc.) into the PC port's
`save/card_a/`, and exports them the other way. Covered are the town (`DobutsunomoriE_MURA`, 0x72000 bytes of data)
and the Famicom common save (`DobutsunomoriE_F_SAVE` → `DobutsunomoriP_F_SAVE.sav` on PC). The tool `afe_gci` is
built into `bin/` along with the game (it is also in the `PC_TESTS_ONLY` build).
Run from `bin/`, its default folder is `save/card_a`, the same as the game's.

```sh
cd pc/build32/bin
./afe_gci info  ~/01-GAEJ-DobutsunomoriE_MURA.gci        # what kind of file it is, whether the checksum is correct
./afe_gci import ~/01-GAEJ-DobutsunomoriE_MURA.gci       # → save/card_a/DobutsunomoriE_MURA
./afe_gci import ~/F_SAVE.gci --force                     # keeps the existing file as .bak and replaces it
./afe_gci export land -o ~/mura.gci                       # town in save/card_a → GCI
./afe_gci export famicom -o ~/fsave.gci [--template original_F_SAVE.gci]
```

- `--card DIR` uses another folder (e.g. `save/card_b`). Make the changes while the game is not running.
- Only files with game code GAEJ01 (Japanese e+) are accepted. A town save is imported only if at least one of its
  two copies (main and backup) has a correct checksum; a copy with a bad checksum is carried over still bad (the game
  then uses the other copy or treats the town as broken, as it originally does).
- Conversion: the field table `pc/src/pc_save_swap_table.inc` is generated by `pc/tools/gen_save_swap.py` from the
  DWARF of the e+ headers compiled as 32-bit and 64-bit (needs `gcc-multilib` and `pip install pyelftools`; CI uses
  `--check` to verify that it matches the headers). Rerun it when the headers change. It stops when it meets a
  union for which it cannot decide which member is used; check the usage in the game code and record it in the
  script's `UNION_POLICY` (or, when a tag selects the member, in a handler in `pc_save_swap.c`).
- Checksums: the town data (0x2E000) and the diary (0xD1C0) are made so that the sum of their u16 words is 0
  (`m_flashrom.c`). The GameCube adds big-endian words, so the checksum is recomputed after conversion. The Famicom
  save uses a byte sum, so byte order does not matter.
- How to check in the game: import into an empty folder with `import ... --card <folder>/save/card_a`, and with
  that `save/` run the button presses (continue) of `make_opening_script.py --korean 2700`
  (`--deterministic --days N`; the first player at the villager's "너 누구였더라?" ("Who were you again?")). Saving
  at the gyroid is `WARP 2085,1505 6` followed by A 26 times. Verified with a real town GCI: import → continue →
  save → continue → export → import again (README.md, "Known issues").
- Not verified: loading an exported GCI on a GameCube or in Dolphin, and a real Famicom save GCI.
