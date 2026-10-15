// Bosses shared in multiplayer. Each boss is its own state machine, so each
// is taught here on its own: Petey Piranha, Gooper Blooper, Eely-Mouth,
// Phantamanta, Wiggler, King Boo, Mecha-Bowser, Bowser's hot tub, Shadow
// Mario's chases and the Gatekeepers (the goop piranha plants of the
// Airstrip, Bianco Hills, and the plaza's statue and its gates to Ricco
// Harbor and Gelato Beach). Il Piantissimo, a Shadow Mario of sorts, is left
// to each game: every player races their own.
//
// The host's boss is the real one. Its state rides at the end of the world
// snapshot as records, one per part ("unit": the boss, or one of
// Phantamanta's pieces), each [kind, unit, length, payload]; pieces take
// turns when there are many. A client's copy follows: it takes each new step
// of the boss's behaviour (nerve) when the host's does (only then, so its
// animations are not restarted every tick), and the values that make the
// fight (hit points, swallowed water, teeth, the weak segment...) whenever a
// record comes. Its own AI still runs between the host's steps: the boss
// aims at the player beside it in that game.
//
// What a client's player does to its copy is found by comparing the copy
// with what was last set from the host, reported (mpg_boss_hit: part, kind,
// count) and done to the real boss. A copy never goes down before the host's;
// when the host's does, every copy plays the defeat and brings out the
// boss's Shine, which anyone can get for everyone (mp_game.cpp's Shine Get).
#include "mp_boss.hpp"
#include "../mp_game_api.h"

#include <Enemy/BossEel.hpp>
#include <Enemy/BossGesso.hpp>
#include <Enemy/BossGessoTentacle.hpp>
#include <Enemy/BossHanachan.hpp>
#include <Enemy/BossManta.hpp>
#include <Enemy/BossPakkun.hpp>
#include <Enemy/BossTelesa.hpp>
#include <Enemy/BossTelesaObj.hpp>
#include <Enemy/Conductor.hpp>
#include <Enemy/Emario.hpp>
#include <Enemy/EnemyMario.hpp>
#include <Enemy/GateKeeper.hpp>
#include <Enemy/TinKoopa.hpp>
#include <JSystem/JDrama/JDRNameRefGen.hpp>
#include <MoveBG/MapObjCorona.hpp>
#include <Player/Mario.hpp>
#include <Player/ModelWaterManager.hpp>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

enum Kind {
	KIND_PETEY   = 1,
	KIND_GESSO   = 2, // Gooper Blooper
	KIND_EEL     = 3, // Eely-Mouth
	KIND_MANTA   = 4, // Phantamanta, every piece a unit
	KIND_WIGGLER = 5,
	KIND_KINGBOO = 6,
	KIND_MECHA   = 7, // Mecha-Bowser (each player rides their own coaster)
	KIND_TUB     = 8, // Bowser's hot tub: its five corners
	KIND_SHADOW  = 9, // Shadow Mario's chases
	KIND_GATE    = 10, // the Gatekeepers, each a unit
};

enum Hit {
	HIT_WATER = 1, // Petey: water in his mouth
	HIT_HIP_DROP,  // Petey: a ground pound on his belly
	HIT_EYE,       // Gooper Blooper: an eye sprayed
	HIT_BEAK,      // Gooper Blooper: the beak pulled off
	HIT_TENTACLE,  // Gooper Blooper: tentacle `unit` torn off
	HIT_TOOTH,     // Eely-Mouth: tooth `unit` cleaned by `count`
	HIT_MANTA,     // Phantamanta: piece `unit` sprayed
	HIT_WIGGLER,   // Wiggler: the weak segment ground-pounded
	HIT_FRUIT,     // King Boo: hit by fruit while spicy
	HIT_SPICY,     // King Boo: ate a pepper
	HIT_ROCKET,    // Mecha-Bowser: a rocket on his plates
	HIT_GRIP,      // the hot tub: corner `unit` broken by a super ground pound
	HIT_SHADOW_WATER, // Shadow Mario: sprayed
	HIT_SHADOW_STOMP, // Shadow Mario: stomped while down
	HIT_GATE_SPRAY,   // Gatekeeper `unit`: sprayed (asleep, it wakes after enough)
	HIT_GATE_MOUTH,   // Gatekeeper `unit`: water in its mouth, `count` hit points
};

typedef const TNerveBase<TLiveActor>* Nerve;
typedef Nerve (*NerveTable)(int);

const int kMaxUnits = 128;
const int kMantaPerUpdate = 6;

bool debug() { return getenv("SMS_MULTIPLAYER_DEBUG") != nullptr; }

// ------------------------------------------------------------------ bytes

struct Out {
	u8* p;
	int n, cap;
	Out(u8* buf, int size) : p(buf), n(0), cap(size) {}
	bool room(int k) const { return n + k <= cap; }
	void bytes(const void* d, int k)
	{
		if (!room(k))
			return;
		memcpy(p + n, d, k);
		n += k;
	}
	void u8_(u8 v) { bytes(&v, 1); }
	void s16_(s16 v) { bytes(&v, 2); }
	void f32_(f32 v) { bytes(&v, 4); } // the same build on both sides: the same layout
};

struct In {
	const u8* p;
	int n, at;
	bool ok;
	In(const u8* buf, int size) : p(buf), n(size), at(0), ok(true) {}
	void bytes(void* d, int k)
	{
		if (at + k > n) {
			ok = false;
			memset(d, 0, k);
			return;
		}
		memcpy(d, p + at, k);
		at += k;
	}
	u8 u8_()
	{
		u8 v;
		bytes(&v, 1);
		return v;
	}
	s16 s16_()
	{
		s16 v;
		bytes(&v, 2);
		return v;
	}
	f32 f32_()
	{
		f32 v;
		bytes(&v, 4);
		return v;
	}
};

s16 yawOf(f32 degrees) { return (s16)(degrees * (65536.0f / 360.0f)); }
f32 degreesOf(s16 yaw) { return yaw * (360.0f / 65536.0f); }

f32 easeAngle(f32 have, f32 want, f32 k)
{
	f32 d = want - have;
	while (d > 180.0f)
		d -= 360.0f;
	while (d < -180.0f)
		d += 360.0f;
	return have + d * k;
}

void writeWhere(Out& o, TLiveActor* a)
{
	o.f32_(a->mPosition.x);
	o.f32_(a->mPosition.y);
	o.f32_(a->mPosition.z);
	o.s16_(yawOf(a->mRotation.y));
}

// Where the host's is, eased (a step's own motion carries it between); at
// once when far (it jumped, or a lost update or two left it behind).
void readWhere(In& in, TLiveActor* a, f32 k = 0.25f)
{
	const f32 x = in.f32_(), y = in.f32_(), z = in.f32_();
	const s16 yaw = in.s16_();
	if (!in.ok)
		return;
	const f32 dx = x - a->mPosition.x, dy = y - a->mPosition.y, dz = z - a->mPosition.z;
	if (dx * dx + dy * dy + dz * dz > 800.0f * 800.0f)
		k = 1.0f;
	a->mPosition.x += (x - a->mPosition.x) * k;
	a->mPosition.y += (y - a->mPosition.y) * k;
	a->mPosition.z += (z - a->mPosition.z) * k;
	a->mRotation.y = easeAngle(a->mRotation.y, degreesOf(yaw), k);
}

// ------------------------------------------------------------------ nerves

int nerveIndex(NerveTable table, Nerve n)
{
	for (int i = 0; i < 64; i++) {
		Nerve t = table(i);
		if (!t)
			break;
		if (t == n)
			return i;
	}
	return 0xFF;
}

int s_hostNerve[kMaxUnits]; // the host's last step taken, per unit
int s_localNerve[kMaxUnits], s_localSince[kMaxUnits]; // the copy's step, and since when (tick)
int s_bossTick;
const int kOwnStepTicks = 12; // a step the copy took on its own (hit here) is let run this long

// A new step of the host's: take it. But not over a step this copy took on
// its own just now (hurt by a hit here, which the host has not had yet): the
// host's from before the hit would cut it short, and its own hurt step, once
// it comes, play it again. And a copy is not down (its `die` steps) before
// the host's is.
void followNerve(TLiveActor* a, int unit, int host, NerveTable table, int die1, int die2)
{
	if (host == 0xFF || !table(host))
		return;
	const int local = nerveIndex(table, a->getLatestNerve());
	if (local != s_localNerve[unit]) {
		s_localNerve[unit] = local;
		s_localSince[unit] = s_bossTick;
	}
	if (host != s_hostNerve[unit]) {
		if (local != host && local != s_hostNerve[unit] && s_hostNerve[unit] >= 0
		    && s_bossTick - s_localSince[unit] < kOwnStepTicks)
			return; // taken once it has run a little (the host's is kept until then)
		s_hostNerve[unit] = host;
		if (local != host) {
			a->mSpine->reset();
			a->mSpine->setNext(table(host));
		}
	} else if ((local == die1 || local == die2) && host != die1 && host != die2) {
		a->mSpine->reset();
		a->mSpine->setNext(table(host));
	}
}

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

Nerve gessoNerve(int i)
{
	switch (i) {
	case 0: return &TNerveBGWait::theNerve();
	case 1: return &TNerveBGEyeDamage::theNerve();
	case 2: return &TNerveBGBeakDamage::theNerve();
	case 3: return &TNerveBGTentacleDamage::theNerve();
	case 4: return &TNerveBGTug::theNerve();
	case 5: return &TNerveBGDie::theNerve();
	case 6: return &TNerveBGPollute::theNerve();
	case 7: return &TNerveBGPolDrop::theNerve();
	case 8: return &TNerveBGRoll::theNerve();
	default: return nullptr;
	}
}
const int kGessoEyeDamage = 1, kGessoDie = 5;

Nerve eelNerve(int i)
{
	switch (i) {
	case 0: return &TNerveBossEelWaitAppear::theNerve();
	case 1: return &TNerveBossEelFirstSpin::theNerve();
	case 2: return &TNerveBossEelSecondSpin::theNerve();
	case 3: return &TNerveBossEelAppear::theNerve();
	case 4: return &TNerveBossEelOutWait::theNerve();
	case 5: return &TNerveBossEelSlowBack::theNerve();
	case 6: return &TNerveBossEelQuickBack::theNerve();
	case 7: return &TNerveBossEelEat::theNerve();
	case 8: return &TNerveBossEelDie::theNerve();
	case 9: return &TNerveBossEelMouthOpenWait::theNerve();
	case 10: return &TNerveBossEelSleepOnBottom::theNerve();
	default: return nullptr;
	}
}
const int kEelDie = 8;

Nerve mantaNerve(int i)
{
	switch (i) {
	case 0: return &TNerveMantaMove::theNerve();
	case 1: return &TNerveMantaHitWater::theNerve();
	case 2: return &TNerveMantaSpawn::theNerve();
	case 3: return &TNerveMantaDeath::theNerve();
	case 4: return &TNerveMantaAppearDemo::theNerve();
	default: return nullptr;
	}
}
const int kMantaSpawn = 2, kMantaDeath = 3;

Nerve wigglerNerve(int i)
{
	switch (i) {
	case 0: return &TNerveBossHanachanGraphWander::theNerve();
	case 1: return &TNerveBossHanachanTumble::theNerve();
	case 2: return &TNerveBossHanachanDown::theNerve();
	case 3: return &TNerveBossHanachanGetUp::theNerve();
	case 4: return &TNerveBossHanachanDamage::theNerve();
	case 5: return &TNerveBossHanachanSnort::theNerve();
	case 6: return &TNerveBossHanachanDead::theNerve();
	default: return nullptr;
	}
}
const int kWigglerTumble = 1, kWigglerDown = 2, kWigglerDead = 6;

Nerve kingBooNerve(int i)
{
	switch (i) {
	case 0: return &TNerveBossTelesaAppear::theNerve();
	case 1: return &TNerveBossTelesaDie::theNerve();
	case 2: return &TNerveBossTelesaFallDemo::theNerve();
	case 3: return &TNerveBossTelesaFreeze::theNerve();
	case 4: return &TNerveBossTelesaHide::theNerve();
	case 5: return &TNerveBossTelesaHideWait::theNerve();
	case 6: return &TNerveBossTelesaPrepareSlot::theNerve();
	case 7: return &TNerveBossTelesaSlotStart::theNerve();
	case 8: return &TNerveBossTelesaSpit::theNerve();
	case 9: return &TNerveBossTelesaSpitSlotItem::theNerve();
	default: return nullptr;
	}
}
const int kKingBooDie = 1;

Nerve mechaNerve(int i)
{
	switch (i) {
	case 0: return &TNerveTinKoopaWait::theNerve();
	case 1: return &TNerveTinKoopaDamage::theNerve();
	case 2: return &TNerveTinKoopaBreak::theNerve();
	default: return nullptr;
	}
}

// ------------------------------------------------------------------ the stage's boss

int s_kind;
TSpineEnemy* s_boss;     // the boss (Phantamanta: none, see s_mantas)
TEnemyManager* s_mantas; // Phantamanta's pieces, by slot
TEnemyManager* s_gates;  // the Gatekeepers, by slot
TBathtub* s_tub;         // Bowser's hot tub
TEnemyMario* s_shadow;   // Shadow Mario
TEMarioManager* s_shadowList; // his hit actor's list: he is found through it once set up
TEMario* s_shadowAlone;       // or he is one on his own (the plaza's chase with Peach)
int s_next;              // the next piece to send
// client: what was last set from the host (to find this game's own hits)
int s_setHp;
int s_setValue[kMaxUnits];
u8 s_reported[kMaxUnits + 4];
bool s_followed;

// ------------------------------------------------------------------ the Gatekeepers

Nerve gateNerve(int i)
{
	switch (i) {
	case 0: return &TNerveBGKSleep::theNerve();
	case 1: return &TNerveBGKAppear::theNerve();
	case 2: return &TNerveBGKWait::theNerve();
	case 3: return &TNerveBGKWait2::theNerve();
	case 4: return &TNerveBGKSleepDamage::theNerve();
	case 5: return &TNerveBGKAwakeDamage::theNerve();
	case 6: return &TNerveBGKDie::theNerve();
	case 7: return &TNerveBGKDive::theNerve();
	case 8: return &TNerveBGKLaunchGoro::theNerve();
	case 9: return &TNerveBGKLaunchName::theNerve();
	default: return nullptr;
	}
}
const int kGateWait = 2, kGateWait2 = 3, kGateAwakeDamage = 5, kGateDie = 6;
const int kMaxGates = 8;
const int kGateRecord = 6;
int s_sprayWait[kMaxGates]; // client: frames until this game's spraying is reported again

TBiancoGateKeeper* gateAt(int unit)
{
	if (!s_gates || unit < 0 || unit >= s_gates->getObjNum() || unit >= kMaxGates)
		return nullptr;
	return dynamic_cast<TBiancoGateKeeper*>(s_gates->getObj(unit));
}

int clampByte(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

void writeGate(Out& o, int unit)
{
	TBiancoGateKeeper* g = gateAt(unit);
	o.u8_((u8)nerveIndex(gateNerve, g->getLatestNerve()));
	o.u8_(g->getHitPoints());
	o.u8_((u8)clampByte(g->unk17C)); // how awake: it comes up at 255
	o.u8_((u8)clampByte(g->unk28A)); // how many times it has come up
	o.u8_(g->checkLiveFlag(LIVE_FLAG_DEAD) ? 1 : 0);
	o.u8_((u8)g->unk296); // Ricco's and Pinna's come back once
}

void readGate(In& in, int unit)
{
	TBiancoGateKeeper* g = gateAt(unit);
	const int nerve = in.u8_(), hp = in.u8_(), wake = in.u8_(), appeared = in.u8_(), dead = in.u8_();
	const int revive = (s8)in.u8_();
	if (!in.ok || !g)
		return;
	if (g->checkLiveFlag(LIVE_FLAG_DEAD))
		return;
	const int local = nerveIndex(gateNerve, g->getLatestNerve());
	if (dead && local != kGateDie) { // gone at the host's before this game saw it go
		g->mStampEnabled = 0;
		g->kill();
		return;
	}
	if (s_followed) {
		// what this game's player did to its copy since the host's last say
		if (g->getHitPoints() < s_setValue[unit])
			mpg_boss_hit(unit, HIT_GATE_MOUTH, s_setValue[unit] - g->getHitPoints());
		// sprayed this tick (the move step marks it so for 16 ticks)
		if (s_sprayWait[unit] > 0)
			s_sprayWait[unit]--;
		if (g->unk17E == 0x0F && !s_sprayWait[unit]) {
			mpg_boss_hit(unit, HIT_GATE_SPRAY, 1);
			s_sprayWait[unit] = 8;
		}
	}
	followNerve(g, unit, nerve, gateNerve, kGateDie, kGateDie);
	// a copy never holds its last hit point: a hit here takes it to 1, not
	// to its defeat (the host's says when it falls)
	const int here = hp == 1 ? 2 : hp;
	g->mHitPoints = (u8)here;
	g->unk17C     = (s16)wake;
	g->unk28A     = (s16)appeared;
	g->unk296     = (s8)revive;
	s_setValue[unit] = here;
}

void hitGate(int unit, int kind, int count)
{
	TBiancoGateKeeper* g = gateAt(unit);
	if (!g || g->checkLiveFlag(LIVE_FLAG_DEAD))
		return;
	if (kind == HIT_GATE_SPRAY) {
		g->unk17E = 0x10; // as the game marks its own water on it
		return;
	}
	const int nerve = nerveIndex(gateNerve, g->getLatestNerve());
	if (kind != HIT_GATE_MOUTH || (nerve != kGateWait && nerve != kGateWait2) || !g->getHitPoints())
		return; // asleep, diving or already hurt: as for this game's own water
	g->decHitPoints();
	g->mSpine->reset();
	g->mSpine->setNext(gateNerve(g->getHitPoints() ? kGateAwakeDamage : kGateDie));
}

// ------------------------------------------------------------------ Petey Piranha

void writePetey(Out& o)
{
	TBossPakkun* b = static_cast<TBossPakkun*>(s_boss);
	o.u8_((u8)nerveIndex(peteyNerve, b->getLatestNerve()));
	o.u8_((u8)b->mState);
	o.u8_(b->getHitPoints());
	o.s16_((s16)b->mWaterMark);
	o.u8_(b->unk17C ? 1 : 0);
	o.s16_(yawOf(b->mHeadYaw));
	writeWhere(o, b);
}

void readPetey(In& in)
{
	TBossPakkun* b = static_cast<TBossPakkun*>(s_boss);
	const int nerve = in.u8_(), state = (s8)in.u8_(), hp = in.u8_();
	const int water = in.s16_(), drain = in.u8_();
	const s16 headYaw = in.s16_();
	if (!in.ok)
		return;
	if (s_followed) {
		if (b->getHitPoints() < s_setHp) // a ground pound on his belly, here
			mpg_boss_hit(0, HIT_HIP_DROP, s_setHp - b->getHitPoints());
		if (b->mWaterMark > s_setValue[0]) // water in his mouth, here
			mpg_boss_hit(0, HIT_WATER, b->mWaterMark - s_setValue[0]);
	}
	followNerve(b, 0, nerve, peteyNerve, 13, 14);
	b->mState     = (s8)state;
	b->mHitPoints = (u8)hp;
	b->mWaterMark = water;
	b->unk17C     = (s8)drain;
	b->mHeadYaw   = degreesOf(headYaw);
	readWhere(in, b);
	s_setHp       = hp;
	s_setValue[0] = water;
}

void hitPetey(int, int kind, int count)
{
	TBossPakkun* b = static_cast<TBossPakkun*>(s_boss);
	if (kind == HIT_WATER)
		for (int i = 0; i < count; i++)
			b->gotWaterDamage();
	else if (kind == HIT_HIP_DROP && b->mState == BOSSPAKU_STATE_BELLY_UP)
		b->gotHipDropDamage();
}

// ------------------------------------------------------------------ Gooper Blooper

void writeGesso(Out& o)
{
	TBossGesso* b = static_cast<TBossGesso*>(s_boss);
	o.u8_((u8)nerveIndex(gessoNerve, b->getLatestNerve()));
	o.u8_(b->getHitPoints());
	o.u8_((u8)b->mAttackMode);
	o.u8_(b->unk190.color.a); // the ink on him
	for (int i = 0; i < TBossGesso::TENTACLE_NUM; i++)
		o.u8_(b->mTentacles[i] ? (u8)b->mTentacles[i]->getState() : 0);
	writeWhere(o, b);
}

// a tentacle torn off (4) or growing back (6): the host's says
bool tornOrRegrowing(int state) { return state == 4 || state == 6; }

void readGesso(In& in)
{
	TBossGesso* b = static_cast<TBossGesso*>(s_boss);
	const int nerve = in.u8_(), hp = in.u8_(), mode = in.u8_(), ink = in.u8_();
	int tentacles[TBossGesso::TENTACLE_NUM];
	for (int i = 0; i < TBossGesso::TENTACLE_NUM; i++)
		tentacles[i] = in.u8_();
	if (!in.ok)
		return;
	const int local = nerveIndex(gessoNerve, b->getLatestNerve());
	if (s_followed) {
		if (b->getHitPoints() < s_setHp) // the beak pulled, here
			mpg_boss_hit(0, HIT_BEAK, s_setHp - b->getHitPoints());
		if (local == kGessoEyeDamage && nerve != kGessoEyeDamage && !s_reported[kMaxUnits]) {
			mpg_boss_hit(0, HIT_EYE, 1); // an eye sprayed, here
			s_reported[kMaxUnits] = 1;
		}
		for (int i = 0; i < TBossGesso::TENTACLE_NUM; i++) {
			TBGTentacle* t = b->mTentacles[i];
			if (t && t->getState() == 4 && tentacles[i] != 4 && !s_reported[i]) {
				mpg_boss_hit(i, HIT_TENTACLE, 1); // torn off, here
				s_reported[i] = 1;
			}
			if (tentacles[i] == 4)
				s_reported[i] = 0;
		}
	}
	if (local != kGessoEyeDamage)
		s_reported[kMaxUnits] = 0;
	followNerve(b, 0, nerve, gessoNerve, kGessoDie, kGessoDie);
	b->mHitPoints     = (u8)hp;
	b->mAttackMode    = mode;
	b->unk190.color.a = (u8)ink;
	// torn off and growing back as the host's; holding and pulling stay with
	// the player who does them, in their own game
	for (int i = 0; i < TBossGesso::TENTACLE_NUM; i++) {
		TBGTentacle* t = b->mTentacles[i];
		if (!t)
			continue;
		if (tornOrRegrowing(tentacles[i]) && t->getState() != tentacles[i])
			t->changeStateAndFixNodes(tentacles[i]);
		else if (!tornOrRegrowing(tentacles[i]) && tornOrRegrowing(t->getState()))
			t->changeStateAndFixNodes(0);
	}
	readWhere(in, b);
	s_setHp = hp;
}

void hitGesso(int unit, int kind, int count)
{
	TBossGesso* b = static_cast<TBossGesso*>(s_boss);
	if (kind == HIT_EYE)
		b->gotEyeDamage();
	else if (kind == HIT_BEAK)
		for (int i = 0; i < count && b->getHitPoints() > 0; i++)
			b->gotBeakDamage();
	else if (kind == HIT_TENTACLE && unit >= 0 && unit < TBossGesso::TENTACLE_NUM && b->mTentacles[unit]
	         && b->mTentacles[unit]->getState() != 4)
		b->mTentacles[unit]->changeStateAndFixNodes(4);
}

// ------------------------------------------------------------------ Eely-Mouth

void writeEel(Out& o)
{
	TBossEel* b = static_cast<TBossEel*>(s_boss);
	o.u8_((u8)nerveIndex(eelNerve, b->getLatestNerve()));
	o.u8_((b->mToothDamaged ? 1 : 0) | (b->mToothBroken ? 2 : 0));
	o.f32_(b->mAppearOffset);
	for (int i = 0; i < 8; i++)
		o.s16_(b->mTeeth[i] ? (s16)b->mTeeth[i]->mHitPoints : 0);
	writeWhere(o, b);
}

// A tooth's dirt shows its hit points (TBossEelTooth::receiveMessage).
void setToothHp(TBossEel* b, TBossEelTooth* t, int hp)
{
	const int max = b->getBossEelParams().mSLToothMaxHitPoint.get();
	t->mHitPoints = hp;
	t->mColor.a   = hp <= 1 ? 0 : (u8)((f32)(hp * 0xFF) / (f32)(max > 0 ? max : 1));
}

void readEel(In& in)
{
	TBossEel* b = static_cast<TBossEel*>(s_boss);
	const int nerve = in.u8_(), flags = in.u8_();
	const f32 appear = in.f32_();
	int teeth[8];
	for (int i = 0; i < 8; i++)
		teeth[i] = in.s16_();
	if (!in.ok)
		return;
	for (int i = 0; i < 8; i++) {
		TBossEelTooth* t = b->mTeeth[i];
		if (!t)
			continue;
		if (s_followed && t->mHitPoints < s_setValue[i]) // cleaned, here
			mpg_boss_hit(i, HIT_TOOTH, s_setValue[i] - t->mHitPoints);
		if (teeth[i] == 1 && t->mHitPoints > 1) {
			// clean in the host's game: it comes off here as it did there
			const bool broken = b->mToothBroken;
			b->mToothBroken   = false;
			for (int k = 0; k < 4096 && t->mHitPoints > 1; k++) {
				t->mDamageCooldown = 0;
				t->receiveMessage(gpMarioOriginal, HIT_MESSAGE_SPRAYED_BY_WATER);
			}
			b->mToothBroken = broken;
		} else if (t->mHitPoints != teeth[i]) {
			setToothHp(b, t, teeth[i]);
		}
		s_setValue[i] = teeth[i];
	}
	followNerve(b, 0, nerve, eelNerve, kEelDie, kEelDie);
	b->mToothDamaged = (flags & 1) != 0;
	b->mToothBroken  = (flags & 2) != 0;
	b->mAppearOffset = appear;
	readWhere(in, b);
}

void hitEel(int unit, int kind, int count)
{
	TBossEel* b = static_cast<TBossEel*>(s_boss);
	if (kind != HIT_TOOTH || unit < 0 || unit >= 8 || !b->mTeeth[unit])
		return;
	for (int i = 0; i < count; i++) {
		b->mTeeth[unit]->mDamageCooldown = 0;
		b->mTeeth[unit]->receiveMessage(gpMarioOriginal, HIT_MESSAGE_SPRAYED_BY_WATER);
	}
}

// ------------------------------------------------------------------ Phantamanta

// One piece (a slot of its manager): whether it is out, its generation (its
// size), its hits, its pause after a hit, its step, where it is and faces.
const int kMantaRecord = 5 + 12 + 12;

void writeManta(Out& o, int slot)
{
	TBossManta* m = static_cast<TBossManta*>(s_mantas->getObj(slot));
	o.u8_(m->checkLiveFlag(LIVE_FLAG_DEAD) ? 0 : 1);
	o.u8_((u8)m->mGeneration);
	o.u8_((u8)m->unk19C);
	o.u8_((u8)m->unk1A0);
	o.u8_((u8)nerveIndex(mantaNerve, m->getLatestNerve()));
	o.f32_(m->mPosition.x);
	o.f32_(m->mPosition.y);
	o.f32_(m->mPosition.z);
	o.f32_(m->unk170.x);
	o.f32_(m->unk170.y);
	o.f32_(m->unk170.z);
}

void readManta(In& in, int slot)
{
	if (!s_mantas || slot >= s_mantas->getObjNum() || slot >= kMaxUnits)
		return;
	TBossManta* m = static_cast<TBossManta*>(s_mantas->getObj(slot));
	const int alive = in.u8_(), gen = in.u8_(), hits = in.u8_(), pause = in.u8_(), nerve = in.u8_();
	const f32 x = in.f32_(), y = in.f32_(), z = in.f32_();
	const f32 fx = in.f32_(), fy = in.f32_(), fz = in.f32_();
	if (!in.ok || !m)
		return;
	const bool here = !m->checkLiveFlag(LIVE_FLAG_DEAD);
	if (here && alive && m->mGeneration == gen && s_reported[slot] && m->unk19C > s_setValue[slot])
		mpg_boss_hit(slot, HIT_MANTA, m->unk19C - s_setValue[slot]); // sprayed, here
	if (alive && (!here || m->mGeneration != gen)) {
		// out (or another size) in the host's game: here too, where it is there
		m->initNthGeneration(gen);
		m->mPosition.set(x, y, z);
		s_hostNerve[slot] = -1;
	} else if (!alive && here) {
		m->kill();
	}
	if (!alive) {
		s_reported[slot] = 0;
		return;
	}
	m->unk19C = hits;
	m->unk1A0 = pause;
	followNerve(m, slot, nerve, mantaNerve, kMantaSpawn, kMantaDeath);
	const f32 k = 0.3f;
	m->mPosition.x += (x - m->mPosition.x) * k;
	m->mPosition.y += (y - m->mPosition.y) * k;
	m->mPosition.z += (z - m->mPosition.z) * k;
	m->unk170.set(fx, fy, fz);
	s_setValue[slot] = hits;
	s_reported[slot] = 1; // followed once: its hits from now on are news
}

void hitManta(int unit, int kind, int count)
{
	if (kind != HIT_MANTA || !s_mantas || unit < 0 || unit >= s_mantas->getObjNum())
		return;
	TBossManta* m = static_cast<TBossManta*>(s_mantas->getObj(unit));
	for (int i = 0; m && i < count && !m->checkLiveFlag(LIVE_FLAG_DEAD); i++) {
		m->unk1A0 = 0; // its pause after a hit was the client's
		m->collidedWithWater();
	}
}

// ------------------------------------------------------------------ Wiggler

const u32 kWigglerFlags = 0x10000 | 0x20000 | 0x40000 | 0x100000;

void writeWiggler(Out& o)
{
	TBossHanachan* b = static_cast<TBossHanachan*>(s_boss);
	o.u8_((u8)nerveIndex(wigglerNerve, b->getLatestNerve()));
	o.u8_(b->getHitPoints());
	o.u8_((u8)b->mWeakBodyIndex);
	const u32 flags = b->mLiveFlag & kWigglerFlags;
	o.bytes(&flags, 4);
	o.s16_(yawOf(b->mRotation.x));
	o.s16_(yawOf(b->mRotation.z));
	writeWhere(o, b);
}

void readWiggler(In& in)
{
	TBossHanachan* b = static_cast<TBossHanachan*>(s_boss);
	const int nerve = in.u8_(), hp = in.u8_(), weak = in.u8_();
	u32 flags;
	in.bytes(&flags, 4);
	const s16 rx = in.s16_(), rz = in.s16_();
	if (!in.ok)
		return;
	if (s_followed && b->getHitPoints() < s_setHp) // its weak segment pounded, here
		mpg_boss_hit(0, HIT_WIGGLER, s_setHp - b->getHitPoints());
	followNerve(b, 0, nerve, wigglerNerve, kWigglerDead, kWigglerDead);
	b->mHitPoints     = (u8)hp;
	b->mWeakBodyIndex = weak; // the segment that can be hit is the host's
	b->mLiveFlag      = (b->mLiveFlag & ~kWigglerFlags) | (flags & kWigglerFlags);
	b->mRotation.x    = easeAngle(b->mRotation.x, degreesOf(rx), 0.25f);
	b->mRotation.z    = easeAngle(b->mRotation.z, degreesOf(rz), 0.25f);
	readWhere(in, b);
	s_setHp = hp;
}

void hitWiggler(int, int kind, int count)
{
	TBossHanachan* b = static_cast<TBossHanachan*>(s_boss);
	const int nerve  = nerveIndex(wigglerNerve, b->getLatestNerve());
	if (kind == HIT_WIGGLER && (nerve == kWigglerTumble || nerve == kWigglerDown))
		for (int i = 0; i < count && b->getHitPoints() > 0; i++)
			b->execDamage();
}

// ------------------------------------------------------------------ King Boo

void writeKingBoo(Out& o)
{
	TBossTelesa* b = static_cast<TBossTelesa*>(s_boss);
	o.u8_((u8)nerveIndex(kingBooNerve, b->getLatestNerve()));
	o.u8_(b->getHitPoints());
	o.u8_(b->unk350 ? 1 : 0);
	writeWhere(o, b);
}

void readKingBoo(In& in)
{
	TBossTelesa* b = static_cast<TBossTelesa*>(s_boss);
	const int nerve = in.u8_(), hp = in.u8_(), spicy = in.u8_();
	if (!in.ok)
		return;
	if (s_followed) {
		// fruit while spicy, here (the host's own hit plays Die here too, and
		// Die takes the hit point: that one is not news)
		if (b->getHitPoints() < s_setHp && nerve != kKingBooDie)
			mpg_boss_hit(0, HIT_FRUIT, s_setHp - b->getHitPoints());
		// a pepper here: spicy, where the host's last word was not
		if (b->unk350 && !s_setValue[0] && !spicy)
			mpg_boss_hit(0, HIT_SPICY, 1);
	}
	s_setValue[0] = spicy;
	followNerve(b, 0, nerve, kingBooNerve, kKingBooDie, kKingBooDie);
	b->mHitPoints = (u8)hp;
	b->unk350     = spicy != 0;
	readWhere(in, b);
	s_setHp = hp;
}

void hitKingBoo(int, int kind, int)
{
	TBossTelesa* b = static_cast<TBossTelesa*>(s_boss);
	if (kind == HIT_SPICY) {
		b->unk350 = true;
		b->unk36C = 0;
	} else if (kind == HIT_FRUIT) {
		b->unk350 = true; // it was spicy in the game it was hit in
		b->kill();        // TBossTelesa::kill: the hit; Die takes the hit point
	}
}

// ------------------------------------------------------------------ Mecha-Bowser

// His fight runs on each player's own coaster ride (his moves are timed off
// it), so only the damage is shared: how many plates are off and how many
// hits the current one has left.
void writeMecha(Out& o)
{
	TTinKoopa* b = static_cast<TTinKoopa*>(s_boss);
	o.u8_((u8)nerveIndex(mechaNerve, b->getLatestNerve()));
	o.u8_((u8)b->mDamageStage);
	o.u8_((u8)b->mPartsHitPoints);
}

void readMecha(In& in)
{
	TTinKoopa* b = static_cast<TTinKoopa*>(s_boss);
	const int nerve = in.u8_(), stage = in.u8_(), parts = in.u8_();
	if (!in.ok)
		return;
	// a rocket here: a hit off the current plate (a plate coming off, at the
	// end of the Break the host's game sends too, is not news)
	if (s_followed && b->mDamageStage == s_setValue[0] && b->mPartsHitPoints < s_setHp)
		mpg_boss_hit(0, HIT_ROCKET, s_setHp - b->mPartsHitPoints);
	followNerve(b, 0, nerve, mechaNerve, -1, -1);
	b->mDamageStage    = stage;
	b->mPartsHitPoints = parts;
	s_setValue[0]      = stage;
	s_setHp            = parts;
}

void hitMecha(int, int kind, int count)
{
	TTinKoopa* b = static_cast<TTinKoopa*>(s_boss);
	for (int i = 0; kind == HIT_ROCKET && i < count && b->mDamageStage < 4; i++)
		b->hitParts();
}

// ------------------------------------------------------------------ Bowser's hot tub

// Bowser falls when all five corners of his tub are broken by super ground
// pounds (TBathtubGrip::receiveMessage, message 3); the last starts the
// ending. A corner broken in any game breaks in every game.
bool gripDown(TBathtubGrip* g) { return g->unk248 || !g->unk249; }

void writeTub(Out& o)
{
	u8 down = 0;
	for (int i = 0; i < 5; i++)
		if (s_tub->unk168[i] && gripDown(s_tub->unk168[i]))
			down |= (u8)(1 << i);
	o.u8_(down);
}

void readTub(In& in)
{
	const int down = in.u8_();
	if (!in.ok)
		return;
	for (int i = 0; i < 5; i++) {
		TBathtubGrip* g = s_tub->unk168[i];
		if (!g)
			continue;
		const bool host = (down >> i) & 1, here = gripDown(g);
		if (here && !host && !s_reported[i]) {
			mpg_boss_hit(i, HIT_GRIP, 1); // broken here
			s_reported[i] = 1;
		} else if (host && !here) {
			s_reported[i] = 1;
			g->receiveMessage(gpMarioOriginal, 3); // broken in the host's game: here too
		}
	}
}

void hitTub(int unit, int kind, int)
{
	if (kind == HIT_GRIP && unit >= 0 && unit < 5 && s_tub->unk168[unit] && !gripDown(s_tub->unk168[unit]))
		s_tub->unk168[unit]->receiveMessage(gpMarioOriginal, 3);
}

// ------------------------------------------------------------------ Shadow Mario

// The host's Shadow Mario is the one everyone chases: where he is, what he is
// doing, his water meter and the stomps left once he is down.
const int kShadowRecord = 2 + 2 + 2 + 12 + 2;
int s_shadowWater, s_shadowStomps; // client: what was last set from the host's Shadow Mario
int s_shadowHostDoing = -1; // client: the host's step last taken
bool s_mainDownSaid;

void writeShadow(Out& o)
{
	TEnemyMario* e = s_shadow;
	o.bytes(&e->mEMDoing, 2);
	o.s16_(e->mWaterCounter);
	o.s16_(e->mTrampleCount);
	o.f32_(e->mPosition.x);
	o.f32_(e->mPosition.y);
	o.f32_(e->mPosition.z);
	o.s16_(e->mFaceAngle.y);
}

void readShadow(In& in)
{
	TEnemyMario* e = s_shadow;
	u16 doing;
	in.bytes(&doing, 2);
	const s16 water = in.s16_(), stomps = in.s16_();
	const f32 x = in.f32_(), y = in.f32_(), z = in.f32_();
	const s16 face = in.s16_();
	if (!in.ok)
		return;
	if (s_followed) {
		if (e->mWaterCounter < s_shadowWater) // sprayed here
			mpg_boss_hit(0, HIT_SHADOW_WATER, s_shadowWater - e->mWaterCounter);
		if (e->mTrampleCount < s_shadowStomps) // stomped here
			mpg_boss_hit(0, HIT_SHADOW_STOMP, s_shadowStomps - e->mTrampleCount);
	}
	// This game's Shadow Mario runs, animates and is hit as ever (so he
	// walks and sounds right, and water lands where it is seen to); the
	// host's says how much water he has left and when he goes down, and
	// gives each new step of his. He is kept near the host's: left alone
	// within a step or so, pulled harder the further off, at once when far.
	if (doing != s_shadowHostDoing) {
		s_shadowHostDoing = doing;
		if (doing != e->mEMDoing) {
			e->mEMDoing      = doing;
			e->mEMDoingTimer = 0;
		}
	}
	e->mWaterCounter = water;
	e->mTrampleCount = stomps;
	const f32 dx = x - e->mPosition.x, dy = y - e->mPosition.y, dz = z - e->mPosition.z;
	const f32 far = dx * dx + dy * dy + dz * dz;
	if (far > 700.0f * 700.0f) {
		e->mPosition.set(x, y, z);
		e->mFaceAngle.y = face;
	} else if (far > 30.0f * 30.0f) {
		const f32 k = far > 200.0f * 200.0f ? 0.15f : 0.06f;
		e->mPosition.x += dx * k;
		e->mPosition.y += dy * k;
		e->mPosition.z += dz * k;
	}
	s_shadowWater  = water;
	s_shadowStomps = stomps;
}

void hitShadow(int, int kind, int count)
{
	TEnemyMario* e = s_shadow;
	for (int i = 0; i < count; i++) {
		if (kind == HIT_SHADOW_WATER) {
			// a spray on him (its splash where he is, not at this game's Mario)
			TWaterHitActor& water = TModelWaterManager::mStaticHitActor;
			water.mPosition       = e->mPosition;
			water.mParticleIndex  = 0;
			e->hitWater(&water);
		}
		else if (kind == HIT_SHADOW_STOMP && e->mEMDoing == TEnemyMario::EM_DOING_DOWN_ANIMATION)
			e->thinkTrample();
	}
}

} // namespace

// ------------------------------------------------------------------ the hooks

void MpBossStageSetup()
{
	s_kind     = 0;
	s_boss     = nullptr;
	s_mantas   = nullptr;
	s_gates    = nullptr;
	memset(s_sprayWait, 0, sizeof s_sprayWait);
	s_tub      = nullptr;
	s_shadow   = nullptr;
	s_shadowList = nullptr;
	s_shadowAlone = nullptr;
	s_shadowWater = s_shadowStomps = 0;
	s_shadowHostDoing = -1;
	s_mainDownSaid = false;
	s_next     = 0;
	s_followed = false;
	s_setHp    = 0;
	memset(s_setValue, 0, sizeof s_setValue);
	memset(s_reported, 0, sizeof s_reported);
	for (int i = 0; i < kMaxUnits; i++)
		s_hostNerve[i] = s_localNerve[i] = -1;
	if (!gpConductor)
		return;
	// every boss here, not just the first found: a stage can have its boss
	// and Gatekeepers and Shadow Mario (the plaza has a sleeping Gatekeeper
	// beside Shadow Mario's chase)
	for (JGadget::TList<TEnemyManager*>::iterator it = gpConductor->unk20.begin(); it != gpConductor->unk20.end(); ++it) {
		TEnemyManager* mgr = *it;
		if (TEMarioManager* em = dynamic_cast<TEMarioManager*>(mgr)) {
			s_shadowList = em; // Shadow Mario himself comes later
			continue;
		}
		if (!mgr || !mgr->getObjNum())
			continue;
		TLiveActor* first = mgr->getObj(0);
		if (dynamic_cast<TBiancoGateKeeper*>(first)) {
			s_gates = mgr;
			continue;
		}
		if (s_kind)
			continue;
		if (TBossPakkunManager* pm = dynamic_cast<TBossPakkunManager*>(mgr)) {
			if (debug())
				fprintf(stderr, "[multiplayer] a Petey manager (%s)\n", pm->mIsLightVersion ? "light" : "full");
			if (!pm->mIsLightVersion && dynamic_cast<TBossPakkun*>(first)) // the light one does not fight
				s_kind = KIND_PETEY, s_boss = static_cast<TSpineEnemy*>(first);
		} else if (dynamic_cast<TBossGesso*>(first)) {
			s_kind = KIND_GESSO, s_boss = static_cast<TSpineEnemy*>(first);
		} else if (dynamic_cast<TBossEel*>(first)) {
			s_kind = KIND_EEL, s_boss = static_cast<TSpineEnemy*>(first);
		} else if (dynamic_cast<TBossManta*>(first)) {
			s_kind = KIND_MANTA, s_mantas = mgr;
		} else if (dynamic_cast<TBossHanachan*>(first)) {
			s_kind = KIND_WIGGLER, s_boss = static_cast<TSpineEnemy*>(first);
		} else if (dynamic_cast<TBossTelesa*>(first)) {
			s_kind = KIND_KINGBOO, s_boss = static_cast<TSpineEnemy*>(first);
		} else if (dynamic_cast<TTinKoopa*>(first)) {
			s_kind = KIND_MECHA, s_boss = static_cast<TSpineEnemy*>(first);
		}
	}
	// Shadow Mario's list may sit with the other object lists, not the enemies'
	if (!s_shadowList)
		for (JGadget::TList<TLiveManager*>::iterator it = gpConductor->unk10.begin(); it != gpConductor->unk10.end(); ++it)
			if (TEMarioManager* em = dynamic_cast<TEMarioManager*>(*it))
				s_shadowList = em;
	// or on his own (the conductor's lone actors: the plaza's Shadow Mario)
	if (!s_shadowList)
		for (JGadget::TList<TLiveActor*>::iterator it = gpConductor->unk30.begin(); it != gpConductor->unk30.end(); ++it)
			if (TEMario* em = dynamic_cast<TEMario*>(*it))
				// Il Piantissimo is one of these too: each player races their own
				if (em->mEnemyMario && em->mEnemyMario->mPlayerType != TMario::PLAYER_TYPE_MONTE_MAN)
					s_shadowAlone = em;
	if (!s_kind)
		if (TBathtub* tub = dynamic_cast<TBathtub*>(JDrama::TNameRefGen::search2("バスタブ")))
			if (tub->unk168)
				s_kind = KIND_TUB, s_tub = tub;
	static const char* const names[] = {"",        "Petey Piranha", "Gooper Blooper", "Eely-Mouth",  "Phantamanta",
	                                    "Wiggler", "King Boo",      "Mecha-Bowser",   "Bowser's tub", "Shadow Mario",
	                                    "The Gatekeeper"};
	if (s_kind)
		fprintf(stderr, "[multiplayer] %s is here: the host's is everyone's\n", names[s_kind]);
	if (s_gates)
		fprintf(stderr, "[multiplayer] %s is here: the host's is everyone's\n", names[KIND_GATE]);
	if (s_shadowList || s_shadowAlone)
		fprintf(stderr, "[multiplayer] %s is here: the host's is everyone's\n", names[KIND_SHADOW]);
	if (!s_kind && !s_gates && !s_shadowList && !s_shadowAlone && debug())
		for (JGadget::TList<TEnemyManager*>::iterator it = gpConductor->unk20.begin(); it != gpConductor->unk20.end(); ++it)
			if (*it && (*it)->getObjNum())
				fprintf(stderr, "[multiplayer] enemies here: %d of actor type %x\n", (*it)->getObjNum(),
				        (unsigned)(*it)->getObj(0)->getActorType());
}

// Shadow Mario is set up after the stage's objects: found when he is there.
bool bossReady()
{
	if (!s_shadow && s_shadowList)
		for (int i = 0; i < s_shadowList->getObjNum() && !s_shadow; i++)
			if (TEMario* em = dynamic_cast<TEMario*>(s_shadowList->getObj(i)))
				s_shadow = em->mEnemyMario;
	if (!s_shadow && s_shadowAlone)
		s_shadow = s_shadowAlone->mEnemyMario;
	return s_kind || s_gates || s_shadow;
}

// The record of the stage's main boss (one kind a stage), as [kind, unit, length, payload].
void writeMain(Out& o)
{
	if (s_kind == KIND_MANTA) {
		const int n = s_mantas->getObjNum() < kMaxUnits ? s_mantas->getObjNum() : kMaxUnits;
		for (int k = 0; k < kMantaPerUpdate && k < n && o.room(3 + kMantaRecord); k++) {
			const int slot = (s_next + k) % n;
			o.u8_(KIND_MANTA);
			o.u8_((u8)slot);
			o.u8_((u8)kMantaRecord);
			writeManta(o, slot);
		}
		if (n)
			s_next = (s_next + kMantaPerUpdate) % n;
		return;
	}
	o.u8_((u8)s_kind);
	o.u8_(0);
	const int lenAt = o.n;
	o.u8_(0);
	switch (s_kind) {
	case KIND_PETEY: writePetey(o); break;
	case KIND_GESSO: writeGesso(o); break;
	case KIND_EEL: writeEel(o); break;
	case KIND_WIGGLER: writeWiggler(o); break;
	case KIND_KINGBOO: writeKingBoo(o); break;
	case KIND_MECHA: writeMecha(o); break;
	case KIND_TUB: writeTub(o); break;
	}
	o.p[lenAt] = (u8)(o.n - lenAt - 1);
}

void MpBossHostFrame()
{
	if (!bossReady())
		return;
	int unit, kind, count;
	while (mpg_next_boss_hit(&unit, &kind, &count)) {
		if (debug())
			fprintf(stderr, "[multiplayer] boss: %d hit(s) of kind %d on part %d from another game\n", count, kind, unit);
		// the kind of hit says whose it is
		if (kind == HIT_GATE_SPRAY || kind == HIT_GATE_MOUTH) {
			hitGate(unit, kind, count);
			continue;
		}
		if (kind == HIT_SHADOW_WATER || kind == HIT_SHADOW_STOMP) {
			if (s_shadow)
				hitShadow(unit, kind, count);
			continue;
		}
		switch (s_kind) {
		case KIND_PETEY: hitPetey(unit, kind, count); break;
		case KIND_GESSO: hitGesso(unit, kind, count); break;
		case KIND_EEL: hitEel(unit, kind, count); break;
		case KIND_MANTA: hitManta(unit, kind, count); break;
		case KIND_WIGGLER: hitWiggler(unit, kind, count); break;
		case KIND_KINGBOO: hitKingBoo(unit, kind, count); break;
		case KIND_MECHA: hitMecha(unit, kind, count); break;
		case KIND_TUB: hitTub(unit, kind, count); break;
		}
	}
	u8 buf[200];
	Out o(buf, sizeof buf);
	if (s_kind)
		writeMain(o);
	if (s_shadow && o.room(3 + kShadowRecord)) {
		o.u8_(KIND_SHADOW);
		o.u8_(0);
		o.u8_((u8)kShadowRecord);
		writeShadow(o);
	}
	if (s_gates) {
		const int n = s_gates->getObjNum() < kMaxGates ? s_gates->getObjNum() : kMaxGates;
		for (int g = 0; g < n && o.room(3 + kGateRecord); g++) {
			if (!gateAt(g))
				continue;
			o.u8_(KIND_GATE);
			o.u8_((u8)g);
			o.u8_((u8)kGateRecord);
			writeGate(o, g);
		}
	}
	mpg_set_boss(buf, o.n);
	static int debugFrames;
	if (debug() && s_boss && ++debugFrames % 300 == 0)
		fprintf(stderr, "[multiplayer] boss (host): kind %d, %d hit points\n", s_kind, s_boss->getHitPoints());
}

void MpBossClientTick()
{
	if (!bossReady())
		return;
	s_bossTick++;
	u8 buf[200];
	float age     = 0.0f;
	const int got = (int)mpg_boss(buf, sizeof buf, &age);
	if (!got || age > 1.0f) {
		s_followed = false;
		return;
	}
	// the main boss down here too: it follows no more (the others still do)
	const bool mainDown = s_boss && s_boss->checkLiveFlag(LIVE_FLAG_DEAD);
	if (mainDown && s_followed && !s_mainDownSaid) {
		fprintf(stderr, "[multiplayer] the boss is down here too: its Shine is out\n");
		s_mainDownSaid = true;
	}
	In in(buf, got);
	while (in.ok && in.at + 3 <= got) {
		const int kind = in.u8_(), unit = in.u8_(), len = in.u8_();
		if (!in.ok || in.at + len > got)
			break;
		In rec(buf + in.at, len);
		in.at += len;
		if (kind == KIND_GATE) {
			if (s_gates)
				readGate(rec, unit);
			continue;
		}
		if (kind == KIND_SHADOW) {
			if (s_shadow)
				readShadow(rec);
			continue;
		}
		if (kind != s_kind || mainDown)
			continue;
		switch (kind) {
		case KIND_PETEY: readPetey(rec); break;
		case KIND_GESSO: readGesso(rec); break;
		case KIND_EEL: readEel(rec); break;
		case KIND_MANTA: readManta(rec, unit); break;
		case KIND_WIGGLER: readWiggler(rec); break;
		case KIND_KINGBOO: readKingBoo(rec); break;
		case KIND_MECHA: readMecha(rec); break;
		case KIND_TUB: readTub(rec); break;
		}
	}
	s_followed = true;
	static int debugTicks;
	if (debug() && s_boss && ++debugTicks % 300 == 0)
		fprintf(stderr, "[multiplayer] boss (here): kind %d, %d hit points\n", s_kind, s_boss->getHitPoints());
}

// Test aid (SMS_MULTIPLAYER_TEST bosshit@N): this game's player lands a hit on
// its copy, as playing would (the report then reaches the host's boss).
int MpBossTestHit()
{
	switch (s_kind ? s_kind : s_gates ? KIND_GATE : s_shadow ? KIND_SHADOW : 0) {
	case KIND_PETEY:
		for (int i = 0; i < 3; i++)
			static_cast<TBossPakkun*>(s_boss)->gotWaterDamage();
		return s_kind;
	case KIND_GESSO: static_cast<TBossGesso*>(s_boss)->gotBeakDamage(); return s_kind;
	case KIND_EEL: {
		TBossEel* b = static_cast<TBossEel*>(s_boss);
		for (int i = 0; i < 5 && b->mTeeth[0]; i++) {
			b->mTeeth[0]->mDamageCooldown = 0;
			b->mTeeth[0]->receiveMessage(gpMarioOriginal, HIT_MESSAGE_SPRAYED_BY_WATER);
		}
		return s_kind;
	}
	case KIND_MANTA:
		for (int i = 0; s_mantas && i < s_mantas->getObjNum(); i++) {
			TBossManta* m = static_cast<TBossManta*>(s_mantas->getObj(i));
			if (m && !m->checkLiveFlag(LIVE_FLAG_DEAD)) {
				m->unk1A0 = 0;
				m->collidedWithWater();
				return s_kind;
			}
		}
		return 0;
	case KIND_WIGGLER: static_cast<TBossHanachan*>(s_boss)->execDamage(); return s_kind;
	case KIND_KINGBOO:
		static_cast<TBossTelesa*>(s_boss)->unk350 = true;
		static_cast<TBossTelesa*>(s_boss)->unk36C = 0;
		return s_kind;
	case KIND_MECHA: static_cast<TTinKoopa*>(s_boss)->hitParts(); return s_kind;
	case KIND_TUB:
		for (int i = 0; i < 5; i++)
			if (s_tub->unk168[i] && !gripDown(s_tub->unk168[i])) {
				s_tub->unk168[i]->receiveMessage(gpMarioOriginal, 3);
				return s_kind;
			}
		return 0;
	case KIND_SHADOW: s_shadow->hitWater(gpMarioOriginal); return KIND_SHADOW;
	case KIND_GATE:
		// water on it asleep, or in its open mouth, as a player aims it: its
		// own step decides what that does
		for (int i = 0; i < kMaxGates; i++)
			if (TBiancoGateKeeper* g = gateAt(i))
				if (!g->checkLiveFlag(LIVE_FLAG_DEAD)) {
					const int nerve = nerveIndex(gateNerve, g->getLatestNerve());
					if (nerve == 0 || nerve == 4 || g->isHeadHitActive())
						g->unk154 = 1;
					return KIND_GATE;
				}
		return 0;
	}
	return 0;
}

// Test aid (shadowspray@N:K): a spray on this game's Shadow Mario.
int MpBossTestShadow()
{
	if (!bossReady() || !s_shadow)
		return 0;
	TWaterHitActor& water = TModelWaterManager::mStaticHitActor;
	water.mPosition       = s_shadow->mPosition;
	water.mParticleIndex  = 0;
	s_shadow->hitWater(&water);
	return KIND_SHADOW;
}

// Whether `actor` is one of the bosses shared here (their hits go through
// the boss's own reports, not as hits on shared objects).
bool MpBossIsBoss(const void* actor)
{
	const TLiveActor* a = static_cast<const TLiveActor*>(actor);
	if (!a)
		return false;
	TLiveActor* b = const_cast<TLiveActor*>(a);
	return dynamic_cast<TBossPakkun*>(b) || dynamic_cast<TBossGesso*>(b) || dynamic_cast<TBossEel*>(b)
	       || dynamic_cast<TBossManta*>(b) || dynamic_cast<TBossHanachan*>(b) || dynamic_cast<TBossTelesa*>(b)
	       || dynamic_cast<TTinKoopa*>(b) || dynamic_cast<TBiancoGateKeeper*>(b) || dynamic_cast<TEMario*>(b);
	// (Il Piantissimo is a TEMario too: left alone, not shared as an object)
}
