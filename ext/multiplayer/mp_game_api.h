#ifndef SMS_MP_GAME_API_H
#define SMS_MP_GAME_API_H

// Between the multiplayer extension's two halves: the game half
// (ext/multiplayer/game, game code built with the game's flags) and the
// controller (ext/multiplayer/sync/mp_controller.cpp, platform code with
// the sockets, the save file merge and the session's rules). Plain C types
// only: the two are compiled with different compilers' settings.
//
// The rules the controller keeps:
// - Slot 0 is the host (Mario); 1, 2 and 3 are Luigi, Peach and Metal Mario.
// - One stage at a time: the host's. Each time the host starts playing a
//   stage it opens a new "stage session"; clients warp into it, taking the
//   host's flags first so the stage loads as the host's did. Only the host
//   leaves for another area; a client's own exits (losing a life, a Shine)
//   bring it back to the host's stage.
// - Coins and items taken from the stage disappear for everyone in the
//   session; a player who reloads the stage gets the session's list again.
// - A Shine collected by anyone sends the whole group out, as it does alone.

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

enum {
	MPG_CHAR_MARIO = 0,
	MPG_CHAR_LUIGI,
	MPG_CHAR_PEACH,
	MPG_CHAR_METAL_MARIO,
};

// What a player looks like this frame, enough to draw them elsewhere.
enum {
	MPG_POSE_VISIBLE   = 1,
	MPG_POSE_FLUDD     = 2,  // wearing FLUDD
	MPG_POSE_SPRAYING  = 4,  // FLUDD emitting water
	MPG_POSE_YOSHI     = 8,  // riding Yoshi
	MPG_POSE_CAP       = 16, // the cap on (else the capless hair)
	MPG_POSE_UPPER     = 32, // the upper body has its own animation (pumping, carrying)
};
typedef struct MpgPose {
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
	uint8_t pad;
	uint32_t hiddenShapes; // the body's shapes hidden (bit n: shape n): shirt, hands, hair
} MpgPose;

// What the game is doing this frame.
typedef struct MpgGame {
	int playing;      // in a stage, playing (loaded, not leaving, not the title)
	int area;         // gpApplication.mCurrArea
	int episode;
	int world;        // SMS_getShineStage(area), for the host's status line
	int paused;
} MpgGame;

// Reads SMS_MULTIPLAYER*; starts hosting or joining. MPG_ROLE_*.
int mpg_start(void);
int mpg_role(void);
int mpg_slot(void);    // ours; -1 before a client has one
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

// Items: a pickup from the stage's own items (`key` names it the same in
// every game: its index among the item manager's objects and its type);
// and the next one to take away here, picked up by someone else.
void mpg_item_taken(uint32_t key);
int mpg_next_removal(uint32_t* key);

// Our look this frame, and another player's (1 when they are in our stage).
void mpg_set_pose(const MpgPose* pose);
int mpg_pose(int slot, MpgPose* pose, float* age);
uint32_t mpg_session(void); // the session this game is playing in (0: none)

// Text for the player: the next notice ("Bob joined as Luigi"), 0 when none;
// and the list of players for the status panel.
int mpg_take_notice(char* out, int size);
void mpg_status(char* out, int size);

#ifdef __cplusplus
}
#endif

#endif
