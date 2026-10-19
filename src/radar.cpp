// Port: the radar (SMS_RADAR=1, radar = on in settings.txt; off by default).
// A round map at the HUD's top right of the stage close around Mario, north
// up: the map stays still and scrolls under the radar's border as he moves,
// an arrow at its centre turns the way he faces, and the stage's Shines and
// blue coins are on it as the HUD's own sprites.
//
// The map is a picture of the stage from straight above (tools/radar/
// gen_maps.py makes one per stage from the disc, assets/maps; sms_gx loads
// it, platform/gx/src/gx_radar_maps.cpp), found by a hash of the stage's
// loaded collision, so a stage without one (a mod's) has no radar. GX
// textures are at most 1024 square and the pictures are larger, so the disc
// is drawn here into a small RGBA8 texture whenever Mario moves: the map,
// round, inside a white border with a dark edge and a soft shadow, all with
// smooth edges in the alpha. Over it go the blue coins, the Shines (a Shine
// got is the pale blue of the game's empty Shine) and Mario's arrow, always
// on top of the map whatever their height.
//
// The HUD draws it last (patch zzzz-radar-01) while a stage plays (not in
// cutscenes, conversations or the pause menu), fading in and out, and with
// SMS_WIDESCREEN_HUD=edges it moves to the right edge with the HUD's pieces.
#include <Enemy/Conductor.hpp>
#include <JSystem/J2D/J2DOrthoGraph.hpp>
#include <JSystem/JDrama/JDRRect.hpp>
#include <JSystem/JKernel/JKRFileLoader.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <MoveBG/MapObjBase.hpp>
#include <Player/MarioAccess.hpp>
#include <Strategic/LiveActor.hpp>
#include <Strategic/LiveManager.hpp>
#include <System/FlagManager.hpp>
#include <System/MarDirector.hpp>
#include <dolphin/gx.h>
#include <dolphin/os.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_radar.h"

extern "C" void* port_low_alloc(unsigned long size);
extern "C" void GXPC_HudPaneBegin(float x1, float x2);
extern "C" void GXPC_HudPaneEnd(void);

namespace {

// the disc's texture: kTex square; the map fills radius kMapR of its kTex / 2,
// the border runs to kRingR, its dark edge to kEdgeR, and the shadow past it
const int kTex      = 320;
const f32 kMapR     = 143.5f;
const f32 kRingR    = 153.0f;
const f32 kEdgeR    = 155.0f;
const f32 kShadowR  = 160.0f;
const u8 kBack[3]   = { 51, 61, 77 }; // past the picture's edge: its own backdrop colour

const f32 kRadius   = 60.0f; // the texture's radius in the HUD's 2D units (its frame is 600 wide)
const f32 kMargin   = 22.0f; // from the frame's right and top edges

// the sprites' sizes, as parts of the radar's radius
const f32 kShineH   = 0.27f;
const f32 kBlueH    = 0.19f;
const f32 kArrowLen = 0.27f; // tip to back, before its outline

const u32 kBlueCoinType = 0x20000010, kShineType = 0x20000013; // MapObjInit's coin_blue, shine

f32 sZoom   = 2500.0f; // world units from the centre to the map's edge
f32 sRadius = kRadius;

// the stage: its collision, the key naming its map, and the map
const void* sCol;
u32 sColProbe[4];
const void* sDirector;
unsigned long long sKey;
int sMapState = -1; // 1 the map is here, 0 it is loading, -1 none
PortRadarMap sMap;

u8* sDisc; // GX RGBA8, kTex x kTex, below 4 GiB
f32 sLastX = 1e30f, sLastZ;
f32 sAlpha; // fades in and out, 0..1

JUTTexture sShineArt, sBlueCoinArt;
bool sHaveArt;

// --- the stage and its map ---------------------------------------------------

inline void feed(unsigned long long& h, u32 v, int bytes)
{
	for (int i = 0; i < bytes; i++) {
		h ^= (v >> (8 * i)) & 0xFF;
		h *= 0x100000001b3ULL;
	}
}

inline u32 swap32(u32 v) { return (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); }
inline u16 swap16(u16 v) { return (u16)((v >> 8) | (v << 8)); }

// A value of the .col: in the host's order once the port has converted it
// (platform/endian), or still in the disc's (big-endian) order.
struct ColReader {
	const u8* b;
	u32 size;
	bool swap;
	u32 w32(u32 o) const
	{
		u32 v;
		memcpy(&v, b + o, 4);
		return swap ? swap32(v) : v;
	}
	u16 w16(u32 o) const
	{
		u16 v;
		memcpy(&v, b + o, 2);
		return swap ? swap16(v) : v;
	}
	// the header fits: vertex and group arrays inside the file
	bool fits() const
	{
		const u32 nv = w32(0), vo = w32(4), ng = w32(8), go = w32(12);
		return nv < 0x100000 && ng < 0x10000 && vo <= size && nv * 12 <= size - vo && go <= size
		       && ng * 0x18 <= size - go;
	}
};

// FNV-1a over the collision's values in little-endian order, as gen_maps.py's
// col_key: the vertex count and vertices, the group count, and each group's
// surface type, triangle count and vertex indices. 0 if it is not a .col
// this can read. The game's own copy is in the host's order (platform/
// endian) and its groups' index offsets are addresses by the time it plays.
unsigned long long colKey(const void* col, long size)
{
	if (size < 16)
		return 0;
	ColReader c = { (const u8*)col, (u32)size, false };
	if (!c.fits()) {
		c.swap = true;
		if (!c.fits())
			return 0;
	}
	const u32 nv = c.w32(0), vo = c.w32(4), ng = c.w32(8), go = c.w32(12);
	unsigned long long h = 0xcbf29ce484222325ULL;
	feed(h, nv, 4);
	for (u32 i = 0; i < nv * 3; i++)
		feed(h, c.w32(vo + i * 4), 4);
	feed(h, ng, 4);
	for (u32 g = 0; g < ng; g++) {
		const u32 e    = go + g * 0x18;
		const u16 type = c.w16(e);
		const s16 tris = (s16)c.w16(e + 2);
		u32 idx        = c.w32(e + 8);
		feed(h, type, 2);
		feed(h, (u16)tris, 2);
		if (tris <= 0)
			continue;
		// once the stage is set up, the game has made this offset an address
		// in the file (TMapCollisionData)
		const u32 base = (u32)(uintptr_t)col;
		if (idx >= base && idx - base < c.size)
			idx -= base;
		if (idx > c.size || (u32)tris * 6 > c.size - idx)
			return 0;
		for (int i = 0; i < tris * 3; i++)
			feed(h, c.w16(idx + i * 2), 2);
	}
	return h;
}

bool loadArt(JUTTexture& tex, const char* name)
{
	char path[64];
	snprintf(path, sizeof path, "/game_6/timg/%s.bti", name);
	const ResTIMG* timg = (const ResTIMG*)JKRGetResource(path);
	if (!timg)
		return false;
	tex.storeTIMG(timg);
	return true;
}

// Which stage this is, its map and the HUD's sprites; false without a map.
bool stage()
{
	const void* col = JKRGetResource("/scene/map/map.col");
	if (!col)
		return false;
	// a new stage can reuse the old one's memory: its first values tell
	u32 probe[4];
	memcpy(probe, col, sizeof probe);
	if (col != sCol || gpMarDirector != sDirector || memcmp(probe, sColProbe, sizeof probe)) {
		sCol      = col;
		sDirector = gpMarDirector;
		memcpy(sColProbe, probe, sizeof probe);
		sKey      = colKey(col, JKRFileLoader::getResSize((void*)col, nullptr));
		sMapState = sKey ? 0 : -1;
		sLastX    = 1e30f;
		sHaveArt  = loadArt(sShineArt, "shine_icon") && loadArt(sBlueCoinArt, "bluecoin_icon");
	}
	if (sMapState == 0) {
		sMapState = port_radar_map(sKey, &sMap);
		if (sMapState == 1)
			sLastX = 1e30f;
	}
	return sMapState == 1 && sHaveArt;
}

bool playing()
{
	return gpMarDirector && gpMarDirector->mState == TMarDirector::STATE_UNK4
	       && !gpMarDirector->isTalkOrDemoModeNow() && gpMarioPos;
}

// --- the disc's texture ----------------------------------------------------------

inline const u8* texel(int x, int y)
{
	if (x < 0 || y < 0 || x >= sMap.w || y >= sMap.h)
		return kBack;
	return sMap.rgba + ((size_t)y * sMap.w + x) * 4;
}

inline f32 clamp01(f32 v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

// "over": colour c at alpha a onto (rgb, alpha)
inline void over(f32* rgb, f32& alpha, f32 r, f32 g, f32 b, f32 a)
{
	const f32 out = a + alpha * (1.0f - a);
	if (out <= 0.0f)
		return;
	rgb[0] = (r * a + rgb[0] * alpha * (1.0f - a)) / out;
	rgb[1] = (g * a + rgb[1] * alpha * (1.0f - a)) / out;
	rgb[2] = (b * a + rgb[2] * alpha * (1.0f - a)) / out;
	alpha  = out;
}

// The map around (mx, mz), north up, in its border, into sDisc.
void compose(f32 mx, f32 mz)
{
	const f32 half = kTex * 0.5f, perTexel = sZoom / kMapR;
	const f32 d = sMap.density;
	size_t o    = 0;
	for (int by = 0; by < kTex; by += 4) {
		for (int bx = 0; bx < kTex; bx += 4) {
			u8 px[16][4];
			for (int i = 0; i < 16; i++) {
				const f32 sx = bx + (i & 3) + 0.5f - half, sy = by + (i >> 2) + 0.5f - half;
				const f32 r = sqrtf(sx * sx + sy * sy);
				f32 rgb[3]  = { 0, 0, 0 }, alpha = 0;
				// the shadow, a little low; the border's dark edge; the border,
				// lighter at the top
				const f32 rs = sqrtf(sx * sx + (sy - 3.0f) * (sy - 3.0f));
				if (rs > kMapR)
					over(rgb, alpha, 0, 0, 0, 0.35f * clamp01((kShadowR - rs) / (kShadowR - kEdgeR + 2.0f)));
				over(rgb, alpha, 0.12f, 0.14f, 0.2f, clamp01(kEdgeR + 0.5f - r));
				const f32 light = 1.0f - 0.16f * clamp01((sy / kRingR + 1.0f) * 0.5f);
				over(rgb, alpha, light, light, light * 1.02f, clamp01(kRingR + 0.5f - r));
				const f32 cover = clamp01(kMapR + 0.5f - r);
				if (cover > 0.0f) {
					const f32 wx = mx + sx * perTexel, wz = mz + sy * perTexel;
					const f32 u = (wx - sMap.x0) * d - 0.5f, v = (wz - sMap.z0) * d - 0.5f;
					const int x0 = (int)floorf(u), y0 = (int)floorf(v);
					const f32 ax = u - x0, ay = v - y0;
					const u8 *a = texel(x0, y0), *b = texel(x0 + 1, y0), *c = texel(x0, y0 + 1),
					         *e = texel(x0 + 1, y0 + 1);
					// a faint shade just inside the border, so the map sits in it
					const f32 shade = 1.0f - 0.22f * clamp01((r - (kMapR - 12.0f)) / 12.0f);
					f32 m[3];
					for (int k = 0; k < 3; k++) {
						const f32 top = a[k] + (b[k] - a[k]) * ax, bottom = c[k] + (e[k] - c[k]) * ax;
						m[k]          = (top + (bottom - top) * ay) / 255.0f * shade;
					}
					over(rgb, alpha, m[0], m[1], m[2], cover);
				}
				for (int k = 0; k < 3; k++)
					px[i][k] = (u8)(clamp01(rgb[k]) * 255.0f + 0.5f);
				px[i][3] = (u8)(clamp01(alpha) * 255.0f + 0.5f);
			}
			for (int i = 0; i < 16; i++) { // AR, then GB
				sDisc[o++] = px[i][3];
				sDisc[o++] = px[i][0];
			}
			for (int i = 0; i < 16; i++) {
				sDisc[o++] = px[i][1];
				sDisc[o++] = px[i][2];
			}
		}
	}
	DCFlushRange(sDisc, kTex * kTex * 4); // the texture cache sees the new picture
}

// --- drawing -----------------------------------------------------------------------

void setupTextured()
{
	GXSetNumChans(1);
	GXSetNumTevStages(1);
	GXSetNumTexGens(1);
	GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_SET);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
}

void setupColour()
{
	GXSetNumChans(1);
	GXSetNumTevStages(1);
	GXSetNumTexGens(0);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_SET);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
}

// A w x h quad of the texture in GX_TEXMAP0, centred on (x, y).
void quad(f32 x, f32 y, f32 w, f32 h, u32 colour, f32 alpha)
{
	static const f32 kU[4] = { 0, 1, 1, 0 }, kV[4] = { 0, 0, 1, 1 };
	const u8 a = (u8)((colour & 0xFF) * alpha);
	setupTextured();
	GXBegin(GX_QUADS, GX_VTXFMT0, 4);
	for (int i = 0; i < 4; i++) {
		GXPosition3f32(x + (kU[i] - 0.5f) * w, y + (kV[i] - 0.5f) * h, 0.0f);
		GXColor4u8(colour >> 24, colour >> 16, colour >> 8, a);
		GXTexCoord2f32(kU[i], kV[i]);
	}
	GXEnd();
}

// A sprite h tall at its own proportions, centred on (x, y).
void sprite(JUTTexture& tex, f32 x, f32 y, f32 h, u32 colour, f32 alpha)
{
	const ResTIMG* t = tex.getTexInfo();
	tex.load(GX_TEXMAP0);
	quad(x, y, t ? h * t->width / t->height : h, h, colour, alpha);
}

// Mario's arrow at (x, y) pointing along (dx, dy): an arrowhead with a notch
// at its back, white around red.
void arrow(f32 x, f32 y, f32 dx, f32 dy, f32 len, f32 alpha)
{
	setupColour();
	const f32 px = -dy, py = dx;
	for (int pass = 0; pass < 2; pass++) {
		const f32 s = pass ? 1.0f : 1.45f; // the white outline: the same shape, larger
		const u32 c = pass ? 0xE6231EFF : 0xFFFFFFFF;
		const u8 a  = (u8)(255 * alpha);
		// tip, the wings and the notch, about the arrow's middle
		const f32 tx = x + dx * len * 0.62f * s, ty = y + dy * len * 0.62f * s;
		const f32 nx = x - dx * len * 0.18f * s, ny = y - dy * len * 0.18f * s;
		const f32 lx = x - dx * len * 0.5f * s + px * len * 0.52f * s, ly = y - dy * len * 0.5f * s + py * len * 0.52f * s;
		const f32 rx = x - dx * len * 0.5f * s - px * len * 0.52f * s, ry = y - dy * len * 0.5f * s - py * len * 0.52f * s;
		GXBegin(GX_TRIANGLES, GX_VTXFMT0, 6);
		GXPosition3f32(tx, ty, 0.0f);
		GXColor4u8(c >> 24, c >> 16, c >> 8, a);
		GXPosition3f32(lx, ly, 0.0f);
		GXColor4u8(c >> 24, c >> 16, c >> 8, a);
		GXPosition3f32(nx, ny, 0.0f);
		GXColor4u8(c >> 24, c >> 16, c >> 8, a);
		GXPosition3f32(tx, ty, 0.0f);
		GXColor4u8(c >> 24, c >> 16, c >> 8, a);
		GXPosition3f32(nx, ny, 0.0f);
		GXColor4u8(c >> 24, c >> 16, c >> 8, a);
		GXPosition3f32(rx, ry, 0.0f);
		GXColor4u8(c >> 24, c >> 16, c >> 8, a);
		GXEnd();
	}
}

struct Marker {
	f32 x, z;
	bool got;
};

// The stage's Shines and blue coins, in every manager the conductor has (a
// placed Shine's is the "star" characters' manager, a boss's another), by
// their actor types.
int scan(Marker* shines, int maxShines, Marker* coins, int maxCoins, int* nCoins)
{
	int ns = 0, nc = 0;
	TFlagManager* flags = TFlagManager::getInstance();
	if (gpConductor) {
		for (JGadget::TList<TLiveManager*>::iterator it = gpConductor->unk10.begin();
		     it != gpConductor->unk10.end(); ++it) {
			TLiveManager* m = *it;
			if (!m)
				continue;
			for (int i = 0; i < m->getObjNum(); i++) {
				TLiveActor* a = m->getObj(i);
				if (!a)
					continue;
				const u32 type = a->getActorType();
				if (type == kBlueCoinType) {
					// a blue coin got, now or before, is dead
					if (!a->checkLiveFlag(LIVE_FLAG_DEAD) && nc < maxCoins) {
						coins[nc].x   = a->mPosition.x;
						coins[nc].z   = a->mPosition.z;
						coins[nc].got = false;
						nc++;
					}
				} else if (type == kShineType) {
					// a Shine still to come (a boss's, a race's) is dead until it
					// appears, so its flag says whether it is got
					const u32 id   = static_cast<TMapObjBase*>(a)->getEventId();
					const bool got = id < 120 && flags->getShineFlag(id);
					if ((got || id < 120 || !a->checkLiveFlag(LIVE_FLAG_DEAD)) && ns < maxShines) {
						shines[ns].x   = a->mPosition.x;
						shines[ns].z   = a->mPosition.z;
						shines[ns].got = got;
						ns++;
					}
				}
			}
		}
	}
	*nCoins = nc;
	return ns;
}

} // namespace

void RadarDraw(const JDrama::TRect& viewport)
{
	if (!port_radar)
		return;
	static bool sInit;
	if (!sInit) {
		sInit = true;
		if (const char* z = getenv("SMS_RADAR_ZOOM"))
			if (atof(z) >= 500.0f)
				sZoom = (f32)atof(z);
		if (const char* s = getenv("SMS_RADAR_SIZE"))
			if (atof(s) >= 40.0f && atof(s) <= 250.0f)
				sRadius = kRadius * (f32)atof(s) / 100.0f;
		sDisc = (u8*)port_low_alloc(kTex * kTex * 4);
	}
	const bool show = playing();
	sAlpha += show ? 0.125f : -0.125f;
	sAlpha = clamp01(sAlpha);
	if (sAlpha <= 0.0f || !sDisc || !gpMarioPos || !stage())
		return;

	const f32 mx = gpMarioPos->x, mz = gpMarioPos->z;
	if (fabsf(mx - sLastX) > 0.5f || fabsf(mz - sLastZ) > 0.5f) {
		compose(mx, mz);
		sLastX = mx;
		sLastZ = mz;
	}

	// at the top right of the frame, clear of the HUD's lives counter
	const f32 R = sRadius, cx = viewport.x2 - kMargin - R, cy = viewport.y1 + kMargin + 6.0f + R;
	const f32 mapR  = R * kMapR / (kTex * 0.5f); // the map's radius on screen
	const f32 scale = mapR / sZoom;              // 2D units per world unit (x right, z down)

	// a 2D of its own on the viewport (the HUD's has its own projection by now)
	J2DOrthoGraph graph(viewport);
	graph.setPort();
	graph.setup2D();
	GXPC_HudPaneBegin(cx - R, cx + R);

	GXTexObj disc;
	GXInitTexObj(&disc, sDisc, kTex, kTex, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GXInitTexObjLOD(&disc, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
	GXLoadTexObj(&disc, GX_TEXMAP0);
	quad(cx, cy, 2 * R, 2 * R, 0xFFFFFFFF, sAlpha);

	// the sprites, inside the border (whole, so none spills over it)
	static Marker shines[64], coins[128];
	int nCoins         = 0;
	const int nShines  = scan(shines, 64, coins, 128, &nCoins);
	const f32 blueH    = R * kBlueH, shineH = R * kShineH;
	for (int i = 0; i < nCoins; i++) {
		const f32 sx = (coins[i].x - mx) * scale, sy = (coins[i].z - mz) * scale;
		const f32 lim = mapR - blueH * 0.5f;
		if (sx * sx + sy * sy < lim * lim)
			sprite(sBlueCoinArt, cx + sx, cy + sy, blueH, 0xFFFFFFFF, sAlpha);
	}
	for (int pass = 0; pass < 2; pass++) { // Shines got under those still to get
		for (int i = 0; i < nShines; i++) {
			if (shines[i].got != (pass == 0))
				continue;
			const f32 sx = (shines[i].x - mx) * scale, sy = (shines[i].z - mz) * scale;
			const f32 h = shines[i].got ? shineH * 0.85f : shineH, lim = mapR - h * 0.5f;
			if (sx * sx + sy * sy >= lim * lim)
				continue;
			if (shines[i].got)
				sprite(sShineArt, cx + sx, cy + sy, h, 0x9CC8FFFF, sAlpha * 0.65f);
			else
				sprite(sShineArt, cx + sx, cy + sy, h, 0xFFFFFFFF, sAlpha);
		}
	}

	// Mario: the way he faces (his angle, 0 facing +z: down the map)
	const f32 angle = (gpMarioAngleY ? *gpMarioAngleY : 0) * (6.2831853f / 65536.0f);
	arrow(cx, cy, sinf(angle), cosf(angle), R * kArrowLen, sAlpha);

	GXPC_HudPaneEnd();
}
