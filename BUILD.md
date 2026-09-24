# Building and running the native port

| System | Build | Run |
| --- | --- | --- |
| [Linux](#linux) | `./build_linux.sh` | `./run_linux.sh /path/to/GMSE01.iso` |
| [macOS](#macos) | `./build_mac.sh` | `./run_mac.sh /path/to/GMSE01.iso` |
| [Windows (MSYS2 MINGW32)](#windows-msys2-mingw32) | `./build_windows.sh` or `build_windows.cmd` | `./run_windows.sh /path/to/GMSE01.iso` or `run_windows.cmd` |
| [Standalone executable](#standalone-executable) | `./build_linux.sh /path/to/GMSE01.iso` (or `build_mac.sh` / `build_windows.sh`) | `./run_linux.sh` / `./run_mac.sh`, or `build/sms-standalone` from anywhere |

## Windows (MSYS2 MINGW32)

Install [MSYS2](https://www.msys2.org/) and open **MSYS2 MINGW32** from the Start menu.
This port must be compiled as a 32-bit program because game code stores pointers in 32-bit fields.
Install the compiler, SDL2, build tools, and the patch utility:

```sh
pacman -S --needed mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-SDL2 mingw-w64-i686-ninja mingw-w64-i686-make mingw-w64-i686-python patch git
```

In the MINGW32 shell, enter this repository (for example,
`cd /c/path/to/sms-pc-port`) and build:

```sh
./build_windows.sh
```

The build creates `build32/bin/sms.exe`.
To play, pass the path to your North American Rev 0 image:

```sh
./run_windows.sh '../sms-english/Super Mario Sunshine (2002)(Nintendo)(US).iso'
```

Or place exactly one `.iso`, `.gcm`, or Dolphin `.ciso` image in `build32/bin/rom/` and run `./run_windows.sh` without an argument.
Use single quotes around paths with spaces or parentheses.
The executable contains game code, while the image supplies models, textures, levels, audio, and other game files at runtime.
`build32/bin/rom/` is only a convenient image search folder for `run_windows.sh`; to put the assets inside the executable, see [Standalone executable](#standalone-executable).
The port reads the image in place; it does not copy or extract it.
Keep the MINGW32 shell open when running so its SDL2 and compiler runtime DLLs are on `PATH`.
Saves default to `%APPDATA%/sms-port/card-a`, or set `SMS_SAVE_DIR`.
The Windows build uses the SDL2 window; the EGL headless mode is not available in this setup.
See [Keys](#keys) below for keyboard and controller input.

### From PowerShell or Command Prompt

The `.sh` files are Bash scripts, so do not open them through Windows file associations or Git for Windows.
If MSYS2 is installed at `C:\msys64`, open PowerShell in this repository and run:

```powershell
.\build_windows.cmd
.\run_windows.cmd '..\sms-english\Super Mario Sunshine (2002)(Nintendo)(US).iso'
```

If you placed one image in `build32\bin\rom\`, run `.\run_windows.cmd` without an argument.
The `.cmd` launchers start MSYS2's MINGW32 Bash and put its 32-bit DLLs on `PATH` for you.
If MSYS2 is installed elsewhere, set `MSYS2_ROOT` to its installation folder first.

### Decompilation build on Windows

The decompilation is a separate GameCube build and produces `mario.dol`, which runs in Dolphin or on GameCube hardware.
Use **PowerShell** with native Windows Python and Ninja; see the decomp README for installation.
In `sms-english` (or this repository's `decomp/` submodule), place your GMSE01 Rev 0 image in `orig/GMSE01/`, then run:

```powershell
python configure.py --version GMSE01
ninja
```

If Ninja is installed through MSYS2 but is not on PowerShell's `PATH`, run
`C:\msys64\mingw32\bin\ninja.exe` in place of `ninja`.
The output is `build/GMSE01/mario.dol`; it should match the original disc's DOL byte for byte.
To play the GameCube version on Windows, open your original disc image in Dolphin.
The image supplies the game files that a standalone DOL does not contain.
The decomp downloads its own GameCube toolchain; the MINGW32 GCC compiler is only for the PC port.
The `sms-english` [README](https://github.com/chasem-dev/sms-english/blob/main/README.md) has the native Windows setup details.

## macOS

macOS builds an **x86_64** binary (runs under **Rosetta 2** on Apple Silicon).
Native arm64 cannot `mmap` below 4 GiB (PAGEZERO), and the port needs stacks below 2 GiB for pointer-in-`u32` slots.
Output goes to `build-mac/`.

No Intel Homebrew is required. The script uses Apple Clang (`-arch x86_64`), normal Homebrew tools, and a universal `SDL2.framework`.

### Requirements (explicit)

| Requirement | Why |
| --- | --- |
| Xcode Command Line Tools | Apple Clang (`clang` / `clang++`), `make`, `patch`, system headers |
| Rosetta 2 (Apple Silicon) | Run the x86_64 `build-mac/sms` binary |
| Homebrew (`/opt/homebrew` on Apple Silicon is fine) | `cmake`, `python3`, `llvm` |
| `llvm` (`brew install llvm`) | `llvm-objcopy` to rename the game's `operator new/delete` in Mach-O archives |
| Universal `SDL2.framework` | Window/audio; `./build_mac.sh` downloads it into `third_party/` if missing (Homebrew’s `sdl2` bottle is often arm64-only) |

Apple Clang has no `-fexec-charset=CP932`; configure mirrors game sources as CP932 (`tools/darwin_cp932_mirror.py`) so disc Shift-JIS names still match.

### One-time setup

```sh
xcode-select --install          # if needed
softwareupdate --install-rosetta
brew install cmake python3 llvm
```

### Get the game source

```sh
git submodule update --init decomp
```

### Build and run

```sh
./build_mac.sh
./run_mac.sh "/path/to/Super Mario Sunshine (US).iso"
```

Or place exactly one `.iso`, `.gcm`, or Dolphin `.ciso` in `build-mac/rom/` and run `./run_mac.sh` with no argument.
Pass the image to the build script for `build-mac/sms-standalone`.
Set `JOBS=2` to limit parallel jobs (default is `hw.ncpu`).
Saves default to `~/.local/share/sms-port/card-a`, or set `SMS_SAVE_DIR`.

The linker uses `-Wl,-pagezero_size,0x1000` so low-memory `mmap` / `MAP_32BIT` works on x86_64.

### Known gaps

| Gap | Why it matters |
| --- | --- |
| Native arm64 | Not supported; PAGEZERO blocks low `mmap` |
| Lockstep `platform/trace` | Linux ELF-only; macOS links empty stubs (same as Windows) |
| EGL headless | Not wired on macOS; windowed SDL2/GL is the path |

### Manual build (same idea as the script)

```sh
# SDL2.framework already in third_party/ (or pass -DSMS_SDL2_FRAMEWORK=...)
cmake -S . -B build-mac \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_C_COMPILER="$(command -v clang)" \
  -DCMAKE_CXX_COMPILER="$(command -v clang++)" \
  -DSMS_SDL2_FRAMEWORK="$PWD/third_party/SDL2.framework" \
  -DSMS_ARCH=64 -DSMS_GX_BUILD_TESTS=OFF
cmake --build build-mac --target sms -j"$(sysctl -n hw.ncpu)"
cp -R third_party/SDL2.framework build-mac/   # found via @rpath (@executable_path)
```

## Linux

### One-time setup

These commands are for Ubuntu or Debian and are already satisfied on this machine.

```sh
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install git cmake make python3 patch binutils gcc-multilib g++-multilib libsdl2-dev libegl-dev libgl-dev
sudo apt install libsdl2-2.0-0:i386 libgl1:i386 libegl1:i386 libgl1-mesa-dri:i386 libegl-mesa0:i386
```

The port is a 32-bit program (the game code assumes 4-byte pointers), so it needs the 32-bit (`:i386`) runtime libraries.

## Get the game source

Run the commands below from the repository root.
The decomp is a git submodule in `decomp/`.
Check out the version pinned by this port commit:

```sh
git submodule update --init decomp
```

## Build

```sh
./build_linux.sh
```

The script configures a 32-bit build in `build/`, updates the pinned decomp submodule, and compiles `sms`.
Set `JOBS=2` to limit parallel compiler jobs.
The first build compiles about 600 game files and takes a while; later builds only rebuild what changed.
The result is `build/sms`.
For a manual build, run `cmake -S . -B build -DSMS_ARCH=32` and `cmake --build build --target sms --parallel 4`.

### 64-bit build (in progress)

```sh
SMS_ARCH=64 ./build_linux.sh
SMS_ARCH=64 ./run_linux.sh "/path/to/Super Mario Sunshine (US).iso"
```

This builds a native x86-64 executable in `build-64/` (no multilib packages needed) and leaves the 32-bit `build/` alone.
It boots, plays the movies and reaches Delfino Plaza, rendering like the 32-bit build; it is not yet tested through every stage, so the 32-bit build stays the default.
See `PLAN-64BIT.md` for how it works and what is left.

## Run

```sh
./run_linux.sh "/path/to/Super Mario Sunshine (US).iso"
```

Or place exactly one `.iso`, `.gcm`, or Dolphin `.ciso` image in `build/rom/` and run `./run_linux.sh` without an argument.
Set `SMS_DISC_IMAGE` to use another path without passing an argument.
The game is read straight from your ISO; nothing is extracted or copied.
Saves go to `~/.local/share/sms-port/card-a`.

## Standalone executable

Pass your disc image to the build script to get an executable with the game's files inside it:

```sh
./build_linux.sh "/path/to/Super Mario Sunshine (US).iso"      # -> build/sms-standalone
./build_mac.sh "/path/to/Super Mario Sunshine (US).iso"        # -> build-mac/sms-standalone
./build_windows.sh '/path/to/Super Mario Sunshine (US).iso'    # -> build32/bin/sms-standalone.exe
```

The scripts also take the image from `SMS_DISC_IMAGE`, or from a single image in `build/rom/` (`build-mac/rom/` on macOS, `build32/bin/rom/` on Windows).
`tools/bundle_disc.py` reads the image (`.iso`, `.gcm` or Dolphin `.ciso`), checks that it is GMSE01, and packs the disc's files into a trimmed disc image with no padding (about 1.1 GiB).
It appends that image to a copy of `sms`, followed by a small trailer that `platform/disc` finds when the program starts.
`build/sms` itself is unchanged and still takes a disc image.
The standalone executable needs no image, no `rom/` folder and no extracted files, so it can be copied and run on its own (on Windows its MinGW and SDL2 DLLs are still needed next to it or on `PATH`).
`./run_linux.sh`, `./run_mac.sh` and `./run_windows.sh` without a disc argument start it when it exists.
A disc argument, `SMS_DISC_IMAGE` or `SMS_DISC_ROOT` still takes precedence over the bundled files.
Keep the executable private: it contains the game.
For a manual build, add `-DSMS_BUNDLE_DISC=/path/GMSE01.iso` to the `cmake -S` command and build the `sms_standalone` target.

Useful options (put them before the command, e.g. `SMS_SKIP_MOVIES=1 ./run_linux.sh ...`):

| Option | Effect |
| --- | --- |
| `SMS_SKIP_MOVIES=1` | skip the intro and opening movies |
| `SMS_AUDIO=0` | no sound |
| `SMS_HEADLESS=1` or `--headless` (after the image path) | no window, for testing |

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
| Game and movie speed x1 / x2 / x4 / x10 (overlay open) | F7 |
| Quit | Esc |

A USB or Bluetooth game controller also works.

On the file-select screen, walk Mario left under a block for about half a second and press A to jump into it.

More detail (every option, the platform layer, the patches) is in `README.md`.
