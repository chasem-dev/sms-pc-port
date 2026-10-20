// The native mod API's game half (ext/modapi/README.md): an extension whose
// hooks become mod events (modh_dispatch in the host half), and the reads and
// writes of the game that the SmsApi tables offer. Built into the game's
// library with the game's flags and headers (C++03); every function runs on
// the game thread.
//
// Objects reach mods as JDrama::TNameRef pointers, the root of every named
// game object, so modg_object_* can ask what an object is (dynamic_cast)
// before reading it as one.
#include "../modapi_bridge.h"
#include "port_ext.h"
#include "port_framerate.h"

#include <Camera/Camera.hpp>
#include <Camera/CubeManagerBase.hpp>
#include <JSystem/JDrama/JDRNameRefGen.hpp>
#include <JSystem/JDrama/JDRPlacement.hpp>
#include <MSound/MSound.hpp>
#include <Map/Map.hpp>
#include <MoveBG/Item.hpp>
#include <MoveBG/ItemManager.hpp>
#include <Player/Mario.hpp>
#include <Player/WaterGun.hpp>
#include <Player/Yoshi.hpp>
#include <Strategic/HitActor.hpp>
#include <System/Application.hpp>
#include <System/FlagManager.hpp>
#include <System/MarDirector.hpp>

#include <math.h>
#include <string.h>

namespace {

bool s_stageReady; // between a stage's setup and leaving it
uint32_t s_tick;   // ticks since the stage's setup

SmsObject obj(JDrama::TNameRef* p) { return (SmsObject)p; }
JDrama::TNameRef* ref(SmsObject o) { return (JDrama::TNameRef*)o; }

TMario* mario()
{
	return s_stageReady ? gpMarioOriginal : 0;
}

int gameMode()
{
	switch (gpApplication.mAppState) {
	case TApplication::APP_STATE_BOOT:
	case TApplication::APP_STATE_NLOGO:
		return SMS_MODE_BOOT;
	case TApplication::APP_STATE_GAMEPLAY:
		return SMS_MODE_STAGE;
	case TApplication::APP_STATE_MOVIE:
		return SMS_MODE_MOVIE;
	case TApplication::APP_STATE_TITLE:
		return SMS_MODE_TITLE;
	case TApplication::APP_STATE_MENU:
		return SMS_MODE_MENU;
	default:
		return SMS_MODE_OTHER;
	}
}

void setVec(SmsVec3* v, f32 x, f32 y, f32 z)
{
	v->x = x;
	v->y = y;
	v->z = z;
}

// ------------------------------------------------------------------ hooks

void frame()
{
	static uint32_t s_frame;
	if (gameMode() != SMS_MODE_STAGE)
		s_stageReady = false;
	modh_frame(s_frame++, gameMode());
}

// The pause menu, the map and the save screens: the stage stands still (its
// objects get no move cue), though its ticks still run.
bool paused()
{
	if (!s_stageReady || !gpMarDirector)
		return false;
	switch (gpMarDirector->mState) {
	case TMarDirector::STATE_PAUSE_MENU:
	case TMarDirector::STATE_CARD_SAVE:
	case TMarDirector::STATE_UNK12:
	case TMarDirector::STATE_GUIDE:
		return true;
	default:
		return false;
	}
}

void afterMovement()
{
	if (!s_stageReady || paused())
		return;
	SmsEvTick ev;
	ev.tick             = s_tick++;
	ev.ticks_per_second = port_active_frame_rate > 0 ? port_active_frame_rate : 30;
	ev.dt               = 1.0f / (float)ev.ticks_per_second;
	modh_dispatch(SMS_EV_TICK, &ev, sizeof ev);
}

void stageSetup()
{
	s_stageReady = true;
	s_tick       = 0;
	SmsEvStage ev;
	ev.area    = gpApplication.mCurrArea.unk0;
	ev.episode = gpApplication.mCurrArea.unk1;
	modh_dispatch(SMS_EV_STAGE_LOADED, &ev, sizeof ev);
}

int allowNextStage(unsigned code, void* actor)
{
	if (!modh_wants(SMS_EV_STAGE_EXIT_REQUEST))
		return 1;
	SmsEvStageExit ev;
	ev.code    = code;
	ev.area    = (int)((code >> 8) & 0xFF) - 1;
	ev.episode = (int)(code & 0xFF);
	ev.actor   = actor ? obj((JDrama::TActor*)actor) : 0;
	return !modh_dispatch(SMS_EV_STAGE_EXIT_REQUEST, &ev, sizeof ev);
}

// An event whose payload is a stage the handlers may change, applied to `seq`.
void stageEvent(uint32_t type, TGameSequence& seq)
{
	if (!modh_wants(type))
		return;
	SmsEvStage ev;
	ev.area    = seq.unk0;
	ev.episode = seq.unk1;
	modh_dispatch(type, &ev, sizeof ev);
	if (ev.area != seq.unk0 || ev.episode != seq.unk1) {
		seq.unk0    = (u8)ev.area;
		seq.unk1 = (u8)ev.episode;
	}
}

void moveStage()
{
	stageEvent(SMS_EV_STAGE_LEAVING, gpApplication.mNextArea);
	s_stageReady = false;
}

void missNextArea() { stageEvent(SMS_EV_LIFE_LOST, gpApplication.mNextArea); }
void afterFileSelect() { stageEvent(SMS_EV_FILE_SELECTED, gpApplication.mNextArea); }
int takeGameOver() { return modh_dispatch(SMS_EV_GAME_OVER, 0, 0); }
int keepCoinsOnMiss() { return modh_dispatch(SMS_EV_COINS_RESET, 0, 0); }

void itemTaken(void* item, void* taker)
{
	if (!modh_wants(SMS_EV_ITEM_COLLECTED))
		return;
	TItem* it = (TItem*)item;
	SmsEvItem ev;
	ev.item       = obj(it);
	ev.taker      = taker ? obj((THitActor*)taker) : 0;
	ev.actor_type = it->getActorType();
	ev.kind       = dynamic_cast<TCoinBlue*>(it) ? SMS_ITEM_BLUE_COIN
	                : dynamic_cast<TCoinRed*>(it) ? SMS_ITEM_RED_COIN
	                : dynamic_cast<TCoin*>(it)    ? SMS_ITEM_COIN
	                                              : SMS_ITEM_OTHER;
	setVec(&ev.position, it->mPosition.x, it->mPosition.y, it->mPosition.z);
	modh_dispatch(SMS_EV_ITEM_COLLECTED, &ev, sizeof ev);
}

int allowSaveStates()
{
	SmsEvSaveState ev;
	ev.reason = 0;
	return !modh_dispatch(SMS_EV_SAVE_STATE_REQUEST, &ev, sizeof ev);
}

const char* saveStatesOffReason() { return modh_save_state_reason(); }

int hidePlayerModel(void* m)
{
	if (!modh_wants(SMS_EV_HIDE_PLAYER_MODEL))
		return 0;
	SmsEvObject ev;
	ev.object = obj((TMario*)m);
	return modh_dispatch(SMS_EV_HIDE_PLAYER_MODEL, &ev, sizeof ev);
}

void padRead(void* status) { modh_pad_read((SmsPadStatus*)status); }

int demoCamera(const char* name, const float* pos, int frames, float, int, int, void* actor, unsigned)
{
	if (!modh_wants(SMS_EV_CUTSCENE))
		return 0;
	SmsEvCutscene ev;
	ev.name         = name ? name : "";
	ev.has_position = pos != 0;
	if (pos)
		setVec(&ev.position, pos[0], pos[1], pos[2]);
	else
		setVec(&ev.position, 0, 0, 0);
	ev.frames = frames;
	ev.actor  = actor ? obj((JDrama::TActor*)actor) : 0;
	modh_dispatch(SMS_EV_CUTSCENE, &ev, sizeof ev);
	return 0; // mods hear of it; none leaves it out
}

void pollutionClean(float x, float y, float z, float size)
{
	if (!modh_wants(SMS_EV_GOOP_CLEANED))
		return;
	SmsEvGoop ev;
	setVec(&ev.position, x, y, z);
	ev.size = size;
	modh_dispatch(SMS_EV_GOOP_CLEANED, &ev, sizeof ev);
}

void objectMove(void* object, int after)
{
	if (!modh_wants(SMS_EV_OBJECT_MOVE))
		return;
	SmsEvObjectMove ev;
	ev.object = obj((JDrama::TViewObj*)object);
	ev.after  = after;
	modh_dispatch(SMS_EV_OBJECT_MOVE, &ev, sizeof ev);
}

int objectHit(void* object, void* sender, unsigned message)
{
	if (!modh_wants(SMS_EV_OBJECT_HIT))
		return 0;
	SmsEvHit ev;
	ev.object  = object ? obj((THitActor*)object) : 0;
	ev.sender  = sender ? obj((THitActor*)sender) : 0;
	ev.message = message;
	return modh_dispatch(SMS_EV_OBJECT_HIT, &ev, sizeof ev);
}

// The field of view a mod gave the camera last tick, and the camera's own: the
// camera's mFovy takes the mod's (the culling and the screen effects read it
// too, not only the projection), and gets its own back the next tick unless
// the camera set a new one itself (cutscenes keep theirs from tick to tick).
f32 s_fovyGiven, s_fovyOwn;

void camera(void* cam, float* eye, float* at, float* fovy)
{
	if (cam != (void*)gpCamera)
		return;
	CPolarSubCamera* c = (CPolarSubCamera*)cam;
	if (s_fovyGiven != 0.0f) {
		if (c->mFovy == s_fovyGiven)
			c->mFovy = s_fovyOwn;
		s_fovyGiven = 0.0f;
		*fovy       = c->mFovy;
	}
	if (!modh_wants(SMS_EV_CAMERA))
		return;
	SmsEvCamera ev;
	setVec(&ev.position, eye[0], eye[1], eye[2]);
	setVec(&ev.target, at[0], at[1], at[2]);
	ev.fovy   = *fovy;
	ev.aspect = c->mAspect;
	ev.roll   = 0;
	modh_dispatch(SMS_EV_CAMERA, &ev, sizeof ev);
	eye[0] = ev.position.x;
	eye[1] = ev.position.y;
	eye[2] = ev.position.z;
	at[0]  = ev.target.x;
	at[1]  = ev.target.y;
	at[2]  = ev.target.z;
	// a field of view the projection can make
	if (ev.fovy > 1.0f && ev.fovy < 179.0f && ev.fovy != *fovy) {
		s_fovyOwn   = c->mFovy;
		s_fovyGiven = ev.fovy;
		c->mFovy    = ev.fovy;
		*fovy       = ev.fovy;
	}
}

int playerStatus(void* m, unsigned* status, unsigned* arg, int force)
{
	if (m != (void*)mario() || !modh_wants(SMS_EV_PLAYER_STATUS))
		return 0;
	SmsEvPlayerStatus ev;
	ev.from   = ((TMario*)m)->mStatus;
	ev.to     = *status;
	ev.arg    = *arg;
	ev.forced = force;
	if (modh_dispatch(SMS_EV_PLAYER_STATUS, &ev, sizeof ev))
		return 1;
	*status = ev.to;
	*arg    = ev.arg;
	return 0;
}

int playerDamage(void* m, int* amount)
{
	if (m != (void*)mario() || !modh_wants(SMS_EV_PLAYER_DAMAGE))
		return 0;
	TMario* p = (TMario*)m;
	SmsEvDamage ev;
	ev.amount     = *amount;
	ev.underwater = p->isUnderWater() || p->checkFlag(MARIO_FLAG_HELMET_FLW_CAMERA);
	if (modh_dispatch(SMS_EV_PLAYER_DAMAGE, &ev, sizeof ev))
		return 1;
	*amount = ev.amount;
	return 0;
}

void shineCollected(int shine, int total)
{
	SmsEvShine ev;
	ev.shine = shine;
	ev.total = total;
	modh_dispatch(SMS_EV_SHINE_COLLECTED, &ev, sizeof ev);
}

// Filled by name, so a hook added to PortExtension anywhere (main's
// particleEmit, before the mod API's) never shifts these into the wrong places.
PortExtension s_extension;

struct Register {
	Register()
	{
		memset(&s_extension, 0, sizeof s_extension);
		PortExtension& e      = s_extension;
		e.name                = "modapi";
		e.frame               = frame;
		e.afterMovement       = afterMovement;
		e.stageSetup          = stageSetup;
		e.allowNextStage      = allowNextStage;
		e.moveStage           = moveStage;
		e.missNextArea        = missNextArea;
		e.takeGameOver        = takeGameOver;
		e.keepCoinsOnMiss     = keepCoinsOnMiss;
		e.afterFileSelect     = afterFileSelect;
		e.itemTaken           = itemTaken;
		e.allowSaveStates     = allowSaveStates;
		e.saveStatesOffReason = saveStatesOffReason;
		e.hidePlayerModel     = hidePlayerModel;
		e.padRead             = padRead;
		e.demoCamera          = demoCamera;
		e.pollutionClean      = pollutionClean;
		e.objectMove          = objectMove;
		e.objectHit           = objectHit;
		e.camera              = camera;
		e.playerStatus        = playerStatus;
		e.playerDamage        = playerDamage;
		e.shineCollected      = shineCollected;
		port_ext_register(&s_extension);
	}
} s_register;

} // namespace

// ------------------------------------------------------------------ reads and writes

extern "C" void modg_state(SmsGameState* out)
{
	SmsGameState s;
	memset(&s, 0, sizeof s);
	s.size             = sizeof s;
	s.mode             = gameMode();
	s.stage_ready      = s_stageReady && gpMarioOriginal;
	s.area             = s.stage_ready ? gpApplication.mCurrArea.unk0 : -1;
	s.episode          = s.stage_ready ? gpApplication.mCurrArea.unk1 : -1;
	s.tick             = s_tick;
	s.ticks_per_second = port_active_frame_rate > 0 ? port_active_frame_rate : 30;
	s.save_file        = gpApplication.mSaveFile;
	s.paused           = paused() ? 1 : 0;
	// the host half fills in frame and eclipse
	const uint32_t n = out->size && out->size < sizeof s ? out->size : (uint32_t)sizeof s;
	memcpy(out, &s, n);
	out->size = n;
}

extern "C" int32_t modg_flag_get(uint32_t id) { return TFlagManager::getInstance()->getFlag(id); }

extern "C" void modg_flag_set(uint32_t id, int32_t value) { TFlagManager::getInstance()->setFlag(id, value); }

extern "C" int modg_warp(int area, int episode)
{
	if (!s_stageReady || !gpMarDirector || area < 0 || area > 254 || episode < 0 || episode > 255)
		return 0;
	gpMarDirector->setNextStage((u16)(((area + 1) << 8) | episode), 0);
	return 1;
}

// The stage's cube sets (SmsCubeSet's order).
static TCubeManagerBase* cubeSet(int set)
{
	if (!s_stageReady)
		return nullptr;
	switch (set) {
	case 0: return gpCubeCamera;
	case 1: return gpCubeArea;
	case 2: return gpCubeFastA;
	case 3: return gpCubeFastB;
	case 4: return gpCubeFastC;
	case 5: return gpCubeMirror;
	case 6: return gpCubeWire;
	case 7: return gpCubeStream;
	case 8: return gpCubeShadow;
	case 9: return gpCubeSoundChange;
	case 10: return gpCubeSoundEffect;
	default: return nullptr;
	}
}

extern "C" int modg_cube_count(int set)
{
	TCubeManagerBase* c = cubeSet(set);
	return c ? (int)c->unk10 : 0;
}

extern "C" int modg_in_cube(int set, int index, float x, float y, float z)
{
	TCubeManagerBase* c = cubeSet(set);
	if (!c || index < 0 || index >= (int)c->unk10)
		return -1;
	Vec v = {x, y, z};
	return c->isInCube(v, index) ? 1 : 0;
}

extern "C" SmsObject modg_find_object(const char* name)
{
	if (!name || !s_stageReady || !JDrama::TNameRefGen::getInstance() || !JDrama::TNameRefGen::getInstance()->getRootNameRef())
		return 0;
	return obj(JDrama::TNameRefGen::getInstance()->getRootNameRef()->search(name));
}

extern "C" const char* modg_object_name(SmsObject o)
{
	return o && ref(o)->getName() ? ref(o)->getName() : "";
}

extern "C" int modg_object_position(SmsObject o, SmsVec3* out)
{
	JDrama::TPlacement* p = o ? dynamic_cast<JDrama::TPlacement*>(ref(o)) : 0;
	if (!p || !out)
		return 0;
	setVec(out, p->mPosition.x, p->mPosition.y, p->mPosition.z);
	return 1;
}

extern "C" int modg_object_set_position(SmsObject o, const SmsVec3* pos)
{
	JDrama::TPlacement* p = o ? dynamic_cast<JDrama::TPlacement*>(ref(o)) : 0;
	if (!p || !pos)
		return 0;
	p->mPosition.set(pos->x, pos->y, pos->z);
	return 1;
}

extern "C" int modg_player_get(SmsPlayer* out)
{
	SmsPlayer s;
	memset(&s, 0, sizeof s);
	s.size   = sizeof s;
	TMario* m = mario();
	if (m) {
		s.valid = 1;
		setVec(&s.position, m->mPosition.x, m->mPosition.y, m->mPosition.z);
		setVec(&s.velocity, m->mVel.x, m->mVel.y, m->mVel.z);
		s.forward_speed = m->mForwardVel;
		s.face_angle[0] = m->mFaceAngle.x;
		s.face_angle[1] = m->mFaceAngle.y;
		s.face_angle[2] = m->mFaceAngle.z;
		s.yaw           = (float)m->mFaceAngle.y * (360.0f / 65536.0f);
		s.health        = m->getHealth();
		s.max_health    = m->mDeParams.mHPMax.get();
		s.has_fludd     = m->checkFlag(MARIO_FLAG_HAS_FLUDD) ? 1 : 0;
		if (m->mWaterGun) {
			s.water     = m->mWaterGun->getCurrentWater();
			s.max_water = m->mWaterGun->getMaxWater();
			s.nozzle    = m->mWaterGun->mSecondNozzle;
		}
		s.status       = m->mStatus;
		s.status_timer = m->mStatusTimer;
		s.on_ground    = (m->mStatus & MARIO_STATUS_FLAG_JUMPING) || (m->mStatus & MARIO_STATUS_FLAG_SWIMMING) ? 0 : 1;
		s.in_water     = m->checkFlag(MARIO_FLAG_IN_ANY_WATER) || (m->mStatus & MARIO_STATUS_FLAG_SWIMMING) ? 1 : 0;
		s.on_yoshi     = m->onYoshi() ? 1 : 0;
		setVec(&s.floor, m->mFloorPosition.x, m->mFloorPosition.y, m->mFloorPosition.z);
		s.object = obj(m);
	}
	const uint32_t n = out->size && out->size < sizeof s ? out->size : (uint32_t)sizeof s;
	memcpy(out, &s, n);
	out->size = n;
	return s.valid;
}

extern "C" int modg_player_set_position(const SmsVec3* pos)
{
	TMario* m = mario();
	if (!m || !pos)
		return 0;
	m->mPosition.set(pos->x, pos->y, pos->z);
	return 1;
}

extern "C" int modg_player_set_velocity(const SmsVec3* vel)
{
	TMario* m = mario();
	if (!m || !vel)
		return 0;
	m->mVel.set(vel->x, vel->y, vel->z);
	return 1;
}

extern "C" int modg_player_set_forward_speed(float speed)
{
	TMario* m = mario();
	if (!m)
		return 0;
	m->mForwardVel = speed;
	return 1;
}

extern "C" int modg_player_set_health(int health)
{
	TMario* m = mario();
	if (!m)
		return 0;
	const int max = m->mDeParams.mHPMax.get();
	m->mHealth    = (s16)(health < 0 ? 0 : health > max ? max : health);
	return 1;
}

extern "C" int modg_player_set_water(int water)
{
	TMario* m = mario();
	if (!m || !m->mWaterGun)
		return 0;
	const int max                  = m->mWaterGun->getMaxWater();
	m->mWaterGun->mCurrentWater = water < 0 ? 0 : water > max ? max : water;
	return 1;
}

extern "C" int modg_player_set_status(uint32_t status, uint32_t arg)
{
	TMario* m = mario();
	return m ? m->changePlayerStatus(status, arg, false) != 0 : 0;
}

extern "C" int modg_camera_get(SmsCamera* out)
{
	SmsCamera s;
	memset(&s, 0, sizeof s);
	s.size = sizeof s;
	CPolarSubCamera* c = s_stageReady ? gpCamera : 0;
	if (c) {
		s.valid = 1;
		const Vec& eye = c->getUnk124Vec();
		const Vec& at  = c->getUnk148Vec();
		setVec(&s.position, eye.x, eye.y, eye.z);
		setVec(&s.target, at.x, at.y, at.z);
		setVec(&s.up, c->mUp.x, c->mUp.y, c->mUp.z);
		s.fovy   = c->mFovy;
		s.aspect = c->mAspect;
		s.near_z = c->mNear;
		s.far_z  = c->mFar;
		MtxPtr view = c->getUnk1EC();
		MtxPtr proj = c->getUnk16C();
		for (int r = 0; r < 3; r++)
			for (int k = 0; k < 4; k++)
				s.view[r][k] = view[r][k];
		for (int r = 0; r < 4; r++)
			for (int k = 0; k < 4; k++)
				s.proj[r][k] = proj[r][k];
	}
	const uint32_t n = out->size && out->size < sizeof s ? out->size : (uint32_t)sizeof s;
	memcpy(out, &s, n);
	out->size = n;
	return s.valid;
}

extern "C" void modg_play_system_sound(uint32_t id)
{
	if (MSound* sound = SMSGetMSound())
		sound->startSoundSystemSE(id, 0, 0, 0);
}

extern "C" void* modg_native(const char* name)
{
	if (!name)
		return 0;
	if (!strcmp(name, "gpApplication"))
		return &gpApplication;
	if (!strcmp(name, "TFlagManager"))
		return TFlagManager::getInstance();
	if (!s_stageReady)
		return 0;
	if (!strcmp(name, "gpMarioOriginal"))
		return gpMarioOriginal;
	if (!strcmp(name, "gpMarDirector"))
		return gpMarDirector;
	if (!strcmp(name, "gpCamera"))
		return gpCamera;
	if (!strcmp(name, "gpItemManager"))
		return gpItemManager;
	if (!strcmp(name, "gpMap"))
		return gpMap;
	return 0;
}
