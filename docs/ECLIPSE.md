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

## What the port already has

- `python3 tools/mods/get.py eclipse` downloads the GameBanana release, checks it and your ISO, and applies the patch (with its own VCDIFF decoder, [`tools/mods/vcdiff.py`](../tools/mods/vcdiff.py)) to `mods/eclipse/Super Mario Eclipse v1.1.0.iso`, which the installer checks against the expected result (MD5 `caa546309e0443f7b47632b040a250d7`).
  It is byte-for-byte the image Eclipse's own patcher (xdelta3) makes, for Dolphin or a console.
  Given to the port (`SMS_DISC_IMAGE`), it does not boot: the vanilla code reports `Seqs/JaiSeInf.bst` missing from Eclipse's disc and then crashes while setting up sound.
  That is expected, since the disc's data is made for Eclipse's code, and nothing of Eclipse runs until that code is ported.
- `SMS_MOD` / `mod = <name>` overlays `mods/<name>/files/` on the disc ([mods/README.md](../mods/README.md)), and `SMS_DISC_IMAGE` takes any GameCube image.
  That serves Eclipse's files, but not its code: the vanilla game cannot load stages that place Eclipse's custom objects, or reach its new menus.
- Widescreen and texture packs, which BSE and Dolphin supply on the other platforms.

## Plan

The route that reuses the most is to build BSE and Eclipse from their own sources into the port, 32-bit first:

1. **Build.** Compile BSE's and Eclipse's C++ with the port's compiler against SunshineHeaderInterface, leaving out the Kuribo loader, the PowerPC assembly and the parts of BSE Eclipse does not use.
   Fetch them at build time (not vendored), as an optional component switched on by `mod = eclipse`.
2. **Layouts.** The 32-bit port lays out the game's classes exactly as retail, so SunshineHeaderInterface's view of an object is valid on the port's objects; static asserts on the sizes and member offsets of the classes Eclipse touches (TMario, TMarDirector, TMapObjBase, the actor bases...) keep that true.
   The 64-bit build lays them out differently (8-byte pointers), so it needs the headers adapted the way the decomp was (`PTR32`), later.
3. **Linking.** SunshineHeaderInterface declares the game's functions and globals by their retail names, which the decomp also uses; where a signature differs (for example `u32` as `unsigned long` there and `unsigned int` here), a generated alias bridges the two names.
4. **Hooks.** Each `SMS_PATCH_BL`/`SMS_PATCH_B` becomes a hook point in the decomp source, added by a port patch (`#ifdef TARGET_PC`) that calls the registered replacement when a mod set one and the original call otherwise.
   The inventory names the call each `bl` redirects, which pins the source line.
   `SMS_WRITE_32` patches (changed constants, removed branches) are translated by hand, each into a hook or a variable the mod sets.
   A switch keeps the vanilla game byte-for-byte unchanged in behaviour when no mod is on.
5. **Data.** The Eclipse ISO that `get.py eclipse` builds, used as the disc; the port runs its own build of Eclipse's code in place of the one on that disc's `main.dol`.
6. **Milestones.** BSE's callback and registration API working with an empty module; Eclipse compiling and linking; booting the Eclipse ISO to its title and character select; its first stage; then the rest of its hooks.

## Feasibility checks (2026-09-25)

Run outside this repository, on Eclipse `5274979` and BSE's current source, with Clang 18 for 32-bit x86 (`-m32`) against SunshineHeaderInterface:

- **Compiling.** Eclipse's 61 source files give 39 errors in total, BSE's 82 give 82; nearly all are missing include paths (BSE's `libs/` and generated headers), an undefined version macro and a few field designators, not code that depends on the PowerPC.
- **Linking.** 21 Eclipse files that compile as they are reference 393 outside symbols; 318 of them are defined in the 32-bit port binary under the same mangled names.
  The rest are Eclipse's and BSE's own, and a few game functions the decomp only has inline (`JUtility::TColor::TColor()`, `JGeometry::TVec3<float>::add`, `JDrama::TNameRefGen::getInstance`), which a small shim can define.
- **Layouts.** SunshineHeaderInterface puts `TMario::mState` at 0x7C and `mSpeed` at 0xA4, where the decomp has `mStatus` and `mVel`, and gives `TMario` 0x4290 bytes; checking every class Eclipse touches is step 2 of the plan.

So the code side looks like engineering rather than research: the hooks (step 4) are the bulk of it.

## Licensing

A binary that includes BSE or Eclipse is a work under GPL-3.0.
Keeping them out of this repository and fetching them only when the Eclipse component is built keeps the port itself unaffected, but how builds with Eclipse may be shared depends on the port's own licence, which this repository does not state yet.
That is for the port's owner to decide before any Eclipse code is added.
