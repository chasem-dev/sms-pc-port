# ext/: extensions

Features built into the game that sit beside it rather than in it: the game calls them at a few chosen points, and each keeps its own code in its own folder here.
They are the start of a code-mod framework for the port: a new feature that needs to run every frame, react to a stage loading, or add objects to a stage does it through these hooks instead of patching the game in many places.

| Folder | What it is |
| --- | --- |
| [`core/`](core/port_ext.cpp) | the registry: the game's hooks call each registered extension in turn |
| [`multiplayer/`](multiplayer/README.md) | up to four players in one game over the network |
| [`demo/`](demo/README.md) | plays Mario from a script, as a player holds the controller, for trailers and footage |

## The hooks

The game calls the functions of [`src/port_include/port_ext.h`](../src/port_include/port_ext.h) from one patch, [`decomp-patches/zzzzz-ext-01-hooks.patch`](../decomp-patches/zzzzz-ext-01-hooks.patch).
An extension fills in a `PortExtension` with the hooks it needs, leaving the rest null:

| Hook | When the game calls it | What it may do |
| --- | --- | --- |
| `frame` | the top of every frame of `TApplication`'s loop: title, file select, stages and movies alike | anything between frames |
| `afterMovement` | every simulation tick of a stage, after the movement pass and before anything is posed for drawing | move objects to where they should be |
| `stageSetup` | a stage's objects are loaded and set up (the end of `TMarDirector::setupObjects`), with the stage's heap current | add objects to the stage; what it allocates lasts as long as the stage |
| `allowNextStage` | `TMarDirector::setNextStage` asks to leave for another area (pipes, paintings, scripts) | return 0 to refuse |
| `moveStage` | `TMarDirector::moveStage` is about to leave for `gpApplication.mNextArea` (every way out of a stage) | change the destination |
| `missNextArea` | Mario lost a life and has lives left | change where he goes |
| `takeGameOver` | Mario lost his last life | return 1 to carry on as after any lost life instead of the game over screen |
| `keepCoinsOnMiss` | losing a life is about to reset the coins | return 1 to keep them |
| `afterFileSelect` | a file was chosen at file select | change where it starts |
| `itemTaken` | a `TItem` (coin, blue or red coin, 1-up) is picked up | note it |
| `allowSaveStates`, `saveStatesOffReason` | the controls or menu ask for a save state | return 0 to refuse, and say why |
| `hidePlayerModel` | Mario's models are about to be drawn | return 1 to leave them out (and draw the player yourself) |
| `padRead` | the controllers were read (`PADRead`), before the game sees them | change the input (scripted play, input mods) |

Every hook runs on the game thread inside the game's own steps, so an extension uses the game's globals (`gpApplication`, `gpMarDirector`, `gpMarioOriginal`, `TFlagManager`) as the game does there.
With no extension switched on, each hook is a loop over nothing.

## Writing an extension

An extension is a folder `ext/<name>/` with:

- `game/*.cpp`: game code, built into the game's library with the game's flags and headers (C++03, Shift-JIS string literals, the decomp's include paths). It registers itself from a static constructor:
  ```cpp
  namespace {
  PortExtension s_extension = { "name", frame, stageSetup, /* ... */ };
  struct Register { Register() { port_ext_register(&s_extension); } } s_register;
  }
  ```
  Game code's `new` allocates from the game's current JKR heap: allocate in `stageSetup` for things that belong to a stage, and keep fixed arrays for the rest.
- optionally `net/*.cpp` and `sync/*.cpp`: platform code, built into the executable with the host's compiler settings (C++11 and the host's libraries, sockets and threads included). Keep the two halves apart behind a small C header, as multiplayer does with [`mp_game_api.h`](multiplayer/mp_game_api.h).
- `tests/` folders, left out of the game build, for host tests.

[`ext.cmake`](ext.cmake) picks up every `ext/*/game`, `ext/*/net` and `ext/*/sync` source by itself.
A feature that needs a hook the game does not have yet adds it to `port_ext.h`, `core/port_ext.cpp` and the hooks patch, each hook general enough for other extensions to use.
On screen, `GXPC_ExtOverlaySet` (`platform/gx`) shows a notice near the top of the window and a status panel at its right.
