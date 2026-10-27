#!/usr/bin/env python3
"""Write an --input-script that plays the new-game opening.

Presses A through the dialogue and fills the three text editors (player
name, town name, town name in English) with two characters each by holding
the stick left and pressing A twice per character, then START. Frame
numbers were measured on the unpatched disc; the editors appear around
frames 5600, 6900 and 7800 and the train reaches the station around 14700.

usage: make_opening_script.py [END_FRAME] > opening.txt
"""
import sys

END = int(sys.argv[1]) if len(sys.argv) > 1 else 16000
EDITORS = (5800, 7100, 8000)
EDITOR_SPAN = 400

ev = ["900 START", "1100 START"]
for f in range(1300, END, 90):
    if any(e <= f < e + EDITOR_SPAN for e in EDITORS):
        continue
    ev.append(f"{f} A")
for start in EDITORS:
    f = start
    for _ in range(2):          # characters
        for _ in range(2):      # row, then vowel
            ev.append(f"{f} SL 40")
            ev.append(f"{f + 20} SL+A 6")
            f += 60
    ev.append(f"{f + 30} START")
for f in range(600, END, 300):
    ev.append(f"{f} SHOT f{f:05d}")
ev.append(f"{END} QUIT")
ev.sort(key=lambda line: int(line.split()[0]))
print("\n".join(ev))
