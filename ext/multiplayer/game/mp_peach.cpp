// Peach's model for the Peach player (mp_puppet.hpp), read from the disc.
//
// Peach is an NPC in only a few stages, but every disc has her in the boss
// stage's archive (/data/scene/coronaBoss.szs, the bathtub scene): her files
// sit at its start, so reading them means decompressing only the archive's
// first ~350 KB, through the game's streaming reader (JKRDvdRipper), never
// the whole 5 MB. The files are copied out and converted to this machine's
// byte order as an archive's files are when the game fetches them.
#include "mp_peach.hpp"

#include <JSystem/J3D/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DTexture.hpp>
#include <JSystem/J3D/J3DGraphLoader/J3DModelLoader.hpp>
#include <JSystem/JKernel/JKRDvdRipper.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <MarioUtil/TexUtil.hpp>
#include <System/MarDirector.hpp>
#include <stdio.h>
#include <string.h>

extern "C" int port_endian_resource(void* data, uint32_t size, const char* name);

namespace {

const char kArchive[] = "/data/scene/coronaBoss.szs";
const u32 kLoadFlags  = J3DMLF_MaterialPEFull | (4 << J3DMLF_TevStageNumShift);

u32 be32(const u8* p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
u16 be16(const u8* p) { return (u16)(p[0] << 8 | p[1]); }

// `size` bytes of the decompressed archive from `offset`, into a new buffer
// (32-byte aligned, from the current heap).
u8* readArchive(u32 offset, u32 size)
{
	u8* buf = (u8*)JKRHeap::alloc((size + 31) & ~31u, 32, nullptr);
	if (!buf)
		return nullptr;
	if (!JKRDvdRipper::loadToMainRAM(kArchive, buf, EXPAND_SWITCH_DECOMPRESS, size, nullptr,
	                                 JKRDvdRipper::ALLOC_DIRECTION_FORWARD, offset, nullptr)) {
		JKRHeap::free(buf, nullptr);
		return nullptr;
	}
	return buf;
}

struct Entry {
	char name[40];
	u32 offset, size;
};

// The archive's file list (RARC: header, info block, entries, names).
int listArchive(Entry* out, int max)
{
	u8* head = readArchive(0, 0x40);
	if (!head)
		return 0;
	int n = 0;
	if (!memcmp(head, "RARC", 4)) {
		const u32 dataStart = 0x20 + be32(head + 0x0C);
		const u32 entries   = be32(head + 0x28);
		const u32 entryOff  = 0x20 + be32(head + 0x2C);
		const u32 strOff    = 0x20 + be32(head + 0x34);
		JKRHeap::free(head, nullptr);
		head = nullptr;
		if (dataStart > 0x20000)
			return 0;
		u8* dir = readArchive(0, dataStart);
		if (!dir)
			return 0;
		for (u32 i = 0; i < entries && n < max; i++) {
			const u8* e = dir + entryOff + i * 0x14;
			if (e + 0x14 > dir + dataStart || be16(e) == 0xFFFF || !(e[4] & 0x01))
				continue; // a folder
			const char* name = (const char*)dir + strOff + be16(e + 6);
			if ((const u8*)name >= dir + dataStart)
				continue;
			snprintf(out[n].name, sizeof out[n].name, "%s", name);
			out[n].offset = dataStart + be32(e + 8);
			out[n].size   = be32(e + 12);
			n++;
		}
		JKRHeap::free(dir, nullptr);
	}
	if (head)
		JKRHeap::free(head, nullptr);
	return n;
}

J3DModelData* loadFile(const Entry* list, int count, const char* name)
{
	for (int i = 0; i < count; i++) {
		if (strcmp(list[i].name, name))
			continue;
		u8* buf = readArchive(list[i].offset, list[i].size);
		if (!buf)
			return nullptr;
		port_endian_resource(buf, list[i].size, name);
		return J3DModelLoaderDataBase::load(buf, kLoadFlags);
	}
	return nullptr;
}

MpPeachParts s_parts;
void* s_partsFor; // the director (stage) they were loaded in
bool s_missing;   // this disc has no Peach to read

} // namespace

const MpPeachParts* MpPeachLoad()
{
	if (s_missing)
		return nullptr;
	if (s_partsFor == gpMarDirector && s_parts.body)
		return &s_parts;
	memset(&s_parts, 0, sizeof s_parts);
	s_partsFor = gpMarDirector;
	static Entry list[160];
	const int count = listArchive(list, 160);
	s_parts.body    = loadFile(list, count, "peach_model.bmd");
	if (!s_parts.body) {
		fprintf(stderr, "[multiplayer] Peach's model is not on this disc; Peach wears Mario's\n");
		s_missing = true;
		return nullptr;
	}
	s_parts.hair  = loadFile(list, count, "peach_hair_ponytail.bmd");
	s_parts.handL = loadFile(list, count, "peach_hand1_l.bmd");
	s_parts.handR = loadFile(list, count, "peach_hand1_r.bmd");
	// her hands wear her body's texture (as NpcParts does)
	J3DTexture* tex = s_parts.body->getTexture();
	const int main  = s_parts.body->getTextureName() ? s_parts.body->getTextureName()->getIndex("H_peach_main_s3tc") : -1;
	if (tex && main >= 0 && main < tex->getNum()) {
		if (s_parts.handL)
			SMS_ChangeTextureAll(s_parts.handL, "H_peach_main_dummy", *tex->getResTIMG((u16)main));
		if (s_parts.handR)
			SMS_ChangeTextureAll(s_parts.handR, "H_peach_main_dummy", *tex->getResTIMG((u16)main));
	}
	return &s_parts;
}
