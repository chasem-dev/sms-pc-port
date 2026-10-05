# Minecraft source assets

The PNGs in this directory are Minecraft Java Edition 1.20.1 assets created by Mojang, downloaded unchanged from the public InventivetalentDev/minecraft-assets GitHub mirror. Minecraft artwork remains the property of Mojang/Microsoft; this directory does not claim ownership or an open-source license for the artwork.

The unchanged Ogg recordings under `audio/` come from Mojang's official `resources.download.minecraft.net` CDN through the Java Edition 1.20.1 asset index. `audio/sources.json` records that index, the original sound event definitions, object SHA-1, SHA-256 and download URL for all retained samples. These recordings also remain the property of Mojang/Microsoft.

`sources.json` records the pinned upstream commit, original path, download URL, and SHA-256 of every PNG and recipe/model JSON. `tools/minecraft/import_assets.py` verifies those hashes and packs the original pixels for the port's GX renderer. The character proportions and game geometry are implemented locally; the skin, tools, wood, crafting table, door, chest, ten destroy-stage crack textures, GUI backgrounds, hearts, and font pixels come from these PNGs. The retained recipe JSON documents the upstream crafting recipes.

FLUDD's model is supplied by the user's Sunshine disc. Its hotbar indicator and the water gauge are specific to this crossover.

`tools/minecraft/import_audio.py` verifies the audio hashes, decodes with libsndfile, resamples with libsamplerate's best sinc converter, and embeds mono 32 kHz PCM in `src/minecraft/audio_assets.h`. These libraries are needed only to regenerate the checked-in header. Builds and launches need no audio download or Ogg decoder.
