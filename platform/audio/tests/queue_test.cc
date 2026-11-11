// The output queue rule (out_queue.h): short dry spells grow the queue, loads
// leave it, and steady play gives blocks back.
#include "../out_queue.h"
#include <assert.h>
#include <stdio.h>

using namespace port_out_queue;

static const size_t kBuffer = 512; // the SDL device buffer ai.cpp asks for

// Plays `frames` with nothing queued, then one full buffer that ends the spell.
static int dry_spell(State& s, uint32_t frames)
{
	int step = 0;
	for (; frames >= kBuffer; frames -= kBuffer)
		step += update(s, kBuffer, kBuffer, true);
	if (frames)
		step += update(s, kBuffer, frames, true);
	return step + update(s, kBuffer, 0, true);
}

static int steady(State& s, uint32_t frames)
{
	int step = 0;
	for (uint32_t played = 0; played < frames; played += kBuffer)
		step += update(s, kBuffer, 0, true);
	return step;
}

int main()
{
	State s = {};

	// A short hitch grows the queue by one block.
	assert(dry_spell(s, 100) == 1 && s.extra == 1);
	// One just past the base queue grows it by what would have covered it.
	assert(dry_spell(s, 3 * kBlockFrames) == 1 && s.extra == 3);
	// Never past the longest queue.
	assert(dry_spell(s, kMaxCoveredFrames) == 1 && s.extra == kMaxExtra);
	assert(dry_spell(s, 100) == 0 && s.extra == kMaxExtra);

	// Steady play gives a block back every kGiveBackFrames.
	assert(steady(s, kGiveBackFrames - kBuffer) == 0 && s.extra == kMaxExtra);
	assert(steady(s, kBuffer) == -1 && s.extra == kMaxExtra - 1);
	assert(steady(s, kMaxExtra * kGiveBackFrames) == -(kMaxExtra - 1) && s.extra == 0);
	assert(steady(s, 2 * kGiveBackFrames) == 0 && s.extra == 0);

	// A load (the user's log: textures prepared for 1.8 s) leaves the queue at the base...
	assert(dry_spell(s, kRate * 18 / 10) == 0 && s.extra == 0);
	assert(dry_spell(s, kMaxCoveredFrames + 1) == 0 && s.extra == 0);
	// ...and does not restart the give-back count of a grown queue.
	assert(dry_spell(s, 100) == 1 && s.extra == 1);
	assert(steady(s, kGiveBackFrames / 2) == 0);
	assert(dry_spell(s, kRate * 2) == 0 && s.extra == 1);
	assert(steady(s, kGiveBackFrames / 2 + kBuffer) == -1 && s.extra == 0);

	// Silence before the first block or with the DMA engine stopped is not a dry spell.
	for (int i = 0; i < 100; i++)
		assert(update(s, kBuffer, kBuffer, false) == 0);
	assert(s.dryFrames == 0 && s.extra == 0);

	puts("queue_test: ok");
	return 0;
}
