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
#include "mp_puppet.hpp"
#include "port_ext.h"

#include <GC2D/GCConsole2.hpp>
#include <M3DUtil/M3UModelMario.hpp>
#include <M3DUtil/MActor.hpp>
#include <MoveBG/Item.hpp>
#include <MoveBG/ItemManager.hpp>
#include <Player/Mario.hpp>
#include <Player/MarioCap.hpp>
#include <Player/WaterGun.hpp>
#include <Player/Yoshi.hpp>
#include <Strategic/Strategy.hpp>
#include <System/Application.hpp>
#include <System/FlagManager.hpp>
#include <System/MarDirector.hpp>
#include <System/StageUtil.hpp>
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
TMpPuppet* s_puppets[4];
TMpPuppet* s_self; // our own character, when we are not Mario
char s_notice[256];
int s_noticeFrames, s_statusFrames;
int s_refusedFrames;
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
	s_itemCount = 0;
	TItemManager* mgr = items();
	if (!mgr)
		return;
	s_itemCount = mgr->getObjNum() < kMaxItems ? mgr->getObjNum() : kMaxItems;
	for (int i = 0; i < s_itemCount; i++) {
		TLiveActor* a = mgr->getObj(i);
		s_itemAlive[i] = a && !a->checkLiveFlag(LIVE_FLAG_DEAD);
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

void MpCapturePose(TMario* m, MpgPose* out)
{
	MpgPose& p = *out;
	memset(&p, 0, sizeof p);
	p.session = mpg_session();
	if (!m || !m->getM3UModel())
		return;
	M3UModelMario* mm = m->getM3UModel();
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
}

namespace {

void sendPose()
{
	MpgPose p;
	MpCapturePose(gpMarioOriginal, &p);
	mpg_set_pose(&p);
}

// ------------------------------------------------------------------- warps

const char* worldName(int area)
{
	static const char* const names[] = {"the Airstrip", "Delfino Plaza", "Bianco Hills", "Ricco Harbor",
	                                    "Gelato Beach", "Pinna Park",   "Sirena Beach", "Noki Bay",
	                                    "Pianta Village", "Corona Mountain"};
	u8 w = area >= 0 && area < 61 ? SMS_getShineStage((u8)area) : 1;
	return names[w < 10 ? w : 1];
}

void followHost(TMarDirector* dir)
{
	int area, episode;
	if (!mpg_flags_ready() || !mpg_target(&area, &episode) || mpg_in_target())
		return;
	// the host's flags are the game's now (synced this frame): load their stage
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

// A client got a Shine: the host leaves with everyone, as it would have.
void hostShineExit(TMarDirector* dir)
{
	if (!mpg_take_shine_exit())
		return;
	dir->onFlag(TMarDirector::DIRECTOR_FLAG_SHINE_TAKEN | TMarDirector::DIRECTOR_FLAG_CARD_SAVE_PENDING
	            | TMarDirector::DIRECTOR_FLAG_STAGE_TRANSITION_PENDING);
	dir->unk261 = 6;
	gpApplication.mNextArea.set(1, 0xFF, 0);
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
	if (paused || s_statusFrames > 0) {
		char status[512];
		mpg_status(status, sizeof status);
		overlay(OVERLAY_STATUS, status);
	} else {
		overlay(OVERLAY_STATUS, "");
	}
}

// ------------------------------------------------------------------- hooks

void frame()
{
	if (s_role < 0)
		s_role = mpg_start();
	if (!enabled())
		return;
	TMarDirector* dir = director();
	const int area    = gpApplication.mCurrArea.getStage();
	MpgGame g;
	g.playing = playing();
	g.area    = area;
	g.episode = gpApplication.mCurrArea.getScenario();
	g.world   = SMS_getShineStage((u8)area);
	g.paused  = dir && dir->mState == TMarDirector::STATE_PAUSE_MENU;
	mpg_frame(&g);

	s_wasPlaying = s_playing;
	s_playing    = g.playing != 0;
	if (s_playing && !s_wasPlaying)
		snapshotItems();
	if (s_role == MPG_ROLE_CLIENT)
		skipTitle(dir);
	if (inStage()) {
		syncFlags();
		takeRemovals();
		sendPose();
		if (s_playing) {
			if (s_role == MPG_ROLE_CLIENT)
				followHost(dir);
			else
				hostShineExit(dir);
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
	if (!enabled())
		return;
	s_readyDir = gpMarDirector;
	if (gpApplication.mCurrArea.getStage() == 15 || !gpStrategy)
		return;
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
		// a puppet takes about 400 KB of the stage's memory; leave the stage
		// room for what it makes as it plays
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

int allowNextStage(unsigned code, void*)
{
	int area, episode;
	if (s_role != MPG_ROLE_CLIENT || !mpg_target(&area, &episode))
		return 1;
	const int next = code >= 0x100 ? (int)(code >> 8) - 1 : (int)code;
	if (next == area)
		return 1;
	if (!s_refusedFrames)
		showNotice("Only the host can take everyone to another area");
	s_refusedFrames = 60;
	return 0;
}

void moveStage()
{
	int area, episode;
	if (s_role != MPG_ROLE_CLIENT || !mpg_target(&area, &episode))
		return;
	// wherever this game meant to go, it goes where the host is
	TGameSequence& next = gpApplication.mNextArea;
	if (next.getStage() != area || next.getScenario() != episode) {
		next.set((u8)area, (u8)episode, 0);
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
	const int n = mgr->getObjNum() < s_itemCount ? mgr->getObjNum() : s_itemCount;
	for (int i = 0; i < n; i++)
		if ((void*)mgr->getObj(i) == item) {
			if (s_itemAlive[i])
				mpg_item_taken(itemKey(i, mgr->getObj(i)));
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
	"multiplayer",   frame,           stageSetup,      allowNextStage, moveStage,
	missNextArea,    keepCoinsOnMiss, afterFileSelect, itemTaken,      allowSaveStates,
	saveStatesOffReason, hidePlayerModel,
};

struct Register {
	Register() { port_ext_register(&s_extension); }
} s_register;

} // namespace
