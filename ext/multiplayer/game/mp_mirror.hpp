#ifndef MP_MIRROR_HPP
#define MP_MIRROR_HPP

// Mirrored objects (mp_mirror.cpp): things whose state the game changes on
// its own (a boat's drift, a wheel's turn, a slot's reels, an egg's wish,
// where an NPC has walked) are run by one game, which sends their state; the
// others take it. The owner is whoever rides or last used the object, else
// the host.

// A stage is in play: find its mirrored objects.
void MpMirrorCollect();
// Every tick after movement: send what this game owns, take the rest.
void MpMirrorTick();
// This game's player used `object` (sprayed it, say): this game runs it for a while.
void MpMirrorUsed(const void* object);
// Whether the others' hits must not land on `object` here (its owner runs
// it, and sends the outcome: a slot machine's draw, for one).
bool MpMirrorKeepsHits(const void* object);
// Whether this game's Mario stands on Bowser's hot tub (the host tips it under him).
bool MpMirrorOnTub();

#endif
