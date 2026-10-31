#!/usr/bin/env python3
"""Write an --input-script that plays the new-game opening.

Presses A through the dialogue and fills the three text editors (player
name, town name, town name in English), then START. By default each editor
gets two characters by holding the stick left and pressing A twice per
character. With --korean (for a Korean patch disc, where the editor takes
keyboard input) the names are typed with the TYPE command instead, with
screenshots after each step. Frame numbers were measured on the unpatched
disc; the editors appear around frames 5600, 6900 and 7800 and the train
reaches the station around 14700.

--idle presses nothing: the title screen waits and then plays the title
demo (the game in a town of its own), screenshots every 300 frames.

usage: make_opening_script.py [--korean] [--idle] [END_FRAME] > opening.txt
"""
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
KOREAN = "--korean" in sys.argv
END = int(args[0]) if args else 16000
EDITORS = (5800, 7100, 8000)
EDITOR_SPAN = 400

# (keys, screenshot name) typed one step every 30 frames from the editor's start
KOREAN_STEPS = (
    (("dkssud", "name_annyeong"), ("{BS}{BS}", "name_bs_jamo"), ("k", "name_moved_final"),
     ("{HAN}A", "name_english"), ("{BS}{BS}", "name_bs_char"), ("{ENTER}", "name_done")),
    (("ehdanf", "town_dongmul"), ("{ENTER}", "town_done")),
    (("Ami{SP}e", "eng_typed"), ("{ENTER}", "eng_done")),
)

# On a Korean patch disc the editors open earlier than EDITORS and stay open
# until the typed {ENTER}; A there would press a key of the on-screen keyboard
# (measured with --deterministic, after the A that opens each one)
KOREAN_OPEN = ((5100, 6000), (6900, 7200), (7700, 8100))

IDLE = "--idle" in sys.argv
ev = [] if IDLE else ["900 START", "1100 START"]
for f in range(1300, END if not IDLE else 0, 90):
    if any(e <= f < e + EDITOR_SPAN for e in EDITORS):
        continue
    if KOREAN and any(a <= f < b for a, b in KOREAN_OPEN):
        continue
    ev.append(f"{f} A")
for n, start in enumerate(EDITORS if not IDLE else ()):
    f = start
    if KOREAN:
        ev.append(f"{f} SHOT {['name', 'town', 'eng'][n]}_open")
        for keys, shot in KOREAN_STEPS[n]:
            f += 30
            ev.append(f"{f} TYPE {keys}")
            ev.append(f"{f + 10} SHOT {shot}")
        continue
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
