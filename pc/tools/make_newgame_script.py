#!/usr/bin/env python3
"""Write an --input-script that starts a new game and walks into town.

Plays the opening (make_opening_script.py), walks off the station platform,
follows Tom Nook to the houses, enters and picks the house left of the
path and talks until the player is free to move (about frame 21500), then
runs extra STEPS. Run the game with --deterministic (the route depends on
the town layout) and preferably --script-fast.

A step is KEY:FRAMES (hold inputs such as SL, SD+A, START), A*:N (press A
N times, every 40 frames, screenshot every 4th press), WAIT:FRAMES
(screenshot after waiting) or SEEK:TARGET:FRAMES (walk to a villager by hex
id, or to a point X,Z; villagers wander differently from run to run). Every
step ends with a screenshot m<frame>; --pos also logs positions there.

--save then greets the villager next door (on day one the haniwa only saves
for a player who has met someone), walks back and saves at the haniwa
(save/card_a/DobutsunomoriE_MURA). make_opening_script.py 5200 loads it.

usage: make_newgame_script.py [--korean] [--pos] [--save] [STEP ...] > script.txt
"""
import subprocess
import sys
from pathlib import Path

ROUTE = ["SL:200", "SD:200", "SR:60", "SD:120",            # station platform to the stairs
         "A*:60",                                           # Tom Nook's welcome
         "SL:20", "SL:20", "SD:25", "WAIT:20", "SL:12",     # to the door of the house left of the path
         "WAIT:20", "SU:8", "A*:3", "WAIT:150", "A*:40",    # look inside
         "WAIT:100", "SD:60", "WAIT:200", "A*:200"]         # take it; Nook explains the job
START = 15600
SAVE = (["SR:80", "SD:40", "SR:80", "SR:30",               # east, to the next block
         "SEEK:E038:600", "A:6", "WAIT:30"]                 # say hello to Milk
        + ["B:6", "WAIT:30"] * 30                           # B ends the talk without starting another
        + ["SEEK:2128,1488:900", "SEEK:2085,1505:200", "SL:6"]  # back home, facing the haniwa
        + ["A:6", "WAIT:30"] * 20 + ["WAIT:600"])           # save and quit

args = [a for a in sys.argv[1:] if not a.startswith("--")]
korean = ["--korean"] if "--korean" in sys.argv else []
opening = subprocess.run([sys.executable, str(Path(__file__).with_name("make_opening_script.py"))] + korean + ["40000"],
                         capture_output=True, text=True, check=True).stdout.splitlines()
ev = [l for l in opening if int(l.split()[0]) < START and l.split()[1] not in ("QUIT", "SHOT")]

f = START
for step in ROUTE + (SAVE if "--save" in sys.argv else []) + args:
    key, _, n = step.rpartition(":")
    n = int(n)
    if key.startswith("SEEK:"):
        ev.append(f"{f} SEEK {key[5:]} {n}")
        f += n
        ev.append(f"{f} SHOT m{f}")
    elif key == "A*":
        for i in range(n):
            ev.append(f"{f} A")
            if i % 4 == 3:
                ev.append(f"{f + 30} SHOT m{f + 30}")
            f += 40
    elif key == "WAIT":
        f += n
        ev.append(f"{f} SHOT m{f}")
    else:
        ev.append(f"{f} {key} {n}")
        ev.append(f"{f + n} SHOT m{f + n}")
        f += n + 10
if "--pos" in sys.argv:
    ev += [f"{l.split()[0]} POS" for l in ev if l.split()[1] == "SHOT" and int(l.split()[0]) >= START]
ev.append(f"{f + 60} SHOT m_end")
ev.append(f"{f + 61} QUIT")
ev.sort(key=lambda l: int(l.split()[0]))
print("\n".join(ev))
