#!/usr/bin/env python3
"""Write an --input-script that does Nook's part-time job the game's way.

Starts from the save that make_newgame_script.py --korean --save leaves
(--deterministic, day one: the job has not begun) and runs one stage of the
job; each stage ends saving at the haniwa, so the next starts from its save
(copy save/ between runs). TALK, CHOOSE, WAITMENU and TALKEND (input script
commands) wait for the talks, so a stage keeps working when a line of text
takes a little longer; POS prints Nook's job number and its progress
(mQst_ERRAND_FIRSTJOB_*: 0 the uniform, 1 planting, 9 introductions ...).

  plant     job 0: Nook's uniform from the pockets (pick it up, put it on
            the player); job 1: the ten flower bags and saplings planted in
            rows south-east of his shop; back to him for the next job
  hello     job 9: hello to each villager and to Tortimer by the shrine, back
            to Nook (on plant's save)
  furniture job 2: the furniture Nook gives to E08C, back to Nook
  letter    job 3: a letter on Nook's stationery to Lucy, posted at the post
            office, back to Nook
  carpet    job 4: the carpet to E09D, back to Nook
  axe       job 5: the axe to E09E, back to Nook
  notice    job 6: a notice on the message board, back to Nook: the job is
            over (Nook greets the player as a shopkeeper the next time)

Run them in turn, each on the save the one before left (about a minute
each; the whole job about seven): plant, hello, furniture, letter, carpet,
axe, notice. Which villager each errand goes to and who is indoors depend on
the town and the hour: POS shows Nook's job and its recipient.

usage: make_job_script.py STAGE > script.txt
"""
import subprocess
import sys
from pathlib import Path

LOADED = 2700   # the load presses of make_opening_script.py end here
FREE = 3000     # the player can move (in front of the house, by the haniwa)
SHOP = (975, 1145)
SHOP_INSIDE = (160, 300)

ev = []
f = FREE
shots = 0
stage = ""


def shot(name, wait=10):
    global f, shots
    f += wait
    shots += 1
    ev.append(f"{f} SHOT j_{stage}_{shots:02d}_{name}")
    ev.append(f"{f} POS")


def do(cmd, wait=30):
    global f
    ev.append(f"{f} {cmd}")
    f += wait


def into_shop():
    do(f"WARP {SHOP[0]},{SHOP[1]} 4", 200)
    do("SU+SR 40", 350)
    shot("shop", 0)


def talk_nook():
    """Up to Nook in his shop (D019, by the counter at 140,220) and talk to
    the end"""
    do("TALKEND 900 120", 30)                   # his greeting at the door, if any
    do("SEEK 140,262 200", 210)
    do("SU 4", 20)
    do("A", 30)
    do("TALKEND 3000 60", 30)
    shot("nook", 0)


def out_of_shop():
    x, z = SHOP_INSIDE
    do("B", 30)
    do(f"SEEK {x},{z - 20} 300", 310)
    do("SD 120", 130)
    do("TALKEND 900 100", 30)
    shot("outside", 0)


def pocket(slot, choice):
    """START, the cursor to a pocket (5 in a row), A, the choice of its menu
    (0 집기 take, 1 the item's own: 심기 plant, 놓기 drop ...), A"""
    do("START", 100)
    for _ in range(slot % 5):
        do("SR 6", 30)
    for _ in range(slot // 5):
        do("SD 6", 30)
    do("A", 60)
    for _ in range(choice):
        do("SD 6", 30)
    do("A", 150)


def save_at_haniwa():
    do("WARP 2085,1505 6", 300)                 # facing west
    do("TALK 2000", 1)
    shot("haniwa", 0)
    do("CHOOSE 0 2000", 1)                      # 기록하고 그만둘래 (save and quit)
    do("TALK 2000", 1)
    shot("haniwa_sure", 0)
    do("CHOOSE 0 2000", 1)                      # 응 (yes)
    do("TALKEND 3000 300", 30)
    shot("saved", 900)


def plant():
    into_shop()
    do("TALKEND 3000", 30)                      # the uniform and how to put it on
    do("START", 100)
    do("A", 60)                                 # the uniform, first pocket: 집기 (take)
    do("A", 60)
    do("SU 6", 30)                              # onto the player
    do("SU 6", 30)
    do("A", 120)
    shot("dressed", 0)
    do("B", 60)
    do("B", 60)
    do("A", 60)                                 # Nook again: the next job, planting
    do("TALKEND 3000 60", 30)
    shot("job1", 0)
    out_of_shop()
    # ten bags and saplings in pockets 1-10: a row facing south at z 1260;
    # the first two places do not take them, so two more on the next row
    for k in range(1, 11):
        do(f"SEEK {1000 + 40 * (k - 1)},1260 200", 210)
        do("SD 4", 30)
        pocket(k, 1)                            # 심기 (plant)
    for i, k in enumerate((1, 3)):
        do(f"SEEK {1120 + 80 * i},1340 200", 210)
        do("SD 4", 30)
        pocket(k, 1)
    shot("planted", 0)
    into_shop()
    talk_nook()                                 # done: the next job
    shot("job2", 0)
    out_of_shop()
    save_at_haniwa()


# The villagers of that town by their houses (make_interior_script.py HOMES)
HOMES = {"E038": (2740, 1700), "E015": (3060, 1780), "E043": (3020, 2900),
         "E08C": (1140, 2380), "E09D": (780, 2780), "E09E": (1740, 3700)}


# Villagers indoors at that hour (POS shows them on their house's spot)
AT_HOME = ("E08C", "E09D", "E09E")


def hello():
    """job 9 (introductions): talk to every villager (from the south of
    each one's house, then walking up to them), then back to Nook"""
    for npc, (x, z) in HOMES.items():
        if npc in AT_HOME:
            # in the house: the door is the unit north of x,z-8 (A opens it)
            do(f"WARP {x},{z - 8} 4", 250)
            do("A", 10)
            shot(f"house_{npc}", 360)
            do(f"SEEK {npc} 300", 310)
        else:
            do(f"WARP {x},{z + 80} 4", 250)
            do(f"SEEK {npc} 900", 910)
        do("A", 30)
        do("TALKEND 3000 60", 30)
        shot(f"hello_{npc}", 0)
        if npc in AT_HOME:
            do("SEEK 160,280 300", 310)
            do("SD 120", 130)
            do("TALKEND 900 100", 30)
    # Tortimer, by the shrine while the job lasts (D07F, 2980,3620)
    do("WARP 2880,3520", 250)
    do("SEEK D07F 900", 910)
    do("A", 30)
    do("TALKEND 4000 60", 30)
    shot("hello_tortimer", 0)
    into_shop()
    talk_nook()
    out_of_shop()
    save_at_haniwa()


def deliver(npc, indoors=False):
    """Talk to a villager with Nook's errand: A through it (the first choice
    is the errand, the pockets open on its item, 건네기 gives it)"""
    x, z = HOMES[npc]
    if indoors:
        do(f"WARP {x},{z - 8} 4", 250)
        do("A", 10)
        shot(f"house_{npc}", 360)
        do(f"SEEK {npc} 300", 310)
    else:
        do(f"WARP {x},{z + 60} 4", 250)
        do(f"SEEK {npc} 900", 910)
    do("A", 30)
    do("TALKEND 4000 60", 30)
    shot(f"delivered_{npc}", 0)
    if indoors:
        do("SEEK 160,280 300", 310)
        do("SD 120", 130)
        do("TALKEND 900 100", 30)


def furniture():
    """job 2: the furniture to E08C (by his house at that hour), back to Nook"""
    deliver("E08C")
    into_shop()
    talk_nook()
    out_of_shop()
    save_at_haniwa()


def post_letter(paper_slot, up_to_top=True):
    """Write a letter on the stationery in a pocket to the first in the
    address book (Lucy, E09E in that town) and give it to Pelly"""
    do("START", 100)
    for _ in range(paper_slot % 5):
        do("SR 6", 30)
    do("A", 60)
    do("SD 6", 30)                              # 편지쓰기 (write a letter)
    do("A", 260)                                # the address first: the list
    do("A", 150)
    for _ in range(6 if up_to_top else 0):
        do("SU 6", 30)
    do("A", 150)
    do("TYPE dkssud{SP}fntl!", 20)              # 안녕 루시!
    shot("letter", 0)
    do("START", 80)
    for _ in range(3):                          # 괜찮아? 네
        do("A", 100)
    do("B", 60)
    do("B", 60)
    do("WARP 3380,1070 4", 200)                 # the post office
    do("SU+SR 8", 20)
    do("A", 370)
    do("SU 80", 90)                             # Pelly at the counter
    do("TALK 3000", 1)
    do("CHOOSE 0 3000", 1)                      # 편지를 보내고 싶어
    do("WAITMENU 3000", 60)
    shot("pocket_letter", 0)                   # the pockets open on the first letter
    do("A", 80)
    do("A", 80)                                 # 보냄 (send)
    do("TALK 3000", 1)                          # 또 다른 편지도?
    do("CHOOSE 9 3000", 1)                      # 이제 됐어
    do("TALKEND 3000", 30)
    shot("posted", 0)
    do("SEEK 160,280 300", 310)
    do("SD 120", 130)
    do("TALKEND 900 100", 30)


def letter():
    """job 3: a letter to Lucy (E09E) on Nook's stationery, posted"""
    post_letter(2)
    into_shop()
    talk_nook()
    out_of_shop()
    save_at_haniwa()


def carpet():
    """job 4: the carpet to E09D, back to Nook"""
    deliver("E09D")
    into_shop()
    talk_nook()
    out_of_shop()
    save_at_haniwa()


def axe():
    """job 5: the axe to E09E, back to Nook"""
    deliver("E09E", indoors="E09E" in ARGS)
    into_shop()
    talk_nook()
    out_of_shop()
    save_at_haniwa()


def notice():
    """job 6: a notice on the message board north of the houses (2220,1580),
    back to Nook"""
    do("WARP 2240,1640 4", 250)
    do("SEEK 2220,1620 100", 110)
    do("SU 4", 20)
    do("A", 90)                                 # the board
    do("A", 90)                                 # 글쓰기 (write)
    do("TYPE dkssudgktpdy!{SP}tofh{SP}dhs{SP}dksdlqslek.", 20)   # 안녕하세요! 새로 온 안입니다.
    shot("notice", 0)
    do("START", 90)
    do("A", 60)                                 # 괜찮아? 네 (as make_ko_edit_script.py boards)
    shot("posted", 30)
    do("A", 60)
    do("A", 60)
    do("B", 60)
    into_shop()
    talk_nook()
    out_of_shop()
    save_at_haniwa()


ARGS = sys.argv[2:]
STAGES = {"plant": plant, "hello": hello, "furniture": furniture, "letter": letter, "carpet": carpet, "axe": axe,
          "notice": notice}
stage = sys.argv[1] if len(sys.argv) > 1 else ""
if stage not in STAGES:
    sys.exit(__doc__)
opening = subprocess.run([sys.executable, str(Path(__file__).with_name("make_opening_script.py")), "--korean",
                          str(LOADED)], capture_output=True, text=True, check=True).stdout.splitlines()
ev += [l for l in opening if int(l.split()[0]) < LOADED and l.split()[1] not in ("QUIT", "SHOT")]
STAGES[stage]()
ev.append(f"{f + 60} SHOT j_{stage}_end")
ev.append(f"{f + 61} QUIT")
ev.sort(key=lambda line: int(line.split()[0]))
print("\n".join(ev))
