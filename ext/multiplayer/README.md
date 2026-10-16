# Multiplayer

Up to four players in one game: one hosts, opening their game and their save to the others, who join it by address and port.
Everyone plays the host's level together and makes progress on the host's save.

- The host is Mario. The others are Luigi, Wario and Metal Mario, in the order they join; each sees the others, and themselves, as their character.
- Shines, blue coins, red coins, coins and lives are shared: what anyone collects counts for everyone, and the host's save keeps it.
- Coins and items one player takes disappear for everyone, including those that appear during the level (from a box, a defeated enemy or a switch): those are matched by type and place, and one that appears in a game after someone took theirs is gone as it appears, so nobody collects one twice.
- A switch pressed in any game (a red coin switch, a switch that throws out hidden coins) is pressed in every game, without taking the others' controls or camera.
- When the shared coin count passes 100, the 100-coin Shine comes out in every game, beside each player, as it does for the player whose coin makes exactly 100.
- Enemies are the host's: everyone sees each enemy where the host's game has it, and an enemy defeated in any game is defeated in all (and comes back when it comes back for the host). While the host's game is paused, the others' enemies move on their own again.
- Cutscenes are watched together: when one starts in any game (a switch's camera, an event's), everyone is taken to the player who started it and their game plays the same camera. Not the level's opening (every game plays its own) or a gate's camera (it follows the gate).
- Bosses are shared (`game/mp_boss.cpp`): the host's boss is the real one, every other game's copy takes each step of the host's fight when the host's does and follows its position and the state of the fight, and a hit landed in any game counts on the real one. When it goes down, it goes down in every game and its Shine comes out for everyone. Petey Piranha (water in his mouth, ground pounds on his belly), Gooper Blooper (eyes, beak, torn tentacles, his ink and attack mode), Eely-Mouth (each tooth's dirt), Phantamanta (every piece: its size, hits and place), Wiggler (its hit points and which segment can be hit), King Boo (spicy or not, fruit hits), Mecha-Bowser (plates and their hits; each player rides their own coaster), Bowser's hot tub (each corner, broken in every game; the last starts the ending everywhere) and Shadow Mario's chases (where he is, his water meter, stomps).
- Water sprayed by any player is sprayed in every game: it washes goop away and hits enemies everywhere.
- One level at a time, the host's. A player who joins goes straight to it, with no file to choose. Only the host leads the group to another area: as the host sets off (a pipe, a painting, a door), everyone sets off with them, so the fades go together; when the others take a way out themselves, they come back to the start of the host's level. A Shine touched by anyone is got by everyone: every player is taken to it and their game plays the whole Shine Get (the camera, the pose, "SHINE!", the fanfare and the fade); the host then gets the save prompt, and the others wait for them in the plaza.
- Moving to another level fades out and in for everyone, as the game's own trips do, with the new level's opening.
- Losing a life plays the usual death, "TOO BAD!" and its jingle, then puts that player back at the start of the same level, alive and keeping the coins, instead of sending them to Delfino Plaza. Lives are shared, so every player's deaths count against them.
- With no lives left, a death shows the game's GAME OVER, but the player is not sent to the title: they come back to the level watching the others, the camera riding with one player at a time (L or R, or left and right on the D-pad, to watch another). A 1-up brings players back: each player watching takes one of the shared lives as it comes and appears beside the player they watched (lower slots first). When everyone is out of lives, the level starts again for everyone with three lives; when the host moves on to another level, everyone goes along, alive.
- Players see each other's position, facing, animation (pumping FLUDD and the nozzle in use included), cap, hands and Yoshi.
- Save states are off while playing together.

It is peer to peer: no server, the joining games talk straight to the host's.
The host must let the others reach that port: forward it on their router (UDP, 24002 unless another is chosen) to their computer.

## Starting it

SMS Launcher has a **Multiplayer** button beside Settings: **Host Game** starts the game hosting, **Join** first checks that a game answers at the address and port, then starts the game to join it.
Without the launcher, these variables do the same:

| Variable | Effect |
| --- | --- |
| `SMS_MULTIPLAYER=host` | host, on `SMS_MULTIPLAYER_PORT` (24002 by default) |
| `SMS_MULTIPLAYER=join` | join `SMS_MULTIPLAYER_ADDRESS` (`host`, `host:port`, `[ipv6]:port`) |
| `SMS_MULTIPLAYER_NAME=name` | the name the others see (23 characters) |
| `SMS_MULTIPLAYER_TIMEOUT=s` | seconds of silence before a player counts as gone (default 10) |
| `SMS_MULTIPLAYER_STATUS=0` | no player list in the corner (the notices still show) |

A joining player's game keeps its own memory card out of it: the launcher gives it a `multiplayer-guest` card folder, since what it plays is the host's save.
Both games must be the same build (the launcher's game version); a different one is refused with a message saying so.

## How it works

The extension has three parts, kept apart so the networking can be tested, and replaced, without the game:

| Folder | What it does |
| --- | --- |
| [`net/`](net/mp_net.h) | the network: one UDP socket, players' slots, a reliable ordered channel, the host relaying each player's state to the others. A thread of its own keeps everyone connected through loading screens. Knows nothing of the game. |
| [`sync/`](sync/mp_controller.cpp) | the rules ([`mp_game_api.h`](mp_game_api.h)): stage sessions, the shared flags' merge ([`mp_flags.h`](sync/mp_flags.h)), items taken, players' looks as packets. Knows the game only as numbers. |
| [`game/`](game/mp_game.cpp) | the game's side, built as game code: reads and writes `TFlagManager`, removes items, warps, and draws the other players ([`mp_puppet.cpp`](game/mp_puppet.cpp)). |

**Packets.** Every packet starts `SMMP`, the protocol version and a type: `PROBE`/`INFO` (the launcher's check), `CONNECT`/`CHALLENGE`/`ACCEPT`/`REJECT` (joining), `DATA` about 30 times a second both ways (the newest player states, and every reliable message the other side has not acknowledged, resent every 150 ms until it is), and `BYE`. Reliable messages that do not fit beside the states and the world snapshot go in packets of their own, so a big one (the whole save, for a player joining a well-played file) never waits.

**Packets from strangers.** The host's port is open to the internet, so:
- a `PROBE` shorter than 200 bytes gets no answer, and an `INFO` is always shorter, so nobody can use the host to send someone else more than they sent;
- a slot goes only to a `CONNECT` that echoes the cookie the host sent to its address in a `CHALLENGE` (a keyed hash of the address, the player's number and the time), so a packet with a forged sender address cannot take a slot, kick a player or make the host send to someone else;
- the network thread reads at most 64 packets a pass, without the lock the game thread needs, so a flood does not hold up the host's frames; a player whose messages the game cannot keep up with, or who sends far ahead of what has arrived, is dropped or ignored;
- every value a player reports into the shared save stays within what the game can hold (coins and best coins 0 to 999, red coins 0 to 99, lives -1 to 99), so a changed game cannot write huge numbers into the host's save. It can still claim Shines it did not get: the host trusts its friends' games.
A client sends the host its own state; the host sends each client everyone else's.

**The save.** The host's flags are the truth. Each frame a client reports what changed in its own game since the last frame; the host merges that into its game and sends everyone the new values. How a flag merges depends on what it is: a Shine, blue coin, nozzle or story flag, once anyone has it, everyone has it; coins and red coins add everyone's pickups, but a client's own reset (losing a life) is not passed on; lives add up gains and losses; a world's best coin count keeps the highest. Until the host answers, a client keeps its own pickups on top of the host's values, so a coin never visibly comes and goes. The Shine and blue coin totals are counted from their flags.

**Stages.** Each time the host starts playing a stage it opens a stage session and sends it, with its flags, to everyone. A client in another session loads the host's stage with the host's flags first, so it loads as the host's did (collected blue coins and Shines are gone). Items are named by their index among the item manager's objects, the same in every game that loaded the same stage; the host keeps the session's list of items taken, and a player who reloads the stage gets it again.

**Enemies.** The stage's ordinary enemies (`TSmallEnemy`; bosses keep their own) are named by their manager's place in the conductor's list and their index there. Each update the host sends a share of them, where each is and whether it is up, through the session's world snapshot (unreliable, like players' states; every enemy goes round several times a second). After every movement tick a client eases its enemies to the host's positions and facing, defeats the ones the host's game has defeated (with their death if it just happened there, quietly if its own spawner had brought one out the host's had not), and brings back, with the game's own respawn (`resetSRTV`), the ones that are back on the host. A client's own defeats go to the host as reliable messages, and the host defeats them there.

**Deaths.** Losing a life runs the game's own miss sequence; the `missNextArea` hook sends the player back into the same stage (the same session: the stage's taken items and red coins stay as they were). Lives are a shared flag that can reach -1, used up; a death then takes the `takeGameOver` hook's way: the game over plays, the stage reloads, and the player is out (their state says so): Mario is out of play (`MARIO_FLAG_IS_PERFORMING`, no collision) and is moved each tick to the player watched, whom the camera then follows. When every player in the game is out, the host sets the lives to 3 and loads the stage again as a new session, which brings everyone back in.

**Water.** While a player sprays, their state carries where FLUDD's water leaves, its direction and the trigger's pressure; every other game emits the same nozzle's water there, through the water manager as FLUDD does, so it cleans and hits there too.

**Smooth movement.** Poses are stamped with the sender's game time and every one that arrives is kept. Each game draws the others a little in the past, about two and a half of their updates behind their newest pose, between the two poses either side of that moment: position and facing (the turn interpolated as a quaternion), animation frames (across a loop's end), blends, FLUDD's and Yoshi's frames. The moment drawn moves on by a steady amount each frame, the other game's time per frame of ours measured over many frames, and leans gently toward where it should be, so others move evenly even when updates arrive in bursts, frames vary in length, or the two games run at different speeds.

**Players' looks.** Each player sends their body's root matrix, its animation, frame and blend, the upper body's (pumping), the face's texture pattern, the hands and shapes shown, the cap, FLUDD's animation and nozzle, and Yoshi's animation and colour. The others' games draw a puppet in the stage's player group, beside Mario, under his light: Mario's own model with his texture recoloured for Luigi (green), Wario (yellow, with Mario's blue overalls) and Metal Mario (silver), each from its own copy of the files. A player who is not Mario sees themselves the same way: the game leaves out Mario's own models and draws theirs.

## Testing

```sh
make -C ext/multiplayer/net/tests run     # a host and clients as separate processes, a third of all packets dropped
make -C ext/multiplayer/sync/tests run    # the shared flags' merge rules
make -C ext/multiplayer/tools             # mp_bot, a stand-in player
python3 ext/multiplayer/tools/mp_shots.py --disc GMSE01.iso [--client] [--bots 3] [--turn]
```

`mp_bot` joins a host through the same controller the game uses, follows it from stage to stage, and stands beside the host doing what the host's Mario does (`--yoshi N` rides Yoshi, `--spray` pumps FLUDD, `--coins N` reports coins picked up), so one game shows every character.
`mp_shots.py` runs a hosting game headless, scripted into Delfino Plaza, with bots and optionally a second, joining game (`--client`), and writes both screens' captures to `build/multiplayer-shots/`.
`--test "take@N:K;kill@N:K;die@N:L;warp@N:area,episode;shine@N:ID;flags@N:K;lives@N:L;coins@N:K;drop@N;takedrop@N;touchshine@N;bosswater@N:K;bossdown@N;bosshit@N;setflag@N:ID"` (`SMS_MULTIPLAYER_TEST`, `--client-test` for the joining game) takes K coins, defeats K enemies, loses a life (with L lives left first, `die@N:0` to run out), leaves for another area, gets Shine ID's flag (on a client: the host then plays the Shine Get and leaves with everyone) sets K save flags after the Shines (a well-played save, whose full update is bigger than a packet), sets the lives to L (a 1-up), sets the coins to K, makes a Shine appear by the player and touches it (`touchshine@N`), sprays Petey's mouth K times (`bosswater@N:K`) or ground-pounds him down (`bossdown@N`), lands a hit on this game's copy of whatever boss is here (`bosshit@N`), sets a game flag (`setflag@N:ID`, hex, e.g. 50007 starts Phantamanta's fight), makes a coin appear at a fixed place near the plaza's start and takes the coins that appeared during the stage (these two counting frames with another player in sight, so both games do it together), N frames into each stage; `SMS_MULTIPLAYER_TRACE=1` logs each other player's drawn and newest position every frame (and every pose received), for measuring smoothness; `SMS_MULTIPLAYER_DEBUG=1` logs every flag reported and merged, some enemies' positions in both games, and every enemy defeated or brought back by the other games' word.
`--spray` has a bot pump FLUDD with water.
Each game runs under a 2.5 GB memory cap.

## Limits

- Each other player's model takes about 450 KB of the stage's memory. Every stage measured has some 44 MB to spare in the port (its arena is larger than the console's), so all four always fit.
- Enemies chase and attack only the host's Mario in the host's game; in another game they go where the host's game sends them, and still hurt the player there when they touch.
- Bosses, switches and the like are each game's own; the save's flags, the stage, items taken, enemies and water are shared.
