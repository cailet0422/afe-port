**English** · [한국어](multiplayer.ko.md)

# Multiplayer (no server)

Up to 8 players in one town at the same time, the way the later Animal Crossing games let friends visit: one player
hosts a room in their town, the others travel there by train, everyone sees everyone, and the town's items are the same
for all of them. There is no server of this project anywhere: the players' games talk to each other directly.

Code: `pc/src/mp/` — `pc_net.c` (UDP sockets), `pc_mp.c` (rooms, players, messages; no game code), `pc_dht.c`
(BitTorrent DHT lobby, STUN), `pc_upnp.c` (router port), `pc_mp_game.c` (the game side), `pc_mp_puppet.c` (the
other players as actors); the menu page `pc/src/gui/pc_gui_mp.cpp`; the game hooks in afe-decomp (`TARGET_PC`):
`m_field_info.c` (item and buried-item writes), `m_card.c` (the town as a card file), `m_player_lib.c` (faces of any
look), `m_actor_dlftbls.c` (the actor in the unused profile slot 185). Test: `pc/tests/test_mp.c`.

## Playing

Pause menu > **Multiplayer**.

- **Host**: room name, public or private, an optional password, 2–8 players, **Open the room**. The page shows the room
  code (`ABCD-EFGH-JKMNP`, with a Copy button), the players (with **Send out**), a chat, and what the internet side
  found (outside address, whether the router opened the port). Save the town once before hosting: the town file guests
  receive is the last save with everything as it is right now. Closing the room ends it for everyone.
- **Find a room**: the public rooms on the same network and on the internet, with their host, town, players and
  whether they want a password. **Join**.
- **Room code**: the code the host gave you, or an address `1.2.3.4:47070`, and the password if any.
- **Your name**: what others see (settings.ini `mp_name`); empty = your player's name.

After joining, the host's town arrives in a moment ("Ready!"). Then **go to your own train station and ask the station
master for a trip**: the game saves your town to slot A and takes you by train to the host's town, as e+ does with a
town on a memory card in slot B. In the host's town:

- the others walk around as themselves (their boy or girl model, face, tan and clothes) with their names over their
  heads; what someone types in the chat shows over their head for a few seconds;
- every change to the outdoor items is shared: dropping and picking up items, placing them, digging and filling holes,
  burying, planting saplings and flowers, shaking fruit down, chopping trees, pulling weeds, collecting shells...;
- guests follow the host's clock.

Coming home is the game's own trip home by train from the host's station: what you found there comes with you. Leaving
the room while you are in the host's town is fine: you stay in your copy of it and take the train home.

## How it works

### Rooms and messages (`pc_mp.c`)

One UDP socket per player (the host's on `mp_port`, default 47070). Every packet has a 16-byte header (magic `AFM1`,
type, flags, sender's slot, room id, sequence number, length). Messages that must arrive (joining, the player list,
item changes, chat, the town) are numbered per peer, acknowledged, sent again until acknowledged (from 250 ms, backing
off) and handed over in order; positions (15 a second) and pings may be lost. A guest talks only to the host, which
passes positions and messages on (a star: the host decides). A peer silent for 15 s is gone.

- Joining: `JOIN` with the protocol version, name, look and the password's SHA-256 (with the room id). The host answers
  `JOIN_OK` (slot and room) or `JOIN_DENY` (full, wrong password, other version, sent out, no such room).
- Sending out (kick): the host tells the guest, drops it and refuses its address for the rest of the room.
- Public rooms answer a broadcast to UDP 47071 on the LAN (also sent to 127.0.0.1 for a second copy of the game on the
  same machine). Private rooms answer only a request that carries their code.

### Over the internet without a server

- **STUN** (`stun.l.google.com:19302`, `stun.cloudflare.com:3478`) tells the host its outside address and port; the
  room code packs that address, the port and 16 bits of the room id (64 bits in Crockford base32).
- **UPnP IGD** (`pc_upnp.c`): the host asks the home router to forward its UDP port (SSDP, then SOAP AddPortMapping;
  taken away when the room closes). Routers without UPnP simply do not answer.
- **BitTorrent Mainline DHT** (BEP 5, read-only node of BEP 43, on the game's own socket so `implied_port` gives the
  address the game is reached at; bootstrap `router.bittorrent.com`, `dht.transmissionbt.com`, `router.utorrent.com`,
  `dht.libtorrent.org`):
  - public rooms announce themselves under a key everyone knows (`afe-port/v1/rooms`); the room list looks it up and
    asks each address for its room;
  - guests that are browsing announce themselves under `afe-port/v1/seek`; public rooms look that up every 15 s and send
    their room info to each, which opens the host's router for that guest (hole punching);
  - a guest joining by code announces itself under a key made from the code; the room looks it up every 8 s and sends a
    packet to each guest found, which opens its router for the guest's next `JOIN`.

  Keys are the first 20 bytes of a SHA-256 of the text. Joining over the internet can take up to half a minute while
  the DHT finds each side; on the same network it is immediate.

With two routers that change the port for every destination (symmetric NAT, some mobile networks) no direct path can
be made; then one side needs the port forwarded (UPnP or by hand).

### In the game (`pc_mp_game.c`, `pc_mp_puppet.c`)

- **The town**: when a guest joins, the host takes its own town file (`save/card_a/DobutsunomoriE_MURA`), puts the town
  as it is now into both save copies of it (`mCD_PC_SnapshotSave`: the save as the game writes it, with its checksum)
  and sends it (467 KB). The guest writes it to `save/card_b_net/` and points memory card slot B there
  (`pc_card_set_slot_b`), so the station's trip and the trip back (which writes the town back to slot B) never touch a
  real card B. Slot B is the real one again once the guest is home (or leaves the room before going).
- **Items**: afe-decomp calls `pc_mp_fg_written` from `mFI_UtNumtoFGSet_common` and `mFI_BlockUtNumtoFGSet` (the
  functions nearly all of the game's item changes go through) and `pc_mp_deposit_written` from the buried-item flags.
  Outdoors in the shared town, a guest's change goes to the host with the item the guest saw there; the host keeps it if
  the unit still holds that (or already holds the new one) and passes it to everyone, otherwise it sends the guest what
  is there. The game's own placeholder while an item drops or a hole opens (`RSV_NO`) is not sent; the change after it
  carries what was there before. Received changes are written into the save's item layer and the field is redrawn
  (`mFI_SetFGUpData`). A guest arriving gets the whole item and buried layer first (30 acres).
- **The clock**: the host sends its game time every 10 s; a guest in its town takes it (`lbRTC_SetTime`) when the minute
  differs.
- **Players**: a guest's or the host's position, facing, walk/run/still and look go out 15 times a second (only
  changes, at least twice a second). A player in the same town and scene is an actor of its own (profile slot 185,
  the shop mannequin's part, not the player's — the game takes the first actor of the player part for "the" player):
  the boy's or girl's skeleton with the standing, walking, running and dashing animations, and its own copies of the
  face (`mPlib_PC_LoadFace`) and clothing (`mPlib_Load_PlayerTexAndPallet`) textures; it follows the received position
  a little smoothed, blinks, and casts the player's shadow. A design of their own as clothing is drawn as the plain
  shirt (the picture is not sent).
- The menu draws the names (and chat balloons) at the head's projected position (`Game_play_Projection_Trans`).

## Limits

- One town is shared: the host's. Guests' towns are not visited (switch hosts for that).
- Villagers, insects and fish move on each machine by themselves; talking to a villager is your own conversation.
- Houses: everyone can go into buildings, and players in the same building see each other, but furniture moved inside a
  house is not shared, and the host's house shows the furniture as it was saved.
- Custom designs worn as clothes show as the plain shirt to others.
- Two players picking up the same item in the same instant can both get it (the host corrects the ground, not the
  pocket).

## Testing

`pc/tests/test_mp.c` (ctest `mp`) runs a host and a guest in one process over loopback: room codes, the bencode and
STUN parsers, finding a public room by broadcast, a wrong password refused, joining with 15% of all packets lost,
players and looks, positions, 50 messages in order, chat, a 200 KB town, sending out and not letting back in; and the
internet path against a fake DHT node: the host announcing itself, the room list finding it through the DHT, a join by
room code that announces itself for punching.

In the game, with two folders that each have a saved town (different town names) and `--deterministic --days 1` (not a
Sunday: the fishing tourney talks over the station), the input-script commands `MPHOST [private]`, `MPJOIN
code|ip:port`, `MPLEAVE`, `MPKICK slot`, `MPCHAT text` and `FG x,z item`: the guest joins, receives the town, walks to the
station (`WARP 2180,900`, `SEEK 2180,862`, A presses), saves its town to slot A, rides the train to the host's town
("동물마을에 어서 오세요!"), and both see each other with their names; items placed on either side appear on the
other.
