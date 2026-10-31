#!/usr/bin/env python3
"""Write an --input-script that goes into a building of the saved town.

Starts from the save that make_newgame_script.py --korean --save leaves
(load it with the same build, --deterministic and --days 1; the positions
are those of that town), moves the player by the door (WARP: rivers and
cliffs are in the way of walking there), ends the part-time job for Nook
(NOJOB, so the building acts as on any later day), goes in through the door
the game's way, looks around inside, and comes out again. Buildings:

  shop        Nook's shop: his menu (six choices), the goods, his goodbye
  museum      the hall, Blathers' room and the exhibit rooms left and right
  post        the post office: Pelly at the counter
  tailors     the Able Sisters: Mabel, the designs
  police      the police box: the lost-and-found
  myhouse     the player's house (the first of the four)
  houseE015   a villager's house, by the villager's id (any of HOMES)
  all         shop to myhouse and the houses of E015 and E038 in one run

On 2026-11-01 (--days 0) the town has its fishing tourney, which NOJOB lets
start: going back outside shows its announcement at the station first.

Every step leaves a screenshot i_<building>_<n>_<step>; with --verbose the
POS at each shows the scene number (7 the town, 9 the shop, 35/36.. the
museum, 14 the post office, 46 the Able Sisters, 17 the police box, 20 the
player's room, 6 a villager's house) and what is in front of the player.
WARP and NOJOB are the input script's debug commands
(pc/include/pc_input_script.h).

usage: make_interior_script.py BUILDING > script.txt
"""
import subprocess
import sys
from pathlib import Path

LOADED = 2700   # the load presses of make_opening_script.py end here
FREE = 3000     # the player can move (in front of the house, by the haniwa)

# Where to stand before the door (x, z), the stick that walks in, and
# whether the door opens with A (the post office, the Able Sisters, houses;
# a villager's door is the unit just north of where the player stands)
DOORS = {
    "shop": ((975, 1145), "SU+SR", False),     # Nook's: 1040,1080; door at its south-west corner
    "museum": ((3500, 3640), "SU", False),     # 3500,3500; door in the middle of the south side
    "post": ((3380, 1070), "SU+SR", True),     # 3440,1000
    "tailors": ((2860, 4150), "SU+SR", True),  # 2920,4080
    "police": ((925, 3580), "SU+SL", False),   # 860,3500; door at its south-east corner
    "myhouse": ((2112, 1485), "", True),       # the first of the four houses, 2080,1440 (door 2100,1460)
}
# Where the player stands inside on coming in, by the way out
INSIDE = {"shop": (160, 300), "museum": (240, 440), "post": (160, 300), "tailors": (180, 300),
          "police": (200, 380), "myhouse": (120, 220), "house": (160, 300)}
# The villagers' houses, by the villager: where they live (Save animals[].home_info)
HOMES = {"E038": (2740, 1700), "E015": (3060, 1780), "E043": (3020, 2900),
         "E08C": (1140, 2380), "E09D": (780, 2780), "E09E": (1740, 3700)}
for npc, (x, z) in HOMES.items():
    DOORS["house" + npc] = ((x, z - 8), "", True)

ev = []
f = FREE
shots = 0
building = ""


def shot(name, wait=10):
    global f, shots
    f += wait
    shots += 1
    ev.append(f"{f} SHOT i_{building}_{shots:02d}_{name}")
    ev.append(f"{f} POS")


def do(cmd, wait=30):
    global f
    ev.append(f"{f} {cmd}")
    f += wait


def presses(n, every=40, name=None):
    for i in range(n):
        do("A", every)
        if name is not None and i % 4 == 3:
            shot(f"{name}{i // 4:02d}", 0)


def go_in(name):
    (x, z), stick, press = DOORS[name]
    do(f"WARP {x},{z} {5 if name == 'myhouse' else 4}", 200)   # facing north west, or north
    do("NOJOB", 40)
    shot("door", 0)
    if press:
        if stick:
            do(f"{stick} 8", 20)
        do("A", 10)
    else:
        do(f"{stick} 40", 50)
    shot("opening", 60)
    shot("inside", 300)


def go_out(name):
    x, z = INSIDE["house" if name.startswith("house") else name]
    do("B", 30)
    do(f"SEEK {x},{z - 20} 300", 310)
    do("SD 120", 130)
    shot("leaving", 0)
    presses(3)                                       # the shopkeeper's goodbye
    shot("left", 60)
    shot("outside", 300)


def visit(name):
    global building, shots
    building, shots = name, 0
    go_in(name)
    if name == "shop":
        presses(2)                                   # his greeting, then what to do
        shot("nook_menu", 20)
        for _ in range(8):
            do("B", 30)
        shot("nook_bye")
        do("SR 40", 50)
        shot("goods_right")
        do("SU 40", 50)
        do("SL 100", 110)
        shot("goods_left")
        do("SR 40", 50)
    elif name == "museum":
        do("SU 160", 170)                            # up the hall to Blathers' room
        shot("curator", 120)
        presses(2)
        shot("blathers", 20)
        for _ in range(10):
            do("B", 30)
        # his room opens on the four exhibit rooms, two each side
        for out, back, room in (("SL", "SR", "left"), ("SR", "SL", "right")):
            do(f"{out} 200", 210)
            shot(f"room_{room}", 150)
            do(f"SU 60", 70)
            shot(f"room_{room}_in")
            do(f"SD 60", 70)
            do(f"{back} 200", 360)
            shot(f"back_{room}")
        do("SD 300", 320)                            # down to the hall
        shot("hall", 150)
    elif name == "post":
        do("SU 80", 90)                              # to the counter
        presses(3)
        shot("pelly", 20)
        for _ in range(10):
            do("B", 30)
    elif name == "tailors":
        presses(2)
        shot("mabel", 20)
        for _ in range(10):
            do("B", 30)
        do("SR 60", 70)
        shot("designs")
    elif name == "police":
        do("SU 30", 40)
        presses(3)
        shot("copper", 20)
        for _ in range(10):
            do("B", 30)
    elif name == "myhouse":
        do("SU 60", 70)
        shot("room")
        do("SL 60", 70)
        shot("room_left")
        do("SR 120", 130)
        shot("room_right")
        do("SL 60", 70)
    elif name.startswith("house"):
        presses(3)
        shot("villager", 20)
        for _ in range(10):
            do("B", 30)
        do("SL 40", 50)
        shot("room")
    go_out(name)


scenario = sys.argv[1] if len(sys.argv) > 1 else ""
if scenario not in DOORS and scenario != "all":
    sys.exit(__doc__)
opening = subprocess.run([sys.executable, str(Path(__file__).with_name("make_opening_script.py")), "--korean",
                          str(LOADED)], capture_output=True, text=True, check=True).stdout.splitlines()
ev += [l for l in opening if int(l.split()[0]) < LOADED and l.split()[1] not in ("QUIT", "SHOT")]
ALL = ("shop", "museum", "post", "tailors", "police", "myhouse", "houseE015", "houseE038")
for name in ALL if scenario == "all" else (scenario,):
    visit(name)
ev.append(f"{f + 60} SHOT i_{scenario}_end")
ev.append(f"{f + 61} QUIT")
ev.sort(key=lambda line: int(line.split()[0]))
print("\n".join(ev))
