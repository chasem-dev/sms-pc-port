// Character voices (src/port_include/port_character.h): a character pack's
// voice/ folder in mods/characters/<id>/ holds WAV files named after
// Mario's voice cues (MV21_JUMP_SMALL_01.wav, MV24_JUMP_SPECIAL_03.wav, ...).
// As a stage loads the character (patch zzzzzz-character-02), each file takes
// the place of the wave its cue plays: encoded to AFC ADPCM (the format of
// Mario's own voice), written into ARAM over Mario's voice waves and pointed
// at by the wave's entry in wave bank 1 (w2ndLoad_0.aw), with its own sample
// rate. Waves without a file keep Mario's voice. Mario's waves and entries are
// kept the first time and put back for Mario, or before another character's.
//
// Every cue Mario's code plays (MSD_SE_MV*, MSound/SoundEffects.hpp) is a key
// of percussion program 0xE6 or 0xE7 in bank 5; the table below is what the
// sound-effect sequence (se.scom, category 7) and the bank resolve each to.
// Some cues share a wave; for those the first cue with a file wins.
#include "port_character.h"
#include "port_platform.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" uint8_t* port_aram_ptr(uint32_t addr);

namespace {

struct Cue {
	const char* name;
	uint16_t wave;
};

const Cue kCues[] = {
	{ "MV01_DAMAGE_BIG_01", 0xCE },
	{ "MV01_DAMAGE_BIG_02", 0xCF },
	{ "MV01_DAMAGE_BIG_03", 0xD0 },
	{ "MV02_DAMAGE_MID_01", 0xD1 },
	{ "MV02_DAMAGE_MID_02", 0xD2 },
	{ "MV02_DAMAGE_MID_03", 0xD4 },
	{ "MV02_DAMAGE_MID_04", 0xD3 },
	{ "MV03_DAMAGE_LITLE_01", 0xD5 },
	{ "MV03_DAMAGE_LITLE_02", 0xD6 },
	{ "MV03_DAMAGE_LITLE_03", 0xD8 },
	{ "MV03_DAMAGE_LITLE_04", 0xD7 },
	{ "MV04_DAMAGE_ELEC_01", 0xD9 },
	{ "MV04_DAMAGE_ELEC_02", 0xDA },
	{ "MV05_DAMAGE_FIRE_01", 0xDC },
	{ "MV05_DAMAGE_FIRE_02", 0xDB },
	{ "MV06_DAMAGE_WATER_01", 0xDD },
	{ "MV06_DAMAGE_WATER_02", 0xDE },
	{ "MV06_DAMAGE_WATER_03", 0xDF },
	{ "MV07_DAMAGE_REACT_01", 0xE0 },
	{ "MV07_DAMAGE_REACT_02", 0xE3 },
	{ "MV07_DAMAGE_REACT_03", 0xE1 },
	{ "MV07_DAMAGE_REACT_04", 0xE2 },
	{ "MV08A_DOWN_01", 0xE4 },
	{ "MV08A_DOWN_02", 0xE5 },
	{ "MV08A_DOWN_03", 0xE6 },
	{ "MV08B_DOWN_REACT_01", 0xE8 },
	{ "MV08B_DOWN_REACT_02", 0xE9 },
	{ "MV08B_DOWN_REACT_03", 0xE7 },
	{ "MV08B_DOWN_REACT_04", 0xE7 },
	{ "MV10A_CRY_SHORT_01", 0xEB },
	{ "MV10A_CRY_SHORT_02", 0xEA },
	{ "MV10A_CRY_SHORT_03", 0xEA },
	{ "MV10B_CRY_JUMP_01", 0x2B },
	{ "MV12_REACT_01", 0xEC },
	{ "MV12_REACT_02", 0xED },
	{ "MV12_REACT_03", 0xEE },
	{ "MV13_ACTION_SMALL_01", 0xEF },
	{ "MV13_ACTION_SMALL_02", 0xF1 },
	{ "MV13_ACTION_SMALL_03", 0xF0 },
	{ "MV15_EXERT_INST_01", 0xF2 },
	{ "MV15_EXERT_INST_02", 0xF3 },
	{ "MV15_REACT_SMALL_01", 0xF4 },
	{ "MV15_REACT_SMALL_02", 0xF5 },
	{ "MV15_REACT_SMALL_03", 0xF6 },
	{ "MV16_EXERT_CONT_01", 0xF7 },
	{ "MV17_EXERT_REACT_01", 0xF9 },
	{ "MV17_EXERT_REACT_02", 0xF8 },
	{ "MV18_FALL_AFTER_01", 0xFA },
	{ "MV18_FALL_AFTER_02", 0xE3 },
	{ "MV19_JMP_PT_01", 0xFC },
	{ "MV19_JMP_PT_02", 0xFD },
	{ "MV20_JMP_PT_REACT_01", 0xFE },
	{ "MV21_JUMP_SMALL_01", 0x102 },
	{ "MV21_JUMP_SMALL_02", 0xFF },
	{ "MV21_JUMP_SMALL_03", 0x100 },
	{ "MV21_JUMP_SMALL_04", 0x101 },
	{ "MV21_JUMP_SMALL_05", 0x102 },
	{ "MV22_JUMP_MID_01", 0x104 },
	{ "MV23_JUMP_LARGE_01", 0x105 },
	{ "MV23_JUMP_LARGE_02", 0x106 },
	{ "MV23_JUMP_LARGE_03", 0x107 },
	{ "MV24_JUMP_SPECIAL_01", 0x108 },
	{ "MV24_JUMP_SPECIAL_02", 0x109 },
	{ "MV24_JUMP_SPECIAL_03", 0x10A },
	{ "MV24_JUMP_SPECIAL_04", 0x10B },
	{ "MV24_JUMP_SPECIAL_05", 0x10C },
	{ "MV24_JUMP_SPECIAL_06", 0x10D },
	{ "MV25A_JUMP_HUGE_01", 0x10E },
	{ "MV25A_JUMP_HUGE_02", 0x10F },
	{ "MV25A_WIRE_ROLL_01A", 0x110 },
	{ "MV25A_WIRE_ROLL_01B", 0x111 },
	{ "MV25A_WIRE_ROLL_01C", 0x112 },
	{ "MV25A_WIRE_ROLL_02A", 0x113 },
	{ "MV25A_WIRE_ROLL_02B", 0x114 },
	{ "MV25A_WIRE_ROLL_02C", 0x115 },
	{ "MV26_JUMP_REACT_01", 0x116 },
	{ "MV26_JUMP_REACT_02", 0x117 },
	{ "MV26_JUMP_REACT_03", 0x118 },
	{ "MV26_JUMP_REACT_04", 0x119 },
	{ "MV26_JUMP_REACT_05", 0x11A },
	{ "MV26_JUMP_REACT_06", 0x11B },
	{ "MV26_JUMP_REACT_07", 0x11C },
	{ "MV26_JUMP_REACT_08", 0x11D },
	{ "MV27_SPRISE_01", 0x11E },
	{ "MV27_SPRISE_02", 0x11F },
	{ "MV28_SPRISE_SMALL_01", 0x120 },
	{ "MV28_SPRISE_SMALL_02", 0x121 },
	{ "MV29_SPRISE_REACT_01", 0x122 },
	{ "MV29_SPRISE_REACT_02", 0x123 },
	{ "MV29_SPRISE_REACT_03", 0x124 },
	{ "MV30_FRIGHT_01", 0x125 },
	{ "MV30_FRIGHT_02", 0x126 },
	{ "MV31_OPEN_DOOR_01", 0x12A },
	{ "MV31_OPEN_DOOR_02", 0x127 },
	{ "MV31_OPEN_DOOR_03", 0x128 },
	{ "MV31_OPEN_DOOR_04", 0x129 },
	{ "MV33_T_01", 0x12B },
	{ "MV33_T_02", 0x12C },
	{ "MV33_T_03", 0x12D },
	{ "MV33_T_04", 0x12E },
	{ "MV33_T_05", 0x12F },
	{ "MV33_T_06", 0x130 },
	{ "MV34_ACTION_T_01", 0x131 },
	{ "MV34_ACTION_T_02", 0x132 },
	{ "MV34_ACTION_T_03", 0x133 },
	{ "MV38_EXERT_INST_T_01", 0x134 },
	{ "MV41_JUMP_T_01", 0x136 },
	{ "MV41_JUMP_T_02", 0x137 },
	{ "MV41_JUMP_T_03", 0x135 },
	{ "MV42_JUMP_HUGE_T_01", 0x138 },
	{ "MV42_JUMP_HUGE_T_02", 0x139 },
	{ "MV46A_OPEN_DOOR_T_01", 0x13A },
	{ "MV46A_OPEN_DOOR_T_02", 0x13B },
	{ "MV46B_NEXT_STG_T_01", 0x13C },
	{ "MV46B_NEXT_STG_T_02", 0x13D },
	{ "MV46B_NEXT_STG_T_03", 0x13E },
	{ "MV_UNTEI_01", 0x101 },
	{ "MV_UNTEI_02", 0x100 },
	{ "MV_UNTEI_03", 0xFF },
	{ "MV_IBIKI", 0x140 },
	{ "MV_IBIKI2", 0x141 },
	{ "MV_DOOR", 0x13F },
	{ "MV_BYEBYE", 0xCD },
};

// DSPADPCM_FILTER (JASDriverTables.cpp): the AFC predictor pairs, Q11.
const int16_t kCoef[16][2] = {
	{ 0, 0 },          { 0x0800, 0 },     { 0, 0x0800 },     { 0x0400, 0x0400 },
	{ 0x1000, -0x800 }, { 0x0E00, -0x600 }, { 0x0C00, -0x400 }, { 0x1200, -0xA00 },
	{ 0x1068, -0x8C8 }, { 0x12C0, -0x8FC }, { 0x1400, -0xC00 }, { 0x0800, -0x800 },
	{ 0x0400, -0x400 }, { -0x400, 0x0400 }, { -0x400, 0 },      { -0x800, 0 },
};

// AFC 4-bit: per 16 samples one byte (scale exponent, predictor) and 16
// residual nibbles, decoded as the mixer does (platform/audio/dsp_mixer.cpp).
// Each frame takes the predictor and scale that decode closest to the input.
std::vector<uint8_t> afc_encode(const std::vector<int16_t>& pcm)
{
	const size_t frames = (pcm.size() + 15) / 16;
	std::vector<uint8_t> out(frames * 9);
	int32_t h1 = 0, h2 = 0;
	for (size_t f = 0; f < frames; f++) {
		int32_t x[16];
		for (int k = 0; k < 16; k++)
			x[k] = f * 16 + k < pcm.size() ? pcm[f * 16 + k] : 0;
		int64_t best = INT64_MAX;
		int bestP = 0, bestS = 0, bestH1 = 0, bestH2 = 0;
		int8_t bestR[16] = {};
		for (int p = 0; p < 16; p++)
			for (int s = 0; s < 16; s++) {
				int32_t a1 = h1, a2 = h2;
				int64_t err = 0;
				int8_t r[16];
				for (int k = 0; k < 16 && err < best; k++) {
					const int32_t pred = kCoef[p][0] * a1 + kCoef[p][1] * a2;
					const double q     = ((double)x[k] * 2048.0 - pred) / (double)(1 << (s + 11));
					int v              = (int)(q < 0 ? q - 0.5 : q + 0.5);
					v                  = v < -8 ? -8 : v > 7 ? 7 : v;
					int32_t y          = (int32_t)(((int64_t)(v * (1 << s)) * 2048 + pred) >> 11);
					y                  = y < -32768 ? -32768 : y > 32767 ? 32767 : y;
					err += (int64_t)(y - x[k]) * (y - x[k]);
					r[k] = (int8_t)v;
					a2   = a1;
					a1   = y;
				}
				if (err < best) {
					best  = err;
					bestP = p;
					bestS = s;
					bestH1 = a1;
					bestH2 = a2;
					memcpy(bestR, r, sizeof r);
				}
			}
		uint8_t* o = &out[f * 9];
		o[0]       = (uint8_t)(bestS << 4 | bestP);
		for (int k = 0; k < 16; k += 2)
			o[1 + k / 2] = (uint8_t)((bestR[k] & 15) << 4 | (bestR[k + 1] & 15));
		h1 = bestH1;
		h2 = bestH2;
	}
	return out;
}

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

// A PCM WAV (8 or 16 bits, any channels, mixed to one), or false.
bool read_wav(const std::string& path, std::vector<int16_t>& pcm, uint32_t& rate)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::vector<uint8_t> d;
	uint8_t buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		d.insert(d.end(), buf, buf + n);
	fclose(f);
	if (d.size() < 12 || memcmp(&d[0], "RIFF", 4) || memcmp(&d[8], "WAVE", 4))
		return false;
	int channels = 0, bits = 0;
	for (size_t p = 12; p + 8 <= d.size();) {
		const uint32_t size = le32(&d[p + 4]);
		const size_t body   = p + 8;
		if (body + size > d.size())
			break;
		if (!memcmp(&d[p], "fmt ", 4) && size >= 16) {
			if (le16(&d[body]) != 1)
				return false; // not PCM
			channels = le16(&d[body + 2]);
			rate     = le32(&d[body + 4]);
			bits     = le16(&d[body + 14]);
		} else if (!memcmp(&d[p], "data", 4) && channels > 0 && (bits == 8 || bits == 16)) {
			const int bytes  = bits / 8 * channels;
			const size_t cnt = size / bytes;
			pcm.resize(cnt);
			for (size_t i = 0; i < cnt; i++) {
				int32_t sum = 0;
				for (int c = 0; c < channels; c++) {
					const uint8_t* s = &d[body + i * bytes + c * bits / 8];
					sum += bits == 16 ? (int16_t)le16(s) : ((int)s[0] - 128) * 256;
				}
				pcm[i] = (int16_t)(sum / channels);
			}
			return rate >= 1000 && rate <= 96000 && !pcm.empty();
		}
		p = body + size + (size & 1);
	}
	return false;
}

struct Original {
	PortCharacterVoiceWave entry;
	std::vector<uint8_t> data;
};
std::vector<Original> g_original; // Mario's, kept the first time
int g_applied = 0;                 // the character whose voice is in place (0: Mario)

void restore(PortCharacterVoiceWave* waves, int count)
{
	for (int i = 0; i < count && i < (int)g_original.size(); i++) {
		const Original& o = g_original[i];
		if (uint8_t* p = port_aram_ptr(o.entry.aram + o.entry.offset))
			memcpy(p, o.data.data(), o.data.size());
		waves[i] = o.entry;
	}
}

} // namespace

extern "C" {

int port_character_voice_waves(PortCharacterVoiceWave* waves, int max)
{
	int n = 0;
	for (const Cue& c : kCues) {
		bool seen = false;
		for (int i = 0; i < n && !seen; i++)
			seen = waves[i].id == c.wave;
		if (!seen && n < max)
			waves[n++].id = c.wave;
	}
	return n;
}

void port_character_voice_apply(int index, PortCharacterVoiceWave* waves, int count)
{
	if (g_original.empty()) {
		for (int i = 0; i < count; i++) {
			const uint8_t* p = port_aram_ptr(waves[i].aram + waves[i].offset);
			if (!p)
				return;
			g_original.push_back({ waves[i], std::vector<uint8_t>(p, p + waves[i].size) });
		}
	}
	if (index == g_applied || (int)g_original.size() != count)
		return;
	restore(waves, count);
	g_applied = index;

	const char* voice = port_character_voice_dir(index);
	if (!voice)
		return;
	const std::string dir = voice;

	// Each wave's file: the first of its cues that has one.
	struct Clip {
		std::vector<uint8_t> afc;
		uint32_t rate, samples, offset;
		bool placed;
	};
	std::vector<Clip> clips;
	std::unordered_map<std::string, int> byFile;
	std::vector<int> clipOf(count, -1);
	for (int i = 0; i < count; i++)
		for (const Cue& c : kCues) {
			if (c.wave != waves[i].id)
				continue;
			const std::string path = dir + c.name + ".wav";
			std::vector<int16_t> pcm;
			uint32_t rate = 0;
			if (!read_wav(path, pcm, rate))
				continue;
			// The same sound under several cues is stored once.
			std::string key((const char*)pcm.data(), pcm.size() * 2);
			key.append((const char*)&rate, sizeof rate);
			auto it = byFile.find(key);
			if (it == byFile.end()) {
				it = byFile.emplace(key, (int)clips.size()).first;
				clips.push_back({ afc_encode(pcm), rate, (uint32_t)pcm.size(), 0, false });
			}
			clipOf[i] = it->second;
			break;
		}

	// Room: the bytes of the waves being replaced, 32-byte aligned as the archive's.
	std::vector<std::pair<uint32_t, uint32_t>> room;
	for (int i = 0; i < count; i++)
		if (clipOf[i] >= 0)
			room.push_back({ waves[i].offset, waves[i].offset + waves[i].size });
	std::sort(room.begin(), room.end());
	std::vector<std::pair<uint32_t, uint32_t>> spans;
	for (auto& r : room) {
		if (!spans.empty() && r.first <= spans.back().second)
			spans.back().second = std::max(spans.back().second, r.second);
		else
			spans.push_back(r);
	}
	int placed = 0, cut = 0;
	for (Clip& c : clips) {
		for (auto& f : spans) {
			const uint32_t at = (f.first + 31) & ~31u;
			if (at >= f.second)
				continue;
			uint32_t size = (uint32_t)c.afc.size();
			if (at + size > f.second) {
				// The last place left: as much of it as fits.
				if (&f != &spans.back() || at + 9 > f.second)
					continue;
				size      = (f.second - at) / 9 * 9;
				c.samples = std::min(c.samples, size / 9 * 16);
				cut++;
			}
			memcpy(port_aram_ptr(waves[0].aram + at), c.afc.data(), size);
			c.afc.resize(size);
			c.offset = at;
			c.placed = true;
			f.first  = at + size;
			placed++;
			break;
		}
	}
	int replaced = 0;
	for (int i = 0; i < count; i++) {
		if (clipOf[i] < 0)
			continue;
		const Clip& c = clips[clipOf[i]];
		PortCharacterVoiceWave& w = waves[i];
		w.offset    = c.placed ? c.offset : w.offset;
		w.size      = c.placed ? (uint32_t)c.afc.size() : 0;
		w.count     = c.placed ? c.samples : 0;
		w.rate      = (float)c.rate;
		w.loop      = 0;
		w.loopStart = 0;
		w.loopEnd   = 0;
		w.last      = 0;
		w.penult    = 0;
		replaced += c.placed;
	}
	port_log("[character] %s's voice: %d of %d voice waves from %d sounds in %s%s\n", port_character_name(index),
	         replaced, count, placed, dir.c_str(), cut ? " (the last cut short: no more room)" : "");
}

} // extern "C"
