# Minecraft crossover

Steve replaces the visible player using Minecraft's head, torso, arm and leg proportions. His skin, tools, wood, crafting table, door, chest, GUI backgrounds, hearts and font use original Minecraft Java Edition 1.20.1 PNGs from the [public GitHub asset mirror](https://github.com/InventivetalentDev/minecraft-assets/tree/1.20.1/assets/minecraft/textures). Sunshine's third-person camera, movement, collision controller and real FLUDD model and water simulation are retained.

FLUDD mounts on Steve's torso and shares his body rotation. Its nozzle animations and water emitters remain active. The axe and sword have thickness and mount by the bottom of their handles at Steve's right palm, with their sprite planes perpendicular to his torso and the approved flipped blades pivoted 45 degrees upward for a forward grip.

## Play

The crossover is enabled in this checkout's `settings.txt` with `minecraft = on`. Run `./run-minecraft.sh`, optionally passing your supported disc image:

```sh
./run-minecraft.sh "/path/to/GMSE01.iso"
```

Build with `SMS_ARCH=64 ./build.sh IMAGE` after source changes. `SMS_MINECRAFT=0 ./run.sh` temporarily disables the crossover; `minecraft = off` disables it in settings. The feature is off when no setting or environment switch enables it.

| Action | Keyboard / mouse | Controller |
| --- | --- | --- |
| Select hotbar slot | Mouse wheel, 1–9, or `[` / `]` | D-pad left / right |
| Mine a targeted trunk/block or swing sword | Hold left click or G | Hold D-pad up |
| Toggle help | H | — |
| Toggle placement preview | P | — |
| Toggle mouse build mode | B | — |
| Open / close inventory | Tab | — |
| Place selected block | Right click | — |
| Place against a table/chest or stack atop a targeted block | Shift + right click | — |
| Open targeted table/chest, toggle door | Right click | — |
| Close menu | Tab or Esc | — |
| Use FLUDD | E by default | Right trigger |
| Switch spray / equipped attachment | V by default | X |
| Move, jump and rotate camera | Usual Sunshine controls | Usual Sunshine controls |

Aim the center crosshair at a coconut palm or placed block in Delfino Plaza. A thin outline marks the selected block or trunk region. Direct targeting uses the camera ray, with reach measured from Steve and native level geometry blocking selection. When that ray misses a nearby target, a short downward check in front of Steve finds ground or a placed block while standing or walking. Directly aimed blocks and trees take priority; intervening terrain and placed blocks stop the lower check. Rotate the Sunshine camera with its usual controls (I/J/K/L on the default keyboard bindings). An optional wireframe previews the exact placement cell; P toggles it.

Hold left click or G to mine continuously. Original Minecraft cracks advance through ten stages while swings, wood-hit sounds and original-texture particles repeat. An axe fells a native palm in 1.4 seconds; an empty hand takes 4.2 seconds. Placed wood uses its Minecraft hardness and iron-axe speed (planks/logs 0.5 seconds with an axe, 3 seconds by hand; table/chest 0.625/3.75 seconds; door 0.75/4.5 seconds). Releasing, changing tools or targets, moving out of reach, opening a menu or losing focus cancels progress. Breaking produces textured, spinning world drops; collection plays the original pickup sound and briefly moves the item toward Steve. Full inventories leave drops available and partial stacks collect only the available amount. A full chest spills its block and every stored stack; entity capacity is reserved before it changes, so contents cannot be lost.

Swing the diamond sword to attack the nearest enemy in a forward area rotated with Steve's model: 180 units long, 120 units wide, with the enemy's collision radius included at the edges and vertical reach extending down to foot level for small blobs. Camera direction does not affect melee targeting. Quick clicks are retained even when press/release arrive between updates. Detection continues through the 0.3-second swing after button release, so approaching enemies can enter reach during the animation. A confirmed hit briefly flashes the crosshair gold and shows “SWORD HIT.” It sends the same stomp message as Mario jumping on an enemy, preserving native vulnerability, death animations, effects and drops. Native respawned enemies remain hittable even when they retain the historical killed flag; dead, hidden, non-colliding and currently dying enemies are excluded. Walls and placed blocks block attacks, and each swing targets one enemy with a 0.38-second cooldown. A nearby enemy takes priority over mining a block under the camera crosshair. The sword does not chop native palms.

Press **B** once to enter mouse build mode and again to return to normal Sunshine controls. The pointer becomes the placement crosshair. Moving it turns the camera and Steve; the camera sits closer over his shoulder and stops at native level geometry. Holding the pointer near a screen edge keeps turning so the mouse can stay free for clicking. Right-click places or interacts at the pointer, and left-click mines there. The camera pauses while inventory or container menus own the pointer. The toggle uses the physical B key, which is free in the supplied keyboard bindings; the GameCube B action still uses Shift/C.

## Inventory and crafting

Two oak planks vertically adjacent in either the 2×2 inventory grid or the 3×3 crafting table produce four sticks. The recipe can be shifted within either grid. Sticks stack to 64, use the original item texture, and can be held or stored in chests.

The inventory has 36 usable slots, including the nine-slot hotbar. Tools occupy one slot each; building materials stack to 64. Left click picks up, moves, merges or swaps a stack. Right click picks up half a stack, or deposits one item from the carried stack. Shift + click moves items between hotbar and storage, or between inventory and an open chest. Hovering a slot highlights it and shows an item tooltip; carried items follow the mouse. Containers hide the gameplay hotbar, selected-item label and action prompts. Instructions are available through H. The inventory renders the actual 3D Steve geometry, approved held-tool grip and native FLUDD pose, with mouse-tracking head movement, centered and clipped to the original black preview rectangle. Separate scene-owned J3D pose buffers keep the UI from altering native equipment animation or water emitters.

The inventory's 2×2 crafting grid makes planks, sticks and tables. Place a table in the world and face it, then right-click to open its 3×3 crafting GUI.

| Recipe | Arrangement | Output |
| --- | --- | --- |
| Oak planks | One log in any crafting slot | 4 planks |
| Sticks | Two planks vertically adjacent in either grid | 4 sticks |
| Crafting table | Four planks filling a 2×2 square | 1 table |
| Oak door | Six planks in two adjacent columns, three rows tall, in the table | 3 doors |
| Chest | Eight planks around an empty center in the table | 1 chest |

Click the result to craft once. Shift + click the result crafts as many batches as the ingredients and free inventory space allow. Closing the menu returns the carried stack and remaining crafting ingredients to the inventory. If there is insufficient room, put them away before closing. The chest armor slot equips a FLUDD nozzle; the other armor slots remain decorative.

## Achievements

Before Taking Inventory is earned, the original book/panel/font reminder says **“Press 'E' to open your inventory”** in the top right. The original E wording is retained by request; Tab actually opens inventory. It slides in after the original 2.5-second delay and remains until Tab opens inventory, then gives way to the short Taking Inventory unlock toast. The earned bit suppresses the reminder after a saved restart. E retains Sunshine's native spray binding, including while aiming at a placed block; right-click handles containers and doors. Native/custom FLUDD bindings and controller triggers are unchanged.

Opening inventory unlocks **Taking Inventory**, collecting the first oak log unlocks **Getting Wood**, and crafting a table unlocks **Benchmaking** (the user-requested title; the original Java language file calls it “Benchmarking”). Each unlock is saved once per world and queued for a three-second toast. The panel uses the unchanged Java 1.11.2 achievement background, original bitmap font and original book/log/table textures, including the original “Achievement get!” header and slide animation. Java 1.11.2 is the legacy achievements era; assets and pinned URLs/checksums are in `assets/minecraft/legacy-achievements/`. No generated artwork is used.

The reminder and unlock toast follow `widescreen_hud`: `edges` anchors the complete panel, icon and text to the widescreen window's right edge, while `center` retains the centred 4:3 HUD position.

## Nozzle equipment

Native nozzle boxes still break and throw their original pickups. Walking into a Hover, Rocket or Turbo pickup adds one matching item to inventory; a full inventory leaves the pickup available. Picking it up grants the normal stage nozzle unlock without running the equipment cutscene. Nozzles occupy one slot each and can also be stored in chests. New inventories start with one Hover, Rocket and Turbo nozzle, plus the iron axe and diamond sword. Existing saved inventories are loaded intact; starting items are not reissued on every load.

Open the inventory with Tab and move a nozzle into the chest armor slot (the second armor slot down), or Shift + click a nozzle to equip it. Equipping another nozzle swaps the old one into its inventory slot. Shift + click the chest armor slot to unequip, or move the equipped item with the mouse. Tools and blocks are rejected by the chest slot. The actual native FLUDD attachment follows the armor item, including its animations, controls and water use; V (controller X) switches between the normal spray and the equipped attachment, including the native transition animation. The chest slot selects which secondary attachment is available. Unequipping restores the native nozzle that was active before equipping. Yoshi and special underwater modes retain their native handling. The PC build also omits retail’s unused prop-rotation store through the abandoned `TNozzleTurbo` type, which writes beyond the actual `TNozzleTrigger` and could corrupt its animation allocations. Native Turbo thrust, water use and prop animation still run. Equipped armor survives saves and scene changes; its override starts after native gameplay and FLUDD initialization. Save version three reads version-one worlds with empty armor and preserves version-two armor.

Icons are native J3D renders of `normal_nozzle_item.bmd`, `rocket_nozzle_item.bmd` and `back_nozzle_item.bmd`, with the original shared `nozzleItem.bmt` colored material, viewed at an isometric 45-degree yaw and 35.264-degree elevation. Their transparent PNGs and SHA-256 source records are in `assets/minecraft/sunshine-rendered/`. No native models are exported into the repository. For regeneration, run the game in Delfino Plaza with `SMS_NOZZLE_RENDER=1 SMS_GX_SCALE=4` and capture a field; `tools/minecraft/extract_nozzle_icons.py --capture FIELD.ppm --disc IMAGE` extracts the three icons, then `import_assets.py` packs them. Item icons use atlas sprites, independent of which models a stage loads. The developer icon mode loads original pickup models; the inventory preview separately clones the already initialized native FLUDD pose into scene-owned rendering buffers.

`tools/minecraft/check-nozzles.sh IMAGE [32|64] [OUTPUT_DIRECTORY]` checks a native box stomp/drop, full-inventory pickup protection, collision collection, live mouse and Shift armor transfers, rejected tools, all three forced attachments, native Hover water use and Turbo boost, native spray/attachment switching and a cold restart. Its fixture moves an initialized Rocket box to a clear plaza floor and enables it for inspection; Turbo is supplied in inventory for its equipment check. `nozzle_test.cpp` additionally checks all three actor-to-item mappings, stack limits, placement rejection, atomic slot swaps and old-save migration. `record_nozzles.py --disc IMAGE --output DIRECTORY` records the same live interactions with native audio.

## Building and chests

Place logs, planks, tables, doors and chests in Delfino Plaza. Placement snaps to a shared 80-unit block grid aligned to the ground at the first placement. Targeting an existing block places adjacent to its face; Shift + right click stacks above it. Blocks reject occupied cells and placements that overlap Steve or the level walls. The world supports up to 512 placed objects.

Placed blocks support standing and jumping. Doors reserve two vertical cells, rotate around their hinges when interacted with, block passage while closed and allow passage while open. A chest opens a 27-slot storage GUI. Left/right/Shift clicks use the same transfer controls as the inventory, and its lid opens while its GUI is active.

Chests use Minecraft's original single-chest body, lid and latch dimensions and cube UV layout. The inventory icon and held chest render those same parts, including the lid seam and textured latch. Placed chests face the player who placed them, rotate around the center of their grid cell, and have collision bounds matching their inset body and 14-pixel height. The lid and latch ease together around the rear hinge over 0.4 seconds when storage opens or closes. Reopening during a close reverses from the current angle. Opening and successful menu closing each play one original Minecraft sound. Animation advances while the GUI owns input and starts closed after a scene reset or load; its transient progress is not written to the save.

`tools/minecraft/record_chest.py --disc IMAGE --output DIRECTORY` records native placement, storage interaction, the inventory icon and four chest orientations using an isolated save directory. It also checks intermediate lid angles, both endpoints and reversal. `--lid-preview` omits the storage overlay through a debugger breakpoint so the actual world-space lid and latch are visible; menu state, live interactions and audio remain active.

The chest recorder also checks that the native controller lands Steve on the visible lid. Pass `--executable build/linux-32/sms` to exercise the 32-bit build. The cuboid dimensions and UV orientation follow Minecraft's [ChestRenderer](https://github.com/mahtomedi/minecraft/blob/main/src/main/java/net/minecraft/client/renderer/blockentity/ChestRenderer.java) and [ModelPart](https://github.com/mahtomedi/minecraft/blob/main/src/main/java/net/minecraft/client/model/geom/ModelPart.java).

Inventory, carried crafting items, placed objects, door states and chest contents save automatically to `minecraft-world.dat` alongside the native card files. The default Linux location is `$XDG_DATA_HOME/sms-port/card-a`, or `~/.local/share/sms-port/card-a`; `SMS_SAVE_DIR` selects another directory. `SMS_MINECRAFT_SAVE` can select the exact sidecar filename. The checksummed file uses an atomic replacement and a previous valid `.bak` snapshot for recovery, and is shared by 32-bit and 64-bit builds. Minecraft data does not alter the native memory-card format. Stage changes preserve it; native trees regenerate, while uncollected world drops and earned achievements persist. Version-three saves add sparse drop records and achievement bits while retaining version-one/two compatibility.

## Sound effects

In Minecraft mode, losing health plays one original Minecraft player-hurt sample and blends a red overlay into Steve’s skin for half a second. The same transient tint appears on the inventory preview; tools, FLUDD, blocks and GUI retain their own colors. Native damage voices and impact beeps are suppressed, and goo sliding no longer plays its two pollution-slide loops or repeated startled voice. Other sliding surfaces and the base game keep their normal sounds.

Original Minecraft sounds accompany item collection, axe hits, the final tree/block break, block placement, chest opening and closing, and wooden door opening and closing. Each event uses its original sample variants with small pitch variation. A blocked placement or door toggle does not play a success sound; closing a chest plays only after the menu actually closes.

Effects mix into Sunshine's existing audio stream alongside music and native effects. `audio = off` / `SMS_AUDIO=0` mutes them with the game. The samples are embedded, so standalone builds do not need additional files or a network connection.

## Bright plaza flag

The independent `clean_shine_gate = on` setting maps to `SMS_CLEAN_SHINE_GATE=1`. It initializes the main Shine Gate monument as clean and removes the plaza darkness overlay, making the plaza bright even with few collected Shines. It does not change the collected gate Shine flag or the collected Shine count. This checkout enables it by default. Use `SMS_CLEAN_SHINE_GATE=0 ./run.sh` or set `clean_shine_gate = off` to restore normal progression lighting. It works with Minecraft disabled.

## Polish verification

`tools/minecraft/check-polish.sh IMAGE [32|64] [OUTPUT_DIRECTORY]` checks live inventory opening, camera-selected native palm mining and release cancellation, four real log pickups, mouse crafting of planks/table, achievement triggers, placement and right-click table interaction, chest GUI/lid, continuous placed-chest mining and complete spill/collection. The debugger fixture positions the native camera/player and supplies a filled chest; all selection, mining, crafting, placement and interaction run through game code and live SDL input. Use a fresh output directory for an initial boot.

`polish_test.cpp` checks mining speed/tool changes, atomic chest spills at the drop capacity limit, partial/full inventory conservation, drop and achievement persistence, V1/V2 migration and long camera-ray intersections. Compile with `g++ -std=c++11 -Isrc tools/minecraft/polish_test.cpp platform/minecraft/storage.cpp -o build/minecraft-polish-test`, then run it with an isolated save filename. Add `-m32` for the other architecture. `record_polish.py --disc IMAGE --output DIRECTORY` records the live scenario with native audio.

## Implementation and checks

`src/minecraft/` owns Steve, GX rendering, inventory, recipes, placement, collision and tree/drop logic. `platform/minecraft/storage.cpp` writes the versioned sidecar. `assets/minecraft/` retains unchanged sources, attribution and a pinned URL/SHA-256 manifest. `python3 tools/minecraft/import_assets.py` verifies all sources and regenerates the embedded nearest-filtered GX atlas. Normal builds and standalone launches require no network or image decoder. FLUDD's hotbar indicator is crossover UI. Its water gauge uses the original 182×5-pixel Minecraft XP-bar strips, recolored to dark blue and cyan-blue while preserving their segmented pixels, shading and transparency. It spans the hotbar at 2× scale and clips the fill to FLUDD's actual water percentage, including empty and full tanks. The actual water percentage is right-aligned above the bar on the same line as FLUDD.

`decomp-patches/port-03-minecraft-crossover.patch` integrates player, console, ground queries and plaza palms. `port-05-nozzle-equipment.patch` reserves native attachment pickups for inventory and preserves spray/attachment switching while chest armor selects the secondary nozzle. `port-04-clean-plaza.patch` integrates the independent monument and lighting flag. Original decomp files remain untouched. Felling removes tree collision while Minecraft is enabled; native ground queries additionally recognize placed block tops.

```sh
g++ -std=c++03 -Wall -Wextra tools/minecraft/core_test.cpp -o build/minecraft-core-test
build/minecraft-core-test
g++ -std=c++11 -Wall -Wextra -Isrc tools/minecraft/building_test.cpp platform/minecraft/storage.cpp -o build/minecraft-building-test
build/minecraft-building-test build/minecraft/building-unit.dat
```

The unit checks cover SDL input layouts, continuous mining duration and cancellation, tool restrictions, sword reach and facing, stacking, click transfers, recipes, placement, door bounds, full-inventory behavior, disk saves and corrupt-primary recovery. The older tree/building fixtures assume player-facing targeting; use `check-polish.sh` for camera targeting. Run `tools/minecraft/check.sh IMAGE [32|64] [OUTPUT_DIRECTORY]` for live tree chopping, pickups, hotbar/inventory input and actual FLUDD water consumption. `check-fludd.sh` checks attachment while turning, running, jumping and spraying, and captures the angled sword/axe grips.

`tools/minecraft/record_mining.py --disc IMAGE --arch 64 --fps 60 --output OUTPUT_DIRECTORY` records a native video with audio and asserts mouse-hold progress, release reset, cancellation when switching to the sword, the last crack stage, felling and four collected logs. Use `--arch 32 --fps 30` to check the other build and frame rate. The recorder needs `imageio_ffmpeg` on its Python path.

`tools/minecraft/check-building.sh IMAGE [32|64] [OUTPUT_DIRECTORY]` clicks the actual GUI to craft planks, a table, doors and a chest, places them, splits and stores items, and checks jumping on stacked blocks and open/closed door collision. A fresh process must restore the world and chest contents. It also checks the clean monument, bright plaza overlay and unchanged gate Shine collection flag. Test fixtures supply initial logs and player positions; crafting, transfers and interactions use live SDL events.

`tools/minecraft/check-sword.sh IMAGE [32|64] [OUTPUT_DIRECTORY]` drives real sword input against native Goopy Stu and Strollin Stu actors in Bianco Hills. It checks model-facing detection at two rotations with the camera looking backward, native stomp/death handling, completed death animation and the behind-player, out-of-reach, axe and open-inventory restrictions. The fixture activates an initialized native Goopy Stu pool entry and sets positions; the game handles attacks and death.

`check-ground-preview.sh IMAGE [32|64] [OUTPUT_DIRECTORY]` checks a level camera's lower ground target while standing, real controller walking, right-click placement and detection of the resulting block. `check-achievement-edges.sh IMAGE [32|64] [edges|center] [OUTPUT_DIRECTORY]` checks reminder and unlock anchoring without shifting subsequent menu/HUD draws; `SMS_WIDESCREEN=off` also checks 4:3 behavior.

`check-sword-click.sh IMAGE [32|64] [quick|moving|natural] [OUTPUT_DIRECTORY]` delivers actual mouse press/release before one game update. It checks retained quick clicks, hit feedback and native enemy death. `moving` holds the enemy outside reach until after release, then lets it enter the active swing. `natural` calls native enemy-pool deactivation and Goopy Stu reset, preserving all resulting lifecycle/collision flags, including the historical killed flag that previously blocked sword hits.

`tools/minecraft/check-build-mode.sh IMAGE [32|64] [OUTPUT_DIRECTORY]` verifies real B press/hold/release toggle behavior, pointer camera and model rotation, the off-centre placement ray, inventory pause, right-click placement and returning to normal controls.

`tools/minecraft/check-plaza.sh IMAGE [32|64] [OUTPUT_DIRECTORY]` checks the lighting flag both off and on with Minecraft disabled, capturing the difference and checking that the collected gate Shine remains unchanged.

`tools/minecraft/import_audio.py` verifies the pinned original Ogg files and regenerates `src/minecraft/audio_assets.h` using libsndfile and libsamplerate. `audio_test.cpp` checks every sound, finite playback, stereo mixing, saturation with overlapping voices, mute, unchanged inactive PCM and alternate output rates:

```sh
g++ -std=c++11 -Wall -Wextra -Isrc tools/minecraft/audio_test.cpp platform/minecraft/audio.cpp -pthread -o build/minecraft-audio-test
build/minecraft-audio-test build/minecraft/sound-effects.wav
```

Set `SMS_AUDIO=1 SMS_MINECRAFT_AUDIO_TRACE=1 SMS_AUDIO_WAV=FILE.wav` when invoking `check.sh` or `check-building.sh` to record the live action tests and log the selected original sample variants. The building test writes a separate `FILE-reload.wav` for its cold restart. The AI output uses the same mixed PCM for recordings and SDL playback.

`tools/minecraft/check-inventory-hint.sh IMAGE [32|64] [OUTPUT_DIRECTORY]` checks the persistent original inventory hint, Tab opening inventory while a real crafting table is targeted, held-key/release behavior, toast replacement and expiry, the physical E key spraying native FLUDD without opening inventory, and suppression after a saved restart. Use a fresh output directory. `record_inventory_hint.py --disc IMAGE --output DIRECTORY` records that same live flow with audio.

`tools/minecraft/check-starter.sh IMAGE [32|64] [OUTPUT_DIRECTORY]` checks fresh starting nozzles, live mouse stick crafting, both directions of inventory mouse tracking, one original hurt sample per native health loss, suppressed goo sounds and damage voice, and unchanged base-game health-loss audio. Use a fresh output directory. `starter_test.cpp` covers shifted recipes in both grids, invalid shapes, full output protection and stick/chest save roundtrips.

`check-nozzle-toggle.sh IMAGE [32|64] [OUTPUT_DIRECTORY]` checks physical V switching all three equipped attachments to Spray and back, the native transition, water use, independent P preview toggles, both inventory mouse axes and switching after armor removal.

To start a fresh crossover world, close the game and remove or rename both `~/.local/share/sms-port/card-a/minecraft-world.dat` and `minecraft-world.dat.bak` (or their equivalents in `SMS_SAVE_DIR` / `SMS_MINECRAFT_SAVE`). This resets inventory, chest contents, placed blocks, world drops and achievements together; native Sunshine card saves are separate. Removing only the primary allows the backup to restore the old world.
