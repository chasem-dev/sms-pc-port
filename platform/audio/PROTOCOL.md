# JAudio ↔ DSP protocol (as SMS uses it)

This is what the game's CPU side expects of the audio DSP task, and how `platform/audio` answers it.
Every fact here comes from the decomp's own source (file names below), the linker map, or the disc data.
Where the microcode's behaviour cannot be read from the CPU side, the choice made is marked **assumed**, with the evidence used to pick it.

Sources: `src/JSystem/dsptask.c` (DspBoot, DSPSendCommands2, DspStartWork/DspFinishWork), `src/JSystem/dspproc.c` (DsetupTable, DsyncFrame, DSPReleaseHalt), `src/JSystem/osdsp_task.c` (__DSPHandler, DsyncFrame2), `JASAudioThread.cpp`, `JASDSPBuf.cpp`, `JASAiCtrl.cpp`, `JASDSPInterface.{hpp,cpp}`, `JASDSPChannel.cpp`, `JASChannel.cpp`, `JASDriverIF.cpp`, `JASDriverTables.cpp`, `JASRate.cpp`, `JAIGFrameStream.cpp`, `JAIData.cpp`.

## Timing

`JASRate.cpp`: DAC rate 32028.5 Hz (the AI's 32 kHz mode, `AISetDSPSampleRate(0)`), a frame of 0x230 = 560 samples, split into 7 subframes of 80 samples.
The AI DMA block is 0x460 halfwords = 560 stereo s16 pairs (17.5 ms).

## Mailbox framing

`DSPSendCommands2(words, n, cb)`: wait for `Dsp_Running_Check`, send mail `n`, `DSPAssertInt()`, then `n` words (`n == 0` means 2 words).
If `cb` is set, `DspStartWork(words[0], cb)` queues `words[0] >> 16` as the id to acknowledge.

Replies come through the DSP interrupt.
`__DSPHandler` reads a first mail; `0xDCD10004` calls the audio task's `req_cb` (`AudioThread::syncDSP`), which reads a second mail:

| Second mail | Meaning | CPU action |
| --- | --- | --- |
| `0xF355FF00` | subframe rendered | message 1 to the audio thread: next subframe, or finish the frame |
| `0xF355xxxx`, other | command acknowledged | `DspFinishWork(xxxx)`: runs the queued callback if `xxxx` matches |

`0xF355` is `DSPInterface::JAS_DSP_PREFIX`.
Boot: the task start mail `0xDCD10000` makes the handler call `init_cb` (`DspHandShake`), which waits for one more mail and then sets `Dsp_Running_Start`.

## Commands

| Words | Sender | Meaning |
| --- | --- | --- |
| `0x81000000 \| n`, `CH_BUF`, `DSPRES_FILTER`, `DSPADPCM_FILTER`, `FX_BUF` | `DsetupTable` (from `DSPInterface::setupBuffer`) | voice count and table addresses; the CPU spins until it is acknowledged (`0xF3558100`) |
| `0x82000000 \| subframes << 16 \| level`, `outA`, `outB` | `DsyncFrame` (from `DSPBuf::finishDSPFrame`) | start a frame: `subframes` × (frame/`subframes`) samples into `outA` and `outB`, which are the two halves of one `dsp_buf` (so frame = `(outB − outA) / 2` samples); `level` = `DSP_MIXERLEVEL`. No acknowledgement expected |
| (count 0) map hi, map lo | `DSPReleaseHalt` (from `DSPBuf::updateDSP`) | the 64-bit enabled-voice map (voice 0 = MSB); the DSP renders the next subframe of the current frame and replies `0xF355FF00` |

A frame therefore runs: `0x82` → release (CPU has just run `TDSPChannel::updateAll`) → subframe 0 → `FF00` → CPU updates channels → release → subframe 1 … → the 7th `FF00` makes the CPU finish the frame and, if a triple-buffer slot is free, start the next (`DSPBuf::process`).

## Output path

`Kernel::vframeWork` (on each AI DMA interrupt) takes the DSP's finished buffer and interleaves it with `Calc::imixcopy(outB-half, outA-half, dac)`, so DMA pairs are (outB, outA).
It then adds CPU-mixed extras through `extMixCallback` and hands the block to `AIInitDMA`.
The DMA callback (`syncAudio`) is the whole audio clock: each block's start wakes the audio thread (message 0 → `Kernel::updateDac`).

## Voice block (`DSPInterface::DSPBuffer`, 0x180 bytes × 64)

Written by the CPU field by field, so on the PC port they are in host byte order.
Fields the DSP writes back are marked ←.

| Offset | Field | Meaning |
| --- | --- | --- |
| 0x000 | `enabled` | set by `playStart`, cleared by the CPU (`replyFinishRequest`, `stop`) |
| 0x002 | `done` ← | the voice has finished: one-shot end, or stop request completed. `TDSPChannel::updateAll` then calls back and clears `done`/`enabled` |
| 0x004 | `resamplingRatio` | Q12 step (0x1000 = one source sample per output sample); `4096 × pitch × waveRate / 32028.5`, capped 0x7FFF. Streams use `(rate << 12) / 32000` |
| 0x008 | `resetVpb` | set by `playStart`: restart decoding (the DSP clears it) |
| 0x00A | `endReached` ← | set with `done` at a one-shot's end |
| 0x00C | `useConstantSample` | pause flag: no output, no advance |
| 0x00E | `samplesToKeepCount` | init delay max (surround delay; **ignored**) |
| 0x010 | `mixChannels[6]` {bus, target, current ←, level} | six sends: bus address (below), Q15 target volume, current volume (ramps to target over one subframe), delay bytes (**ignored**) |
| 0x050–0x058 | auto mixer: position (pan << 8 \| dolby), fx << 8, volume current ←/target, enable | used instead of `mixChannels` when the channel's mix config is 0xFFFF (`updateAutoMixer`); SMS uses this for most sound effects |
| 0x060, 0x068 | position fraction ←, position ← | for streams: `currentPosition >> 16` is read by `Get_DirectPCM_Counter` |
| 0x06C | `samplesBeforeLoop` ← | streams: samples to the ring's end, 16.16 (`Get_DirectPCM_LoopRemain`) |
| 0x074 | `remainingLength` ↔ | streams: samples still to play (0xFFFFFFFF = endless); `Get_DirectPCM_Remain` |
| 0x100 | `samplesSourceType` | 9 = ADPCM4, 5 = ADPCM2, 8 = PCM8, 16 = PCM16 (from `setWaveInfo`'s `COMP_BLOCKBYTES`), 0x21 = direct PCM (streams), < 4 = oscillator (`setOscInfo`) |
| 0x102 | `isLooping` | |
| 0x104/0x106 | `loopYN1`/`loopYN2` | ADPCM history for the loop |
| 0x108 | `filterMode` | bit 5: IIR on, low 5 bits: FIR8 length (**filters not applied yet**) |
| 0x10A | `endRequested` | `TDSPChannel::forceStop`: fade out this subframe, then `done` |
| 0x10C | `unk10C` | CPU-owned age counter (voice stealing) |
| 0x110 | `loopAddress` | loop start, a **sample index** (stored through an `s16*`); streams: ring start |
| 0x114 | `endPosition` | loop end, or sample count; streams: ring length << 16 |
| 0x118 | `baseAddress` | ARAM byte address of the wave; streams: main-memory `s16*` ring |
| 0x11C | `sampleCount` | |
| 0x120, 0x148, 0x150 | FIR8, IIR (biquad), distance low-pass coefficients | **not applied yet** |

Bus addresses (`setBusConnect`'s `connect_table`, index = the mix config's high byte): 1 = 0x0D00, 2 = 0x0D60, 3 = 0x0DC0, 4 = 0x0E20, 5 = 0x0E80, 6 = 0x0EE0, 7 = 0x0CA0, 8 = 0x0F40, 9 = 0x0FA0, 10 = 0x0B00, 11 = 0x09A0.
The default mix config (`JASChannelMgr`: 0x150, 0x210, 0x352, 0x412) sends vol·sin(1−pan) to bus 1, vol·sin(pan) to bus 2, and the same times fxmix to buses 3 and 4.
Streams send voice *i* to bus *i*+1 at 0x7FFF.
So bus 1 is left and bus 2 right (pan 0 → bus 1), `outA` carries bus 1 and `outB` bus 2 (**assumed**: the only reading consistent with the pan law and the stream routing), and DMA pairs are (right, left).

## FX lines (`DSPInterface::FXBuffer`, 0x20 bytes × 4)

`setFXLine` from `JAIData` with the AAF's FX scene table (cmd 7): mode (0x00), delay length in 80-sample subframes (0x02), a main-memory delay buffer (0x04), bus/gain pairs `SEND_TABLE[cfg.unk2]`/`cfg.unk4` (0x08/0x0A) and `SEND_TABLE[cfg.unk6]`/`cfg.unk8` (0x0C/0x0E), and 8 FIR taps (0x10).
SMS's scenes: lines 0/1 return to buses 1/2 (gain 4096 or 2047), second target bus 8, delay 48 subframes (120 ms), taps `0 0 846 846 4715 8343 13421 −4096`.
**Assumed**: line *i* is fed by bus 3+*i* (the per-voice fx sends), its delayed signal is FIR-filtered, fed back into the delay line, and returned to its first bus at gain/4096; returns to buses other than 1/2 are dropped.
This gives an echo/reverb tail of the right length and routing, not a verified match.

## Levels

Voice volumes are Q15 (the CPU caps channel volumes at `MAX_MIXERLEVEL` = 0.802 × 16384 in SMS; auto-mixer volumes go up to 0x7FFF).
`DSP_MIXERLEVEL` defaults to 0x4000 and SMS sets 5.0 × 4096 = 0x5000.
**Assumed**: Q14, i.e. 0x4000 = unity and SMS's 0x5000 = 1.25×.
The Q12 reading (5×) clips most of the game's output; Q14 leaves the boot jingle at about −1.5 dBFS and clips only 2 samples in 99 s of play.
`SMS_AUDIO_MASTER_SHIFT` overrides it.

## Wave data

`.aw` files hold the waves at the WSYS table's offsets; `JASWaveArcLoader` copies them into ARAM, so `baseAddress` is an ARAM address and `port_aram_ptr` resolves it.

- **ADPCM4** (AFC, 9-byte frames of 16 samples): header byte = scale exponent (high nibble) and predictor index (low nibble) into `DSPADPCM_FILTER` (16 × {c1, c2}, Q11); then 16 signed nibbles, high nibble first.
  `y = sat16(((n << scale) << 11) + c1·y₋₁ + c2·y₋₂) >> 11)`.
- **ADPCM2** (5-byte frames): 16 signed 2-bit values, high bits first, scaled as `n × 4` on the 4-bit scale above.
- **PCM8**: signed 8-bit. **PCM16**: big-endian s16.
- **Loops**: the stored `loopYN1/YN2` are y₋₁/y₋₂ of the ADPCM frame that contains the loop start.
  The decoder resumes at that frame with that history and decodes forward to the loop start.
  Looped waves are stored only up to the loop end, so the table's sample count can exceed the data.

`tests/audio_test` checks the decoders against the disc: decoding every looped wave from its start reproduces the stored loop history exactly for all 449 ADPCM4 and 3 ADPCM2 loops (which confirms the nibble order, the scale and 2-bit rules and the predictor table).

## Resampling

`DSPRES_FILTER`'s first 256 halfwords are 64 phases × 4 taps (Q15, each phase sums to 0x8000), applied to x[n−1], x[n], x[n+1], x[n+2] with phase = fraction >> 10.
The position advances by `ratio << 4` in 16.16 per output sample.
Without the game's table (the WAV test) a Catmull-Rom table is built instead.

## Not implemented

- Oscillator voices (`samplesSourceType` < 4, `noteOnOsc`): skipped (logged once), still finished on stop requests.
- Per-voice FIR8/IIR/low-pass filters and the surround delay bytes.
- Buses 5–11 other than as FX inputs (Dolby surround output mode).
- DVD audio streaming (DTK `AISetStreamPlayState`, `JASHardStream`): SMS's streams go through DirectPCM voices instead.
