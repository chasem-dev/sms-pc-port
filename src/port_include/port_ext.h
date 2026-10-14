#ifndef PORT_EXT_H
#define PORT_EXT_H

// Extensions (ext/, see ext/README.md): optional features built into the game
// that it calls at a few chosen points. An extension fills in a PortExtension
// with the hooks it needs (the others null) and registers it before the game
// starts; the game's patches (zzzzz-ext-*) call the port_ext_* functions
// below, which ask every registered extension in turn. With no extension
// switched on, each hook is a loop over nothing and the game plays as before.
//
// Every hook runs on the game thread, between or inside the game's own steps,
// so an extension may use the game's globals (gpApplication, gpMarDirector,
// gpMarioOriginal, TFlagManager) as the game does there.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PortExtension {
	const char* name;
	// The top of every frame of TApplication's loop (title, file select,
	// stages and movies alike), before the frame's work.
	void (*frame)(void);
	// Every simulation tick of a stage, after everything has moved and
	// before anything is posed for drawing (TMarDirector::direct, after the
	// movement pass): an extension can put objects where they should be.
	void (*afterMovement)(void);
	// A stage's objects are loaded and set up (the end of
	// TMarDirector::setupObjects); the stage's heap is the current one, so
	// what is allocated here lasts as long as the stage.
	void (*stageSetup)(void);
	// TMarDirector::setNextStage(code, actor) asks to leave for another area;
	// return 0 to refuse (the game carries on where it is).
	int (*allowNextStage)(unsigned code, void* actor);
	// TMarDirector::moveStage is about to leave for gpApplication.mNextArea,
	// which the hook may change (an explicit scenario skips episode select).
	void (*moveStage)(void);
	// Mario lost a life and has lives left: gpApplication.mNextArea is where
	// he goes next, which the hook may change.
	void (*missNextArea)(void);
	// Mario lost his last life: return 1 to carry on as after any other
	// lost life (missNextArea says where) instead of the game over screen.
	int (*takeGameOver)(void);
	// Losing a life resets the coin count unless this returns 1.
	int (*keepCoinsOnMiss)(void);
	// A file was chosen at file select: gpApplication.mNextArea is where it
	// starts, which the hook may change.
	void (*afterFileSelect)(void);
	// A TItem (coin, blue or red coin, 1-up...) was picked up by `taker`.
	void (*itemTaken)(void* item, void* taker);
	// Return 0 to switch save states off (the controls then say why).
	int (*allowSaveStates)(void);
	// Why save states are off, for the message the controls show.
	const char* (*saveStatesOffReason)(void);
	// Return 1 to leave out the player's own models (body, hands, cap,
	// FLUDD) when `mario` is drawn; the extension draws the player itself.
	// Yoshi and the shadow are drawn as ever.
	int (*hidePlayerModel)(void* mario);
} PortExtension;

// Adds an extension; call from a static constructor or before the first frame.
void port_ext_register(const PortExtension* ext);
// The registered extension called `name`, or null.
const PortExtension* port_ext_find(const char* name);

// The hooks, as the game's patches call them.
void port_ext_frame(void);
void port_ext_after_movement(void);
void port_ext_stage_setup(void);
int port_ext_allow_next_stage(unsigned code, void* actor);
void port_ext_move_stage(void);
void port_ext_miss_next_area(void);
int port_ext_keep_coins_on_miss(void);
int port_ext_take_game_over(void);
void port_ext_after_file_select(void);
void port_ext_item_taken(void* item, void* taker);
// 1 when save states may be used; otherwise 0 and *reason says why.
int port_ext_allow_save_states(const char** reason);
int port_ext_hide_player_model(void* mario);

#ifdef __cplusplus
}
#endif

#endif
