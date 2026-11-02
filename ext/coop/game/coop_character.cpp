// Player 2's character in split-screen co-op (ext/coop/README.md). The
// launcher's Character page chooses an outfit for player 2, and Play co-op
// says who it is: SMS_COOP_CHARACTER=<id> (mario, or a character in
// mods/characters, port_character.h), and for Mario his colours,
// SMS_COOP_LOOK="hat=2fa846;shirt=..." (as multiplayer sends a player's),
// painted from the launcher's SMS_MARIO_PAINT. Without SMS_COOP_CHARACTER,
// player 2 is Luigi as ever: Mario's model recoloured (coop.cpp).
//
// Player 2 is made from the "mario" volume, as Mario is (TMario::load). It
// holds player 1's character's archive; for another character, theirs is read
// into the stage's heap once a stage (patch zzzzzz-character-06) and put on
// the volume while player 2 loads (their model, hands, cap, animations, FLUDD
// and its place) and while their loadAfter runs, then player 1's put back
// (patch zzzzzz-coop-12), so nothing else in the stage reads player 2's.
// Their voice stays player 1's character's: the game has one voice bank.
// Game code: C++03.
#include "coop_character.h"
#include "port_character.h"
#include "../../multiplayer/mp_game_api.h"

#include <JSystem/J3D/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DTexture.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <M3DUtil/M3UModelMario.hpp>
#include <Player/Mario.hpp>
#include <Player/MarioCap.hpp>
#include <dolphin/gx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// patches zzzzzz-character-06 and zzzzzz-coop-12 (Application.cpp)
void* ApplicationCharacterArchive(int index);
void* ApplicationCharacterMounted();
void ApplicationCharacterPut(void* arc);

namespace {

// What a character's models and tables take beyond its archive, with room to
// spare (Mario's own come to about 0.6 MB), as patch zzzzzz-character-03 asks.
const s32 kModelRoom = 0x180000;

bool s_read;          // the environment was read
const char* s_wanted; // SMS_COOP_CHARACTER, or null
const char* s_look;   // SMS_COOP_LOOK, or ""
bool s_chosen;        // this stage's player 2 is the chosen character
int s_index;          // ...whose index in the character list is this
void* s_arc;          // ...and whose archive goes on the volume (null: player 1's will do)
void* s_p1Arc;        // player 1's, while player 2's is on the volume
int s_told = -1;      // the last fallback logged, so a stage's reload doesn't repeat it

// The textures given a picture or kept plain for player 2, undone as the next
// stage's player 2 is made (their memory is the old stage's).
struct Dressed {
	const void* data;
	u32 bytes;
};
const int kMostDressed = 256;
Dressed s_dressed[kMostDressed];
int s_dressedCount;

void readEnvironment()
{
	if (s_read)
		return;
	s_read = true;
	const char* e = getenv("SMS_COOP_CHARACTER");
	s_wanted      = e && *e ? e : nullptr;
	const char* l = getenv("SMS_COOP_LOOK");
	s_look        = l ? l : "";
}

void tell(int why, const char* text)
{
	if (why == s_told)
		return;
	s_told = why;
	fprintf(stderr, "[coop] %s\n", text);
}

// One texture of player 2's: in their outfit's colours when SMS_MARIO_PAINT
// has a picture for it, else as it is (not player 1's look).
void dressTexture(const char* name, const ResTIMG* timg)
{
	if (!timg || !name)
		return;
	// where GX reads it: the header's offset added in 32 bits, as on the console
	const void* data = (const void*)(uintptr_t)(u32)((uintptr_t)timg + timg->imageDataOffset);
	const u32 bytes  = GXGetTexBufferSize(timg->width, timg->height, timg->format, GX_FALSE, 0);
	if (!*s_look || !mpg_dress_look(s_look, MPG_CHAR_MARIO, name, data, bytes))
		mpg_dress_plain(data, bytes);
	if (s_dressedCount < kMostDressed) {
		s_dressed[s_dressedCount].data  = data;
		s_dressed[s_dressedCount].bytes = bytes;
		s_dressedCount++;
	}
}

// A model's textures; its texture 0 is named `first` when given (the hands'
// and the cap's draw the body's main texture, copied in as theirs).
void dressModel(J3DModelData* model, const char* first)
{
	J3DTexture* textures = model ? model->getTexture() : nullptr;
	JUTNameTab* names    = model ? model->getTextureName() : nullptr;
	if (!textures || !names)
		return;
	for (u16 i = 0; i < textures->getNum(); i++)
		dressTexture(i == 0 && first ? first : names->getName(i), textures->getResTIMG(i));
}

} // namespace

namespace coop_character {

void stageStart()
{
	readEnvironment();
	for (int i = 0; i < s_dressedCount; i++)
		mpg_undress(s_dressed[i].data, s_dressed[i].bytes);
	s_dressedCount = 0;
	s_chosen       = false;
	s_arc          = nullptr;
	if (!s_wanted)
		return;
	if (!port_character_available()) {
		tell(1, "player 2 is Luigi: this game plays as its own characters (a code mod's), not the launcher's");
		return;
	}
	const int index = port_character_find(s_wanted);
	if (index < 0) {
		char text[128];
		snprintf(text, sizeof text, "player 2 is Luigi: there is no character \"%s\" in mods/characters", s_wanted);
		tell(2, text);
		return;
	}
	if (index != port_character_selected()) {
		JKRHeap* heap = JKRHeap::getCurrentHeap();
		void* arc     = ApplicationCharacterArchive(index);
		if (!arc || heap->getFreeSize() < kModelRoom) {
			if (arc && port_character_path(index))
				JKRHeap::free(arc, nullptr);
			char text[160];
			snprintf(text, sizeof text, "player 2 is Luigi in this stage: %s doesn't fit in the stage's memory beside player 1",
			         port_character_name(index));
			tell(3, text);
			return;
		}
		s_arc = arc;
	}
	s_chosen = true;
	s_index  = index;
	s_told   = -1;
	fprintf(stderr, "[coop] player 2 is %s%s\n", port_character_name(index),
	        s_arc ? "" : " (player 1's character too)");
}

void enter()
{
	if (!s_chosen || !s_arc || s_p1Arc)
		return;
	s_p1Arc = ApplicationCharacterMounted();
	ApplicationCharacterPut(s_arc);
}

void leave()
{
	if (!s_p1Arc)
		return;
	ApplicationCharacterPut(s_p1Arc);
	s_p1Arc = nullptr;
}

bool chosen() { return s_chosen; }

bool mario() { return s_chosen && s_index == 0; }

const char* name() { return s_chosen ? port_character_name(s_index) : "Luigi"; }

const char* them() { return s_chosen ? "them" : "him"; }

void dress(TMario* m)
{
	if (!mario() || !m || !m->mBodyModelData)
		return;
	J3DModelData* body = m->mBodyModelData;
	JUTNameTab* names  = body->getTextureName();
	const char* main   = names && body->getTexture() && body->getTexture()->getNum() ? names->getName(0) : nullptr;
	dressModel(body, nullptr);
	for (int h = 0; h < 2; h++)
		for (int s = 0; s < 2; s++)
			if (m->mHandModels[h][s])
				dressModel(m->mHandModels[h][s]->getModelData(), main);
	if (m->mRHand4ndModel)
		dressModel(m->mRHand4ndModel->getModelData(), main);
	if (m->mCap)
		for (int c = 0; c < 2; c++)
			if (J3DModel* cap = m->mCap->unk10[c])
				dressModel(cap->getModelData(), main);
	fprintf(stderr, "[coop] player 2's textures: %d, %s\n", s_dressedCount,
	        *s_look ? "in their outfit's colours" : "Mario's own colours");
}

} // namespace coop_character
