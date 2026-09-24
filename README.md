# sms-port

Native PC port of Super Mario Sunshine (GMSE01), built from the matching decompilation.

- `decomp/` — the decompilation (git submodule, branch `main` of [sms-english](https://github.com/chasem-dev/sms-english)).
  Game and JSystem source come from here; its GameCube build stays byte-identical to retail and is the reference.
- `tools/bundle_disc.py` — packs the disc's files into a standalone executable (`build/sms-standalone`).
- `platform/` — host replacements for the Dolphin SDK surface the game calls (see "Platform layer").
- `src/` — port entry point (`port_main.cpp`), the force-included compat header (`port_compat.h`) and header overrides (`port_include/`).
- `decomp-patches/` — the decomp source changes the port cannot avoid, applied to copies at configure time (never to `decomp/`).
- Assets are never stored here: the port reads the user's own disc image (or extracted disc) at run time.

### Where a fix goes

- **Decompilation bugs go in the decomp** (`sms-english`, the `decomp/` submodule): source that does not do what retail does (a wrong member, a swapped argument, a wrong constant) is fixed there, verified with `ninja changes_all` and the DOL hash, and picked up here by bumping the submodule.
  A decomp bug can show up only on PC (MWCC and g++ read the same wrong source differently), and it is still a decomp bug.
- **PC-specific fixes go in `decomp-patches/`**: byte order (`endian-*`), host compiler leniency (`0001`–`0010`, `ret-*`), host services (`thp-*`, `audio-*`), port-only features (`port-*`, `SMS_*` switches). Each patch starts with a `Reason:` line saying why it cannot live in the decomp.
- **Emulation of the hardware goes in `platform/`** (GX, DVD, OS, audio), never in game source.
- `port-03` is a decomp bug (the Group 2D 2 ortho camera's width and height are swapped in `MarDirectorInitECT.cpp`) carried as a patch until the fix lands in `sms-english`.

## Build and run

| System | Build | Run with your GMSE01 Rev 0 disc image |
| --- | --- | --- |
| Linux | `./build_linux.sh` | `./run_linux.sh "/path/to/Super Mario Sunshine (US).iso"` |
| Windows (MSYS2 MINGW32) | `./build_windows.sh` | `./run_windows.sh '/path/to/Super Mario Sunshine (US).iso'` |

See [BUILD.md](BUILD.md) for dependencies, Windows `.cmd` launchers, and manual build commands.
On Linux, `SMS_HEADLESS=1 ./run_linux.sh "/path/to/Super Mario Sunshine (US).iso"` uses offscreen rendering.
Each run script can also find a single image in its build directory's `rom/` folder.
Passing the image to the build script (`./build_linux.sh GAME.iso`) also produces `build/sms-standalone`, an executable with the disc's files bundled in that runs with no image (see [BUILD.md](BUILD.md#standalone-executable)).

- **The game comes from your disc image** (`.iso`/`.gcm`, or Dolphin `.ciso`), read in place through `platform/disc`.
  Pass an image path, set `SMS_DISC_IMAGE`, or place one image in `build/rom/` on Linux or `build32/bin/rom/` on Windows.
  An extracted disc still works: pass its `files/` directory (the disc's `sys/fst.bin` next to it keeps entry numbers exact).
- Saves go to a host-file memory card in slot A: `$SMS_SAVE_DIR`, default `~/.local/share/sms-port/card-a`.
- Sound plays through SDL2 (`platform/audio`); `SMS_AUDIO=0` mutes it and `SMS_AUDIO_WAV=out.wav` records it.
- The window uses OpenGL 3.3. The 32-bit build here falls back to Mesa's GLX when the i386 NVIDIA userspace does not match the kernel driver, so rendering is software (llvmpipe).
- A ready-to-run copy with bindings lives in `/home/netflix/sms-demo/`.

### Environment

| Variable | Effect |
| --- | --- |
| `SMS_DISC_IMAGE`, `SMS_DISC_ROOT` | game source (image file, or extracted `files/` folder); either, or a disc argument, overrides files bundled into the executable |
| `SMS_SAVE_DIR` | memory card directory |
| `SMS_HEADLESS=1` / `--headless` | no window (offscreen EGL) |
| `SMS_BINDINGS` | key bindings file (default `./bindings.txt`, then `../bindings.txt`) |
| `SMS_AUDIO=0`, `SMS_NO_AUDIO=1` | idle the AI DMA / run with an empty JAudio configuration; either way THP movies stall (their video waits for audio), so combine with `SMS_SKIP_MOVIES=1` |
| `SMS_SKIP_MOVIES=1` | report every THP movie as finished at once |
| `SMS_WARP=stage,scenario[,shines]` | debugging: loading a file goes to that area instead (`1,0,1` is Delfino Plaza right after the airstrip), optionally with that Shine count |
| `SMS_SHOTS=f,f,...`, `SMS_SHOT_DIR` | capture the XFB at these fields (retail numbering) as PPM; `tools/shots.py` converts and compares with retail |
| `SMS_AUTOPRESS=control@field[+hold],...` | scripted input (e.g. `start@1400+20,stick_left@3300+40,a@3500+30`); in a window it is injected as SDL key events, i.e. through the keyboard path |
| `SMS_FIELD_CLOCK=retrace` | shots/autopress count VI retraces (wall clock) instead of game fields (2 per display copy, the default) |
| `SMS_VI_DETERMINISTIC=1` | virtual VI/OS clock: retraces fire when the game idles (or spins on `OSGetTick` for a whole field), `OSGetTime` follows them from a fixed date, AI DMA is paced by retraces (no output device); two runs with the same input give identical frames |
| `SMS_VI_HZ=<rate>` | retrace rate override (benchmarking) |
| `SMS_VI_FIELD_BASE=<n>` | the retrace counter starts at `n` (retail spends about 240 fields in IPL/apploader/DOL load before the game's first frame; 240 puts the Nintendo logo on retail's field 300) and selects game-frame parity |
| `SMS_DVD_BPS`, `SMS_DVD_SEEK_MS`, `SMS_DVD_LOG=1` | drive timing model (reads occupy the drive for bytes/rate + seek, counted in fields; off by default) and a per-read log |
| `SMS_MOVIE`, `SMS_TRACE_OUT` | `.dtm` movie input and retail-format field traces (`platform/trace`) |
| `SMS_MEM_MB`, `SMS_QUIET_STUBS=1` | emulated MEM1 size; silence first-call stub logs |
| `SMS_GX_*` | graphics switches (`platform/gx/README.md`) |

## Building

- 32-bit (`-m32`, needs `gcc-multilib g++-multilib`); `-DSMS_ARCH=64` compiles but is not expected to run (pointer-in-u32 data).
- Default build type `RelWithDebInfo` = `-O2 -g` for all targets (the fall-off-the-end functions got explicit returns, `ret-*` patches).
- Only the i386 *runtime* SDL2/EGL libraries are installed here, so the GX build compiles against the amd64 headers and links `/usr/lib/i386-linux-gnu/libSDL2-2.0.so.0` and `libEGL.so.1` directly.
- Game units: every `.c`/`.cpp` in `decomp/src` except `dolphin/`, `PowerPC_EABI_Support/`, `TRK_MINNOW_DOLPHIN/` and `OdemuExi2/` (588 units, listed by `tools/gen_sources.py` from `configure.py`, including which get `SMS.pch`), plus the decomp's THP decoder (`platform/thp/thp.cmake`).
- Flags: `-std=gnu++03 -fno-gnu-keywords -fpermissive -fno-strict-aliasing -fwrapv -finput-charset=UTF-8 -fexec-charset=CP932 -DGEKKO -DTARGET_PC -DVERSION_GMSE01 -DBUILD_VERSION=2 -DNDEBUG=1`, host libc/libstdc++ instead of MSL, `-include src/port_compat.h`.
  String literals are Shift-JIS, as in the MWCC build (archive object names are matched against them).
- The game's global `operator new/delete` (JKRHeap) are renamed in `libsms_game.a` with `objcopy --redefine-syms`, so only game code allocates from JKR heaps; libstdc++ and `platform/` use the host allocator.
- `rand()` is MSL's (RAND_MAX 32767, same LCG) via `port_compat.h`; glibc's 2^31 range overflows the game's `1.f / (RAND_MAX + 1)`.
- Tools: `tools/contact.py OUT.png FIELDS...` (contact sheet of captures), `tools/run_capture.sh SECS FIELDS` (headless run + captures + retail comparison), `tools/gdbrun.sh` (backtrace at the first fatal signal), `tools/hangdump.sh N` (all thread stacks after N s), `tools/warn_scan.py` (one g++ warning class over all units), `tools/syntax_check.py`, `tools/gen_stubs.py`, `tools/mkpatch.sh`.

## Platform layer

| Module | What it does |
| --- | --- |
| `platform/sdk_stubs.cpp` | Generated: a weak, logging stub for all 321 SDK functions in `api-surface.tsv` that are not header inlines/macros; real implementations override them. |
| `platform/port_runtime.cpp` | Boot: emulated MEM1 (24 MiB) mapped at `0x80000000`, game source selection, GLX vendor choice, crash handler. |
| `platform/os/` | One emulated CPU: every `OSThread` is a host thread, only the CPU owner runs, strict priorities; message queues, mutexes, conds; interrupts delivered at OS-call check points and on idle; arena, clocks, OSAlloc, stopwatches, cache ops (→ `GXPC_InvalidateRange`). |
| `platform/dvd/` | DVD over the disc image (`platform/disc`) or an extracted folder; the disc's own FST; async reads complete as interrupts. |
| `platform/disc/` | GameCube `.iso`/`.gcm`/`.ciso` reader; also reads the disc image bundled at the end of the executable. |
| `platform/vi/` | 59.94 Hz retrace from a host timer (or the deterministic clock), callbacks, captures, trace hook. |
| `platform/pad/` | Controller 1 from keyboard and SDL game controllers, bindings, scripted input, `.dtm` movie input hook. |
| `platform/card/` | Memory card in slot A as host files. |
| `platform/ar/` | 16 MiB ARAM, ARQ transfers (completion runs before `ARQPostRequest` returns: JAudio busy-waits on it). |
| `platform/mtx/` | C versions of `PSMTX*`/`PSVEC*`/`C_MTX*`. |
| `platform/audio/` | DSP mail HLE, software mixer, AI output via SDL2; `noaudio.cpp` is the `SMS_NO_AUDIO` configuration. |
| `platform/thp/` | THP movie decoding (the decomp's SDK decoder built for the host). |
| `platform/endian/`, `platform/misc/endian.cpp` | Big-endian → host conversion of resources the game reads in place (dispatch from archive fetches by magic/file name). |
| `platform/gx/` | GX/GD over OpenGL 3.3. |
| `platform/trace/` | Native-vs-retail lockstep tracing. |
| `platform/misc/` | SDK globals, fallback fake DSP (only without the audio HLE), MSL `rand`. |

Low-memory OS globals and hardware register blocks that SDK headers *define* (`AT_ADDRESS`) become weak host globals via the `dolphin/types.h` override; `u32`/`s32` are `int` on LP64 hosts and unchanged on 32-bit.

## Controls

Controller 1 is read from the keyboard and from any SDL game controller (A/B/X/Y, Start, right shoulder = Z, triggers = L/R, sticks, d-pad).
Keyboard defaults (edit `bindings.txt`, or point `SMS_BINDINGS` at another file):

| GameCube | Keys |
| --- | --- |
| Control stick | arrow keys or WASD (hold Left Ctrl for half tilt) |
| C-stick | I / J / K / L |
| A | Space or X |
| B | Shift or C |
| X | V |
| Y | F |
| Z | Z |
| L / R (full press) | Q / E |
| Start | Enter |
| D-pad | 1 2 3 4 (up, down, left, right) or keypad 8 2 4 6 |
| Quit | Esc |

Format: `CONTROL = KEY KEY ...`, one control per line; a line replaces that control's defaults.
Key names are listed at the top of `bindings.txt`; `#<n>` binds a raw SDL scancode.
On the file-select screen, walk Mario under a block (hold left about half a second from the start position for block A) and jump into it with A.

## Decomp patches

Each file in `decomp-patches/` starts with a `Reason:` line; they are applied in name order to copies under `build/patched/`.

| Patch | Reason |
| --- | --- |
| 0001–0003 | g++ rejects `case` labels that jump over initialised locals (`gesso.cpp`, `MarioAutodemo.cpp`, `MarDirectorDirect.cpp`): brace the case bodies. |
| 0004 | `JASTrack.cpp`: `goto bail` jumps over `u32 r31 = 0`: hoist the declaration. |
| 0005 | `MapDraw.cpp` redeclares `SMSGetGameRenderWidth/Height` as `int` (conflicts with `Resolution.hpp`). |
| 0006–0008 | Reference-binding leniency (`MarDirectorInitECT.cpp`, `GCConsole2.cpp`, `bombhei.cpp`, `tobiPuku.cpp`). |
| 0009–0010 | Duplicate string globals (`SunModel.hpp`, `NpcAnm.cpp`/`NpcParts.cpp`) made weak. |
| 0011 | Endian: `JKRMemArchive` converts RARC metadata on open and passes each resource (with its name) to the converters on first fetch. |
| 0012 | Endian: Yaz0 decompressed length read big-endian. |
| 0013 | Endian: `JSUInputStream` typed reads convert from big-endian. |
| 0014 | `SMS_NO_AUDIO`: empty JAudio configuration; per-frame work and wave-load queries become no-ops. |
| 0015 | `TVec2`/`TVec3` `operator+`/`-` (and two TU-local helpers) return a reference to a local, which g++ compiles to a NULL return: return by value on the port. |
| 0016 | `SMS_SKIP_MOVIES`. |
| 0017 | Endian: `J3DTevStage::load` builds its `{reg, op, AB, CD}` BP command words big-endian. |
| `endian-01..16` | Loader-site byte-order fixes (JPA, J2D BLO, BMG, JUTColor, PRM, SPC, streams, DL vertex counts, sequences, card saves, THP headers, J3DSkinDeform/J3DCluster display lists, the plaza shine-shadow sphere, the HUD/map 2D archive swap); see `platform/endian/README.md`. |
| `port-02` | `SMS_WARP`: debug warp from a file-select load. |
| `port-03` | The Screen 2D and Group 2D 2 ortho cameras take width then height, as in retail. |
| `audio-01..02` | JAudio bitfield/byte-order fixes (`TChannel` mix config, BMS note-on flags); see `platform/audio/README.md`. |
| `ret-01..03` | Explicit returns for the 42 functions that fall off the end of a non-void body. |
| `thp-01..02` | Host THP decoder (portable bit reader and IDCT, big-endian audio header); see `platform/thp/README.md`. |

0001–0010 and 0015 are candidates for `#ifdef TARGET_PC` (or neutral) fixes in the decomp itself.

## Boot status

With the disc image, `build/sms` boots through the Nintendo logo, the Dolby screen, the opening THP movie and the title, creates a save file on the host memory card, shows file select, and on a scripted run (`SMS_AUTOPRESS=start@1400+20,a@3000+30,stick_left@3300+40,a@3500+30,stick_right@3700+60,a@3900+30`) walks Mario to block A, opens the file menu, starts a new game and plays the airstrip opening cutscene.
Frames are in `shots/` (`title-*`, `filesel-*`, `airstrip-*`, and `compare-*` next to retail).
Next: reach the controllable airstrip and test movement.

## Performance

Measured headless on this machine (Mesa llvmpipe software GL), 2026-09-23; details are in the commit that added this section.

- **Returns fixed.**
  `decomp-patches/ret-01..03` give all 42 fall-off-the-end functions (39 with no return statement, 3 with a path that falls off) the value retail's r3 carries.
  Five of them are live and their callers read the result: `DSPBuf::mixDSP`, `Dvd::openDvd`, `TMap::intersectLine`, `TLampTrapSpike::receiveMessage` and `TConductor::isBossDefeated`.
  After the patches, `-Wreturn-type` reports nothing over all units, so the game library no longer depends on `-O0`.
- **-O1/-O2 are safe as far as the port reaches.**
  With the existing `-fno-strict-aliasing -fwrapv`, a game library built at -O1 or -O2 boots through the logo, the attract movies (THP) and the title to file select, with audio, and showed no crashes over about 10 runs of 60–100 s.
  Gameplay levels are not reachable yet, so level code is untested at -O2.
- **Where the time goes** (main thread = game threads plus the GX translation and the GL driver front-end):

  | Build | Title/movies, VI uncapped | Boot to frame 1600 (VI 240 Hz) | File select: main thread | File select: fps (VI 240 Hz) |
  | --- | --- | --- | --- | --- |
  | all -O0 (current) | 61 fps | 97 s | 26.3 ms/frame | ~13 |
  | game lib -O1 | – | 73 s | 26.2 ms/frame | 17.4 |
  | game lib -O2 | 77 fps | 58 s | 26.2 ms/frame | 17.8 |
  | game lib + platform + `platform/gx` -O2 | – | ~42 s | 10.2 ms/frame | 36.6 |

  The game library's -O level mainly speeds up boot and movies (THP decoding lives there).
  The per-frame cost of 3D scenes is in `platform/` and `platform/gx` at -O0.
  After both are optimised, llvmpipe (about 52 ms of CPU per frame, spread over its threads) is the limit.
- **Proposal for the build lead:** build `sms_game`, `sms` and `sms_gx` with `-O2`.
  Keep `-g`, `-fno-strict-aliasing` and `-fwrapv` (the decomp type-puns freely; strict aliasing was not tried).
  Done: the default build type is `RelWithDebInfo` with `-O2 -g` for every target.
  `SMS_VI_HZ=<rate>` overrides the retrace rate for benchmarking.
