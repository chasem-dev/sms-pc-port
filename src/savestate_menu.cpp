// Port: the save state menu, drawn by the game in its pause menu (save
// states, port_savestate.h). L1 + R1 there opens it: the states as a list on
// the HUD's brushstrokes, beside a stack of their pictures. Both show one
// selection in one list (port_savestate_scan): up and down step through the
// list, left and right through the pictures. A loads, Y deletes (pressed
// twice), B goes back to the pause menu.
//
// Everything it draws is the game's: the system font (its button glyphs swap
// for the controller in use, patch zzz-prompts-01), the HUD's art from
// game_6 (the brushstroke behind its counters, the Shine, coin and life
// icons, the counter digits, the pointing hand), drawn as the font draws its
// glyphs, in GX quads. The pictures are the states' own, as GX textures the
// port decodes (port_savestate_picture).
//
// Built into the game library (CMakeLists.txt), so its statics are game
// globals: an exact load puts them back too, as the menu was when the state
// was saved, closed.
#include <JSystem/J2D/J2DOrthoGraph.hpp>
#include <JSystem/JDrama/JDRGraphics.hpp>
#include <JSystem/JDrama/JDRRect.hpp>
#include <JSystem/JKernel/JKRFileLoader.hpp>
#include <JSystem/JUtility/JUTResFont.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <MSound/MSound.hpp>
#include <MSound/MSoundSE.hpp>
#include <System/Application.hpp>
#include <System/MarioGamePad.hpp>
#include <dolphin/gx.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "port_framerate.h"
#include "port_savestate.h"
#include "port_savestate_menu.h"

namespace {

// layout, in the pause menu's 2D units (the viewport, 640 wide)
const int kListRows    = 5;
const f32 kListX       = 26.0f;
const f32 kListTop     = 62.0f;
const f32 kRowW        = 282.0f;
const f32 kRowH        = 64.0f;
const f32 kPictureFromRight = 162.0f; // the front picture's centre, from the right edge
const f32 kPictureY    = 168.0f;
const f32 kPictureW    = 244.0f;
const f32 kTextSize    = 14.0f;

const u32 kSelectedRow = 0x28AA28FF; // the HUD counters' green
const u32 kOtherRow    = 0x6E28A0FF; // the pause menu's stage panel purple
const u32 kWhite       = 0xFFFFFFFF;
const u32 kPale        = 0xE1EBFFFF;
const u32 kNoteColour  = 0xFFD25AFF;
const u32 kFrame       = 0xFFF8D2FF; // the front picture's photo border
const u32 kOtherFrame  = 0xFFFFFFFF;

bool sOpen;
int sCount;
int sSelected;
f32 sShown;      // the picture stack's position, chasing sSelected
f32 sListFirst;  // the list's first row shown, chasing its target
bool sDeleteArmed;
u8 sAlpha;       // the pause menu's fade
u32 sTicks;      // frames open, for the selected row's bounce
char sNote[96];

// the HUD's art, from game_6 (loaded with every stage)
JUTTexture sBrush, sShine, sBlueCoin, sCoin, sLife, sCursor, sCross, sDigit[10];
bool sHaveArt;

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

// --- drawing: quads as JUTResFont draws its glyphs --------------------------

void colourOf(u32 c, u8 alpha, GXColor* out)
{
	out->r = c >> 24;
	out->g = c >> 16;
	out->b = c >> 8;
	out->a = (u8)(((c & 0xFF) * alpha) / 255);
}

// The corners of a w x h box centred on (cx, cy), turned `degrees`
// anticlockwise as seen, in drawing order.
void corners(f32 cx, f32 cy, f32 w, f32 h, f32 degrees, f32 out[4][2])
{
	const f32 r = degrees * 3.14159265f / 180.0f;
	const f32 c = cosf(r), s = sinf(r);
	const f32 dx[4] = { -w / 2, w / 2, w / 2, -w / 2 };
	const f32 dy[4] = { -h / 2, -h / 2, h / 2, h / 2 };
	for (int i = 0; i < 4; i++) {
		out[i][0] = cx + dx[i] * c + dy[i] * s;
		out[i][1] = cy - dx[i] * s + dy[i] * c;
	}
}

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

void quad(f32 cx, f32 cy, f32 w, f32 h, f32 degrees, u32 colour, u8 alpha, bool textured)
{
	f32 at[4][2];
	corners(cx, cy, w, h, degrees, at);
	static const f32 kU[4] = { 0, 1, 1, 0 }, kV[4] = { 0, 0, 1, 1 };
	GXColor c;
	colourOf(colour, alpha, &c);
	GXBegin(GX_QUADS, GX_VTXFMT0, 4);
	for (int i = 0; i < 4; i++) {
		GXPosition3f32(at[i][0], at[i][1], 0.0f);
		GXColor4u8(c.r, c.g, c.b, c.a);
		if (textured)
			GXTexCoord2f32(kU[i], kV[i]);
	}
	GXEnd();
}

void fill(f32 cx, f32 cy, f32 w, f32 h, f32 degrees, u32 colour, u8 alpha)
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
	quad(cx, cy, w, h, degrees, colour, alpha, false);
}

void art(JUTTexture& tex, f32 cx, f32 cy, f32 w, f32 h, f32 degrees, u32 colour, u8 alpha)
{
	tex.load(GX_TEXMAP0);
	setupTextured();
	quad(cx, cy, w, h, degrees, colour, alpha, true);
}

// An icon of height h with its left edge at x, centred on y; its width.
f32 icon(JUTTexture& tex, f32 x, f32 y, f32 h, u8 alpha)
{
	const ResTIMG* t = tex.getTexInfo();
	const f32 w = t ? h * t->width / t->height : h;
	art(tex, x + w / 2, y, w, h, 0.0f, kWhite, alpha);
	return w;
}

// "x12" in the HUD's counter digits, its left edge at x and centred on y; its width.
f32 count(int value, f32 x, f32 y, f32 h, u8 alpha)
{
	char text[16];
	snprintf(text, sizeof text, "%d", value < 0 ? 0 : value);
	const f32 step = h * 0.86f;
	art(sCross, x + h / 2, y, h, h, 0.0f, kWhite, alpha);
	x += step;
	for (const char* p = text; *p; p++, x += step)
		art(sDigit[*p - '0'], x + h / 2, y, h, h, 0.0f, kWhite, alpha);
	return step * (1 + strlen(text));
}

f32 textWidth(const char* text, f32 size)
{
	JUTFont* font = gpSystemFont;
	f32 w = 0.0f;
	for (; *text; text++)
		w += font->getWidth((u8)*text) * (size / font->getWidth());
	return w;
}

enum { LEFT, CENTRE, RIGHT };

// Text with its top at y, in the system font, with the game's drop shadow.
f32 text(const char* s, f32 x, f32 y, f32 size, u32 colour, int align, u8 alpha)
{
	JUTFont* font = gpSystemFont;
	const f32 w = textWidth(s, size);
	if (align == CENTRE)
		x -= w / 2;
	else if (align == RIGHT)
		x -= w;
	const f32 baseline = y + size;
	font->setGX();
	font->setCharColor(JUtility::TColor(0, 0, 0, alpha / 2));
	font->drawString_size_scale(x + size * 0.09f, baseline + size * 0.09f, size, size, s, strlen(s), false);
	GXColor c;
	colourOf(colour, alpha, &c);
	font->setCharColor(JUtility::TColor(c.r, c.g, c.b, c.a));
	font->drawString_size_scale(x, baseline, size, size, s, strlen(s), false);
	return w;
}

// A state's picture with a photo border, tilted, with its shadow.
void picture(int index, int slot, f32 cx, f32 cy, f32 w, f32 degrees, bool front, u8 alpha)
{
	int tw = 0, th = 0;
	const void* data = port_savestate_picture(index, slot, &tw, &th);
	const f32 h   = data ? w * th / tw : w * 0.7f;
	const f32 pad = front ? 6.0f : 4.0f;
	fill(cx + 4.0f, cy + 6.0f, w + 2 * pad, h + 2 * pad, degrees, 0x00000078, alpha);
	fill(cx, cy, w + 2 * pad, h + 2 * pad, degrees, front ? kFrame : kOtherFrame, alpha);
	if (!data) {
		fill(cx, cy, w, h, degrees, 0x203040FF, alpha);
		return;
	}
	GXTexObj obj;
	GXInitTexObj(&obj, (void*)data, tw, th, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GXInitTexObjLOD(&obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
	GXLoadTexObj(&obj, GX_TEXMAP0);
	setupTextured();
	quad(cx, cy, w, h, degrees, kWhite, alpha, true);
}

void sound(u32 id) { SMSGetMSound()->startSoundSystemSE(id, 0, nullptr, 0); }

void selectState(int index)
{
	if (sCount == 0)
		return;
	index = index < 0 ? 0 : index >= sCount ? sCount - 1 : index;
	if (index == sSelected)
		return;
	sSelected    = index;
	sDeleteArmed = false;
	sNote[0]     = 0;
	sTicks       = 0;
	sound(MSD_SE_SY_CURSOR);
}

int listTarget()
{
	int first = sSelected - kListRows / 2;
	if (first > sCount - kListRows)
		first = sCount - kListRows;
	return first < 0 ? 0 : first;
}

void drawRow(int index, f32 y, u8 alpha)
{
	const PortSaveStateInfo* st = port_savestate_scanned(index);
	if (!st)
		return;
	const bool selected = index == sSelected;
	// the selected row breathes as the pause menu's options do
	const f32 pulse = selected ? 1.0f + 0.025f * sinf(sTicks * port_frame_scale() * 0.2f) : 1.0f;
	art(sBrush, kListX + kRowW / 2, y + 23.0f, kRowW * pulse, 58.0f * pulse, 0.0f,
	    selected ? kSelectedRow : kOtherRow, (u8)(alpha * (selected ? 0.95f : 0.72f)));
	text(st->stageName[0] ? st->stageName : "SAVE STATE", kListX + 20.0f, y + 1.0f, selected ? 15.0f : 13.0f,
	     kWhite, LEFT, alpha);
	text(st->when, kListX + 20.0f, y + 21.0f, 9.5f, kPale, LEFT, alpha);
	if (st->hasCounts) {
		f32 x = kListX + kRowW - 128.0f;
		x += icon(sShine, x, y + 25.0f, 17.0f, alpha) + 1.0f;
		x += count(st->shines, x, y + 26.0f, 13.0f, alpha) + 9.0f;
		x += icon(sBlueCoin, x, y + 25.0f, 17.0f, alpha) + 1.0f;
		count(st->blueCoins, x, y + 26.0f, 13.0f, alpha);
	}
	if (selected)
		art(sCursor, kListX - 2.0f, y + 18.0f, 24.0f * 32 / 46, 24.0f, 90.0f, kWhite, alpha);
}

} // namespace

bool SaveStateMenuIsOpen() { return sOpen; }

void SaveStateMenuOpen()
{
	if (sOpen)
		return;
	sHaveArt = loadArt(sBrush, "coin_back") && loadArt(sShine, "shine_icon") && loadArt(sBlueCoin, "bluecoin_icon")
	           && loadArt(sCoin, "coin_icon") && loadArt(sLife, "mario_icon") && loadArt(sCursor, "cursor")
	           && loadArt(sCross, "coin_number_x");
	for (int i = 0; i < 10 && sHaveArt; i++) {
		char name[24];
		snprintf(name, sizeof name, "coin_number_%d", i);
		sHaveArt = loadArt(sDigit[i], name);
	}
	if (!sHaveArt)
		return;
	sCount       = port_savestate_scan();
	sSelected    = 0;
	sShown       = 0.0f;
	sListFirst   = 0.0f;
	sDeleteArmed = false;
	sTicks       = 0;
	sNote[0]     = 0;
	sOpen        = true;
	sound(MSD_SE_SY_2D_IN);
}

void SaveStateMenuClose()
{
	sOpen = false;
}

void SaveStateMenuMove(TMarioGamePad* pad)
{
	if (!sOpen)
		return;
	++sTicks;
	// two ways through one list: up and down (the list), left and right (the pictures)
	if (pad->checkFrameMeaning(0x2) || pad->checkFrameMeaning(0x8))
		selectState(sSelected - 1);
	else if (pad->checkFrameMeaning(0x4) || pad->checkFrameMeaning(0x10))
		selectState(sSelected + 1);

	if (pad->checkFrameMeaning(0x40)) {
		sound(MSD_SE_SY_2D_OUT);
		sOpen = false;
		return;
	}
	const PortSaveStateInfo* st = port_savestate_scanned(sSelected);
	if (pad->checkFrameMeaning(0x20) && st) {
		if (!st->sameGame) {
			snprintf(sNote, sizeof sNote, "This state is from another game");
		} else {
			sound(MSD_SE_SY_DECIDE);
			port_savestate_request_load(st->path);
			sOpen = false;
			return;
		}
	}
	if (pad->testTrigger(JUTGamePad::Y) && st) {
		if (!sDeleteArmed) {
			sDeleteArmed = true;
			snprintf(sNote, sizeof sNote, "Press \xA5 again to delete this state");
		} else {
			sDeleteArmed = false;
			snprintf(sNote, sizeof sNote, port_savestate_delete(st->path) ? "State deleted" : "Could not delete the state");
			sound(MSD_SE_SY_CANCEL_COMMON);
			sCount = port_savestate_scan();
			if (sSelected >= sCount)
				sSelected = sCount - 1;
			if (sSelected < 0)
				sSelected = 0;
		}
	}
	// the pictures and the list follow the selection
	const f32 chase = port_frame_chase(0.3f);
	sShown += (sSelected - sShown) * chase;
	sListFirst += (listTarget() - sListFirst) * chase;
}

void SaveStateMenuSetAlpha(u8 alpha) { sAlpha = alpha; }

void SaveStateMenuDraw(const JDrama::TRect& screen)
{
	const u8 alpha = sAlpha;
	if (!sOpen || !alpha)
		return;
	const f32 width = screen.getWidth(), height = screen.getHeight();
	J2DOrthoGraph graph(screen);
	graph.setPort();
	graph.setup2D();

	// the paused game behind, its HUD and pause menu too, darker
	fill(width / 2, height / 2, width, height, 0.0f, 0x000014C8, alpha);

	// the heading, with the buttons that opened it
	f32 x = 28.0f + text("LOAD SAVE STATE", 28.0f, 14.0f, 22.0f, kWhite, LEFT, alpha) + 12.0f;
	PortSaveStateHint hint;
	port_savestate_hint(&hint);
	for (int i = 0; i < hint.count; i++) {
		gpSystemFont->setGX();
		gpSystemFont->setCharColor(JUtility::TColor(255, 255, 255, alpha));
		if (hint.hasImage[i]) {
			gpSystemFont->drawChar_scale(x, 40.0f, 26.0f, 26.0f, PORT_PROMPT_SAVESTATE + i, false);
			x += 30.0f;
		} else {
			x += text(hint.label[i], x, 18.0f, 14.0f, kPale, LEFT, alpha) + 8.0f;
		}
	}

	if (sCount == 0) {
		text("No save states yet", width / 2, 180.0f, 20.0f, kWhite, CENTRE, alpha);
		text("Save one in a level with the buttons above", width / 2, 210.0f, 13.0f, kPale, CENTRE, alpha);
		text("# Back", width / 2, height - 36.0f, 15.0f, kWhite, CENTRE, alpha);
		return;
	}

	// the list: the rows near the selection, sliding as it moves
	const int first = (int)sListFirst;
	for (int i = first - 1; i <= first + kListRows; i++) {
		if (i < 0 || i >= sCount)
			continue;
		const f32 y = kListTop + (i - sListFirst) * kRowH;
		if (y < kListTop - kRowH * 0.5f || y > kListTop + kRowH * (kListRows - 0.5f))
			continue;
		drawRow(i, y, alpha);
	}

	const f32 kPictureX = width - kPictureFromRight;
	// the pictures: the selection in front, its neighbours tilted behind,
	// where the stack has slid to (sShown)
	const int nearIndex = (int)floorf(sShown + 0.5f);
	const int order[3] = { nearIndex - 1, nearIndex + 1, nearIndex };
	for (int k = 0; k < 3; k++) {
		const int i = order[k];
		if (i < 0 || i >= sCount)
			continue;
		const f32 off   = i - sShown;
		const f32 apart = off < -1.0f ? -1.0f : off > 1.0f ? 1.0f : off;
		const f32 scale = 1.0f - 0.3f * fabsf(apart);
		picture(i, (i % PORT_SAVESTATE_PICTURE_SLOTS + PORT_SAVESTATE_PICTURE_SLOTS) % PORT_SAVESTATE_PICTURE_SLOTS,
		        kPictureX + apart * 94.0f, kPictureY + fabsf(apart) * 36.0f, kPictureW * scale, -apart * 11.0f,
		        k == 2, alpha);
	}

	// about the selection
	const PortSaveStateInfo* st = port_savestate_scanned(sSelected);
	if (st) {
		text(st->stageName, kPictureX, 258.0f, 20.0f, kWhite, CENTRE, alpha);
		if (st->episodeName[0])
			text(st->episodeName, kPictureX, 284.0f, 12.5f, kWhite, CENTRE, alpha);
		if (st->hasCounts) {
			f32 cx = kPictureX - 86.0f;
			cx += icon(sCoin, cx, 320.0f, 22.0f, alpha) + 2.0f;
			cx += count(st->coins, cx, 321.0f, 18.0f, alpha) + 22.0f;
			cx += icon(sLife, cx, 320.0f, 22.0f, alpha) + 2.0f;
			count(st->lives, cx, 321.0f, 18.0f, alpha);
		}
		char position[24];
		snprintf(position, sizeof position, "%d of %d", sSelected + 1, sCount);
		text(position, kPictureX, 340.0f, 11.0f, kPale, CENTRE, alpha);
	}

	// a note, then the buttons (the font's button glyphs, as the game writes them)
	const char* note = sNote;
	if (!note[0] && st && st->sameGame && !st->exact)
		note = "Saved by another version: loads Mario's place and progress";
	if (note[0])
		text(note, width / 2, height - 62.0f, 12.0f, kNoteColour, CENTRE, alpha);
	text("@ Load     \xA5 Delete     # Back", width / 2, height - 36.0f, 15.0f, kWhite, CENTRE, alpha);
}
