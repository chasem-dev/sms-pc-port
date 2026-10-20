// Servers' game side (mp_server.hpp): what a server mod on the host has this
// game do, through the commands of mp_game_api.h. Every game carries them
// out, so a player joining a server needs nothing but the game: the server
// mod only says when and where (a banner now, the clock counting, the player
// onto a Blooper there), and this game shows and does it with its own models,
// sounds and HUD. Game code, built with the game's flags: C++03, fixed arrays.
//
// A command's state lasts for the stage session it came in: a new session
// (the host moving everyone on) starts this game afresh, and the host replays
// the session's sticky commands as each player arrives in it.
#include "mp_server.hpp"

#include "../mp_game_api.h"
#include "mp_puppet.hpp"
#include "port_framerate.h"

#include <Enemy/Conductor.hpp>
#include <GC2D/ConsoleStr.hpp>
#include <GC2D/GCConsole2.hpp>
#include <JSystem/JDrama/JDRViewObjPtrList.hpp>
#include <JSystem/JKernel/JKRFileLoader.hpp>
#include <MSound/MSound.hpp>
#include <MSound/MSoundBGM.hpp>
#include <MoveBG/Item.hpp>
#include <MoveBG/MapObjRicco.hpp>
#include <Player/Mario.hpp>
#include <Strategic/LiveManager.hpp>
#include <System/Application.hpp>
#include <System/EventWatcher.hpp>
#include <System/FlagManager.hpp>
#include <System/MarDirector.hpp>
#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern "C" void GXPC_ExtOverlaySet(int panel, const char* text) __attribute__((weak));
void MpShowNotice(const char* text); // mp_game.cpp

namespace {

const int kOverlayBanner = 2;     // GXPC_EXT_OVERLAY_BANNER: big, in the middle
const int kMountTries    = 30;    // frames to keep trying to get on (held up landing, talking)
const u32 kShineType     = 0x20000013;

TMarDirector* director()
{
	return gpApplication.mAppState == TApplication::APP_STATE_GAMEPLAY ? gpMarDirector : nullptr;
}

unsigned clockMs()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void overlay(int panel, const char* text)
{
	if (GXPC_ExtOverlaySet)
		GXPC_ExtOverlaySet(panel, text);
}

// what the commands of this session have set up
uint32_t s_session;            // the session they belong to
char s_banner[MPG_CMD_TEXT + 8]; // "#rrggbb text", as the overlay takes it
char s_bannerShown[MPG_CMD_TEXT + 8];
char s_status[MPG_CMD_TEXT];
int s_timerMode;               // MPG_CMD_TIMER's: 0 hidden, 1 stopped, 2 up, 3 down
s32 s_timerBase;               // hundredths, as of s_timerAt
unsigned s_timerAt;
bool s_timerShown;             // the console shows it (this stage's console)
bool s_hold;
int s_watch = -1;              // the slot watched; -1 none
bool s_watching;               // Mario out of play, riding with them
bool s_stay;
MpgCmd s_mount;                // getting on a Blooper, until on (s_mountTries)
int s_mountTries;
u32 s_bgm;                     // the music a command started
unsigned s_readyAt;            // when to start "Ready... Go!" (s_readyDue)
bool s_readyDue;
// the host's own: a stage a server mod asked for
int s_loadArea = -1, s_loadEpisode = -1;
bool s_loading;
int s_titleFrames;

void resetSession()
{
	s_banner[0] = 0;
	s_status[0] = 0;
	s_timerMode = 0;
	s_hold      = false;
	s_watch     = -1;
	s_stay      = false;
	s_mountTries = 0;
	s_readyDue   = false;
	if (s_watching) {
		s_watching = false;
		if (TMario* m = gpMarioOriginal) {
			m->offFlag(MARIO_FLAG_IS_PERFORMING);
			m->offHitFlag(HIT_FLAG_NO_COLLISION);
		}
	}
}

bool surfing(TMario* m) { return m && m->checkStatusType(MARIO_STATUS_FLAG_UNK10000) && m->mSurfGesso; }

// ------------------------------------------------------------------- the HUD's clock

s32 timerNow()
{
	const s32 gone = (s32)((clockMs() - s_timerAt) / 10);
	switch (s_timerMode) {
	case 2: return s_timerBase + gone;
	case 3: return s_timerBase > gone ? s_timerBase - gone : 0;
	default: return s_timerBase;
	}
}

void updateTimer(TMarDirector* dir)
{
	TGCConsole2* console = dir ? dir->getConsole() : nullptr;
	if (!console)
		return;
	if (s_timerMode == 0) {
		if (s_timerShown)
			console->startDisappearTimer();
		s_timerShown = false;
		return;
	}
	if (!s_timerShown) {
		console->startAppearTimer(0, 0); // minutes, seconds, hundredths: set each frame
		s_timerShown = true;
	}
	console->setTimer(timerNow());
}

// The game's "Ready... Go!" (its letters are in every stage's HUD, though
// nothing in the game plays them): "Go!" comes kReadyFrames frames later.
const int kReadyFrames = 60;
int readyMs() { return kReadyFrames * 1000 / (port_active_frame_rate > 0 ? port_active_frame_rate : 30); }
void readyGo(TMarDirector* dir)
{
	TGCConsole2* console = dir ? dir->getConsole() : nullptr;
	TConsoleStr* str     = console ? console->unk94 : nullptr;
	if (!str || str->unk2B4 != 0)
		return;
	str->startAppearReady(); // "Go!" made ready (its panes hidden, its clock at 0)...
	str->unk2B4 = 5;         // ...after "Ready..."
}

// ------------------------------------------------------------------- the stage's actors

// Each live actor of types lo to hi: f(actor) for each.
template <class F> void eachActor(u32 lo, u32 hi, F& f)
{
	if (!gpConductor)
		return;
	for (JGadget::TList<TLiveManager*>::iterator it = gpConductor->unk10.begin(); it != gpConductor->unk10.end(); ++it)
		for (int i = 0; *it && i < (*it)->getObjNum(); i++) {
			TLiveActor* a = (*it)->getObj(i);
			if (!a)
				continue;
			const u32 type = (u32)a->getActorType();
			if (type >= lo && type <= hi)
				f(a);
		}
}

struct SetActor {
	int mode;
	int count;
	void operator()(TLiveActor* a)
	{
		count++;
		if (mode == 2) {
			if (TShine* shine = dynamic_cast<TShine*>(a)) {
				if (!shine->checkLiveFlag(LIVE_FLAG_DEAD))
					shine->kill();
				return;
			}
		}
		// out of the stage, as event scripts take things away
		// (setDead4LiveActor; a lone NO_COLLISION an item's perform undoes)
		if (mode) {
			a->onLiveFlag(LIVE_FLAG_DEAD);
			a->onHitFlag(HIT_FLAG_NO_COLLISION);
		} else {
			a->offLiveFlag(LIVE_FLAG_DEAD);
			a->offHitFlag(HIT_FLAG_NO_COLLISION);
		}
	}
};

struct FirstActor {
	TLiveActor* found;
	void operator()(TLiveActor* a)
	{
		if (!found)
			found = a;
	}
};

// Onto a Blooper of this game's own, at the command's place: as touching it does.
bool mount(const MpgCmd& c)
{
	TMario* m = gpMarioOriginal;
	if (!m || MpServerDedicated())
		return true;
	if (surfing(m))
		return true;
	FirstActor first = {nullptr};
	eachActor((u32)c.i[0], (u32)c.i[1], first);
	TItem* blooper = (TItem*)first.found;
	if (!blooper)
		return false;
	blooper->offLiveFlag(LIVE_FLAG_DEAD);
	blooper->offHitFlag(HIT_FLAG_NO_COLLISION);
	blooper->mPosition.set(c.f[0], c.f[1], c.f[2]);
	m->mPosition   = blooper->mPosition;
	m->mForwardVel = 0.0f;
	m->mVel.set(0.0f, 0.0f, 0.0f);
	m->mFaceAngle.y = (s16)(c.f[3] * (65536.0f / 360.0f));
	blooper->taken(m);
	return surfing(m);
}

// The stage's event script loaded from `path`, stopped (a course's time trial,
// a story event).
void stopScript(TMarDirector* dir, const char* path)
{
	void* script = JKRGetResource(path);
	bool stopped = false;
	if (script && dir->unk80) {
		JGadget::TList_pointer<JDrama::TViewObj*>& objs = dir->unk80->getChildren();
		for (JGadget::TList_pointer<JDrama::TViewObj*>::iterator it = objs.begin(); it != objs.end(); ++it) {
			TEventWatcher* w = dynamic_cast<TEventWatcher*>(*it);
			if (w && w->mBinary && (void*)w->mBinary->getHeader() == script) {
				w->mInterp = nullptr;
				stopped    = true;
			}
		}
	}
	fprintf(stderr, "[server] script %s %s\n", path, stopped ? "stopped" : "not running here");
}

// ------------------------------------------------------------------- each command

void run(TMarDirector* dir, const MpgCmd& c)
{
	TMario* m = gpMarioOriginal;
	switch (c.type) {
	case MPG_CMD_BANNER:
		if (c.text[0])
			snprintf(s_banner, sizeof s_banner, "#%06x %s", (unsigned)((u32)c.i[0] >> 8), c.text);
		else
			s_banner[0] = 0;
		break;
	case MPG_CMD_NOTICE:
		MpShowNotice(c.text);
		break;
	case MPG_CMD_STATUS:
		snprintf(s_status, sizeof s_status, "%s", c.text);
		break;
	case MPG_CMD_TIMER:
		s_timerMode = c.i[0] >= 0 && c.i[0] <= 3 ? c.i[0] : 0;
		s_timerBase = c.i[1] > 0 ? c.i[1] : 0;
		s_timerAt   = clockMs();
		break;
	case MPG_CMD_READY_GO:
		// "Go!" i[0] ms from now: "Ready..." started that long before at this
		// game's frame rate (its letters move a frame at a time)
		s_readyAt = clockMs() + (c.i[0] > readyMs() ? (unsigned)(c.i[0] - readyMs()) : 0);
		s_readyDue = true;
		break;
	case MPG_CMD_SOUND:
		if (c.i[0] != 0)
			SMSGetMSound()->startSoundSystemSE((u32)c.i[0], 0, nullptr, 0);
		break;
	case MPG_CMD_MUSIC:
		if (c.i[0] != 0) {
			s_bgm = (u32)c.i[0];
			MSBgm::startBGM(s_bgm);
		} else if (s_bgm) {
			MSBgm::stopBGM(s_bgm, 30);
			s_bgm = 0;
		}
		break;
	case MPG_CMD_WARP:
		if (m && !MpServerDedicated()) {
			JGeometry::TVec3<f32> at(c.f[0], c.f[1], c.f[2]);
			m->warpRequest(at, c.f[3]);
		}
		break;
	case MPG_CMD_HOLD:
		s_hold = c.i[0] != 0;
		break;
	case MPG_CMD_MOUNT:
		s_mount      = c;
		s_mountTries = mount(c) ? 0 : 1;
		break;
	case MPG_CMD_WATCH:
		s_watch = c.i[0] >= 0 && c.i[0] < MPG_MAX_PLAYERS && c.i[0] != mpg_slot() ? c.i[0] : -1;
		if (s_watch < 0 && s_watching && m) {
			s_watching = false;
			m->offFlag(MARIO_FLAG_IS_PERFORMING);
			m->offHitFlag(HIT_FLAG_NO_COLLISION);
		}
		break;
	case MPG_CMD_ACTORS: {
		SetActor set = {c.i[2], 0};
		eachActor((u32)c.i[0], (u32)c.i[1], set);
		fprintf(stderr, "[server] actors %x-%x: %d %s\n", (unsigned)c.i[0], (unsigned)c.i[1], set.count,
		        c.i[2] == 0 ? "shown" : c.i[2] == 1 ? "hidden" : "gone");
		break;
	}
	case MPG_CMD_STOP_SCRIPT:
		stopScript(dir, c.text);
		break;
	case MPG_CMD_STAY:
		s_stay = c.i[0] != 0;
		break;
	default:
		break;
	}
}

// ------------------------------------------------------------------- the host's own

// A dedicated server's player: out of play, unseen, touching nothing.
void bench()
{
	if (TMario* m = gpMarioOriginal) {
		m->onFlag(MARIO_FLAG_IS_PERFORMING);
		m->onHitFlag(HIT_FLAG_NO_COLLISION);
	}
}

// A dedicated server goes from the title straight to its stage, with a new
// save's flags (as the game starts with): the one a server mod asked for,
// else SMS_MULTIPLAYER_STAGE ("area,episode"), else Delfino Plaza.
void serverTitle(TMarDirector* dir)
{
	if (!dir || gpApplication.mCurrArea.getStage() != 15 || dir->mState != TMarDirector::STATE_UNK4
	    || dir->checkFlag(TMarDirector::DIRECTOR_FLAG_STAGE_TRANSITION_PENDING)) {
		s_titleFrames = 0;
		return;
	}
	if (++s_titleFrames < 30)
		return; // the title's own setup first (about a second)
	s_titleFrames = 0;
	int area = 1, episode = 0;
	if (s_loadArea >= 0) {
		area    = s_loadArea;
		episode = s_loadEpisode;
		s_loadArea = -1;
	} else if (const char* stage = getenv("SMS_MULTIPLAYER_STAGE")) {
		sscanf(stage, "%d,%d", &area, &episode);
	}
	fprintf(stderr, "[server] from the title to area %d, episode %d\n", area, episode);
	TFlagManager::getInstance()->setFlag(0x40003, episode);
	dir->setNextStage((u16)(((area + 1) << 8) | episode), nullptr);
}

// The host to the stage a server mod asked for, afresh: once nothing else is
// on (a fade, a lost life's reload).
void hostLoad(TMarDirector* dir)
{
	if (s_loadArea < 0 || !dir || gpApplication.mCurrArea.getStage() == 15)
		return;
	if (dir->mState != TMarDirector::STATE_UNK4 || dir->checkFlag(TMarDirector::DIRECTOR_FLAG_STAGE_TRANSITION_PENDING))
		return;
	fprintf(stderr, "[server] everyone to area %d, episode %d (a new session)\n", s_loadArea, s_loadEpisode);
	s_loading = true;
	mpg_restart_session(); // a new session even for the stage played now
	TFlagManager::getInstance()->setFlag(0x40003, s_loadEpisode);
	dir->setNextStage((u16)(((s_loadArea + 1) << 8) | s_loadEpisode), nullptr);
	s_loadArea = -1;
}

} // namespace

bool MpServerDedicated() { return mpg_role() == MPG_ROLE_HOST && mpg_dedicated(); }

bool MpServerLoading() { return s_loading; }

const char* MpServerStatusLine() { return s_status; }

void MpServerStageSetup()
{
	s_timerShown = false; // the new stage's console
	s_loading    = false;
	s_watching   = false; // a new Mario
	s_mountTries = 0;
}

void MpServerFrame(TMarDirector* dir, bool arrived, bool playing)
{
	if (mpg_role() == MPG_ROLE_HOST) {
		int area, episode;
		if (mpg_take_load_stage(&area, &episode)) {
			s_loadArea    = area;
			s_loadEpisode = episode;
		}
		if (MpServerDedicated())
			serverTitle(dir);
		if (playing)
			hostLoad(dir);
	}
	// a new session: what the last one's commands set up is over
	const uint32_t session = mpg_session();
	if (session != s_session) {
		s_session = session;
		resetSession();
	}
	if (playing && dir) {
		(void)arrived;
		MpgCmd c;
		while (mpg_next_cmd(&c))
			run(dir, c);
		if (s_readyDue && (int)(clockMs() - s_readyAt) >= 0) {
			s_readyDue = false;
			readyGo(dir);
		}
		updateTimer(dir);
		if (s_mountTries > 0 && s_mountTries < kMountTries && !surfing(gpMarioOriginal))
			s_mountTries = mount(s_mount) ? 0 : s_mountTries + 1;
	}
	const char* banner = playing || s_banner[0] == 0 ? s_banner : "";
	if (strcmp(banner, s_bannerShown) != 0) {
		snprintf(s_bannerShown, sizeof s_bannerShown, "%s", banner);
		overlay(kOverlayBanner, s_bannerShown);
	}
}

void MpServerTick()
{
	TMario* m = gpMarioOriginal;
	if (!m)
		return;
	if (MpServerDedicated()) {
		bench();
		return;
	}
	// held: a Blooper ridden stops where it is, rather than surfing on unsteered
	if (s_hold && surfing(m))
		m->mForwardVel = 0.0f;
	// watching: out of play, riding with that player (the camera follows Mario)
	if (s_watch >= 0 && MpWatchable(s_watch)) {
		TMpPuppet* p = MpPuppet(s_watch);
		s_watching   = true;
		m->onFlag(MARIO_FLAG_IS_PERFORMING);
		m->onHitFlag(HIT_FLAG_NO_COLLISION);
		m->mPosition        = p->mPosition;
		const MpgPose& pose = p->pose();
		m->mFaceAngle.y     = (s16)(atan2f(pose.mtx[0][2], pose.mtx[2][2]) * (32768.0f / 3.14159265f));
	}
}

void MpServerPadRead(void* status)
{
	if (!s_hold && !s_watching)
		return;
	PADStatus* pads  = (PADStatus*)status;
	pads[0].button   = 0;
	pads[0].stickX = pads[0].stickY = pads[0].substickX = pads[0].substickY = 0;
	pads[0].triggerLeft = pads[0].triggerRight = 0;
	pads[0].analogA = pads[0].analogB = 0;
}

int MpServerAllowLeave(int area)
{
	if (!s_stay || s_loading || area == gpApplication.mCurrArea.getStage())
		return 1; // free, the host's own stage afresh, or back into this stage (a lost life's reload)
	int target, episode;
	return mpg_role() == MPG_ROLE_CLIENT && mpg_target(&target, &episode) && area == target; // following the host
}

int MpServerAfterFileSelect()
{
	if (mpg_role() != MPG_ROLE_HOST || s_loadArea < 0)
		return 0;
	TFlagManager::getInstance()->setFlag(0x40003, s_loadEpisode);
	gpApplication.mNextArea.set((u8)s_loadArea, (u8)s_loadEpisode, 0);
	s_loadArea = -1;
	return 1;
}
