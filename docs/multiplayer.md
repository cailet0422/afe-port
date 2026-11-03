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
  heads; what someone types in the chat shows over their head for a few seconds. The chat box opens while playing
  with the Chat control (**T**, Settings > Controls; a gamepad button can be bound too) or, on touch screens, the
  speech bubble button at the top (between the pad's Start and R). Enter sends and closes it, Esc closes it; while it
  is open the game keeps running but gets no input (`pc_pad.c`), and the touch pad hides. New lines show at the top for
  8 seconds (`pc/src/gui/pc_gui_mp.cpp`);
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
  room code packs the outside address, a port and 16 bits of the room id (64 bits in Crockford base32).
- **UPnP IGD** (`pc_upnp.c`): the host asks the home router to forward its UDP port (SSDP, then SOAP AddPortMapping;
  taken away when the room closes) and asks for the router's outside address. Routers without UPnP simply do not
  answer.
- **Which port the code holds**: the forwarded port when the router forwarded one and its outside address is the one
  STUN (or the DHT) sees, otherwise the port STUN saw. Some routers give the game's packets another outside port than
  the one they forward, so STUN's port alone can be the wrong one to give out. A router whose outside address is not
  the one the internet sees (behind another router, or behind the carrier's NAT, `100.64.0.0/10`) forwards a port
  nobody outside can reach: then the code keeps STUN's port, and the multiplayer page says so. Public rooms announce
  the same forwarded port to the DHT (`implied_port` 0), and the room looks for guests punching with a code of either
  port.
- **BitTorrent Mainline DHT** (BEP 5, read-only node of BEP 43, on the game's own socket so `implied_port` gives the
  address the game is reached at; bootstrap `router.bittorrent.com`, `dht.transmissionbt.com`, `router.utorrent.com`,
  `dht.libtorrent.org`):
  - public rooms announce themselves under a key everyone knows (`afe-port/v1/rooms`); the room list looks it up and
    asks each address for its room;
  - guests that are browsing announce themselves under `afe-port/v1/seek`; public rooms look that up every 15 s and send
    their room info to each, which opens the host's router for that guest (hole punching);
  - a guest joining by code announces itself under a key made from the code; the room looks it up every 8 s and sends a
    packet to each guest found, which opens its router for the guest's next `JOIN`. The guest then sends its `JOIN`
    to where that packet came from: the code's port may not reach the room (a router that does not really forward
    it, a Windows firewall closed to unasked packets) while this way, opened from the room's side, is open.

  Two STUN servers (Google's and Cloudflare's) are asked for the socket's outside address. If they see different
  ports, the router gives each destination its own port (a "symmetric" NAT): the room page and a failed join say so
  and point to UPnP or a forwarded UDP 47070, which no punching replaces. As such routers often take the next ports,
  the room's punch also goes to the 24 ports after the guest's known one, and every eighth `JOIN` of a guest also to
  the 24 ports after the code's.

  On Windows the firewall's first prompt allows the game on private networks only and adds a rule blocking it on
  public ones (a block rule wins over any allow rule); a guest's packets that come from another outside port than the
  room has seen (an endpoint-dependent NAT, many mobile networks) are then dropped. **Allow in Windows Firewall** on the
  room page (`pc_firewall.c`) runs, with the administrator's consent (`ShellExecuteExW` "runas"), `cmd /s /c "netsh
  advfirewall firewall delete rule name=all program=<the game> & netsh advfirewall firewall add rule ... dir=in
  action=allow program=<the game> protocol=UDP profile=any"`.

  On Windows, a UDP socket reports an ICMP "port unreachable" for an earlier packet as an error of the next `recvfrom`
  (`WSAECONNRESET`); the DHT sends to many nodes that are gone, so this is turned off (`SIO_UDP_CONNRESET`) and such an
  error does not stop the reading of the packets after it.

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
  a little smoothed, blinks, and casts the player's shadow. With the position go the place (the field id and, in a
  house, its owner: a puppet shows only to players in the same room), the animation the player plays
  (`animation0_idx`, any of the player's animations: picking up, shaking, fishing...) and its frame, which the puppet
  plays and catches up with when it is more than 4 frames off. A look changed in a room (other clothes) goes out at
  once (`T_LOOK`).
- **Designs** (`G_DESIGN`): every player sends its 8 designs (`my_org`: palette, name, 32x32 CI4 pixels) on joining
  and any that changes (compared every second); the host passes the guests' on and gives everyone's to a newcomer. A
  design worn as clothing goes in the look as `0xFD00 | slot`, and the puppet draws it with those pixels and palette
  (again when they change). A guest in the host's town also writes the host's designs into its copy of the town, where
  the house door, the wallpaper and carpet and design furniture read them; the room is built again if the guest stands
  in it. The house doors (`door_original`, the design on a player's door) go with the furniture.
- The menu draws the names (and chat balloons) at the head's projected position (`Game_play_Projection_Trans`).
- **Building placeholders**: while the actor of a building exists (a player's house, a villager's house, a mailbox, the
  shop...), the game keeps a placeholder in its unit (`DUMMY_HOUSE0`, `DUMMY_NPC_HOUSE_xxx`...) and puts the name back
  when the actor goes (`m_actor.c` `restore_fgdata`). Placeholders are each game's own: never sent or taken, and a town
  given whole (`mCD_PC_SnapshotSave`) or an item layer sent whole has the actor's name in their place. (Before, the
  host's placeholders reached guests, and guests saw no house where the host's stands.)
- **House furniture** (`homes[]`): in a house the save data is the room itself (its furniture actors are built from it
  when the room loads). Every 10 frames the host compares, for every floor, the items of layers 1 and 2
  (`layer_main`, `layer_secondary`) and the wallpaper and carpet (`wall_floor`, `floor_bit_info`) with what it sent, and
  sends what changed (`G_HOME`; an `RSV_NO` of an item in the air is left out). A guest gets all of it on arriving
  (`G_HOME_REQ`). Outside that room it writes it into its save at once; standing in it, it waits until the room is quiet
  (no menu, talk, held or reserved furniture, not sitting), then afe-decomp `ac_my_room.c` `aMR_PC_SyncRoom` takes the
  furniture away as the destructor does, writes what came, and builds the furniture again as the constructor does
  (`indoor_dma_again_proc` if the wallpaper or carpet changed). Only the owner can move the furniture of a house (the
  game's `aMR_CheckRoomOwner`), so nothing goes from guests.
- **Villagers** (afe-decomp `ac_npc_pc_mp.c_inc`): after each move the host reports every ordinary field villager
  (`mAc_PROFILE_NORMAL_NPC`) — position, facing, standing/walking/running, hidden (in its house), `is_home`, whether it
  is wandering (`pc_mp_npc_report`) — and sends all of them every 6 frames (`G_NPC`, unreliable): exactly those it has as
  actors, the others at their `npclist` position, each with its schedule of the moment (out, home, asleep...), which a
  guest holds its own to with the game's override (`mNPS_schedule_c` `forced_type`, renewed with every message). On a guest, after a villager's think (`aNPC_schedule_proc`) and before
  its action, it is asked to walk, run or stand toward the host's at priority 3 (`aNPC_set_request_act`: above
  wandering, below talking and greeting); only while it wanders, and it jumps there out of sight when more than 160
  away. A guest also keeps its villagers that are not actors at the host's positions and `is_home`, does not let the
  manager send them travelling or home by itself (`aSNMgr_walk_npc`, `aSNMgr_force_go_home`), brings out one the host
  has in its acre and it does not (`setupActor_proc`), and removes, out of sight, one it has that the host has in
  another acre.
- **Villagers in their houses** (afe-decomp `ac_npc2.c`, the indoor controller): the same hooks report and follow in
  room coordinates (`pc_mp_npc_room_report`, `pc_mp_npc_room_target`); the host sends the villagers of the house it
  is in every 6 frames with the house's owner (`G_NPC_ROOM`), and a guest in the same house makes its copy walk where
  the host's walks while it wanders the room. A villager that is out comes into its room on the host when the host
  walks in from the door (the game's own rule); on a guest it comes in when the host's does.
- **The rest of the town's data** (`G_REGION`): parts of the host's Save that everyone should see the same: the
  weather, the town tune, the museum, the noticeboard, the police station's lost and found, Nook's shop, the Able
  Sisters' display, the post office, the turnip prices, the visitors' events, the snowmen. The host compares them
  with what it sent every second and sends what changed (in 1000-byte pieces); a guest arriving gets all of them
  (`G_REGION_REQ`). What a visitor can change there (a post on the board, the town tune, a lost item taken, things
  bought or sold, a design on display, a letter sent) goes from a guest to the host while the guest is at that place
  (3 s after it last took the host's), and from the host to everyone.
- **Every item outside, again**: besides the changes the hooks see, the host compares the whole item and buried layer
  with what it sent every half second and sends the acres that differ (changes the game makes without the hooks: a
  day's new weeds, things the villagers or events put down); a guest stepping outside asks for the whole layer again.
- **Held items** (`pc_mp_puppet.c`): with the position go the upper body's animation (`animation1_idx`, its frame,
  `part_table_idx`), what the hand shows (`now_item_main_index`, the item's model and animation from
  `item_shape_type` / `item_animation_idx` of the current bank, its frame, `item_scale`, `net_angle` or the balloon's
  tilt and sway) and an item held up (picked up, dug up: the item, its size and its place from the player). The
  puppet combines the two body animations with the game's part table (`cKF_SkeletonInfo_R_combine_play`), keeps the
  matrix of the right hand while drawing (joint `mPlayer_JOINT_HAND`) and draws the item there as the player actor's
  `Player_actor_Item_draw_*` do: a display list (axe, shovel, fan, party popper, flower with its palette), the item's
  own skeleton (net with its angle, rod, pinwheel, balloon from the hand's position) or the umbrella as the umbrella
  tool actor draws it (a custom design from the owner's designs). Held-up items go through the field's or the shop
  goods' single-item draw.
- **The same item taken twice** (`G_ITEM_NO`): the host keeps who changed each unit lately; a guest's change that
  another made a moment before (the same unit, the same item to the same result), or that does not match the unit,
  is refused with what is there. The guest then takes one of that item back out of its pockets once it arrives
  there (or puts back into them an item it could not put down).
- **Inside the houses, more**: the storage layers (`layer_storage1/2`), the switches (`ftr_switch`), the gyroid steps
  and the tempo of each floor. While the host is in a room, afe-decomp `ac_my_room.c` writes the switches and steps
  into the save every half second (`aMR_PC_KeepState`; the room's constructor clears them there), and the host sends
  that floor with what storage furniture holds put back (`aMR_PC_StorageIntoCopy`: the constructor takes those items
  out of the save into the furniture actors).
- **More of the town** (`G_REGION`): each house's outside (size, roof colour, flags: `homes[].haniwa_tempo` to
  `door_original`), mailbox, door gyroid (the gyroid's stock and bells after a guest's purchase go to the host while the guest is outdoors), cockroaches and
  music collected; `animals[]`, `event_save_common`, the bridge, the lighthouse, the fishing records, Blanca, the
  returning villager, the town assessment, the rainbow, the insect and fish terms, the e+ villager additions,
  monuments, the sick villager and the town's flags.
- **Saving while visiting**: the port's autosave (`pc_autosave.c`) writes a visiting guest's pockets, bells and
  letters into the guest's own town file (`mCD_PC_PatchTraveller`), as the trip home would, so a guest whose game
  stops in the host's town keeps what it had.

## Limits

- One town is shared: the host's. Guests' towns are not visited (switch hosts for that).
- Villagers follow the host, but not exactly: only those the host has as actors (in the host's acre) have an exact
  position; for the others only the rough one the game keeps (the acre) is shared. Talking and greeting, going in and
  out of houses, sleeping and catching insects are each game's own. Villagers inside houses follow only while the host
  is in the same house. The island villager, insects and fish are not shared.
- Each player's own: insects and fish (spawned by each game's set manager for the acre it enters), balloons floating
  by, Wisp's spirits, rolling snowballs, a beach ball, the money rock's coins, bees, and the island (its villager, items
  and weather). The fishing bobber and line, and a letter or present handed over, are not drawn on puppets.
- A guest can turn a lamp in the host's house on or off on its own screen; it follows the host's again when the
  host's changes.

## Testing

`pc/tests/test_mp.c` (ctest `mp`) runs a host and a guest in one process over loopback: room codes, the bencode and
STUN parsers, finding a public room by broadcast, a wrong password refused, joining with 15% of all packets lost,
players and looks, positions, 50 messages in order, chat, a 200 KB town, sending out and not letting back in; and the
internet path against a fake DHT node: the host announcing itself, the room list finding it through the DHT, a join by
room code that announces itself for punching; with a forwarded port, the code and the DHT announce carrying it, and a
router behind another router being ignored.

In the game, with two folders that each have a saved town (different town names) and `--deterministic --days 1` (not a
Sunday: the fishing tourney talks over the station), the input-script commands `MPHOST [private]`, `MPJOIN
code|ip:port`, `MPLEAVE`, `MPKICK slot`, `MPCHAT text` and `FG x,z item`: the guest joins, receives the town, walks to the
station (`WARP 2180,900`, `SEEK 2180,862`, A presses), saves its town to slot A, rides the train to the host's town
("동물마을에 어서 오세요!"), and both see each other with their names; items placed on either side appear on the
other.
