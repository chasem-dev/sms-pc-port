// The multiplayer extension's game half (ext/multiplayer/README.md): what the
// game does with the controller's decisions (mp_game_api.h). Game code, built
// with the game's flags: C++03, fixed arrays, nothing allocated but the
// stage's puppets.
//
// Each frame (port_ext frame hook):
//   1. tell the controller what the game is doing; it takes in the network;
//   2. share the save file's flags (mpg_flags_sync), and take away the Shines
//      and blue coins someone else collected;
//   3. take away the stage's items someone else picked up;
//   4. send our look (pose) for the others' puppets;
//   5. a client follows the host into their stage; the host leaves when a
//      client got a Shine.
#include "../mp_game_api.h"
#include "mp_boss.hpp"
#include "mp_mirror.hpp"
#include "mp_puppet.hpp"
#include "port_ext.h"
#include "port_framerate.h"

#include <Enemy/BossPakkun.hpp>
#include <Enemy/Conductor.hpp>
#include <Map/Map.hpp>
#include <Map/PollutionManager.hpp>
#include <Enemy/EnemyManager.hpp>
#include <Enemy/SmallEnemy.hpp>
#include <GC2D/ConsoleStr.hpp>
#include <GC2D/GCConsole2.hpp>
#include <GC2D/ScrnFader.hpp>
#include <MSound/MSoundBGM.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <M3DUtil/M3UModelMario.hpp>
#include <M3DUtil/MActor.hpp>
#include <Enemy/Koopa.hpp>
#include <Enemy/KoopaNerve.hpp>
#include <MoveBG/MapObjCorona.hpp>
#include <MoveBG/Item.hpp>
#include <JSystem/JDrama/JDRNameRefGen.hpp>
#include <MoveBG/ItemManager.hpp>
#include <NPC/NpcBase.hpp>
#include <MoveBG/MapObjManager.hpp>
#include <MoveBG/MapObjTown.hpp>
#include <Player/ModelWaterManager.hpp>
#include <Player/Mario.hpp>
#include <Player/NozzleBase.hpp>
#include <Player/MarioCap.hpp>
#include <Player/WaterGun.hpp>
#include <Player/Yoshi.hpp>
#include <Strategic/Strategy.hpp>
#include <System/Application.hpp>
#include <System/FlagManager.hpp>
#include <System/MarDirector.hpp>
#include <System/StageUtil.hpp>
#include <math.h>
#include <new>
#include <typeinfo>
#include <dolphin/pad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The window's text panels (platform/gx; absent in a build without a window).
extern "C" void GXPC_ExtOverlaySet(int panel, const char* text) __attribute__((weak));
enum { OVERLAY_NOTICE = 0, OVERLAY_STATUS = 1 };

namespace {

const int kMaxFlags = 1024;
const int kMaxItems = 2048;
const int kNoticeFrames = 150; // about five seconds at 30 frames a second

int s_role = -1; // MPG_ROLE_*, -1 before the first frame
bool s_playing, s_wasPlaying;
int32_t s_flags[kMaxFlags];
uint32_t s_flagIds[kMaxFlags];
int s_flagCount;
uint8_t s_itemAlive[kMaxItems]; // the stage's own items, alive when it started
int s_itemCount;
uint8_t s_itemLive[kMaxItems]; // every item, alive last frame
int s_takenAtSeen;             // the session's taken-at list, looked at so far
int s_lastCoins = -1;          // the coin count after the last sync (the 100-coin Shine)
TMpPuppet* s_puppets[4];
int s_clock; // frames since the game started

TMpPuppet* s_self; // our own character, when we are not Mario
unsigned s_stageNumber;
// Out of lives: watching another player instead of playing (until the
// session changes: the host moves on, or everyone is out and it restarts).
bool s_spectating;
u32 s_spectateSession;
int s_watching = -1;  // the slot whose player we watch
bool s_restarting;    // host: everyone is out, the stage is starting again
char s_notice[256];
int s_noticeFrames, s_statusFrames;
int s_refusedFrames;
// Client: we got a Shine and left for the plaza ahead of the host, who is
// still in the stage (saving): wait for the host there, not back in the stage.
int s_shineHoldFrames;
int s_shineHoldArea = -1, s_shineHoldEpisode = -1;
// The Shine Get together (syncShineGets)
bool s_inWinDemo;    // our player was in the Shine Get last frame
bool s_winRemote;    // ...because another player's Shine was played here
int s_shineGetFrames; // a Shine Get played here lately: the flag-only exit stays out of it

int s_titleFrames; // a client's frames on the title screen
TMarDirector* s_readyDir; // the director whose stage stageSetup saw set up

bool enabled() { return s_role > MPG_ROLE_OFF; }

TMarDirector* director()
{
	return gpApplication.mAppState == TApplication::APP_STATE_GAMEPLAY ? gpMarDirector : nullptr;
}

// In a stage (not the title's file select) whose objects are all set up,
// past its opening (the director's setup states): flags can be shared.
bool inStage()
{
	TMarDirector* dir = director();
	return dir && dir == s_readyDir && gpMarioOriginal && gpApplication.mCurrArea.getStage() != 15
	       && dir->mState >= TMarDirector::STATE_UNK4;
}

// Playing it: loaded, not leaving.
bool playing()
{
	TMarDirector* dir = director();
	return inStage() && dir->mState == TMarDirector::STATE_UNK4
	       && !dir->checkFlag(TMarDirector::DIRECTOR_FLAG_STAGE_TRANSITION_PENDING);
}

void overlay(int panel, const char* text)
{
	if (GXPC_ExtOverlaySet)
		GXPC_ExtOverlaySet(panel, text);
}

void showNotice(const char* text)
{
	snprintf(s_notice, sizeof s_notice, "%s", text);
	s_noticeFrames = kNoticeFrames;
	s_statusFrames = kNoticeFrames * 2;
}

// ------------------------------------------------------------------- items

TItemManager* items() { return gpItemManager; }

void snapshotItems()
{
	memset(s_itemAlive, 0, sizeof s_itemAlive);
	s_itemCount   = 0;
	s_takenAtSeen = 0;
	s_lastCoins   = -1;
	TItemManager* mgr = items();
	if (!mgr)
		return;
	s_itemCount = mgr->getObjNum() < kMaxItems ? mgr->getObjNum() : kMaxItems;
	for (int i = 0; i < s_itemCount; i++) {
		TLiveActor* a = mgr->getObj(i);
		s_itemAlive[i] = a && !a->checkLiveFlag(LIVE_FLAG_DEAD);
	}
	memcpy(s_itemLive, s_itemAlive, sizeof s_itemLive);
}

// Items that appear during the stage (from a box, a defeated enemy, a red
// coin switch) are matched by type and place: each game makes its own, a
// little apart where the enemy fell differently.
const f32 kTakenAtRadius = 60.0f;
void removeItem(TItem* item);

bool takenAtMatches(const MpgTakenAt& t, TLiveActor* a)
{
	if ((u8)(a->getActorType() & 0xFF) != t.type)
		return false;
	const f32 dx = a->mPosition.x - t.pos[0], dy = a->mPosition.y - t.pos[1], dz = a->mPosition.z - t.pos[2];
	return dx * dx + dy * dy + dz * dz <= kTakenAtRadius * kTakenAtRadius;
}

bool appearedDuringStage(int i) { return i >= s_itemCount || !s_itemAlive[i]; }

// Switches pressed in any game are pressed in every game (and for a player
// who joins later): a red coin switch, or a switch that throws out hidden
// coins. They go through the taken-at list as a type of their own; the
// coins they bring out are shared like any item that appears.
const u8 kSwitchPressed = 0xF0;
const int kMaxSwitches  = 32;
TMapObjBase* s_switches[kMaxSwitches];
bool s_switchDown[kMaxSwitches];
int s_switchCount;

void collectSwitches()
{
	s_switchCount = 0;
	if (!gpMapObjManager)
		return;
	for (int i = 0; i < gpMapObjManager->getObjNum() && s_switchCount < kMaxSwitches; i++) {
		TLiveActor* a = gpMapObjManager->getObj(i);
		if (!a || !(dynamic_cast<TRedCoinSwitch*>(a) || dynamic_cast<TMapObjSwitch*>(a)))
			continue;
		s_switches[s_switchCount]   = static_cast<TMapObjBase*>(a);
		s_switchDown[s_switchCount] = a->checkHitFlag(HIT_FLAG_NO_COLLISION) != 0;
		s_switchCount++;
	}
}

// Pressed in another game: as a ground pound would, but without taking this
// player's controls (the red coin switch holds them for its animation) or
// the camera (the coin switch shows where its coins go).
void pressSwitch(int k)
{
	TMapObjBase* sw  = s_switches[k];
	s_switchDown[k]  = true;
	if (TRedCoinSwitch* red = dynamic_cast<TRedCoinSwitch*>(sw)) {
		TMarioGamePad* pad = director() ? director()->getGamePad() : nullptr;
		const s32 held     = pad ? pad->mDisabledFrames : 0;
		red->receiveMessage(gpMarioOriginal, HIT_MESSAGE_HIP_DROP);
		if (pad)
			pad->mDisabledFrames = held;
	} else if (TMapObjSwitch* coins = dynamic_cast<TMapObjSwitch*>(sw)) {
		coins->startBck("objswitch");
		coins->removeMapCollision();
		for (int i = 0; i < coins->unk13C; ++i)
			coins->unk144[i]->action(coins->unk140);
		coins->startStateTimer(coins->unk140);
		coins->onHitFlag(HIT_FLAG_NO_COLLISION);
	}
}

// Debug: red coins out in this stage, not yet taken.
int redCoinsOut()
{
	int n = 0;
	for (int i = 0; gpItemManager && i < gpItemManager->getObjNum(); i++) {
		TLiveActor* a = gpItemManager->getObj(i);
		n += a && a->getActorType() == 0x2000000F && !a->checkLiveFlag(LIVE_FLAG_DEAD) && !a->checkLiveFlag(LIVE_FLAG_HIDDEN);
	}
	return n;
}

void syncSwitches()
{
	static bool redOn;
	static int redFrames;
	const bool on = TFlagManager::getInstance()->getBool(0x50009);
	if (on != redOn && getenv("SMS_MULTIPLAYER_DEBUG"))
		fprintf(stderr, "[multiplayer] red coin switch event %s\n", on ? "on (its timer runs)" : "off");
	redOn = on;
	if (on && getenv("SMS_MULTIPLAYER_DEBUG") && director() && ++redFrames % 150 == 0)
		fprintf(stderr, "[multiplayer] red coin event: %d red coins here, %d of them out (talk/demo %d, on ground %d, "
		        "jumping %d, Mario status %x)\n",
		        (int)TFlagManager::getInstance()->getFlag(0x60000), redCoinsOut(), (int)director()->isTalkOrDemoModeNow(),
		        (int)gpMarioOriginal->isTouchGround4cm(), (int)gpMarioOriginal->checkStatusType(MARIO_STATUS_FLAG_JUMPING),
		        (unsigned)gpMarioOriginal->getStatus());
	if (!on)
		redFrames = 0;
	for (int k = 0; k < s_switchCount; k++) {
		const bool down = s_switches[k]->checkHitFlag(HIT_FLAG_NO_COLLISION) != 0;
		if (down && !s_switchDown[k]) {
			const float pos[3] = {s_switches[k]->mPosition.x, s_switches[k]->mPosition.y, s_switches[k]->mPosition.z};
			mpg_item_taken_at(kSwitchPressed, pos);
		}
		s_switchDown[k] = down;
	}
}

// One from the taken-at list: the nearest switch not yet down, if close.
void pressSwitchAt(const MpgTakenAt& t)
{
	int best    = -1;
	f32 bestD   = 200.0f * 200.0f;
	for (int k = 0; k < s_switchCount; k++) {
		if (s_switchDown[k])
			continue;
		const JGeometry::TVec3<f32>& p = s_switches[k]->mPosition;
		const f32 d = (p.x - t.pos[0]) * (p.x - t.pos[0]) + (p.y - t.pos[1]) * (p.y - t.pos[1])
		              + (p.z - t.pos[2]) * (p.z - t.pos[2]);
		if (d < bestD)
			best = k, bestD = d;
	}
	if (best >= 0)
		pressSwitch(best);
}

// Every frame: items someone else took that this game has too go; and items
// that appear here after someone took theirs (their own red coin switch
// pressed later, say) go as they appear, so nobody gets one twice.
void syncTakenAt()
{
	TItemManager* mgr = items();
	if (!mgr)
		return;
	const int n = mgr->getObjNum() < kMaxItems ? mgr->getObjNum() : kMaxItems;
	syncSwitches();
	MpgTakenAt t;
	for (; mpg_taken_at(s_takenAtSeen, &t); s_takenAtSeen++) {
		if (t.type == kSwitchPressed) {
			pressSwitchAt(t);
			continue;
		}
		for (int i = 0; i < n; i++) {
			TLiveActor* a = mgr->getObj(i);
			if (a && appearedDuringStage(i) && !a->checkLiveFlag(LIVE_FLAG_DEAD) && takenAtMatches(t, a)) {
				if (getenv("SMS_MULTIPLAYER_DEBUG"))
					fprintf(stderr, "[multiplayer] item %d taken in another game: removed\n", i);
				removeItem(static_cast<TItem*>(a));
			}
		}
	}
	for (int i = 0; i < n; i++) {
		TLiveActor* a   = mgr->getObj(i);
		const bool live = a && !a->checkLiveFlag(LIVE_FLAG_DEAD);
		if (live && !s_itemLive[i] && appearedDuringStage(i))
			for (int k = 0; mpg_taken_at(k, &t); k++)
				if (takenAtMatches(t, a)) {
					if (getenv("SMS_MULTIPLAYER_DEBUG"))
						fprintf(stderr, "[multiplayer] item %d appeared, already taken: removed\n", i);
					removeItem(static_cast<TItem*>(a));
					break;
				}
		s_itemLive[i] = a && !a->checkLiveFlag(LIVE_FLAG_DEAD);
	}
}

uint32_t itemKey(int index, const TLiveActor* a) { return (uint32_t)index | ((uint32_t)(a->getActorType() & 0xFF) << 16); }

// Removes an item as TItem::taken does, without anyone getting it.
void removeItem(TItem* item)
{
	if (item->checkLiveFlag(LIVE_FLAG_DEAD))
		return;
	if (item->mContainer)
		item->mContainer->receiveMessage(item, HIT_MESSAGE_UNK8);
	item->kill();
	if (item->checkMapObjFlag(TMapObjBase::MAP_OBJ_FLAG_RESPAWNING)) {
		item->makeObjDefault();
		item->appear();
	}
}

void takeRemovals()
{
	uint32_t key;
	TItemManager* mgr = items();
	while (mpg_next_removal(&key)) {
		const int index = (int)(key & 0xFFFF);
		if (!mgr || index >= mgr->getObjNum())
			continue;
		TLiveActor* a = mgr->getObj(index);
		if (!a || (uint32_t)(a->getActorType() & 0xFF) != (key >> 16) || (a->getActorType() & 0xFFFFFF00) != 0x20000000)
			continue;
		removeItem(static_cast<TItem*>(a));
	}
}

// After the flags were shared: the Shines and blue coins someone else got
// leave this stage too.
void takeCollectedBy(const int32_t* before)
{
	TItemManager* mgr = items();
	if (!mgr)
		return;
	const u8 world = SMS_getShineStage(gpApplication.mCurrArea.getStage());
	for (int i = 0; i < mgr->getObjNum(); i++) {
		TLiveActor* a = mgr->getObj(i);
		if (!a || a->checkLiveFlag(LIVE_FLAG_DEAD))
			continue;
		uint32_t flag = 0;
		if (a->getActorType() == 0x20000013) { // Shine
			u32 id = static_cast<TMapObjBase*>(a)->getEventId() & 0xFF;
			flag   = 0x10000 + (id < 120 ? id : 0);
		} else if (a->getActorType() == 0x20000010) { // blue coin
			u32 id = static_cast<TMapObjBase*>(a)->getEventId();
			u8 w   = world == 0 || world >= 10 ? 1 : world;
			flag   = 0x10078 + (w - 1) * 50 + (id < 50 ? id : 0);
		} else {
			continue;
		}
		const int idx = (int)(flag - 0x10000); // card bools come first, by number
		if (idx >= 0 && idx < s_flagCount && !before[idx] && s_flags[idx])
			removeItem(static_cast<TItem*>(a));
	}
}

// ------------------------------------------------------------------- flags

void syncFlags()
{
	TFlagManager* fm = TFlagManager::getInstance();
	if (!fm)
		return;
	if (!s_flagCount) {
		s_flagCount = mpg_flag_count() < kMaxFlags ? mpg_flag_count() : kMaxFlags;
		for (int i = 0; i < s_flagCount; i++)
			s_flagIds[i] = mpg_flag_id(i);
	}
	static int32_t before[kMaxFlags];
	for (int i = 0; i < s_flagCount; i++)
		before[i] = s_flags[i] = fm->getFlag(s_flagIds[i]);
	const s32 coinsHere = fm->getFlag(0x40002); // with this frame's own pickups
	mpg_flags_sync(s_flags);
	bool counts = false, hud = false;
	for (int i = 0; i < s_flagCount; i++) {
		if (s_flags[i] == before[i])
			continue;
		fm->setFlag(s_flagIds[i], s_flags[i]);
		if (s_flagIds[i] < 0x20000)
			counts = true;
		else
			hud = true;
	}
	if (counts) {
		// the Shine and blue coin totals, as the game counts them
		int shines = 0, blue = 0;
		for (int i = 0; i < 0x78; i++)
			shines += fm->getFlag(0x10000 + i) != 0;
		for (int i = 0; i < 0x2EE; i++)
			blue += fm->getFlag(0x10078 + i) != 0;
		fm->setFlag(0x40000, shines);
		fm->setFlag(0x40001, blue);
		if (inStage())
			takeCollectedBy(before);
	}
	// The 100-coin Shine: the game brings it out when its own pickup makes
	// exactly 100 (TCoin::taken). The count is everyone's now, so when the
	// others' coins take it past 100, this game brings it out by its own
	// player as that pickup would have; whoever reaches it first gets it.
	const s32 coins = fm->getFlag(0x40002);
	if (inStage() && s_playing && !s_spectating && s_lastCoins >= 0 && s_lastCoins < 100 && coins >= 100
	    && coinsHere != 100 && gpItemManager && gpMarioOriginal
	    // a stage without one (the plaza): the game's own call would crash
	    && JDrama::TNameRefGen::search2("シャイン（１００枚コイン用）")) {
		const JGeometry::TVec3<f32>& at = gpMarioOriginal->mPosition;
		fprintf(stderr, "[multiplayer] the coins passed 100 (%d): the 100-coin Shine comes out\n", (int)coins);
		gpItemManager->makeShineAppearWithDemo("シャイン（１００枚コイン用）", "シャイン（１００枚コイン用）カメラ", at.x, at.y,
		                                       at.z);
	}
	if (inStage())
		s_lastCoins = coins;
	if (hud && inStage()) {
		// lives the HUD only shows going up (a lost life is drawn by the miss)
		if (TGCConsole2* console = director()->getConsole()) {
			int lives        = fm->getFlag(0x20001);
			console->unk3AC  = (u8)(lives > 99 ? 99 : lives < 0 ? 0 : lives);
		}
	}
}

// ------------------------------------------------------------------- pose

} // namespace

unsigned MpStageNumber() { return s_stageNumber; }

void MpCapturePose(TMario* m, MpgPose* out)
{
	MpgPose& p = *out;
	memset(&p, 0, sizeof p);
	p.session = mpg_session();
	if (!m || !m->getM3UModel())
		return;
	M3UModelMario* mm = m->getM3UModel();
	if (s_spectating) {
		p.flags = MPG_POSE_SPECTATING;
		return;
	}
	if (TMarDirector* dir = director())
		if (dir->isDemoModeNow())
			p.flags |= MPG_POSE_DEMO;
	if (!m->checkFlag(MARIO_FLAG_UNK4) && !m->checkFlag(MARIO_FLAG_IS_PERFORMING))
		p.flags |= MPG_POSE_VISIBLE;
	if (m->checkFlag(MARIO_FLAG_HAS_FLUDD))
		p.flags |= MPG_POSE_FLUDD;
	if (m->mWaterGun && m->checkFlag(MARIO_FLAG_HAS_FLUDD) && m->mWaterGun->isEmitting())
		p.flags |= MPG_POSE_SPRAYING;
	if (m->onYoshi())
		p.flags |= MPG_POSE_YOSHI;
	if (!m->mCap || m->mCap->unkC == m->mCap->unk10[0]) // the cap, not the hair under it
		p.flags |= MPG_POSE_CAP;
	if (mm->unk24[1].unk3 != 0xff)
		p.flags |= MPG_POSE_UPPER;
	memcpy(p.mtx, mm->getModel()->getBaseTRMtx(), sizeof p.mtx);
	p.bodyBck    = mm->unk24[0].unk4[0];
	p.bodyFrame  = mm->getFrameCtrl(0).getFrame();
	p.bodyBlend  = mm->unk20->unk18[0].mMotionBlendRatio;
	p.upperBck   = mm->unk24[1].unk4[0];
	p.upperFrame = mm->getFrameCtrl(1).getFrame();
	p.btp        = mm->unk1C ? mm->unk1C->unk0 : 0xff;
	p.btpFrame   = mm->getFrameCtrl(2).getFrame();
	// which hands: the body's own (shape 5 shown), or ma_hnd2, or ma_hnd3
	J3DModelData* body = mm->getModel()->getModelData();
	for (u16 i = 0; i < body->getShapeNum() && i < 32; i++)
		if (body->getShapeNodePointer(i)->checkFlag(J3DShpFlag_Visible))
			p.hiddenShapes |= 1u << i;
	if (!body->getShapeNodePointer(5)->checkFlag(J3DShpFlag_Visible))
		p.hand = 0;
	else if (m->mHandModels[0][0]
	         && !m->mHandModels[0][0]->getModelData()->getShapeNodePointer(0)->checkFlag(J3DShpFlag_Visible))
		p.hand = 1;
	else
		p.hand = 2;
	if (TWaterGun* gun = m->mWaterGun) {
		p.nozzle = gun->mCurrentNozzle;
		if (MActor* f = gun->mFluddModel) {
			p.fluddBck = (u16)f->getCurAnmIdx(ANM_TYPE_BCK);
			if (J3DFrameCtrl* fc = f->getFrameCtrl(ANM_TYPE_BCK))
				p.fluddFrame = fc->getFrame();
		}
	}
	if (TYoshi* y = m->mYoshi) {
		if (y->mActor) {
			p.yoshiBck = (u16)y->mActor->getCurAnmIdx(ANM_TYPE_BCK);
			if (J3DFrameCtrl* fc = y->mActor->getFrameCtrl(ANM_TYPE_BCK))
				p.yoshiFrame = fc->getFrame();
		}
		p.yoshiColor[0] = (u8)y->unk84.x;
		p.yoshiColor[1] = (u8)y->unk84.y;
		p.yoshiColor[2] = (u8)y->unk84.z;
	}
	// where the water leaves, while it does (the others' games spray it too)
	TWaterGun* gun = m->mWaterGun;
	if ((p.flags & MPG_POSE_SPRAYING) && gun && gun->mCurrentNozzle < 6 && gun->mNozzleList[gun->mCurrentNozzle]) {
		const int count = nozzleBmdData.getEmitterCount(gun->mCurrentNozzle);
		p.emitters      = (u8)(count < 2 ? count : 2);
		for (int i = 0; i < p.emitters; i++) {
			JGeometry::TVec3<f32> pos, dir, speed;
			gun->getEmitPosDirSpeed(i, &pos, &dir, &speed);
			p.emitPos[i][0] = pos.x, p.emitPos[i][1] = pos.y, p.emitPos[i][2] = pos.z;
			p.emitDir[i][0] = dir.x, p.emitDir[i][1] = dir.y, p.emitDir[i][2] = dir.z;
			p.emitSpeed[0] = speed.x, p.emitSpeed[1] = speed.y, p.emitSpeed[2] = speed.z;
		}
		p.pressure = gun->mNozzleList[gun->mCurrentNozzle]->unk378;
	}
}

// A drop of water drawn for another player (a bit of its flags the game leaves alone).
static const u16 kDrawnWater = 0x8000;

// Another player's water, emitted here as their FLUDD emits it
// (TNozzleBase::emit): the same nozzle's parameters, from where their game
// says it leaves. It is Mario's kind of water, so it cleans goop and hits
// what it hits in this game too. `carry` keeps the fraction of a particle
// between frames, as the nozzle does.
//
// Its drops carry kDrawnWater: they do not hit NPCs or map objects here
// (that player's own game sends the hits it lands, mp_game.cpp's
// objectHit), so nothing is hit twice.
void MpEmitWater(const MpgPose& p, f32* carry)
{
	TMario* m = gpMarioOriginal;
	if (!m || !m->mWaterGun || !gpModelWaterManager || p.nozzle >= 6 || !p.emitters)
		return;
	TWaterGun* gun     = m->mWaterGun;
	TNozzleBase* nozzle = gun->mNozzleList[p.nozzle];
	TWaterEmitInfo* info = gun->mEmitInfo;
	if (!nozzle || !info)
		return;
	const TNozzleBase::TEmitParams& e = nozzle->mEmitParams;
	*carry += e.mNum.get();
	const s32 num = (s32)*carry;
	if (num <= 0)
		return;
	*carry -= (f32)num;
	const s32 type = e.mType.get();
	if (type < 0 || type >= 17 || !gpModelWaterManager->mWaterParticleTypes[type])
		return;
	for (int i = 0; i < p.emitters; i++) {
		info->mAlive.set(gpModelWaterManager->mWaterParticleTypes[type]->mAlive.get());
		info->mPos.value.set(p.emitPos[i][0], p.emitPos[i][1], p.emitPos[i][2]);
		info->mDir.value.set(p.emitDir[i][0], p.emitDir[i][1], p.emitDir[i][2]);
		info->mV.value.set(p.emitSpeed[0], p.emitSpeed[1], p.emitSpeed[2]);
		info->mDirTremble  = e.mDirTremble;
		info->mPowTremble  = e.mPowTremble;
		info->mSize        = e.mSize;
		info->mSizeTremble = e.mSizeTremble;
		info->mType        = e.mType;
		info->mHitRadius   = e.mHitRadius;
		info->mHitHeight   = e.mHitHeight;
		info->mNum.set(num);
		info->mAttack = e.mAttack;
		const f32 pow  = e.mEmitPow.get();
		const f32 ctrl = e.mEmitCtrl.get();
		info->mPow.set(pow * p.pressure * ctrl + pow * (1.0f - ctrl));
		info->mFlag.set(0x40 | kDrawnWater);
		gpModelWaterManager->emitRequest(*info);
	}
}

namespace {

void sendPose()
{
	MpgPose p;
	MpCapturePose(gpMarioOriginal, &p);
	if (MpMirrorOnTub())
		p.more |= MPG_POSE2_ON_TUB;
	if (p.flags & MPG_POSE_VISIBLE)
		MpMeasureSmoothness(mpg_slot(), p);
	mpg_set_pose(&p);
}

// ------------------------------------------------------------------- warps

const char* worldName(int area)
{
	// SMS_getShineStage's worlds: Pianta Village (monte) is 7, Noki Bay (mare) 8
	static const char* const names[] = {"the Airstrip", "Delfino Plaza", "Bianco Hills", "Ricco Harbor",
	                                    "Gelato Beach", "Pinna Park",   "Sirena Beach", "Pianta Village",
	                                    "Noki Bay", "Corona Mountain"};
	u8 w = area >= 0 && area < 61 ? SMS_getShineStage((u8)area) : 1;
	return names[w < 10 ? w : 1];
}

// The host's travel (TMarDirector::moveStage, as it went): the session
// flags that shape how a stage starts (0x30000 on: this game came from an
// event, a movie was seen) and the movie played on the way. A client takes
// them, so it arrives as the host did (the same opening, the same movie).
const int kSessionFlags = 0x1D; // 0x30000 to 0x3001C
int s_travelWait; // frames this client has waited for the host's travel

void takeTravel(TMarDirector* dir)
{
	uint32_t flags;
	int movie;
	if (!mpg_travel(&flags, &movie))
		return;
	for (int i = 0; i < kSessionFlags; i++)
		TFlagManager::getInstance()->setBool((flags >> i) & 1, 0x30000 + i);
	if (movie >= 0 && dir) {
		gpApplication.setMovie((u32)movie);
		dir->onUnk4CFlag(TMarDirector::DIRECTOR_FLAG_MOVIE_PENDING);
	}
	mpg_travel_done();
}

void followHost(TMarDirector* dir)
{
	int area, episode;
	if (!mpg_flags_ready() || !mpg_target(&area, &episode) || mpg_in_target())
		return;
	if (s_shineHoldFrames > 0 && area == s_shineHoldArea && episode == s_shineHoldEpisode)
		return; // the host is still saving the Shine we got: they come to us
	s_shineHoldFrames = 0;
	// the host is on its way: how it goes decides how everyone arrives (a
	// few seconds at most; then this game goes as it would)
	uint32_t flags;
	int movie;
	const int fps = port_active_frame_rate > 0 ? port_active_frame_rate : 30;
	if (mpg_target_traveling() && !mpg_travel(&flags, &movie) && s_travelWait < 4 * fps) {
		s_travelWait++;
		return;
	}
	s_travelWait = 0;
	takeTravel(dir);
	// the host's flags are the game's now (synced this frame): load their stage
	if (episode != 0xFF) // 0xFF: the game picks the episode from the flags, as the host's does
		TFlagManager::getInstance()->setFlag(0x40003, episode);
	gpApplication.mNextArea.set((u8)area, (u8)episode, 0);
	dir->onFlag(TMarDirector::DIRECTOR_FLAG_STAGE_TRANSITION_PENDING);
	char text[128];
	snprintf(text, sizeof text, "Joining the host in %s", worldName(area));
	showNotice(text);
}

// A client needs no file of its own (it plays the host's): from the title
// screen it goes straight to the host's stage with the host's flags, once
// both have arrived.
void skipTitle(TMarDirector* dir)
{
	int area, episode;
	static int debugFrames;
	if (getenv("SMS_MULTIPLAYER_DEBUG") && ++debugFrames % 120 == 0)
		fprintf(stderr, "[multiplayer] title: app %d dir %p area %d state %d flags %d target %d\n",
		        (int)gpApplication.mAppState, (void*)dir, (int)gpApplication.mCurrArea.getStage(),
		        dir ? (int)dir->mState : -1, mpg_flags_ready(), mpg_target(&area, &episode));
	if (!dir || gpApplication.mCurrArea.getStage() != 15 || dir->mState != TMarDirector::STATE_UNK4
	    || dir->checkFlag(TMarDirector::DIRECTOR_FLAG_STAGE_TRANSITION_PENDING)) {
		s_titleFrames = 0;
		return;
	}
	// the title's own setup first (about a second)
	if (++s_titleFrames < 30 || !mpg_flags_ready() || !mpg_target(&area, &episode))
		return;
	syncFlags();
	TFlagManager::getInstance()->setFlag(0x40003, episode);
	dir->setNextStage((u16)(((area + 1) << 8) | episode), nullptr);
	char text[128];
	snprintf(text, sizeof text, "Joining the host in %s", worldName(area));
	showNotice(text);
	s_titleFrames = 0;
}

// A client got a Shine: the host leaves with everyone, as when its own Mario
// gets one (TMarDirector::updateGameMode): the "Shine Get!" banner, the
// fanfare and the fade, then the save prompt and the plaza. The Shine's flag
// is set already (it came with the client's report). There is no Shine
// camera here (that needs the game's own pickup, TMarDirector::fireGetStar),
// so the save prompt waits for the fade instead of the camera.
int s_hostShineFrames; // until the save prompt

void hostShineExit(TMarDirector* dir)
{
	if (s_hostShineFrames > 0) {
		if (--s_hostShineFrames == 0) {
			dir->onFlag(TMarDirector::DIRECTOR_FLAG_SHINE_TAKEN | TMarDirector::DIRECTOR_FLAG_CARD_SAVE_PENDING
			            | TMarDirector::DIRECTOR_FLAG_STAGE_TRANSITION_PENDING);
			dir->unk261 = 6;
			gpApplication.mNextArea.set(1, 0xFF, 0);
		}
		return;
	}
	if (!mpg_take_shine_exit())
		return;
	// the Shine Get played here already (everyone was taken to it): the
	// game's own sequence leads to the prompt
	if (s_shineGetFrames > 0 || dir->checkFlag(TMarDirector::DIRECTOR_FLAG_SHINE_TAKEN))
		return;
	if (TGCConsole2* console = dir->getConsole()) {
		if (console->unk94)
			console->unk94->startAppearShineGet();
		console->unk47 = 1;
	}
	MSBgm::startBGM(MSD_BGM_GET_SHINE);
	TFlagManager::getInstance()->setBool(true, 0x30006);
	if (dir->unkDC) {
		const f32 rate = dir->unkDC->getRate();
		dir->unkDC->registFadeout((u16)rate, (u16)(rate * 5.3333333f));
	}
	// the banner and the fade (about six seconds), then the prompt
	s_hostShineFrames = (int)(6.5f * (port_active_frame_rate > 0 ? port_active_frame_rate : 30));
}

// ------------------------------------------------------------------- Shine Get

// A Shine got by anyone is got by everyone together: when a player touches
// one, every other game moves its player to that Shine and has it touch them
// (TShine::touchPlayer), so each game plays the game's own Shine Get: the
// camera, the victory pose, the banner, the fanfare and the fade. Only the
// host saves (clientShineExit).

// The stage's Shines, wherever they are kept (any of the conductor's object
// lists, or among its loose actors), in turn: `index` from 0 until null.
TShine* shineAt(int index)
{
	if (!gpConductor)
		return nullptr;
	for (JGadget::TList<TLiveManager*>::iterator it = gpConductor->unk10.begin(); it != gpConductor->unk10.end(); ++it)
		for (int i = 0; *it && i < (*it)->getObjNum(); i++) {
			TLiveActor* a = (*it)->getObj(i);
			if (a && a->getActorType() == 0x20000013 && index-- == 0)
				return static_cast<TShine*>(a);
		}
	for (JGadget::TList<TLiveActor*>::iterator it = gpConductor->unk30.begin(); it != gpConductor->unk30.end(); ++it)
		if (*it && (*it)->getActorType() == 0x20000013 && index-- == 0)
			return static_cast<TShine*>(*it);
	return nullptr;
}

TShine* findShine(int eventId, const float pos[3])
{
	TShine* best = nullptr;
	f32 bestD    = 1e30f;
	for (int i = 0; shineAt(i); i++) {
		TLiveActor* a = shineAt(i);
		if (!a || a->getActorType() != 0x20000013 || (int)(static_cast<TMapObjBase*>(a)->getEventId() & 0xFF) != eventId)
			continue;
		const f32 dx = a->mPosition.x - pos[0], dy = a->mPosition.y - pos[1], dz = a->mPosition.z - pos[2];
		const f32 d = dx * dx + dy * dy + dz * dz;
		if (d < bestD)
			best = static_cast<TShine*>(a), bestD = d;
	}
	return best;
}

// Every frame in a stage (the Shine Get takes the game out of play at once,
// so this looks whether or not it is playing).
void noticeOwnShineGet()
{
	TMario* m = gpMarioOriginal;
	if (!m)
		return;
	if (s_shineGetFrames > 0)
		s_shineGetFrames--;
	const bool win = m->mStatus == MARIO_STATUS_WIN_DEMO;
	if (win && !s_inWinDemo) {
		s_shineGetFrames = 30 * (port_active_frame_rate > 0 ? port_active_frame_rate : 30);
		THitActor* shine = m->unk384;
		if (!s_winRemote && shine && shine->getActorType() == 0x20000013) {
			// we touched it: the others come to it
			const float pos[3] = {shine->mPosition.x, shine->mPosition.y, shine->mPosition.z};
			fprintf(stderr, "[multiplayer] we touched Shine %d: everyone comes to it\n",
			        (int)(static_cast<TMapObjBase*>(shine)->getEventId() & 0xFF));
			mpg_shine_get((int)(static_cast<TMapObjBase*>(shine)->getEventId() & 0xFF), pos);
		}
	}
	if (!win)
		s_winRemote = false;
	s_inWinDemo = win;
}

// While playing: another player's Shine Get comes here.
void playOthersShineGets()
{
	TMario* m = gpMarioOriginal;
	if (!m)
		return;
	const bool win = m->mStatus == MARIO_STATUS_WIN_DEMO;
	int slot, eventId;
	float pos[3];
	while (mpg_next_shine_get(&slot, &eventId, pos)) {
		if (win || s_spectating)
			continue; // in one already, or watching
		TShine* shine = findShine(eventId, pos);
		if (!shine) {
			fprintf(stderr, "[multiplayer] %s got Shine %d, which this game does not have\n", mpg_character_name(slot),
			        eventId);
			continue;
		}
		if (shine->checkLiveFlag(LIVE_FLAG_DEAD)) {
			// out only in their game so far (an event brought it there): here too
			shine->mPosition.set(pos[0], pos[1], pos[2]);
			shine->appear();
		}
		fprintf(stderr, "[multiplayer] %s got Shine %d: everyone to it\n", mpg_character_name(slot), eventId);
		char text[96];
		snprintf(text, sizeof text, "%s got a Shine!", mpg_character_name(slot));
		showNotice(text);
		// where they touched it, as the game puts a player who touches it
		m->mPosition.set(pos[0], pos[1], pos[2]);
		s_winRemote = true;
		shine->touchPlayer(m);
		break;
	}
}

// A client's own Shine: no save prompt (its game plays the host's save, which
// the host saves), and it waits in the plaza for the host.
void clientShineExit(TMarDirector* dir)
{
	if (!dir || !dir->checkFlag(TMarDirector::DIRECTOR_FLAG_SHINE_TAKEN))
		return;
	dir->offFlag(TMarDirector::DIRECTOR_FLAG_CARD_SAVE_PENDING);
	int area, episode;
	if (!s_shineHoldFrames && mpg_target(&area, &episode)) {
		s_shineHoldArea    = area;
		s_shineHoldEpisode = episode;
		s_shineHoldFrames  = 60 * (port_active_frame_rate > 0 ? port_active_frame_rate : 30);
		showNotice("Shine Get! The host saves it, then everyone meets in the plaza");
	}
}

// ------------------------------------------------------------------- enemies

// The host's enemies: clients put each of their small enemies (the stage's
// ordinary enemies; bosses keep their own) where the host's game has it, and
// an enemy defeated in any game is defeated in all. Enemies are named by
// their manager's place in the conductor's list and their index in it,
// which every game that loaded the stage shares.
const int kMaxManagers = 48;
const int kMaxEnemies  = 128; // per manager
TEnemyManager* s_managers[kMaxManagers];
int s_managerCount;
u8 s_enemyWasDown[kMaxManagers][kMaxEnemies]; // dying or dead last frame
u8 s_enemySyncKill[kMaxManagers][kMaxEnemies]; // we killed it on the host's word
u16 s_enemyDownFrames[kMaxManagers][kMaxEnemies];
bool s_enemiesSeen; // the first look at this stage's enemies is not news
u8 s_hostAlive[kMaxManagers][kMaxEnemies];  // as the host's last word had it (2: no word yet)
u16 s_enemyGrace[kMaxManagers][kMaxEnemies]; // frames since we brought it back

// The host's last word on each enemy: where it was, which way it faced, how
// fast it went, and when that came (this game's ticks). Every enemy is
// steered toward it every tick (each update carries only some of them),
// a little ahead by its speed, smoothly whatever the frame rate.
struct EnemyTarget {
	JGeometry::TVec3<f32> pos, vel;
	f32 yaw;
	int at;
	bool known;
};
EnemyTarget s_targets[kMaxManagers][kMaxEnemies];
f32 s_lastWorldAge = 99.0f;
const f32 kEnemySettle  = 0.1f;  // seconds to close most of a gap
const f32 kEnemyAhead   = 0.15f; // the furthest an enemy is led on by its speed
const f32 kEnemyStale   = 1.5f;  // seconds without word: left to itself

void collectManagers()
{
	s_managerCount = 0;
	memset(s_enemyWasDown, 0, sizeof s_enemyWasDown);
	memset(s_enemySyncKill, 0, sizeof s_enemySyncKill);
	memset(s_enemyDownFrames, 0, sizeof s_enemyDownFrames);
	memset(s_hostAlive, 2, sizeof s_hostAlive);
	memset(s_enemyGrace, 0xFF, sizeof s_enemyGrace);
	memset(s_targets, 0, sizeof s_targets);
	s_enemiesSeen = false;
	if (!gpConductor)
		return;
	for (JGadget::TList<TEnemyManager*>::iterator it = gpConductor->unk20.begin();
	     it != gpConductor->unk20.end() && s_managerCount < kMaxManagers; ++it)
		s_managers[s_managerCount++] = *it;
}

TSmallEnemy* enemyAt(int manager, int index)
{
	if (manager < 0 || manager >= s_managerCount || index < 0 || index >= kMaxEnemies)
		return nullptr;
	TEnemyManager* mgr = s_managers[manager];
	if (!mgr || index >= mgr->getObjNum())
		return nullptr;
	return dynamic_cast<TSmallEnemy*>(mgr->getObj(index));
}

// Defeated, or on the way (its death animation playing).
bool enemyDown(TSmallEnemy* e)
{
	return e->checkLiveFlag(LIVE_FLAG_DEAD) || e->mSpine->getCurrentNerve() == &TNerveSmallEnemyDie::theNerve();
}

s16 yaw(f32 degrees) { return (s16)(degrees * (65536.0f / 360.0f)); }

// Host, once a frame: kills from the clients' games, then every enemy as it is.
void hostEnemies()
{
	int manager, index;
	while (mpg_next_enemy_kill(&manager, &index))
		if (TSmallEnemy* e = enemyAt(manager, index))
			if (!enemyDown(e)) {
				fprintf(stderr, "[multiplayer] enemy %d.%d defeated in a client's game\n", manager, index);
				e->kill();
			}
	static MpgEnemy list[kMaxManagers * 16];
	int n = 0;
	for (int m = 0; m < s_managerCount; m++)
		for (int i = 0; i < s_managers[m]->getObjNum() && i < kMaxEnemies && n < (int)(sizeof list / sizeof *list); i++) {
			TSmallEnemy* e = enemyAt(m, i);
			if (!e)
				continue;
			MpgEnemy& out = list[n++];
			out.manager   = (u8)m;
			out.index     = (u16)i;
			out.alive     = !enemyDown(e);
			out.pos[0]    = e->mPosition.x;
			out.pos[1]    = e->mPosition.y;
			out.pos[2]    = e->mPosition.z;
			out.rotY      = yaw(e->mRotation.y);
			out.pad       = 0;
		}
	mpg_set_enemies(list, n);
	static int debugFrames;
	if (getenv("SMS_MULTIPLAYER_DEBUG") && ++debugFrames % 150 == 0)
		for (int k = 0; k < n && k < 4; k++)
			fprintf(stderr, "[multiplayer] enemy %d.%d %s at %.0f %.0f %.0f\n", list[k].manager, list[k].index,
			        list[k].alive ? "up" : "down", list[k].pos[0], list[k].pos[1], list[k].pos[2]);
}

// Client, once a frame: our own defeats go to the host.
void reportEnemyDefeats()
{
	for (int m = 0; m < s_managerCount; m++)
		for (int i = 0; i < s_managers[m]->getObjNum() && i < kMaxEnemies; i++) {
			TSmallEnemy* e = enemyAt(m, i);
			if (!e)
				continue;
			const bool down = enemyDown(e);
			// not one we just brought back (it can fall while it settles)
			if (s_enemyGrace[m][i] < 0xFFFF)
				s_enemyGrace[m][i]++;
			if (down && !s_enemyWasDown[m][i] && !s_enemySyncKill[m][i] && s_enemiesSeen && s_enemyGrace[m][i] > 180)
				mpg_enemy_killed(m, i);
			if (!down)
				s_enemySyncKill[m][i] = 0;
			s_enemyDownFrames[m][i] = down ? (u16)(s_enemyDownFrames[m][i] < 60000 ? s_enemyDownFrames[m][i] + 1 : 60000) : 0;
			s_enemyWasDown[m][i] = down;
		}
	s_enemiesSeen = true;
	static int debugFrames;
	if (getenv("SMS_MULTIPLAYER_DEBUG") && ++debugFrames % 150 == 0) {
		int shown = 0;
		for (int m = 0; m < s_managerCount && shown < 4; m++)
			for (int i = 0; i < s_managers[m]->getObjNum() && i < kMaxEnemies && shown < 4; i++)
				if (TSmallEnemy* e = enemyAt(m, i)) {
					fprintf(stderr, "[multiplayer] enemy %d.%d %s at %.0f %.0f %.0f (here)\n", m, i,
					        enemyDown(e) ? "down" : "up", e->mPosition.x, e->mPosition.y, e->mPosition.z);
					shown++;
				}
	}
}

// Client, every tick after the movement pass: the host's enemies where the
// host has them (eased in), defeated or back as the host's are.
f32 shortestTurn(f32 from, f32 to)
{
	f32 d = to - from;
	while (d > 180.0f)
		d -= 360.0f;
	while (d < -180.0f)
		d += 360.0f;
	return d;
}

void steerEnemies()
{
	const int fps  = port_active_frame_rate > 0 ? port_active_frame_rate : 30;
	const f32 tick = 1.0f / fps;
	const f32 k    = 1.0f - expf(-tick / kEnemySettle);
	int followed = 0;
	f32 off = 0.0f, worst = 0.0f;
	for (int m = 0; m < s_managerCount; m++)
		for (int i = 0; i < kMaxEnemies; i++) {
			EnemyTarget& t = s_targets[m][i];
			if (!t.known)
				continue;
			const f32 since = (s_clock - t.at) * tick;
			if (since > kEnemyStale)
				continue;
			TSmallEnemy* e = enemyAt(m, i);
			if (!e || enemyDown(e))
				continue;
			const f32 lead = since < kEnemyAhead ? since : kEnemyAhead;
			const JGeometry::TVec3<f32> want(t.pos.x + t.vel.x * lead, t.pos.y + t.vel.y * lead, t.pos.z + t.vel.z * lead);
			const f32 dx = want.x - e->mPosition.x, dy = want.y - e->mPosition.y, dz = want.z - e->mPosition.z;
			if (dx * dx + dy * dy + dz * dz > 500.0f * 500.0f) {
				e->mPosition = want;
				e->mRotation.y = t.yaw;
				continue;
			}
			e->mPosition.x += dx * k;
			e->mPosition.y += dy * k;
			e->mPosition.z += dz * k;
			e->mRotation.y += shortestTurn(e->mRotation.y, t.yaw) * k;
			const f32 d = sqrtf(dx * dx + dy * dy + dz * dz);
			followed++;
			off += d;
			worst = d > worst ? d : worst;
		}
	if (getenv("SMS_MULTIPLAYER_DEBUG") && s_clock % (10 * fps) == 0 && followed)
		fprintf(stderr, "[multiplayer] enemies: %d followed, %.0f units off on average, %.0f at most\n", followed,
		        off / followed, worst);
}

void followHostEnemies()
{
	static MpgEnemy list[64];
	f32 age        = 0.0f;
	const int n    = mpg_enemies(list, 64, &age);
	const bool fresh = n > 0 && age + 0.001f < s_lastWorldAge;
	s_lastWorldAge   = age;
	// nothing from the host for a second (its game is paused, or the link
	// is down): enemies here go by themselves until it comes again
	static bool onOwn;
	if ((age > 1.0f) != onOwn) {
		onOwn = age > 1.0f;
		if (getenv("SMS_MULTIPLAYER_DEBUG"))
			fprintf(stderr, "[multiplayer] enemies: %s\n",
			        onOwn ? "nothing from the host lately, they go by themselves here" : "following the host's again");
	}
	if (age > 1.0f) {
		steerEnemies();
		return;
	}
	for (int k = 0; fresh && k < n; k++) {
		const MpgEnemy& h = list[k];
		TSmallEnemy* e    = enemyAt(h.manager, h.index);
		if (!e)
			continue;
		const bool down = enemyDown(e);
		const u8 hostWas = s_hostAlive[h.manager][h.index];
		s_hostAlive[h.manager][h.index] = h.alive;
		if (!h.alive) {
			if (!down) {
				s_enemySyncKill[h.manager][h.index] = 1;
				if (hostWas == 1) {
					// defeated in the host's game just now: defeated here
					if (getenv("SMS_MULTIPLAYER_DEBUG"))
						fprintf(stderr, "[multiplayer] enemy %d.%d defeated in the host's game\n", h.manager, h.index);
					e->kill();
				} else {
					// one this game brought out by itself (its own spawner, near
					// this player) that the host's game has not: gone, quietly
					e->TLiveActor::kill();
					e->onLiveFlag(LIVE_FLAG_HIDDEN);
				}
			}
			continue;
		}
		const JGeometry::TVec3<f32> at(h.pos[0], h.pos[1], h.pos[2]);
		if (down) {
			// back on the host: back here, once ours has been gone a while
			// (not one we defeated a moment ago, whose news is on its way)
			if (e->checkLiveFlag(LIVE_FLAG_DEAD) && s_enemyDownFrames[h.manager][h.index] > 90) {
				const JGeometry::TVec3<f32> rot(0.0f, h.rotY * (360.0f / 65536.0f), 0.0f);
				const JGeometry::TVec3<f32> still(0.0f, 0.0f, 0.0f);
				if (getenv("SMS_MULTIPLAYER_DEBUG"))
					fprintf(stderr, "[multiplayer] enemy %d.%d back, as in the host's game\n", h.manager, h.index);
				e->resetSRTV(at, rot, e->mScaling, still);
				s_enemyGrace[h.manager][h.index] = 0;
			}
			continue;
		}
		// the host's word on it: kept, with how fast it went since the last
		EnemyTarget& t = s_targets[h.manager][h.index];
		const int fps  = port_active_frame_rate > 0 ? port_active_frame_rate : 30;
		const f32 dt   = (s_clock - t.at) / (f32)fps;
		if (t.known && dt > 0.02f && dt < 1.0f)
			t.vel.set((at.x - t.pos.x) / dt, (at.y - t.pos.y) / dt, (at.z - t.pos.z) / dt);
		else
			t.vel.set(0.0f, 0.0f, 0.0f);
		t.pos   = at;
		t.yaw   = h.rotY * (360.0f / 65536.0f);
		t.at    = s_clock;
		t.known = true;
	}
	steerEnemies();
}

// ------------------------------------------------------------------- arriving

// A player who joins starts beside the stage's start, not inside the host:
// a step to the side by slot (Luigi right, Peach left, Metal Mario two steps
// right), where there is floor at about the same height. Where a wall, a
// ledge or water is in the way, the next of the slot's other places is tried
// (the other side, a step behind), so no two players start in one spot.
void spreadStart()
{
	TMario* m = gpMarioOriginal;
	const int slot = mpg_slot();
	if (!m || !gpMap || slot <= 0 || slot > 3 || m->onYoshi())
		return;
	// to the right and behind, per slot, in order of preference
	static const f32 kPlaces[4][4][2] = {
		{{0, 0}, {0, 0}, {0, 0}, {0, 0}},
		{{130, 0}, {130, -130}, {0, -130}, {-130, -260}},
		{{-130, 0}, {-130, -130}, {130, -260}, {0, -260}},
		{{260, 0}, {-260, 0}, {260, -130}, {-130, -390}},
	};
	const f32 yaw  = m->mFaceAngle.y * (3.14159265f / 32768.0f);
	// the right of the way Mario faces, and his back
	const f32 rx = -cosf(yaw), rz = sinf(yaw);
	const f32 bx = -sinf(yaw), bz = -cosf(yaw);
	for (int i = 0; i < 4; i++) {
		const f32 side = kPlaces[slot][i][0], back = -kPlaces[slot][i][1];
		JGeometry::TVec3<f32> at(m->mPosition.x + rx * side + bx * back, m->mPosition.y,
		                         m->mPosition.z + rz * side + bz * back);
		const TBGCheckData* floor = nullptr;
		const f32 ground = gpMap->checkGround(at.x, at.y + 80.0f, at.z, &floor);
		if (!floor || ground < m->mPosition.y - 60.0f || ground > m->mPosition.y + 60.0f)
			continue;
		at.y = ground;
		m->warpRequest(at, m->mFaceAngle.y * (360.0f / 65536.0f));
		return;
	}
	// nowhere beside it: start where the stage says
}

// Puts Mario beside where he is now, on floor at about the same height (to
// the side or behind, the first that has some); false if there is none.
bool placeBeside(TMario* m)
{
	if (!m || !gpMap)
		return false;
	static const f32 kSpots[4][2] = {{130.0f, 0.0f}, {-130.0f, 0.0f}, {0.0f, 130.0f}, {0.0f, -130.0f}};
	const f32 yaw = m->mFaceAngle.y * (3.14159265f / 32768.0f);
	const f32 rx = -cosf(yaw), rz = sinf(yaw), bx = -sinf(yaw), bz = -cosf(yaw);
	for (int i = 0; i < 4; i++) {
		JGeometry::TVec3<f32> at(m->mPosition.x + rx * kSpots[i][0] + bx * kSpots[i][1], m->mPosition.y,
		                         m->mPosition.z + rz * kSpots[i][0] + bz * kSpots[i][1]);
		const TBGCheckData* floor = nullptr;
		const f32 ground = gpMap->checkGround(at.x, at.y + 80.0f, at.z, &floor);
		if (!floor || ground < m->mPosition.y - 60.0f || ground > m->mPosition.y + 60.0f)
			continue;
		at.y = ground;
		m->warpRequest(at, m->mFaceAngle.y * (360.0f / 65536.0f));
		return true;
	}
	return false;
}

// ------------------------------------------------------------------- spectating

bool watchable(int slot)
{
	TMpPuppet* p = slot >= 0 && slot < 4 ? s_puppets[slot] : nullptr;
	return p && slot != mpg_slot() && p->active() && !(p->pose().flags & MPG_POSE_SPECTATING);
}

f32 distance2(const JGeometry::TVec3<f32>& a, const JGeometry::TVec3<f32>& b)
{
	const f32 x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
	return x * x + y * y + z * z;
}

// The next player to watch from `from`, in `step`'s direction; -1 if none.
int nextWatch(int from, int step)
{
	for (int k = 1; k <= 4; k++) {
		const int slot = ((from < 0 ? 0 : from) + step * k + 8) % 4;
		if (watchable(slot))
			return slot;
	}
	return -1;
}

void showWatching()
{
	char text[128];
	if (s_watching >= 0)
		snprintf(text, sizeof text, "Out of lives: watching %s. L / R to watch another player", mpg_character_name(s_watching));
	else
		snprintf(text, sizeof text, "Out of lives: waiting for the others");
	showNotice(text);
}

// Each frame while out of lives: switch who we watch.
void spectateFrame(TMarDirector* dir)
{
	TMarioGamePad* pad = dir->getGamePad();
	int step           = 0;
	if (pad && pad->testTrigger(PAD_TRIGGER_R | PAD_BUTTON_RIGHT))
		step = 1;
	else if (pad && pad->testTrigger(PAD_TRIGGER_L | PAD_BUTTON_LEFT))
		step = -1;
	if (step || !watchable(s_watching)) {
		const int next = nextWatch(s_watching, step ? step : 1);
		if (next != s_watching || step) {
			s_watching = next;
			showWatching();
		}
	}
	s_noticeFrames = kNoticeFrames; // the line stays while we watch
}

// Every tick while out of lives: Mario is out of play (not moved, not drawn,
// touching nothing) and rides along with the player watched, so the camera
// follows them.
void spectateTick()
{
	TMario* m = gpMarioOriginal;
	if (!m)
		return;
	m->onFlag(MARIO_FLAG_IS_PERFORMING);
	m->onHitFlag(HIT_FLAG_NO_COLLISION);
	if (watchable(s_watching)) {
		TMpPuppet* p    = s_puppets[s_watching];
		m->mPosition    = p->mPosition;
		const MpgPose& pose = p->pose();
		// their facing, from their body's forward axis
		m->mFaceAngle.y = (s16)(atan2f(pose.mtx[0][2], pose.mtx[2][2]) * (32768.0f / 3.14159265f));
	}
}

// Out of lives, a 1-up brings a player back. The lives are shared, so each
// player who comes back takes one: when the lives reach the number of players
// watching ahead of us (lower slots first), we take ours and come back
// beside the player we were watching. Held for half a second, so that one
// coming back ahead of us has reached every game first.
int s_reviveFrames;

void reviveWithOneUp()
{
	TMario* m = gpMarioOriginal;
	if (!s_spectating || !m) {
		s_reviveFrames = 0;
		return;
	}
	const s32 lives = TFlagManager::getInstance()->getFlag(0x20001);
	int ahead       = 0;
	for (int slot = 0; slot < mpg_slot(); slot++) {
		MpgPose pose;
		float age;
		if (mpg_player_present(slot) && mpg_pose(slot, &pose, &age) && (pose.flags & MPG_POSE_SPECTATING))
			ahead++;
	}
	if (lives < ahead) {
		s_reviveFrames = 0;
		return;
	}
	if (++s_reviveFrames < (port_active_frame_rate > 0 ? port_active_frame_rate : 30) / 2)
		return;
	s_reviveFrames = 0;
	s_spectating   = false;
	TFlagManager::getInstance()->setFlag(0x20001, lives - 1);
	m->offFlag(MARIO_FLAG_IS_PERFORMING);
	m->offHitFlag(HIT_FLAG_NO_COLLISION);
	placeBeside(m);
	fprintf(stderr, "[multiplayer] back in the game with a 1-up (%d lives left)\n", lives - 1);
	showNotice("A 1-up! Back in the game");
}

// Host: everyone out of lives? Then the stage starts again for all, with
// lives as a continue gives them.
void hostCheckEveryoneOut(TMarDirector* dir)
{
	if (s_restarting || !s_spectating)
		return;
	for (int slot = 0; slot < 4; slot++) {
		if (slot == mpg_slot() || !mpg_player_present(slot))
			continue;
		MpgPose pose;
		float age;
		if (!mpg_pose(slot, &pose, &age) || !(pose.flags & MPG_POSE_SPECTATING))
			return;
	}
	s_restarting = true;
	showNotice("Everyone is out of lives: back to the start");
	TFlagManager::getInstance()->setFlag(0x20001, 3);
	mpg_restart_session();
	const TGameSequence& cur = gpApplication.mCurrArea;
	gpApplication.mNextArea.set(cur.getStage(), cur.getScenario(), 0);
	dir->onFlag(TMarDirector::DIRECTOR_FLAG_STAGE_TRANSITION_PENDING);
}

int takeGameOver()
{
	if (!enabled())
		return 0;
	// no game over screen: back into the stage, watching the others
	s_spectating      = true;
	s_spectateSession = mpg_session();
	s_watching        = -1;
	return 1;
}

// ------------------------------------------------------------------- tests

// SMS_MULTIPLAYER_TEST="take@N:K;kill@N:K;die@N:L;warp@N:area,episode"
// (tools/mp_shots.py): N frames into each stage, take K of its coins as if
// Mario had touched them, defeat K of its enemies, lose a life (with L lives
// left first, when L is given), and leave for another area as if through a
// pipe. Test runs only.
struct TestPlan {
	bool parsed;
	int takeAt, takeCount;
	int againAt, againCount; // takes the first coins even if gone (as one who had not heard): two take one
	int hatchAt;             // feeds Yoshi's egg its fruit
	bool hatched;
	int tubAt;               // goes to the edge of Bowser's tub
	bool onTub;
	int poundAt;             // ground-pounds Bowser's tub (as landing on it does)
	bool pounded;
	int claimAt;             // claims an NPC as a talk does, and walks it off
	bool claimed;
	int redAt;               // ground-pounds the first red coin switch
	bool redPressed;
	int redTakeAt, redTakeCount; // pickred@N:K: takes K red coins that are out
	bool redTook;
	int toRedAt;             // goes to the red coin switch
	bool atRed;
	int nearAt;              // stands before an NPC (presses of B talk to it), up to 1800 frames
	bool talked;
	bool tookAgain;
	int warpAt, warpArea, warpEpisode;
	int killAt, killCount;
	int dieAt, dieLives;
	int shineAt, shineId;  // shine@N:ID: this game gets Shine ID (its flag)
	int flagsAt, flagsCount; // flags@N:K: K save flags set (a well-played save)
	int livesAt, livesValue; // lives@N:L: the lives set to L (a 1-up, say)
	int dropAt;              // drop@N: a coin appears in front of Mario (as from a box)
	int takeDropAt;          // takedrop@N: this player takes the coins that appeared during the stage
	int together;            // frames this stage that another player was in sight (drop@ and takedrop@ count these)
	int coinsAt, coinsValue; // coins@N:K: the coin count set to K
	int touchAt;             // touchshine@N: a Shine appears where this player is and they touch it
	int bossWaterAt, bossWaterCount; // bosswater@N:K: K sprays in the boss's mouth, in this game
	int bossDownAt;                  // bossdown@N: the boss loses all its hit points to ground pounds
	int bossHitAt;                   // bosshit@N: a hit on this game's copy of the boss (mp_boss.cpp)
	int bossSprayAt, bossSprayFrames; // bossspray@N:K: that hit every tick for K ticks (a Gatekeeper sprayed)
	int shadowSprayAt, shadowSprayFrames; // shadowspray@N:K: Shadow Mario sprayed every 4 ticks for K ticks
	int npcSprayAt, npcSprayFrames;   // npcspray@N:K: the nearest NPC sprayed every tick for K ticks
	int carryAt, carryFrames;         // carry@N:K: pick up the nearest thing that can be carried, put it down K ticks on
	int setFlagAt;                   // setflag@N:ID: a flag of the game's set (hex), e.g. what starts a fight
	unsigned setFlagId;
	int frames; // in this stage
	bool tookHere, warped, died, shone, flagged, livesSet, dropped, tookDrop, coinsSet, touched, bossWatered, bossDowned, bossHit, flagSet;
};
TestPlan s_test;
int s_testPressA; // test aids: frames left to hold B down (padRead; B talks)

void testPadRead(void* status)
{
	if (s_testPressA > 0) {
		static_cast<PADStatus*>(status)[0].button |= PAD_BUTTON_B;
		s_testPressA--;
	}
}

void testSprayNpc(bool first); // shared objects, below
bool testPoundTub();
void testCarry(bool take);

void runTests(TMarDirector* dir, bool arrived)
{
	if (!s_test.parsed) {
		s_test.parsed = true;
		s_test.againAt = s_test.hatchAt = s_test.tubAt = s_test.poundAt = s_test.claimAt = s_test.nearAt = s_test.redAt = s_test.redTakeAt = s_test.toRedAt = -1;
		s_test.takeAt = s_test.warpAt = s_test.killAt = s_test.dieAt = s_test.shineAt = s_test.flagsAt = -1;
		s_test.livesAt = s_test.dropAt = s_test.takeDropAt = s_test.coinsAt = s_test.touchAt = s_test.bossWaterAt = s_test.bossDownAt = s_test.bossHitAt = s_test.setFlagAt = -1;
		s_test.bossSprayAt = s_test.shadowSprayAt = s_test.npcSprayAt = s_test.carryAt = -1;
		s_test.dieLives = -1;
		if (const char* plan = getenv("SMS_MULTIPLAYER_TEST")) {
			const char* t = strstr(plan, "take@");
			if (t)
				sscanf(t, "take@%d:%d", &s_test.takeAt, &s_test.takeCount);
			if (const char* tr = strstr(plan, "gotoswitch@"))
				sscanf(tr, "gotoswitch@%d", &s_test.toRedAt);
			if (const char* rt = strstr(plan, "pickred@"))
				sscanf(rt, "pickred@%d:%d", &s_test.redTakeAt, &s_test.redTakeCount);
			if (const char* rd = strstr(plan, "redswitch@"))
				sscanf(rd, "redswitch@%d", &s_test.redAt);
			if (const char* nr = strstr(plan, "nearnpc@"))
				sscanf(nr, "nearnpc@%d", &s_test.nearAt);
			if (const char* cl = strstr(plan, "claim@"))
				sscanf(cl, "claim@%d", &s_test.claimAt);
			if (const char* pd = strstr(plan, "pound@"))
				sscanf(pd, "pound@%d", &s_test.poundAt);
			if (const char* tb = strstr(plan, "tubedge@"))
				sscanf(tb, "tubedge@%d", &s_test.tubAt);
			if (const char* h = strstr(plan, "hatch@"))
				sscanf(h, "hatch@%d", &s_test.hatchAt);
			if (const char* a = strstr(plan, "again@"))
				sscanf(a, "again@%d:%d", &s_test.againAt, &s_test.againCount);
			const char* k = strstr(plan, "kill@");
			if (k)
				sscanf(k, "kill@%d:%d", &s_test.killAt, &s_test.killCount);
			const char* d = strstr(plan, "die@");
			if (d)
				sscanf(d, "die@%d:%d", &s_test.dieAt, &s_test.dieLives);
			const char* w = strstr(plan, "warp@");
			if (w)
				sscanf(w, "warp@%d:%d,%d", &s_test.warpAt, &s_test.warpArea, &s_test.warpEpisode);
			const char* sh = strstr(plan, "shine@");
			while (sh && sh > plan && sh[-1] == 'h') // touchshine@
				sh = strstr(sh + 1, "shine@");
			if (sh)
				sscanf(sh, "shine@%d:%d", &s_test.shineAt, &s_test.shineId);
			const char* f = strstr(plan, "flags@");
			if (f)
				sscanf(f, "flags@%d:%d", &s_test.flagsAt, &s_test.flagsCount);
			if (const char* l = strstr(plan, "lives@"))
				sscanf(l, "lives@%d:%d", &s_test.livesAt, &s_test.livesValue);
			if (const char* dr = strstr(plan, "drop@"))
				if (dr == plan || dr[-1] != 'e') // not takedrop@
					sscanf(dr, "drop@%d", &s_test.dropAt);
			if (const char* td = strstr(plan, "takedrop@"))
				sscanf(td, "takedrop@%d", &s_test.takeDropAt);
			if (const char* c = strstr(plan, "coins@"))
				sscanf(c, "coins@%d:%d", &s_test.coinsAt, &s_test.coinsValue);
			if (const char* ts = strstr(plan, "touchshine@"))
				sscanf(ts, "touchshine@%d", &s_test.touchAt);
			if (const char* bw = strstr(plan, "bosswater@"))
				sscanf(bw, "bosswater@%d:%d", &s_test.bossWaterAt, &s_test.bossWaterCount);
			if (const char* bd = strstr(plan, "bossdown@"))
				sscanf(bd, "bossdown@%d", &s_test.bossDownAt);
			if (const char* bh = strstr(plan, "bosshit@"))
				sscanf(bh, "bosshit@%d", &s_test.bossHitAt);
			if (const char* bs = strstr(plan, "bossspray@"))
				sscanf(bs, "bossspray@%d:%d", &s_test.bossSprayAt, &s_test.bossSprayFrames);
			if (const char* ss = strstr(plan, "shadowspray@"))
				sscanf(ss, "shadowspray@%d:%d", &s_test.shadowSprayAt, &s_test.shadowSprayFrames);
			if (const char* ns = strstr(plan, "npcspray@"))
				sscanf(ns, "npcspray@%d:%d", &s_test.npcSprayAt, &s_test.npcSprayFrames);
			if (const char* cs = strstr(plan, "carry@"))
				sscanf(cs, "carry@%d:%d", &s_test.carryAt, &s_test.carryFrames);
			if (const char* sf = strstr(plan, "setflag@"))
				sscanf(sf, "setflag@%d:%x", &s_test.setFlagAt, &s_test.setFlagId);
		}
	}
	if (arrived) {
		s_test.frames   = 0;
		s_test.together = 0;
		s_test.tookHere = false;
	}
	s_test.frames++;
	for (int slot = 0; slot < 4; slot++) {
		MpgPose pose;
		float age;
		if (slot != mpg_slot() && mpg_pose(slot, &pose, &age) && (pose.flags & MPG_POSE_VISIBLE)) {
			s_test.together++;
			break;
		}
	}
	if (getenv("SMS_MULTIPLAYER_TEST") && s_test.frames % 600 == 0)
		fprintf(stderr, "[multiplayer] test: frame %d, %d together, flag set %d, hatched %d\n", s_test.frames,
		        s_test.together, (int)s_test.flagSet, (int)s_test.hatched);
	if (s_test.nearAt >= 0 && !s_test.talked && s_test.frames >= s_test.nearAt && s_test.frames < s_test.nearAt + 1800) {
		// stood there now and then, left to stand still a while (a talk wants
		// him waiting), then B pressed, as a player talks
		if ((s_test.frames - s_test.nearAt) % 150 == 0)
			MpMirrorTestNearNpc();
		if ((s_test.frames - s_test.nearAt) % 150 == 90) {
			s_testPressA = 3;
			TBaseNPC* near = gpMarDirector ? gpMarDirector->getTalkingNPC() : nullptr;
			fprintf(stderr, "[multiplayer] test: B pressed (Mario status %x, talk target %p%s)\n",
			        (unsigned)gpMarioOriginal->getStatus(), (void*)near,
			        near && near->checkLiveFlag(LIVE_FLAG_UNK20000) ? ", in range" : "");
		}
		if (gpMarDirector && gpMarDirector->isTalkModeNow()) {
			s_test.talked = true;
			fprintf(stderr, "[multiplayer] test: talking to an NPC\n");
		}
	}
	if (s_test.toRedAt >= 0 && !s_test.atRed && s_test.frames >= s_test.toRedAt) {
		for (int k = 0; k < s_switchCount; k++)
			if (dynamic_cast<TRedCoinSwitch*>(s_switches[k])) {
				const JGeometry::TVec3<f32>& at = s_switches[k]->mPosition;
				gpMarioOriginal->mPosition.set(at.x + 150.0f, at.y + 100.0f, at.z);
				fprintf(stderr, "[multiplayer] test: to the red coin switch\n");
			}
		s_test.atRed = true;
	}
	if (s_test.redTakeAt >= 0 && !s_test.redTook && s_test.frames >= s_test.redTakeAt && gpItemManager) {
		int taken = 0;
		for (int i = 0; i < gpItemManager->getObjNum() && taken < s_test.redTakeCount; i++) {
			TLiveActor* a = gpItemManager->getObj(i);
			if (a && a->getActorType() == 0x2000000F && !a->checkLiveFlag(LIVE_FLAG_DEAD) && !a->checkLiveFlag(LIVE_FLAG_HIDDEN)) {
				static_cast<TItem*>(a)->taken(gpMarioOriginal);
				taken++;
			}
		}
		s_test.redTook = taken > 0;
		if (taken)
			fprintf(stderr, "[multiplayer] test: took %d red coins (%d here now)\n", taken,
			        (int)TFlagManager::getInstance()->getFlag(0x60000));
	}
	if (s_test.redAt >= 0 && !s_test.redPressed && s_test.frames >= s_test.redAt) {
		for (int k = 0; k < s_switchCount && !s_test.redPressed; k++)
			if (TRedCoinSwitch* red = dynamic_cast<TRedCoinSwitch*>(s_switches[k])) {
				fprintf(stderr, "[multiplayer] test: ground-pounding the red coin switch\n");
				red->receiveMessage(gpMarioOriginal, HIT_MESSAGE_HIP_DROP);
			}
		s_test.redPressed = true;
	}
	if (s_test.claimAt >= 0 && !s_test.claimed && s_test.frames >= s_test.claimAt)
		s_test.claimed = MpMirrorTestClaimNpc();
	if (s_test.poundAt >= 0 && !s_test.pounded && s_test.frames >= s_test.poundAt)
		s_test.pounded = testPoundTub();
	if (s_test.tubAt >= 0 && !s_test.onTub && s_test.frames >= s_test.tubAt)
		s_test.onTub = MpMirrorTestTubEdge();
	if (s_test.hatchAt >= 0 && !s_test.hatched && s_test.frames >= s_test.hatchAt)
		s_test.hatched = MpMirrorTestHatch();
	if (s_test.againAt >= 0 && s_test.frames == s_test.againAt + 300)
		fprintf(stderr, "[multiplayer] test: coins now %d\n", (int)TFlagManager::getInstance()->getFlag(0x40002));
	if (s_test.againAt >= 0 && !s_test.tookAgain && s_test.frames >= s_test.againAt) {
		s_test.tookAgain = true;
		TItemManager* mgr = items();
		int taken         = 0;
		for (int i = 0; mgr && i < mgr->getObjNum() && i < s_itemCount && taken < s_test.againCount; i++) {
			TLiveActor* a = mgr->getObj(i);
			if (a && a->getActorType() == 0x2000000E) {
				static_cast<TItem*>(a)->taken(gpMarioOriginal);
				taken++;
			}
		}
		fprintf(stderr, "[multiplayer] test: took %d coins again (coins %d)\n", taken,
		        (int)TFlagManager::getInstance()->getFlag(0x40002));
	}
	if (s_test.takeAt >= 0 && !s_test.tookHere && s_test.frames >= s_test.takeAt) {
		s_test.tookHere = true;
		TItemManager* mgr = items();
		int taken         = 0;
		for (int i = 0; mgr && i < mgr->getObjNum() && taken < s_test.takeCount; i++) {
			TLiveActor* a = mgr->getObj(i);
			if (a && a->getActorType() == 0x2000000E && !a->checkLiveFlag(LIVE_FLAG_DEAD) && i < s_itemCount
			    && s_itemAlive[i]) {
				static_cast<TItem*>(a)->taken(gpMarioOriginal);
				taken++;
			}
		}
		fprintf(stderr, "[multiplayer] test: took %d coins (coins %d)\n", taken,
		        (int)TFlagManager::getInstance()->getFlag(0x40002));
	}
	if (s_test.killAt >= 0 && s_test.frames == s_test.killAt) {
		int killed = 0;
		for (int m = 0; m < s_managerCount && killed < s_test.killCount; m++)
			for (int i = 0; i < s_managers[m]->getObjNum() && killed < s_test.killCount; i++)
				if (TSmallEnemy* e = enemyAt(m, i))
					if (!enemyDown(e)) {
						fprintf(stderr, "[multiplayer] test: defeating enemy %d.%d\n", m, i);
						e->kill();
						killed++;
					}
		fprintf(stderr, "[multiplayer] test: defeated %d enemies\n", killed);
	}
	if (s_test.dieAt >= 0 && !s_test.died && s_test.frames == s_test.dieAt && gpMarioOriginal && !s_spectating) {
		s_test.died = true;
		if (s_test.dieLives >= 0)
			TFlagManager::getInstance()->setFlag(0x20001, s_test.dieLives);
		fprintf(stderr, "[multiplayer] test: losing a life (%d left)\n", TFlagManager::getInstance()->getFlag(0x20001));
		gpMarioOriginal->loserExec();
	}
	if (s_test.flagsAt >= 0 && !s_test.flagged && s_test.frames >= s_test.flagsAt) {
		s_test.flagged = true;
		// after the 120 Shines: blue coins, then the story's own flags
		for (int i = 0; i < s_test.flagsCount && 0x78 + i < 0x3B4; i++)
			TFlagManager::getInstance()->setBool(true, 0x10078 + i);
		fprintf(stderr, "[multiplayer] test: set %d save flags\n", s_test.flagsCount);
	}
	if (s_test.livesAt >= 0 && !s_test.livesSet && s_test.frames >= s_test.livesAt) {
		s_test.livesSet = true;
		TFlagManager::getInstance()->setFlag(0x20001, s_test.livesValue);
		fprintf(stderr, "[multiplayer] test: lives set to %d\n", s_test.livesValue);
	}
	if (s_test.coinsAt >= 0 && !s_test.coinsSet && s_test.frames >= s_test.coinsAt) {
		s_test.coinsSet = true;
		TFlagManager::getInstance()->setFlag(0x40002, s_test.coinsValue);
		fprintf(stderr, "[multiplayer] test: coins set to %d\n", s_test.coinsValue);
	}
	if (s_test.dropAt >= 0 && !s_test.dropped && s_test.together >= s_test.dropAt && gpItemManager && gpMarioOriginal) {
		s_test.dropped = true;
		// the same place in every game: the stage's start, a little ahead
		const JGeometry::TVec3<f32>& at = gpMarioOriginal->mPosition;
		TMapObjBase* coin = gpItemManager->makeObjAppear(6500.0f, at.y + 50.0f, -3500.0f, 0x2000000E, true);
		fprintf(stderr, "[multiplayer] test: a coin appeared (%p)\n", (void*)coin);
	}
	if (s_test.takeDropAt >= 0 && !s_test.tookDrop && s_test.together >= s_test.takeDropAt) {
		s_test.tookDrop   = true;
		TItemManager* mgr = items();
		int taken         = 0;
		for (int i = 0; mgr && i < mgr->getObjNum() && i < kMaxItems; i++) {
			TLiveActor* a = mgr->getObj(i);
			if (a && a->getActorType() == 0x2000000E && !a->checkLiveFlag(LIVE_FLAG_DEAD) && appearedDuringStage(i)) {
				static_cast<TItem*>(a)->taken(gpMarioOriginal);
				taken++;
			}
		}
		fprintf(stderr, "[multiplayer] test: took %d coins that appeared during the stage\n", taken);
	}
	if (s_test.bossWaterAt >= 0 && !s_test.bossWatered && s_test.together >= s_test.bossWaterAt && gpConductor) {
		for (JGadget::TList<TEnemyManager*>::iterator it = gpConductor->unk20.begin(); it != gpConductor->unk20.end(); ++it)
			for (int i = 0; *it && i < (*it)->getObjNum(); i++)
				if (TBossPakkun* b = dynamic_cast<TBossPakkun*>((*it)->getObj(i))) {
					if (!dynamic_cast<TBossPakkunManager*>(*it) || dynamic_cast<TBossPakkunManager*>(*it)->mIsLightVersion)
						continue; // the light one (no fight) takes no water
					s_test.bossWatered = true;
					for (int k = 0; k < s_test.bossWaterCount; k++)
						b->gotWaterDamage();
					fprintf(stderr, "[multiplayer] test: %d sprays in Petey's mouth, water now %d\n", s_test.bossWaterCount,
					        b->mWaterMark);
				}
	}
	if (s_test.bossDownAt >= 0 && !s_test.bossDowned && s_test.together >= s_test.bossDownAt && gpConductor) {
		for (JGadget::TList<TEnemyManager*>::iterator it = gpConductor->unk20.begin(); it != gpConductor->unk20.end(); ++it) {
			TBossPakkunManager* mgr = dynamic_cast<TBossPakkunManager*>(*it);
			for (int i = 0; mgr && !mgr->mIsLightVersion && i < mgr->getObjNum(); i++)
				if (TBossPakkun* b = dynamic_cast<TBossPakkun*>(mgr->getObj(i))) {
					s_test.bossDowned = true;
					while (b->getHitPoints() > 0) {
						b->mState = BOSSPAKU_STATE_BELLY_UP;
						b->gotHipDropDamage();
					}
					fprintf(stderr, "[multiplayer] test: Petey ground-pounded down\n");
				}
		}
	}
	// (after the test's own trip, when it has one: in the stage it went to)
	if (s_test.setFlagAt >= 0 && !s_test.flagSet && s_test.frames >= s_test.setFlagAt && s_test.together > 0
	    && (s_test.warpAt < 0 || (s_test.warped && !arrived && s_test.frames > 1 && gpApplication.mCurrArea.getStage()
	                                                                                       == s_test.warpArea))) {
		s_test.flagSet = true;
		TFlagManager::getInstance()->setBool(true, s_test.setFlagId);
		fprintf(stderr, "[multiplayer] test: flag %x set\n", s_test.setFlagId);
	}
	if (s_test.bossSprayAt >= 0 && s_test.together >= s_test.bossSprayAt
	    && s_test.together < s_test.bossSprayAt + s_test.bossSprayFrames) {
		const int kind = MpBossTestHit();
		if (s_test.together == s_test.bossSprayAt || s_test.together + 1 == s_test.bossSprayAt + s_test.bossSprayFrames)
			fprintf(stderr, "[multiplayer] test: spraying this game's boss (kind %d), %s\n", kind,
			        s_test.together == s_test.bossSprayAt ? "starting" : "stopping");
	}
	if (s_test.shadowSprayAt >= 0 && s_test.frames >= s_test.shadowSprayAt
	    && s_test.frames < s_test.shadowSprayAt + s_test.shadowSprayFrames && s_test.frames % 4 == 0) {
		const int kind = MpBossTestShadow();
		if (s_test.frames < s_test.shadowSprayAt + 4)
			fprintf(stderr, "[multiplayer] test: spraying this game's Shadow Mario (kind %d)\n", kind);
	}
	if (s_test.carryAt >= 0 && (s_test.frames == s_test.carryAt || s_test.frames == s_test.carryAt + s_test.carryFrames))
		testCarry(s_test.frames == s_test.carryAt);
	if (s_test.npcSprayAt >= 0 && s_test.frames >= s_test.npcSprayAt
	    && s_test.frames < s_test.npcSprayAt + s_test.npcSprayFrames)
		testSprayNpc(s_test.frames == s_test.npcSprayAt);
	if (s_test.bossHitAt >= 0 && !s_test.bossHit && s_test.together >= s_test.bossHitAt) {
		const int kind = MpBossTestHit();
		if (kind) {
			s_test.bossHit = true;
			fprintf(stderr, "[multiplayer] test: a hit on this game's boss (kind %d)\n", kind);
		}
	}
	if (s_test.touchAt >= 0 && !s_test.touched && s_test.together >= s_test.touchAt && gpMarioOriginal) {
		int shines = 0;
		while (shineAt(shines))
			shines++;
		s_test.touched = shines > 0; // none here: try again in the next stage
		static int told = -1;
		if (told != shines)
			fprintf(stderr, "[multiplayer] test: %d Shines in this stage\n", shines);
		told = shines;
		for (int i = 0; shineAt(i); i++) {
			TShine* shine = shineAt(i);
			const JGeometry::TVec3<f32>& at = gpMarioOriginal->mPosition;
			shine->mPosition.set(at.x, at.y + 60.0f, at.z);
			if (shine->checkLiveFlag(LIVE_FLAG_DEAD))
				shine->appear();
			fprintf(stderr, "[multiplayer] test: touching Shine %d\n", (int)(shine->getEventId() & 0xFF));
			shine->touchPlayer(gpMarioOriginal);
			fprintf(stderr, "[multiplayer] test: Mario's status now %x (Shine Get %x), holding %p\n",
			        (unsigned)gpMarioOriginal->mStatus, (unsigned)MARIO_STATUS_WIN_DEMO, (void*)gpMarioOriginal->unk384);
			break;
		}
	}
	if (s_test.shineAt >= 0 && !s_test.shone && s_test.frames >= s_test.shineAt) {
		s_test.shone = true;
		TFlagManager::getInstance()->setShineFlag(s_test.shineId);
		fprintf(stderr, "[multiplayer] test: got Shine %d\n", s_test.shineId);
	}
	if (s_test.warpAt >= 0 && !s_test.warped && s_test.frames >= s_test.warpAt) {
		s_test.warped = true;
		fprintf(stderr, "[multiplayer] test: leaving for area %d, episode %d\n", s_test.warpArea, s_test.warpEpisode);
		dir->setNextStage((u16)(((s_test.warpArea + 1) << 8) | s_test.warpEpisode), nullptr);
	}
}

// ------------------------------------------------------------------- text

void updateText()
{
	char line[256];
	while (mpg_take_notice(line, sizeof line))
		showNotice(line);
	if (s_noticeFrames > 0 && --s_noticeFrames == 0)
		s_notice[0] = 0;
	overlay(OVERLAY_NOTICE, s_notice);
	TMarDirector* dir = director();
	const bool paused = dir && dir->mState == TMarDirector::STATE_PAUSE_MENU;
	if (s_statusFrames > 0)
		s_statusFrames--;
	static const bool statusOn = !getenv("SMS_MULTIPLAYER_STATUS") || strcmp(getenv("SMS_MULTIPLAYER_STATUS"), "0");
	if (statusOn && (paused || s_statusFrames > 0)) {
		char status[512];
		mpg_status(status, sizeof status);
		overlay(OVERLAY_STATUS, status);
	} else {
		overlay(OVERLAY_STATUS, "");
	}
}

// ------------------------------------------------------------------- cutscenes

// A camera cutscene started in one game plays in every game: the others are
// taken to the player who started it and watch it with them. Not the stage's
// opening (every game plays its own), the Shine Get (syncShineGets) or a
// gate's camera (it follows the gate, which the others cannot be handed).
bool s_replayingDemo; // playing another game's cutscene: not news to send back

// Cutscenes seen lately, by name. One that two games both start (the host
// leads a boss, and every copy plays its defeat) is one cutscene: the game
// that gets the other's after starting its own neither plays it again nor
// sends it back, so nobody is pulled to anybody.
struct RecentDemo {
	char name[64];
	int frame;
};
RecentDemo s_recentDemos[8];
int s_recentNext;

bool seenDemoLately(const char* name)
{
	const int fps = port_active_frame_rate > 0 ? port_active_frame_rate : 30;
	for (const RecentDemo& d : s_recentDemos)
		if (d.name[0] && !strcmp(d.name, name) && s_clock - d.frame < 10 * fps)
			return true;
	return false;
}

void rememberDemo(const char* name)
{
	RecentDemo& d = s_recentDemos[s_recentNext++ & 7];
	snprintf(d.name, sizeof d.name, "%s", name);
	d.frame = s_clock;
}
int s_stageFrames;    // frames of play in this stage
char s_demoNames[8][64];
JGeometry::TVec3<f32> s_demoPos[8];
int s_demoNext;

void demoCamera(const char* name, const float* pos, int frames, float angle, int option, int hasCallback, void* actor,
                unsigned flags)
{
	(void)hasCallback;
	(void)actor;
	if (getenv("SMS_MULTIPLAYER_DEBUG") && name)
		fprintf(stderr, "[multiplayer] cutscene asked for: %s (stage frame %d)\n", name, s_stageFrames);
	TMario* m = gpMarioOriginal;
	if (!enabled() || s_replayingDemo || !inStage() || !s_playing || !name || !m || s_spectating || flags == 1
	    || m->mStatus == MARIO_STATUS_WIN_DEMO)
		return;
	const int fps = port_active_frame_rate > 0 ? port_active_frame_rate : 30;
	if (s_stageFrames < 3 * fps)
		return;
	if (getenv("SMS_MULTIPLAYER_DEBUG"))
		fprintf(stderr, "[multiplayer] cutscene here: %s%s\n", name, seenDemoLately(name) ? " (seen lately)" : "");
	// another game's, just got (or ours, again): nothing to tell anyone
	if (seenDemoLately(name)) {
		rememberDemo(name);
		return;
	}
	rememberDemo(name);
	uint8_t buf[128];
	size_t n         = 0;
	const size_t len = strnlen(name, 63);
	buf[n++]         = (uint8_t)len;
	memcpy(buf + n, name, len);
	n += len;
	buf[n++] = pos ? 1 : 0;
	const float values[8] = {pos ? pos[0] : 0.0f, pos ? pos[1] : 0.0f, pos ? pos[2] : 0.0f, angle,
	                         m->mPosition.x, m->mPosition.y, m->mPosition.z, m->mFaceAngle.y * (360.0f / 65536.0f)};
	memcpy(buf + n, values, sizeof values); // the same build on both sides: the same float layout
	n += sizeof values;
	const s32 f = frames;
	memcpy(buf + n, &f, 4);
	n += 4;
	buf[n++] = (uint8_t)(option != 0);
	mpg_demo(buf, n);
}

void playOthersCutscenes(TMarDirector* dir)
{
	uint8_t buf[256];
	int slot;
	size_t n;
	while ((n = mpg_next_demo(&slot, buf, sizeof buf)) != 0) {
		TMario* m = gpMarioOriginal;
		if (!m || s_spectating || dir->isDemoModeNow() || m->mStatus == MARIO_STATUS_WIN_DEMO)
			continue; // watching one already
		const size_t len = buf[0];
		if (n < 1 + len + 1 + 32 + 4 + 1 || len > 63)
			continue;
		const int k = s_demoNext++ & 7;
		memcpy(s_demoNames[k], buf + 1, len);
		s_demoNames[k][len] = 0;
		if (seenDemoLately(s_demoNames[k]))
			continue; // this game played it already (its own copy started it too)
		rememberDemo(s_demoNames[k]);
		const bool hasPos   = buf[1 + len] != 0;
		float values[8];
		memcpy(values, buf + 2 + len, sizeof values);
		s32 frames;
		memcpy(&frames, buf + 2 + len + sizeof values, 4);
		const bool option = buf[2 + len + sizeof values + 4] != 0;
		s_demoPos[k].set(values[0], values[1], values[2]);
		// to the player who started it, then their cutscene; one about a
		// place (a boss, a statue) brings everyone to the player nearest it,
		// and leaves that player where they are
		JGeometry::TVec3<f32> at(values[4], values[5], values[6]);
		f32 face = values[7];
		bool warp = true;
		if (hasPos) {
			const JGeometry::TVec3<f32>& focus = s_demoPos[k];
			f32 best = distance2(m->mPosition, focus);
			warp     = false;
			for (int other = 0; other < 4; other++)
				if (watchable(other) && distance2(s_puppets[other]->mPosition, focus) < best) {
					best = distance2(s_puppets[other]->mPosition, focus);
					at   = s_puppets[other]->mPosition;
					face = m->mFaceAngle.y * (360.0f / 65536.0f);
					warp = true;
				}
		}
		if (warp)
			m->warpRequest(at, face);
		fprintf(stderr, "[multiplayer] %s started a cutscene (%s): everyone watches\n", mpg_character_name(slot),
		        s_demoNames[k]);
		s_replayingDemo = true;
		dir->fireStartDemoCamera(s_demoNames[k], hasPos ? &s_demoPos[k] : nullptr, frames, values[3], option, nullptr, 0,
		                         nullptr, JDrama::TFlagT<u16>(0));
		s_replayingDemo = false;
	}
}

// The others hear that a cutscene is on (the pose's flag), so a friend
// standing still is not a mystery.
bool s_inDemo[4];

void noticeCutscenes()
{
	for (int slot = 0; slot < 4; slot++) {
		MpgPose pose;
		float age;
		const bool demo = slot != mpg_slot() && mpg_player_present(slot) && mpg_pose(slot, &pose, &age)
		                  && (pose.flags & MPG_POSE_DEMO);
		if (demo && !s_inDemo[slot]) {
			char text[96];
			snprintf(text, sizeof text, "%s is watching a cutscene", mpg_character_name(slot));
			showNotice(text);
		}
		s_inDemo[slot] = demo;
	}
}

// ------------------------------------------------------------------- the nearest player

// Most of the game decides by where Mario is (gpMarioPos): an enemy chases
// him, a goop blob leaps at him, a switch or an event waits for him, a
// clipped-out object waits for him to come near. On the host, which runs the
// shared world, "Mario" is the player nearest each object while it moves
// (JDrama::TViewObj::testPerform: before and after), so all of that happens
// for every player, not just the host. An object near another player and
// off the host's screen counts as on screen while it moves (some wait for
// that), as if the host were watching it.
const f32 kNearOnScreen = 2500.0f;

bool s_nearestOn; // host, in a stage, with another player in it
int s_swapCount[4]; // debug: object moves led by each other player lately
struct MoveSwap {
	void* object;
	JGeometry::TVec3<f32>* was;
	bool unclipped;
};
MoveSwap s_swaps[32];
int s_swapDepth;

// Where each other player is now, as near as the host can tell: their newest
// pose (their puppet is drawn a little behind it, for smoothness), carried on
// at their pace for as long as it took to get here (half the round trip, and
// since it came). What the host's enemies chase and wait for.
struct Aim {
	JGeometry::TVec3<f32> at, last;
	f32 vx, vz; // units a millisecond, along the ground
	uint32_t lastTime;
	bool have;
};
Aim s_aims[4];

// Debug: how far the aim (and the drawn puppet, which the host chased before)
// is from where the player turns out to have been at that moment, once their
// poses from then arrive.
struct AimCheck {
	uint32_t when; // their game's time the aim was for
	JGeometry::TVec3<f32> aim, drawn;
};
AimCheck s_aimChecks[4][64];
int s_aimCheckNext[4];
double s_aimErr[4], s_drawnErr[4];
int s_aimErrCount[4];

void checkAims(int slot, const JGeometry::TVec3<f32>& from, uint32_t fromTime, const JGeometry::TVec3<f32>& to,
               uint32_t toTime)
{
	if (!getenv("SMS_MULTIPLAYER_DEBUG") || toTime == fromTime)
		return;
	for (int i = 0; i < 64; i++) {
		AimCheck& c = s_aimChecks[slot][i];
		if (!c.when || (int32_t)(c.when - fromTime) <= 0 || (int32_t)(c.when - toTime) > 0)
			continue;
		const f32 t = (f32)(int32_t)(c.when - fromTime) / (f32)(int32_t)(toTime - fromTime);
		const JGeometry::TVec3<f32> was(from.x + (to.x - from.x) * t, from.y, from.z + (to.z - from.z) * t);
		const f32 ax = c.aim.x - was.x, az = c.aim.z - was.z, dx = c.drawn.x - was.x, dz = c.drawn.z - was.z;
		s_aimErr[slot] += sqrtf(ax * ax + az * az);
		s_drawnErr[slot] += sqrtf(dx * dx + dz * dz);
		s_aimErrCount[slot]++;
		c.when = 0;
	}
	if (s_aimErrCount[slot] >= 300) {
		fprintf(stderr, "[multiplayer] aim at %s: %.0f units off on average (the drawn puppet: %.0f)\n",
		        mpg_character_name(slot), s_aimErr[slot] / s_aimErrCount[slot], s_drawnErr[slot] / s_aimErrCount[slot]);
		s_aimErr[slot] = s_drawnErr[slot] = 0.0;
		s_aimErrCount[slot]                = 0;
	}
}

void updateAims()
{
	for (int slot = 0; slot < 4; slot++) {
		Aim& a = s_aims[slot];
		MpgPose pose;
		float age = 0.0f;
		if (!watchable(slot) || !mpg_pose(slot, &pose, &age)) {
			a.have = false;
			continue;
		}
		const JGeometry::TVec3<f32> pos(pose.mtx[0][3], pose.mtx[1][3], pose.mtx[2][3]);
		if (!a.have) {
			a.vx = a.vz = 0.0f;
		} else if (pose.time != a.lastTime) {
			const f32 dt = (f32)(int32_t)(pose.time - a.lastTime);
			const f32 dx = pos.x - a.last.x, dz = pos.z - a.last.z;
			if (dt > 0.0f && dt < 500.0f && dx * dx + dz * dz < 400.0f * 400.0f) {
				a.vx += (dx / dt - a.vx) * 0.5f;
				a.vz += (dz / dt - a.vz) * 0.5f;
			} else {
				a.vx = a.vz = 0.0f; // a warp, or a long gap: no pace to go by
			}
		}
		if (!a.have || pose.time != a.lastTime) {
			if (a.have)
				checkAims(slot, a.last, a.lastTime, pos, pose.time);
			a.last     = pos;
			a.lastTime = pose.time;
		}
		a.have         = true;
		const int ping = mpg_ping(slot);
		f32 lead       = (ping > 0 ? ping * 0.5f : 0.0f) + age * 1000.0f;
		if (lead > 200.0f)
			lead = 200.0f;
		a.at.set(a.last.x + a.vx * lead, a.last.y, a.last.z + a.vz * lead);
		if (getenv("SMS_MULTIPLAYER_DEBUG")) {
			AimCheck& c = s_aimChecks[slot][s_aimCheckNext[slot]++ & 63];
			c.when      = a.lastTime + (uint32_t)lead;
			c.aim       = a.at;
			c.drawn     = s_puppets[slot]->mPosition;
		}
	}
}

const JGeometry::TVec3<f32>& aimOf(int slot)
{
	return s_aims[slot].have ? s_aims[slot].at : s_puppets[slot]->mPosition;
}

void objectMove(void* object, int after)
{
	if (after) {
		if (s_swapDepth > 0 && s_swaps[s_swapDepth - 1].object == object) {
			MoveSwap& w = s_swaps[--s_swapDepth];
			gpMarioPos  = w.was;
			if (w.unclipped)
				static_cast<TLiveActor*>(object)->onLiveFlag(LIVE_FLAG_CLIPPED_OUT);
		}
		return;
	}
	if (!s_nearestOn || s_swapDepth >= 32 || !gpMarioPos)
		return;
	TLiveActor* a = dynamic_cast<TLiveActor*>(static_cast<JDrama::TViewObj*>(object));
	if (!a)
		return;
	int best    = -1;
	f32 nearest = distance2(a->mPosition, *gpMarioPos);
	for (int slot = 0; slot < 4; slot++)
		if (watchable(slot)) {
			const f32 d = distance2(a->mPosition, aimOf(slot));
			if (d < nearest)
				nearest = d, best = slot;
		}
	if (best < 0)
		return; // the host is nearest: as ever
	s_swapCount[best]++;
	MoveSwap& w = s_swaps[s_swapDepth++];
	w.object    = object;
	w.was       = gpMarioPos;
	w.unclipped = false;
	gpMarioPos  = const_cast<JGeometry::TVec3<f32>*>(&aimOf(best));
	if (a->checkLiveFlag(LIVE_FLAG_CLIPPED_OUT) && nearest < kNearOnScreen * kNearOnScreen) {
		a->offLiveFlag(LIVE_FLAG_CLIPPED_OUT);
		w.unclipped = true;
	}
}

void updateNearest()
{
	const int fps = port_active_frame_rate > 0 ? port_active_frame_rate : 30;
	if (s_clock % (10 * fps) == 0) {
		if (getenv("SMS_MULTIPLAYER_DEBUG"))
			for (int slot = 0; slot < 4; slot++)
				if (s_swapCount[slot])
					fprintf(stderr, "[multiplayer] nearest: %d object moves led by %s lately\n", s_swapCount[slot],
					        mpg_character_name(slot));
		memset(s_swapCount, 0, sizeof s_swapCount);
	}
	s_nearestOn = false;
	if (s_role != MPG_ROLE_HOST || !inStage() || s_swapDepth)
		return;
	updateAims();
	for (int slot = 0; slot < 4; slot++)
		if (watchable(slot))
			s_nearestOn = true;
}

// ------------------------------------------------------------------- shared objects

// What players do to NPCs and map objects (spray a goop-covered Pianta,
// ground-pound a box, stomp a switch) happens in every game: each game sends
// the hits its own player lands (objectHit, from the game's hit points), and
// the others land them on their copies. Objects are named by their manager's
// place in the stage's list and their place in it (every game loads a stage
// the same way). Enemies, bosses and items have their own sharing and are
// left out.
// object -> manager << 16 | index, in an open-addressed table (0: empty)
const int kSharedSlots = 8192;
struct SharedEntry {
	const void* object;
	uint32_t where;
};
SharedEntry s_shared[kSharedSlots];
TLiveManager* s_sharedManagers[256];
int s_sharedCount;
// the stage's lone actors (not in a manager: some NPCs, a statue, the
// plaza's Shadow Mario), as manager kAlone
const int kAlone = 255;
TLiveActor* s_alone[512];
int s_aloneCount;
bool s_landingHit;

uint32_t sharedSlot(const void* object) { return (uint32_t)(((uintptr_t)object >> 4) * 2654435761u) & (kSharedSlots - 1); }

void shareObject(const void* object, uint32_t where)
{
	for (uint32_t i = sharedSlot(object), n = 0; n < kSharedSlots / 2; i = (i + 1) & (kSharedSlots - 1), n++)
		if (!s_shared[i].object || s_shared[i].object == object) {
			s_shared[i].object = object;
			s_shared[i].where  = where;
			return;
		}
}

// Where `object` is in the shared lists; false if it is not shared.
bool sharedWhere(const void* object, uint32_t* where)
{
	for (uint32_t i = sharedSlot(object), n = 0; n < kSharedSlots / 2; i = (i + 1) & (kSharedSlots - 1), n++) {
		if (!s_shared[i].object)
			return false;
		if (s_shared[i].object == object) {
			*where = s_shared[i].where;
			return true;
		}
	}
	return false;
}

void collectShared()
{
	memset(s_shared, 0, sizeof s_shared);
	s_sharedCount = 0;
	s_aloneCount  = 0;
	if (!gpConductor)
		return;
	for (JGadget::TList<TLiveActor*>::iterator it = gpConductor->unk30.begin();
	     it != gpConductor->unk30.end() && s_aloneCount < 512; ++it) {
		TLiveActor* a       = *it;
		const int i         = s_aloneCount++;
		s_alone[i]          = nullptr;
		if (!a || dynamic_cast<TSmallEnemy*>(a) || MpBossIsBoss(a) || dynamic_cast<TItem*>(a))
			continue; // enemies, bosses and items share their own way
		s_alone[i] = a;
		shareObject(a, (uint32_t)kAlone << 16 | (uint32_t)i);
	}
	for (JGadget::TList<TLiveManager*>::iterator it = gpConductor->unk10.begin();
	     it != gpConductor->unk10.end() && s_sharedCount < kAlone; ++it) {
		TLiveManager* mgr = *it;
		const int m       = s_sharedCount++;
		s_sharedManagers[m] = nullptr;
		// enemies (the host's run them), bosses (mp_boss.cpp) and items share
		// their own way; NPCs and animals sit in enemy managers too, and are shared here
		TLiveActor* first = mgr && mgr->getObjNum() ? mgr->getObj(0) : nullptr;
		const bool skip   = !mgr || dynamic_cast<TItemManager*>(mgr) || dynamic_cast<TSmallEnemy*>(first)
		                  || MpBossIsBoss(first);
		if (getenv("SMS_MULTIPLAYER_DEBUG") && mgr)
			fprintf(stderr, "[multiplayer] objects: manager %d %s, %d of %s%s\n", m, typeid(*mgr).name(), mgr->getObjNum(),
			        mgr->getObjNum() && mgr->getObj(0) ? typeid(*mgr->getObj(0)).name() : "-", skip ? " (not shared here)" : "");
		if (skip)
			continue;
		s_sharedManagers[m] = mgr;
		for (int i = 0; i < mgr->getObjNum() && i < 0x10000; i++)
			if (TLiveActor* a = mgr->getObj(i))
				shareObject(a, (uint32_t)m << 16 | (uint32_t)i);
	}
}

THitActor* sharedAt(int manager, int index)
{
	if (manager == kAlone)
		return index >= 0 && index < s_aloneCount ? s_alone[index] : nullptr;
	if (manager < 0 || manager >= s_sharedCount || !s_sharedManagers[manager])
		return nullptr;
	TLiveManager* mgr = s_sharedManagers[manager];
	return index >= 0 && index < mgr->getObjNum() ? mgr->getObj(index) : nullptr;
}

bool sharedHit(unsigned message)
{
	return message == HIT_MESSAGE_SPRAYED_BY_WATER || message == HIT_MESSAGE_HIP_DROP
	       || message == HIT_MESSAGE_SUPER_HIP_DROP || message == HIT_MESSAGE_PUNCH || message == HIT_MESSAGE_TRAMPLE
	       || message == HIT_MESSAGE_PUSH_UP;
}

int objectHit(void* object, void* sender, unsigned message)
{
	if (!enabled() || !inStage() || s_landingHit || !gpModelWaterManager)
		return 0;
	uint32_t where;
	if (!sharedWhere(object, &where))
		return 0;
	TWaterHitActor& waterHit = TModelWaterManager::mStaticHitActor;
	const bool water         = sender == &waterHit;
	if (water && waterHit.mParticleIndex >= 0 && waterHit.mParticleIndex < TModelWaterManager::SLOT_NUM
	    && (gpModelWaterManager->getParticleFlagSOA()[waterHit.mParticleIndex] & kDrawnWater))
		return 1; // another player's water, drawn here: their game sends what it hits
	if ((!water && sender != gpMarioOriginal) || !sharedHit(message))
		return 0;
	MpMirrorUsed(object); // this game runs it a while (a slot machine sprayed here)
	const THitActor* from = static_cast<THitActor*>(sender);
	mpg_hit((int)(where >> 16), (int)(where & 0xFFFF), (int)message, water, &from->mPosition.x);
	return 0;
}

// Test aid (npcspray@N:K): this game's water on the NPC nearest its Mario,
// through the game's own hit point (so it is sent like any spray).
void testSprayNpc(bool first)
{
	TBaseNPC* best = nullptr;
	f32 nearest    = 0.0f;
	uint32_t where = 0;
	for (int m = 0; m < s_sharedCount; m++)
		for (int i = 0; s_sharedManagers[m] && i < s_sharedManagers[m]->getObjNum(); i++)
			if (TBaseNPC* npc = dynamic_cast<TBaseNPC*>(s_sharedManagers[m]->getObj(i))) {
				const f32 d = distance2(npc->mPosition, gpMarioOriginal->mPosition);
				if (!best || d < nearest)
					best = npc, nearest = d, where = (uint32_t)m << 16 | (uint32_t)i;
			}
	if (!best)
		return;
	TWaterHitActor& water = TModelWaterManager::mStaticHitActor;
	water.mPosition       = best->mPosition;
	water.mParticleIndex  = -1; // not a drawn drop: this game's own
	if (first)
		fprintf(stderr, "[multiplayer] test: spraying NPC %u.%u\n", (unsigned)(where >> 16), (unsigned)(where & 0xFFFF));
	if (!objectHit(best, &water, HIT_MESSAGE_SPRAYED_BY_WATER))
		best->receiveMessage(&water, HIT_MESSAGE_SPRAYED_BY_WATER);
}

// Test aid (pound@N): this game's Mario ground-pounds Bowser's tub, through
// the hit hook as his landing does (patch zzzzz-ext-06).
bool testPoundTub()
{
	// when Bowser can stagger (standing or turning: not breathing fire, as
	// TKoopa::stagger leaves those be)
	TKoopa* koopa = JDrama::TNameRefGen::search<TKoopa>("\x83\x4e\x83\x62\x83\x70");
	if (!koopa)
		return false;
	const void* n = koopa->getSpine()->getCurrentNerve();
	if (n != &TNerveKoopaWait::theNerve() && n != &TNerveKoopaTurnL::theNerve() && n != &TNerveKoopaTurnR::theNerve())
		return false;
	for (int m = 0; m < s_sharedCount; m++)
		for (int i = 0; s_sharedManagers[m] && i < s_sharedManagers[m]->getObjNum(); i++)
			if (TBathtub* tub = dynamic_cast<TBathtub*>(s_sharedManagers[m]->getObj(i))) {
				fprintf(stderr, "[multiplayer] test: ground-pounding the tub (%d.%d)\n", m, i);
				if (!objectHit(tub, gpMarioOriginal, HIT_MESSAGE_HIP_DROP))
					tub->receiveMessage(gpMarioOriginal, HIT_MESSAGE_HIP_DROP);
				return true;
			}
	return false;
}

// The others' hits on shared objects, landed here as theirs were.
void landHits()
{
	int slot, manager, index, message, water, count;
	float pos[3];
	while (mpg_next_hit(&slot, &manager, &index, &message, &water, &count, pos)) {
		THitActor* to = sharedAt(manager, index);
		if (!to || MpMirrorKeepsHits(to))
			continue; // its owner runs it, and sends how it went
		THitActor* from = nullptr;
		if (water) {
			TWaterHitActor& waterHit = TModelWaterManager::mStaticHitActor;
			waterHit.mPosition.set(pos[0], pos[1], pos[2]);
			waterHit.mParticleIndex = 0;
			from = &waterHit;
		} else if (slot >= 0 && slot < 4 && s_puppets[slot] && s_puppets[slot]->active()) {
			from = s_puppets[slot];
		}
		if (!from)
			continue;
		// a stomp or a ground pound counts as the player's when it comes from
		// Mario's kind of actor: the other player's character is, while it lands
		const int type = from->mActorType;
		if (!water)
			from->mActorType = 0x80000001;
		s_landingHit = true;
		for (int i = 0; i < count && i < 30; i++)
			to->receiveMessage(from, (u32)message);
		s_landingHit = false;
		from->mActorType = type;
		static int landed;
		if (getenv("SMS_MULTIPLAYER_DEBUG") && landed++ % 50 == 0)
			fprintf(stderr, "[multiplayer] hits: %s's %d (message %d) landed on %d.%d here\n", mpg_character_name(slot), count,
			        message, manager, index);
	}
}

// ------------------------------------------------------------------- carried objects

// Another player's hands, here: an object they carry is held by it, and
// follows it (in front of their character, chest high), as one follows
// Mario's hands. Nobody here can take it from them meanwhile.
class TMpHolder : public TTakeActor {
public:
	TMpHolder() : TTakeActor("<TMpHolder>") { MTXIdentity(mMtx); }
	virtual MtxPtr getTakingMtx() { return mMtx; }
	Mtx mMtx;
};
union HolderStore {
	char bytes[sizeof(TMpHolder)];
	double align;
};
HolderStore s_holderStore[4]; // made once, kept: not on a stage's heap
TMpHolder* s_holders[4];
TTakeActor* s_myHeld; // what this game's Mario held last tick

TMpHolder* holderFor(int slot)
{
	if (!s_holders[slot])
		s_holders[slot] = new (&s_holderStore[slot]) TMpHolder();
	return s_holders[slot];
}

void placeHolder(int slot)
{
	TMpHolder* h = s_holders[slot];
	TMpPuppet* p = s_puppets[slot];
	if (!h || !p)
		return;
	const f32 yaw = p->mRotation.y * (3.14159265f / 180.0f);
	const f32 x = p->mPosition.x + sinf(yaw) * 50.0f, y = p->mPosition.y + 100.0f, z = p->mPosition.z + cosf(yaw) * 50.0f;
	MsMtxSetXYZRPH(h->mMtx, x, y, z, 0, (s16)(p->mRotation.y * (65536.0f / 360.0f)), 0);
	h->mPosition.set(x, y, z);
}

// Each tick: what other players carry follows them; one who left drops it.
void tickHolders()
{
	for (int slot = 0; slot < 4; slot++) {
		TMpHolder* h = s_holders[slot];
		if (!h || !h->mHeldObject)
			continue;
		if (!watchable(slot)) {
			h->mHeldObject->mHolder = nullptr; // the object goes back to lying where it is
			h->mHeldObject          = nullptr;
			continue;
		}
		placeHolder(slot);
	}
}

// This game's Mario picked something up, put it down or threw it: the
// others do the same with theirs (where it ends up, and how it moves).
void watchMyCarry()
{
	TTakeActor* now = gpMarioOriginal ? gpMarioOriginal->mHeldObject : nullptr;
	if (now == s_myHeld)
		return;
	const int yaw = gpMarioOriginal ? gpMarioOriginal->mFaceAngle.y : 0;
	uint32_t where;
	if (s_myHeld && sharedWhere(s_myHeld, &where)) {
		TLiveActor* a       = static_cast<TLiveActor*>(s_myHeld);
		// thrown: on its way sideways (put down, or dropped, it only falls)
		const bool thrown = a->mVelocity.x * a->mVelocity.x + a->mVelocity.z * a->mVelocity.z > 1.0f;
		mpg_carry((int)(where >> 16), (int)(where & 0xFFFF), thrown ? 2 : 1, &a->mPosition.x, &a->mVelocity.x, yaw);
	}
	if (now && sharedWhere(now, &where)) {
		const float still[3] = {0.0f, 0.0f, 0.0f};
		mpg_carry((int)(where >> 16), (int)(where & 0xFFFF), 0, &now->mPosition.x, still, yaw);
	}
	s_myHeld = now;
}

// Test aid (carry@N:K): this game's Mario takes the nearest shared thing
// that can be carried, then puts it down (as the game's own take and put do).
TLiveActor* s_testCarried;

void testCarry(bool take)
{
	TMario* m = gpMarioOriginal;
	if (!m)
		return;
	if (!take) {
		if (s_testCarried && m->mHeldObject == s_testCarried) {
			s_testCarried->receiveMessage(m, HIT_MESSAGE_PUT);
			m->mHeldObject = nullptr;
			fprintf(stderr, "[multiplayer] test: put it down\n");
		}
		return;
	}
	TMapObjBase* best = nullptr;
	f32 nearest       = 0.0f;
	int takeable      = 0;
	for (int k = 0; k < kSharedSlots; k++) {
		TMapObjBase* o = s_shared[k].object ? dynamic_cast<TMapObjBase*>((TLiveActor*)s_shared[k].object) : nullptr;
		if (!o || !o->checkMapObjFlag(TMapObjBase::MAP_OBJ_FLAG_UNK100000))
			continue;
		takeable++;
		const f32 d = distance2(o->mPosition, m->mPosition);
		if (!best || d < nearest)
			best = o, nearest = d;
	}
	fprintf(stderr, "[multiplayer] test: %d things here can be carried\n", takeable);
	if (best && best->receiveMessage(m, HIT_MESSAGE_TAKE)) {
		m->mHeldObject  = best;
		s_testCarried   = best;
		watchMyCarry(); // at once: this Mario is not really holding, and lets go next tick
		uint32_t where  = 0;
		sharedWhere(best, &where);
		fprintf(stderr, "[multiplayer] test: picked up %u.%u\n", (unsigned)(where >> 16), (unsigned)(where & 0xFFFF));
	}
}

// What the other players picked up, put down and threw, done here.
void landCarries()
{
	int slot, manager, index, action, yaw;
	float pos[3], vel[3];
	while (mpg_next_carry(&slot, &manager, &index, &action, pos, vel, &yaw)) {
		TLiveActor* obj = dynamic_cast<TLiveActor*>(sharedAt(manager, index));
		if (!obj || slot < 0 || slot > 3 || slot == mpg_slot())
			continue;
		TMpHolder* h = holderFor(slot);
		placeHolder(slot);
		if (action == 0) {
			if (obj->mHolder && obj->mHolder != h)
				continue; // held here already (both took it at once): it stays with ours
			if (obj->receiveMessage(h, HIT_MESSAGE_TAKE))
				h->mHeldObject = obj;
			if (getenv("SMS_MULTIPLAYER_DEBUG"))
				fprintf(stderr, "[multiplayer] carry: %s picked up %d.%d (%s here)\n", mpg_character_name(slot), manager,
				        index, h->mHeldObject == obj ? "held" : "not held");
			continue;
		}
		if (obj->mHolder == h)
			obj->receiveMessage(h, action == 2 ? HIT_MESSAGE_THROWN : HIT_MESSAGE_PUT);
		if (obj->mHolder == h)
			obj->mHolder = nullptr;
		if (h->mHeldObject == obj)
			h->mHeldObject = nullptr;
		// where it is and how it moves, as in their game
		obj->mPosition.set(pos[0], pos[1], pos[2]);
		obj->mVelocity.set(vel[0], vel[1], vel[2]);
		if (getenv("SMS_MULTIPLAYER_DEBUG"))
			fprintf(stderr, "[multiplayer] carry: %s %s %d.%d\n", mpg_character_name(slot), action == 2 ? "threw" : "put down",
			        manager, index);
	}
}

// ------------------------------------------------------------------- hooks

void frame()
{
	if (s_role < 0)
		s_role = mpg_start();
	if (!enabled())
		return;
	s_clock++;
	updateNearest();
	TMarDirector* dir = director();
	const int area    = gpApplication.mCurrArea.getStage();
	MpgGame g;
	g.playing = playing();
	g.area    = area;
	g.episode = gpApplication.mCurrArea.getScenario();
	g.world   = SMS_getShineStage((u8)area);
	g.paused  = dir && dir->mState == TMarDirector::STATE_PAUSE_MENU;
	g.frameMs = 1000.0f / (port_active_frame_rate > 0 ? port_active_frame_rate : 30);
	mpg_frame(&g);

	s_wasPlaying = s_playing;
	s_playing    = g.playing != 0;
	if (s_playing && !s_wasPlaying) {
		snapshotItems();
		collectManagers();
		collectShared();
		MpMirrorCollect();
		s_myHeld = nullptr;
		for (int slot = 0; slot < 4; slot++)
			if (s_holders[slot])
				s_holders[slot]->mHeldObject = nullptr; // last stage's objects are gone
		collectSwitches();
		s_stageFrames = 0;
		s_restarting = false;
		if (s_spectating && mpg_session() != s_spectateSession) {
			s_spectating = false; // a new start: alive again
			showNotice("Back in the game");
		}
		if (s_spectating)
			showWatching();
		else if (s_role == MPG_ROLE_CLIENT)
			spreadStart();
	}
	if (s_role == MPG_ROLE_CLIENT) {
		skipTitle(dir);
		clientShineExit(dir);
		if (s_shineHoldFrames > 0)
			s_shineHoldFrames--;
	}
	if (inStage()) {
		syncFlags();
		takeRemovals();
		syncTakenAt();
		sendPose();
		if (s_role == MPG_ROLE_HOST) {
			// paused, the host's enemies stand still: the others' games would
			// hold theirs still too while they still bite, so send none
			if (g.paused)
				mpg_set_enemies(nullptr, 0);
			else {
				MpBossHostFrame(); // before the enemies: it rides in their snapshot
				hostEnemies();
			}
		} else
			reportEnemyDefeats();
		noticeOwnShineGet();
		if (s_playing) {
			playOthersShineGets();
			if (s_role == MPG_ROLE_CLIENT)
				followHost(dir);
			else
				hostShineExit(dir);
			runTests(dir, !s_wasPlaying);
			noticeCutscenes();
			playOthersCutscenes(dir);
			s_stageFrames++;
			if (s_spectating) {
				spectateFrame(dir);
				if (s_role == MPG_ROLE_HOST)
					hostCheckEveryoneOut(dir);
				reviveWithOneUp();
			}
		}
	} else {
		MpgPose none;
		memset(&none, 0, sizeof none);
		mpg_set_pose(&none);
	}
	if (s_refusedFrames > 0)
		s_refusedFrames--;
	updateText();
}

void stageSetup()
{
	s_hostShineFrames = 0; // a Shine's exit belongs to the stage it was got in
	MpBossStageSetup();
	s_inWinDemo = s_winRemote = false;
	s_shineGetFrames  = 0;
	if (!enabled())
		return;
	s_readyDir = gpMarDirector;
	s_stageNumber++;
	s_managerCount = 0; // the last stage's are gone
	if (gpApplication.mCurrArea.getStage() == 15 || !gpStrategy)
		return;
	if (getenv("SMS_MULTIPLAYER_DEBUG"))
		fprintf(stderr, "[multiplayer] stage %d: %d bytes free in the stage's heap\n",
		        (int)gpApplication.mCurrArea.getStage(), (int)JKRHeap::getCurrentHeap()->getTotalFreeSize());
	TIdxGroupObj* group = gpStrategy->unk10[6]; // "プレーヤーグループ": Mario's
	memset(s_puppets, 0, sizeof s_puppets);
	if (!group)
		return;
	const int me = mpg_slot();
	s_self       = nullptr;
	for (int slot = 0; slot < 4; slot++) {
		// our own character too, unless we are Mario (the game draws him)
		const bool self = slot == me;
		if (self && mpg_character(slot) == MPG_CHAR_MARIO)
			continue;
		const s32 before   = JKRHeap::getCurrentHeap()->getTotalFreeSize();
		// a puppet takes about 450 KB of the stage's memory (Peach 700 KB);
		// the port's stages have some 44 MB to spare (its arena is larger than
		// the console's), so this only guards against a stage that has not
		if (before < 1024 * 1024) {
			fprintf(stderr, "[multiplayer] not enough memory in this stage for %s (%d bytes free)\n",
			        mpg_character_name(slot), (int)before);
			continue;
		}
		TMpPuppet* puppet  = new TMpPuppet(slot, self);
		if (!puppet->init()) {
			fprintf(stderr, "[multiplayer] no room for %s in this stage\n", mpg_character_name(slot));
			continue;
		}
		THitActor* actor = puppet;
		group->add(actor);
		s_puppets[slot] = puppet;
		if (self)
			s_self = puppet;
		fprintf(stderr, "[multiplayer] %s's model ready (%d bytes)\n", mpg_character_name(slot),
		        (int)(before - JKRHeap::getCurrentHeap()->getTotalFreeSize()));
	}
}

// ------------------------------------------------------------------- goop

// Cleaning what another game cleaned (not this game's own to send).
bool s_cleaning;

// Goop cleaned here (FLUDD's water, Mario's feet, an event): the others clean it too.
void pollutionClean(float x, float y, float z, float size)
{
	if (s_cleaning || !enabled() || !inStage())
		return;
	mpg_clean(x, y, z, size);
}

// What the others cleaned, cleaned here: a few hundred a tick, so catching up
// on a whole stage's cleaning does not hold up a frame.
void applyCleans()
{
	if (!gpPollution)
		return;
	float x, y, z, size;
	int n = 0;
	for (; n < 400 && mpg_next_clean(&x, &y, &z, &size); n++) {
		s_cleaning = true;
		gpPollution->clean(x, y, z, size);
		s_cleaning = false;
	}
	static int cleaned, reported;
	cleaned += n;
	if (getenv("SMS_MULTIPLAYER_DEBUG") && cleaned >= reported + 100) {
		fprintf(stderr, "[multiplayer] goop: %d spots cleaned here that the others cleaned\n", cleaned);
		reported = cleaned;
	}
}

void afterMovement()
{
	if (!enabled() || !inStage())
		return;
	applyCleans();
	landHits();
	watchMyCarry();
	landCarries();
	tickHolders();
	MpMirrorTick();
	if (s_role == MPG_ROLE_CLIENT) {
		followHostEnemies();
		MpBossClientTick();
	}
	if (s_spectating)
		spectateTick();
}

int allowNextStage(unsigned code, void* actor)
{
	int area, episode;
	const int next = code >= 0x100 ? (int)(code >> 8) - 1 : (int)code;
	if (s_role == MPG_ROLE_HOST && inStage()) {
		// setting off: everyone sets off now, so the fades go together
		mpg_host_leaving(next, code >= 0x100 ? (int)(code & 0xFF) : 0xFF);
		return 1;
	}
	if (s_role != MPG_ROLE_CLIENT || !mpg_target(&area, &episode))
		return 1;
	if (next == area)
		return 1;
	if (!s_refusedFrames)
		showNotice("Only the host can take everyone to another area");
	s_refusedFrames = 60;
	// Let the trip go ahead (moveStage brings this game back into the host's
	// stage): the game's callers carry on as if it went, so refusing it would
	// leave Mario hidden on an exit floor, a stage-change object without its
	// collision, or a coaster or boat ride half done.
	(void)actor;
	return 1;
}

void moveStage()
{
	int area, episode;
	if (s_role == MPG_ROLE_HOST) {
		// on its way (any way out: the pause menu's, a Shine's, an event's),
		// with what shapes the arrival: the others go too, and arrive alike
		const TGameSequence& next = gpApplication.mNextArea;
		uint32_t flags = 0;
		for (int i = 0; i < kSessionFlags; i++)
			if (TFlagManager::getInstance()->getBool(0x30000 + i))
				flags |= 1u << i;
		TMarDirector* dir = director();
		const int movie   = dir && dir->checkUnk4CFlag(TMarDirector::DIRECTOR_FLAG_MOVIE_PENDING)
		                        ? (int)gpApplication.getMovie() : -1;
		// the stage it is in, again (a way back in, a restart): a new session
		// all the same, so the others load it afresh too
		const TGameSequence& cur = gpApplication.mCurrArea;
		if (next.getStage() == cur.getStage() && next.getScenario() == cur.getScenario())
			mpg_restart_session();
		mpg_host_going(next.getStage(), next.getScenario(), flags, movie);
		return;
	}
	if (s_role != MPG_ROLE_CLIENT || !mpg_target(&area, &episode))
		return;
	takeTravel(director()); // if it came after this game set off
	// after its own Shine it goes to the plaza, as the host will once saved
	TMarDirector* dir = director();
	if (dir && dir->checkFlag(TMarDirector::DIRECTOR_FLAG_SHINE_TAKEN))
		return;
	// wherever this game meant to go, it goes where the host is
	TGameSequence& next = gpApplication.mNextArea;
	if (next.getStage() != area || (episode != 0xFF && next.getScenario() != episode)) {
		next.set((u8)area, (u8)episode, 0);
		if (episode != 0xFF)
			TFlagManager::getInstance()->setFlag(0x40003, episode);
	}
}

void missNextArea()
{
	if (!enabled())
		return;
	// back into the same stage, not out to the plaza: the others are still here
	const TGameSequence& cur = gpApplication.mCurrArea;
	gpApplication.mNextArea.set(cur.getStage(), cur.getScenario(), 0);
}

int keepCoinsOnMiss() { return enabled(); }

void afterFileSelect()
{
	int area, episode;
	if (s_role != MPG_ROLE_CLIENT || !mpg_flags_ready() || !mpg_target(&area, &episode))
		return;
	// the host's save, then straight to the host's stage
	syncFlags();
	TFlagManager::getInstance()->setFlag(0x40003, episode);
	gpApplication.mNextArea.set((u8)area, (u8)episode, 0);
}

void itemTaken(void* item, void*)
{
	if (!enabled() || !inStage())
		return;
	TItemManager* mgr = items();
	if (!mgr)
		return;
	const int n = mgr->getObjNum() < kMaxItems ? mgr->getObjNum() : kMaxItems;
	for (int i = 0; i < n; i++)
		if ((void*)mgr->getObj(i) == item) {
			TLiveActor* a = mgr->getObj(i);
			if (!appearedDuringStage(i)) {
				mpg_item_taken(itemKey(i, a));
			} else {
				const float pos[3] = {a->mPosition.x, a->mPosition.y, a->mPosition.z};
				mpg_item_taken_at(a->getActorType() & 0xFF, pos);
			}
			return;
		}
}

int allowSaveStates() { return !enabled(); }

// Mario's own models make way for our character's (s_self draws them).
int hidePlayerModel(void* mario)
{
	return enabled() && s_self && mario == gpMarioOriginal && inStage();
}
const char* saveStatesOffReason() { return "Save states are off in multiplayer"; }

PortExtension s_extension = {
	"multiplayer",   frame,           afterMovement,   stageSetup,     allowNextStage, moveStage,
	missNextArea,    takeGameOver,    keepCoinsOnMiss, afterFileSelect, itemTaken,      allowSaveStates,
	saveStatesOffReason, hidePlayerModel, testPadRead, demoCamera, pollutionClean, objectMove, objectHit,
};

struct Register {
	Register() { port_ext_register(&s_extension); }
} s_register;

} // namespace

// Where player `slot` is in this game's stage (another extension's question,
// e.g. ext/demo): 1 and xyz filled in when they are here and in play.
extern "C" int mp_player_position(int slot, float* xyz)
{
	if (!enabled() || !inStage() || slot < 0 || slot > 3)
		return 0;
	if (slot == mpg_slot()) {
		xyz[0] = gpMarioOriginal->mPosition.x;
		xyz[1] = gpMarioOriginal->mPosition.y;
		xyz[2] = gpMarioOriginal->mPosition.z;
		return 1;
	}
	if (!watchable(slot))
		return 0;
	xyz[0] = s_puppets[slot]->mPosition.x;
	xyz[1] = s_puppets[slot]->mPosition.y;
	xyz[2] = s_puppets[slot]->mPosition.z;
	return 1;
}
