#!/usr/bin/env python3
"""Write an --input-script that goes into a building of the saved town.

Starts from the save that make_newgame_script.py --korean --save leaves
(load it with the same build, --deterministic and --days 1; the positions
are those of that town), moves the player by the door (WARP: rivers and
cliffs are in the way of walking there), ends the part-time job for Nook
(NOJOB, so the building acts as on any later day), goes in through the door
the game's way, looks around inside, and comes out again. Buildings:

  shop        Nook's shop: his menu (six choices), the goods, his goodbye
  museum      the hall, Blathers' room and the fish room
  post        the post office: Pelly at the counter
  tailors     the Able Sisters: Mabel, the designs
  police      the police box: the lost-and-found
  myhouse     the player's house (the first of the four)
  houseE015   a villager's house, by the villager's id (any of HOMES)
  all         shop to myhouse and the houses of E015 and E038 in one run
  town        no building: the town at a few points (for --days and --time:
              rain, snow, night)
  furnish     myhouse, putting down a chest from the pockets
  buy         shop, buying from the goods by the west wall (BELLS first)
  donate      museum, giving Blathers a fish (GIVE), then the fish room
  state       furnish, buy and donate, then save at the haniwa
  state-check after state (or with its save): the same rooms again, and save

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

CHEST = 0x1000   # FTR_SUM_HAL_CHEST02 facing south
FISH = 0x2300    # ITM_FISH00, a crucian carp, for the museum
BUY = (95, 218)   # in the shop, facing west the goods there (2209 in that town)
furnish = donate = buy = False

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
        if buy:
            do("BELLS 30000", 10)
            do(f"SEEK {BUY[0]},{BUY[1]} 200", 210)  # before the goods by the west wall
            do("SL 4", 20)
            shot("goods_front")
            presses(8, name="buy")                   # 살래 (buy it), and it goes in the pockets
            for _ in range(6):
                do("B", 30)
            shot("bought")
        do("SR 40", 50)
        shot("goods_right")
        do("SU 40", 50)
        do("SL 100", 110)
        shot("goods_left")
        do("SR 40", 50)
    elif name == "museum":
        do("SU 160", 170)                            # up the hall to Blathers' room
        shot("curator", 120)
        do("SEEK 280,275 150", 160)                  # in front of Blathers (D06D, 280,220)
        do("SU 4", 20)
        if donate:
            do(f"GIVE {FISH:04X}", 30)
            presses(28, name="donate")               # 기증할래 (donate), the fish from the pockets;
                                                     # anything else? 있어 opens them again, B shuts them
        else:
            presses(12, name="blathers")             # nothing on show yet
        for _ in range(10):
            do("B", 30)
        if donate:
            do("A", 40)                              # his thanks (B picked 없어, no more)
            for _ in range(6):
                do("B", 30)
        # his room's doors (museum_entrance.c): the fish room east
        do("SR 150", 160)
        shot("fish_room", 150)
        do("SR 60", 70)
        shot("fish_tank")
        do("SL 200", 360)                            # back, by the east door
        shot("back_fish")
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
    elif name == "myhouse" and furnish:
        do("SU 60", 70)
        do(f"GIVE {CHEST:04X}", 30)                  # a chest, put down in front of the player
        do("START", 100)
        shot("pocket", 0)
        do("A", 50)
        do("SD 8", 40)                               # 배치하기 (place), under 집기 (take)
        shot("item_menu", 0)
        do("A", 150)
        shot("placed")                               # the pockets close by themselves
        do("SL 30", 40)
        shot("placed_left")
        do("SD 40", 50)                              # away from it: B facing it takes it back
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


# Points of the town for a look around (the station, the shop, the museum,
# the police box, the villagers' houses in the south, the house)
TOUR = ((2240, 1000), (1000, 1200), (3500, 3700), (960, 3600), (1740, 3780), (2128, 1488))


def town_tour():
    """No buildings: the town at each point, standing and walking a little"""
    global building, shots
    building, shots = "town", 0
    for i, (x, z) in enumerate(TOUR):
        do(f"WARP {x},{z}", 220)
        shot(f"p{i}", 0)
        do("SD 40", 50)
        shot(f"p{i}_walk", 0)


def save_at_haniwa():
    """By the haniwa in front of the house, facing it: save and quit (no
    presses after it, they would load the town again from the title)"""
    global building, shots
    building, shots = "save", 0
    do("WARP 2085,1505 6", 300)                      # facing west
    presses(26)
    shot("saving", 0)
    shot("saved", 800)


scenario = sys.argv[1] if len(sys.argv) > 1 else ""
ALL = ("shop", "museum", "post", "tailors", "police", "myhouse", "houseE015", "houseE038")
SCENARIOS = {"all": ALL, "furnish": ("myhouse",), "donate": ("museum",), "buy": ("shop",),
             "state": ("myhouse", "museum", "shop"), "state-check": ("myhouse", "museum", "shop")}
if scenario not in DOORS and scenario not in SCENARIOS and scenario != "town":
    sys.exit(__doc__)
furnish = scenario in ("furnish", "state")
donate = scenario in ("donate", "state")
buy = scenario in ("buy", "state")
opening = subprocess.run([sys.executable, str(Path(__file__).with_name("make_opening_script.py")), "--korean",
                          str(LOADED)], capture_output=True, text=True, check=True).stdout.splitlines()
ev += [l for l in opening if int(l.split()[0]) < LOADED and l.split()[1] not in ("QUIT", "SHOT")]
if scenario == "town":
    town_tour()
for name in SCENARIOS.get(scenario, (scenario,) if scenario != "town" else ()):
    visit(name)
if scenario in ("state", "state-check"):
    save_at_haniwa()
ev.append(f"{f + 60} SHOT i_{scenario}_end")
ev.append(f"{f + 61} QUIT")
ev.sort(key=lambda line: int(line.split()[0]))
print("\n".join(ev))
