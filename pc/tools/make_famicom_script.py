#!/usr/bin/env python3
"""Write an --input-script that plays a Famicom game from a saved town.

Starts from the save that make_newgame_script.py --save leaves (load it with
the same build and --deterministic): the player continues in front of the
house, gets a Famicom (the GIVE debug command), walks in, places it from the
pocket (배치하기), faces it and presses A, answers 놀자!, reads how to quit,
starts the game with START on its title, moves around, quits with L+R+Z and
is back in the room. Every step leaves a screenshot fc_<n>_<step>.

  --game N    which of the common Famicom furniture (FTR_FAMICOM_COMMON00 +
              N, item 1DE8 + 4N; 0 is Clu Clu Land, the timings are its)
  --play F    frames of play before quitting (default 900)

With --audio-dump the NES sound replaces the town's music from about frame
5400 (the title) until the quit; audio_stats.py --seg nes:5600-6700 looks
at it.

usage: make_famicom_script.py [--game N] [--play F] > script.txt
"""
import argparse
import subprocess
import sys
from pathlib import Path

LOADED = 2700   # the load presses of make_opening_script.py end here
FREE = 3000     # the player can move (in front of the house, by the haniwa)

ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
ap.add_argument("--game", type=int, default=0)
ap.add_argument("--play", type=int, default=900)
args = ap.parse_args()

ev = []
shots = 0


def shot(f, name):
    global shots
    shots += 1
    ev.append(f"{f} SHOT fc_{shots:02d}_{name}")


opening = subprocess.run([sys.executable, str(Path(__file__).with_name("make_opening_script.py")), "--korean",
                          str(LOADED)], capture_output=True, text=True, check=True).stdout.splitlines()
ev += [l for l in opening if int(l.split()[0]) < LOADED and l.split()[1] not in ("QUIT", "SHOT")]

ev.append(f"{FREE + 10} GIVE {0x1DE8 + 4 * args.game:04X}")
ev += ["3040 SL 30",        # onto the doorstep
       "3080 SU 8",         # face the door
       "3110 A"]            # in
shot(3390, "room")
ev += ["3400 SU 20",        # a step into the room
       "3440 START",        # the pocket
       "3550 A"]            # the Famicom's menu: 집기 / 배치하기 / 그만둘래
shot(3610, "menu")
ev += ["3620 SD 8",         # 배치하기
       "3670 A"]            # placed in front of the player (up and to the right)
shot(3860, "placed")
ev += ["3870 SD 8",         # back a step,
       "3890 SR 6",         # half a tile right, below it
       "3910 SR 6",
       "3930 SU 12",        # facing it
       "3960 A"]            # a short press uses it
shot(4150, "question")      # ...를 플레이할까?
ev.append("4160 A")
shot(4230, "choices")       # 놀자! / 어드벤스로 놀기 / 안 놀래
ev.append("4240 A")
shot(4320, "how_to_quit")   # L, R and Z quit
ev += ["5000 START",        # through the explanation
       "5300 A"]            # the emulator starts (about 5376)
shot(5500, "title")
ev.append("5520 START 6")   # the game
f = 5900
for i in range(args.play // 100):
    if f % 300 == 0:
        shot(f, "play")
    ev.append(f"{f} {('SR', 'SD', 'SL', 'SU')[i % 4]} 40")
    f += 100
shot(f, "play")
quit_at = f + 100
ev.append(f"{quit_at} L+R+Z 20")
shot(quit_at + 60, "quitting")
shot(quit_at + 400, "back")
shot(quit_at + 900, "room_again")
ev.append(f"{quit_at + 901} QUIT")
ev.sort(key=lambda l: int(l.split()[0]))
print("\n".join(ev))
