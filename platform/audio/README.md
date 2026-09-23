# platform/audio — software DSP and audio output

SMS's audio driver (JAudio, in the decomp) runs unchanged on the emulated CPU.
This module replaces the hardware it talks to: the DSP microcode that mixes voices (a software mixer that reads the same voice blocks the game writes), and the AI DMA engine that plays the mixed blocks (SDL2 output at 32 kHz).
`PROTOCOL.md` documents the mail protocol, the voice-block layout and the evidence behind each choice.

| File | Owner | What |
| --- | --- | --- |
| `dsp_mixer.{h,cpp}` | audio | The mixer: ADPCM4/ADPCM2/PCM8/PCM16 from ARAM, DirectPCM stream rings from main memory, 4-tap polyphase resampling with the game's `DSPRES_FILTER`, per-send Q15 volume ramps, the auto mixer, the four FX lines, the master level. No game headers. |
| `dsp_hle.cpp` | audio | The DSP task's side of the mail protocol: setup (0x81), sync frame (0x82), release-halt → render one subframe → `0xF355FF00` through `__DSPHandler`. The SDK-named `DSP*` functions are built only with `SMS_AUDIO_DSP_HLE` (see Integration). |
| `ai.cpp` | audio | `AI*`: DMA latch/callback emulation, SDL2 output (loaded with `dlopen`), host-clock pacing without a device, WAV recording. |
| `noaudio.cpp` | bring-up lead | `SMS_NO_AUDIO` (empty sound configuration). |
| `tests/` | audio | `audio_test`: offline mixer test against the disc (not part of the CMake build). |
| `../../decomp-patches/audio-01-*.patch`, `audio-02-*.patch` | audio | JAudio bitfield/byte views that assumed big-endian layout (mix-config bus numbers, BMS note-on flags). |

## Integration (for the bring-up lead)

The AI layer is already live: `ai.cpp`'s strong `AI*` definitions replace the weak stubs, so JAudio's DAC loop now runs on its real clock.
With the current handshake-only fake DSP, that loop outputs silence.
`SMS_AUDIO=0` restores the old idle DMA.

To switch to the software DSP (two edits, both in your files):

1. In `platform/misc/sdk_data.cpp`, remove or `#ifndef SMS_AUDIO_DSP_HLE` the fake DSP block: the `__DSP_curr_task`/`__DSP_first_task`/`__DSP_last_task` globals, the mail queue, `DSPCheckMailFromDSP`, `DSPReadMailFromDSP`, `DSPCheckMailToDSP`, `DSPSendMailToDSP`, `DSPAssertInt`, `DSPInit`, `__DSP_boot_task`, `__DSP_insert_task`, `__DSP_exec_task`, `__DSP_remove_task`.
   `dsp_hle.cpp` defines the same set.
2. Add `SMS_AUDIO_DSP_HLE=1` to the `sms` target's compile definitions in `CMakeLists.txt`.

If you would rather keep your functions, forward to the hook instead.
Each of these is `extern "C"` in `dsp_hle.cpp` and always built:

```c
void port_audio_dsp_boot(DSPTaskInfo* task);   /* from __DSP_boot_task, after setting __DSP_curr_task */
u32  port_audio_dsp_check_mail_from(void);     /* DSPCheckMailFromDSP */
u32  port_audio_dsp_read_mail_from(void);      /* DSPReadMailFromDSP */
void port_audio_dsp_mail_to(u32 mail);         /* DSPSendMailToDSP */
void port_audio_dsp_assert_int(void);          /* DSPAssertInt */
/* DSPCheckMailToDSP must return 0 (mail is consumed at once). */
```

Replies are delivered with `port_irq_defer` → the decomp's `__DSPHandler`.
That means the handshake, the setup acknowledgement and every subframe reach the game at the next interrupt check point, which is `OSRestoreInterrupts` at the end of `DSPSendCommands2`.
The busy-wait loops in `DsetupTable` and `DspBoot` rely on this.
Nothing else in the OS layer is needed.
The ARAM lookup uses `port_aram_ptr` from `platform/ar`.

This was verified by linking a private binary with `-DSMS_AUDIO_DSP_HLE=1` and `-Wl,--allow-multiple-definition` (audio objects first) against the `build/` game library.
It boots, and the logo, UI sounds, sequences and voice clips play; see Status.

## Environment

| Variable | Effect |
| --- | --- |
| `SMS_AUDIO=0` | AI DMA stays idle (no audio thread clock, no output); also implied by `SMS_NO_AUDIO` |
| `SMS_AUDIO_OUT=sdl\|null` | output device; the default is SDL, or `null` when headless. `null` paces DMA from the host clock |
| `SMS_AUDIO_WAV=file.wav` | record everything played (32 kHz stereo) |
| `SMS_AUDIO_TRACE=1` | log each voice start and the voice counts every 5 s |
| `SMS_AUDIO_FX=0` | bypass the FX (echo) lines |
| `SMS_AUDIO_SWAP=0` | keep DMA pair order (the default swaps the (right, left) DMA pairs for SDL) |
| `SMS_AUDIO_MASTER_SHIFT=n` | fixed point of the DSP master level (default 14: 0x4000 = unity) |

## Status

- **Decoders are bit-exact against the disc.**
  Decoding every looped wave from its start reproduces the loop-start history stored in the WSYS tables: 449 of 449 ADPCM4 loops and 3 of 3 ADPCM2 loops (`tests/audio_test`).
- **In game** (60–100 s headless runs with `SMS_AUDIO_WAV`, using `SMS_AUTOPRESS` to reach the menus): the boot jingle, menu sound effects, Mario's PCM16 voice clips and BMS sequence music play.
  That is about 2,000 note-ons in 100 s, up to 13 simultaneous voices, and voices are released and reused.
  Output is paced in real time (29.3 s recorded in a 30 s SDL run).
- Before `audio-02`, every sequence note-on was misparsed (little-endian bitfields): notes never released, all 64 voices stayed busy, and `TNoteMgr::getChannel` could crash on a slot index of 121.
  Before `audio-01`, the mix-config bus number read the wrong byte.
- Levels: with the Q14 master, the loudest moments peak near full scale and clipping is rare: 2 clipped samples in a 99 s run and 17 (at one moment) in a 30 s run.
  The Q14 choice is an inference (PROTOCOL.md, Levels).
- Stream voices (DirectPCM, `title.afc` and cutscene audio) are implemented to the protocol but have not been reached in a test run yet.
- Not implemented: oscillator voices, per-voice FIR/IIR/low-pass filters, surround delay, Dolby surround buses.
  The FX line model is an approximation (PROTOCOL.md).

## Offline test

```sh
cd platform/audio/tests && make && ./audio_test /path/to/disc/files out/
```

It reads `mSound.aaf` from `data/nintendo.szs` (Yaz0 + RARC), checks the decoders over all 24 wave archives, and writes WAVs.
The WAVs are one-shots and loops from `w1stLoad_0.aw`/`wScene_0.aw`, a wave an octave up and down (lengths halve and double), and one through the AAF's FX scene 0.
It prints lengths and levels.
