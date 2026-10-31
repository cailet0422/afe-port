#!/usr/bin/env python3
"""Write an --input-script for the days after the part-time job.

Starts from the save that make_newgame_script.py --korean --save leaves (load
it with the same build and --deterministic; the first day of that save is
2026-11-01, a Sunday, and --days N starts N days later). JOBDONE (an input
script debug command) ends the job the way the game does once Nook lets the
player go, so a scenario can start on what the game makes of day two.

  letter      JOBDONE, write Milk a letter (keyboard), give it to Pelly at the
              post office, save at the haniwa (run with --days 1)
  reply       the next day at 16:59 (--days 2 --time 16:59 on the letter run's
              save): the postman's round, the mailbox, Milk's answer

Every step leaves a screenshot n_<scenario>_<n>_<step>; with --verbose the
POS at each shows where the player is.

usage: make_days_script.py SCENARIO > script.txt
"""
import subprocess
import sys
from pathlib import Path

LOADED = 2700   # the load presses of make_opening_script.py end here
FREE = 3000     # the player can move (in front of the house, by the haniwa)
# make_interior_script.py: before the door, the stick, inside on coming in
DOORS = {"post": ((3380, 1070), "SU+SR")}
INSIDE = {"post": (160, 300)}

ev = []
f = FREE
shots = 0
scenario = ""


def shot(name, wait=10):
    global f, shots
    f += wait
    shots += 1
    ev.append(f"{f} SHOT n_{scenario}_{shots:02d}_{name}")
    ev.append(f"{f} POS")


def do(cmd, wait=30):
    global f
    ev.append(f"{f} {cmd}")
    f += wait


def presses(n, every=40, name=None, each=1):
    for i in range(n):
        do("A", every)
        if name is not None and i % each == each - 1:
            shot(f"{name}{i // each:02d}", 0)


def go_in(name):
    (x, z), stick = DOORS[name]
    do(f"WARP {x},{z} 4", 200)
    do(f"{stick} 8", 20)
    do("A", 10)
    shot("inside", 360)


def go_out(name):
    x, z = INSIDE[name]
    for _ in range(6):
        do("B", 30)
    do(f"SEEK {x},{z - 20} 300", 310)
    do("SD 120", 130)
    presses(3)
    shot("outside", 360)


def save_at_haniwa():
    """By the haniwa in front of the house, facing it: save and quit"""
    do("WARP 2085,1505 6", 300)                 # facing west
    presses(26)
    shot("saving", 0)
    shot("saved", 800)


def write_letter(body):
    """Stationery from the pockets, to Milk (the first in the address book),
    the body typed (keys of the Korean keyboard), then put away in the pockets"""
    do("GIVE 2000", 50)
    do("START", 100)
    do("A", 50)                                 # the item's menu
    do("SD 8", 40)
    do("A", 260)                                # write a letter: first the address
    do("A", 150)
    do("A", 150)                                # Milk
    shot("letter_open", 0)
    do(f"TYPE {body}", 10)
    shot("letter_typed", 10)
    do("START", 80)
    shot("letter_confirm", 0)
    do("A", 100)
    do("A", 100)
    do("A", 100)
    for _ in range(4):
        do("B", 60)
    shot("letter_done", 30)


def letter():
    do("JOBDONE", 20)
    # A villager answers a letter the game rates good (enough words from its
    # Japanese keyword table, m_mail_check_ovl.c) or bad (under 5 characters,
    # or one character run on); anything else gets no answer
    # (mNpc_CheckNormalMail_length). The Korean patch keeps that check and
    # its table, so a Korean letter of a few words is never answered: send
    # "안녕" (4 bytes), which is answered as a bad letter.
    write_letter("dkssud")
    go_in("post")
    do("SU 80", 90)                             # to the counter
    presses(4, every=60, name="pelly")          # 편지를 보내고 싶어: the pockets open
    shot("pockets", 60)
    presses(2, every=80, name="send")           # the letter, 보냄 (send)
    presses(2, every=80, name="received")       # 네, 확실히 받았습니다. / 또 다른 편지도?
    shot("another", 60)
    do("A", 200)                                # the question once more (its page again)
    do("A", 60)                                 # its choices: 보낼래 / 이제 됐어
    shot("choices", 0)
    do("SD 4", 40)                              # 이제 됐어
    shot("chosen", 0)
    do("A", 60)
    presses(3, every=60, name="bye")
    go_out("post")
    save_at_haniwa()


def reply():
    """The day after the letter, from 16:59: Milk's answer is made when the
    town loads (mNpc_Remail) and kept at the post office until the 17:00
    round; the postman brings it to the mailbox (2140,1420, east of the
    door). Open it facing north-west and read the second letter."""
    for i in range(8):                          # the postman's round
        shot(f"wait{i}", 300)
    do("WARP 2165,1445 5", 500)                # by the mailbox, facing north-west
    do("A", 150)
    shot("mailbox", 0)
    for _ in range(2):                          # the second letter in the mailbox column
        do("SR 8", 60)
    shot("milk", 0)
    do("A", 60)
    shot("milk_menu", 0)
    do("A", 240)                                # 읽기 (read)
    shot("milk_letter", 0)
    do("A", 150)
    shot("closed", 0)


SCENARIOS = {"letter": letter, "reply": reply}
scenario = sys.argv[1] if len(sys.argv) > 1 else ""
if scenario not in SCENARIOS:
    sys.exit(__doc__)
opening = subprocess.run([sys.executable, str(Path(__file__).with_name("make_opening_script.py")), "--korean",
                          str(LOADED)], capture_output=True, text=True, check=True).stdout.splitlines()
ev += [l for l in opening if int(l.split()[0]) < LOADED and l.split()[1] not in ("QUIT", "SHOT")]
SCENARIOS[scenario]()
ev.append(f"{f + 60} SHOT n_{scenario}_end")
ev.append(f"{f + 61} QUIT")
ev.sort(key=lambda line: int(line.split()[0]))
print("\n".join(ev))
