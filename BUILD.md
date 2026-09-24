# Building and running the native port

## One-time setup

These are already installed on this machine; you only need them on a new one.

```sh
sudo apt install cmake make gcc-multilib g++-multilib
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install libsdl2-2.0-0:i386 libgl1:i386 libegl1:i386 libgl1-mesa-dri:i386 libegl-mesa0:i386
```

The port is a 32-bit program (the game code assumes 4-byte pointers), so it needs the 32-bit (`:i386`) runtime libraries.

## Get the game source

The decomp is a git submodule in `decomp/`.
After pulling new decomp commits, update it:

```sh
cd ~/sms-port
git submodule update --init --remote decomp
```

## Build

```sh
cd ~/sms-port
cmake -B build
nice -n 19 make -C build -j4 sms
```

The first build compiles about 600 game files and takes a while; later builds only rebuild what changed.
The result is `build/sms`.
If the build acts strangely after big changes, delete the `build` folder and run both commands again.

## Run

```sh
cd ~/sms-port
build/sms "/home/netflix/sms/Super Mario Sunshine (2002)(Nintendo)(US).iso"
```

The game is read straight from your ISO; nothing is extracted or copied.
Saves go to `~/.local/share/sms-port/card-a`.

Useful options (put them before the command, e.g. `SMS_SKIP_MOVIES=1 build/sms ...`):

| Option | Effect |
| --- | --- |
| `SMS_SKIP_MOVIES=1` | skip the intro and opening movies |
| `SMS_AUDIO=0` | no sound |
| `--headless` (after `build/sms`) | no window, for testing |

## Keys

Edit `bindings.txt` to change them.

| GameCube | Keys |
| --- | --- |
| Control stick | arrow keys or WASD (hold Left Ctrl for half tilt) |
| C-stick | I / J / K / L |
| A | Space or X |
| B | Shift or C |
| X / Y | V / F |
| Z | Z |
| L / R | Q / E |
| Start | Enter |
| D-pad | 1 2 3 4 |
| Debug overlay (FPS, stats, keys) | ` (backtick) |
| Game speed x1 / x2 / x4 / x10 (overlay open) | F7 |
| Quit | Esc |

A USB or Bluetooth game controller also works.

On the file-select screen, walk Mario left under a block for about half a second and press A to jump into it.

More detail (every option, the platform layer, the patches) is in `README.md`.
