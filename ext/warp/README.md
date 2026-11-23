# ext/warp: start straight in a stage

With `SMS_WARP=area,episode[,shines]` and `SMS_WARP_DIRECT=1`, the game starts in that area's episode. There are no logos, no opening movie, no title and no file select. It plays as a new game, and the screen stays black until the stage fades in.
SMS Launcher's World editor uses this for Play in game, with the level laid over the disc as a mod (`SMS_MOD`) and a memory card of its own (`SMS_SAVE_DIR`).

| Variable | What it does |
| --- | --- |
| `SMS_WARP=area,episode[,shines]` | the area and episode, and optionally the Shine count (flag `0x40000`) |
| `SMS_WARP_DIRECT=1` | start there directly instead of after file select |
| `SMS_WARP_FLAGS=flag=value[,flag=value...]` | save-file flags to set on the new game before the stage loads, in C notation (`0x10021=1` is Yoshi's Shine, which a stage's Yoshi eggs need, and `0x1038F=1` Yoshi hatched in Delfino Plaza, which the plaza's need) |

Without `SMS_WARP_DIRECT`, `SMS_WARP` keeps its old meaning: the file chosen at file select goes there (`decomp-patches/port-02-warp-after-file-select.patch`).

## How

- **Intro:** on its first frame, before the logos, the extension turns on `SMS_SKIP_INTRO` (`port_skip_intro`).
- **Black screen:** every frame until the stage is in, it draws black over the screen with `GXPC_ExtOverlayScreen`, as a multiplayer guest's join does.
- **Leaving the title:** once the title stage (area 15) has set itself up (director state 4, then 30 frames), it sets the episode flag (`0x40003`, as an episode select does) and calls `TMarDirector::setNextStage` for the area. This is the same way a multiplayer guest leaves the title for the host's stage (`ext/multiplayer`, `skipTitle`).
- **Flags:** they are a new game's, as `TFlagManager`'s constructor leaves them (`firstStart`), so there is nothing to load. `SMS_WARP_FLAGS` sets more with `TFlagManager::setFlag` just before `setNextStage`, so the stage loads with them. The World editor uses it for objects that are only there on a file that has got that far: a Yoshi egg is gone (`TEggYoshi::load`) without Yoshi's Shine (`0x10021`), or in Delfino Plaza without flag `0x1038F`.
- **Story movies:** the first visit to some stages plays a movie (`TApplication::checkAdditionalMovie`). The extension marks those movies as seen, as the game does once they have played:
  - the airstrip's (flag `0x30009`);
  - Delfino Plaza's episode 0 (`0x3000B`), which then asks "Do you want to hear my explanation again?";
  - Delfino Plaza's episode 1 (`0x3000C`);
  - FLUDD taken at a secret course or in Pianta Village (`0x3000D`).

It warps once. If the player goes back to the title, the game carries on from there as usual.
