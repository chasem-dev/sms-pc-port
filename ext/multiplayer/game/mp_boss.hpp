#ifndef MP_BOSS_HPP
#define MP_BOSS_HPP

// Bosses shared in multiplayer (mp_boss.cpp): the host's boss is the real
// one; the others' copies follow it, and the hits they land count on it.

// A stage's objects are set up: find its boss, if multiplayer knows it.
void MpBossStageSetup();
// Host, every frame in play: send the boss's state, take the others' hits.
void MpBossHostFrame();
// Client, every tick after movement: follow the host's boss, report hits.
void MpBossClientTick();
// Test aid: a hit by this game's player on its copy; the boss's kind, 0 if none.
int MpBossTestHit();
// Whether `actor` (a TLiveActor) is a boss shared here.
bool MpBossIsBoss(const void* actor);
// Test aid: a spray on this game's Shadow Mario; his kind, 0 if he is not here.
int MpBossTestShadow();

#endif
