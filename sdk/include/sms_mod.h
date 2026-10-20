/* sms_mod.h: the native mod API of the Super Mario Sunshine PC port.
 *
 * A native mod is a shared library (.so, .dll, .dylib) in its own folder
 * under mods/, beside a mod.ini that names it (docs/MODDING.md). The game
 * loads every such mod at start and calls its sms_mod_load with an SmsApi:
 * tables of functions to hear of the game's events, read and change the
 * game, draw over it, read the controllers and keyboard, keep settings, and
 * talk to other mods. A mod needs nothing but this header to build: it links
 * against nothing of the game's, so one build of a mod runs with every build
 * of the game that has the same major API version.
 *
 * Rules that keep several mods working together:
 *   - Every function is called on the game thread, inside the game's own
 *     steps (the log is the one function any thread may call). Do the slow
 *     work on threads of your own and hand results over.
 *   - Events reach each mod in priority order; an event's documentation says
 *     whether a handler may change its payload, cancel it, or consume it.
 *   - Draw only inside SMS_EV_DRAW. Take screen space from draw->hud_slot, so
 *     panels of different mods stack instead of overlapping.
 *   - Take the controllers and keyboard (input->capture) only while a menu of
 *     yours is open, and bind keys through input->bind_key, which reports
 *     clashes with the port's keys and other mods'.
 *   - Pointers the game hands you (SmsObject, payload pointers) are good for
 *     the call, and objects until the stage changes. Copy what you keep.
 *
 * Compatibility: the tables only grow at their ends, and each starts with its
 * size as the game built it, so a mod built against a newer minor version
 * checks a function is there with SMS_API_HAS before calling it. A new major
 * version is an incompatible change; the game refuses mods of another major.
 */
#ifndef SMS_MOD_H
#define SMS_MOD_H

#include <stddef.h>
#include <stdint.h>

#define SMS_MOD_API_MAJOR 1
#define SMS_MOD_API_MINOR 1
#define SMS_MOD_API_VERSION ((uint32_t)SMS_MOD_API_MAJOR << 16 | SMS_MOD_API_MINOR)

#if defined(_WIN32)
#define SMS_MOD_EXPORT __declspec(dllexport)
#else
#define SMS_MOD_EXPORT __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
#define SMS_MOD_EXTERN_C extern "C"
#else
#define SMS_MOD_EXTERN_C
#endif
#if defined(__GNUC__) || defined(__clang__)
#define SMS_MOD_PRINTF(f, a) __attribute__((format(printf, f, a)))
#else
#define SMS_MOD_PRINTF(f, a)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ basics */

typedef struct SmsMod SmsMod;     /* one loaded mod: yours is passed to sms_mod_load */
typedef struct SmsApi SmsApi;
typedef void* SmsObject;          /* a game object; good until the stage changes */
typedef uint32_t SmsImage;        /* an image loaded with draw->image_load; 0 = none */
typedef uint32_t SmsColor;        /* 0xRRGGBBAA */

#define SMS_RGBA(r, g, b, a) \
	((SmsColor)(((uint32_t)(r) & 0xFF) << 24 | ((uint32_t)(g) & 0xFF) << 16 | ((uint32_t)(b) & 0xFF) << 8 | ((uint32_t)(a) & 0xFF)))
#define SMS_RGB(r, g, b) SMS_RGBA(r, g, b, 255)

typedef struct SmsVec3 {
	float x, y, z;
} SmsVec3;

typedef struct SmsRect {
	float x, y, w, h;
} SmsRect;

/* Is `member` in this table (the game is new enough to have it)? */
#define SMS_API_HAS(table, member) \
	((table) && (table)->size >= offsetof(__typeof__(*(table)), member) + sizeof((table)->member) && (table)->member)

typedef enum SmsLogLevel {
	SMS_LOG_DEBUG = 0, /* shown with SMS_NATIVE_MODS_DEBUG=1 */
	SMS_LOG_INFO  = 1,
	SMS_LOG_WARN  = 2,
	SMS_LOG_ERROR = 3
} SmsLogLevel;

/* ------------------------------------------------------------------ events */

typedef enum SmsEventType {
	/* Top of every frame of the game's loop: title, file select, stages and
	   movies alike. Payload SmsEvFrame. */
	SMS_EV_FRAME = 1,
	/* Every simulation tick of a stage, after everything moved and before
	   anything is posed for drawing: the place to move things. Not while
	   the pause menu, the map or a save screen is up. SmsEvTick. */
	SMS_EV_TICK = 2,
	/* Once a frame, after SMS_EV_FRAME: build what you draw over the game
	   (the draw table). SmsEvDraw. */
	SMS_EV_DRAW = 3,
	/* A stage's objects are loaded and set up. SmsEvStage. */
	SMS_EV_STAGE_LOADED = 4,
	/* The game asks to leave for another area (pipes, paintings, scripts).
	   Cancellable: cancel to stay. SmsEvStageExit. */
	SMS_EV_STAGE_EXIT_REQUEST = 5,
	/* The stage is about to change (every way out). The payload's area and
	   episode may be changed: the game goes there instead. SmsEvStage. */
	SMS_EV_STAGE_LEAVING = 6,
	/* Mario lost a life and has lives left; area/episode is where he goes
	   next and may be changed. SmsEvStage. */
	SMS_EV_LIFE_LOST = 7,
	/* Mario lost his last life. Cancellable: cancel to carry on as after any
	   other lost life instead of the game over screen. No payload. */
	SMS_EV_GAME_OVER = 8,
	/* Losing a life is about to reset the coins. Cancellable: cancel to keep
	   them. No payload. */
	SMS_EV_COINS_RESET = 9,
	/* A file was chosen at file select; area/episode is where it starts and
	   may be changed. SmsEvStage. */
	SMS_EV_FILE_SELECTED = 10,
	/* An item (coin, blue or red coin, 1-up, ...) was picked up.
	   SmsEvItem. */
	SMS_EV_ITEM_COLLECTED = 11,
	/* The controllers were read, before the game sees them. The pads may be
	   changed (scripted play, input remapping). SmsEvPadRead. */
	SMS_EV_PAD_READ = 12,
	/* A key went down or up in the game's window. Consumable: consume it and
	   neither later mods nor the game's controls see it. SmsEvKey. */
	SMS_EV_KEY = 13,
	/* A camera cutscene was asked for. SmsEvCutscene. */
	SMS_EV_CUTSCENE = 14,
	/* Goop is about to be cleaned at a place (water, Mario's feet).
	   SmsEvGoop. */
	SMS_EV_GOOP_CLEANED = 15,
	/* The player, or their water, hit an object (sprayed, stomped, punched,
	   ground-pounded). Cancellable: cancel and the object never hears of
	   it. SmsEvHit. */
	SMS_EV_OBJECT_HIT = 16,
	/* A save state was asked for. Cancellable: cancel to refuse; set the
	   payload's reason for the message shown. SmsEvSaveState. */
	SMS_EV_SAVE_STATE_REQUEST = 17,
	/* Mario's models are about to be drawn. Consumable: consume to leave
	   them out (draw the player your own way). SmsEvObject. */
	SMS_EV_HIDE_PLAYER_MODEL = 18,
	/* Mario's status (his action: running, jumping, swimming...) is about
	   to change. `to` and `arg` may be changed. Cancellable: cancel and he
	   stays as he is. SmsEvPlayerStatus. */
	SMS_EV_PLAYER_STATUS = 19,
	/* Mario is about to lose health (or air, under water). `amount` may be
	   changed. Cancellable: cancel and he loses nothing. SmsEvDamage. */
	SMS_EV_PLAYER_DAMAGE = 20,
	/* A Shine was collected for the first time on this file. SmsEvShine. */
	SMS_EV_SHINE_COLLECTED = 21,
	/* The game camera placed itself for this tick and is about to make its
	   matrices: position, target, field of view may be changed (a
	   first-person camera, a field of view setting). SmsEvCamera. */
	SMS_EV_CAMERA = 22,
	/* An object is about to move this tick (after = 0) or has (after = 1);
	   each "before" has its "after", nested as objects move their parts.
	   Many of them: subscribe only when you need it. SmsEvObjectMove. */
	SMS_EV_OBJECT_MOVE = 23,
	/* Another mod finished loading (or this event is replayed for each mod
	   that loaded before you subscribed). SmsEvMod. */
	SMS_EV_MOD_LOADED = 24,
	/* The game is closing. No payload. */
	SMS_EV_SHUTDOWN = 25,

	/* core->event_id hands out ids from here for mods' own events. */
	SMS_EV_CUSTOM_BASE = 0x10000
} SmsEventType;

typedef struct SmsEvent {
	uint32_t type;            /* SmsEventType, or a custom id */
	uint32_t size;            /* of *data, as the game built it */
	void* data;               /* the payload for the type, or NULL */
	int cancelled;            /* an earlier handler cancelled it */
	const char* cancelled_by; /* that mod's id */
} SmsEvent;

/* A handler's answer. */
#define SMS_CONTINUE 0
#define SMS_CANCEL   1 /* cancellable events: veto; consumable events: consume */

typedef int (*SmsEventFn)(SmsMod* mod, SmsEvent* ev, void* user);

/* A script runtime's start and stop (core->host_runtime, API 1.1): `script`
   is a mod whose mod.ini says `runtime = <name>`; start runs it (its file is
   mod_dir(script) + "/" + mod_info(script, "script")) and returns 0, or
   refuses; stop ends it. Every API call made for the script passes `script`,
   so it has its own settings, options, keys and HUD slots. */
typedef int (*SmsScriptStartFn)(SmsMod* host, SmsMod* script, void* user);
typedef void (*SmsScriptStopFn)(SmsMod* host, SmsMod* script, void* user);

/* Priorities: lower runs first. Handlers of the same priority run in the
   order the mods loaded. Use MONITOR for handlers that only watch the
   outcome (they run last and must not change anything). */
#define SMS_PRIORITY_FIRST   (-1000)
#define SMS_PRIORITY_EARLY   (-100)
#define SMS_PRIORITY_NORMAL  0
#define SMS_PRIORITY_LATE    100
#define SMS_PRIORITY_MONITOR 1000

typedef enum SmsGameMode {
	SMS_MODE_BOOT = 0,     /* starting, logos */
	SMS_MODE_TITLE = 1,    /* title screen */
	SMS_MODE_MENU = 2,     /* file select and other menus */
	SMS_MODE_STAGE = 3,    /* a stage is being played */
	SMS_MODE_MOVIE = 4,    /* a full-screen movie */
	SMS_MODE_OTHER = 5
} SmsGameMode;

typedef struct SmsEvFrame {
	uint32_t frame; /* frames since start */
	int mode;       /* SmsGameMode */
} SmsEvFrame;

typedef struct SmsEvTick {
	uint32_t tick;        /* ticks since the stage loaded */
	float dt;             /* seconds per tick (1 / ticks_per_second) */
	int ticks_per_second; /* 30, 60 or 120 (the frame rate setting) */
} SmsEvTick;

typedef struct SmsEvDraw {
	float canvas_w, canvas_h; /* draw->canvas: the game picture, 480 high */
	uint32_t frame;
	int mode;                 /* SmsGameMode */
} SmsEvDraw;

typedef struct SmsEvStage {
	int area, episode; /* the stage: see docs/MODDING.md for the area numbers */
} SmsEvStage;

typedef struct SmsEvStageExit {
	uint32_t code;     /* the game's (area + 1) << 8 | episode request */
	int area, episode; /* decoded from code */
	SmsObject actor;   /* what asked (a pipe, a painting), or NULL */
} SmsEvStageExit;

typedef enum SmsItemKind {
	SMS_ITEM_OTHER = 0,
	SMS_ITEM_COIN = 1,
	SMS_ITEM_RED_COIN = 2,
	SMS_ITEM_BLUE_COIN = 3,
	SMS_ITEM_ONE_UP = 4
} SmsItemKind;

typedef struct SmsEvItem {
	SmsObject item, taker;
	int kind;            /* SmsItemKind */
	uint32_t actor_type; /* the game's actor type id */
	SmsVec3 position;
} SmsEvItem;

/* The GameCube controller as the game reads it (PADStatus). */
typedef struct SmsPadStatus {
	uint16_t buttons; /* SMS_PAD_* */
	int8_t stick_x, stick_y, cstick_x, cstick_y;
	uint8_t trigger_l, trigger_r, analog_a, analog_b;
	int8_t err;       /* 0 = connected */
} SmsPadStatus;

#define SMS_PAD_LEFT  0x0001
#define SMS_PAD_RIGHT 0x0002
#define SMS_PAD_DOWN  0x0004
#define SMS_PAD_UP    0x0008
#define SMS_PAD_Z     0x0010
#define SMS_PAD_R     0x0020
#define SMS_PAD_L     0x0040
#define SMS_PAD_A     0x0100
#define SMS_PAD_B     0x0200
#define SMS_PAD_X     0x0400
#define SMS_PAD_Y     0x0800
#define SMS_PAD_START 0x1000

typedef struct SmsEvPadRead {
	SmsPadStatus* pads; /* [4], changeable */
} SmsEvPadRead;

typedef struct SmsEvKey {
	int scancode; /* SDL scancode (USB HID usage): input->key_from_name("F3") */
	int down;     /* 1 pressed, 0 released */
	int repeat;   /* held down: a key repeat */
	int modifiers;/* SMS_KEYMOD_* */
} SmsEvKey;

#define SMS_KEYMOD_SHIFT 1
#define SMS_KEYMOD_CTRL  2
#define SMS_KEYMOD_ALT   4

typedef struct SmsEvCutscene {
	const char* name; /* the camera's name */
	int has_position;
	SmsVec3 position; /* the point it centres on */
	int frames;
	SmsObject actor;
} SmsEvCutscene;

typedef struct SmsEvGoop {
	SmsVec3 position;
	float size;
} SmsEvGoop;

typedef struct SmsEvHit {
	SmsObject object, sender;
	uint32_t message; /* the game's HIT_MESSAGE_* (see docs/MODDING.md) */
} SmsEvHit;

typedef struct SmsEvSaveState {
	const char* reason; /* set when cancelling: shown to the player */
} SmsEvSaveState;

typedef struct SmsEvObject {
	SmsObject object;
} SmsEvObject;

typedef struct SmsEvPlayerStatus {
	uint32_t from;     /* the status he has */
	uint32_t to, arg;  /* the status asked for, and its argument; changeable */
	int forced;
} SmsEvPlayerStatus;

typedef struct SmsEvDamage {
	int amount;     /* changeable */
	int underwater; /* air is lost, not health */
} SmsEvDamage;

typedef struct SmsEvShine {
	int shine;      /* the Shine's number (0..119) */
	int total;      /* Shines on the file, counting this one */
} SmsEvShine;

typedef struct SmsEvCamera {
	SmsVec3 position, target; /* changeable */
	float fovy;               /* degrees, changeable */
	float aspect;
	float roll;               /* reserved: 0 */
} SmsEvCamera;

typedef struct SmsEvObjectMove {
	SmsObject object;
	int after;
} SmsEvObjectMove;

typedef struct SmsEvMod {
	const char* id;
	const char* name;
	const char* version;
} SmsEvMod;

/* ------------------------------------------------------------------ tables */

typedef struct SmsCoreApi {
	uint32_t size;
	/* printf to the game's log, prefixed with the mod's id. Any thread. */
	void (*log)(SmsMod* mod, int level, const char* fmt, ...) SMS_MOD_PRINTF(3, 4);
	/* Hear of an event. Returns a handle (> 0) for unsubscribe, 0 on error. */
	int (*subscribe)(SmsMod* mod, uint32_t type, SmsEventFn fn, void* user, int priority);
	void (*unsubscribe)(SmsMod* mod, int handle);
	/* Custom events: the id for a name ("author.mod.event"); the same name
	   gives every mod the same id. */
	uint32_t (*event_id)(const char* name);
	/* Send a custom event to its subscribers; returns 1 if one cancelled. */
	int (*emit)(SmsMod* mod, uint32_t type, void* data, uint32_t size);
	/* Services: an interface (a struct of functions, say) under a name for
	   other mods. `version` is yours to bump. */
	int (*provide)(SmsMod* mod, const char* name, uint32_t version, const void* iface);
	/* The interface provided under `name` at `min_version` or later, or NULL.
	   List the providing mod in your mod.ini's `after` so it loads first. */
	const void* (*require)(SmsMod* mod, const char* name, uint32_t min_version);
	/* About mods. */
	const char* (*mod_id)(SmsMod* mod);
	const char* (*mod_dir)(SmsMod* mod);  /* the mod's folder (read only) */
	const char* (*data_dir)(SmsMod* mod); /* a folder the mod may write, made on first use */
	int (*is_loaded)(const char* id);
	/* The game build: "<commit>[-eclipse]", and pointer size (4 or 8). */
	const char* (*game_build)(void);
	int (*pointer_size)(void);
	/* Seconds since start, steady. */
	double (*time)(void);
	/* API 1.1. A value of the mod's mod.ini ("name", "version", "script",
	   or a key of the mod's own), or NULL. */
	const char* (*mod_info)(SmsMod* mod, const char* key);
	/* API 1.1. Run the script mods whose mod.ini says `runtime = <name>`
	   (the lua mod hosts "lua"). One host a runtime; script mods load after
	   every native mod, and stop before their host unloads. */
	int (*host_runtime)(SmsMod* host, const char* runtime, SmsScriptStartFn start, SmsScriptStopFn stop, void* user);
} SmsCoreApi;

typedef struct SmsGameState {
	uint32_t size;
	int mode;             /* SmsGameMode */
	int area, episode;    /* the current stage, or -1 */
	int stage_ready;      /* a stage is loaded and Mario exists */
	uint32_t frame;       /* frames since start */
	uint32_t tick;        /* ticks since the stage loaded */
	int ticks_per_second; /* 30, 60 or 120 */
	int eclipse;          /* a Super Mario Eclipse build */
	int save_file;        /* the chosen file 0..2, or -1 */
	int paused;           /* the pause menu, map or a save screen is up (no ticks) */
} SmsGameState;

typedef struct SmsGameApi {
	uint32_t size;
	void (*state)(SmsGameState* out); /* set out->size = sizeof *out first */
	/* The save file's flags (TFlagManager): numbers in docs/MODDING.md. */
	int32_t (*flag_get)(uint32_t id);
	void (*flag_set)(uint32_t id, int32_t value);
	/* Shorthands over the flags. */
	int (*coins)(void);
	int (*lives)(void);
	int (*shines)(void);
	int (*blue_coins)(void);
	int (*has_shine)(int shine);
	/* Leave for another stage as a pipe would; 0 when no stage is played. */
	int (*warp)(int area, int episode);
	/* Objects. find_object: by the name the stage gives it, or NULL. */
	SmsObject (*find_object)(const char* name);
	const char* (*object_name)(SmsObject obj);
	/* Objects that came with events, or from find_object, that have a place
	   in the world: 1 and the position, else 0. */
	int (*object_position)(SmsObject obj, SmsVec3* out);
	int (*object_set_position)(SmsObject obj, const SmsVec3* pos);
	/* The display name of an area ("Delfino Plaza"), or "". */
	const char* (*area_name)(int area);
} SmsGameApi;

typedef struct SmsPlayer {
	uint32_t size;       /* set to sizeof before calling player->get */
	int valid;           /* 0: no Mario now (menus, loading) */
	SmsVec3 position;
	SmsVec3 velocity;    /* units a tick */
	float forward_speed; /* units a tick along his facing */
	float yaw;           /* degrees: 0 faces +Z, 90 faces +X */
	int16_t face_angle[3];
	int health, max_health; /* 0..8 */
	int water, max_water;   /* FLUDD's tank */
	int has_fludd;
	int nozzle;          /* the second nozzle FLUDD has: 0 none, see docs */
	uint32_t status;     /* MARIO_STATUS_* */
	uint32_t status_timer;
	int on_ground, in_water, on_yoshi;
	SmsVec3 floor;       /* the ground below him */
	SmsObject object;    /* the game's TMario */
} SmsPlayer;

typedef struct SmsPlayerApi {
	uint32_t size;
	int (*get)(SmsPlayer* out); /* returns out->valid */
	int (*set_position)(const SmsVec3* pos);
	int (*set_velocity)(const SmsVec3* vel);
	int (*set_forward_speed)(float speed);
	int (*set_health)(int health);
	int (*set_water)(int water);
	/* Ask for a status as the game does (it may refuse). */
	int (*set_status)(uint32_t status, uint32_t arg);
} SmsPlayerApi;

typedef struct SmsCamera {
	uint32_t size;
	int valid;
	SmsVec3 position, target, up;
	float fovy, aspect, near_z, far_z;
	float view[3][4]; /* world to camera, as drawn last */
	float proj[4][4];
} SmsCamera;

typedef struct SmsCameraApi {
	uint32_t size;
	int (*get)(SmsCamera* out);
	/* Where a world point is on the canvas (draw->canvas units); returns 1
	   when it is in front of the camera. depth (may be NULL): distance along
	   the view. */
	int (*world_to_canvas)(const SmsVec3* p, float* x, float* y, float* depth);
} SmsCameraApi;

typedef enum SmsAnchor {
	SMS_ANCHOR_TOP_LEFT = 0,
	SMS_ANCHOR_TOP = 1,
	SMS_ANCHOR_TOP_RIGHT = 2,
	SMS_ANCHOR_LEFT = 3,
	SMS_ANCHOR_CENTER = 4,
	SMS_ANCHOR_RIGHT = 5,
	SMS_ANCHOR_BOTTOM_LEFT = 6,
	SMS_ANCHOR_BOTTOM = 7,
	SMS_ANCHOR_BOTTOM_RIGHT = 8
} SmsAnchor;

/* text flags */
#define SMS_TEXT_SHADOW  1
#define SMS_TEXT_OUTLINE 2
#define SMS_TEXT_CENTER  4  /* x is the middle */
#define SMS_TEXT_RIGHT   8  /* x is the right end */

/* The canvas is the game's picture, 480 units high and as wide as the
   picture's aspect (640 at 4:3, about 853 at 16:9), from its top left. It is
   drawn at the window's resolution over the game and under the port's own
   menus. Text is ASCII. */
typedef struct SmsDrawApi {
	uint32_t size;
	void (*canvas)(float* w, float* h);
	void (*rect)(float x, float y, float w, float h, SmsColor c);
	void (*rect_outline)(float x, float y, float w, float h, float thickness, SmsColor c);
	void (*rect_gradient)(float x, float y, float w, float h, SmsColor top, SmsColor bottom);
	void (*line)(float x0, float y0, float x1, float y1, float thickness, SmsColor c);
	void (*triangle)(float x0, float y0, float x1, float y1, float x2, float y2, SmsColor c);
	void (*circle)(float cx, float cy, float radius, SmsColor c);
	void (*circle_outline)(float cx, float cy, float radius, float thickness, SmsColor c);
	/* `size` is the line height; returns the width drawn. */
	float (*text)(float x, float y, float size, SmsColor c, int flags, const char* text);
	float (*text_width)(float size, const char* text);
	/* Images: PNG, JPEG, TGA or BMP, from a path in the mod's folder. */
	SmsImage (*image_load)(SmsMod* mod, const char* path);
	/* An image from pixels: RGBA8, w x h, row 0 at the top (copied). */
	SmsImage (*image_create)(SmsMod* mod, const uint8_t* rgba, int w, int h);
	void (*image_free)(SmsMod* mod, SmsImage image);
	int (*image_size)(SmsImage image, int* w, int* h);
	void (*image)(SmsImage image, float x, float y, float w, float h, SmsColor tint);
	void (*image_region)(SmsImage image, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
	                     SmsColor tint);
	/* Only draw inside a rectangle until the matching pop (nest freely). */
	void (*clip_push)(float x, float y, float w, float h);
	void (*clip_pop)(void);
	/* The world, through the game camera, drawn over the scene (not hidden
	   behind walls). */
	void (*world_line)(const SmsVec3* a, const SmsVec3* b, float thickness, SmsColor c);
	void (*world_box)(const SmsVec3* center, const SmsVec3* half_size, float thickness, SmsColor c);
	void (*world_ring)(const SmsVec3* center, float radius, float thickness, SmsColor c); /* level */
	/* Text whose middle bottom is at a world point; size shrinks with
	   distance when `scaled`. Returns 1 if drawn. */
	int (*world_text)(const SmsVec3* p, float size, int scaled, SmsColor c, int flags, const char* text);
	/* A w x h rectangle at an anchor of the canvas for this frame, below (or
	   above, at the bottom anchors) the ones other mods took there this
	   frame, so panels never overlap. Returns 0 if it does not fit. */
	int (*hud_slot)(SmsMod* mod, int anchor, float w, float h, SmsRect* out);
} SmsDrawApi;

typedef struct SmsPad {
	uint32_t size;
	int connected;
	uint16_t held, pressed, released; /* SMS_PAD_*; pressed/released: this frame */
	float stick_x, stick_y;           /* -1..1, up is +y */
	float cstick_x, cstick_y;
	float trigger_l, trigger_r;       /* 0..1 */
} SmsPad;

typedef struct SmsInputApi {
	uint32_t size;
	/* Controller `port` (0..3) as read this frame, before any mod changed it
	   and before a capture blanked it. */
	int (*pad)(int port, SmsPad* out);
	/* Keyboard, by scancode (key_from_name): down now; went down this frame. */
	int (*key_down)(int scancode);
	int (*key_pressed)(int scancode);
	int (*key_from_name)(const char* name); /* SDL key names: "F3", "Space", "A"; -1 if unknown */
	const char* (*key_name)(int scancode);
	/* A key for a named action of the mod: `default_key` unless the player
	   set another in the mod's settings (key.<action>). Warns when the key
	   is the port's or another mod's. Returns the scancode or -1. */
	int (*bind_key)(SmsMod* mod, const char* action, const char* default_key);
	/* Take the controllers and keyboard from the game while a menu is open:
	   the game sees nothing pressed and keys reach only you. Returns 0 if
	   another mod has them. Release with on = 0. */
	int (*capture)(SmsMod* mod, int on);
	/* The id of the mod that has them, or NULL. */
	const char* (*captured_by)(void);
} SmsInputApi;

typedef enum SmsOptionType {
	SMS_OPTION_BOOL = 0,
	SMS_OPTION_INT = 1,
	SMS_OPTION_FLOAT = 2,
	SMS_OPTION_CHOICE = 3, /* an int index into `choices` */
	SMS_OPTION_ACTION = 4  /* a button: on_change is called when chosen */
} SmsOptionType;

/* A setting the player can change in the Mods menu (F1). Its value is kept
   in the mod's settings under `key`, read with config->get_*. */
typedef struct SmsOption {
	uint32_t size; /* sizeof(SmsOption) */
	const char* key;
	const char* label;
	int type;      /* SmsOptionType */
	float def, min, max, step;
	const char* const* choices;
	int choice_count;
	void (*on_change)(SmsMod* mod, const char* key, void* user);
	void* user;
} SmsOption;

/* Settings, kept per mod in <data_dir>/settings.ini (key = value). */
typedef struct SmsConfigApi {
	uint32_t size;
	int (*get_int)(SmsMod* mod, const char* key, int def);
	float (*get_float)(SmsMod* mod, const char* key, float def);
	/* The returned string is good until the key is set again. */
	const char* (*get_string)(SmsMod* mod, const char* key, const char* def);
	void (*set_int)(SmsMod* mod, const char* key, int value);
	void (*set_float)(SmsMod* mod, const char* key, float value);
	void (*set_string)(SmsMod* mod, const char* key, const char* value);
	/* Written at once and at exit; call after a batch of sets if you like. */
	void (*save)(SmsMod* mod);
	int (*add_option)(SmsMod* mod, const SmsOption* option);
} SmsConfigApi;

/* Some of the game's system sounds, for audio->play_system_sound. */
#define SMS_SOUND_HEAL   0x4801 /* health back */
#define SMS_SOUND_APPEAR 0x480D /* something appears */
#define SMS_SOUND_CURSOR 0x480F /* menu cursor */
#define SMS_SOUND_DECIDE 0x4810 /* menu choice */
#define SMS_SOUND_COIN   0x4811
#define SMS_SOUND_NEWS   0x4812 /* a message arrives */
#define SMS_SOUND_SHINE  0x4814 /* Shine get */
#define SMS_SOUND_CANCEL 0x481D /* menu back */
#define SMS_SOUND_DAMAGE 0x4823

typedef struct SmsAudioApi {
	uint32_t size;
	/* One of the game's system sound effects (MSD_SE_SY_*: menu blips,
	   coin, 1-up...), not placed in the world. */
	void (*play_system_sound)(uint32_t id);
} SmsAudioApi;

/* The game's own objects, for mods built against the decomp's headers with
   the same compiler and pointer size as the game. Layouts change between
   game builds: check native->build() against what you were built for. */
typedef struct SmsNativeApi {
	uint32_t size;
	const char* (*build)(void);
	/* "gpMarioOriginal", "gpMarDirector", "gpApplication", "gpCamera",
	   "gpItemManager", "gpMap", "TFlagManager": the object, or NULL. */
	void* (*get)(const char* name);
} SmsNativeApi;

struct SmsApi {
	uint32_t size;
	uint16_t major, minor;
	const SmsCoreApi* core;
	const SmsGameApi* game;
	const SmsPlayerApi* player;
	const SmsCameraApi* camera;
	const SmsDrawApi* draw;
	const SmsInputApi* input;
	const SmsConfigApi* config;
	const SmsAudioApi* audio;
	const SmsNativeApi* native;
};

/* ------------------------------------------------------------------ the mod's side */

/* Every mod defines these with SMS_MOD_DEFINE() and sms_mod_load:
 *
 *   SMS_MOD_DEFINE()
 *   SMS_MOD_EXTERN_C SMS_MOD_EXPORT int sms_mod_load(SmsMod* mod, const SmsApi* api) { ...; return 0; }
 *
 * sms_mod_load returns 0 when loaded; anything else refuses (the game logs it
 * and goes on without the mod). sms_mod_unload, if defined, is called at
 * exit and before a hot reload; subscriptions, images, options and
 * services are dropped for you. */
typedef int (*SmsModLoadFn)(SmsMod* mod, const SmsApi* api);
typedef void (*SmsModUnloadFn)(SmsMod* mod);
typedef uint32_t (*SmsModVersionFn)(void);

#define SMS_MOD_DEFINE() \
	SMS_MOD_EXTERN_C SMS_MOD_EXPORT uint32_t sms_mod_api_version(void) { return SMS_MOD_API_VERSION; }

#ifdef __cplusplus
}
#endif

#endif
