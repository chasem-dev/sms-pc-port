// Bosses shared in multiplayer. Each boss is its own state machine, so each
// is taught here one at a time; Petey Piranha (TBossPakkun) first.
//
// The host's boss is the real one. Its state rides in the world snapshot:
// where it is, which step of its behaviour (nerve) it is in, its hit points,
// how much water it has swallowed and whether it is belly up or flying. A
// client's copy follows: it takes each new step when the host's boss does
// (only then, so its animations are not restarted every tick), and its
// position, hit points and water every tick. Its own AI still runs between
// the host's steps (Petey aims at the player beside him in that game).
//
// What a client's player does to its copy is reported to the host and done
// to the real boss: water in Petey's mouth (TBossPakkun::gotWaterDamage) and
// ground pounds on his belly (gotHipDropDamage). A copy never goes down
// before the host's boss does; when the host's goes down, every copy plays
// the defeat and brings out the boss's Shine, which anyone can get for
// everyone (mp_game.cpp's Shine Get).
#include "mp_boss.hpp"
#include "../mp_game_api.h"

#include <Enemy/BossPakkun.hpp>
#include <Enemy/Conductor.hpp>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

enum { HIT_WATER = 1, HIT_HIP_DROP = 2 };

// The boss state in the snapshot (the same build on both sides).
struct BossState {
	u8 kind;  // 1: Petey Piranha
	u8 nerve; // index in the boss's nerve table; 0xFF: one not in it
	s8 state;
	u8 hitPoints;
	s16 water;
	u8 flags; // 1: defeated (dead), 2: draining water (unk17C)
	u8 pad;
	f32 pos[3];
	s16 rotY;
	s16 headYaw;
};

typedef const TNerveBase<TLiveActor>* Nerve;

// Petey's nerves, in a fixed order both games share.
Nerve peteyNerve(int i)
{
	switch (i) {
	case 0: return &TNerveBPWait::theNerve();
	case 1: return &TNerveBPCannon::theNerve();
	case 2: return &TNerveBPVomit::theNerve();
	case 3: return &TNerveBPTornado::theNerve();
	case 4: return &TNerveBPPivot::theNerve();
	case 5: return &TNerveBPSwallow::theNerve();
	case 6: return &TNerveBPTumbleIn::theNerve();
	case 7: return &TNerveBPTumble::theNerve();
	case 8: return &TNerveBPTumbleOut::theNerve();
	case 9: return &TNerveBPGetUp::theNerve();
	case 10: return &TNerveBPSwing::theNerve();
	case 11: return &TNerveBPStompReact::theNerve();
	case 12: return &TNerveBPJumpReact::theNerve();
	case 13: return &TNerveBPPreDie::theNerve();
	case 14: return &TNerveBPDie::theNerve();
	case 15: return &TNerveBPTakeOff::theNerve();
	case 16: return &TNerveBPFly::theNerve();
	case 17: return &TNerveBPTouchDown::theNerve();
	case 18: return &TNerveBPFlyCannon::theNerve();
	case 19: return &TNerveBPFlyPivot::theNerve();
	case 20: return &TNerveBPHover::theNerve();
	case 21: return &TNerveBPFall::theNerve();
	case 22: return &TNerveBPSleep::theNerve();
	case 23: return &TNerveBPBreakSleep::theNerve();
	default: return nullptr;
	}
}
const int kPeteyNerves = 24;
const int kPeteyPreDie = 13, kPeteyDie = 14;

int peteyNerveIndex(Nerve n)
{
	for (int i = 0; i < kPeteyNerves; i++)
		if (peteyNerve(i) == n)
			return i;
	return 0xFF;
}

TBossPakkun* s_petey;
// client: the host's last step we took, and what we set this game's copy to
int s_hostNerve = -1;
u8 s_setHitPoints;
s32 s_setWater;
bool s_followed;

s16 yawOf(f32 degrees) { return (s16)(degrees * (65536.0f / 360.0f)); }
f32 degreesOf(s16 yaw) { return yaw * (360.0f / 65536.0f); }

} // namespace

void MpBossStageSetup()
{
	s_petey     = nullptr;
	s_hostNerve = -1;
	s_followed  = false;
	if (!gpConductor)
		return;
	for (JGadget::TList<TEnemyManager*>::iterator it = gpConductor->unk20.begin(); it != gpConductor->unk20.end(); ++it) {
		TBossPakkunManager* mgr = dynamic_cast<TBossPakkunManager*>(*it);
		if (mgr && getenv("SMS_MULTIPLAYER_DEBUG"))
			fprintf(stderr, "[multiplayer] a Petey manager (%s), %d of them\n",
			        mgr->mIsLightVersion ? "light" : "full", mgr->getObjNum());
		if (!mgr || mgr->mIsLightVersion) // the light Petey (no fight) needs nothing
			continue;
		for (int i = 0; i < mgr->getObjNum() && !s_petey; i++)
			s_petey = dynamic_cast<TBossPakkun*>(mgr->getObj(i));
	}
	if (s_petey)
		fprintf(stderr, "[multiplayer] Petey Piranha is here: the host's Petey is everyone's\n");
}

void MpBossHostFrame()
{
	TBossPakkun* b = s_petey;
	if (!b)
		return;
	// the others' hits, on the real one
	int kind, count;
	while (mpg_next_boss_hit(&kind, &count)) {
		if (getenv("SMS_MULTIPLAYER_DEBUG"))
			fprintf(stderr, "[multiplayer] Petey: %d hit(s) of kind %d from another game\n", count, kind);
		if (kind == HIT_WATER)
			for (int i = 0; i < count; i++)
				b->gotWaterDamage();
		else if (kind == HIT_HIP_DROP && b->mState == BOSSPAKU_STATE_BELLY_UP)
			b->gotHipDropDamage();
	}
	BossState st;
	memset(&st, 0, sizeof st);
	st.kind      = 1;
	st.nerve     = (u8)peteyNerveIndex(b->getLatestNerve());
	st.state     = b->mState;
	st.hitPoints = b->getHitPoints();
	st.water     = (s16)b->mWaterMark;
	st.flags     = (b->checkLiveFlag(LIVE_FLAG_DEAD) ? 1 : 0) | (b->unk17C ? 2 : 0);
	st.pos[0]    = b->mPosition.x;
	st.pos[1]    = b->mPosition.y;
	st.pos[2]    = b->mPosition.z;
	st.rotY      = yawOf(b->mRotation.y);
	st.headYaw   = yawOf(b->mHeadYaw);
	mpg_set_boss(&st, sizeof st);
	static int debugFrames;
	if (getenv("SMS_MULTIPLAYER_DEBUG") && ++debugFrames % 300 == 0)
		fprintf(stderr, "[multiplayer] Petey (host): nerve %d, state %d, %d hit points, water %d\n", st.nerve, st.state,
		        st.hitPoints, st.water);
}

void MpBossClientTick()
{
	TBossPakkun* b = s_petey;
	if (!b)
		return;
	BossState st;
	float age = 0.0f;
	const size_t got = mpg_boss(&st, sizeof st, &age);
	static int why = -1;
	const int now = got != sizeof st ? 1 : st.kind != 1 ? 2 : age > 1.0f ? 3 : b->checkLiveFlag(LIVE_FLAG_DEAD) ? 4 : 0;
	if (now != why && getenv("SMS_MULTIPLAYER_DEBUG"))
		fprintf(stderr, "[multiplayer] Petey (here): following %s (got %d bytes, age %.1f)\n",
		        now == 0 ? "the host's" : now == 1 ? "nothing: no boss in the snapshot" : now == 2 ? "nothing: another boss"
		        : now == 3 ? "nothing: the snapshot is old" : "nothing: defeated here", (int)got, age);
	why = now;
	if (got != sizeof st || st.kind != 1 || age > 1.0f) {
		s_followed = false;
		return;
	}
	// what our player did to this copy since we last set it: for the real one
	if (s_followed) {
		if (b->getHitPoints() < s_setHitPoints)
			mpg_boss_hit(HIT_HIP_DROP, s_setHitPoints - b->getHitPoints());
		if (b->mWaterMark > s_setWater)
			mpg_boss_hit(HIT_WATER, b->mWaterMark - s_setWater);
	}
	if (b->checkLiveFlag(LIVE_FLAG_DEAD)) {
		if (s_followed)
			fprintf(stderr, "[multiplayer] Petey is down here too: his Shine is out\n");
		s_followed = false;
		return; // defeated here: its Shine is out
	}
	// a new step of the host's: take it
	const int local = peteyNerveIndex(b->getLatestNerve());
	if (st.nerve != s_hostNerve && st.nerve < kPeteyNerves) {
		s_hostNerve = st.nerve;
		if (local != st.nerve) {
			b->mSpine->reset();
			b->mSpine->setNext(peteyNerve(st.nerve));
		}
	} else if ((local == kPeteyPreDie || local == kPeteyDie) && st.nerve != kPeteyPreDie && st.nerve != kPeteyDie
	           && st.nerve < kPeteyNerves) {
		// down here but not for the host: not yet
		b->mSpine->reset();
		b->mSpine->setNext(peteyNerve(st.nerve));
	}
	b->mState       = st.state;
	b->mHitPoints   = st.hitPoints;
	b->mWaterMark   = st.water;
	b->unk17C       = (st.flags & 2) ? 1 : 0;
	b->mHeadYaw     = degreesOf(st.headYaw);
	// where the host's is, eased (a step's own motion carries it between)
	const f32 k = 0.25f;
	b->mPosition.x += (st.pos[0] - b->mPosition.x) * k;
	b->mPosition.y += (st.pos[1] - b->mPosition.y) * k;
	b->mPosition.z += (st.pos[2] - b->mPosition.z) * k;
	f32 dr = degreesOf(st.rotY) - b->mRotation.y;
	while (dr > 180.0f)
		dr -= 360.0f;
	while (dr < -180.0f)
		dr += 360.0f;
	b->mRotation.y += dr * k;
	s_setHitPoints = st.hitPoints;
	s_setWater     = st.water;
	s_followed     = true;
	static int debugTicks;
	if (getenv("SMS_MULTIPLAYER_DEBUG") && ++debugTicks % 300 == 0)
		fprintf(stderr, "[multiplayer] Petey (here): nerve %d (host %d), state %d, %d hit points, water %d, %.0f from the host's\n",
		        peteyNerveIndex(b->getLatestNerve()), st.nerve, b->mState, b->getHitPoints(), b->mWaterMark,
		        sqrtf((st.pos[0] - b->mPosition.x) * (st.pos[0] - b->mPosition.x)
		              + (st.pos[2] - b->mPosition.z) * (st.pos[2] - b->mPosition.z)));
}
