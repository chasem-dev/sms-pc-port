# Super Mario Eclipse in the native port

[Super Mario Eclipse](https://github.com/JoshuaMKW/Super-Mario-Eclipse) is a large Sunshine mod: new stages, objects, characters, menus and script functions.
This page records what running it natively takes, what the port already has for it, and a plan.
Nothing of Eclipse is in this repository.

## How Eclipse runs on a GameCube (or Dolphin)

- **Code.** Eclipse is a Kuribo module (PowerPC code loaded at boot by a patched `main.dol`) built on [BetterSunshineEngine](https://github.com/DotKuribo/BetterSunshineEngine) (BSE), another Kuribo module.
  Both change the game by overwriting the retail binary at fixed addresses: a `bl` redirected to their own function (`SMS_PATCH_BL`), a branch (`SMS_PATCH_B`), or an instruction replaced (`SMS_WRITE_32`).
  BSE turns many of those into an API (stage, player and game callbacks, object registration, SunScript functions, THP and music, settings), which Eclipse uses; Eclipse adds its own patches besides.
- **Game classes.** Both are written against [SunshineHeaderInterface](https://github.com/JoshuaMKW/SunshineHeaderInterface), their own declarations of the retail classes: the same memory layout as the decomp's, under other member names (`TMario::mState` there is `mStatus` here, `mSpeed` is `mVel`).
- **Data.** Eclipse's stages, models, text and movies are files on its disc: its `build.py` assembles an extracted game folder and packs it into an ISO.
- **Release.** Players get Eclipse from [GameBanana](https://gamebanana.com/mods/536309) (v1.1.0): a 7z holding an xdelta patch that turns the North American ISO (MD5 `0c6d2edae9fdf40dfc410ff1623e4119`) into a `GMSE04` Super Mario Eclipse ISO, with its code already built into the disc's `main.dol`.
- **Licence.** Eclipse's code, BSE and SunshineHeaderInterface are GPL-3.0; the released mod is CC BY-NC-ND 4.0, and its patcher script MIT.

## Size of the job

Counted with [`tools/mods/patch_inventory.py`](../tools/mods/patch_inventory.py), which resolves every patch address to the game function it lands in and, from the decomp's linked `mario.elf`, the retail instruction it replaces:

| | patches | game functions touched | of which `bl` redirects |
| --- | --- | --- | --- |
| Eclipse ([inventory](mods/eclipse-patches.md)) | 257 | 95 | 144 |
| BSE ([inventory](mods/bse-patches.md)) | 697 | 259 | 377 |

Eclipse's own code is about 18,500 lines and calls 79 BSE API functions (most often `Spc::` script builtins, `Stage::register*Stage` and `add*Callback`, `Objects::registerObjectAs*`, `Player::add*Callback` and per-player data, `THP::addTHP`, `Music::`, `Settings::`).
Much of BSE's own patching is features the port has or does not need (60 fps, 16:9 and 21:9, bug fixes, the Kuribo loader), so only the part of BSE that Eclipse reaches has to come along.

## Building it

```sh
python3 tools/mods/get.py eclipse          # the Eclipse ISO, mods/eclipse/Super Mario Eclipse v1.1.0.iso
cmake -S . -B build-ecl -DSMS_ARCH=32 -DSMS_ECLIPSE=ON
cmake --build build-ecl
SMS_DISC_IMAGE="mods/eclipse/Super Mario Eclipse v1.1.0.iso" build-ecl/sms
```

`-DSMS_ECLIPSE=ON` ([cmake/eclipse.cmake](../cmake/eclipse.cmake)) fetches Eclipse, BSE and SunshineHeaderInterface at pinned revisions into the build directory (`SMS_ECLIPSE_SRC_DIR` to put them elsewhere), fixes them up mechanically ([fixup_sources.py](../platform/mods/eclipse/fixup_sources.py)) and builds them with clang into the 32-bit port.
Nothing of theirs is kept in this repository.
Without it, the build is the plain port: every hook below is in the source but finds nothing registered and runs the original code.

## How the port runs it

- **Patches.** Each `SMS_PATCH_BL`/`SMS_PATCH_B`/`SMS_WRITE_32` registers under its retail address in the port's registry ([modhooks.cpp](../platform/mods/modhooks.cpp)) instead of writing to memory, from the mods' static constructors, which run when the modules load (after `TApplication::initialize` has the heaps and DVD up), as Kuribo runs them.
  BSE's run-time instruction rewrites (`PowerPC::writeU32`) are recorded the same way.
- **Hooks.** The decomp source asks the registry at each patched call site ([sms_modhook.h](../src/port_include/sms_modhook.h)).
  [tools/mods/gen_hooks.py](../tools/mods/gen_hooks.py) writes most of them (`decomp-patches/zz-modhook-50-calls.patch`): it finds the retail call in the disassembly, the matching call in the source, and emits a typed hook.
  It also passes on what a mod function reads from its caller's registers (`SMS_FROM_GPR`), worked out from the retail code around the call, and reorders arguments into the mod function's declared order (the PowerPC keeps integer and float arguments apart, so a mod may declare them in any interleaving).
  The rest are hand-written `modhook-*` patches.
- **Game functions by retail name.** SunshineHeaderInterface's `raw_fn.hxx` calls game functions through casts of their retail addresses; those go to typed trampolines into the decomp, generated by [tools/mods/gen_rawfn.py](../tools/mods/gen_rawfn.py).
  Functions the decomp only has inline are in [port_shims.cpp](../platform/mods/eclipse/port_shims.cpp).
- **Layouts.** The 32-bit port lays out the game's classes as retail does (the `layout-01` patch removes Itanium tail-padding reuse), so SunshineHeaderInterface's view of an object is valid on the port's.
- **Data.** The Eclipse disc as it is, with the port's byte-order conversion; two converter fixes came from it (JAudio files read straight from disc, and J3D files whose empty sections point at the next table).

For bisecting, `SMS_MOD_LIST=1` prints every registered patch and `SMS_MOD_DISABLE=addr,addr` switches patches off by retail address; `SMS_MOD_REPORT=1` lists, at exit, patches the game never reached.

## Status (2026-09-26)

- The 32-bit port boots the Eclipse disc to its title screen, file select (save-file creation included) and first stage, and runs it with Eclipse's dialogue and HUD.
- **BetterSunshineMoveset.** Eclipse's disc also loads a third module, BetterSunshineMoveset, and Eclipse refuses to start without it.
  It is not built in yet, so the runs above switch off BSE's replacement of the application loop, where that check happens: `SMS_MOD_DISABLE=80005624`.
  Building it in is the next step, pending a decision to fetch that repository too.
- **Hooks left.** 194 of 257 redirected calls are hooked; the rest need hand-written hooks, and about 150 of BSE's patches replace an instruction inside a function rather than a call (the scenario-select screen's table rewrite, Mario's extended animation tables and many physics tweaks), each ported by hand.
- **64-bit.** Not yet: SunshineHeaderInterface describes 32-bit layouts.

## Licensing

A binary that includes BSE or Eclipse is a work under GPL-3.0.
Keeping them out of this repository and fetching them only when the Eclipse component is built keeps the port itself unaffected, but how builds with Eclipse may be shared depends on the port's own licence, which this repository does not state yet.
That is for the port's owner to decide before any Eclipse code is added.
