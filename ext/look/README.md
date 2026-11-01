# ext/look: Customize Mario's colours stay on Mario

SMS Launcher's Customize Mario repaints Mario's textures into a texture pack the game reads first (`SMS_MARIO_LOOK`, `platform/gx/src/gx_hires.cpp`).
The game finds a pack's pictures by the texture's hash, not by the model that draws it.

In Super Mario Eclipse, Luigi and Il Piantissimo draw one of Mario's own textures: his mouth (`H_ma_mouse1_s3tc`).
Without this extension, a player who chose either of them would wear Mario's custom skin colour around their mouth, so the launcher used to leave the mouth out of the pack, and Mario's chin kept his usual skin.
The launcher's Luigi (a pack in `mods/characters` made from Eclipse's, played in Sunshine) does too, as would any pack made from Mario's own textures.

## How

When this game's player isn't Mario (`MpLocalBody`, from the archive Eclipse's character select mounted as `mario`, or a pack of `mods/characters` chosen: `port_character_selected`, asked each tick since F4 changes it in place), every texture of their body model is drawn plain (`GXPC_SetTexturePlain`): their own, never Mario's look.
Their cap and hands draw the body's texture data, so those are covered too.
It runs as each stage is set up (`stageSetup`) and every tick (`afterMovement`), where one call (`GXPC_TextureIsPlain`) checks the marks are still there: multiplayer lets go of every plain texture as a stage is set up, and may of our own when looks change.

Only with `SMS_MARIO_LOOK` set, and only with another character than Mario (Eclipse's or a pack); otherwise it does nothing.
In multiplayer the other players' models already keep out of the look (`mpg_dress_plain`).

The launcher paints Mario's mouth in Eclipse only for a game that has this extension (it looks for `ext/look/game/look_game.cpp`).

## HD textures

A plain texture shows a texture pack's own image where there is one (`hiresPackName`): Mario's look takes the texture's name from the packs, and `gx_hires.cpp` keeps the pack's image for that name apart.
So Luigi's and Il Piantissimo's mouth stays HD with Customize Mario's colours on, as do the other players' textures in multiplayer that have no picture of their own.
