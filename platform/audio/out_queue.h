// How many DAC blocks the SDL output keeps queued (ai.cpp).
//
// Blocks are mixed on the emulated CPU, so the output runs dry when the
// game's thread holds it for longer than the queue lasts, and the gap clicks.
// After a short dry spell (a slow frame, a shader compile) the queue grows by
// a block, or by enough to have covered the spell, so hitches like it play
// through. Every queued block is 17.5 ms more between a sound starting in the
// game and the player hearing it, so a block is given back after each
// kGiveBackFrames without a dry spell.
//
// A dry spell longer than all the extra blocks together cannot be covered by
// queueing more: that is a load (the HD texture preload and the shader
// warm-up hold the CPU for a second or more as a stage starts), not a hitch.
// It leaves the queue as it was. Growing for loads put every stage at the
// longest queue, 140 ms, which took 150 s of play to give back: sounds came
// about 5 frames late (sms-launcher Discord, 2026-11-03).
//
// No game headers are used, so tests/queue_test.cc can include it on its own.
#ifndef SMS_PORT_AUDIO_OUT_QUEUE_H
#define SMS_PORT_AUDIO_OUT_QUEUE_H
#include <stddef.h>
#include <stdint.h>

namespace port_out_queue {

const uint32_t kRate        = 32000;
const uint32_t kBlockFrames = 560; // JAudio's DAC block (0x460 samples), 17.5 ms
const int kBaseBlocks       = 3;   // blocks the output keeps queued, 52 ms
const int kMaxExtra         = 5;   // more after a dry spell: up to 8 blocks, 140 ms
// Longest dry spell the extra blocks can cover (87.5 ms); longer ones are loads.
const uint32_t kMaxCoveredFrames = kMaxExtra * kBlockFrames;
const uint32_t kGiveBackFrames   = kRate * 10; // a block back after 10 s without a dry spell

struct State {
	int extra;             // blocks queued beyond kBaseBlocks
	uint32_t dryFrames;    // device frames with nothing to play, this dry spell
	uint32_t steadyFrames; // device frames played since the last counted dry spell
};

// One device buffer: `frames` played, `dry` of them with nothing queued.
// `counting` is false before the first block and while the DMA engine is
// stopped, when silence is expected. Returns +1 when the queue grew, -1 when
// it gave a block back, 0 otherwise.
inline int update(State& s, size_t frames, size_t dry, bool counting)
{
	if (dry && counting) {
		uint32_t total = s.dryFrames + (uint32_t)dry;
		s.dryFrames    = total < s.dryFrames ? UINT32_MAX : total; // a stall of days saturates
		return 0;
	}
	if (s.dryFrames) {
		uint32_t spell = s.dryFrames;
		s.dryFrames    = 0;
		if (spell > kMaxCoveredFrames)
			return 0; // a load: no queue covers it, and the queue it left would only delay sounds
		s.steadyFrames = 0;
		int cover      = (int)((spell + kBlockFrames - 1) / kBlockFrames);
		int need       = s.extra + 1 > cover ? s.extra + 1 : cover;
		if (need > kMaxExtra)
			need = kMaxExtra;
		if (need > s.extra) {
			s.extra = need;
			return 1;
		}
		return 0;
	}
	if (s.extra > 0 && (s.steadyFrames += (uint32_t)frames) >= kGiveBackFrames) {
		s.extra--;
		s.steadyFrames = 0;
		return -1;
	}
	return 0;
}

} // namespace port_out_queue

#endif
