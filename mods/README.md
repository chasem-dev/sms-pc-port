# mods/

Optional additions to the game, each switched on by putting it here.
Nothing in this folder is needed to play, and git ignores everything in it but this file.

## textures/: HD texture packs

Texture packs made for Dolphin's "Load Custom Textures" work unchanged.
Unpack one into `mods/textures/`, for example the [Super Mario Sunshine UHD Texture Pack](https://github.com/qashto/Super_Mario_Sunshine_UHD_Texture_Pack) (from its `GMS.7z` release, `GMS/Textures/GMS` goes to `mods/textures/GMS`).
Every `tex1_*.png` and `tex1_*.dds` below `mods/textures/` is used, in any sub-folder; several packs can sit side by side.
DDS files can hold BC1–BC3 or BC7 blocks or plain RGBA; a GPU that cannot sample BC7 (macOS) gets them decoded.

A pack names each image after the texture it replaces (its size, format and a hash of its data), so it matches the game's own textures wherever they are loaded.
Replacements load in the background: a texture shows its original until its replacement has been read.
They look best with a larger internal resolution, for example `SMS_GX_SCALE=2`.

Switches: `SMS_TEXTURE_PACKS=dir;dir` uses those folders instead of `mods/textures/`, `SMS_TEXTURE_PACKS=0` turns packs off, and `SMS_TEXTURE_PACK_LOG=1` logs the pack name of every texture the game loads and whether it was replaced (for checking a pack or making one).
Replacements take more video memory than the original textures: the UHD pack's plaza textures are about 16 times the size of the originals.
