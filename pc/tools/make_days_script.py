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
  kk          Saturday night (--days 6 --time 20 on the letter run's save):
              ask K.K. by the station for a song (typed in Korean), his answer
  house       (--days 3 on the letter run's save) the loan paid at the post
              office, Nook's HRA talk, his offer of a bigger house; save
  house-next [PAYS [OFFER]]  each day after (--days 4, 5, ...): Nook tells of
              the bigger house, the loan paid (PAYS times 99999 bells), his
              next offer by its choices (0,9: the first, then nothing; 9:
              nothing, so no more building); save
  island-buy [PAYS]  the day the upper floor is built: pay it, and Nook sells
              an island (its name typed in Korean); save
  rooms       the house up to the upper floor and the basement: the stairs up,
              the room upstairs, down, the basement (on the island-buy save)
  island [CHOICES]  the next day: to the dock from the north, Kapp'n, the boat
              trip with his song, a walk on the island, and the trip back
  week        one run through the week, the clock moved on with CLOCK (an
              input script debug command) instead of a new run per day
              (--days 1, on the save make_newgame_script.py --save leaves):
              Monday the letter to Milk; on past midnight into Tuesday, save
              and quit at the haniwa and continue from the title (her answer
              is made when the town loads); 16:59, her letter in the
              mailbox; on through four more midnights to Saturday 20:00,
              K.K.'s song; save. POS prints the game's date (time ...)

Events, each on the save make_newgame_script.py --save leaves (the job is
still on there: NOJOB first). Where things stand is that town's: the lot the
broker's tent goes on, Franklin's block, the pond (POS prints the pool block,
the visitor's place and today's events with their blocks):

  broker      the broker's sale (--days 26: 11/27; the week's visitor is
              decided a day or two ahead, POS "special event 75"): CLOCK 18:01,
              when it opens; the tent goes on a lot out of sight (WARP home
              first), then in front of it: Redd at the door, into the tent
  harvest     the harvest festival (--days 25 --time 18:30, 11/26): the
              announcement at the station on the first scene change, Franklin
              (D08D) and his talk
  countdown   New Year's Eve (--days 60 --time 23:45): the announcement, the
              countdown by the pond (walked into from the north: the event's
              people are set while the block is out of sight), CLOCK 23:59:40,
              midnight: HAPPY NEW YEAR, fireworks, a villager's talk

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
INSIDE = {"post": (160, 300), "shop": (160, 300)}

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


def leave(name):
    """Out of a building the way the player came in; TALKEND also answers the
    player's thought outside after paying off a loan"""
    x, z = INSIDE[name]
    do("TALKEND 900", 30)
    do(f"SEEK {x},{z - 20} 300", 310)
    do("SD 120", 130)
    do("TALKEND 900 200", 30)
    shot("outside", 0)


def pay_loan(times=1):
    """At the post office: 입금하고 싶어 (pay into Nook's account), the most
    the wallet holds each time (BELLS 99999 first: the wallet holds no more)"""
    go_in("post")
    do("SU 80", 90)                             # to the counter
    for i in range(times):
        do("BELLS 99999", 10)
        do("TALK 3000", 1)
        do("CHOOSE 1 3000", 1)                  # 입금하고 싶어
        do("WAITMENU 3000", 60)                 # the amount: up to the most there is
        do("SU 6", 40)
        shot(f"repay{i}", 0)
        do("START", 120)
        do("TALK 1500", 1)                      # 그 밖에 또 다른 용건이 있으신가요?
        do("CHOOSE 9 1500", 1)                  # 아무것도 (nothing), or 9: the last
    leave("post")


def nook(choices, name="nook", typed=None):
    """Into Nook's shop and talk: the choices in turn (CHOOSE n each), then
    the talk to its end; with typed, the name editor that opens after the
    choices gets those keys and Enter (the island's name)"""
    do("WARP 975,1145 4", 200)
    do("SU+SR 40", 350)
    shot(f"{name}_in", 0)
    for c in choices:
        do("TALK 3000", 1)
        shot(f"{name}_q", 0)
        do(f"CHOOSE {c} 3000", 1)
    if typed is not None:
        do("WAITMENU 3000", 90)
        shot(f"{name}_editor", 0)
        do(f"TYPE {typed}", 20)
        shot(f"{name}_typed", 0)
        do("TYPE {ENTER}", 120)
    do("TALKEND 3000", 30)
    shot(f"{name}_end", 0)
    leave("shop")


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


def letter(save=True):
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
    if save:
        save_at_haniwa()


def midnight(day):
    """The clock to 23:59:40, then 40 seconds of the game on into the next
    day (the game reads the clock every frame: the date changes, the stalk
    market and the day's renewals run as when playing on at night)"""
    do("CLOCK 23:59:40", 30)
    shot(f"{day}_2359", 0)
    shot(f"{day}_past", 2400)


def continue_game():
    """After save and quit (writing the town takes a while): A through Milk's
    questions (who the player is, the town) until the town is loaded"""
    presses(40, every=90, name="load", each=8)
    shot("continued", 300)


def week():
    letter(save=False)                          # Monday, 2026-11-02
    midnight("mon")                             # into Tuesday
    save_at_haniwa()
    continue_game()                             # the town loads on Tuesday: Milk answers
    do("CLOCK 16:59", 30)
    shot("tue_1659", 0)
    reply()
    for _ in range(4):                          # the mailbox shut
        do("B", 40)
    shot("mailbox_shut", 30)
    for day in ("tue", "wed", "thu", "fri"):    # on to Saturday
        midnight(day)
    do("CLOCK 20:00", 30)
    shot("sat_2000", 0)
    kk()
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


def kk(keys="{HAN}K.K.{HAN}qhtkshqk"):
    """Saturday night (--days 6 --time 20 on a save after the job): K.K. by
    the station (D05D, 2220,940). Ask for a song in the request editor (the
    keyboard), hear his answer (the request echoed back, aMKBC_clip_set_itemstr3)
    and the song; the record he gives last"""
    do("WARP 2220,990 4", 300)                 # south of him, facing north
    shot("kk", 0)
    presses(6, every=80, name="talk")           # his greeting, the question
    shot("question", 60)
    presses(3, every=80, name="ask")            # 고마워요. / 내 뮤직의 제목을 말해 주면 ...
    do("A", 200)                                # 혹시, 듣고 싶은 곡 있나요?
    do("A", 90)                                 # its choices: 딱히 없어 / 있어 있어!
    shot("choices", 0)
    do("SD 4", 40)                              # 있어 있어!
    shot("chosen", 0)
    do("A", 150)                                # the editor opens
    shot("editor", 0)
    do(f"TYPE {keys}", 30)                      # K.K.보사노바
    shot("typed", 10)
    do("TYPE {ENTER}", 180)
    shot("asked", 0)
    presses(10, every=90, name="answer", each=2)
    for i in range(12):                         # the song (and the staff roll, the first time)
        shot(f"song{i}", 600)
    presses(16, every=90, name="after", each=2)


def house():
    """The loan paid at the post office, Nook's HRA talk (나한테 맡겨, then
    nothing more), back into his shop for his offer to enlarge the house:
    the roof's colour (the first), then nothing more; save"""
    pay_loan()
    nook([1, 9], "hra")
    nook([0, 9], "offer")
    save_at_haniwa()


def island_buy():
    """With the upper floor paid off, Nook offers an island: 사 볼래? (the
    last choice, yes), its name in the editor (Korean, the keyboard), then
    nothing more; save"""
    nook([9], "built")
    pay_loan(PAYS)
    nook([9], "island", typed="tjsfl")   # 선리: the editor starts in Korean
    save_at_haniwa()


def island():
    """The day after buying the island: to the dock from the north (the boat
    and Kapp'n are set out on entering the blocks north of it), talk to him
    (his first meeting, the island explained, then 섬에 갈래 – go), the boat
    trip with his song, the island, and the trip back"""
    do("WARP 3520,3760", 250)                  # the block north of the dock
    do("SEEK 3470,4240 400", 410)              # on the pier, by Kapp'n (D072, 3438,4230): close enough
    do("SL 6", 30)
    shot("pier", 0)
    for c in ISLAND_CHOICES:
        do("TALK 3000", 1)
        shot("kappn_q", 0)
        do(f"CHOOSE {c} 3000", 1)
    for i in range(24):                         # the trip: his song, the island
        do("A", 10)
        shot(f"trip{i}", 300)
    do("TALKEND 3000 200", 30)                  # on the island: off the boat
    shot("island", 0)
    for i, d in enumerate(("SL 120", "SL 120", "SU 120", "SL 120")):   # off the pier, west
        do(d, 130)
        shot(f"island_walk{i}", 0)
    # back: north of Kapp'n on the island's pier (he waits in the boat), home
    do("SEEK 3420,5370 500", 510)
    do("SEEK 3660,5262 300", 310)
    do("SD 4", 30)
    shot("back_pier", 0)
    for c in ISLAND_CHOICES:
        do("TALK 3000", 1)
        shot("back_q", 0)
        do(f"CHOOSE {c} 3000", 1)
    for i in range(24):                         # the trip back
        do("A", 10)
        shot(f"back{i}", 300)
    do("TALKEND 3000 200", 30)
    shot("town", 0)


def rooms():
    """In the house built up to the upper floor and the basement (scenes 40
    the main floor, 41 upstairs, 45 the basement): the stairs up are at the
    front left (100,370, walking south), down at the front right (300,370)"""
    do("WARP 2112,1485 5", 200)
    do("A", 10)
    shot("main", 360)
    do("SEEK 100,370 200", 210)
    do("SD 10", 400)                            # up
    shot("upstairs", 0)
    for i, d in enumerate(("SR 60", "SU 60", "SL 60")):
        do(d, 70)
        shot(f"up{i}", 0)
    do("SEEK 60,300 200", 210)
    do("SL 10", 400)                            # down
    shot("main_again", 0)
    do("SEEK 300,370 200", 210)
    do("SD 10", 400)                            # the basement
    shot("basement", 0)
    for i, d in enumerate(("SL 60", "SU 60", "SR 60")):
        do(d, 70)
        shot(f"bm{i}", 0)
    do("SEEK 300,380 200", 210)
    do("SD 10", 400)                            # up again (the basement is unlit)
    shot("back", 0)


def house_next():
    """The next day: Nook tells of the bigger house and its loan; then pay it
    (PAYS times, as much as the wallet holds each time), back to him for the
    next offer, and save"""
    nook([9], "built")
    pay_loan(PAYS)
    nook(OFFER, "offer")
    save_at_haniwa()


def announcement():
    """The first scene change of an event day shows its announcement at the
    station; A through it, back by the house"""
    do("NOJOB", 10)
    do("WARP 2128,1488", 400)
    for i in range(6):
        shot(f"announce{i}", 0)
        do("A", 300)


def broker():
    do("NOJOB", 10)
    do("CLOCK 18:01", 10)                      # the sale opens at 18:00
    do("WARP 2128,1488", 680)                  # home: the tent goes on a lot out of sight
    do("WARP 1420,1700 4", 400)                # south of the lot by the sign (1420,1580)
    shot("tent", 0)
    do("SU 30", 50)
    do("A", 240)
    shot("redd", 0)                             # 이야~, 어이쿠 이거 참! 손님, 어서옵쇼~~!
    do("TALKEND 3000 60", 10)
    shot("talked", 0)
    do("SU 120", 260)
    shot("door", 400)
    shot("inside", 0)                           # scene 12
    do("SU 60", 90)
    do("A", 290)
    shot("redd_inside", 0)                      # 자, 어서어서, 손님~!
    do("TALKEND 2000 60", 10)


def harvest():
    announcement()                              # 여러분~! 추수감사절이에요!
    do("WARP 3520,2880 4", 400)                # Franklin's block in that run
    shot("franklin", 0)
    do("SEEK D08D 600", 610)
    do("A", 270)
    shot("franklin_talk", 0)                    # 으아아아! 뭐, 뭐, 뭐야? ...
    do("TALKEND 3000 60", 10)
    shot("franklin_after", 0)


def countdown():
    announcement()                              # 여러분~! 새해맞이 카운트다운에 와 주세요~!!
    do("WARP 2240,1800", 400)                  # north of the pond's block (3,3)
    do("SEEK 2240,2100 400", 410)
    shot("pond", 0)
    do("CLOCK 23:59:40", 10)
    for i in range(8):                          # 1분 전이야!!! ... HAPPY NEW YEAR!!
        shot(f"midnight{i}", 300)
    do("SEEK D047 300", 310)
    do("A", 290)
    shot("villager", 0)
    do("TALKEND 3000 60", 10)


SCENARIOS = {"letter": letter, "reply": reply, "kk": kk, "house": house, "house-next": house_next,
             "island-buy": island_buy, "island": island,
             "rooms": rooms, "week": week, "broker": broker, "harvest": harvest, "countdown": countdown}
ISLAND_CHOICES = [int(c) for c in sys.argv[2].split(",")] if len(sys.argv) > 2 and sys.argv[1] == "island" else [0]
PAYS = int(sys.argv[2]) if len(sys.argv) > 2 else 1
OFFER = [int(c) for c in sys.argv[3].split(",")] if len(sys.argv) > 3 else [0, 9]
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
