# Native mods

A native mod is a program of your own that the game loads at start and runs inside itself.
It hears of what happens in the game (a stage loads, Mario jumps, a coin is taken, a key is pressed), reads and changes the game (Mario's position, health and water, the save file's flags, the camera), draws over the game's picture (panels, text, images, lines and markers in the world), and keeps settings the player changes in a shared **Mods menu** (F1).

Several mods run together.
They all go through the same API, which puts their handlers in order, stacks their panels instead of letting them overlap, warns about keys two of them want, and hands the controllers to one menu at a time.

This page is the whole manual: how to [write one](#quick-start), [what a mod folder holds](#the-mod-folder), [how mods live together](#living-together), the [events](#events) and [functions](#the-api) in full, mods [in Lua](#lua-scripts), [game numbers](#game-reference) such as stage and flag ids, [debugging](#debugging), and the [design](#design-notes) behind it all, including what is planned.

The API itself is one C header, [`sdk/include/sms_mod.h`](../include/sms_mod.h), with every function and payload documented where it is declared. [`sdk/include/sms_mod.hpp`](../include/sms_mod.hpp) adds a header-only C++ layer. The examples in [`sdk/examples/`](../examples) are complete mods.

Contents:
[Languages](#languages) ·
[Quick start](#quick-start) ·
[The mod folder](#the-mod-folder) ·
[Life of a mod](#life-of-a-mod) ·
[Living together](#living-together) ·
[Events](#events) ·
[The API](#the-api) ·
[Drawing](#drawing) ·
[Input](#input) ·
[Settings and the Mods menu](#settings-and-the-mods-menu) ·
[Talking to other mods](#talking-to-other-mods) ·
[Lua scripts](#lua-scripts) ·
[Time, units and frame rate](#time-units-and-frame-rate) ·
[Multiplayer, save states, Eclipse](#multiplayer-save-states-and-eclipse) ·
[Game reference](#game-reference) ·
[Debugging](#debugging) ·
[Compatibility](#compatibility) ·
[Design notes](#design-notes) ·
[Adding to the API](#adding-to-the-api-port-developers)

## Languages

The API is a C ABI: a mod is a shared library (`.so` on Linux, `.dll` on Windows, `.dylib` on macOS) that exports two functions, and it reaches the game only through tables of function pointers the game hands it.
It links against nothing of the game's, so it builds without the game's sources and keeps working across game updates (see [Compatibility](#compatibility)).
Any language that can make such a library works:

| Language | How | Notes |
| --- | --- | --- |
| **C** | include `sms_mod.h` | The reference. `trainer-hud` and `stage-timer` are written in C. |
| **C++** | include `sms_mod.hpp` (C++11) | Lambdas for handlers, payloads by type, a class for the mod's state; exceptions are caught before they reach the game. `hello` is written in C++. |
| **Rust** | `bindgen` on `sms_mod.h`, a `cdylib` crate | Export `sms_mod_api_version` and `sms_mod_load` with `#[no_mangle] pub extern "C"`; see [Rust](#rust). |
| **Zig, Odin, Nim, D, …** | their C import or FFI | Anything that can export C functions and call C function pointers. |
| **Lua** | `runtime = lua` in `mod.ini`, a `main.lua` | The `lua` mod runs script mods: no compiler, no binary per platform, reloaded on save. See [Lua scripts](#lua-scripts); `lua-challenges` is written in Lua. |
| **Other script languages** | a native mod hosting the runtime | `core->host_runtime` lets a mod run script mods of a language it embeds, as the lua mod does ([Other languages](#other-languages)). |

A native mod runs inside the game with the game's rights; players should install mods they trust, as with any program.

### Rust

A Rust mod is a `cdylib` with bindings generated from the header:

```toml
# Cargo.toml
[lib]
crate-type = ["cdylib"]
[build-dependencies]
bindgen = "0.69"
```

```rust
// build.rs: bindgen::Builder::default().header("sdk/include/sms_mod.h") ... .write_to_file(out.join("sms.rs"))
include!(concat!(env!("OUT_DIR"), "/sms.rs"));

static mut API: *const SmsApi = std::ptr::null();

#[no_mangle]
pub extern "C" fn sms_mod_api_version() -> u32 { (SMS_MOD_API_MAJOR << 16) | SMS_MOD_API_MINOR }

#[no_mangle]
pub extern "C" fn sms_mod_load(m: *mut SmsMod, api: *const SmsApi) -> i32 {
    unsafe {
        API = api;
        let core = &*(*api).core;
        (core.subscribe.unwrap())(m, SmsEventType_SMS_EV_DRAW, Some(on_draw), std::ptr::null_mut(), 0);
    }
    0
}

unsafe extern "C" fn on_draw(_m: *mut SmsMod, _ev: *mut SmsEvent, _u: *mut std::ffi::c_void) -> i32 {
    let d = &*(*API).draw;
    (d.text.unwrap())(20.0, 400.0, 14.0, 0xFFFFFFFF, 1, c"Hello from Rust".as_ptr());
    0
}
```

Do not let a panic unwind into the game: wrap handler bodies in `std::panic::catch_unwind`, or build with `panic = "abort"`.
The game never builds this crate itself; this outline has not been run against a game build.

## Quick start

1. **Copy an example.** `sdk/examples/hello` (C++) is the smallest; `trainer-hud` (C) shows almost everything.
2. **Build it** with the SDK's CMake helper, which lays the mod out as the game loads it:
   ```sh
   cmake -S sdk/examples -B build/mods-examples -DSMS_MODS_OUT=$PWD/mods -DSMS_MOD_ARCH=32
   cmake --build build/mods-examples
   ```
   `SMS_MOD_ARCH=32` is for the default Linux game (`build/linux-32`), which is a 32-bit program; a mod must have the game's pointer size.
   For a 64-bit game (`SMS_ARCH=64`, Windows, macOS) leave it out.
   For your own mod outside this repository, `include(path/to/sdk/cmake/SmsMod.cmake)` and call `sms_add_mod(<folder> <sources>)`.
   Without CMake, one command does: `cc -shared -fPIC -fvisibility=hidden -O2 -I sdk/include mymod.c -o mods/mymod/bin/linux-x86_64/mymod.so` (add `-m32` and use `linux-x86` for the 32-bit game).
3. **Run the game.** Every folder in `mods/` with a `mod.ini` is loaded. The log says so:
   ```
   [mods] 3 native mod(s) in ../../mods (linux-x86); settings in ~/.local/share/sms-port/mods
   [mod:hello] loaded Hello 1.0.0 by SMS PC port examples
   [mod:hello] hello from DELFINO PLAZA, episode 1
   ```
4. **Press F1** in the game for the Mods menu: each mod, whether it loaded (and why not), a switch for it, and its settings.

The smallest mod in C:

```c
#include <sms_mod.h>

static const SmsApi* api;

static int on_draw(SmsMod* mod, SmsEvent* ev, void* user)
{
    const SmsEvDraw* d = (const SmsEvDraw*)ev->data;
    if (d->mode == SMS_MODE_STAGE)
        api->draw->text(20, 440, 14, SMS_RGB(255, 255, 255), SMS_TEXT_SHADOW, "Hello, Delfino!");
    return SMS_CONTINUE;
}

SMS_MOD_DEFINE()

SMS_MOD_EXTERN_C SMS_MOD_EXPORT int sms_mod_load(SmsMod* mod, const SmsApi* a)
{
    api = a;
    api->core->subscribe(mod, SMS_EV_DRAW, on_draw, NULL, SMS_PRIORITY_NORMAL);
    return 0; /* loaded */
}
```

The same in C++:

```cpp
#include <sms_mod.hpp>

struct Hello : sms::Mod {
    using sms::Mod::Mod;
    int load() override {
        on<SmsEvDraw>(SMS_EV_DRAW, [this](SmsEvDraw& d) {
            if (d.mode == SMS_MODE_STAGE)
                draw().text(20, 440, 14, SMS_RGB(255, 255, 255), SMS_TEXT_SHADOW, "Hello, Delfino!");
        });
        return 0;
    }
};
SMS_MOD_CLASS(Hello)
```

## The mod folder

```
mods/
  trainer-hud/
    mod.ini                         what the mod is (required)
    bin/linux-x86/trainer_hud.so    the library, one per platform
    bin/linux-x86_64/trainer_hud.so
    bin/windows-x86_64/trainer_hud.dll
    bin/macos-arm64/trainer_hud.dylib
    logo.png                        anything else the mod reads (draw->image_load, core->mod_dir)
```

The game looks for the library in `bin/<platform>/`, then `<platform>/`, then the mod's folder itself, so one folder can carry every platform.
The platforms are `linux-x86` (the default 32-bit Linux game), `linux-x86_64`, `linux-arm64`, `windows-x86_64`, `windows-x86`, `macos-x86_64` and `macos-arm64`; the log names the one it wants.
A mod folder may sit beside the other things in `mods/` (texture packs, Eclipse, language packs); only folders with a `mod.ini` are native mods.

### mod.ini

`key = value` lines; `#` starts a comment.

| Key | Meaning | Default |
| --- | --- | --- |
| `id` | Unique name: letters, digits, `-`, `_`, `.`. Settings, services, logs and other mods' `after`/`requires` use it. | the folder's name |
| `name` | Shown in the Mods menu. | the id |
| `version` | Shown in the Mods menu and the log. | `0` |
| `author`, `description` | Shown in the Mods menu. | |
| `library` | The library's file name without `.so`/`.dll`/`.dylib`. | the id |
| `after` | Mods (ids, comma separated) to load before this one when they are there. | |
| `before` | Mods to load after this one when they are there. | |
| `requires` | Mods that must be loaded first; without them this mod is not loaded. | |
| `conflicts` | Mods this one cannot run with; the one that would load second is not loaded. | |
| `priority` | Load order among mods with no `after`/`before` between them: lower first, then by id. | `0` |
| `runtime` | A script mod: the language that runs it (`lua`). It has no library; the native mod hosting that runtime starts it ([Lua scripts](#lua-scripts)). | |
| `script` | A script mod's file, in its folder. | `main.<runtime>` |
| `gameplay` | `1` when the mod changes how the game plays (rather than only showing things). The log lists such mods; multiplayer will compare them between players (see [Multiplayer](#multiplayer-save-states-and-eclipse)). | `0` |

## Life of a mod

1. **Found.** At the first frame the game reads every `mods/*/mod.ini` (or the folders in `SMS_NATIVE_MODS_DIR`).
   Mods switched off in the Mods menu or listed in `native_mods_disabled` are left alone.
2. **Ordered.** `requires` and `after` load first, `before` later; otherwise by `priority`, then id. A cycle is logged and broken by priority.
3. **Loaded.** Native mods first, then script mods (`runtime = lua`), each in order. For a script mod, the native mod hosting its runtime starts it; without one, it is not loaded and the Mods menu says what it needs. For a native mod, the game opens the library, checks `sms_mod_api_version()` (a mod made for another major version is refused), and calls `sms_mod_load(mod, api)`.
   Subscribe, add options, bind keys, load images and provide services there.
   Return 0 to load; anything else refuses (logged, and shown in the Mods menu).
4. **Running.** Events arrive on the game thread, every frame, tick and happening the mod subscribed to.
5. **Unloaded** at exit, when the player switches the mod off in the Mods menu, or for a [hot reload](#debugging).
   The game calls `sms_mod_unload(mod)` if the mod has one, then drops everything the mod registered: its handlers, services, key bindings, options, images and the input capture.
   It then closes the library, so a mod must stop any thread it started in `sms_mod_unload`.
   A mod switched on again in the Mods menu loads at once, mid-game: `game->state` says whether a stage is already being played.
   Script mods stop before the mod running them unloads, and start again when it is back.

A mod whose handler throws a C++ exception through the API is switched off and logged; one that crashes takes the game with it, as any code in the process would.

## Living together

The API is built so mods that do different things never get in each other's way, and so it is clear what happens when two want the same thing.

- **One thread.** Every API function (but `core->log`) and every handler runs on the game thread, between or inside the game's own steps. Do slow work (files, networks) on threads of your own and hand the results over; never call the API from them.
- **Ordered handlers.** Each event reaches its handlers by priority (`SMS_PRIORITY_FIRST` -1000, `EARLY` -100, `NORMAL` 0, `LATE` 100, `MONITOR` 1000), then load order, then the order they subscribed. `MONITOR` handlers watch the outcome and must not change it.
- **Three kinds of event.** An event's documentation says which it is:
  - *Changeable*: handlers may change fields of the payload; each sees the changes of the ones before (an exit's destination, the camera's field of view, the controllers).
  - *Cancellable* (a veto): returning `SMS_CANCEL` stops what the game was about to do (a hit, a stage exit, damage). Every handler still runs; later ones see `ev->cancelled` and `ev->cancelled_by` (the id of the first mod that cancelled), so a `MONITOR` handler can tell a hit that landed from one another mod stopped.
  - *Consumable*: the first handler to return `SMS_CANCEL` takes it, and no later handler or the game sees it (a key press, the player's model).
- **Screen space.** Take the rectangles you draw panels in from `draw->hud_slot(mod, anchor, w, h, &rect)`. Panels at one anchor stack, in handler order, and a panel that is hidden simply takes no slot, so the others move up. The slots start clear of the game's own HUD.
- **Keys.** Ask for keys with `input->bind_key(mod, "action", "F3")`. The player can change each in the mod's settings (`key.action = F6`), and the game warns in the log when the key is one of the port's own (the Mods menu, F5–F11, the backtick, Escape, every key in `bindings.txt`) or another mod's.
  Consume your key's events in `SMS_EV_KEY` so the game's controls do not also see them.
- **Menus.** A mod with a menu of its own calls `input->capture(mod, 1)` while it is open: the game then sees no buttons or sticks, keys reach only that mod, and other mods' `capture` calls fail until it lets go. Most mods need no menu: [options](#settings-and-the-mods-menu) appear in the shared Mods menu.
- **Settings and files** are per mod: `config->*` keeps `key = value` settings in the mod's data folder, `core->data_dir(mod)` is a folder of its own to write, and `core->mod_dir(mod)` is its install folder (treat it as read only; the launcher may replace it).
- **Names.** Custom events and services are named by convention `<mod id>.<name>`, so two mods never pick the same one.

## Events

Subscribe with `core->subscribe(mod, type, fn, user, priority)`; `fn(mod, ev, user)` gets an `SmsEvent` whose `data` points to the payload struct named below (check `ev->size` against `sizeof` the struct before reading fields added in a later minor version).

| Event | When | Payload | Handlers may |
| --- | --- | --- | --- |
| `SMS_EV_FRAME` | the top of every frame: title, file select, stages, movies | `SmsEvFrame` frame number, game mode | |
| `SMS_EV_TICK` | every simulation tick of a stage, after everything moved, before drawing; not while the pause menu, map or a save screen is up | `SmsEvTick` tick, `dt`, ticks per second | move things |
| `SMS_EV_DRAW` | once a frame, after `SMS_EV_FRAME` | `SmsEvDraw` canvas size, frame, mode | draw (the only place) |
| `SMS_EV_STAGE_LOADED` | a stage's objects are loaded and set up | `SmsEvStage` area, episode | |
| `SMS_EV_STAGE_EXIT_REQUEST` | the game asks to leave for another area (pipe, painting, script) | `SmsEvStageExit` code, area, episode, the asking actor | cancel (stay) |
| `SMS_EV_STAGE_LEAVING` | the stage is about to change, any way out | `SmsEvStage` where it goes | change area/episode |
| `SMS_EV_LIFE_LOST` | Mario lost a life and has lives left | `SmsEvStage` where he goes | change area/episode |
| `SMS_EV_GAME_OVER` | Mario lost his last life | none | cancel (carry on as after any lost life) |
| `SMS_EV_COINS_RESET` | a lost life is about to reset the coins | none | cancel (keep the coins) |
| `SMS_EV_FILE_SELECTED` | a file was chosen at file select | `SmsEvStage` where it starts | change area/episode |
| `SMS_EV_ITEM_COLLECTED` | an item was picked up: coin, red or blue coin, other `TItem`s | `SmsEvItem` item, taker, kind, actor type, position | |
| `SMS_EV_PAD_READ` | the controllers were read, before the game sees them | `SmsEvPadRead` the four pads | change the pads |
| `SMS_EV_KEY` | a key went down, repeated or up in the window | `SmsEvKey` scancode, down, repeat, modifiers | consume |
| `SMS_EV_CUTSCENE` | a camera cutscene was asked for | `SmsEvCutscene` camera name, centre, frames, actor | |
| `SMS_EV_GOOP_CLEANED` | goop is about to be cleaned at a place | `SmsEvGoop` position, size | |
| `SMS_EV_OBJECT_HIT` | the player or their water hit an object (sprayed, stomped, punched, ground-pounded) | `SmsEvHit` object, sender, [message](#hit-messages) | cancel (the object never hears of it) |
| `SMS_EV_SAVE_STATE_REQUEST` | a save state was asked for | `SmsEvSaveState` reason | cancel (set `reason` for the message shown) |
| `SMS_EV_HIDE_PLAYER_MODEL` | Mario's models are about to be drawn | `SmsEvObject` Mario | consume (leave them out; draw him your own way) |
| `SMS_EV_PLAYER_STATUS` | Mario's [status](#mario-statuses) (his action) is about to change | `SmsEvPlayerStatus` from, to, arg, forced | change `to`/`arg`; cancel (he stays as he is) |
| `SMS_EV_PLAYER_DAMAGE` | Mario is about to lose health, or air under water | `SmsEvDamage` amount, underwater | change `amount`; cancel (he loses nothing) |
| `SMS_EV_SHINE_COLLECTED` | a Shine's flag was set for the first time on this file | `SmsEvShine` shine number, total | |
| `SMS_EV_CAMERA` | the game camera placed itself this tick and is about to make its matrices | `SmsEvCamera` position, target, fovy, aspect | change position, target, fovy |
| `SMS_EV_OBJECT_MOVE` | an object is about to move this tick (`after` 0), or has (`after` 1) | `SmsEvObjectMove` object, after | (many per tick: subscribe only when needed) |
| `SMS_EV_MOD_LOADED` | another mod loaded; on subscribing, once for each mod already loaded | `SmsEvMod` id, name, version | |
| `SMS_EV_SHUTDOWN` | the game is closing | none | save |
| `SMS_EV_CUSTOM_BASE` and up | mods' own events, from `core->event_id(name)` | whatever the sender says | cancel |

Notes:

- The title screen's background is a stage being played (area 15, Delfino Plaza): `SMS_EV_STAGE_LOADED` comes for it, and Mario exists. Leave area 15 out to act only in the player's own game.
- `SMS_EV_PLAYER_STATUS` and `SMS_EV_PLAYER_DAMAGE` are only sent for the player's Mario, not Shadow Mario (who is a Mario too).
- `SMS_EV_CAMERA`: a new position or target changes only what is drawn this tick; the camera keeps following Mario as before. A new field of view is given to the camera for the tick (its culling and screen effects use it too) and taken back the next tick, unless the camera set one itself in the meantime.
- `SMS_EV_TICK` and `SMS_EV_FRAME` are not the same clock: at 60 or 120 frames per second, see [Time](#time-units-and-frame-rate).
- `SMS_EV_PAD_READ`: `input->pad` still answers with the controllers as read, before any mod's change.

## The API

`sms_mod_load` receives `const SmsApi* api`. Its tables:

| Table | For |
| --- | --- |
| `api->core` | logging, events, services, the mod's folders, the game build, time |
| `api->game` | the game's state, save-file flags, warping, objects, stage names |
| `api->player` | Mario: reading and changing him |
| `api->camera` | the game camera, and projecting world points onto the canvas |
| `api->draw` | drawing over the game |
| `api->input` | controllers, keyboard, key bindings, taking the input for a menu |
| `api->config` | settings and the Mods menu's options |
| `api->audio` | the game's sound effects |
| `api->native` | the game's own objects, for mods built against the decomp's headers |

Every table starts with `size`, the size the game was built with. Tables only grow at their ends, so a mod checks a function from a later minor version is there with `SMS_API_HAS(api->draw, some_new_function)` before calling it.
Structs a mod passes in for the game to fill (`SmsGameState`, `SmsPlayer`, `SmsCamera`, `SmsPad`) start with a `size` the mod sets to `sizeof` the struct; the game fills only that much, so an old mod works with a newer game and the other way round.

### core

| Function | Does |
| --- | --- |
| `log(mod, level, fmt, ...)` | printf to the game's log as `[mod:<id>]`. `SMS_LOG_DEBUG` lines show only with `SMS_NATIVE_MODS_DEBUG=1`. The one function any thread may call. |
| `subscribe(mod, type, fn, user, priority)` | hear of an event; returns a handle (> 0) |
| `unsubscribe(mod, handle)` | stop; safe inside a handler |
| `event_id(name)` | the id of a custom event: every mod gets the same id for the same name |
| `emit(mod, type, data, size)` | send a custom event; returns 1 if a handler cancelled it. The game's own events cannot be sent. |
| `provide(mod, name, version, iface)` | offer an interface (usually a struct of function pointers) to other mods; the first mod to provide a name keeps it |
| `require(mod, name, min_version)` | another mod's interface at that version or later, or NULL |
| `mod_id(mod)`, `mod_dir(mod)`, `data_dir(mod)` | the mod's id, its install folder, and a folder of its own to write (made on first use) |
| `is_loaded(id)` | whether another mod is loaded |
| `game_build()`, `pointer_size()` | the game's build (`<commit>` or `<commit>-eclipse`) and 4 or 8 |
| `time()` | seconds since start, steady |
| `mod_info(mod, key)` | a value from a mod's `mod.ini` (its own keys too), or NULL (API 1.1) |
| `host_runtime(mod, runtime, start, stop, user)` | run the script mods of a language: `start(host, script, user)` for each `runtime = <name>` mod, `stop` as it unloads (API 1.1; see [Other languages](#other-languages)) |

### game

| Function | Does |
| --- | --- |
| `state(&s)` | `SmsGameState`: mode (`SMS_MODE_BOOT`, `TITLE`, `MENU`, `STAGE`, `MOVIE`, `OTHER`), area and episode (-1 without a stage), `stage_ready`, frame and tick counts, ticks per second, `eclipse`, the save file, `paused` (the pause menu, map or a save screen) |
| `flag_get(id)`, `flag_set(id, value)` | the save file's [flags](#flags) |
| `coins()`, `lives()`, `shines()`, `blue_coins()`, `has_shine(n)` | shorthands over the flags |
| `warp(area, episode)` | leave for a stage as a pipe would; 0 when no stage is being played |
| `find_object(name)` | an object by the name the stage gives it, or NULL |
| `object_name(obj)` | an object's name (often Japanese, Shift-JIS) |
| `object_position(obj, &pos)`, `object_set_position(obj, &pos)` | for objects with a place in the world; 0 for others |
| `area_name(area)` | the stage's display name in the game's language ("DELFINO PLAZA"), or "" |

Objects (`SmsObject`) come from events and `find_object`; they are good until the stage changes. Do not keep them across `SMS_EV_STAGE_LEAVING`.

### player

`get(&p)` fills an `SmsPlayer`: `valid` (0 outside stages), position, velocity, forward speed, yaw in degrees and the raw face angles, health and maximum, FLUDD's water and maximum, whether he has FLUDD and its second [nozzle](#nozzles), his [status](#mario-statuses) and how long he has had it, `on_ground`, `in_water`, `on_yoshi`, the ground below him, and the game's object for him.

`set_position`, `set_velocity`, `set_forward_speed`, `set_health` (clamped to 0..max), `set_water` (clamped to 0..max) change him; `set_status(status, arg)` asks for a status as the game does (it may refuse). Each returns 0 when there is no Mario.

### camera

`get(&c)` fills an `SmsCamera`: position, target, up, vertical field of view, aspect, near and far planes, and the view (3×4, world to camera) and projection (4×4) matrices it drew the last frame with.
`world_to_canvas(&p, &x, &y, &depth)` gives where a world point is on the [canvas](#drawing) and returns 1 when it is in front of the camera.

### draw, input, config, audio

See [Drawing](#drawing), [Input](#input), [Settings](#settings-and-the-mods-menu).
`audio->play_system_sound(id)` plays one of the game's system sound effects (its `MSD_SE_SY_*` numbers in the decomp's `MSound` headers).

### native

For mods that go beyond the API: `native->get("gpMarioOriginal")` (also `gpMarDirector`, `gpApplication`, `gpCamera`, `gpItemManager`, `gpMap`, `TFlagManager`) returns the game's own object.
Reading it needs the decomp's headers and the same compiler, pointer size and layout as the game, which change between builds: check `native->build()` (`<commit>/<platform>`) against the one you built for, and refuse to load otherwise.
Prefer asking for the event or function you need (see [Adding to the API](#adding-to-the-api-port-developers)).

## Drawing

Draw only inside `SMS_EV_DRAW`. Calls anywhere else are ignored (and logged once).

**The canvas** is the game's picture: 480 units high and as wide as the picture's aspect (640 at 4:3, about 853 with the port's 16:9 widescreen), from its top left. `draw->canvas(&w, &h)` and the `SMS_EV_DRAW` payload give its size.
Whatever the window's size, a mod draws in canvas units, and the game draws it at the window's full resolution over the game's picture and under the port's own menus (the debug overlay, the cheat and warp menus, the quit prompt), with the Mods menu over every mod.
Mods draw in the order of their `SMS_EV_DRAW` handlers: a later one draws over an earlier one.
The drawing appears in the window (and `SMS_WINDOW_SHOTS` captures), not in `SMS_SHOTS` (the game's picture alone).

| Function | Draws |
| --- | --- |
| `rect`, `rect_outline`, `rect_gradient` (top to bottom) | rectangles |
| `line(x0, y0, x1, y1, thickness, c)`, `triangle`, `circle`, `circle_outline` | shapes |
| `text(x, y, size, c, flags, s)`, `text_width(size, s)` | text, `size` the line's height; flags `SMS_TEXT_SHADOW`, `SMS_TEXT_OUTLINE`, `SMS_TEXT_CENTER`, `SMS_TEXT_RIGHT`; `\n` breaks lines. ASCII only (other characters show as `?`). |
| `image_load(mod, "file.png")`, `image_create(mod, rgba, w, h)` | an image (PNG, JPEG, TGA or BMP from the mod's folder, or RGBA8 pixels, row 0 at the top); freed with `image_free` or when the mod unloads |
| `image(img, x, y, w, h, tint)`, `image_region(img, x, y, w, h, u0, v0, u1, v1, tint)`, `image_size` | images, smoothly scaled (mipmapped) |
| `clip_push(x, y, w, h)`, `clip_pop()` | draw only inside a rectangle; nest freely |
| `world_line(&a, &b, thickness, c)`, `world_box(&centre, &half, thickness, c)`, `world_ring(&centre, radius, thickness, c)` | lines in the world through the game camera, cut at the camera's near plane; over the scene, not hidden behind walls |
| `world_text(&p, size, scaled, c, flags, s)` | text standing on a world point, smaller with distance when `scaled` |
| `hud_slot(mod, anchor, w, h, &rect)` | a place for a w × h panel this frame (below) |

Colours are `0xRRGGBBAA`: `SMS_RGB(r, g, b)` and `SMS_RGBA(r, g, b, a)`.

**HUD slots.** The canvas has nine anchors, `SMS_ANCHOR_TOP_LEFT` to `SMS_ANCHOR_BOTTOM_RIGHT`.
Each frame, `hud_slot` hands out rectangles at an anchor one below the other (above the other at the bottom anchors) in the order the mods ask, 6 units apart, and returns 0 when the next one would not fit.
The stacks start clear of the game's HUD: the left column below the Shine and coin counters, the top middle below the lives, the right column below the health meter, and the bottom ones above the water gauge and the talk boxes.
Panels that ask every frame and are hidden by skipping the call let the others move into their place.

**Stepping aside.** The mods' drawing is over everything the game draws, its pause menu and map included. Skip HUD panels while `game->state` says `paused` (the pause menu, map or a save screen) and outside `SMS_MODE_STAGE`, as the examples do.

```
TOP_LEFT       TOP        TOP_RIGHT
LEFT          CENTER          RIGHT
BOTTOM_LEFT   BOTTOM   BOTTOM_RIGHT
```

## Input

| Function | Does |
| --- | --- |
| `pad(port, &p)` | controller `port` (0–3) as read this frame: held buttons, buttons pressed and released this frame, sticks (-1..1, up is +y), triggers (0..1). Before any mod's `SMS_EV_PAD_READ` change and before a capture blanks it. |
| `key_down(sc)`, `key_pressed(sc)` | a key is down; went down this frame |
| `key_from_name("F3")`, `key_name(sc)` | SDL key names ("F3", "Space", "Left Shift", "A") and scancodes |
| `bind_key(mod, action, default)` | the key for an action: the player's `key.<action>` setting or the default, with clash warnings |
| `capture(mod, on)`, `captured_by()` | take the controllers and keyboard for a menu; who has them |

Buttons are the GameCube's: `SMS_PAD_A`, `B`, `X`, `Y`, `Z`, `L`, `R`, `START`, `UP`, `DOWN`, `LEFT`, `RIGHT`.
Keys arrive in `SMS_EV_KEY` as SDL scancodes, after the port's own keys and menus have taken theirs and before the game's controls.

## Settings and the Mods menu

Each mod's settings are `key = value` lines in `<data folder>/<id>/settings.ini` (on Linux `~/.local/share/sms-port/mods/<id>/settings.ini`; `%APPDATA%\sms-port\mods` on Windows), read on first use and written within a couple of seconds of a change, at `config->save`, and at exit.
`config->get_int / get_float / get_string(mod, key, default)` read them and `set_*` change them.

`config->add_option(mod, &option)` puts a setting in the **Mods menu** (F1):

| `type` | Shows | Changes with |
| --- | --- | --- |
| `SMS_OPTION_BOOL` | ON / OFF | Enter, Left, Right |
| `SMS_OPTION_INT`, `SMS_OPTION_FLOAT` | the number | Left / Right by `step`, within `min`..`max` |
| `SMS_OPTION_CHOICE` | one of `choices` (the setting is its index) | Left / Right |
| `SMS_OPTION_ACTION` | a button | Enter calls `on_change` |

The value is kept under `key`; `def` is shown until the player changes it; `on_change(mod, key, user)` is called after each change.
The menu copies the option, so its strings need not outlive the call.

The menu also lists every mod found, with its version, description, or why it did not load, and a switch: switching a mod off unloads it at once and remembers it (in `<data folder>/native-mods.ini`) until it is switched on again.
It is opened and closed with F1 (`SMS_NATIVE_MODS_MENU_KEY`), and Escape closes it; it takes the keyboard and the first controller while open (arrows or D-pad or stick, Enter or A, Escape or B).

## Talking to other mods

**Custom events**, for news: the sender and every listener get one id from `core->event_id("stage-timer.split")`, and `core->emit` sends a payload to the listeners.
Neither needs the other to be there; `stage-timer` sends a split at each Shine and `trainer-hud` shows it as a pop-up when both are loaded.
Put a struct in the payload and check `ev->size` before reading it.

**Services**, for asking: a mod offers a struct of functions with `core->provide(mod, "radar.api", 1, &iface)`, and another fetches it with `core->require(mod, "radar.api", 1)`.
List the provider in your `mod.ini`'s `after` (or `requires`, if you cannot work without it) so it is loaded first.
Bump the version when the struct changes incompatibly, and only add fields at its end otherwise.

## Lua scripts

Mods can be written in Lua 5.4 instead of a compiled language.
The **lua** mod (`sdk/examples/lua`, a native mod that embeds Lua) runs every folder in `mods/` whose `mod.ini` says `runtime = lua`.
A script mod is a mod like any other: it has its own id, settings, options in the Mods menu, keys, HUD slots, place in the load order and `[mod:<id>]` log lines, and it can be switched off, reloaded and ordered with `after` and `requires`.
It needs no compiler and no library per platform: the same folder runs on every computer the lua mod is installed on.

[`sdk/examples/lua-challenges`](../examples/lua-challenges) is a complete script mod: a challenge against the clock each time a stage loads, with a panel, world markers, sounds, settings, a key, a module of its own, and the C `stage-timer` mod's events read from Lua.

### A script mod

```
mods/
  lua/                         the lua mod, installed once
    mod.ini
    bin/linux-x86/lua_host.so
  my-script/
    mod.ini
    main.lua
    helpers.lua                require("helpers")
```

```ini
# mods/my-script/mod.ini
id = my-script
name = My script
version = 1.0.0
runtime = lua
script = main.lua
```

```lua
-- mods/my-script/main.lua: runs once when the mod loads
local coins = 0
sms.option{ key = "show", label = "Show the counter", type = "bool", default = true }

sms.on("stage_loaded", function(ev)
  coins = 0
  sms.log("now in", sms.area_name(ev.area), "episode", ev.episode + 1)
end)

sms.on("item_collected", function(ev)
  if ev.kind == "coin" then coins = coins + 1 end
end)

sms.on("draw", function(ev)
  if ev.mode ~= "stage" or sms.state().paused or not sms.setting("show", true) then return end
  local x, y, w, h = sms.draw.hud_slot("bottom_left", 150, 22)
  if not x then return end
  sms.draw.rect(x, y, w, h, sms.rgba(0, 0, 0, 160))
  sms.draw.text(x + 8, y + 5, 13, sms.rgb(255, 230, 120), "Coins here: " .. coins, sms.SHADOW)
end)
```

The script's top level runs once as the mod loads; register handlers and options there.
An error at the top level refuses the mod (the log has the error and its traceback); an error in a handler is logged (the first five, then a note) and the game goes on.
With `SMS_NATIVE_MODS_HOT=1`, saving the script loads it again in place: a new Lua state, its settings kept.

Each script has a Lua state of its own; scripts share nothing but what they say through events (below).
They have Lua's whole standard library, `io` and `os` included, so a script mod is as trusted as a native one; `require` finds modules in the script's folder only, and only Lua source (no compiled chunks, no C modules) is loaded.

### Events in Lua

`sms.on(name, function(ev) ... end [, priority])` subscribes and returns a handle for `sms.off(handle)`.
The names are the C events' without `SMS_EV_`, in lower case: `frame`, `tick`, `draw`, `stage_loaded`, `stage_exit_request`, `stage_leaving`, `life_lost`, `game_over`, `coins_reset`, `file_selected`, `item_collected`, `pad_read`, `key`, `cutscene`, `goop_cleaned`, `object_hit`, `save_state_request`, `hide_player_model`, `player_status`, `player_damage`, `shine_collected`, `camera`, `object_move`, `mod_loaded`, `shutdown`.
Any other name is a custom event (`"stage-timer.split"`).
Priorities: `sms.FIRST`, `sms.EARLY`, `sms.NORMAL` (the default), `sms.LATE`, `sms.MONITOR`.

The handler gets a table: `ev.type` (the name), `ev.cancelled` and `ev.cancelled_by`, and the payload's fields.
**Return `true` to cancel** a cancellable event or consume a consumable one (the [same rules](#living-together) as in C).
**Change the fields** of a changeable event's table and the game takes them when the handler returns.

| Event | Fields (writable in bold) |
| --- | --- |
| `frame` | `frame`, `mode` |
| `tick` | `tick`, `dt`, `ticks_per_second` |
| `draw` | `w`, `h` (the canvas), `frame`, `mode` |
| `stage_loaded` | `area`, `episode` |
| `stage_leaving`, `life_lost`, `file_selected` | **`area`**, **`episode`** |
| `stage_exit_request` | `code`, `area`, `episode`, `actor` |
| `item_collected` | `item`, `taker`, `kind` (`"coin"`, `"red_coin"`, `"blue_coin"`, `"other"`), `actor_type`, `position` |
| `pad_read` | **`pads`**: four tables of **`buttons`**, **`stick_x`**, **`stick_y`**, **`cstick_x`**, **`cstick_y`** (-128..127), **`trigger_l`**, **`trigger_r`**, **`analog_a`**, **`analog_b`** (0..255), `connected` |
| `key` | `scancode`, `key` (its name), `down`, `repeated`, `shift`, `ctrl`, `alt` |
| `cutscene` | `name`, `position` (when it has one), `frames`, `actor` |
| `goop_cleaned` | `position`, `size` |
| `object_hit` | `object`, `sender`, `message` |
| `save_state_request` | **`reason`** (shown when you cancel) |
| `hide_player_model` | `object` |
| `player_status` | `from`, **`to`**, **`arg`**, `forced` |
| `player_damage` | **`amount`**, `underwater` |
| `shine_collected` | `shine`, `total` |
| `camera` | **`position`**, **`target`**, **`fovy`**, `aspect` |
| `object_move` | `object`, `after` |
| `mod_loaded` | `id`, `name`, `version` |
| a custom event | `data`: the payload's bytes as a string |

`mode` is `"boot"`, `"title"`, `"menu"`, `"stage"`, `"movie"` or `"other"`.
Positions are tables `{x =, y =, z =}`; functions that take one also take `{x, y, z}`.
Objects are light userdata, for `sms.object_name`, `sms.object_position` and `sms.set_object_position`.

### The sms table

| Function | Does |
| --- | --- |
| `sms.on(name, fn [, priority])`, `sms.off(handle)` | subscribe; unsubscribe |
| `sms.log(...)`, `sms.warn(...)`, `sms.error(...)`, `sms.debug(...)` | the game's log, as `[mod:<id>]`; `print` is `sms.log` |
| `sms.event_id(name)`, `sms.emit(name [, data])` | a custom event's id; send one, with a string of bytes as its payload (returns whether a handler cancelled it) |
| `sms.time()` | seconds since start |
| `sms.is_loaded(id)` | whether another mod is loaded |
| `sms.data_dir()`, `sms.info(key)` | a folder the script may write; a value from its `mod.ini` |
| `sms.id`, `sms.dir`, `sms.name`, `sms.version`, `sms.game_build`, `sms.lua_version` | about the script and the game |
| `sms.unload` | set it to a function, and it is called as the script stops |
| `sms.state()` | `{mode, stage_ready, area, episode, frame, tick, ticks_per_second, eclipse, save_file, paused}` |
| `sms.flag(id)`, `sms.set_flag(id, value)` | the save file's [flags](#flags) |
| `sms.coins()`, `sms.lives()`, `sms.shines()`, `sms.blue_coins()`, `sms.has_shine(n)` | shorthands |
| `sms.warp(area [, episode])` | leave for a stage |
| `sms.area_name(area)` | the stage's name |
| `sms.find_object(name)`, `sms.object_name(o)`, `sms.object_position(o)`, `sms.set_object_position(o, pos)` | objects |
| `sms.mario()` | `nil` outside stages, or `{position, velocity, forward_speed, yaw, health, max_health, water, max_water, has_fludd, nozzle, status, status_timer, on_ground, in_water, on_yoshi, floor, object}` |
| `sms.set_mario{position =, velocity =, forward_speed =, health =, water =}` | change what is given |
| `sms.set_status(status [, arg])` | ask for a [status](#mario-statuses) |
| `sms.camera()`, `sms.to_canvas(pos)` | `{position, target, up, fovy, aspect, near, far}`; a world point's `x, y, depth` on the canvas, or nothing behind the camera |
| `sms.play_sound(id)` | a system sound: `sms.SOUND_COIN`, `SOUND_NEWS`, `SOUND_SHINE`, `SOUND_DAMAGE`, `SOUND_HEAL`, `SOUND_APPEAR`, `SOUND_CURSOR`, `SOUND_DECIDE`, `SOUND_CANCEL`, or any `MSD_SE_SY_*` number |
| `sms.pad([port])` | `{connected, held, pressed, released, stick_x, stick_y, cstick_x, cstick_y, trigger_l, trigger_r}`; test buttons with `pad.pressed & sms.PAD_A ~= 0` (`PAD_A`, `B`, `X`, `Y`, `Z`, `L`, `R`, `START`, `UP`, `DOWN`, `LEFT`, `RIGHT`) |
| `sms.key_down(key)`, `sms.key_pressed(key)` | a key (a name such as `"F3"`, or a scancode) is down; went down this frame |
| `sms.key(name)`, `sms.key_name(scancode)` | names and scancodes |
| `sms.bind_key(action, default)` | the scancode for an action (the player's `key.<action>` setting or the default), with clash warnings |
| `sms.capture([on])`, `sms.captured_by()` | take the controllers and keyboard for a menu |
| `sms.setting(key, default)` | a setting, read as the default's type (boolean, integer, number or string) |
| `sms.set_setting(key, value)`, `sms.save_settings()` | change one; write them now |
| `sms.option{key, label, type, default, min, max, step, choices, on_change}` | a setting in the Mods menu; `type` is `"bool"`, `"int"`, `"float"`, `"choice"` (with `choices = {"a", "b"}`) or `"action"`; `on_change(key)` after each change |
| `sms.rgb(r, g, b)`, `sms.rgba(r, g, b, a)` | colours (or write `0xRRGGBBAA`) |

`sms.draw` mirrors the [C drawing](#drawing), in canvas units, only inside `draw` handlers. Colours default to white.

| Function | Draws |
| --- | --- |
| `canvas()` | returns `w, h` |
| `rect(x, y, w, h, c)`, `rect_outline(x, y, w, h, thickness, c)`, `gradient(x, y, w, h, top, bottom)` | rectangles |
| `line(x0, y0, x1, y1, thickness, c)`, `triangle(x0, y0, x1, y1, x2, y2, c)`, `circle(x, y, r, c)`, `circle_outline(x, y, r, thickness, c)` | shapes |
| `text(x, y, size, c, text [, flags])`, `text_width(size, text)` | text; returns its width. Flags: `sms.SHADOW`, `sms.OUTLINE`, `sms.CENTER`, `sms.RIGHT` (add them) |
| `image_load(path)`, `image_create(w, h, rgba)`, `image_free(img)`, `image_size(img)` | images: a file in the mod's folder, or a string of `w * h * 4` RGBA bytes |
| `image(img, x, y, w, h [, tint])`, `image_region(img, x, y, w, h, u0, v0, u1, v1 [, tint])` | draw one |
| `clip_push(x, y, w, h)`, `clip_pop()` | clipping |
| `world_line(a, b [, thickness, c])`, `world_box(centre, half [, thickness, c])`, `world_ring(centre, r [, thickness, c])` | the world |
| `world_text(pos, size, c, text [, flags [, scaled]])` | text on a world point |
| `hud_slot(anchor, w, h)` | `x, y, w, h` of a panel's place, or nothing when it does not fit; anchors `"top_left"`, `"top"`, `"top_right"`, `"left"`, `"center"`, `"right"`, `"bottom_left"`, `"bottom"`, `"bottom_right"` |

### Lua and C mods together

Lua and C mods hear the same events in one order and share HUD slots, keys and the Mods menu.
They talk through custom events: a payload is bytes, so pack C structs with `string.pack` and read them with `string.unpack`.
The C `stage-timer` sends `struct { float seconds; int shine; }` as `"stage-timer.split"`, and `lua-challenges` reads it:

```lua
sms.on("stage-timer.split", function(ev)
  local seconds, shine = string.unpack("<fi4", ev.data)
end)
```

and a script can send one a C mod reads: `sms.emit("my-script.score", string.pack("<i4f", 12, 3.5))`.

### Other languages

The lua mod is an ordinary native mod using two functions any mod can: `core->host_runtime(mod, "lua", start, stop, user)` makes it the runner of `runtime = lua` mods, and `core->mod_info(script, "script")` names a script's file.
A mod embedding another language (Python, Wren, JavaScript) does the same with its own runtime name; the game gives each script mod its own `SmsMod`, so whatever the script calls through the API is its own.

## Time, units and frame rate

- **Frames and ticks.** `SMS_EV_FRAME` and `SMS_EV_DRAW` come once a displayed frame. `SMS_EV_TICK` comes once a simulation step of a stage, `ticks_per_second` (30, 60 or 120, from the port's `frame_rate` setting) times a second while the stage runs, and not while it is paused or loading. Count game time in ticks (or `dt`, the seconds a tick), not in frames.
- **Real time.** `core->time()` is seconds since start; it goes on through pauses and loading.
- **Distances** are the game's units (100 to a metre, roughly: Mario is about 160 tall). **Velocities** (`SmsPlayer.velocity`, `forward_speed`) are units a tick, so their meaning changes with the frame rate; for a speed in units a second, measure the distance moved over `dt` (as `trainer-hud` does) or multiply by `ticks_per_second` with care.
- **Angles**: `SmsPlayer.yaw` is degrees, 0 facing +Z and 90 facing +X; the raw `face_angle` is the game's 16-bit angle (65536 to a turn). `SmsCamera.fovy` is degrees.
- **The world's axes**: +Y is up.

## Multiplayer, save states and Eclipse

- **Multiplayer** (`ext/multiplayer`) runs one game per player, each with its own mods. Mods that only show things are safe. A mod that changes how the game plays (mark it `gameplay = 1`) can make the players' games disagree unless every player runs it with the same settings; the session does not yet compare mod lists (planned).
- **Save states** capture the game's memory, not the mods'. After a load, a mod's own state (timers, trails) may not match the game; mods that care should check `game->state` and the player each frame rather than trusting what they kept. (A save/load event is planned.)
- **Super Mario Eclipse** builds run native mods too. `game->state` says `eclipse = 1`; its stages and flags are numbered differently, and its own code patches the game in places, so mods that change gameplay should be tested there.

## Game reference

### Areas

`game->area_name(area)` gives the name in the game's language. The vanilla game's areas (`SmsEvStage.area`; `SMS_WARP=area,episode` takes the same numbers, and `SMS_WARP_LIST=1` prints every stage and episode):

| Area | Stage | Area | Stage |
| --- | --- | --- | --- |
| 0 | Delfino Airstrip | 13 | Pinna Park (the park) |
| 1 | Delfino Plaza | 14 | Sirena Beach |
| 2 | Bianco Hills | 15 | Delfino Plaza (title screen) |
| 3 | Ricco Harbor | 16 | Noki Bay |
| 4 | Gelato Beach | 20–24, 29 | Delfino Plaza secret courses |
| 5 | Pinna Park (beach) | 30 | Ricco Harbor secret course |
| 6, 7 | Sirena Beach | 31 | Noki Bay secret course |
| 8 | Pianta Village | 32, 33 | Gelato Beach secret courses |
| 9 | Noki Bay | 46, 47, 55 | Bianco Hills (secrets, boss) |
| 12 | Delfino Plaza | 48, 59 | Ricco Harbor |
| 40, 51, 56 | Sirena Beach | 41, 50, 58 | Pinna Park |
| 42 | Pianta Village | 44, 57 | Noki Bay |
| 52, 60 | Corona Mountain | | |

Episodes count from 0 (`episode + 1` is the number the game shows).

### Flags

`game->flag_get` / `flag_set` read and write the save file's flags (`TFlagManager`):

| Flag | Is |
| --- | --- |
| `0x10000 + n` | Shine `n` (0–119) collected |
| `0x20001` | lives |
| `0x20005 + area` | the coin record of an area |
| `0x40000` | Shines collected |
| `0x40001` | blue coins collected |
| `0x40002` | coins in the current stage |

Other flags (events seen, nozzles unlocked, the plaza's state) are in the decomp's `src/System/FlagManager.cpp`; setting them can put the story in a state it does not expect.

### Item kinds

`SmsEvItem.kind`: `SMS_ITEM_COIN`, `SMS_ITEM_RED_COIN`, `SMS_ITEM_BLUE_COIN`, `SMS_ITEM_OTHER` (Shines, nozzles and other `TItem`s; `actor_type` tells them apart). 1-Up mushrooms are not `TItem`s and do not come through this event.

### Hit messages

`SmsEvHit.message` (the decomp's `HIT_MESSAGE_*`): `0x0` stomped, `0x1` ground-pounded, `0x2` pushed up, `0x3` super ground pound, `0x4` taken, `0x6` put down, `0x7` thrown, `0x9` electric shock, `0xC` punched, `0xE` touched, `0xF` sprayed with water.

### Nozzles

`SmsPlayer.nozzle`: 0 spray, 1 rocket, 2 underwater, 3 Yoshi, 4 hover, 5 turbo, 7 diving helmet.

### Mario statuses

`SmsPlayer.status` and `SmsEvPlayerStatus` use the game's status numbers (all in the decomp's `include/Player/MarioStatus.hpp`). Some:

| Status | Value | Status | Value |
| --- | --- | --- | --- |
| standing | `0x0C400201` | dive | `0x00000891` |
| running | `0x04000440` | rocket | `0x0000088B` |
| jump | `0x02000880` | landing | `0x0000088C` |
| second jump | `0x02000881` | wall slide | `0x000008A7` |
| triple jump | `0x00000882` | swimming | `0x000022D2` |
| back flip | `0x00000883` | hanging | `0x3800034B` |
| long jump | `0x00000888` | Shine get | `0x00001302` |

Bits: `0x800` airborne, `0x2000` swimming, `0x400` running; `status & 0x1C0` is the kind (`0x000` waiting, `0x040` running, `0x080` jumping, `0x0C0` swimming, `0x100` cutscene, `0x140` special, `0x180` action).

## Debugging

| Setting (`settings.txt`) | Variable | Does |
| --- | --- | --- |
| `native_mods = off` | `SMS_NATIVE_MODS=0` | load no native mods |
| `native_mods_disabled = a;b` | `SMS_NATIVE_MODS_DISABLED` | leave these mods out |
| | `SMS_NATIVE_MODS_DIR=dir;dir` | look for mods in these folders instead of `mods/` |
| | `SMS_NATIVE_MODS_DATA=dir` | keep mods' settings and data here |
| | `SMS_NATIVE_MODS_DEBUG=1` | show `SMS_LOG_DEBUG` lines and the API's own notes |
| | `SMS_NATIVE_MODS_HOT=1` | **hot reload**: load a mod again when its library or script changes (checked twice a second) |
| | `SMS_NATIVE_MODS_MENU_KEY=F2` | the Mods menu's key |

- **The log** (the terminal, or the launcher's log) has a `[mods]` summary, each mod's `[mod:<id>]` lines, why any mod did not load, and every key clash.
- **Hot reload.** With `SMS_NATIVE_MODS_HOT=1` the game loads a copy of each library, so you can rebuild while it runs; when the library changes it unloads the mod (calling `sms_mod_unload`) and loads the new build, keeping its settings. Keep the state you want to survive in settings.
- **A debugger.** The mod is an ordinary shared library in the game's process: `gdb build/linux-32/sms`, `break sms_mod_load`, and its symbols load with it (build it with `-g`).
- **A crash in a mod** takes the game down; the backtrace names the mod's library. Start with `SMS_NATIVE_MODS=0` to rule mods out, then `SMS_NATIVE_MODS_DISABLED` to find the one.

## Compatibility

- The game loads a mod whose `sms_mod_api_version()` has the game's **major** version; another major is refused with a message.
- The current version is **1.1** (1.1 added `core->mod_info` and `core->host_runtime`, for script mods).
- **Minor versions only add**: events, functions at the ends of tables, fields at the ends of structs. A mod built for an older minor runs on a newer game unchanged. A mod built for a newer minor runs on an older game if it checks what it uses with `SMS_API_HAS` and `ev->size` (the game logs a note when a mod is newer than it).
- Nothing in the API depends on the game's internal layout, so game updates (and the decomp's progress) do not break mods. `api->native` is the exception, by design.

## Design notes

### Why a C ABI and events

The port compiles the game from the decomp as native code, so there are no fixed addresses to patch as GameCube code mods do, and the game's classes change layout with every compiler, pointer size and decomp update.
A C function-table API keeps mods independent of all of that: one build of a mod works with every game build of the same major version, mods can be written in any language, and the game knows exactly what each mod registered, which is what lets it order, isolate, unload and hot-reload them.
Hooks are named events at chosen places in the game (`port_ext.h`), each general enough for many mods, rather than detours on arbitrary functions, which two mods cannot share predictably.

### Built-in extensions

`ext/` holds the port's built-in extensions (multiplayer, the demo pilot), which hook the game through `port_ext.h`.
The native mod API is one more of them (`ext/modapi`): it turns those hooks into mod events and adds the rest (loading, drawing, input, settings).
New hook points go into `port_ext.h` once and serve both built-in extensions and mods.

### What modders will want, and where it stands

| Need | Today | Planned |
| --- | --- | --- |
| HUDs, overlays, information | `draw`, `hud_slot`, `camera->world_to_canvas`, `player`, `game` | the game's own font; textures from the game's archives |
| Markers in the world | `world_line/box/ring/text` (over the scene) | drawing inside the scene, hidden by walls (a GX layer in the game's frame) |
| New moves, movement tweaks | `SMS_EV_TICK` + `player->set_*`, `SMS_EV_PLAYER_STATUS` | per-status hooks (a status's code each tick), Mario's parameters |
| Rules (lives, damage, coins, exits) | `SMS_EV_PLAYER_DAMAGE`, `GAME_OVER`, `COINS_RESET`, `STAGE_EXIT_REQUEST`, `STAGE_LEAVING`, flags | timers, the pause menu, Shine conditions |
| Camera mods | `SMS_EV_CAMERA` (position, target, field of view) | camera modes, roll |
| Input remapping, scripted play, TAS tools | `SMS_EV_PAD_READ`, `SMS_EV_KEY`, `capture` | mouse, more controllers |
| Objects and enemies | `SMS_EV_OBJECT_HIT`, `SMS_EV_OBJECT_MOVE`, `find_object`, positions | spawning the game's actors, an object list with kinds, per-object hooks |
| New content | files in `mods/<name>/files/` (game file mods) | a native mod switching on its own `files/`; new stages |
| Sound | `audio->play_system_sound` | sounds placed in the world, a mod's own WAV/OGG, music |
| Messages and text | | the game's talk boxes and messages |
| Saving a mod's progress | `config`, `core->data_dir` | per-save-file data; save-state events |
| Multiplayer-aware mods | | the session comparing `gameplay` mods; mods sending their own messages over it |
| Scripting | Lua script mods (the `lua` mod), `core->host_runtime` for other languages | services for scripts; a sandboxed mode for script mods |
| Anything else | `api->native` + the decomp's headers (same build only) | the code mods' retail-address hooks (`sms_modhook.h`, about 500 places the Eclipse port uses) offered to native mods |

### Scripting

Lua is a mod, not part of the game: the `lua` mod embeds Lua 5.4 and runs script mods through `core->host_runtime`.
The game's part is small and general: it treats a `runtime = <name>` folder as a mod of its own (id, settings, menu row, order, hot reload) and hands its start and stop to the mod hosting that runtime.
Scripts need no compiler and no binary per platform, and their speed is plenty for HUDs, rules and menus; anything heavier, or anything needing a new hook, stays in C.
Lua 5.4 is small (the whole lua mod is about 350 KB stripped), embeds cleanly, sandboxes well, and is familiar from many games' mod scenes.

## Adding to the API (port developers)

The runtime is [`ext/modapi`](../../ext/modapi/README.md). To add an event:

1. If the game needs a new hook point, add it to `PortExtension` and the `port_ext_*` functions (`src/port_include/port_ext.h`, `ext/core/port_ext.cpp`) and call it from a `zzzzz-ext-*` decomp patch, general enough for other extensions.
2. Add the event's id and payload at the end of the lists in `sdk/include/sms_mod.h`, and bump `SMS_MOD_API_MINOR`.
3. In `ext/modapi/game/modapi_game.cpp`, build the payload in the extension's hook and call `modh_dispatch` (skip it when `modh_wants` says nobody listens). If it is cancellable or consumable, add it to `cancellable()` / `consumable()` in `ext/modapi/host/mod_runtime.cpp`.
4. Document it in the [Events](#events) table here.

Functions go at the end of their table's struct in `sms_mod.h` and its initializer in the runtime; never reorder or remove (that is a major version).
