// DVD audio streaming (DTK): the drive decodes an ADPCM file region and the
// Audio Interface mixes it with the DSP's output, as streamed music. Retail
// Sunshine never streams; BetterSunshineEngine plays Super Mario Eclipse's
// stage music this way (/AudioRes/Streams/Music/*.adp).
//
// DVD side (platform/dvd/dvd.cpp): DVDPrepareStreamAsync hands the region,
// read whole, to port_dtk_prepare; the play address is the file's startAddr
// (its FST entry here) plus the bytes played, which is all a caller can
// compare it with; a cancelled or finished stream is idle (status 0).
// AI side: the stream's play state, volumes, sample counter and trigger
// interrupt (AIRegisterStreamCallback) as the SDK's AIS functions keep them.
// The counter runs at the stream rate whenever the stream plays, data or not.
//
// port_dtk_mix runs where each AI DMA block starts (platform/audio/ai.cpp):
// it decodes as many stream samples as the block lasts, resampled linearly to
// the DAC rate, scaled by the volumes (n * vol >> 8, as the hardware) and by
// the music volume (dsp_mixer.h: the stream counts as music), added to the
// block's output, and raises the trigger interrupt. It runs with
// or without an output, so the stream keeps time as the DMA engine does.
//
// ADPCM: 32-byte blocks, a predictor/scale byte for each channel then 28
// bytes of samples (left in the low nibble), decoded as Dolphin's
// StreamADPCM does.
#include "port_compat.h"
#include "port_os.h"
#include "port_platform.h"
#include <dolphin/ai.h>

#include "dsp_mixer.h"

#include <mutex>
#include <string.h>
#include <vector>

extern "C" u32 AIGetStreamSampleRate(void);

namespace {

struct Dtk {
	std::mutex mu;
	// DVD side: the prepared region
	bool active;
	u32 base, offset; // startAddr of the file, file offset of data[0]
	std::vector<u8> data;
	size_t pos;       // bytes of data decoded
	s16 block[28 * 2];
	int index;        // next frame of block
	s32 hl1, hl2, hr1, hr2;
	// AI side
	bool playing;
	u8 volL, volR;
	u32 count, trigger;
	AISCallback cb;
	// resampler: the two input frames around the next output frame
	double t;
	s16 prevL, prevR, curL, curR;
	// the music volume (Q15) at the end of the last block, where the next one ramps from
	bool gainSeen;
	s32 gain;
} g;

// s32 is long in the 32-bit build (as CodeWarrior's), so the std::min and
// std::max of it and an int literal do not deduce
s32 clamp(s32 v, s32 lo, s32 hi) { return v < lo ? lo : v > hi ? hi : v; }

s16 decode_sample(s32 bits, s32 q, s32& hist1, s32& hist2)
{
	s32 hist = 0;
	switch (q >> 4) {
	case 1: hist = hist1 * 0x3c; break;
	case 2: hist = hist1 * 0x73 - hist2 * 0x34; break;
	case 3: hist = hist1 * 0x62 - hist2 * 0x37; break;
	}
	hist    = clamp((hist + 0x20) >> 6, -0x200000, 0x1fffff);
	s32 cur = (((s16)(bits << 12) >> (q & 0xf)) << 6) + hist;
	hist2   = hist1;
	hist1   = cur;
	cur >>= 6;
	return (s16)clamp(cur, -0x8000, 0x7fff);
}

// The next stream frame (silence once the region is played).
void next_frame(s16& l, s16& r)
{
	if (g.index >= 28) {
		if (!g.active || g.pos + 32 > g.data.size()) {
			g.active = false; // stopped at the end of the region
			l = r = 0;
			return;
		}
		const u8* b = &g.data[g.pos];
		for (int i = 0; i < 28; i++) {
			g.block[i * 2]     = decode_sample(b[4 + i] & 0xf, b[0], g.hl1, g.hl2);
			g.block[i * 2 + 1] = decode_sample(b[4 + i] >> 4, b[1], g.hr1, g.hr2);
		}
		g.pos += 32;
		g.index = 0;
	}
	l = g.block[g.index * 2];
	r = g.block[g.index * 2 + 1];
	g.index++;
}

} // namespace

// --- DVD side -------------------------------------------------------------------

extern "C" void port_dtk_prepare(u32 base, u32 offset, std::vector<u8>& data)
{
	std::lock_guard<std::mutex> lk(g.mu);
	g.base   = base;
	g.offset = offset;
	g.data.swap(data);
	g.pos    = 0;
	g.index  = 28;
	g.hl1 = g.hl2 = g.hr1 = g.hr2 = 0;
	g.active = !g.data.empty();
}

extern "C" void port_dtk_cancel(void)
{
	std::lock_guard<std::mutex> lk(g.mu);
	g.active = false;
	g.data.clear();
	g.pos   = 0;
	g.index = 28;
}

extern "C" u32 port_dtk_play_addr(void)
{
	std::lock_guard<std::mutex> lk(g.mu);
	return g.base + g.offset + (u32)g.pos;
}

extern "C" int port_dtk_active(void)
{
	std::lock_guard<std::mutex> lk(g.mu);
	return g.active;
}

// --- Mixing --------------------------------------------------------------------

// frames stereo frames at outRate (left, right) start playing: add the stream.
// lr may be null (no output): the stream still advances.
extern "C" void port_dtk_mix(s16* lr, size_t frames, u32 outRate)
{
	AISCallback cb = NULL;
	u32 fired      = 0;
	{
		std::lock_guard<std::mutex> lk(g.mu);
		if (!g.playing)
			return;
		const double step = (AIGetStreamSampleRate() == AI_SAMPLERATE_48KHZ ? 48000.0 : 32000.0) / outRate;
		const s32 gainTo   = port_dspmix_class_gain(PORT_DSPMIX_CLASS_MUSIC);
		const s32 gainFrom = g.gainSeen ? g.gain : gainTo;
		g.gain             = gainTo;
		g.gainSeen         = true;
		for (size_t i = 0; i < frames; i++) {
			while (g.t >= 1.0) {
				g.t -= 1.0;
				g.prevL = g.curL;
				g.prevR = g.curR;
				next_frame(g.curL, g.curR);
				if (++g.count == g.trigger && g.cb) {
					cb    = g.cb;
					fired = g.count;
				}
			}
			if (lr) {
				// the music volume (dsp_mixer.h), ramped over the block so a change does not click
				const s32 gain = gainFrom + (gainTo - gainFrom) * (s32)(i + 1) / (s32)frames;
				s32 l = (s32)(g.prevL + (g.curL - g.prevL) * g.t) * g.volL >> 8;
				s32 r = (s32)(g.prevR + (g.curR - g.prevR) * g.t) * g.volR >> 8;
				l     = (s32)((int64_t)l * gain >> 15);
				r     = (s32)((int64_t)r * gain >> 15);
				lr[i * 2]     = (s16)clamp(lr[i * 2] + l, -0x8000, 0x7fff);
				lr[i * 2 + 1] = (s16)clamp(lr[i * 2 + 1] + r, -0x8000, 0x7fff);
			}
			g.t += step;
		}
	}
	// the AIS interrupt, in the interrupt context the DMA block's runs in
	if (cb)
		cb(fired);
}

// --- Save states ---------------------------------------------------------------

// The stream as a save state keeps it (platform/savestate): the region (which
// file, from where, how long), how far it has played, the decoder and
// resampler, and the AI side. It lives here, outside game memory, and the game
// keeps its own idea of it in memory (BetterSunshineEngine's music player: what
// plays, paused or not, where the next trigger is), so a state carries both or
// the two disagree after a load: a state saved with music playing, loaded from
// the pause menu (where the music is paused), played on in silence.
namespace {
struct DtkState {
	u32 magic; // kDtkMagic
	u8 active, playing, volL, volR, gainSeen, pad[3];
	u32 base, offset, size; // the region: a file's FST entry, the offset and length read
	u32 pos;
	s32 index;
	s32 hl1, hl2, hr1, hr2;
	u32 count, trigger;
	u64 cb;
	double t;
	s16 prevL, prevR, curL, curR;
	s32 gain;
	s16 block[28 * 2];
};
const u32 kDtkMagic = 0x444b5431; // "DKT1"
} // namespace

extern "C" size_t port_dtk_state_size(void) { return sizeof(DtkState); }

extern "C" void port_dtk_state_save(void* out)
{
	std::lock_guard<std::mutex> lk(g.mu);
	DtkState s;
	memset(&s, 0, sizeof s);
	s.magic    = kDtkMagic;
	s.active   = g.active;
	s.playing  = g.playing;
	s.volL     = g.volL;
	s.volR     = g.volR;
	s.gainSeen = g.gainSeen;
	s.base     = g.base;
	s.offset   = g.offset;
	s.size     = (u32)g.data.size();
	s.pos      = (u32)g.pos;
	s.index    = g.index;
	s.hl1 = g.hl1, s.hl2 = g.hl2, s.hr1 = g.hr1, s.hr2 = g.hr2;
	s.count    = g.count;
	s.trigger  = g.trigger;
	s.cb       = (u64)(uintptr_t)g.cb;
	s.t        = g.t;
	s.prevL = g.prevL, s.prevR = g.prevR, s.curL = g.curL, s.curR = g.curR;
	s.gain = g.gain;
	memcpy(s.block, g.block, sizeof s.block);
	memcpy(out, &s, sizeof s);
}

// Reads a region again (platform/dvd/dvd.cpp), as DVDPrepareStreamAsync did.
extern "C" void port_dvd_stream_region(u32 entry, u32 offset, u32 length, std::vector<u8>& data);

// Puts the stream back as port_dtk_state_save had it, reading its region again
// when another (or none) is loaded now. Only between frames, with the game's
// threads waiting (an exact load); the mixer takes the lock.
extern "C" int port_dtk_state_load(const void* in, size_t size)
{
	DtkState s;
	if (size != sizeof s)
		return 0;
	memcpy(&s, in, sizeof s);
	if (s.magic != kDtkMagic)
		return 0;
	std::vector<u8> data;
	bool reread;
	{
		std::lock_guard<std::mutex> lk(g.mu);
		reread = s.size && (g.base != s.base || g.offset != s.offset || g.data.size() != s.size);
	}
	if (reread) {
		port_dvd_stream_region(s.base, s.offset, s.size, data);
		if (data.size() != s.size) {
			port_log("[dtk] save state: the music stream's file could not be read again\n");
			s.active = 0;
		}
	}
	std::lock_guard<std::mutex> lk(g.mu);
	if (reread)
		g.data.swap(data);
	else if (!s.size)
		g.data.clear();
	g.base     = s.base;
	g.offset   = s.offset;
	g.active   = s.active && s.pos <= g.data.size();
	g.pos      = s.pos <= g.data.size() ? s.pos : g.data.size();
	g.index    = s.index;
	g.hl1 = s.hl1, g.hl2 = s.hl2, g.hr1 = s.hr1, g.hr2 = s.hr2;
	memcpy(g.block, s.block, sizeof g.block);
	g.playing  = s.playing;
	g.volL     = s.volL;
	g.volR     = s.volR;
	g.count    = s.count;
	g.trigger  = s.trigger;
	g.cb       = (AISCallback)(uintptr_t)s.cb;
	g.t        = s.t;
	g.prevL = s.prevL, g.prevR = s.prevR, g.curL = s.curL, g.curR = s.curR;
	g.gainSeen = s.gainSeen;
	g.gain     = s.gain;
	return 1;
}

// --- AI stream registers --------------------------------------------------------

extern "C" AISCallback AIRegisterStreamCallback(AISCallback cb)
{
	std::lock_guard<std::mutex> lk(g.mu);
	AISCallback old = g.cb;
	g.cb            = cb;
	return old;
}
extern "C" u32 AIGetStreamSampleCount(void) { return g.count; }
extern "C" void AIResetStreamSampleCount(void)
{
	std::lock_guard<std::mutex> lk(g.mu);
	g.count = 0;
}
extern "C" void AISetStreamTrigger(u32 trigger) { g.trigger = trigger; }
extern "C" u32 AIGetStreamTrigger(void) { return g.trigger; }
extern "C" void AISetStreamPlayState(u32 state)
{
	std::lock_guard<std::mutex> lk(g.mu);
	g.playing = state != 0;
}
extern "C" u32 AIGetStreamPlayState(void) { return g.playing; }
extern "C" void AISetStreamVolLeft(u8 vol) { g.volL = vol; }
extern "C" u8 AIGetStreamVolLeft(void) { return g.volL; }
extern "C" void AISetStreamVolRight(u8 vol) { g.volR = vol; }
extern "C" u8 AIGetStreamVolRight(void) { return g.volR; }
