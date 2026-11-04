// Servers' game side (mp_server.hpp): MPG_CMD_GOOP, the stage's goop as a
// server mod has it. Goop lives in the stage's goop layers (TPollutionLayer:
// the stretches of ground, wall or water the stage can have goop on, each with
// its goop map, an I8 texture the game draws its goop from and redraws with
// every stamp). A paint or a clean is a stamp of the game's own (pollute.bti,
// clean.bti), queued as FLUDD's are, in every layer the disc reaches; one of
// every height (PORT_POLLUTION_ANY_HEIGHT, zzzzz-ext-35) covers all the
// layer's surface under the disc, as seen from above (from the front, for a
// wall), where the game's own stamps touch only the surface near their height.
//
// Tints: each layer keeps, beside its goop map, which tint each texel's goop
// was painted in (0: the stage's own goop), and the session's tints say each
// one's colour. A layer with any tint in colour has its tint map (the colour of
// every texel) drawn over its goop by the renderer (GXPC_SetGoopTint), which
// keeps the goop's own shading. Game code, built with the game's flags: C++03.
#include "mp_server.hpp"

#include "../mp_game_api.h"
#include "port_ext.h"

#include <JSystem/J3D/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DTexture.hpp>
#include <JSystem/ResTIMG.hpp>
#include <Map/PollutionManager.hpp>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" void GXPC_SetGoopTint(const void* mask, int w, int h, const unsigned char* rgba, float lightness)
    __attribute__((weak));
extern "C" void GXPC_ClearGoopTints(void) __attribute__((weak));

namespace {

const int kMostLayers = 32;  // a stage's goop layers (the game names 20 at most)
const int kStampRoom  = 32;  // stamps a frame left to the game's own (FLUDD's cleaning): 255 a frame in all

struct Layer {
	TPollutionLayer* layer; // this stage's
	u8* tint;               // its texels' tints, row by row (null: all the stage's own, 0)
	u8* rgba;               // its tint map, as last given to the renderer
	int w, h;
	float lightness;        // its goop's typical lightness (its colour texture's)
	bool dirty;             // its tint map to make again
	bool shown;             // the renderer has a tint map for it
};

Layer s_layers[kMostLayers];
int s_layerCount;
TPollutionManager* s_manager; // the stage's, as its layers were taken
u32 s_colour[MPG_GOOP_TINTS]; // the session's tints: 0xRRGGBBAA, the alpha how strongly; 0 the goop's own
bool s_tinted;                // any tint in colour

int planeOf(TPollutionLayer* l) { return l->getPlaneType(); }

// Where on layer l's goop map a place is (texels), and the coordinate its
// surface's height is along (y for ground; across the wall for walls).
void texelOf(TPollutionLayer* l, f32 x, f32 y, f32 z, int* s, int* t, f32* across)
{
	switch (planeOf(l)) {
	case 2: case 3: // walls facing x: across the wall in z, up in y
		*s = l->getTexPosS(z), *t = l->getTexPosT(y), *across = x;
		break;
	case 4: case 5: // walls facing z
		*s = l->getTexPosS(x), *t = l->getTexPosT(y), *across = z;
		break;
	default:        // the ground (and water)
		*s = l->getTexPosS(x), *t = l->getTexPosT(z), *across = y;
		break;
	}
}

// RGB565 to lightness, 0..1
f32 lightOf565(u16 c)
{
	const f32 r = ((c >> 11) & 31) / 31.0f, g = ((c >> 5) & 63) / 63.0f, b = (c & 31) / 31.0f;
	return 0.299f * r + 0.587f * g + 0.114f * b;
}

// The typical lightness of layer l's goop: its colour texture's (the model's
// CMPR one, the goop map aside), from each block's two colours.
f32 lightnessOf(TPollutionLayer* l)
{
	J3DModelData* data = l->getModelData();
	J3DTexture* tex    = data ? data->getTexture() : nullptr;
	if (!tex)
		return 0.35f;
	for (u16 i = 0; i < tex->getNum(); i++) {
		ResTIMG* img = tex->getResTIMG(i);
		const u8* pix = (const u8*)img + img->imageDataOffset;
		if (img->format != 14 || pix == l->mPollutionMap)
			continue;
		const int blocks = (img->width / 4) * (img->height / 4);
		if (blocks <= 0)
			continue;
		f32 sum = 0.0f;
		for (int b = 0; b < blocks; b++) {
			const u8* p = pix + b * 8;
			sum += lightOf565((u16)(p[0] << 8 | p[1])) + lightOf565((u16)(p[2] << 8 | p[3]));
		}
		const f32 light = sum / (blocks * 2);
		return light > 0.05f ? light : 0.05f;
	}
	return 0.35f;
}

// The stage's goop layers, taken once it plays (they are made as it loads).
bool takeLayers()
{
	if (!gpPollution)
		return false;
	if (s_manager == gpPollution && s_layerCount)
		return true;
	s_manager    = gpPollution;
	s_layerCount = 0;
	const int n  = gpPollution->getJointModelNum();
	for (int i = 0; i < n && s_layerCount < kMostLayers; i++) {
		TPollutionLayer* l = gpPollution->getLayer(i);
		if (!l || !l->getPollutionImage() || !l->mPollutionMap)
			continue;
		Layer& L    = s_layers[s_layerCount++];
		L.layer     = l;
		L.tint      = nullptr;
		L.rgba      = nullptr;
		L.w         = l->getPollutionImage()->width;
		L.h         = l->getPollutionImage()->height;
		L.lightness = lightnessOf(l);
		L.dirty     = true;
		L.shown     = false;
	}
	return s_layerCount > 0;
}

void dropLayers()
{
	for (int i = 0; i < s_layerCount; i++) {
		free(s_layers[i].tint);
		free(s_layers[i].rgba);
	}
	memset(s_layers, 0, sizeof s_layers);
	s_layerCount = 0;
	s_manager    = nullptr;
	if (GXPC_ClearGoopTints)
		GXPC_ClearGoopTints();
}

// The tint of the texels a disc of `r` texels about (s, t) covers on L (only
// those whose surface is within the stamp's reach of `depth` when depth >= 0).
void markTint(Layer& L, int s, int t, int r, int depth, int tint)
{
	if (!L.tint) {
		if (tint == 0)
			return; // all the stage's own already
		L.tint = (u8*)calloc((size_t)L.w * L.h, 1);
		if (!L.tint)
			return;
	}
	TPollutionLayer* l = L.layer;
	const int reach    = (int)l->getUnk48();
	const int r2       = r * r;
	bool changed       = false;
	const int y0 = t - r > 0 ? t - r : 0, y1 = t + r < L.h - 1 ? t + r : L.h - 1;
	const int x0 = s - r > 0 ? s - r : 0, x1 = s + r < L.w - 1 ? s + r : L.w - 1;
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			if ((x - s) * (x - s) + (y - t) * (y - t) > r2)
				continue;
			if (depth >= 0) {
				const int d = l->getPos().getDepth(x, y) - depth;
				if (d < -reach || d > reach)
					continue;
			}
			u8& cell = L.tint[(size_t)y * L.w + x];
			if (cell != (u8)tint) {
				cell    = (u8)tint;
				changed = true;
			}
		}
	}
	if (changed)
		L.dirty = true;
}

// Room for a stamp of `type` this frame, the game's own left room too.
bool stampRoom(int type)
{
	TPollutionCounterLayer& c = gpPollution->getCounterLayer();
	if (type >= c.mTexStampNum)
		return false;
	const TPollutionTexStamp& st = c.mTexStamps[type];
	return st.mTaskNum + kStampRoom < st.mTaskCapacity;
}

// One layer's tint map, made again and handed to the renderer (or taken back
// when nothing on it is in colour).
void showLayer(Layer& L)
{
	L.dirty = false;
	if (!s_tinted) { // nothing in colour anywhere: drawn as the game draws it
		if (L.shown && GXPC_SetGoopTint)
			GXPC_SetGoopTint(L.layer->mPollutionMap, 0, 0, nullptr, 0.0f);
		L.shown = false;
		return;
	}
	if (!L.rgba)
		L.rgba = (u8*)malloc((size_t)L.w * L.h * 4);
	if (!L.rgba || !GXPC_SetGoopTint)
		return;
	bool any = false;
	for (size_t i = 0, n = (size_t)L.w * L.h; i < n; i++) {
		const u32 c = s_colour[L.tint ? L.tint[i] : 0];
		u8* p = L.rgba + i * 4;
		p[0] = (u8)(c >> 24), p[1] = (u8)(c >> 16), p[2] = (u8)(c >> 8), p[3] = (u8)c;
		any |= (c & 0xFF) != 0;
	}
	if (any)
		GXPC_SetGoopTint(L.layer->mPollutionMap, L.w, L.h, L.rgba, L.lightness);
	else if (L.shown) // nothing on it in colour: drawn as the game draws it
		GXPC_SetGoopTint(L.layer->mPollutionMap, 0, 0, nullptr, 0.0f);
	L.shown = any;
}

} // namespace

void MpGoopStageSetup() { dropLayers(); }

void MpGoopSessionStart()
{
	memset(s_colour, 0, sizeof s_colour);
	s_tinted = false;
	for (int i = 0; i < s_layerCount; i++)
		s_layers[i].dirty = true;
}

bool MpGoopApply(const MpgGoop& op)
{
	if (!takeLayers())
		return true; // no goop here: nothing to do, nothing to wait for
	const bool paint = op.what == MPG_GOOP_PAINT;
	const int type   = paint ? 1 : 0; // the game's pollute and clean stamps
	if (!stampRoom(type))
		return false; // next frame
	const f32 r = op.radius > 1.0f ? op.radius : 1.0f;
	const bool anyHeight = !(op.flags & MPG_GOOP_AT_HEIGHT);
	const int tint = paint ? (op.tint >= 0 && op.tint < MPG_GOOP_TINTS ? op.tint : 0) : 0;
	int layers = 0;
	for (int i = 0; i < s_layerCount; i++) {
		Layer& L           = s_layers[i];
		TPollutionLayer* l = L.layer;
		if (!l->isInAreaSize(op.x, op.y, op.z, r))
			continue;
		int s, t;
		f32 across;
		texelOf(l, op.x, op.y, op.z, &s, &t, &across);
		const int depth = anyHeight ? -1 : l->worldToDepth(across);
		gpPollution->getCounterLayer().pushStampTask(
		    (u16)type, (u8)(l->mIndexInParent & 0xFF), (u16)l->getPos().worldToTexSize(r * 2.0f), (u16)s, (u16)t,
		    (s16)(anyHeight ? PORT_POLLUTION_ANY_HEIGHT : depth));
		// a clean takes the place back to the stage's own (its goop, should it come back)
		markTint(L, s, t, l->getPos().worldToTexSize(r) + 1, depth, tint);
		layers++;
	}
	if (getenv("SMS_MULTIPLAYER_DEBUG"))
		fprintf(stderr, "[server] goop: %s %.0f %.0f %.0f, radius %.0f%s, tint %d: %d of %d layers\n",
		        paint ? "paint" : "clean", op.x, op.y, op.z, r, anyHeight ? "" : " at its height", tint, layers,
		        s_layerCount);
	return true;
}

void MpGoopTint(int tint, unsigned colour)
{
	if (tint < 0 || tint >= MPG_GOOP_TINTS || s_colour[tint] == colour)
		return;
	s_colour[tint] = colour;
	s_tinted       = false;
	for (int k = 0; k < MPG_GOOP_TINTS; k++)
		if (s_colour[k] & 0xFF)
			s_tinted = true;
	for (int i = 0; i < s_layerCount; i++)
		s_layers[i].dirty = true;
	fprintf(stderr, "[server] goop: tint %d is #%08x\n", tint, (unsigned)colour);
}

void MpGoopFrame()
{
	if (!s_tinted && !s_layerCount)
		return;
	if (s_tinted && !takeLayers())
		return;
	for (int i = 0; i < s_layerCount; i++)
		if (s_layers[i].dirty)
			showLayer(s_layers[i]);
}
