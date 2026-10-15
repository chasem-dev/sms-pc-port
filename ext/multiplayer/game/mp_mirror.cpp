// Mirrored objects in multiplayer: things whose state the game changes on
// its own, run by one game (the owner), which sends their state about ten
// times a second; the others take it after each movement step.
//
//   - Who owns one: the game whose player rides it (a boat: Mario's ground
//     is it) or used it lately (sprayed a slot machine, bumped the
//     watermelon); else the host. A game that hears from a client about an
//     object leaves it to that client for half a second.
//   - Objects are named as shared objects are (mp_game.cpp): their manager's
//     place in the stage's list and their place in it, items' managers
//     included, lone actors as manager 255; the sea's waves are manager 254.
//   - Each kind says what to send (at most 48 bytes) and how to take it:
//     movers are put where the owner's are (moved on by their speed since),
//     NPCs eased toward it, a slot machine's reels set to the owner's.
//
// What the owner's game alone may decide (a slot machine's draw, an egg's
// wish) is never decided in the others: their copies are kept from it.
#include "mp_mirror.hpp"
#include "mp_boss.hpp"
#include "../mp_game_api.h"

#include <Enemy/BossWanwan.hpp>
#include <Enemy/Conductor.hpp>
#include <Enemy/FruitsBoat.hpp>
#include <Enemy/Koopa.hpp>
#include <Enemy/KoopaJr.hpp>
#include <Enemy/KoopaNerve.hpp>
#include <Enemy/LimitKoopaJr.hpp>
#include <Enemy/SleepBossHanachan.hpp>
#include <M3DUtil/MActor.hpp>
#include <Map/MapData.hpp>
#include <MoveBG/Item.hpp>
#include <MoveBG/ItemManager.hpp>
#include <MoveBG/MapObjBall.hpp>
#include <MoveBG/MapObjBianco.hpp>
#include <MoveBG/MapObjMare.hpp>
#include <MoveBG/MapObjPinna.hpp>
#include <MoveBG/MapObjSirena.hpp>
#include <MoveBG/MapObjWave.hpp>
#include <NPC/NpcBase.hpp>
#include <Player/Mario.hpp>
#include <System/Application.hpp>
#include <System/MarDirector.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const int kAlone = 255, kWaves = 254;

int fps(); // ticks a second (below)
const int kMaxMirrors = 160;

enum Owner {
	OWN_HOST,  // the host's (no player steers it)
	OWN_RIDER, // whoever stands on it
	OWN_USER,  // whoever used it last (sprayed it, bumped it)
};

struct Kind {
	const char* name;
	bool (*match)(TLiveActor*);
	int (*save)(TLiveActor*, u8* out);                     // bytes written
	void (*take)(TLiveActor*, const u8* in, int size, int age, int* scratch); // age: ticks since it came
	Owner owner;
	int every;       // ticks between sends
	bool keepsHits;  // the others' hits do not land here (the owner sends the outcome)
};

struct Mirrored {
	TLiveActor* actor; // null for the waves
	int manager, index;
	const Kind* kind;
	int usedAt;  // this game's player used it (tick)
	int heardAt; // a state came from another game (tick)
	int heardSlot;
	u8 state[48];
	int stateSize, stateAt;
	u8 sent[48];
	int sentSize, sentAt;
	int scratch; // the kind's own (a follower: the host's step last taken)
};

Mirrored s_mirrors[kMaxMirrors];
int s_count;
int s_clock;

// ------------------------------------------------------------------ bytes

struct Put {
	u8* p;
	int n;
	explicit Put(u8* out) : p(out), n(0) {}
	void raw(const void* d, int k)
	{
		if (n + k <= 48) {
			memcpy(p + n, d, k);
			n += k;
		}
	}
	void f(f32 v) { raw(&v, 4); }
	void i(s32 v) { raw(&v, 4); }
	void h(s16 v) { raw(&v, 2); }
	void b(u8 v) { raw(&v, 1); }
	void v3(const JGeometry::TVec3<f32>& v)
	{
		f(v.x);
		f(v.y);
		f(v.z);
	}
};

struct Get {
	const u8* p;
	int n, at;
	bool ok;
	Get(const u8* in, int size) : p(in), n(size), at(0), ok(true) {}
	void raw(void* d, int k)
	{
		if (at + k > n) {
			ok = false;
			memset(d, 0, k);
			return;
		}
		memcpy(d, p + at, k);
		at += k;
	}
	f32 f()
	{
		f32 v;
		raw(&v, 4);
		return v;
	}
	s32 i()
	{
		s32 v;
		raw(&v, 4);
		return v;
	}
	s16 h()
	{
		s16 v;
		raw(&v, 2);
		return v;
	}
	u8 b()
	{
		u8 v;
		raw(&v, 1);
		return v;
	}
	JGeometry::TVec3<f32> v3()
	{
		JGeometry::TVec3<f32> v;
		v.x = f();
		v.y = f();
		v.z = f();
		return v;
	}
};

f32 frameOf(MActor* a)
{
	J3DFrameCtrl* c = a ? a->getFrameCtrl(ANM_TYPE_BCK) : nullptr;
	return c ? c->getFrame() : 0.0f;
}

void setFrameOf(MActor* a, f32 frame)
{
	if (J3DFrameCtrl* c = a ? a->getFrameCtrl(ANM_TYPE_BCK) : nullptr)
		c->setFrame(frame);
}

// Eases `have` toward `want`: at once when far, a little each tick when near.
void easeTo(JGeometry::TVec3<f32>& have, const JGeometry::TVec3<f32>& want, f32 snap, f32 k)
{
	const f32 dx = want.x - have.x, dy = want.y - have.y, dz = want.z - have.z;
	if (dx * dx + dy * dy + dz * dz > snap * snap) {
		have = want;
		return;
	}
	have.x += dx * k;
	have.y += dy * k;
	have.z += dz * k;
}

// ------------------------------------------------------------------ kinds

// NPCs: where the host's have walked (each game walks its own; they are
// eased toward the host's, so they stand where everyone sees them).
bool isNpc(TLiveActor* a) { return dynamic_cast<TBaseNPC*>(a) != nullptr; }
int saveNpc(TLiveActor* a, u8* out)
{
	Put o(out);
	o.v3(a->mPosition);
	o.f(a->mRotation.y);
	return o.n;
}
void takeNpc(TLiveActor* a, const u8* in, int size, int, int*)
{
	Get g(in, size);
	const JGeometry::TVec3<f32> pos = g.v3();
	const f32 yaw                   = g.f();
	if (!g.ok || a->mHolder) // one being carried goes with its carrier
		return;
	easeTo(a->mPosition, pos, 500.0f, 0.15f);
	(void)yaw;
}

// Bianco's leaf boats: where they float and how they drift.
bool isLeafBoat(TLiveActor* a) { return dynamic_cast<TLeafBoat*>(a) != nullptr; }
int saveMover(TLiveActor* a, u8* out)
{
	Put o(out);
	o.v3(a->mPosition);
	o.v3(a->mVelocity);
	return o.n;
}
void takeMover(TLiveActor* a, const u8* in, int size, int age, int*)
{
	Get g(in, size);
	const JGeometry::TVec3<f32> pos = g.v3(), vel = g.v3();
	if (!g.ok)
		return;
	a->mPosition.set(pos.x + vel.x * age, pos.y + vel.y * age, pos.z + vel.z * age);
	a->mVelocity = vel;
}

// Noki's mud boat: steered by its rider's water.
bool isMuddyBoat(TLiveActor* a) { return dynamic_cast<TMuddyBoat*>(a) != nullptr; }
int saveMuddyBoat(TLiveActor* a, u8* out)
{
	TMuddyBoat* b = static_cast<TMuddyBoat*>(a);
	Put o(out);
	o.v3(b->mPosition);
	o.f(b->mRotation.y);
	o.f(b->mSpeed);
	o.f(b->mTurnSpeed);
	o.h((s16)b->mState);
	o.i(b->mStateTimer);
	return o.n;
}
void takeMuddyBoat(TLiveActor* a, const u8* in, int size, int, int*)
{
	TMuddyBoat* b = static_cast<TMuddyBoat*>(a);
	Get g(in, size);
	const JGeometry::TVec3<f32> pos = g.v3();
	const f32 yaw = g.f(), speed = g.f(), turn = g.f();
	const s16 state = g.h();
	const s32 timer = g.i();
	if (!g.ok)
		return;
	b->mPosition    = pos;
	b->mRotation.y  = yaw;
	b->mSpeed       = speed;
	b->mTurnSpeed   = turn;
	b->mState       = (u16)state;
	b->mStateTimer  = timer;
}

// The plaza's fruit boats: rowed along their routes, stopped and set off by riders.
bool isFruitsBoat(TLiveActor* a) { return dynamic_cast<TFruitsBoat*>(a) != nullptr; }
int saveFruitsBoat(TLiveActor* a, u8* out)
{
	TFruitsBoat* b = static_cast<TFruitsBoat*>(a);
	Put o(out);
	o.v3(b->mPosition);
	o.f(b->mRotation.y);
	o.v3(b->mRollAxis);
	o.f(b->mRollAngle);
	o.f(b->mRollSpeed);
	o.h(b->mReversed);
	o.i((s32)(b->mLiveFlag & 0x30000));
	return o.n;
}
void takeFruitsBoat(TLiveActor* a, const u8* in, int size, int, int*)
{
	TFruitsBoat* b = static_cast<TFruitsBoat*>(a);
	Get g(in, size);
	const JGeometry::TVec3<f32> pos = g.v3();
	const f32 yaw                   = g.f();
	const JGeometry::TVec3<f32> axis = g.v3();
	const f32 roll = g.f(), rollSpeed = g.f();
	const s16 reversed = g.h();
	const s32 flags    = g.i();
	if (!g.ok)
		return;
	b->mPosition   = pos;
	b->mRotation.y = yaw;
	b->mRollAxis   = axis;
	b->mRollAngle  = roll;
	b->mRollSpeed  = rollSpeed;
	b->mReversed   = reversed;
	b->mLiveFlag   = (b->mLiveFlag & ~0x30000u) | ((u32)flags & 0x30000u);
}

// Pinna Park's Ferris wheel: its turn, speed and calming down.
bool isFerrisWheel(TLiveActor* a) { return dynamic_cast<TFerrisWheel*>(a) != nullptr; }
int saveFerrisWheel(TLiveActor* a, u8* out)
{
	TFerrisWheel* w = static_cast<TFerrisWheel*>(a);
	Put o(out);
	o.f(frameOf(w->getMActor()));
	o.f(w->mAnmRate);
	o.h((s16)w->mState);
	o.i(w->mStateTimer);
	return o.n;
}
void takeFerrisWheel(TLiveActor* a, const u8* in, int size, int age, int*)
{
	TFerrisWheel* w = static_cast<TFerrisWheel*>(a);
	Get g(in, size);
	const f32 frame = g.f(), rate = g.f();
	const s16 state = g.h();
	const s32 timer = g.i();
	if (!g.ok)
		return;
	setFrameOf(w->getMActor(), frame + rate * age);
	w->mAnmRate    = rate;
	w->mState      = (u16)state;
	w->mStateTimer = timer;
}

// Pinna Park's merry-go-round: how far round it is.
bool isMerrygoround(TLiveActor* a) { return dynamic_cast<TMerrygoround*>(a) != nullptr; }
int saveTurner(TLiveActor* a, u8* out)
{
	Put o(out);
	o.f(a->mRotation.y);
	o.f(frameOf(a->getMActor()));
	return o.n;
}
void takeTurner(TLiveActor* a, const u8* in, int size, int, int*)
{
	Get g(in, size);
	const f32 yaw = g.f(), frame = g.f();
	if (!g.ok)
		return;
	a->mRotation.y = yaw;
	setFrameOf(a->getMActor(), frame);
}

// Pinna Park's coaster car going round its rail (scenery: riding one is
// each player's own).
bool isPinnaCoaster(TLiveActor* a) { return dynamic_cast<TPinnaCoaster*>(a) != nullptr; }
int savePinnaCoaster(TLiveActor* a, u8* out)
{
	TPinnaCoaster* c = static_cast<TPinnaCoaster*>(a);
	Put o(out);
	o.f(frameOf(c->mRail));
	o.f(frameOf(c->getMActor()));
	return o.n;
}
void takePinnaCoaster(TLiveActor* a, const u8* in, int size, int, int*)
{
	TPinnaCoaster* c = static_cast<TPinnaCoaster*>(a);
	Get g(in, size);
	const f32 rail = g.f(), car = g.f();
	if (!g.ok)
		return;
	setFrameOf(c->mRail, rail);
	setFrameOf(c->getMActor(), car);
}

// The casino's roulette (King Boo spins it): its turn and speed. Its
// sinking switch is left to each game (it holds the player still).
bool isRoulette(TLiveActor* a) { return dynamic_cast<TRoulette*>(a) != nullptr; }
int saveRoulette(TLiveActor* a, u8* out)
{
	TRoulette* r = static_cast<TRoulette*>(a);
	Put o(out);
	o.f(r->mRotation.y);
	o.f(r->unk13C);
	return o.n;
}
void takeRoulette(TLiveActor* a, const u8* in, int size, int, int*)
{
	TRoulette* r = static_cast<TRoulette*>(a);
	Get g(in, size);
	const f32 yaw = g.f(), speed = g.f();
	if (!g.ok)
		return;
	r->mRotation.y = yaw;
	r->unk13C      = speed;
}

// Slot machines: the reels as the player spraying them sees them. The big
// one's lights and win too; the small ones' draws (and the coins and Boos
// they pay out) happen in that player's game only, so the copies' reels
// are only turned, never left running.
int reelCount(TSirenaRollMapObj* s) { return s->unk164 > 0 && s->unk164 <= 3 ? s->unk164 : 3; }
bool isSlotDrum(TLiveActor* a)
{
	return dynamic_cast<TSlotDrum*>(a) != nullptr && dynamic_cast<TItemSlotDrum*>(a) == nullptr;
}
bool isItemSlotDrum(TLiveActor* a) { return dynamic_cast<TItemSlotDrum*>(a) != nullptr; }
int saveSlot(TLiveActor* a, u8* out)
{
	TSlotDrum* s = static_cast<TSlotDrum*>(a);
	Put o(out);
	const int n = reelCount(s);
	o.b((u8)n);
	for (int i = 0; i < n; i++)
		o.f(s->unk13C ? s->unk13C[i] : 0.0f);
	for (int i = 0; i < n; i++)
		o.f(s->unk138 ? s->unk138[i] : 0.0f);
	for (int i = 0; i < n; i++)
		o.raw(&s->unk170[i], sizeof s->unk170[i]);
	o.b(s->unk194 ? 1 : 0);
	return o.n;
}
void takeSlotWith(TLiveActor* a, const u8* in, int size, bool turnOnly)
{
	TSlotDrum* s = static_cast<TSlotDrum*>(a);
	Get g(in, size);
	const int n = g.b();
	if (!g.ok || n != reelCount(s) || !s->unk13C || !s->unk138)
		return;
	f32 angles[3], speeds[3];
	GXColorS10 lights[3];
	for (int i = 0; i < n; i++)
		angles[i] = g.f();
	for (int i = 0; i < n; i++)
		speeds[i] = g.f();
	for (int i = 0; i < n; i++)
		g.raw(&lights[i], sizeof lights[i]);
	const bool won = g.b() != 0;
	if (!g.ok)
		return;
	for (int i = 0; i < n; i++) {
		s->unk13C[i] = angles[i];
		s->unk138[i] = turnOnly ? 0.0f : speeds[i];
		s->unk170[i] = lights[i];
	}
	s->unk194 = won;
}
void takeSlotDrum(TLiveActor* a, const u8* in, int size, int, int*) { takeSlotWith(a, in, size, false); }
void takeItemSlotDrum(TLiveActor* a, const u8* in, int size, int, int*) { takeSlotWith(a, in, size, true); }

// Yoshi's egg: the fruit it wants (the host's draw), and whether it is still
// there (a player who hatched it rides Yoshi; the others' eggs go).
bool isEgg(TLiveActor* a) { return dynamic_cast<TEggYoshi*>(a) != nullptr; }
const u16 kEggHatching = 0xB, kEggHatched = 0xC;
// Hatching or hatched here (seen in this game's own run, not an egg still
// waiting to appear at a stage's start).
bool eggHatching(TEggYoshi* e) { return e->mState == kEggHatching || e->mState == kEggHatched; }
bool s_eggHatchedHere;
int saveEgg(TLiveActor* a, u8* out)
{
	TEggYoshi* e = static_cast<TEggYoshi*>(a);
	Put o(out);
	o.i((s32)e->unk14C);
	o.b(s_eggHatchedHere ? 1 : 0);
	return o.n;
}
void takeEgg(TLiveActor* a, const u8* in, int size, int, int*)
{
	TEggYoshi* e = static_cast<TEggYoshi*>(a);
	Get g(in, size);
	const u32 wants = (u32)g.i();
	const bool gone = g.b() != 0;
	if (!g.ok || s_eggHatchedHere || eggHatching(e) || e->checkLiveFlag(LIVE_FLAG_DEAD))
		return;
	if (gone) {
		e->makeObjDead(); // hatched in another game: that player has Yoshi
		return;
	}
	if (wants && wants != e->unk14C && e->mState == 1) {
		e->unk14C = wants;
		e->startBalloonAnim();
	}
}

// Gelato Beach's big watermelon: rolled by whoever bumped it last.
bool isWatermelon(TLiveActor* a) { return dynamic_cast<TBigWatermelon*>(a) != nullptr; }
int saveWatermelon(TLiveActor* a, u8* out)
{
	TBigWatermelon* w = static_cast<TBigWatermelon*>(a);
	Put o(out);
	o.v3(w->mPosition);
	o.v3(w->mVelocity);
	o.h((s16)w->mState);
	o.i(w->mStateTimer);
	o.f(w->mScaling.x);
	o.i(w->unk19C);
	return o.n;
}
void takeWatermelon(TLiveActor* a, const u8* in, int size, int age, int*)
{
	TBigWatermelon* w = static_cast<TBigWatermelon*>(a);
	Get g(in, size);
	const JGeometry::TVec3<f32> pos = g.v3(), vel = g.v3();
	const s16 state = g.h();
	const s32 timer = g.i();
	const f32 scale = g.f();
	const s32 coins = g.i();
	if (!g.ok || w->mHolder)
		return;
	w->mPosition.set(pos.x + vel.x * age, pos.y + vel.y * age, pos.z + vel.z * age);
	w->mVelocity   = vel;
	w->mState      = (u16)state;
	w->mStateTimer = timer;
	w->mScaling.set(scale, scale, scale);
	w->unk19C = coins;
}


// Bosses that follow the host's steps (as Shadow Mario does, mp_boss.cpp):
// each game's copy runs, animates and is hit as ever; it takes each new step
// of the host's (its nerve, by its place in the boss's own table), its hit
// points, and is eased toward where the host's is. Hits a player lands on a
// copy reach the host's as hits on shared objects (mp_game.cpp).
typedef const TNerveBase<TLiveActor>* Nerve;
typedef Nerve (*Steps)(int);

int stepOf(Steps steps, Nerve n)
{
	for (int i = 0; i < 64; i++) {
		Nerve t = steps(i);
		if (!t)
			break;
		if (t == n)
			return i;
	}
	return 0xFF;
}

int saveFollower(TLiveActor* a, u8* out, Steps steps)
{
	TSpineEnemy* e = static_cast<TSpineEnemy*>(a);
	Put o(out);
	o.b((u8)stepOf(steps, e->getLatestNerve()));
	o.v3(e->mPosition);
	o.f(e->mRotation.y);
	o.b((u8)e->getHitPoints());
	return o.n;
}

void takeFollower(TLiveActor* a, const u8* in, int size, int* last, Steps steps)
{
	TSpineEnemy* e = static_cast<TSpineEnemy*>(a);
	Get g(in, size);
	const int step                  = g.b();
	const JGeometry::TVec3<f32> pos = g.v3();
	const f32 yaw                   = g.f();
	const int hp                    = g.b();
	if (!g.ok || e->checkLiveFlag(LIVE_FLAG_DEAD))
		return;
	// *last: the host's step last taken, plus one (low byte); ticks this copy
	// has been on another step since (above it)
	if (step != 0xFF && steps(step)) {
		const bool newStep = step + 1 != (*last & 0xFF);
		int apart          = *last >> 8;
		apart              = stepOf(steps, e->getLatestNerve()) != step ? apart + 1 : 0;
		// a new step of the host's: taken (only then, so animations are not
		// restarted); a copy gone its own way for a second is put back on it
		if (newStep || apart > fps()) {
			if (stepOf(steps, e->getLatestNerve()) != step) {
				e->mSpine->reset();
				e->mSpine->setNext(steps(step));
			}
			apart = 0;
		}
		*last = (step + 1) | apart << 8;
	}
	easeTo(e->mPosition, pos, 700.0f, 0.2f);
	f32 d = yaw - e->mRotation.y;
	while (d > 180.0f)
		d -= 360.0f;
	while (d < -180.0f)
		d += 360.0f;
	e->mRotation.y += d * 0.2f;
	e->mHitPoints = (u8)hp;
}

Nerve chompStep(int i)
{
	switch (i) {
	case 0: return &TNerveBWGraphWander::theNerve();
	case 1: return &TNerveBWRoll::theNerve();
	case 2: return &TNerveBWBark::theNerve();
	case 3: return &TNerveBWJump::theNerve();
	case 4: return &TNerveBWStun::theNerve();
	case 5: return &TNerveBWWakeup::theNerve();
	case 6: return &TNerveBWJumpToBath::theNerve();
	case 7: return &TNerveBWDie::theNerve();
	case 8: return &TNerveBWJumpAway::theNerve();
	case 9: return &TNerveBWShake::theNerve();
	case 10: return &TNerveBWFall::theNerve();
	default: return nullptr;
	}
}
Nerve bowserStep(int i)
{
	switch (i) {
	case 0: return &TNerveKoopaWait::theNerve();
	case 1: return &TNerveKoopaTurnL::theNerve();
	case 2: return &TNerveKoopaTurnR::theNerve();
	case 3: return &TNerveKoopaTumble::theNerve();
	case 4: return &TNerveKoopaProvoke::theNerve();
	case 5: return &TNerveKoopaFlame::theNerve();
	case 6: return &TNerveKoopaGetDown::theNerve();
	case 7: return &TNerveKoopaStagger::theNerve();
	case 8: return &TNerveKoopaFall::theNerve();
	case 9: return &TNerveKoopaGetShowered::theNerve();
	default: return nullptr;
	}
}
Nerve juniorStep(int i)
{
	switch (i) {
	case 0: return &TNerveKoopaJrWait::theNerve();
	case 1: return &TNerveKoopaJrDamage::theNerve();
	case 2: return &TNerveKoopaJrDemo::theNerve();
	case 3: return &TNerveKoopaJrLaunch::theNerve();
	case 4: return &TNerveKoopaJrYahoo::theNerve();
	case 5: return &TNerveKoopaJrSubmarineWait::theNerve();
	case 6: return &TNerveKoopaJrSubmarineCannonOpenClose::theNerve();
	case 7: return &TNerveKoopaJrSubmarineLaunchKiller::theNerve();
	case 8: return &TNerveLimitKoopaJrWait::theNerve();
	case 9: return &TNerveLimitKoopaJrRun::theNerve();
	case 10: return &TNerveLimitKoopaJrLaunch::theNerve();
	case 11: return &TNerveLimitKoopaJrYahoo::theNerve();
	default: return nullptr;
	}
}
Nerve sleepyWigglerStep(int i)
{
	switch (i) {
	case 0: return &TNerveSBH_SleepContinue::theNerve();
	case 1: return &TNerveSBH_Fall::theNerve();
	default: return nullptr;
	}
}

// Chain Chomp's bath (Pianta Village): run by the player dragging him by his picket, else the host.
bool isChomp(TLiveActor* a) { return dynamic_cast<TBossWanwan*>(a) != nullptr; }
int saveChomp(TLiveActor* a, u8* out)
{
	TBossWanwan* c = static_cast<TBossWanwan*>(a);
	const int n    = saveFollower(a, out, chompStep);
	Put o(out + n);
	o.v3(c->mPullVelocity);
	o.f(c->mRollAngle);
	o.b((u8)c->mIsRolling);
	o.b((u8)c->mIsInBath);
	o.b((u8)c->mShineAppeared);
	return n + o.n;
}
void takeChomp(TLiveActor* a, const u8* in, int size, int, int* last)
{
	TBossWanwan* c = static_cast<TBossWanwan*>(a);
	const int n    = 1 + 12 + 4 + 1;
	takeFollower(a, in, size, last, chompStep);
	Get g(in + n, size - n);
	const JGeometry::TVec3<f32> pull = g.v3();
	const f32 roll                   = g.f();
	const u8 rolling = g.b(), inBath = g.b(), shine = g.b();
	if (!g.ok)
		return;
	c->mPullVelocity = pull;
	c->mRollAngle    = roll;
	c->mIsRolling    = rolling;
	c->mIsInBath     = (s8)inBath;
	if (shine)
		c->mShineAppeared = 1;
}
// Bowser in the final bath, Bowser Jr. (in it, and his sub and chases), Gelato's sleeping Wiggler: the host's.
bool isBowser(TLiveActor* a) { return dynamic_cast<TKoopa*>(a) != nullptr; }
int saveBowser(TLiveActor* a, u8* out) { return saveFollower(a, out, bowserStep); }
void takeBowser(TLiveActor* a, const u8* in, int size, int, int* last) { takeFollower(a, in, size, last, bowserStep); }
bool isJunior(TLiveActor* a)
{
	return dynamic_cast<TKoopaJr*>(a) || dynamic_cast<TKoopaJrSubmarine*>(a) || dynamic_cast<TLimitKoopaJr*>(a);
}
int saveJunior(TLiveActor* a, u8* out) { return saveFollower(a, out, juniorStep); }
void takeJunior(TLiveActor* a, const u8* in, int size, int, int* last) { takeFollower(a, in, size, last, juniorStep); }
bool isSleepyWiggler(TLiveActor* a) { return dynamic_cast<TSleepBossHanachan*>(a) != nullptr; }
int saveSleepyWiggler(TLiveActor* a, u8* out) { return saveFollower(a, out, sleepyWigglerStep); }
void takeSleepyWiggler(TLiveActor* a, const u8* in, int size, int, int* last)
{
	takeFollower(a, in, size, last, sleepyWigglerStep);
}

const Kind kKinds[] = {
	{"NPC", isNpc, saveNpc, takeNpc, OWN_HOST, 6, false},
	{"leaf boat", isLeafBoat, saveMover, takeMover, OWN_RIDER, 2, false},
	{"mud boat", isMuddyBoat, saveMuddyBoat, takeMuddyBoat, OWN_RIDER, 2, false},
	{"fruit boat", isFruitsBoat, saveFruitsBoat, takeFruitsBoat, OWN_RIDER, 2, false},
	{"Ferris wheel", isFerrisWheel, saveFerrisWheel, takeFerrisWheel, OWN_HOST, 15, false},
	{"merry-go-round", isMerrygoround, saveTurner, takeTurner, OWN_HOST, 6, false},
	{"coaster car", isPinnaCoaster, savePinnaCoaster, takePinnaCoaster, OWN_HOST, 6, false},
	{"roulette", isRoulette, saveRoulette, takeRoulette, OWN_HOST, 3, false},
	{"slot machine", isSlotDrum, saveSlot, takeSlotDrum, OWN_USER, 2, true},
	{"item slot", isItemSlotDrum, saveSlot, takeItemSlotDrum, OWN_USER, 2, true},
	{"Yoshi's egg", isEgg, saveEgg, takeEgg, OWN_USER, 10, false},
	{"Chain Chomp", isChomp, saveChomp, takeChomp, OWN_USER, 2, false},
	{"Bowser", isBowser, saveBowser, takeBowser, OWN_HOST, 2, false},
	{"Bowser Jr.", isJunior, saveJunior, takeJunior, OWN_HOST, 2, false},
	{"sleeping Wiggler", isSleepyWiggler, saveSleepyWiggler, takeSleepyWiggler, OWN_HOST, 3, false},
	{"watermelon", isWatermelon, saveWatermelon, takeWatermelon, OWN_USER, 2, false},
};
const int kKindCount = sizeof kKinds / sizeof kKinds[0];

// The sea's waves (their phases start at random): the host's.
const Kind kWaveKind = {"waves", nullptr, nullptr, nullptr, OWN_HOST, 30, false};

// ------------------------------------------------------------------ ownership

int fps() { return port_active_frame_rate > 0 ? port_active_frame_rate : 30; }

bool rides(TLiveActor* a)
{
	TMario* m = gpMarioOriginal;
	return m && m->mGroundPlane && m->mGroundPlane->getActor() == a && m->isTouchGround4cm();
}

bool ownedHere(Mirrored& m)
{
	if (m.actor) {
		if (m.kind->owner == OWN_RIDER && rides(m.actor))
			m.usedAt = s_clock;
		// a big melon this game's Mario is up against: bumped here
		if (m.kind == &kKinds[kKindCount - 1] && gpMarioOriginal) {
			const f32 dx = gpMarioOriginal->mPosition.x - m.actor->mPosition.x;
			const f32 dz = gpMarioOriginal->mPosition.z - m.actor->mPosition.z;
			const f32 r  = m.actor->mDamageRadius + 80.0f;
			if (dx * dx + dz * dz < r * r)
				m.usedAt = s_clock;
		}
	}
	// Chain Chomp dragged by this game's Mario (his picket in hand): ours
	if (m.actor && m.kind->match == isChomp && gpMarioOriginal
	    && gpMarioOriginal->mHeldObject == static_cast<TBossWanwan*>(m.actor)->mPicket)
		m.usedAt = s_clock;
		// an egg that hatched here: ours to say so (the others' go)
	if (m.actor && m.kind->match == isEgg) {
		if (eggHatching(static_cast<TEggYoshi*>(m.actor)))
			s_eggHatchedHere = true;
		if (s_eggHatchedHere)
			m.usedAt = s_clock;
	}
	// used here lately: ours (two seconds after; riding, while on it)
	if (m.kind->owner != OWN_HOST && s_clock - m.usedAt < 2 * fps())
		return true;
	if (mpg_role() != MPG_ROLE_HOST)
		return false;
	// the host runs it, unless a player is using it now
	return !(m.heardSlot != 0 && s_clock - m.heardAt < fps() / 2);
}

Mirrored* find(int manager, int index)
{
	for (int i = 0; i < s_count; i++)
		if (s_mirrors[i].manager == manager && s_mirrors[i].index == index)
			return &s_mirrors[i];
	return nullptr;
}

Mirrored* findActor(const void* a)
{
	for (int i = 0; i < s_count; i++)
		if (s_mirrors[i].actor == a)
			return &s_mirrors[i];
	return nullptr;
}

void add(TLiveActor* a, int manager, int index)
{
	if (!a || s_count >= kMaxMirrors || MpBossIsBoss(a))
		return;
	for (int k = 0; k < kKindCount; k++)
		if (kKinds[k].match(a)) {
			Mirrored& m = s_mirrors[s_count++];
			memset(&m, 0, sizeof m);
			m.actor   = a;
			m.manager = manager;
			m.index   = index;
			m.kind    = &kKinds[k];
			m.usedAt  = m.heardAt = -100000;
			return;
		}
}

int saveWaves(u8* out)
{
	Put o(out);
	o.f(gpMapObjWave->mWavePhaseX);
	o.f(gpMapObjWave->mWavePhaseZ);
	return o.n;
}

void takeWaves(const u8* in, int size)
{
	Get g(in, size);
	const f32 x = g.f(), z = g.f();
	if (g.ok && gpMapObjWave) {
		gpMapObjWave->mWavePhaseX = x;
		gpMapObjWave->mWavePhaseZ = z;
	}
}

} // namespace

void debugMirrors();

void MpMirrorCollect()
{
	s_count          = 0;
	s_eggHatchedHere = false;
	if (!gpConductor)
		return;
	int m = 0;
	for (JGadget::TList<TLiveManager*>::iterator it = gpConductor->unk10.begin(); it != gpConductor->unk10.end() && m < kWaves;
	     ++it, ++m) {
		TLiveManager* mgr = *it;
		for (int i = 0; mgr && i < mgr->getObjNum() && i < 0x10000; i++)
			add(mgr->getObj(i), m, i);
	}
	int i = 0;
	for (JGadget::TList<TLiveActor*>::iterator it = gpConductor->unk30.begin(); it != gpConductor->unk30.end() && i < 512;
	     ++it, ++i)
		add(*it, kAlone, i);
	if (gpMapObjWave && s_count < kMaxMirrors) {
		Mirrored& w = s_mirrors[s_count++];
		memset(&w, 0, sizeof w);
		w.manager = kWaves;
		w.kind    = &kWaveKind;
		w.usedAt = w.heardAt = -100000;
	}
	if (getenv("SMS_MULTIPLAYER_DEBUG")) {
		int counts[16] = {0};
		for (int k = 0; k < s_count; k++)
			if (s_mirrors[k].kind != &kWaveKind)
				counts[s_mirrors[k].kind - kKinds]++;
		for (int k = 0; k < kKindCount; k++)
			if (counts[k])
				fprintf(stderr, "[multiplayer] mirrored here: %d %s\n", counts[k], kKinds[k].name);
	}
}

void MpMirrorTick()
{
	s_clock++;
	// the others' states, kept: taken every tick (a mover between theirs moves on)
	int slot, manager, index, size;
	u8 data[48];
	while (mpg_next_mirror(&slot, &manager, &index, data, &size)) {
		Mirrored* m = find(manager, index);
		if (!m || size > 48)
			continue;
		m->heardAt   = s_clock;
		m->heardSlot = slot;
		memcpy(m->state, data, size);
		m->stateSize = size;
		m->stateAt   = s_clock;
	}
	for (int i = 0; i < s_count; i++) {
		Mirrored& m = s_mirrors[i];
		if (ownedHere(m)) {
			if (s_clock % m.kind->every)
				continue;
			u8 out[48];
			const int n = m.actor ? m.kind->save(m.actor, out) : (gpMapObjWave ? saveWaves(out) : 0);
			// unchanged: once a second, so a newcomer has it
			if (n <= 0 || (n == m.sentSize && !memcmp(out, m.sent, n) && s_clock - m.sentAt < fps()))
				continue;
			mpg_mirror(m.manager, m.index, out, n);
			memcpy(m.sent, out, n);
			m.sentSize = n;
			m.sentAt   = s_clock;
			continue;
		}
		if (!m.stateSize || s_clock - m.stateAt > 2 * fps())
			continue; // nothing lately: it runs as it would
		if (m.actor)
			m.kind->take(m.actor, m.state, m.stateSize, s_clock - m.stateAt, &m.scratch);
		else if (m.stateAt == s_clock)
			takeWaves(m.state, m.stateSize);
	}
	debugMirrors();
}

// Debug: the first of each kind, where it is here and who runs it.
void debugMirrors()
{
	if (!getenv("SMS_MULTIPLAYER_DEBUG") || s_clock % (10 * fps()))
		return;
	bool said[16] = {false};
	for (int i = 0; i < s_count; i++) {
		Mirrored& m = s_mirrors[i];
		if (!m.actor || said[m.kind - kKinds])
			continue;
		said[m.kind - kKinds] = true;
		fprintf(stderr, "[multiplayer] mirror: %s %d.%d at %.0f %.0f %.0f, %s, last heard %d ticks ago\n", m.kind->name,
		        m.manager, m.index, m.actor->mPosition.x, m.actor->mPosition.y, m.actor->mPosition.z,
		        ownedHere(m) ? "run here" : "taken", s_clock - m.heardAt);
	}
}

void MpMirrorUsed(const void* object)
{
	if (Mirrored* m = findActor(object))
		m->usedAt = s_clock;
}

bool MpMirrorKeepsHits(const void* object)
{
	Mirrored* m = findActor(object);
	return m && m->kind->keepsHits;
}
