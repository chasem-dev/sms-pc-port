// Split-screen co-op (SMS_COOP=1, ext/coop/README.md): a second player in the
// same game, on the same screen. Player 2 is a second TMario, made from the
// stage's own Mario record while the stage loads, green as Luigi, reading
// controller 2 (platform/pad: the controller that joined with Start). A
// second CPolarSubCamera follows them. While player 2 runs (their move, their
// camera's, and the drawing of their view), the game's player globals
// (gpMarioOriginal, gpMarioPos and the rest, gpCamera and its helpers) are
// theirs, so the game's own Mario code plays them as it plays Mario. Each
// view is drawn into its half of the frame (GXPC_SetSplitView), the HUD once
// over both. Objects near player 2 move as if player 2 were Mario
// (objectMove), so enemies go after whoever is nearest.
// Game code: C++03.
#include "port_ext.h"
#include "port_framerate.h"
#include "../../multiplayer/mp_recolour.h"

#include <Camera/Camera.hpp>
#include <Camera/CameraMarioData.hpp>
#include <Camera/CameraOption.hpp>
#include <Camera/CameraShake.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DTexture.hpp>
#include <JSystem/JDrama/JDRNameRefGen.hpp>
#include <JSystem/JDrama/JDRViewObjPtrList.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/JSupport/JSUMemoryInputStream.hpp>
#include <MarioUtil/DrawUtil.hpp>
#include <MoveBG/ItemManager.hpp>
#include <MoveBG/Item.hpp>
#include <MSound/MSound.hpp>
#include <M3DUtil/M3UModelMario.hpp>
#include <Player/Mario.hpp>
#include <Player/MarioAccess.hpp>
#include <Player/MarioCap.hpp>
#include <Strategic/LiveActor.hpp>
#include <Strategic/Strategy.hpp>
#include <System/Application.hpp>
#include <GC2D/BoundPane.hpp>
#include <GC2D/GCConsole2.hpp>
#include <JSystem/J2D/J2DPicture.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <System/FlagManager.hpp>
#include <System/MarDirector.hpp>
#include <System/MarioGamePad.hpp>
#include <System/PerformList.hpp>
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
// platform/pad (pad.cpp): the second player's controller
int port_coop_enabled(void);
int port_coop_player2(void);
void port_coop_set_joinable(int on);
void port_coop_leave(void);
unsigned port_coop_take_events(void);
int port_coop_can_join(void);
void port_coop_join_virtual(void);
extern int port_coop_in_p2;
// platform/gx
void GXPC_SetSplitView(int on, float x, float y, float w, float h) __attribute__((weak));
void GXPC_ExtOverlaySet(int panel, const char* text) __attribute__((weak));
// ext/multiplayer/game/mp_spawn.cpp: the stage's scene.bin records
int port_spawn_record_of(const void* object, const void** record, unsigned* size);
// decomp-patches/zzzzzz-coop-01: ViewFrustumClipCheck's other view
Mtx* port_coop_other_view = nullptr;
// ...and TPollutionManager::stampModel's: the second view's entry pass is on
int port_coop_second_entry = 0;
}

namespace {

const int kNoticeFrames = 30 * 5;
const int kOverlayNotice = 0; // GXPC_EXT_OVERLAY_NOTICE
const int kOverlayStatus = 1; // GXPC_EXT_OVERLAY_STATUS

// ------------------------------------------------------------ the players' globals

// The globals the game reads "the player" and "the camera" through.
struct Ctx {
	TMario* mario;
	CPolarSubCamera* cam;
	TCameraShake* shake;
	TCameraMarioData* camMario;
	TCameraOption* camOption;
};

Ctx s_p1, s_p2;
bool s_inP2;          // player 2's globals are in force
bool s_made;          // this stage has player 2 and their camera
bool s_active;        // player 2 is playing
bool s_split;         // the stage is drawn in two views this frame
bool s_wanted;        // SMS_COOP: co-op is on
int s_layout;         // 0 side by side, 1 one above the other
f32 s_baseAspect;     // the game camera's own aspect
TMarDirector* s_dir;  // the director whose stage has them
int s_respawnFrames;  // player 2 is down: the frame they are back on
// Lives are shared, as in multiplayer: player 2's lost life is one of the
// game's (TFlagManager 0x20001, the HUD's count). Lost on the last one,
// player 2 is out until the next life comes (a 1-Up), which brings them back.
const u32 kLivesFlag = 0x20001;

// A life taken from the shared count. The HUD redraws its lives only when
// they go up (losing one reloads the stage), so its digits are set here, as
// TGCConsole2 sets them, and its lives panel shown as for a 1-Up.
void showLives()
{
	TGCConsole2* c = gpMarDirector ? gpMarDirector->getConsole() : nullptr;
	if (!c)
		return;
	int lives = TFlagManager::getInstance()->getFlag(kLivesFlag);
	lives     = lives < 0 ? 0 : lives > 99 ? 99 : lives;
	c->unk3AC = (u8)lives;
	((J2DPicture*)c->unk39C[0]->getPane())->changeTexture(c->unkE0[lives / 10]->getTexInfo(), 0);
	((J2DPicture*)c->unk39C[1]->getPane())->changeTexture(c->unkE0[lives % 10]->getTexInfo(), 0);
	c->startAppearMario(false);
}

void takeLife()
{
	TFlagManager::getInstance()->decFlag(kLivesFlag, 1);
	showLives();
}
bool s_out;           // player 2 is out of lives
bool s_lastLife;      // ...and their death is playing: out once it has
s32 s_livesWhenOut;   // the count when they went out: a life more brings them back
int s_noticeFrames;
int s_hintFrames;     // "press Start to join" shown at a stage's start
bool s_hintShown;
bool s_testJoined;
int s_testJoinFrame;
int s_frame;
int s_swapCount;      // object moves led by player 2 lately (SMS_COOP_DEBUG)

void capture(Ctx& c)
{
	c.mario     = gpMarioOriginal;
	c.cam       = gpCamera;
	c.shake     = gpCameraShake;
	c.camMario  = gpCameraMario;
	c.camOption = gpCameraOption;
}

void apply(const Ctx& c)
{
	gpMarioOriginal = c.mario;
	gpMarioAddress  = c.mario;
	if (c.mario)
		SMS_SetMarioAccessParams();
	gpCamera       = c.cam;
	gpCameraShake  = c.shake;
	gpCameraMario  = c.camMario;
	gpCameraOption = c.camOption;
}

// Player 2's globals for as long as it lives (nested: the outermost restores).
struct InP2 {
	Ctx saved;
	bool entered;
	InP2()
	    : entered(!s_inP2 && s_p2.mario && s_p2.cam)
	{
		if (!entered)
			return;
		capture(saved);
		apply(s_p2);
		s_inP2          = true;
		port_coop_in_p2 = 1;
	}
	~InP2()
	{
		if (!entered)
			return;
		capture(s_p2); // what player 2's code changed (a camera's helpers) stays theirs
		s_p2.mario = (TMario*)gpMarioOriginal;
		apply(saved);
		s_inP2          = false;
		port_coop_in_p2 = 0;
	}
};

// ------------------------------------------------------------ Luigi's colours

// The bytes a texture's image takes, its mipmaps included.
u32 imageBytes(const ResTIMG& t)
{
	const u8 levels = t.mipmapEnabled && t.mipmapCount > 1 ? t.mipmapCount : 1;
	return GXGetTexBufferSize(t.width, t.height, t.format, levels > 1 ? GX_TRUE : GX_FALSE, levels);
}

// A texture table of a model's own: its headers and then every image and
// palette, copied into one block so each header's offsets stay positive.
// Texture 0 is `first` when given. A header's offsets are 32-bit and may point
// back before it (Eclipse sets some of Mario's textures to images loaded
// earlier): the game's memory is under 4 GB, so the game's own 32-bit image
// addresses wrap to the right place; read here, they are signed.
J3DTexture* ownTextures(J3DTexture* old, const ResTIMG* first)
{
	if (!old || !old->getNum())
		return old;
	const u16 n = old->getNum();
	u32 total   = sizeof(ResTIMG) * n;
	for (u16 i = 0; i < n; i++) {
		const ResTIMG& t = i == 0 && first ? *first : *old->getResTIMG(i);
		total = (total + 31) & ~31u;
		total += imageBytes(t);
		if (t.isIndexTexture && t.numColors)
			total = ((total + 31) & ~31u) + t.numColors * 2;
	}
	u8* block = (u8*)JKRHeap::alloc(total, 32, nullptr);
	if (!block)
		return old;
	ResTIMG* heads = (ResTIMG*)block;
	u32 at         = sizeof(ResTIMG) * n;
	for (u16 i = 0; i < n; i++) {
		const ResTIMG& t = i == 0 && first ? *first : *old->getResTIMG(i);
		heads[i]         = t;
		at               = (at + 31) & ~31u;
		const u32 bytes  = imageBytes(t);
		if (getenv("SMS_COOP_DEBUG"))
			fprintf(stderr, "[coop] texture %d/%d at %p: format %d %dx%d mips %d/%d image +0x%x (%u bytes) palette %d +0x%x (%d)\n",
			        (int)i, (int)n, (const void*)&t, (int)t.format, (int)t.width, (int)t.height, (int)t.mipmapEnabled,
			        (int)t.mipmapCount, (unsigned)t.imageDataOffset, (unsigned)bytes, (int)t.isIndexTexture,
			        (unsigned)t.paletteOffset, (int)t.numColors);
		memcpy(block + at, (const u8*)&t + (s32)t.imageDataOffset, bytes);
		heads[i].imageDataOffset = (u32)(at - sizeof(ResTIMG) * i);
		at += bytes;
		if (t.isIndexTexture && t.numColors) {
			at = (at + 31) & ~31u;
			memcpy(block + at, (const u8*)&t + (s32)t.paletteOffset, t.numColors * 2);
			heads[i].paletteOffset = (u32)(at - sizeof(ResTIMG) * i);
			at += t.numColors * 2;
		}
	}
	DCFlushRange(block, total);
	return new J3DTexture(n, heads);
}

void unpack565(u16 c, int& r, int& g, int& b)
{
	r = (c >> 11) & 31;
	g = (c >> 5) & 63;
	b = c & 31;
	r = (r << 3) | (r >> 2);
	g = (g << 2) | (g >> 4);
	b = (b << 3) | (b >> 2);
}

u16 pack565(int r, int g, int b)
{
	r = r < 0 ? 0 : r > 255 ? 255 : r;
	g = g < 0 ? 0 : g > 255 ? 255 : g;
	b = b < 0 ? 0 : b > 255 ? 255 : b;
	return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Mario's red made Luigi's green in a CMPR texture (each 4x4 block's two
// colours, as multiplayer's puppets are), every mipmap.
void luigiColours(ResTIMG* t)
{
	if (!t || t->format != GX_TF_CMPR)
		return;
	u8* data         = (u8*)t + t->imageDataOffset;
	const u32 blocks = imageBytes(*t) / 8;
	for (u32 i = 0; i < blocks; i++) {
		u8* blk     = data + i * 8;
		u16 c0      = (u16)(blk[0] << 8 | blk[1]);
		u16 c1      = (u16)(blk[2] << 8 | blk[3]);
		const bool four = c0 > c1;
		int r, g, b;
		unpack565(c0, r, g, b);
		mp_recolour::recolour(MPG_CHAR_LUIGI, r, g, b);
		u16 n0 = pack565(r, g, b);
		unpack565(c1, r, g, b);
		mp_recolour::recolour(MPG_CHAR_LUIGI, r, g, b);
		u16 n1 = pack565(r, g, b);
		if (four ? n0 <= n1 : n0 > n1) {
			if (n0 == n1) {
				// equal colours would turn a four-colour block into three and transparent
				if (four && n1 > 0)
					n1--;
				else if (four)
					n0++;
			} else {
				u16 tmp = n0;
				n0      = n1;
				n1      = tmp;
				u32 idx = (u32)blk[4] << 24 | (u32)blk[5] << 16 | (u32)blk[6] << 8 | blk[7];
				if (four) {
					idx ^= 0x55555555; // 0<->1, 2<->3
				} else {
					// three colours: 0<->1, 2 (the mix) and 3 (transparent) stay
					u32 out = 0;
					for (int k = 0; k < 16; k++) {
						u32 v = (idx >> (k * 2)) & 3;
						if (v < 2)
							v ^= 1;
						out |= v << (k * 2);
					}
					idx = out;
				}
				blk[4] = (u8)(idx >> 24), blk[5] = (u8)(idx >> 16), blk[6] = (u8)(idx >> 8), blk[7] = (u8)idx;
			}
		}
		blk[0] = (u8)(n0 >> 8), blk[1] = (u8)n0, blk[2] = (u8)(n1 >> 8), blk[3] = (u8)n1;
	}
	DCFlushRange(data, imageBytes(*t));
}

// Player 2 in Luigi's colours: their body, hands and cap get textures of
// their own (Mario's models share theirs with Mario), the body's main
// texture recoloured. Before their loadAfter, which makes the body's display
// lists with the textures it then has.
void dressAsLuigi(TMario* m)
{
	J3DModelData* body = m->mBodyModelData;
	if (!body)
		return;
	J3DTexture* own = ownTextures(body->getTexture(), nullptr);
	if (own == body->getTexture())
		return;
	body->setTexture(own);
	luigiColours(own->getResTIMG(0));
	const ResTIMG* main = own->getResTIMG(0);
	for (int h = 0; h < 2; h++)
		for (int s = 0; s < 2; s++)
			if (m->mHandModels[h][s]) {
				J3DModelData* d = m->mHandModels[h][s]->getModelData();
				d->setTexture(ownTextures(d->getTexture(), main));
			}
	if (m->mRHand4ndModel) {
		J3DModelData* d = m->mRHand4ndModel->getModelData();
		d->setTexture(ownTextures(d->getTexture(), main));
	}
	if (m->mCap)
		for (int c = 0; c < 2; c++)
			if (J3DModel* cap = m->mCap->unk10[c]) {
				J3DModelData* d = cap->getModelData();
				d->setTexture(ownTextures(d->getTexture(), main));
				cap->unlock();
				SMS_MakeDLAndLock(cap); // its display lists again, with the new texture
			}
}

// ------------------------------------------------------------ player 2 and camera 2

TMario* p1() { return s_p1.mario; }
TMario* p2() { return s_made ? s_p2.mario : nullptr; }

class TCoopMario : public TMario {
public:
	virtual void load(JSUMemoryInputStream& stream)
	{
		InP2 in;
		TMario::load(stream);
	}
	virtual void loadAfter()
	{
		InP2 in;
		TMario::loadAfter();
		// the sound's player is Mario, not this one (TMario::loadAfter set it while this was Mario)
		if (s_p1.mario)
			SMSGetMSound()->setPlayerInfo(&s_p1.mario->mPosition, &s_p1.mario->mPrevPosition,
			                              s_p1.mario->getM3UModel()->getModel()->getAnmMtx(1), true);
	}
	virtual void perform(u32 cue, JDrama::TGraphics* graphics)
	{
		if (!s_active || !s_made)
			return;
		const bool hellDead = gpMarDirector && (gpMarDirector->unk4E & TMarDirector::DEMO_FLAG_HELL_DEAD);
		{
			InP2 in;
			TMario::perform(cue, graphics);
			// their camera follows them, after they moved (as Mario's does after him)
			if (cue & (CUE_MOVE | CUE_CALC_ANIM))
				gpCamera->testPerform(cue & (CUE_MOVE | CUE_CALC_ANIM | CUE_MOVEMENT_GATE_A | CUE_MOVEMENT_GATE_B),
				                      graphics);
		}
		// a fall into a pit is theirs: the stage carries on
		if (gpMarDirector && !hellDead)
			gpMarDirector->unk4E &= ~TMarDirector::DEMO_FLAG_HELL_DEAD;
	}
};

class TCoopCamera : public CPolarSubCamera {
public:
	TCoopCamera(const char* name)
	    : CPolarSubCamera(name)
	{
	}
	virtual void loadAfter()
	{
		InP2 in;
		CPolarSubCamera::loadAfter();
	}
	virtual void perform(u32 cue, JDrama::TGraphics* graphics)
	{
		InP2 in;
		CPolarSubCamera::perform(cue, graphics);
	}
};

// The stage's scene is in: player 2 and their camera are made from Mario's
// record and with the stage's parameters, and put where the scene's loadAfter
// finds them (the player group, the cameras).
void sceneLoaded()
{
	s_made   = false;
	s_split  = false;
	s_active = false; // a new stage's player 2 joins it once it plays (frame)
	if (!s_wanted || !gpMarioOriginal || !gpCamera || !gpStrategy || gpApplication.mCurrArea.getStage() == 15)
		return;
	TIdxGroupObj* players = gpStrategy->unk10[6]; // "プレーヤーグループ"
	JDrama::TViewObjPtrListT<JDrama::TViewObj>* cameras
	    = (JDrama::TViewObjPtrListT<JDrama::TViewObj>*)JDrama::TNameRefGen::search2("Cameras");
	const void* record = nullptr;
	unsigned size      = 0;
	if (!players || !cameras || !port_spawn_record_of(gpMarioOriginal, &record, &size) || size < 8) {
		fprintf(stderr, "[coop] this stage has no Mario of its own to copy\n");
		return;
	}
	capture(s_p1);
	s_baseAspect = gpCamera->mAspect;

	// camera 2: its constructor makes the camera's helpers anew (gpCameraShake...)
	TCoopCamera* cam = new TCoopCamera("camera 2");
	capture(s_p2);
	s_p2.cam = cam;
	apply(s_p1);
	cam->mAspect = s_baseAspect;
	cam->setGamePad(gpMarDirector->getGamePad(1));
	cameras->getChildren().push_back(cam);

	// player 2: Mario's record, its type name passed over (TNameRef::getType)
	const u8* r = (const u8*)record;
	u32 at      = 4 + 2;
	at += 2 + ((u32)r[at] << 8 | r[at + 1]);
	if (at >= size) {
		fprintf(stderr, "[coop] Mario's record is too short\n");
		return;
	}
	TCoopMario* m = new TCoopMario;
	s_p2.mario    = m;
	JSUMemoryInputStream body((void*)(r + at), size - at);
	m->load(body); // as player 2 (InP2): its parameters, models and FLUDD
	m->setGamePad(gpMarDirector->getGamePad(1));
	dressAsLuigi(m);
	m->onHitFlag(HIT_FLAG_NO_COLLISION); // until they join
	THitActor* actor = m;
	players->add(actor);
	s_made = true;
	s_dir  = gpMarDirector;
	fprintf(stderr, "[coop] player 2 and camera 2 ready (%d bytes free in the stage's heap)\n",
	        (int)JKRHeap::getCurrentHeap()->getTotalFreeSize());
}

// ------------------------------------------------------------ drawing two views

// Links of the stage's perform lists that perform camera 1 (drawn with
// camera 2 for player 2's view), and the GX Post list's parts: the stage's
// 3D before the 2D (TMarDirector::initECDisp), its own 3D overlays (lens
// flare, talk cursor), and the 2D drawn once over both views.
const int kMostCamLinks = 64;
TPerformLink* s_camLinks[kMostCamLinks];
int s_camLinkCount;
const int kMostPost = 512;
TPerformLink* s_post[kMostPost];
int s_postCount, s_post3DEnd, s_postOverlayFrom, s_postOverlayTo, s_composite;

void findCamLinks(TPerformList* list, JDrama::TViewObj* cam)
{
	if (!list)
		return;
	for (JGadget::TSingleLinkList<TPerformLink, 0>::iterator it = list->getChildren().begin();
	     it != list->getChildren().end(); ++it)
		if (it->mPerformer == cam && s_camLinkCount < kMostCamLinks)
			s_camLinks[s_camLinkCount++] = &*it;
}

void swapCamLinks(JDrama::TViewObj* to)
{
	for (int i = 0; i < s_camLinkCount; i++)
		s_camLinks[i]->mPerformer = to;
}

void stageSetup()
{
	s_camLinkCount = s_postCount = 0;
	s_post3DEnd = s_postOverlayFrom = s_postOverlayTo = s_composite = -1;
	s_respawnFrames = 0;
	s_hintShown     = false;
	if (s_lastLife) { // the stage changed while their last life was being lost
		s_lastLife     = false;
		s_out          = true;
		s_livesWhenOut = TFlagManager::getInstance()->getFlag(kLivesFlag);
	}
	if (!s_made || !gpMarDirector)
		return;
	// camera 2's list of the actors the close-up view Y opens can turn to, as
	// the director makes camera 1's (TMarDirector::setupObjects, once the
	// conductor has its managers): without it, Y read an unmade list
	s_p2.cam->setNoticeInfo();
	TMarDirector* d = gpMarDirector;
	findCamLinks(d->unk34, s_p1.cam);
	findCamLinks(d->mPerformListGX, s_p1.cam);
	findCamLinks(d->mPerformListSilhouette, s_p1.cam);
	findCamLinks(d->mPerformListGXPost, s_p1.cam);
	JDrama::TViewObj* group2D2  = (JDrama::TViewObj*)JDrama::TNameRefGen::search2("Group 2D 2");
	for (JGadget::TSingleLinkList<TPerformLink, 0>::iterator it = d->mPerformListGXPost->getChildren().begin();
	     it != d->mPerformListGXPost->getChildren().end() && s_postCount < kMostPost; ++it) {
		TPerformLink* l = &*it;
		const int i     = s_postCount;
		s_post[s_postCount++] = l;
		const char* name = l->mPerformer ? l->mPerformer->getName() : nullptr;
		if (s_post3DEnd < 0 && name && !strcmp(name, "stageDisp"))
			s_post3DEnd = i;
		if (s_postOverlayTo < 0 && group2D2 && l->mPerformer == group2D2)
			s_postOverlayTo = i - 1; // the projection before Group 2D 2
	}
	// the composite (合成3) follows the 2D part's viewport and projection
	// (TMarDirector::initECDisp); found by place, as a literal in Japanese
	// would not match the scene's names where the compiler cannot write them
	// in Shift-JIS (the CP932 mirror covers the game's sources, not ext/)
	if (s_post3DEnd >= 0 && s_post3DEnd + 3 < s_postCount && s_post[s_post3DEnd + 1]->mPerformer
	    && s_post[s_post3DEnd + 1]->mPerformer->getName() && !strcmp(s_post[s_post3DEnd + 1]->mPerformer->getName(), "Screen 2D"))
		s_composite = s_post3DEnd + 3;
	s_postOverlayFrom = s_composite + 1;
	if (s_post3DEnd < 0 || s_composite < s_post3DEnd || s_postOverlayTo < s_postOverlayFrom) {
		fprintf(stderr, "[coop] this stage's GX Post list is not as expected: one view only\n");
		s_postCount = 0;
	}
	if (getenv("SMS_COOP_DEBUG"))
		fprintf(stderr, "[coop] %d camera links; GX Post: %d links, 3D to %d, composite %d, overlays %d-%d\n",
		        s_camLinkCount, s_postCount, s_post3DEnd, s_composite, s_postOverlayFrom, s_postOverlayTo);
}

void performPost(int from, int to, JDrama::TGraphics* g)
{
	for (int i = from; i < to && i < s_postCount; i++)
		if (i != s_composite)
			s_post[i]->perform(CUE_ALL, g);
}

// The two views' parts of the frame: player 1 left or top.
void viewRect(int view, float* r)
{
	if (s_layout == 1) {
		r[0] = 0.0f, r[1] = view ? 0.5f : 0.0f, r[2] = 1.0f, r[3] = 0.5f;
	} else {
		r[0] = view ? 0.5f : 0.0f, r[1] = 0.0f, r[2] = 0.5f, r[3] = 1.0f;
	}
}

// One view: its camera's models' view matrices and draw buffers (the entry
// pass the game makes after each tick, made again for a view after the
// first), then the stage's 3D, into its part.
void drawView(TMarDirector* d, JDrama::TGraphics* g, int view, bool entry)
{
	float r[4];
	viewRect(view, r);
	port_coop_other_view = (Mtx*)(view ? s_p1.cam : s_p2.cam)->getUnk1EC();
	GXPC_SetSplitView(1, r[0], r[1], r[2], r[3]);
	g->unk0 = 0;
	if (entry) {
		port_coop_second_entry = 1;
		d->unk34->perform(CUE_ALL, g);
		port_coop_second_entry = 0;
	}
	d->unk40->perform(CUE_ALL, g);
	d->mPerformListGX->perform(CUE_ALL, g);
	if (gpSilhouetteManager->unk48 > 0.0f || gpCamera->getUnk2C8() != -1)
		d->mPerformListSilhouette->perform(CUE_ALL, g);
	performPost(0, s_post3DEnd, g);
	// the 2D part's 3D: its viewport and projection, then the lens flare and talk cursor
	performPost(s_post3DEnd + 1, s_composite, g);
	performPost(s_postOverlayFrom, s_postOverlayTo, g);
}

// A dark line between the two views, under the HUD.
void drawDivider()
{
	Mtx44 ortho;
	C_MTXOrtho(ortho, 0.0f, 448.0f, 0.0f, 640.0f, 0.0f, 1.0f);
	GXSetProjection(ortho, GX_ORTHOGRAPHIC);
	Mtx id;
	PSMTXIdentity(id);
	GXLoadPosMtxImm(id, GX_PNMTX0);
	GXSetCurrentMtx(GX_PNMTX0);
	GXSetViewport(0.0f, 0.0f, 640.0f, 448.0f, 0.0f, 1.0f);
	GXSetScissor(0, 0, 640, 448);
	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GXSetNumChans(1);
	GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
	const GXColor dark = { 16, 16, 20, 255 };
	GXSetChanMatColor(GX_COLOR0A0, dark);
	GXSetNumTexGens(0);
	GXSetNumIndStages(0);
	GXSetNumTevStages(1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
	GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GXSetCullMode(GX_CULL_NONE);
	GXSetColorUpdate(GX_TRUE);
	f32 x0, y0, x1, y1;
	if (s_layout == 1)
		x0 = 0.0f, x1 = 640.0f, y0 = 223.0f, y1 = 225.0f;
	else
		x0 = 319.0f, x1 = 321.0f, y0 = 0.0f, y1 = 448.0f;
	GXBegin(GX_QUADS, GX_VTXFMT0, 4);
	GXPosition2f32(x0, y0);
	GXPosition2f32(x1, y0);
	GXPosition2f32(x1, y1);
	GXPosition2f32(x0, y1);
	GXEnd();
}

int drawStage(void* director, void* graphics)
{
	if (!s_split || !GXPC_SetSplitView || !s_postCount || director != s_dir)
		return 0;
	TMarDirector* d     = (TMarDirector*)director;
	JDrama::TGraphics* g = (JDrama::TGraphics*)graphics;
	// once: the frame's setup and the goop's textures (drawn at the frame's corner, then copied)
	d->unk40->perform(CUE_ALL, g);
	d->unk38->perform(CUE_ALL, g);
	d->unk3C->perform(CUE_ALL, g);
	// player 1's view first, from the entry pass the game made after the
	// tick; then player 2's, entered anew (the buffers drawn are cleared as
	// they are drawn, the rest as the entry pass begins)
	drawView(d, g, 0, false);
	{
		InP2 in;
		swapCamLinks(s_p2.cam);
		drawView(d, g, 1, true);
		swapCamLinks(s_p1.cam);
	}
	GXPC_SetSplitView(0, 0, 0, 1, 1);
	port_coop_other_view = nullptr;
	drawDivider();
	// the 2D, once over both: the frame's copy set up, the screen's effects, the HUD, the copy
	for (int i = s_post3DEnd; i <= s_composite; i++)
		s_post[i]->perform(CUE_ALL, g);
	for (int i = s_postOverlayTo; i < s_postCount; i++)
		s_post[i]->perform(CUE_ALL, g);

	return 1;
}

// ------------------------------------------------------------ joining, leaving, play

TMarDirector* director()
{
	return gpApplication.mAppState == TApplication::APP_STATE_GAMEPLAY ? gpMarDirector : nullptr;
}

bool playing()
{
	TMarDirector* d = director();
	return s_made && d && d == s_dir && d->mState == TMarDirector::STATE_UNK4 && gpMarioOriginal == s_p1.mario
	       && !d->checkFlag(TMarDirector::DIRECTOR_FLAG_STAGE_TRANSITION_PENDING);
}

void notice(const char* text, int frames)
{
	if (GXPC_ExtOverlaySet)
		GXPC_ExtOverlaySet(kOverlayNotice, text);
	s_noticeFrames = frames;
	fprintf(stderr, "[coop] %s\n", text);
}

// Beside player 1, facing as they do, a little behind and to their right.
void placeBesideP1(TMario* m)
{
	TMario* a  = p1();
	const f32 s = sinf(a->mFaceAngle.y * (3.14159265f / 32768.0f));
	const f32 c = cosf(a->mFaceAngle.y * (3.14159265f / 32768.0f));
	m->mPosition.x = a->mPosition.x + c * 90.0f - s * 40.0f;
	m->mPosition.y = a->mPosition.y + 20.0f;
	m->mPosition.z = a->mPosition.z - s * 90.0f - c * 40.0f;
	m->mFaceAngle.y = a->mFaceAngle.y;
	m->mModelFaceAngle = a->mFaceAngle.y;
	m->mVel.x = m->mVel.y = m->mVel.z = 0.0f;
	m->mForwardVel = 0.0f;
}

void revive(TMario* m)
{
	m->offFlag(MARIO_FLAG_GAME_OVER);
	m->mHealth = m->mDeParams.mHPMax.get();
	placeBesideP1(m);
	{
		InP2 in;
		// as a new life starts: the death's status ends (changePlayerStatus refuses to leave it)
		m->mPrevStatus  = MARIO_STATUS_WAIT;
		m->mStatus      = MARIO_STATUS_WAIT;
		m->mStatusArg   = 0;
		m->mStatusState = 0;
		m->mStatusTimer = 0;
		// their camera out of its death close-up, as player 1's is after a life
		CPolarSubCamera* cam = gpCamera;
		cam->unk64 &= ~(CPolarSubCamera::CAMERA_FLAG_DEAD_DEMO | CPolarSubCamera::CAMERA_FLAG_HELL_DEAD_DEMO);
		cam->mDeadDemoCountdown          = 0;
		cam->mDeadDemoCountdownToFovZoom = 0;
		cam->mPosFreezeFrames            = 0;
		cam->mFovy                       = s_p1.cam->mFovy;
		cam->warpPosAndAt(cam->mCurrentTarget.unk28, *gpMarioAngleY + 0x8000);
	}
}

void activate(bool on)
{
	TMario* m = p2();
	if (!m || s_active == on)
		return;
	s_active = on;
	if (getenv("SMS_COOP_DEBUG"))
		fprintf(stderr, "[coop] frame %d (gx frame %u): player 2 %s\n", s_frame, (unsigned)GXPC_FrameCount(),
		        on ? "in play" : "out of play");
	if (on) {
		m->offHitFlag(HIT_FLAG_NO_COLLISION);
		revive(m);
	} else {
		m->onHitFlag(HIT_FLAG_NO_COLLISION);
		m->mPosition.y = -100000.0f; // out of everyone's way
	}
}

// A view's camera as wide as its part of the frame.
void setAspects(bool split)
{
	f32 aspect = s_baseAspect;
	if (split)
		aspect *= s_layout == 1 ? 2.0f : 0.5f;
	if (s_p1.cam)
		s_p1.cam->mAspect = aspect;
	if (s_p2.cam)
		s_p2.cam->mAspect = aspect;
}

// The HUD's health meter is player 1's: player 2's health shows in the
// window's corner while they are hurt (as the game's meter shows Mario's).
void showHealth()
{
	static int shown = -1;
	TMario* m = p2();
	const int max = m ? m->mDeParams.mHPMax.get() : 0;
	const int now = s_active && m && !m->checkFlag(MARIO_FLAG_GAME_OVER) && m->mHealth < max ? m->mHealth : -1;
	if (now == shown || !GXPC_ExtOverlaySet)
		return;
	shown = now;
	if (now < 0) {
		GXPC_ExtOverlaySet(kOverlayStatus, "");
		return;
	}
	char text[64]; // the overlay's font is ASCII: a bar of the health left
	int at = snprintf(text, sizeof text, "#5ad25a Luigi [");
	for (int i = 0; i < max && at < (int)sizeof text - 3; i++)
		text[at++] = i < now ? '#' : '-';
	snprintf(text + at, sizeof text - at, "]");
	GXPC_ExtOverlaySet(kOverlayStatus, text);
}

void frame()
{
	s_frame++;
	if (!s_wanted)
		return;
	if (gpApplication.mCurrArea.getStage() == 15)
		s_out = s_lastLife = false; // the title: a file's lives start again
	TMarDirector* d = director();
	const bool here = s_made && d && d == s_dir;
	if (!here) {
		port_coop_set_joinable(0);
		s_split = false;
		return;
	}
	port_coop_set_joinable(playing());
	// controller 2 reads as controller 1 does in each of the stage's modes
	// (the director sets controller 1's: play, talk, menus)
	if (TMarioGamePad* pad2 = d->getGamePad(1))
		pad2->mFlags = d->getGamePad(0)->mFlags;
	// a test's player 2 (SMS_COOP_TEST_JOIN=frames into the stage)
	if (const char* t = getenv("SMS_COOP_TEST_JOIN"))
		if (!s_testJoined && playing() && ++s_hintFrames > atoi(t)) {
			s_testJoined    = true;
			s_testJoinFrame = s_frame;
			port_coop_join_virtual();
			if (const char* l = getenv("SMS_COOP_TEST_LIVES")) // the shared lives as the test joins
				TFlagManager::getInstance()->setFlag(kLivesFlag, atoi(l)), showLives();
		}
	if (const char* t = getenv("SMS_COOP_TEST_ONE_UP")) // a 1-Up, frames after the test's join
		if (s_testJoined && s_frame == s_testJoinFrame + atoi(t))
			TFlagManager::getInstance()->incMario(1);
	if (const char* t = getenv("SMS_COOP_TEST_LEAVE")) // a test's player 2 leaving, frames after joining
		if (s_testJoined && s_frame == s_testJoinFrame + atoi(t))
			port_coop_leave();
	const unsigned events = port_coop_take_events();
	if ((events & 1) && s_out)
		notice("Luigi is out of lives. The next 1-Up brings him back.", kNoticeFrames);
	else if (events & 1)
		notice("Luigi joined! Hold START on their controller to leave.", kNoticeFrames);
	if (events & 2)
		notice("Luigi left. Press START on another controller to play again.", kNoticeFrames);
	// out of lives: back with the next life that comes (it is theirs)
	if (s_out && !s_active && playing()) {
		TFlagManager* flags = TFlagManager::getInstance();
		const s32 lives     = flags->getFlag(kLivesFlag);
		if (lives > s_livesWhenOut) {
			takeLife();
			s_out = false;
			notice("A 1-Up for Luigi: he's back!", kNoticeFrames / 2);
		} else if (lives < s_livesWhenOut) {
			s_livesWhenOut = lives; // Mario lost one meanwhile
		}
	}
	const bool want = port_coop_player2() != 0 && !s_out;
	if (want != s_active && (!want || playing()))
		activate(want);
	if (!s_active && !s_hintShown && playing() && port_coop_can_join()) {
		s_hintShown = true;
		notice("Co-op: press START on another controller to join as Luigi.", kNoticeFrames);
	}
	if (s_noticeFrames > 0 && --s_noticeFrames == 0 && GXPC_ExtOverlaySet)
		GXPC_ExtOverlaySet(kOverlayNotice, "");
	showHealth();
	// two views while both play the stage; one (player 1's camera) for its
	// cutscenes, Shine Gets and the like, which the game shows on camera 1
	const bool split = s_active && d->mState >= TMarDirector::STATE_UNK4 && !d->isDemoModeNow()
	                   && !d->checkDemoFlag(TMarDirector::DEMO_FLAG_SHINE_GET_STOP_THE_WORLD)
	                   && !s_p1.cam->isDemoCamera() && s_postCount > 0;
	if (split != s_split)
		setAspects(split);
	s_split = split;
}

void afterMovement()
{
	TMario* m = p2();
	if (!s_active || !m || director() != s_dir)
		return;
	// test aids (frames since the game started): player 2 loses a life; the
	// stage changes (SMS_COOP_TEST_WARP=frame:area,episode)
	static bool killed, warped;
	if (const char* k = getenv("SMS_COOP_TEST_KILL"))
		if (!killed && s_frame >= atoi(k)) {
			killed = true;
			InP2 in;
			m->loserExec();
		}
	if (const char* w = getenv("SMS_COOP_TEST_WARP")) {
		int at = 0, area = 0, episode = 0;
		if (!warped && sscanf(w, "%d:%d,%d", &at, &area, &episode) == 3 && s_frame >= at) {
			warped = true;
			s_dir->setNextStage((u16)(((area + 1) << 8) | episode), nullptr);
		}
	}
	// FLUDD put on player 2's back mid-stage, frames after joining (as a stage
	// that starts without it hands it out)
	static bool gaveFludd;
	if (const char* t = getenv("SMS_COOP_TEST_FLUDD"))
		if (!gaveFludd && s_testJoined && s_frame >= s_testJoinFrame + atoi(t)) {
			gaveFludd = true;
			m->onFlag(MARIO_FLAG_HAS_FLUDD);
			fprintf(stderr, "[coop] test: player 2 has FLUDD now\n");
		}
	// coins brought out at player 2 and taken by them, frames after joining
	static int coinsDone;
	if (const char* t = getenv("SMS_COOP_TEST_COINS")) {
		int at = 0, count = 0;
		if (sscanf(t, "%d:%d", &at, &count) == 2 && s_testJoined && coinsDone < count && gpItemManager
		    && s_frame >= s_testJoinFrame + at + coinsDone * 10 && playing()) {
			if (TCoin* coin = gpItemManager->newAndRegisterCoinReal()) {
				coin->mPosition = m->mPosition;
				coin->appear();
				coin->touchActor(m); // as the hit check does when they meet
			}
			coinsDone++;
			fprintf(stderr, "[coop] test: player 2 took coin %d; the stage's coins: %d\n", coinsDone,
			        (int)TFlagManager::getInstance()->getFlag(0x40002));
		}
	}
	// the stage's 100-coin Shine brought out, frames after joining, and player 2
	// put on it once it has come out
	static TShine* shine;
	static int shineAt;
	if (const char* t = getenv("SMS_COOP_TEST_SHINE")) {
		if (!shineAt && s_testJoined && s_frame >= s_testJoinFrame + atoi(t) && gpItemManager) {
			shineAt = s_frame;
			shine   = gpItemManager->makeShineAppearWithDemo("シャイン（１００枚コイン用）", "シャイン（１００枚コイン用）カメラ",
			                                                 m->mPosition.x, m->mPosition.y + 50.0f, m->mPosition.z);
		}
		if (shine && shineAt && s_frame >= shineAt + 360 && shineAt > 0 && playing()) {
			shineAt      = -1;
			m->mPosition = shine->mPosition;
			m->mPosition.y -= 60.0f;
			fprintf(stderr, "[coop] test: player 2 touches the Shine at %.0f %.0f %.0f (state %d)\n", shine->mPosition.x,
			        shine->mPosition.y, shine->mPosition.z, (int)shine->mState);
			shine->touchActor(m); // as the hit check does when they meet
		}
	}
	if (getenv("SMS_COOP_DEBUG") && s_frame % 30 == 0) {
		TMarioGamePad* pad = m->getGamePad();
		fprintf(stderr, "[coop] frame %d: player 2 at %.0f %.0f %.0f status %x stick %.2f,%.2f; player 1 at %.0f %.0f %.0f\n",
		        s_frame, m->mPosition.x, m->mPosition.y, m->mPosition.z, (unsigned)m->mStatus,
		        pad ? pad->getMainStickX() : 0.0f, pad ? pad->getMainStickY() : 0.0f, p1()->mPosition.x, p1()->mPosition.y,
		        p1()->mPosition.z);
		fprintf(stderr, "[coop] %d object moves led by player 2 lately; player 2 health %d\n", s_swapCount, (int)m->mHealth);
		s_swapCount = 0;
	}
	// player 2 down (a lost life, a pit): a life from the shared count, as
	// Mario's is; back beside player 1 a moment later, or out on the last one
	if (m->checkFlag(MARIO_FLAG_GAME_OVER)) {
		const int fps = port_active_frame_rate > 0 ? port_active_frame_rate : 30;
		TFlagManager* flags = TFlagManager::getInstance();
		if (s_respawnFrames == 0) {
			s_respawnFrames = s_frame + fps * 3;
			if (flags->getFlag(kLivesFlag) > 0) {
				takeLife();
				notice("Luigi lost a life.", kNoticeFrames / 2);
			} else {
				s_lastLife = true; // the death plays out, then they leave the screen
			}
		} else if (s_frame >= s_respawnFrames) {
			s_respawnFrames = 0;
			if (s_lastLife) {
				s_lastLife     = false;
				s_out          = true;
				s_livesWhenOut = flags->getFlag(kLivesFlag);
				activate(false);
				notice("Luigi is out of lives. The next 1-Up brings him back.", kNoticeFrames);
				return;
			}
			revive(m);
		}
	}
	// fell out of the world, or left far behind (player 1 took a pipe, a cannon)
	const f32 dx = m->mPosition.x - p1()->mPosition.x, dz = m->mPosition.z - p1()->mPosition.z;
	if (!m->checkFlag(MARIO_FLAG_GAME_OVER) && (m->mPosition.y < -20000.0f || dx * dx + dz * dz > 30000.0f * 30000.0f))
		revive(m);
}

// Objects near player 2 move as if player 2 were Mario: an enemy goes after
// whoever is nearest, an NPC talks to whoever is beside it.
struct Swap {
	void* object;
	Ctx saved;
};
Swap s_swaps[16];
int s_swapDepth;

f32 dist2(const JGeometry::TVec3<f32>& a, const JGeometry::TVec3<f32>& b)
{
	const f32 x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
	return x * x + y * y + z * z;
}

void objectMove(void* object, int after)
{
	if (after) {
		if (s_swapDepth > 0 && s_swaps[s_swapDepth - 1].object == object) {
			Swap& w = s_swaps[--s_swapDepth];
			gpMarioOriginal = w.saved.mario;
			gpMarioAddress  = w.saved.mario;
			SMS_SetMarioAccessParams();
		}
		return;
	}
	if (!s_active || s_inP2 || s_swapDepth >= 16 || !s_p1.mario || gpMarioOriginal != s_p1.mario)
		return;
	TLiveActor* a = dynamic_cast<TLiveActor*>(static_cast<JDrama::TViewObj*>(object));
	if (!a || dynamic_cast<TMario*>(a))
		return;
	if (dist2(a->mPosition, s_p2.mario->mPosition) >= dist2(a->mPosition, s_p1.mario->mPosition)
	    || s_p2.mario->checkFlag(MARIO_FLAG_GAME_OVER))
		return;
	s_swapCount++;
	Swap& w        = s_swaps[s_swapDepth++];
	w.object       = object;
	w.saved.mario  = gpMarioOriginal;
	gpMarioOriginal = s_p2.mario;
	gpMarioAddress  = s_p2.mario;
	SMS_SetMarioAccessParams();
}

// A test's player 2 input (SMS_COOP_TEST_PAD=stick x:y@frame+frames,...;
// "a@frame+frames" presses A; frames since they joined): what controller 2 reads.
struct TestPress {
	int x, y;
	u16 buttons;
	int at, until;
};
TestPress s_test[32];
int s_testCount = -1;

void padRead(void* statusv)
{
	if (!s_wanted || !port_coop_player2())
		return;
	PADStatus* status = (PADStatus*)statusv;
	if (s_testCount < 0) {
		s_testCount = 0;
		const char* e = getenv("SMS_COOP_TEST_PAD");
		char buf[512];
		snprintf(buf, sizeof buf, "%s", e ? e : "");
		for (char* tok = strtok(buf, ","); tok && s_testCount < 32; tok = strtok(nullptr, ",")) {
			TestPress p = { 0, 0, 0, 0, 0 };
			int len     = 8;
			if (strchr("abrxyzl", tok[0]) && tok[1] == '@') {
				p.buttons = tok[0] == 'a' ? PAD_BUTTON_A : tok[0] == 'b' ? PAD_BUTTON_B : tok[0] == 'x' ? PAD_BUTTON_X
				          : tok[0] == 'y' ? PAD_BUTTON_Y : tok[0] == 'z' ? PAD_TRIGGER_Z : tok[0] == 'l' ? PAD_TRIGGER_L
				                                                                         : PAD_TRIGGER_R;
				if (sscanf(tok + 1, "@%d+%d", &p.at, &len) < 1)
					continue;
			} else if (sscanf(tok, "%d:%d@%d+%d", &p.x, &p.y, &p.at, &len) < 3) {
				continue;
			}
			p.until             = p.at + len;
			s_test[s_testCount++] = p;
		}
	}
	if (!s_testCount)
		return;
	PADStatus& s = status[1];
	s.err        = PAD_ERR_NONE;
	for (int i = 0; i < s_testCount; i++)
		if (s_frame - s_testJoinFrame >= s_test[i].at && s_frame - s_testJoinFrame < s_test[i].until) {
			if (s_test[i].buttons)
				s.button |= s_test[i].buttons;
			else
				s.stickX = (s8)s_test[i].x, s.stickY = (s8)s_test[i].y;
			if (s_test[i].buttons & PAD_TRIGGER_R)
				s.triggerRight = 255;
			if (s_test[i].buttons & PAD_TRIGGER_L)
				s.triggerLeft = 255;
		}
}

int allowSaveStates() { return !s_active; }
const char* saveStatesOffReason() { return "Save states are off while two play"; }

PortExtension s_extension;

} // namespace

// decomp-patches/zzzzzz-coop-02: the camera a controller's player sees
// through, for the controller's meanings (TMarioGamePad::updateMeaning reads
// whether it is in the close-up view Y opens). Controller 2 is player 2's,
// with camera 2, while they play this stage; everything else gpCamera.
CPolarSubCamera* port_coop_pad_camera(const TMarioGamePad* pad)
{
	if (s_made && s_active && s_p2.cam && gpApplication.mAppState == TApplication::APP_STATE_GAMEPLAY
	    && gpMarDirector == s_dir && pad == gpMarDirector->getGamePad(1))
		return s_p2.cam;
	if (s_made && s_inP2 && s_p1.cam && pad == gpMarDirector->getGamePad(0))
		return s_p1.cam; // controller 1 updated while player 2's globals are in force
	return gpCamera;
}

namespace {

struct Register {
	Register()
	{
		const char* e = getenv("SMS_COOP");
		s_wanted      = e && *e && strcmp(e, "0") != 0 && !getenv("SMS_MULTIPLAYER");
		if (!s_wanted)
			return;
		const char* layout = getenv("SMS_COOP_SPLIT");
		s_layout           = layout && (!strcmp(layout, "stacked") || !strcmp(layout, "horizontal")) ? 1 : 0;
		memset(&s_extension, 0, sizeof s_extension);
		s_extension.name                = "coop";
		s_extension.frame               = frame;
		s_extension.afterMovement       = afterMovement;
		s_extension.stageSetup          = stageSetup;
		s_extension.padRead             = padRead;
		s_extension.objectMove          = objectMove;
		s_extension.allowSaveStates     = allowSaveStates;
		s_extension.saveStatesOffReason = saveStatesOffReason;
		s_extension.sceneLoaded         = sceneLoaded;
		s_extension.drawStage           = drawStage;
		port_ext_register(&s_extension);
	}
} s_register;

} // namespace
