#!/usr/bin/env python3
"""Write an --input-script that tries the Korean patch's editors on a saved town.

Starts from the save that make_newgame_script.py --korean --save leaves
(load it with the same build and --deterministic) and runs one scenario:

  letter         write a letter to Milk: body by keyboard (TYPE), the on-screen
                 keyboard by stick and A and by TOUCH, the D-pad up to the
                 header and down to the closing, BS and the cursor; confirm it,
                 open it again from the pocket, then save at the haniwa
  letter-check   after a letter run: open the letter from the pocket again
  ephrase        give Milk a new catchphrase (the name editor through the LEDIT
                 debug command), hear it when talking to her, save
  ephrase-check  after an ephrase run: print it (CATCH) and talk to her again
  voice          talk to Milk with VOICELOG: the voice of each character
  request        the song request editor (LEDIT 4): a name with a dictionary
                 code, one typed with and without a space, one that is no song;
                 ITEM prints the song each gives (2A0A, 2A13, 2A13, 2A8C)
  reset          Resetti on his third visit (RESETS 6) asks for an apology: a
                 wrong one, then his phrase with a space he does not mind
  ui             the time input, the haniwa's alarm (two kinds) and message board,
                 the password screen and the notice board (MENU debug command)
  warning        the 24 warnings of the pocket menu (MENU WARNING n)

Every step leaves a screenshot k_<scenario>_<n>_<step>; CATCH and ITEM print
with --verbose. GIVE, LEDIT, RESETS, CATCH and ITEM are the input script's
debug commands (pc/include/pc_input_script.h).

usage: make_ko_edit_script.py SCENARIO > script.txt
"""
import subprocess
import sys
from pathlib import Path

LOADED = 2700   # the load presses of make_opening_script.py end here
FREE = 3000     # the player can move (in front of the house, by the haniwa)

# On-screen keyboard (pc/src/ko/ko_osk.c): rows of keys, the cursor starts on
# the first letter key; where TOUCH finds a key, in percent of the window
OSK_TOUCH = {"ㄱ": (40.2, 71.5), "ㅏ": (65.9, 78.8)}

ev = []
f = FREE


shots = 0


def shot(name, wait=10):
    global f, shots
    f += wait
    shots += 1
    ev.append(f"{f} SHOT k_{scenario}_{shots:02d}_{name}")


def do(cmd, wait=30):
    global f
    ev.append(f"{f} {cmd}")
    f += wait


def typed(keys, name):
    do(f"TYPE {keys}", 10)
    shot(name, 10)


def presses(n, every=36):
    for _ in range(n):
        do("A", every)


def save_at_haniwa():
    """Walk to the haniwa, save and quit (the first choice of Milk's question);
    no presses after it, they would load the town again from the title"""
    do("SEEK 2085,1505 200", 210)
    do("SL 6", 20)
    presses(26)
    shot("saving", 0)
    shot("saved", 800)


def open_letter(menu_open=False):
    """START, the pointer from the first pocket to the first letter, A"""
    if not menu_open:
        do("START", 100)
    for d in ("SR",) * 5 + ("SU",) * 2:
        do(f"{d} 6", 30)
    do("A", 60)
    shot("letter_menu", 0)


scenario = sys.argv[1] if len(sys.argv) > 1 else ""
opening = subprocess.run([sys.executable, str(Path(__file__).with_name("make_opening_script.py")), "--korean",
                          str(LOADED)], capture_output=True, text=True, check=True).stdout.splitlines()
ev += [l for l in opening if int(l.split()[0]) < LOADED and l.split()[1] not in ("QUIT", "SHOT")]

if scenario == "letter":
    do("GIVE 2000", 50)                         # stationery
    do("START", 100)
    do("A", 50)                                  # the item's menu
    do("SD 8", 40)
    do("A", 260)                                 # write a letter: first the address
    do("A", 150)
    do("A", 150)                                 # the first in the address book (Milk)
    shot("open", 0)
    typed("dkssudgktpdy", "body_typed")          # 안녕하세요
    do("TYPE {ENTER}", 30)
    # the on-screen keyboard: 반 with the stick and A, 가 by touch
    do("A", 12)                                  # ㅂ
    do("SD 4", 12)
    for _ in range(7):
        do("SR 4", 12)
    do("A", 12)                                  # ㅏ
    for _ in range(6):
        do("SL 4", 12)
    do("A", 12)                                  # ㄴ
    for key in "ㄱㅏ":
        x, y = OSK_TOUCH[key]
        do(f"TOUCH {x},{y} 4", 12)
    shot("body_osk")
    # a long line wraps; BS takes one jamo, L/R move by a syllable
    typed("{SP}ekfkakqktk{SP}dkwkcnzm{SP}xkvkgk{SP}rkskekfkakqktk", "body_wrap")
    typed("{BS}{BS}", "body_bs")
    do("L", 12)
    do("L", 12)
    typed("gh", "body_insert")                   # 호 between two syllables
    # the D-pad: up to the header, before and after the name
    for _ in range(4):
        do("UP 6", 20)
    typed("dkssud", "header_pre")                # 안녕
    do("RIGHT 6", 20)
    do("RIGHT 6", 20)
    typed("{SP}gkskrkt", "header_post")          # only what fits: " 하"
    for _ in range(8):
        do("DOWN 6", 20)
    shot("footer")
    typed("rlavkf", "footer_typed")              # 김팔
    do("LEFT 6", 20)
    do("LEFT 6", 20)
    shot("footer_left")
    do("START", 80)
    shot("confirm", 0)
    do("A", 100)
    do("A", 100)
    do("A", 100)                                 # into the letters of the pocket menu
    open_letter(menu_open=True)
    do("A", 150)                                 # write again: the letter as it was
    shot("reopened", 0)
    do("START", 80)
    do("A", 120)                                 # 괜찮아? then 네
    do("A", 200)                                 # back in the pocket menu
    do("B", 150)                                 # which closes
    save_at_haniwa()
elif scenario == "letter-check":
    open_letter()
    do("A", 150)
    shot("reopened", 0)
elif scenario in ("ephrase", "ephrase-check", "voice"):
    if scenario == "voice":
        do("VOICELOG 1", 0)                      # print the voice of every character
    else:
        do("CATCH E038", 0)
    if scenario == "ephrase":
        do("LEDIT 2 E038", 60)                   # Milk's catchphrase
        shot("open", 0)
        typed("gkskenfdl", "typed")              # 하나둘이: packed into the 7 bytes
        do("TYPE {ENTER}", 120)
        do("CATCH E038", 40)
    # to Milk (east, the next block); she wanders, so walk to her and press A again
    for d in ("SR 80", "SD 40", "SR 80", "SR 30"):
        do(d, int(d.split()[1]) + 10)
    for _ in range(6):
        do("SEEK E038 120", 125)
        do("A", 35)
    for i in range(36 if scenario != "voice" else 12):
        do("A", 40)
        shot(f"talk{i:02d}", -10)
    for _ in range(30):
        do("B", 36)
    if scenario == "ephrase":
        do("SEEK 2128,1488 900", 900)
        save_at_haniwa()
elif scenario == "request":
    songs = (("{HAN}K.K.{HAN}qhtkshqk", "bossa"),            # K.K.보사노바, a dictionary code on the disc
             ("dkdlffosemdml{SP}shfo", "irish"),               # 아일랜드의 노래
             ("dkdlffosemdmlshfo", "irish_nospace"),
             ("{HAN}K.K.{HAN}akfl", "none"))                   # K.K.마리: no such song
    for keys, name in songs:
        do("LEDIT 4", 80)
        typed(keys, name)
        do("TYPE {ENTER}", 180)
        do("ITEM", 100)
elif scenario == "reset":
    for fr in range(1500, FREE):                 # his third visit, every frame of the loading
        ev.append(f"{fr} RESETS 6")
    do("SL 40", 60)                              # he comes once the player walks
    # his talk up to the editor and past it: presses there type a key, which
    # BS takes back. With the save of make_newgame_script.py --save he asks
    # for 정말죄송합니다; first a wrong answer, and he asks again
    for keys, name in (("akffk", "wrong"),                     # 몰라
                       ("wjdakf{SP}whlthdgkqslek", "right")):  # 정말 죄송합니다
        for _ in range(80):
            do("A", 60)
        shot(f"{name}_open", 0)
        typed("{BS}" * 10, f"{name}_cleared")
        typed(keys, name)
        do("TYPE {ENTER}", 100)
        for i in range(12):
            do("A", 60)
            shot(f"{name}_talk{i:02d}", -20)
elif scenario == "ui":
    # screens the game opens only now and then, through the MENU debug command
    for name, menu, keys, close in (
            ("timein", "TIMEIN 0 0", [], "START"),                  # 시간을 맞춰보자
            ("alarm_play", "ALARMIN 0 0", [], "START"),             # 시간 / 분 뒤에
            ("alarm_time", "ALARMIN 1 0", [], "START"),             # 분에
            ("password", "PASSWORDMAKE 0 0", [], "START"),          # 마을 / 이름
            ("hboard", "HBOARD 0 0", ["{DOWN}" * 3, "{RIGHT}" * 16] + ["{BS}" * 20] * 3 +  # clear the message
             ["dkssudgktpdy{SP}qksrkdnj,{SP}dhsmfdms{SP}woalTsms{SP}skfdlrh.",
              "{SP}sodlfdms{SP}rmadydlfdlqslek."], "START"),
            ("notice", "NOTICE 0 0", [], "B")):                     # the date on the board
        do(f"MENU {menu}", 90)
        shot(f"{name}_open", 0)
        for part in keys:                           # TYPE takes up to 127 characters
            do(f"TYPE {part}", 30)
        if keys:
            shot(f"{name}_typed", 0)
        do(close, 60)
        do("B", 40)
        shot(f"{name}_closed", 30)
        do("B", 60)
elif scenario == "boards":
    # writing on the notice board (A on its page) and in the diary (calendar)
    do("MENU NOTICE 0 0", 90)
    do("A", 90)
    shot("notice_editor", 0)
    typed("dkssudgktpdy{ENTER}qksrkqtmqslek.{SP}{SP}wkf{SP}qnxkrgody!", "notice_typed")
    do("START", 90)
    shot("notice_end", 0)
    do("A", 60)                                  # 괜찮아? yes
    shot("notice_posted", 30)
    presses(2, 60)
    do("B", 60)
    do("MENU CALENDAR 0 0", 120)
    shot("calendar", 0)
    do("A", 120)                                 # today
    do("A", 120)                                 # its diary page
    shot("diary", 0)
    do("A", 90)                                  # write
    typed("dhsmfdms{SP}qkrhks{SP}dkswjsgkrp{SP}tkftkfgotek.{ENTER}dlqkdmfh{SP}rkTek", "diary_typed")
    do("START", 90)
    shot("diary_end", 0)
    do("A", 90)
    do("A", 90)                                  # 괜찮아? yes
    shot("diary_lock", 0)
    do("A", 120)                                 # 남이 봐도 돼? yes
    shot("diary_saved", 0)
    for _ in range(4):
        do("B", 60)
    do("MENU PASSWORDCHK 0 0", 90)
    shot("passwordchk", 0)
    do("START", 60)
    do("B", 60)
    do("MENU FILM 0 0", 120)                     # the photo menu; A would try the SD card
    shot("film", 0)
    do("B", 120)
    shot("film_closed", 0)
elif scenario == "warning":
    # the pocket menu's warnings (wr_win_data) and the photo menu's messages
    for n in range(24):
        do(f"MENU WARNING {n} 0", 60)
        shot(f"w{n:02d}", 0)
        do("A", 60)
        do("B", 60)
else:
    sys.exit(__doc__)

ev.append(f"{f + 60} SHOT k_{scenario}_end")
ev.append(f"{f + 61} QUIT")
ev.sort(key=lambda line: int(line.split()[0]))
print("\n".join(ev))
