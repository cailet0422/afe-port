#!/usr/bin/env python3
"""Write an --input-script that starts a new game and walks into town.

Plays the opening (make_opening_script.py), walks off the station platform,
follows Tom Nook to the houses, enters and picks the house left of the
path and talks until the player is free to move (about frame 21500), then
runs extra STEPS. Run the game with --deterministic (the route depends on
the town layout) and preferably --script-fast.

A step is KEY:FRAMES (hold inputs such as SL, SD+A, START), A*:N (press A
N times, every 40 frames, screenshot every 4th press) or WAIT:FRAMES
(screenshot after waiting). Every step ends with a screenshot m<frame>.

usage: make_newgame_script.py [--korean] [STEP ...] > script.txt
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

args = [a for a in sys.argv[1:] if not a.startswith("--")]
korean = ["--korean"] if "--korean" in sys.argv else []
opening = subprocess.run([sys.executable, str(Path(__file__).with_name("make_opening_script.py"))] + korean + ["40000"],
                         capture_output=True, text=True, check=True).stdout.splitlines()
ev = [l for l in opening if int(l.split()[0]) < START and l.split()[1] not in ("QUIT", "SHOT")]

f = START
for step in ROUTE + args:
    key, n = step.split(":")
    n = int(n)
    if key == "A*":
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
ev.append(f"{f + 60} SHOT m_end")
ev.append(f"{f + 61} QUIT")
ev.sort(key=lambda l: int(l.split()[0]))
print("\n".join(ev))
