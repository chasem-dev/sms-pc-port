#ifndef SMS_MP_GAME_API_H
#define SMS_MP_GAME_API_H

// Between the multiplayer extension's two halves: the game half
// (ext/multiplayer/game, game code built with the game's flags) and the
// controller (ext/multiplayer/sync/mp_controller.cpp, platform code with
// the sockets, the save file merge and the session's rules). Plain C types
// only: the two are compiled with different compilers' settings.
//
// The rules the controller keeps:
// - Slot 0 is the host (Mario); the others' characters are mpg_character's.
// - One stage at a time: the host's. Each time the host starts playing a
//   stage it opens a new "stage session"; clients warp into it, taking the
//   host's flags first so the stage loads as the host's did. Anyone's way
//   out (a pipe, a painting, a secret course's entrance) takes everyone: a
//   client's is asked of the host, who sets off with everyone. A client's
//   own lost life, Shine or pause menu exit bring it back to the host's stage.
// - Coins and items taken from the stage disappear for everyone in the
//   session; a player who reloads the stage gets the session's list again.
// - A Shine collected by anyone sends the whole group out, as it does alone.
// - Enemies are the host's: clients put them where the host's game has them,
//   and an enemy defeated in any game is defeated in all.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	MPG_ROLE_OFF = 0,
	MPG_ROLE_HOST,
	MPG_ROLE_CLIENT,
};

// Players a game can have at most (the host picks how many it takes, 2 to
// this; MP_MAX_PLAYERS in net/mp_net.h is the same).
#define MPG_MAX_PLAYERS 8

// Each player's character, by slot: the first four, then Marios in colours of
// their own.
enum {
	MPG_CHAR_MARIO = 0,
	MPG_CHAR_LUIGI,
	MPG_CHAR_WARIO,
	MPG_CHAR_METAL_MARIO,
	MPG_CHAR_FIRE_MARIO,   // white cap and shirt, red overalls
	MPG_CHAR_PURPLE_MARIO, // purple, dark overalls
	MPG_CHAR_ORANGE_MARIO, // orange, brown overalls
	MPG_CHAR_ICE_MARIO,    // light blue, white overalls
};

// What a player looks like this frame, enough to draw them elsewhere.
enum {
	MPG_POSE_VISIBLE   = 1,
	MPG_POSE_FLUDD     = 2,  // wearing FLUDD
	MPG_POSE_SPRAYING  = 4,  // FLUDD emitting water
	MPG_POSE_YOSHI     = 8,  // riding Yoshi
	MPG_POSE_CAP       = 16, // the cap on (else the capless hair)
	MPG_POSE_UPPER     = 32, // the upper body has its own animation (pumping, carrying)
	MPG_POSE_SPECTATING = 64, // out of lives, watching the others
	MPG_POSE_DEMO       = 128, // in a cutscene
	// MpgPose::more
	MPG_POSE2_ON_TUB    = 1,   // standing on Bowser's hot tub (it tips under each player)
	MPG_POSE2_SURF      = 2,   // riding a Blooper (its colour: MPG_POSE2_SURF_TYPE)
	MPG_POSE2_SURF_TYPE = 12,  // 0 red, 4 yellow, 8 green (TMario::mSurfGessoType << 2)
	MPG_POSE2_PAUSED    = 16,  // the game is paused (the host's: the others' worlds run on their own)
	MPG_POSE2_COASTER   = 32,  // riding Pinna Park's coaster car (Mecha-Bowser's fight: TMario::mTorocco)
};
// One of Mario's effects as his game asked for it (TMarioParticleManager's
// emit and its kin: port_ext.h's particleEmit), for the others to show at
// this player.
enum {
	MPG_FX_BIND_NONE  = 0, // where it was, from the player's root (a matrix: and its turn)
	MPG_FX_BIND_ROOT  = 1, // follows the player (bound to Mario's position)
	MPG_FX_BIND_BODY  = 2, // follows a joint of the body (`joint`)
	MPG_FX_BIND_FLUDD = 3, // follows a joint of FLUDD (`joint`)
};
typedef struct MpgFx {
	uint16_t id;
	uint8_t how;      // particleEmit's: 0 emit, 1 with a turn, 2 bound to a position, 3-5 to a matrix
	uint8_t group;    // the particle manager's: 0, 2 once; 1, 3 every frame while it goes on
	uint8_t bind;     // MPG_FX_BIND_*
	uint8_t joint;
	int16_t off[3];   // from the player's root, world axes
	int8_t turn[4];   // with a turn (how 1): rx, ry, rz in 256ths of a full turn; a matrix's: its quaternion (x 127)
} MpgFx;
#define MPG_MAX_FX 4

typedef struct MpgPose {
	uint32_t time;      // when the sender's game was like this, in its game time (ms; set by mpg_set_pose)
	uint32_t session;   // the stage session the player is in (0: none)
	uint8_t flags;      // MPG_POSE_*
	uint8_t hand;       // which hand models (TMario::changeHand)
	uint8_t nozzle;     // TWaterGun::mCurrentNozzle
	uint8_t btp;        // the face's texture pattern
	float mtx[3][4];    // the body's root matrix
	uint16_t bodyBck, upperBck;
	float bodyFrame, bodyBlend, upperFrame, btpFrame;
	uint16_t fluddBck; // FLUDD body pose
	uint16_t yoshiBck;
	float fluddFrame, yoshiFrame;
	uint8_t yoshiColor[3];
	uint8_t more;       // MPG_POSE2_*
	uint32_t hiddenShapes; // the body's shapes hidden (bit n: shape n): shirt, hands, hair
	// what Mario's joint callbacks add to the animation (MarioDraw.cpp): the
	// head's turn (looking, FLUDD's aim) and the waist's (FLUDD's aim, the
	// lean while running), quaternions (x, y, z, w) in 16-bit fractions; and
	// FLUDD's own (TWaterGun's): the nozzle's aim (TNozzleBase::getGunAngle,
	// also Yoshi's head), the hover nozzles' tilts and the Turbo nozzle's
	// propeller turn
	int16_t headTurn[4], waistTurn[4];
	int16_t gunAngle, hoverTiltL, hoverTiltR, propAngle;
	int16_t coasterAngle; // how the rider turns on the coaster car (TMario::mToroccoAngle)
	// their effects: those asked for every frame while they go on (a slide's
	// spray), as of their last tick; and the newest asked for once (a
	// landing's dust), the last of them numbered shotSeq, so each is shown once
	uint8_t fxCount, shotCount, shotSeq;
	MpgFx fx[MPG_MAX_FX];
	MpgFx shots[MPG_MAX_FX];
	// while spraying: where FLUDD's water leaves (up to two emitters, the
	// hover nozzle has two), the speed it inherits, and the trigger's pressure
	uint8_t emitters;
	uint8_t pad2[3];
	float emitPos[2][3], emitDir[2][3], emitSpeed[3];
	float pressure;
} MpgPose;

// An enemy as the host's game has it, named by its manager's place in the
// stage's list of enemy managers and its index there (the same in every game
// that loaded the stage).
typedef struct MpgEnemy {
	uint8_t manager;
	uint8_t alive;
	uint16_t index;
	float pos[3];
	int16_t rotY; // degrees * 65536 / 360
	uint8_t owner; // whose game runs it (the player nearest it): 0 the host's, else that client's, which reports it
	uint8_t pad;
} MpgEnemy;

// What the game is doing this frame.
typedef struct MpgGame {
	int playing;      // in a stage, playing (loaded, not leaving, not the title)
	int area;         // gpApplication.mCurrArea
	int episode;
	int world;        // SMS_getShineStage(area), for the host's status line
	int paused;
	float frameMs;    // how long this frame is in game time (1000 / the frame rate)
	unsigned loads;   // stages loaded so far (the same one again included): playing again
	                  // after a pause, a save prompt or the map is not arriving
} MpgGame;

// Reads SMS_MULTIPLAYER*; starts hosting or joining. MPG_ROLE_*.
int mpg_start(void);
// Test tools: the game build to claim (before mpg_start), for joining a game
// built from another commit.
void mpg_set_build(const char* build);
int mpg_role(void);
// Whether `slot` is in the game now (ours included).
int mpg_player_present(int slot);
// Host: the next time we arrive in a stage, open a new session there even
// if it is the same stage (everyone loads it afresh).
void mpg_restart_session(void);
// Client: this game set off for another area or episode by a way out in
// the stage everyone is in (a pipe, a secret course's entrance, a story
// event, with the movie it plays on the way, -1: none): the host is asked to
// take everyone there; 1 if asked. Host: such a request, next in line (1),
// else 0.
int mpg_request_trip(int area, int episode, int movie);
int mpg_next_trip(int* slot, int* area, int* episode, int* movie);
int mpg_slot(void);    // ours; -1 before a client has one
// Client: the link to the host is over for good (refused, closed, or lost and
// not coming back); 0 while connecting, connected or reconnecting.
int mpg_link_over(void);
// Client: the host left while we were in its game (closed it, went back to
// the title, or was lost and not found again): 1 once, with why (a sentence
// for the player), for the game to leave the host's stage for the title.
int mpg_host_left(char* why, int size);
int mpg_ping(int slot); // round trip to `slot` (host) or to the host (client), ms; -1 unknown
// Client: the newest snapshot of the host's world's number (each new one, a new number).
uint32_t mpg_world_seq(void);
int mpg_character(int slot);
const char* mpg_character_name(int slot);

// Once a frame, first: takes in the network's news and tracks the session.
void mpg_frame(const MpgGame* game);

// The shared flags: `values` holds the game's flags (mpg_flag_id(i) for each
// i below mpg_flag_count()); on return, what they should be now. Call every
// frame while playing, and at file select.
int mpg_flag_count(void);
uint32_t mpg_flag_id(int index);
void mpg_flags_sync(int32_t* values);
// A client has the host's flags (and so may load the host's stage).
int mpg_flags_ready(void);

// Client: where the host is (1), or 0 while unknown.
int mpg_target(int* area, int* episode);
// Client: whether this stage is the host's current one (no warp needed).
int mpg_in_target(void);
// Host: a client got a Shine; leave as if we had (once per Shine).
int mpg_take_shine_exit(void);
// Host: a client got a blue coin: its slot + 1 (the host is asked to save,
// as the game asks the player who gets one), else 0. Once per report.
int mpg_take_blue_save(void);

// An item that appeared during the stage (from a box, a defeated enemy, a red
// coin switch), taken by anyone this session: its type (the low byte of its
// actor type) and where it was.
typedef struct MpgTakenAt {
	uint8_t type;
	float pos[3];
} MpgTakenAt;
// This game's player took one: the others' games remove theirs.
void mpg_item_taken_at(int type, const float pos[3]);
// The session's list so far, from 0 (new ones are added at the end); 0 past it.
int mpg_taken_at(int i, MpgTakenAt* out);

// A hit this game's player (`water`: its water) landed on a shared object
// (manager and index in the stage's lists, mp_game.cpp): the others land it
// on theirs. mpg_next_hit gives one another player landed (repeated `count`
// times), 0 when there is none.
void mpg_hit(int manager, int index, int message, int water, const float pos[3]);
int mpg_next_hit(int* slot, int* manager, int* index, int* message, int* water, int* count, float pos[3]);

// This game's player picked up (action 0), put down (1) or threw (2) a shared
// object; where it is and how it moves after, and the player's facing. The
// others do the same with theirs. mpg_next_carry gives one another player did.
void mpg_carry(int manager, int index, int action, const float pos[3], const float vel[3], int yaw);
int mpg_next_carry(int* slot, int* manager, int* index, int* action, float pos[3], float vel[3], int* yaw);

// A mirrored object's state (at most 48 bytes, mp_game.cpp's mirror kinds)
// from the game that owns it now: the others take it. mpg_next_mirror gives
// one another game sent (its size in *size), 0 when there is none.
void mpg_mirror(int manager, int index, const void* data, int size);
int mpg_next_mirror(int* slot, int* manager, int* index, void* data, int* size);

// Goop this game cleaned (TPollutionManager::clean): the others clean it too.
void mpg_clean(float x, float y, float z, float size);
// The next goop another game cleaned, for this game to clean; 0 when none.
int mpg_next_clean(float* x, float* y, float* z, float* size);

// This game's player touched a Shine (its event id, where it is, the way
// they face, Mario's 16-bit angle): every other game moves its player beside
// them and plays the Shine Get as well.
void mpg_shine_get(int eventId, const float pos[3], int yaw);
// The next Shine another player touched; 0 when there is none.
int mpg_next_shine_get(int* slot, int* eventId, float pos[3], int* yaw);

// Host: setting off for another area (episode 0xFF: the game decides on
// arrival); the others start their own trip there at once.
void mpg_host_leaving(int area, int episode);
// Host: on its way now (TMarDirector::moveStage): its session flags (0x30000
// on, bit n for flag n) and the movie it plays on the way (-1: none), so the
// others arrive as it does.
void mpg_host_going(int area, int episode, uint32_t sessionFlags, int movie);
// Client: the host's travel, once it has gone (1); 0 before.
int mpg_travel(uint32_t* sessionFlags, int* movie);
// Client: the host is between stages (set off, not arrived).
int mpg_target_traveling(void);
// Host, each frame of its episode select: the world's area, the episode it
// points at, whether it chose it. Client: the host's, as last heard (1), or 0.
void mpg_host_select(int area, int episode, int chosen);
int mpg_select(int* area, int* episode, int* chosen);
void mpg_travel_done(void);

// A cutscene this game started (the game's own description of it, at most 256
// bytes): the others play it too. mpg_next_demo gives one another player's
// game started (its size; 0 when there is none).
void mpg_demo(const void* data, size_t size);
size_t mpg_next_demo(int* slot, void* data, size_t size);

// Bosses (game/mp_boss.cpp): the host's boss state, sent with the enemies
// (at most 200 bytes), and the hits the others land on their copies (on which
// part, what kind, how many).
void mpg_set_boss(const void* data, size_t size);
size_t mpg_boss(void* out, size_t size, float* age);
void mpg_boss_hit(int unit, int kind, int count);
int mpg_next_boss_hit(int* unit, int* kind, int* count);

// Items: a pickup from the stage's own items (`key` names it the same in
// every game: its index among the item manager's objects and its type);
// and the next one to take away here, picked up by someone else.
void mpg_item_taken(uint32_t key);
int mpg_next_removal(uint32_t* key);

// Enemies. Host: every enemy now, each frame (the controller sends a share
// of them with each update, so all of them go round several times a second);
// and the next one a client's game killed, to kill here. Client: the share
// that arrived last for our stage (how many), and a kill in our game the
// host should know of.
// Host: its enemies this frame; a null list sends none (paused: the others'
// games stop following the snapshot once it is a second old).
void mpg_set_enemies(const MpgEnemy* list, int count);
int mpg_next_enemy_kill(int* manager, int* index);
int mpg_enemies(MpgEnemy* out, int max, float* age);
void mpg_enemy_killed(int manager, int index);

// Our look this frame, and another player's newest (1 when they are in our
// stage).
void mpg_set_pose(const MpgPose* pose);
int mpg_pose(int slot, MpgPose* pose, float* age);
// Another player's look for drawing now, smoothly: the two poses they sent
// either side of a moment a little in the past (the network's unevenness
// hidden in the gap), and how far between them (0..1) now falls. 1 when
// they are in our stage.
int mpg_pose_pair(int slot, MpgPose* a, MpgPose* b, float* t);
uint32_t mpg_session(void); // the session this game is playing in (0: none)

// Text for the player: the next notice ("Bob joined"), 0 when none;
// and the list of players for the status panel.
int mpg_take_notice(char* out, int size);
void mpg_status(char* out, int size);
// The name a player joined with ("" for none).
void mpg_player_name(int slot, char* out, int size);
// What the players are called on screen: the name they joined with, else
// "Player N" (their characters' names are not shown). Valid until the next
// call for the same slot.
const char* mpg_display_name(int slot);

// Players' colours (Customize Mario's): each player's own, which they send the
// host and the host passes on. mpg_look gives `slot`'s ("hat=2fa846;...", 1),
// or "" (0) for Mario's own; mpg_looks_version changes whenever one does.
uint32_t mpg_looks_version(void);
int mpg_look(int slot, char* out, int size);
// A player's picture for one of their model's textures, by the texture's name
// in the model ("H_ma_new_main_s3tc"): their colours (else their character's)
// painted over Mario's (sync/mp_look.cpp, from the launcher's SMS_MARIO_PAINT),
// set to show in place of the texture whose data is at `data` (`bytes` long).
// 1 if this game has a picture for that texture, 0 to leave it as it is.
int mpg_dress(int slot, int character, const char* textureName, const void* data, size_t bytes);
// Forgets every picture mpg_dress set (a new stage's puppets are made).
void mpg_undress_all(void);

#ifdef __cplusplus
}
#endif

#endif
