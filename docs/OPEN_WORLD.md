# Delfino coastal connections

An optional addition to the native PC port connects **Delfino Plaza and Ricco Harbor** by a broad coastal promenade, and **Plaza and Pinna Park’s beach** by an open-water Blooper ferry.
The walking route has a tiled Plaza bridge, timber Harbor pier, and vaulted coastal curve.
The ferry approaches the actual Pinna coastline without a tunnel.

## Build this branch and include the mod

Check out `mod-open-world` and install the platform prerequisites from [BUILD.md](../BUILD.md).
On Linux, build the tested 64-bit version from the repository root:

```sh
git switch mod-open-world
git submodule update --init decomp
SMS_ARCH=64 JOBS=8 ./build.sh "/path/to/your/GMSE01.iso"
./run-open-world.sh
```

Use your North American Rev 0 disc image (GMSE01); `.iso`, `.gcm`, and Dolphin `.ciso` are accepted.
Passing the image to `build.sh` builds both `build/linux-64/sms` and `build/linux-64/sms-standalone`.
The wrapper uses the standalone executable when no image argument is supplied.
To use the ordinary executable instead, pass the image to `run-open-world.sh`.

The mod is compiled into this branch automatically: CMake includes `src/open_world.cpp` and `src/open_world_sea.cpp`,
and applies the `decomp-patches/zz-open-world-*.patch` hooks to generated copies of the pinned game sources.
The `decomp/` submodule stays unchanged.
Its pinned commit is published on the `mod-open-world` branch of `chasem-dev/sms-english`.
If an existing checkout reports `not our ref`, update this port branch and synchronize the submodule before rebuilding:

```sh
git pull --ff-only origin mod-open-world
git submodule sync --recursive
git submodule update --init --recursive
```

Enable the compiled feature at runtime with the wrapper, `SMS_OPEN_WORLD=1`, or `open_world = on` in `settings.txt`.
There is no separate mod archive to download or directory to install under `mods/`; `SMS_MOD` is not required for this feature.
Models and textures are read from your disc at runtime.

The tested 32-bit Linux alternative requires the multilib prerequisites in [BUILD.md](../BUILD.md#linux):

```sh
SMS_ARCH=32 JOBS=8 ./build.sh "/path/to/your/GMSE01.iso"
SMS_ARCH=32 ./run-open-world.sh
```

When rebuilding with CMake directly, rebuild `sms_standalone` too if you launch without an image argument.
The macOS and Windows build procedures are documented in [BUILD.md](../BUILD.md); this mod has been gameplay-tested on Linux in both word sizes.

## Enable it

From this checkout, run:

```sh
./run-open-world.sh
```

The wrapper enables the connection and defaults to the 64-bit build. It accepts the same disc-image argument as `run.sh`:

```sh
./run-open-world.sh "/path/to/Super Mario Sunshine.iso"
```

Alternatively, add `open_world = on` to `settings.txt`, or run `SMS_OPEN_WORLD=1 ./run.sh` with a rebuilt executable. The default is off; the wrapper only enables it for that launch. Run `./run.sh` normally to return to the default, or remove the setting if you enabled it there.

The installed SMS Launcher's existing release is separate from this development checkout; launch this checkout to try this addition.

## Find the route

- **Delfino Plaza:** follow the western waterfront wooden pier beside the fruit market. The extension now leaves its seaward side toward the northwest, rather than continuing south. It has wave-pattern paving, a white balustrade with blue coping, lanterns, and a **RICCO HARBOR** sign.
- **Ricco Harbor:** find the timber extension on the clear quay beside the crate stacks, approaching the harbor from the southeast. Its sign reads **DELFINO PLAZA**.

Follow the promenade into the short covered curve. Keep walking after the crossing to reach the destination's waterfront. Normal stage entries keep their original spawn positions.

## Waterfront materials and construction

The bridges use textures from the player's loaded stage assets, including Plaza's mosaic paving and plaster, Harbor's timber atlas, and native masonry. Plaza has a dressed-stone entrance arch, shaped balusters, blue coping, lanterns, and visible supports under the deck. Harbor has individual deck planks, timber pilings, metal collars, and sagging rope rails. The shared interior has tiled paving, masonry skirting, a plaster barrel vault, and stone ribs.

Shared passage textures are copied into bounded host storage and retained between maps, so changing stage does not swap the material underneath Mario. A session starting directly in Harbor uses its corresponding native tile/stone/plaster set for the shared passage. Exterior materials follow the current stage. Texture pixels remain sourced from the user's disc at runtime; extracted assets are not included in the source or distributed separately. Decorative railings and supports do not add collision; deck, curbs, passage walls, and the curved ceiling have native collision.

## Geographic layout (v4)

The Plaza entrance is moved from the southern tip of the western pier to its seaward side at native coordinates `(-7900, 300, 1000)`, heading northwest. The Harbor connection starts at `(-300, 300, 1900)` and heads south-southeast toward Plaza. These bearings follow the island guide’s relative Plaza–Harbor placement; the original stages still have independent coordinate systems, not a surveyed common world map.

Each approach has one broad, progressive curve instead of four right-angle turns. The promenade is shorter, and the covered section follows that curve into a shared straight. The complete shore-to-shore centerline is approximately 8,100 native units, 51% shorter than v3’s 16,600 units. The same stone, plaster, mosaic, timber, lamps, and railings remain. Shared texture faces are split at their coordinate folds to prevent stretched paving along the diagonal passage.

The transfer now rotates position, velocity, facing, and camera between each entrance’s own heading. The trigger is measured across the diagonal passage, not against a world axis. Geometry and camera remain matched at the exchange.

## Continuous passage and arrival camera

The former dead-end transfer boxes have been replaced with matching, traversable passage geometry. The stage exchange happens in the middle, where the covered curve conceals both outside worlds. Position, movement velocity, facing, animation phase, and camera pose are mapped into the matching location on the other side. Mario keeps moving instead of stopping at an entrance spawn.

Inside the passage, a short guided camera follows its centre line and stays below the ceiling. This prevents the arrival view from getting stuck outside the passage shell. The ordinary waterfront camera blends back in along the approach. Lighting also blends on the approach, eliminating the Plaza-to-Harbor exposure change at the boundary.

Crossings skip the black wipe, entrance cinematic, and HUD entry animations. The renderer retains the last complete frame until the destination passage is ready to display. **There is still a brief pause during native stage construction.** This is visual continuity across separate maps, not simultaneous simulation or a fully streamed world. The neighboring stage archive is read in the background while you explore; the three host-memory cache slots are bounded to 16 MiB each. Stage construction remains on the game thread.

The return trigger arms after walking away from the central boundary, so arriving or standing still cannot bounce between maps.

## Scope

The walking connection has a bridge/pier at each waterfront; the additional Pinna ferry is described below. These do not form a completed island-wide coastal landscape. The original maps remain separate.

The link remembers the last visited Plaza and Harbor episodes for this game session; the first Harbor visit uses episode 1 (internal episode 0). If the session starts directly in Harbor, its first return uses the dry Plaza (internal episode 2) until a Plaza visit has been remembered. Crossings carry health, FLUDD water, and ordinary Mario nozzle selection. The route is intended for Mario on foot: Yoshi and carried actors do not transfer. Shines, progression, saving, and other exits remain governed by the original game. This prototype does not add persistent free-roam Shine collection or connect the remaining worlds beyond Harbor and Pinna.

Testing uses the standard North American game in the dry Plaza and Ricco Harbor. Flooded Plaza and replacement stage geometry, including Eclipse, have not been validated with this route. The addition uses generated geometry and requires no modified disc assets or separate download.

## Implementation and verification

`src/open_world.cpp` generates rendered geometry and native collision from the same faces, handles route state, and transfers Mario and the camera. The `zz-open-world-*` patches connect it to stage setup, drawing, camera updates, lighting, transitions, and collision allocation. The audio patch fixes a stale sequence-owner handle exposed by repeated stage changes. `platform/dvd/dvd.cpp` supplies background archive caching using host memory, without running game allocation or stage construction on the worker thread. The GX renderer retains the displayed framebuffer during a crossing, with a two-second failsafe. `open_world` is exposed through the existing settings loader.

Build normally with `build.sh`; rebuild the standalone executable too if you launch without a disc-image argument.

The controller replay steers ordinary Mario movement around every bend and checks both land joins, alternating destinations, episode memory, speed, health/water carry, and archive-cache use:

```sh
python3 tools/open_world/smoke.py \
  --iso "/path/to/Super Mario Sunshine.iso" \
  --seed-card "$HOME/.local/share/sms-port/card-a" \
  --stress --fps 60 --audio --spray
```

Use `--exe build/linux-32/sms` for the 32-bit executable, or `--record` to capture the complete first round trip at 15 fps. The test copies the seed memory card into its evidence directory and never writes to the player's card. Test-only `SMS_OPEN_WORLD_TEST_SPAWN` places Mario near the route; `SMS_OPEN_WORLD_TEST_WALK` supplies controller axes to follow the bends. Normal launches set neither. `SMS_OPEN_WORLD_LOG=1` adds position diagnostics.

A second replay captures consecutive displayed frames through the boundary, including retained frames, and checks for black frames, speed discontinuity, and a lighting/camera jump:

```sh
python3 tools/open_world/transition.py \
  --iso "/path/to/Super Mario Sunshine.iso" \
  --seed-card "$HOME/.local/share/sms-port/card-a"
```

Use `--from-stage 3` for the reverse direction, `--fps 60` for that frame rate, and `--jump` to exercise an airborne crossing. Run GPU replays one at a time for useful timing measurements. Evidence and result summaries are stored under `build/open-world/`.

The v4 evidence in `build/open-world/v4/` includes a recorded 64-bit round trip at 30 fps, a 32-bit round trip at 60 fps with audio and spent water, and consecutive-frame walking/airborne handoff checks in both directions. Both standalone bundles contain the rebuilt executables. Geometry and material setup remain gated behind `open_world`.

The actual gameplay recording is `concepts/delfino-coastal-curve-v4.mp4`; the matching PNG shows the relocated Plaza approach, Harbor landing, and shared curved passage. Copies are available in FileBrowser’s `Render-Previews` folder. The recording includes held frames during native map construction.

## Pinna Park Blooper ferry (v5)

The western Plaza waterfront now has a second connection: an open-water Blooper ride to Pinna Park's beach.
Find the floating pink Blooper beside the clear northern side of the cannon pier, beyond the fruit market, and press **GameCube X** when the boarding prompt appears.
At Pinna, the return Blooper waits offshore by the beach's eastern end; its prompt is reachable from the sand.
The ferry lands on the beach, where the original park entrance remains available.
The beach-to-park gate retains its normal stage transition.

The control stick steers within a wide water lane, **A** hops, and **B** turns the ferry around, including after the offshore map exchange.
The ride follows one smooth coastal curve toward the distant island, without a tunnel or added bridge.
It uses the game's animated Blooper model and Mario's surfing animation, loaded from the player's disc.
Movement is guided along the route; this is a ferry connection rather than unrestricted ocean surfing.
Yoshi, held objects, and actors carrying Mario cannot board.

The full Pinna coastline and static park scenery appear from the Plaza side before the exchange.
A shared geographic transform registers Pinna's native beach with that offshore scenery, while an actual Plaza model provides the return view.
Native distant island cards and far-water layers are replaced during the ride because their geometry was designed for cameras confined to the original maps.
The common ocean uses a native wave texture, world-space coordinates, and an animation phase that continues across the exchange.
A closer following camera carries its position and target across maps, then eases back into ordinary camera control on landing.
Short Blooper leaps join the boarding spots and the raised Plaza pier, avoiding an instant reposition at those joins.

The offshore exchange preserves route progress, steering offset, hop phase, facing, velocity, camera, health, FLUDD water/nozzles, and Mario/Blooper animation phase.
A brief blend affects sky and distant scenery while Mario and the near ocean remain live.
The entry wipe, title banner, and entrance camera are skipped.
**Native stage construction still pauses the ride; the first Pinna load can take several seconds.**
The complete source frame stays visible until the first destination frame is ready, with an eight-second failsafe.
This is continuity across separate native maps, not simultaneous stage simulation.

Archive prefetch now has three bounded 16 MiB host slots for Plaza, Harbor, and Pinna.
The ferry's immutable raw model/animation/texture resources share a separate bounded 16 MiB host cache, copied into the current stage heap before native loading.
No extracted game assets are included as deliverables.
The source is `src/open_world_sea.cpp`, with small player/map/water patches that take effect only while the optional connection is enabled.

Replay the native controller input, steering and a hop through the exchange with a private memory-card copy:

```sh
python3 tools/open_world/sea.py \
  --iso "/path/to/Super Mario Sunshine.iso" \
  --seed-card "$HOME/.local/share/sms-port/card-a" \
  --controls --record --out build/open-world/pinna-check
```

Use `--fps 60 --exe build/linux-32/sms --audio` for the other build/frame rate.
Use `--from-stage 5` to begin on Pinna's beach, or `--rides 1 --turn-back` to test a mid-ocean return to the departure pier.
The test-only `SMS_SEA_TEST_SPAWN`, `SMS_SEA_TEST_RIDES`, `SMS_SEA_TEST_CONTROL`, and `SMS_SEA_TEST_TURNBACK` flags are absent from normal launches.
The replay checks both landings, stage/episode selection, speed and state carry, steering/hopping, retained frames, and absence of black frames or FIFO errors.

To verify the complete Plaza-to-park journey, including ordinary walking after landing and the native gate's collision trigger:

```sh
python3 tools/open_world/sea.py \
  --iso "/path/to/Super Mario Sunshine.iso" \
  --seed-card "$HOME/.local/share/sms-port/card-a" \
  --rides 1 --controls --enter-park --record --out build/open-world/park-entry-check
```

This test supplies controller axes along the beach and up the west staircase, then verifies player movement inside Pinna Park (internal stage 13).
It does not reposition Mario or request a stage transition after the ferry landing.
The additional `SMS_SEA_TEST_ENTER_PARK` flag is only used by this replay.
The no-black-frame invariant covers the offshore ferry exchange; the original beach-to-park gate keeps its native fade.
The recorded 64-bit evidence in `build/open-world/v5/park-entry64-full` reaches the park entrance in episode 0,
then proves controller movement of 125.8 native units inside the park, with all eight health points.
The 32-bit replay in `build/open-world/v5/park-entry32-full` verifies the same journey at 60 fps with audio,
including 125.9 native units of movement inside the park and all eight health points.
The first displayed offshore handoff changes the image by 3.41 RGB levels out of 255 and brightness by 0.57 levels, with Mario visible.

Final v5 evidence is in `build/open-world/v5/release64-final` (64-bit, 30 fps, recorded round trip),
`release32-60-audio` (32-bit, 60 fps, audio, starting on Pinna), `turnback` (controller reversal),
and `coastal-regression` (60 fps, audio, spent water, both Harbor walking joins).
Both native builds and their standalone bundles were rebuilt and the bundle executable prefixes verified.
The first displayed ferry handoffs changed the captured image by 3.41 and 3.13 RGB levels out of 255,
with brightness changes below one level; Mario remained visible in both.
Native trigger-to-arrival times were 3.77 s / 0.23 s for the recorded round trip,
and 0.17 s / 5.94 s for the reverse-start 32-bit test.

The actual, uninterrupted captured gameplay is `concepts/pinna-blooper-ferry-v5.mp4`;
the matching PNG shows the approach, beach landing, and Plaza return.
Both are copied into FileBrowser's `Render-Previews` folder with the same filenames.
The recording retains the visible pause during stage construction.
