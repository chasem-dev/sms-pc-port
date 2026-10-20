# Native Apple Silicon (macOS arm64)

`SMS_ARCH=arm64 ./build.sh` builds `build/macos-arm64/sms` (and `SMS.app`), a native arm64 program; the default macOS build stays x86_64, which Apple Silicon runs under Rosetta ([BUILD.md](../BUILD.md#native-apple-silicon)).

## The problem

The game keeps pointers in 32-bit slots (script VM slots, resource structs laid over file data, `u32` arithmetic on addresses), so every address it can see has to survive a round trip through a `u32`.
The other 64-bit builds keep that memory below 4 GiB ([64-BIT.md](64-BIT.md)): MEM1 at `0x80000000`, static data, thread stacks and `port_low_alloc`'s blocks.

Native arm64 macOS cannot: the kernel reserves the low 4 GiB of every arm64 process (PAGEZERO). An executable linked with a smaller `-pagezero_size` is killed at launch, and deallocating part of PAGEZERO at run time leaves addresses nothing can be mapped at.
Above it, the system's shared library cache takes `0x180000000` to `0x300000000`, and the kernel loads the executable just above PAGEZERO (`-image_base` is ignored for arm64).

## Game addresses above PAGEZERO

A game address `x` (the 32-bit value the game sees, as on every other build) lives at

| Game addresses | Host addresses | What is there |
| --- | --- | --- |
| below `0x80000000` | `0x100000000 + x` (`PORT_GAME_LO`) | the executable (loaded at `0x100000000`), the boot and thread stacks, `port_low_alloc`'s pool at `0x110000000` |
| from `0x80000000` | `0x10000000000 + x` (`PORT_GAME_HI`, 1 TiB) | MEM1 at `0x10080000000`, the hardware register sink at `0x100CC000000` |

Truncating any of these pointers to 32 bits gives the game address, so pointer-to-`u32` casts (all of them truncations) need nothing.

`PORT_GAME_HI` was first `0x700000000` (MEM1 at `0x780000000`, just above the shared cache). On macOS 27 with an M5 that address was already taken when the game started, and MEM1 anywhere else cannot work: the game's code reaches it at `PORT_GAME_HI` only. Addresses just above the shared cache are where the system puts its own mappings (on macOS 15 with an M4, a hint at `0x1000000000` is placed at `0x7000000000`), so MEM1 is at 1 TiB, far above them. If it still cannot be mapped there, the game stops with a message and logs what holds the address, instead of falling back to a heap block as the other builds do.
The way back is `tools/gameptr/GamePtrPass.cpp`, an LLVM pass plugin that CMake builds at configure time against the compiler's own LLVM (Homebrew's `llvm`, which also has `llvm-config` and the headers) and loads with `-fpass-plugin=` for the game and the platform layer:

- **Every integer turned into a pointer** (each `inttoptr`, after the optimiser has run) gets the base of its half added when it is from 64 KiB up to 4 GiB, which on arm64 macOS is never a host address.
  Smaller values stay as they are: no game address is that low (the linker pads the executable's header so that no code is in its first 64 KiB), and they are small integers passed as pointers, such as null and the `OSMessage` codes JAudio's audio thread sends itself and reads back with `switch ((uintptr_t)msg)`.
  Constant addresses (the game's low-memory globals at `0x80000028`, initialisers) are rebased at compile time.
- **Pointer arithmetic on such a pointer** is 32-bit arithmetic on the console, and relocations rely on it with a file offset as the pointer: `(u8*)info->mLayerInfos + (u32)info` (the pollution map) adds an address in MEM1 to an offset that was rebased into the low half. So each `getelementptr` whose base is one of those casts is rebased again from the low 32 bits of its result; no object spans the two halves, so nothing else changes.
- **Float-to-integer conversions** get x86's results where C leaves them undefined: a negative float converted to an unsigned type wraps (`-41.9` to `u8` is `0xD7`, as on x86 and in MWCC's code; arm64's instruction clamps it to 0, which blurred textures through `J3DTevs`' colour arithmetic), and a value outside a signed 32-bit int gives `0x80000000` (arm64 saturates).

`PTR32` fields (`dolphin/types.h`) convert with a plain cast in plugin-built code (so `(uintptr_t)(void*)field` folds back to the offset a relocation adds, as on the other builds), do their in-place `+=` / `-=` in 32 bits, and `port_game_ptr` rebases for code built without the plugin.
`platform/gx` is built without it, since GL takes buffer offsets as pointers: its MEM1 base for physical addresses and the write-gather pipe address add `PORT_GAME_HI` themselves.
The rest of the platform layer is built with `-ffp-contract=off` on arm64 (clang fuses multiply-adds there by default, which x86 does not).

Save states hold pointers to the game's globals and vtables, which need the executable at the same address in a later run. arm64 macOS always loads executables at a random address, so `port_main.cpp` starts the process again once with address randomisation off (`POSIX_SPAWN_SETEXEC` with `_POSIX_SPAWN_DISABLE_ASLR`, as lldb launches), which puts it at `0x100000000` every run.
The boot thread moves onto its stack in the game's window as the x86_64 build does (AppKit wants the main thread), and `codesign -s -` signs the executable again after `tools/savestate_ranges.py` writes into it (arm64 macOS kills a process whose signature does not match).

## Compared with the x86_64 build (Apple M4, macOS 15.7)

Scripted runs with the deterministic clock, captures compared pixel by pixel (`tools/vulkan/compare.py`'s runs, both builds with the same backend):

- The title and its attract cycles to field 20,000: every capture (every 150 fields) identical; the game's audio (`SMS_AUDIO_WAV`, 914 s) byte-identical.
- `plaza` and the plaza `tour` (warps through the plaza's episodes, the pollution included), OpenGL and Vulkan: identical but for a small moving object in a few fields (46 pixels at plaza field 4100, 51 at tour field 9400: a particle and a reflection drawn a pixel or a shade apart). Two arm64 runs give identical captures, so it is a systematic difference, not yet traced.
- A save state saved by one run loads exactly (not the portable way) in another, and the frame after the load matches the frame after loading in place.
- The x86_64 build's captures are unchanged by this work (title to field 20,000 and the plaza, compared with captures from before it).

Frame rate (`tools/vulkan/bench.py --exe`, the plaza tour in a 1280x960 window, vsync off):

| | OpenGL | Vulkan |
| --- | --- | --- |
| x86_64 (Rosetta) | 2.30 ms (435 fps) | 2.21 ms (453 fps) |
| arm64 | 1.85 ms (542 fps) | 1.76 ms (570 fps) |

## Building it, and checking it in CI

- The plugin needs a Clang built with plugin support and `llvm-config` with LLVM's headers beside it: Homebrew's `llvm`, or the arm64 toolset of SMS Launcher (LLVM's clang and `llvm-config` in its `env/llvm/bin`, which the launcher puts first for an arm64 build). Apple's clang cannot load it.
- `tools/gameptr/test.sh` builds the plugin and checks what it rebases and converts.
- `.github/workflows/macos-arm64.yml` runs that test and builds `SMS_ARCH=arm64` with Homebrew's LLVM on an Apple Silicon runner; `launcher-build.yml` builds it with the launcher's toolset (`SMS_SMOKE_ARCHES=64,arm64` on the Apple Silicon runner).
- Every compile's arguments carry the plugin's hash (`PORT_GAMEPTR_HASH`), so a compiler cache does not reuse objects built with another plugin.

## Not covered yet

- Eclipse (`SMS_ECLIPSE=1`) has not been built for arm64; the launcher keeps it on x86_64.
- The small difference above, and anything a scripted run does not reach.
