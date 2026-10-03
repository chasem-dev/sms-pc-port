# Delfino coastal connections

Walk between **Delfino Plaza and Ricco Harbor**, or ride a **Blooper across the water to Pinna Beach** and continue into the amusement park.
These optional routes are included in the `mod-open-world` branch.

This is a playable prototype. The Harbor now sits along the Plaza coast, with a curved, graded walking connection. The Blooper leaves from the bell-tower waterfront and approaches the same Pinna island you can see from shore. Small scenery changes and a brief loading hold can still be visible.

## Start playing

After building this branch, run from its folder:

```sh
./run-open-world.sh
```

If you built without embedding your disc image, supply it when launching:

```sh
./run-open-world.sh "/path/to/your/GMSE01.iso"
```

This launches the 64-bit Linux build with the routes enabled. The existing SMS Launcher release is a separate installation; launch this checkout to try these changes.
There is no additional mod archive to install and nothing to copy into `mods/`.

## Find the routes

| Journey | Where to go | What to do |
| --- | --- | --- |
| Plaza → Ricco Harbor | The waterfront beside the red-roofed bell tower on the west side of the Plaza. Follow the shore uphill to the **RICCO HARBOR** sign. | Follow the covered curve and keep walking until you reach the Harbor quay. |
| Harbor → Plaza | The raised quay at the east end of the Harbor buildings, marked **DELFINO PLAZA**. | Follow the passage back to the Plaza waterfront. |
| Plaza → Pinna Beach | The promenade around the red-roofed bell tower. Look over the seawall for the waiting Blooper. | Approach until **X: Board** appears, then press **X**. |
| Pinna Beach → Plaza | The waiting Blooper at the beach landing. | Approach and press **X** when prompted. |

The Blooper lands on **Pinna Beach**. Follow the beach left toward the stairs and the amusement park entrance. Entering the park uses the game's normal gate transition.

## Riding the Blooper

| Control | Action |
| --- | --- |
| **X**, beside the waiting Blooper | Board |
| **Control stick**, during the ride | Steer within the route |
| **A** | Hop |
| **B** | Turn back toward the other shore |

The Blooper follows a guided coastal route and lands automatically. You can turn back before or after the offshore exchange.
Board as Mario on foot, without Yoshi or a carried object.
The walking connection is also intended for Mario on foot; carried actors and Yoshi do not transfer between maps.

## What to expect

- Health, FLUDD water, and ordinary nozzle selection carry across the added connections.
- The routes remember the last visited Plaza, Harbor, and beach episodes for the current session. They do not add free-order Shine collection or change the game's progression and saving rules.
- The ferry avoids a tunnel. A short pause can still occur while the next area loads; timing depends on your machine and settings.
- Palms and the Plaza's Shine monument remain visible in distant previews, using the destination episode's saved positions. Moving boats, park rides, and other actors can still appear at the exchange. The route is not yet visually seamless everywhere.
- All three moving Plaza boats cleared the connected Harbor scenery and walkway during full circuits in the dry-Plaza episode.
- Flooded Plaza, replacement level geometry such as Eclipse, and other disc regions have not been validated with these routes.

The [player experience review](OPEN_WORLD_PLAYER_REVIEW.md) tracks the remaining visual and usability work. Detailed replay results and implementation history are in the [development notes](OPEN_WORLD_DEVELOPMENT.md).

## Build this branch and include the mod

Use your **North American Rev 0** Sunshine disc image (**GMSE01**). `.iso`, `.gcm`, and Dolphin `.ciso` are accepted.
Install the prerequisites in [BUILD.md](../BUILD.md), then run:

```sh
git switch mod-open-world
git submodule sync --recursive
git submodule update --init --recursive
SMS_ARCH=64 JOBS=8 ./build.sh "/path/to/your/GMSE01.iso"
./run-open-world.sh
```

The mod compiles with the game. Supplying the disc image to `build.sh` also creates the standalone executable used by the launch command.
If you rebuild through CMake instead, rebuild `sms_standalone` as well before launching without a disc-image argument.

The pinned `decomp` dependency is published on GitHub; it does not require a copy from the original developer's machine.
If an older checkout reports **not our ref**, update the port branch and synchronize again:

```sh
git pull --ff-only origin mod-open-world
git submodule sync --recursive
git submodule update --init --recursive
```

For a 32-bit Linux build, install the multilib prerequisites from [BUILD.md](../BUILD.md#linux), then use `SMS_ARCH=32` for both `build.sh` and `run-open-world.sh`.
Windows and macOS build instructions are in [BUILD.md](../BUILD.md); this feature has been gameplay-tested on Linux in both word sizes.

## Turn the routes off

Launch with `SMS_OPEN_WORLD=0 ./run.sh` to disable them for that launch.
If you added `open_world = on` to `settings.txt`, change it to `off` to disable them for ordinary launches. `run-open-world.sh` always enables them for its launch.
