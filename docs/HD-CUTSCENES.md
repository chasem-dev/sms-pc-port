# HD cutscenes

The optional HD pack replaces movie pixels while keeping the game's cutscene playback, subtitles, rumble, skip controls, frame rate and original compressed audio tracks.
The disc stays intact; movies without a replacement play normally.
The complete pack enhances all 21 original THP movies at 3× resolution: most become 1920×960, taller movies become 1920×1344, and the in-world portal animation becomes 384×432.
Download the patches from the [HD cutscene release](https://github.com/chasem-dev/sms-pc-port/releases/tag/hd-cutscenes-ai-v1), or let the launcher install them.

## Play with HD textures

In the updated SMS Launcher, enable **HD textures** in Settings and choose **Finish HD setup** or **Play**.
The launcher downloads the texture pack and movie patches, then prepares the movies using your own North American Sunshine disc image (GMSE01).
Existing HD texture users also receive the cutscenes when finishing setup.
The game loads a complete installed movie pack automatically while HD textures are enabled.
Turning HD textures off disables the automatic movie replacement.
Your disc and saves stay intact, and the pack carries over across game updates.
Eclipse uses its own movie assets; the Sunshine patches are installed for Sunshine mode.

For a source installation, build this revision of the port (see [build instructions](../BUILD.md)), then run the same installer used by the launcher:

```sh
python3 tools/media/install_cutscenes.py --iso /path/to/GMSE01.iso
./run.sh
```

The installer accepts ISO, GCM and CISO images.
Python 3 is needed for installation; FFmpeg and an AI runtime are not needed to install or play the pack.
Each movie requires its exact original disc checksum.
All 21 reconstructed movies must match the release checksums before the pack becomes active.
Failed or cancelled setup keeps the previously installed pack.
The movie patches download about 5.7 GB and install about 5.8 GB. Allow at least 7.8 GB free for movie setup, plus space for HD textures if those are not installed yet; the installer checks space before starting.
The launcher shows download and installation progress and the required space.

For offline installation, add `--manifest /path/to/cutscene-release.json --bundle /path/to/patches`.
The local bundle must contain all 21 matching `.smpatch` assets.
To disable only the automatic movie enhancement from a source installation, use `SMS_HD_CUTSCENES=0 ./run.sh`.
Explicit asset mods retain precedence over the automatic enhancement pack.

## Play a locally generated movie

Put generated THP replacements in `mods/hd-cutscenes/files/data/`, using the original movie filenames, then run:

```sh
./run-hd-cutscenes.sh /path/to/GMSE01.iso
```

This helper also supports an incomplete set of locally generated movies, and enables cutscenes even if they were disabled in settings.
With a freshly built standalone executable, the disc argument is optional.
Use `SMS_ARCH=64` to select the 64-bit build on Linux when both builds exist.
Existing `SMS_MOD` entries are retained; the helper's HD pack takes precedence for the movies it replaces.
For a pack elsewhere:

```sh
SMS_HD_CUTSCENES=/path/to/hd-cutscenes ./run-hd-cutscenes.sh
```

Movies and conversion work are separate from Git.
Use a drive with enough free space for large movie and intermediate frame files.

## Generate from your disc

### Batch 6× AI previews

On the configured Linux machine, run this command to start the remaining scenes in the background:

```sh
/home/netflix/sms-port/upscale-remaining-cutscenes.sh --background
```

It prints the process ID and progress log path. You can close the terminal after it starts.
To see the list and check the tools and original files without processing, use `--plan` instead.
To watch progress in the foreground, omit `--background`.
The local configuration skips the already completed airplane intro (`openingA.thp`) and processes the other 20 movies, shortest first.
Rerunning the same command reuses verified completed movies and valid intermediate AI frames.
Only missing, damaged, or unfinished AI frames are processed again.
Stop a foreground run with Ctrl+C, or stop a background run with `kill PROCESS_ID`; rerun the command to resume.
One batch can run at a time in the configured work folder.

Each scene receives a **2× AI pass followed by a 3× AI pass**, using `realesr-animevideov3`.
The video pipeline adds padding when needed and preserves the AI-generated pixels without an interpolation resize.
Wide 640×320 scenes become 3840×1920 footage on a 3840×2160 canvas.
Taller 640×448 scenes remain 3840×2688 so their framing is preserved.
The 128×144 portal animation becomes a native 768×864 preview.
Frame rate and original dialogue are retained; silent animations stay silent.
Every movie is fully decoded and checked for its original frame count before it is marked complete or copied to FileBrowser.
Temporary frame sequences are cleared after each successful movie to limit disk usage; interrupted scenes retain their work for resuming.
The batch stops if either working drive has less than 3 GiB free.

Local outputs go to `/mnt/1tbhdd2/sms-hd-cutscenes-v1/6x-ai-previews/`.
Finished movies are also copied to **Render-Previews** in FileBrowser, named `sunshine-SCENE-6x-ai-v1.mp4`.
The source disc, model files, generated movies, progress logs, and machine configuration remain outside Git.
These are MP4 previews. In-game playback of 6× movies needs additional THP playback support.
The intro took about 36 minutes on the GTX 1060 / i5-6600K; allow roughly **9–10 hours** for the remaining set, an estimate rather than a full-set measurement.

The helper reads `build/media/6x-ai-config.json`, or the path in `SMS_AI_CUTSCENES_CONFIG`.
To configure another Linux checkout, create that JSON file with these fields, pointing at the tools and originals described below:

```json
{
  "manifest": "/path/to/cutscene-work/manifest.json",
  "originals": "/path/to/cutscene-work/originals",
  "output": "/path/to/local/6x-ai-previews",
  "work": "/path/to/large-drive/6x-ai-work",
  "ffmpeg": "/path/to/ffmpeg",
  "realesrgan": "/path/to/realesrgan-ncnn-vulkan",
  "models": "/path/to/models",
  "gpu": "0",
  "threads": 4,
  "skip": ["openingA.thp"],
  "minimum_free_gb": 3,
  "keep_frames": false
}
```

Add `"publish": "/path/to/FileBrowser/Render-Previews"` to copy verified movies there.
The model folder must contain both the `.bin` and `.param` files for `realesr-animevideov3-x2` and `realesr-animevideov3-x3`.
Use `--only SCENE.thp` to select one movie from the manifest.

### Single movie previews and THP replacements

Install Python 3 and FFmpeg.
For AI processing, also obtain the executable and model files from the official [Real-ESRGAN project](https://github.com/xinntao/Real-ESRGAN) and [ncnn Vulkan releases](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan/releases).
The release uses the ncnn executable from [v0.2.0](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan/releases/tag/v0.2.0), with the `realesr-animevideov3` models from the official [20220424 Ubuntu bundle](https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-ubuntu.zip), scale 3.
It cleans up compressed edges and produces smoother surfaces, but reconstructs detail and can change fine textures or small facial features.
It is not a re-render from the original animation assets.
Lanczos mode provides a more conservative interpolation option.

Extract all THPs and a manifest with dimensions, timing, audio tracks and SHA-256 hashes:

```sh
python3 tools/media/cutscenes.py extract \
  --iso /path/to/GMSE01.iso --out /path/to/cutscene-work
```

Generate a full AI preview and a playable THP replacement:

```sh
python3 tools/media/cutscenes.py upscale \
  --input /path/to/cutscene-work/originals/openingA.thp \
  --output /path/to/cutscene-work/opening-ai-hd.mp4 \
  --method ai --scale 3 \
  --realesrgan /path/to/realesrgan-ncnn-vulkan \
  --models /path/to/models \
  --work /path/to/cutscene-work/opening-ai \
  --thp mods/hd-cutscenes/files/data/openingA.thp
```

Use `--ffmpeg /path/to/ffmpeg` if FFmpeg is not on PATH.
Omit the AI arguments and use `--method lanczos` for interpolation.
Choose fresh output filenames for a new run.
Work folders record the source hash and processing settings, so partial AI frame batches can be reused only for the same job.
Conversion logs stay in that folder; `--keep-frames` also retains the decoded and upscaled frames and intermediate MJPEG for inspection.
HD frame sets can use several gigabytes of temporary space.

The MP4 preview fits the original framing inside a 1920×1080 canvas and uses AAC audio.
The THP keeps its natural dimensions (1920×960 or 1920×1344 at scale 3), every source frame, and all original compressed audio packets without re-encoding.
The tool verifies the preview's decoded frame count and the THP's audio packet hash.

## Playback implementation

Host-only patches expand the SDK decoder's MCU row buffers to support aligned dimensions up to 2048×2048 and preserve pointer width in work-buffer alignment.
The host draw helper copies Y/U/V tile rectangles into a persistent low-address pool, then uses the original renderer with textures at most 960×960.
This avoids GX's 1024-pixel texture limit and its 32-bit texture address slots.
MovieDirector scales only its display-size copy of the header, preserving placement and subtitle geometry.
Original-sized movies follow the existing draw path.
No FFmpeg library or AI runtime is required by the game; those tools are used only when generating movies.

The standalone decoder check is:

```sh
make -C platform/thp/tests run ARCH=-m64 \
  DISC=/path/to/hd-cutscenes/files MOVIE=data/openingA.thp
```

All 21 enhanced movies passed full native SDK decoding: 31,638 video frames and matching audio sample counts.
Source frame counts, frame rates and all compressed audio track hashes are preserved. Every release patch was reconstructed from the original disc and matched its target SHA-256 checksum.

The tiled draw check covers every Y/U/V pixel and display boundary at 1920×960, 1920×1344, 1280×896 and 2048×2048, plus the unchanged 640×448 path:

```sh
make -C platform/thp/tests tiles ARCH=-m64
make -C platform/thp/tests tiles ARCH=-m32
```

Full opening playback completed in both Linux word sizes; matching captures were pixel-identical. Taller movies, the portal animation, widescreen framing and the ending's return to the save screen were also checked. The largest movie allocation passed playback in the 32-bit build's default memory budget.
The ordinary plaza and audio checks also match the existing regression baseline on Mesa software rendering.

## Build the patch release

The installer applies SMP1 patches to movies extracted from the player's own disc.
Patches contain enhanced video frames; audio and original timing metadata come from that disc.
Maintainers build and round-trip verify the full release with:

```sh
python3 tools/media/build_cutscene_release.py \
  --source /path/to/cutscene-work/originals \
  --pack /path/to/hd-cutscenes --out /path/to/patches \
  --url-base https://github.com/chasem-dev/sms-pc-port/releases/download/hd-cutscenes-ai-v1
```

Each patch reconstructs the exact verified HD THP hash.
The catalog records source, patch and output identities for all 21 movies; it is committed with the matching port revision after the full set passes verification.
Publish the catalog and patch files at its pinned release URLs before selecting that revision in the launcher.
The large movie assets are release files, not Git files.
