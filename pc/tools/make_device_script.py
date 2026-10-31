#!/usr/bin/env python3
"""Write an --input-script that tries the e+ devices a PC does not have.

The GameCube's SD card adapter, the Game Boy Advance cable and the e-Reader
(through the GBA) are answered as "nothing there" on PC (EXIProbeEx -1, the
GBA calls fail). The game then has to end each try with its own "no SD card"
or "no GBA" message; none of them may wait forever (the photo menu did,
before EXIProbeEx said -1). Starts from the save that
make_newgame_script.py --korean --save leaves (load it with the same build,
--deterministic and --days 1, as make_interior_script.py does).

  film        the photo menu (MENU FILM): save to SD? yes -> "SD card
              writing", "no SD card", where to put it; the same again
  terminal    the post office's e-Reader terminal: read a card (through the
              GBA); ends in its "the GBA is not connected" message
  terminal-send  the same terminal: send the reader program to the GBA
  tailors     the Able Sisters: Mabel's GBA talk (send a design to the GBA)
  station     leaving town by train: the SD check, then the memory card in
              slot B; the title, and back again (the save is written)
  all         film, terminal and tailors in one run

Every step leaves a screenshot d_<scenario>_<n>_<step>; --verbose prints the
POS of each (the scene: 14 the post office, 46 the Able Sisters).

usage: make_device_script.py SCENARIO > script.txt
"""
import subprocess
import sys
from pathlib import Path

LOADED = 2700   # the load presses of make_opening_script.py end here
FREE = 3000     # the player can move (in front of the house, by the haniwa)

# make_interior_script.py: where to stand before the door, the stick, and
# where the player is inside on coming in
DOORS = {"post": ((3380, 1070), "SU+SR"), "tailors": ((2860, 4150), "SU+SR")}
INSIDE = {"post": (160, 300), "tailors": (180, 300)}

ev = []
f = FREE
shots = 0
part = ""


def shot(name, wait=10):
    global f, shots
    f += wait
    shots += 1
    ev.append(f"{f} SHOT d_{part}_{shots:02d}_{name}")
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
    do("NOJOB", 40)
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


def film():
    global part, shots
    part, shots = "film", 0
    do("MENU FILM 0 0", 120)
    shot("question", 0)
    for _ in range(2):                         # 사진을, SD카드에 저장? -> 네
        presses(9, every=90, name="save")      # writing, no card, where the adapter goes, again?
    for _ in range(4):
        do("B", 60)
    shot("closed", 60)


def terminal(choice=0):
    """choice 0 reads a card, 1 sends the reader program to the GBA"""
    global part, shots
    part, shots = "terminal" if choice == 0 else "terminal-send", 0
    go_in("post")
    do("SEEK 97,222 200", 210)                 # in reach: x > 90, within 43 of 60,220
    do("SL 12", 60)
    shot("front", 0)
    do("A", 60)
    presses(3, every=50, name="q")
    shot("question", 100)
    do("A", 10)                                 # its choices come up
    shot("choices", 60)
    for _ in range(choice):
        do("SD 4", 30)
    shot("chosen", 0)
    presses(14, every=60, name="", each=2)
    shot("done", 60)
    go_out("post")


def tailors():
    global part, shots
    part, shots = "tailors", 0
    go_in("tailors")
    presses(3)                                  # her welcome
    shot("welcomed", 20)
    do("SEEK 180,262 300", 310)                # before Mabel (D070, 180,220)
    do("SU 4", 20)
    for choice in (4,):                         # her menu: 어드밴스 얘기 (the GBA)
        do("A", 60)
        presses(2, every=50, name=f"c{choice}_q")
        do("A", 120)
        for _ in range(choice):
            do("SD 4", 30)
        shot(f"c{choice}_choice", 0)
        presses(12, every=60, name=f"c{choice}_", each=2)
        for _ in range(6):
            do("B", 40)
        shot(f"c{choice}_done", 60)
    go_out("tailors")


def station():
    """Leaving town by train: the station master looks for an SD card first,
    then writes the player to the memory card in slot B; the train comes,
    the game goes back to its title, and loading the town again moves the
    player back from slot B (no other town to go to)"""
    global part, shots
    part, shots = "station", 0
    do("NOJOB", 10)                             # before the town loads: he waits for the job otherwise
    do("WARP 2180,900", 250)
    do("SEEK 2180,862 200", 210)               # the station master, 2180,820
    do("SU 4", 20)
    shot("front", 0)
    presses(3, every=70)                        # 외출하는 거끼? -> 외출할 거야
    presses(16, every=70, name="talk", each=2)  # the SD check, slot B, slot A, the train
    for i in range(6):
        shot(f"train{i}", 200)
    presses(16, every=200, name="away", each=2)  # boarding, the title, Milk, slot B to A
    shot("back", 600)


scenario = sys.argv[1] if len(sys.argv) > 1 else ""
SCENARIOS = {"film": (film,), "terminal": (terminal,), "terminal-send": (lambda: terminal(1),), "tailors": (tailors,),
             "station": (station,), "all": (film, terminal, tailors)}
if scenario not in SCENARIOS:
    sys.exit(__doc__)
opening = subprocess.run([sys.executable, str(Path(__file__).with_name("make_opening_script.py")), "--korean",
                          str(LOADED)], capture_output=True, text=True, check=True).stdout.splitlines()
ev += [l for l in opening if int(l.split()[0]) < LOADED and l.split()[1] not in ("QUIT", "SHOT")]
for step in SCENARIOS[scenario]:
    step()
ev.append(f"{f + 60} SHOT d_{scenario}_end")
ev.append(f"{f + 61} QUIT")
ev.sort(key=lambda line: int(line.split()[0]))
print("\n".join(ev))
