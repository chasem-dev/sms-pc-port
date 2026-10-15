// The multiplayer session's rules (mp_game_api.h): stage sessions, warps,
// the shared flags, items and players' looks, between the game half and the
// network (mp_net.h).
#include "../mp_game_api.h"
#include "../net/mp_net.h"
#include "../net/mp_wire.h"
#include "mp_flags.h"

#include <algorithm>
#include <ctype.h>
#include <cmath>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#if defined(SMS_HAVE_GAME_BUILD_H) && !defined(SMS_GAME_BUILD)
#include "sms_game_build.h" // ext/ext.cmake: the port's commit, Eclipse marked
#endif
#ifndef SMS_GAME_BUILD
#define SMS_GAME_BUILD "dev"
#endif

namespace mp {
namespace {

// The game's reliable messages.
enum Kind : uint8_t {
	K_STAGE = 1,    // host -> client: area, episode, session
	K_FLAGS,        // host -> client: a FlagUpdate's message
	K_REPORT,       // client -> host: a FlagReport's message
	K_READY,        // client -> host: arrived in session
	K_TAKEN,        // either way: session, item key
	K_TAKEN_LIST,   // host -> client: session, keys (all taken so far)
	K_ENEMY_KILL,   // client -> host: session, manager, index
	K_TAKEN_AT,     // either way: session, item type, position (an item that appeared during the stage)
	K_SHINE_GET,    // either way: session, slot, Shine event id, position (someone touched a Shine)
	K_LEAVING,      // host -> client: area, episode (0xFF: the game decides) the host is setting off for;
	                // then, as it goes (moveStage), its travel: 1, session flags (0x30000 on), movie (0xFF: none)
	K_DEMO,         // either way: session, slot, the game's description of a cutscene (mp_game.cpp)
	K_BOSS_HIT,     // client -> host: session, part, kind, count (a hit on its copy of the boss, for the real one)
	K_CLEAN,        // either way: session, count, goop cleaned (x, y, z halved, size), the host passing it on
	K_HIT,          // either way: session, count, hits a player landed on shared objects (mp_game.cpp), passed on
	K_CARRY,        // either way: session, slot, manager, index, action, position, velocity, yaw (a carried object)
	K_MIRROR,       // either way: session, count, objects' states from the game that owns them (mp_game.cpp), passed on
};
// The host's boss rides at the end of the world snapshot, after the enemies.
const size_t kMaxBoss = 200;
// Items that appear during a stage (from a box, a defeated enemy, a red coin
// switch) are named by their type and where they are, not their place in the
// item manager (every game fills that differently); at most this many a stage.
const size_t kMaxTakenAt = 512;
// Losing the host: tries to reach it again, this far apart.
const int kReconnectTries = 8;
const uint32_t kReconnectMs = 3000;
// Goop cleaned (TPollutionManager::clean): sent in batches, kept by the host
// for the session (a newcomer, or a player starting the stage again, gets it
// all). A spot cleaned again within kCleanFresh ms, no wider, is not sent
// again: spraying cleans the same few spots many times a second.
const size_t kMaxCleanLog = 16384;
const size_t kCleanBatch  = 120; // 8 bytes each, within a message
const uint32_t kCleanFresh = 2000;

const char* const kCharacters[MP_MAX_PLAYERS] = {"Mario", "Luigi", "Peach", "Metal Mario"};
const char* const kWorlds[] = {"the Airstrip", "Delfino Plaza", "Bianco Hills", "Ricco Harbor", "Gelato Beach",
                               "Pinna Park", "Sirena Beach", "Pianta Village", "Noki Bay", "Corona Mountain"}; // SMS_getShineStage's order
const size_t kPoseSize = 4 + 4 + 4 + 48 + 4 + 16 + 4 + 8 + 4 + 4 + 4 + 48 + 48 + 12 + 4;
const size_t kEnemySize = 18;

struct Controller {
	int role = MPG_ROLE_OFF;
	MpgGame game{};
	bool wasPlaying = false;

	// host
	uint32_t session    = 0; // the open stage session
	int sessionArea     = -1, sessionEpisode = -1;
	std::vector<uint32_t> taken;
	HostFlags hostFlags;
	FlagValues hostValues;       // the host's flags as of the last sync
	std::deque<std::pair<int, FlagReport>> reports;
	FlagReportReader reportReaders[MP_MAX_PLAYERS];
	bool shineExit       = false;
	int32_t sessionRed   = 0;   // red coins this session, kept over a reload
	bool restoreRed      = false;
	bool restartSession  = false;
	unsigned waitingStage = 0; // players who joined while the host was between stages: told once it has arrived

	// client: the host's travel (as it went: its session flags and movie), for
	// this game to arrive the same way
	bool travelKnown      = false;
	uint32_t travelFlags  = 0;
	int travelMovie       = -1;

	// client: reconnecting after losing the host (a few tries, a few seconds apart)
	MpConfig cfg{};
	bool hadSlot          = false;
	uint32_t reconnectAt  = 0; // mp_clock_ms; 0: not reconnecting
	int reconnectTries    = 0;

	// client
	bool hasTarget     = false;
	int targetArea     = -1, targetEpisode = -1;
	uint32_t targetSession = 0;
	uint32_t localSession  = 0;
	ClientFlags clientFlags;
	FlagUpdateReader updateReader;

	std::deque<uint32_t> removals;
	// items that appeared during the stage and were taken, by anyone, this session
	std::vector<MpgTakenAt> takenAt;
	// Shines another player touched, for this game to play the Shine Get too
	struct ShineGet {
		int slot, eventId;
		float pos[3];
	};
	std::deque<ShineGet> shineGets;
	// cutscenes another player's game started, for this game to play too
	struct Demo {
		int slot;
		std::vector<uint8_t> data;
	};
	std::deque<Demo> demos;
	// host: its boss's state for the snapshot, and the hits the others report
	std::vector<uint8_t> boss;
	struct BossHit {
		int unit, kind, count;
	};
	std::deque<BossHit> bossHits;
	// enemies: the host's list and where the next share starts; a client's
	// newest share; kills reported to the host
	std::vector<MpgEnemy> enemies;
	size_t enemyNext = 0, enemyBatch = 0;
	std::deque<std::pair<int, int>> enemyKills;
	// goop: what this game cleaned and has yet to send; what the others
	// cleaned, for this game to clean; host: the session's, for newcomers;
	// the spots seen lately (64 by 128 by 64 units), widest size and when
	struct Clean {
		int16_t x, y, z;
		uint16_t size;
	};
	struct Seen {
		uint16_t size;
		uint32_t at;
	};
	std::vector<Clean> cleanOut;
	std::deque<Clean> cleanIn;
	std::vector<Clean> cleanLog;
	std::unordered_map<uint32_t, Seen> cleanSeen;
	// hits players landed on shared objects (NPCs, map objects): this
	// game's, to send (repeats within a frame counted); the others', to land here
	struct Hit {
		uint8_t slot, manager;
		uint16_t index;
		uint8_t message, water, count;
		int16_t x, y, z;
	};
	std::vector<Hit> hitOut;
	std::deque<Hit> hitIn;
	// objects players pick up, put down and throw: the others', to do here
	struct Carry {
		int slot, manager, index, action;
		float pos[3], vel[3];
		int16_t yaw;
	};
	std::deque<Carry> carries;
	// mirrored objects' states: this game's to send; the others', to take
	struct Mirror {
		uint8_t slot, manager, len;
		uint16_t index;
		uint8_t data[48];
	};
	std::vector<Mirror> mirrorOut;
	std::deque<Mirror> mirrorIn;

	// each other player's recent poses, for drawing them smoothly
	struct Timed {
		MpgPose pose;
		uint32_t received; // on our mp_clock_ms
	};
	std::deque<Timed> poses[MP_MAX_PLAYERS];
	// how far behind our clock their stamps arrive, at least (their clock's
	// offset plus the quickest trip), measured afresh every few seconds
	int32_t lagMin[MP_MAX_PLAYERS]     = {};
	int32_t lagNextMin[MP_MAX_PLAYERS] = {};
	uint32_t lagSince[MP_MAX_PLAYERS]  = {};
	bool lagKnown[MP_MAX_PLAYERS]      = {};
	// the moment each player is drawn at, on their clock: it moves on by a
	// steady step a frame (our frames' usual length) and leans slowly toward
	// where it should be, so their movement is even when our frames are not
	double drawAt[MP_MAX_PLAYERS]  = {};
	bool drawing[MP_MAX_PLAYERS]   = {};
	// how much of their game's time passes in a frame of ours, on average
	// (their updates come in bursts; their game may run faster or slower)
	double theirsPerFrame[MP_MAX_PLAYERS] = {};
	uint32_t newestSeen[MP_MAX_PLAYERS]   = {};
	int framesSinceNew[MP_MAX_PLAYERS]    = {}; // our frames since their newest pose came
	// the usual gap between their poses, in their game's time
	double poseGap[MP_MAX_PLAYERS] = {};
	// our game's clock: the frames played, in ms (poses are stamped with it,
	// so a player's movement is spaced as their game played it)
	double gameMs  = 0.0;
	double frameMs = 1000.0 / 30.0;
	std::deque<std::string> notices;
	MpgPose pose{};
	bool havePose = false;
};

Controller g;
std::mutex g_lock;
const bool g_debug = getenv("SMS_MULTIPLAYER_DEBUG") != nullptr; // the game thread is the only caller; this keeps tests honest

void notice(const std::string& text)
{
	fprintf(stderr, "[multiplayer] %s\n", text.c_str());
	g.notices.push_back(text);
	while (g.notices.size() > 8)
		g.notices.pop_front();
}

void send(int slot, Kind kind, const std::vector<uint8_t>& data) { mp_send(slot, kind, data.data(), data.size()); }

void sendStage(int slot)
{
	uint8_t buf[16];
	Writer w(buf, sizeof buf);
	w.u8((uint8_t)g.sessionArea);
	w.u8((uint8_t)g.sessionEpisode);
	w.u32(g.session);
	mp_send(slot, K_STAGE, buf, w.len);
}

void sendFull(int slot)
{
	if (g.hostValues.size() != sharedFlags().size())
		return;
	std::vector<std::vector<uint8_t>> msgs;
	writeUpdate(g.hostFlags.full(slot, g.hostValues), MP_MAX_MESSAGE, msgs);
	for (auto& m : msgs)
		send(slot, K_FLAGS, m);
}

void sendTakenList(int slot)
{
	size_t at = 0;
	do {
		size_t n = std::min<size_t>(200, g.taken.size() - at);
		std::vector<uint8_t> buf(10 + n * 4);
		Writer w(buf.data(), buf.size());
		w.u32(g.session);
		w.u16((uint16_t)n);
		for (size_t i = at; i < at + n; i++)
			w.u32(g.taken[i]);
		buf.resize(w.len);
		send(slot, K_TAKEN_LIST, buf);
		at += n;
	} while (at < g.taken.size());
}

std::vector<uint8_t> takenAtMessage(uint32_t session, const MpgTakenAt& t)
{
	std::vector<uint8_t> buf(4 + 1 + 12);
	Writer w(buf.data(), buf.size());
	w.u32(session);
	w.u8(t.type);
	w.f32(t.pos[0]);
	w.f32(t.pos[1]);
	w.f32(t.pos[2]);
	return buf;
}

bool readTakenAt(Reader& r, MpgTakenAt& t)
{
	t.type   = r.u8();
	t.pos[0] = r.f32();
	t.pos[1] = r.f32();
	t.pos[2] = r.f32();
	return r.ok && t.pos[0] == t.pos[0] && t.pos[1] == t.pos[1] && t.pos[2] == t.pos[2]; // not NaN
}

std::vector<uint8_t> shineGetMessage(uint32_t session, int slot, int eventId, const float pos[3])
{
	std::vector<uint8_t> buf(4 + 1 + 1 + 12);
	Writer w(buf.data(), buf.size());
	w.u32(session);
	w.u8((uint8_t)slot);
	w.u8((uint8_t)eventId);
	w.f32(pos[0]);
	w.f32(pos[1]);
	w.f32(pos[2]);
	return buf;
}

bool readShineGet(Reader& r, Controller::ShineGet& out)
{
	out.slot    = r.u8();
	out.eventId = r.u8();
	out.pos[0]  = r.f32();
	out.pos[1]  = r.f32();
	out.pos[2]  = r.f32();
	return r.ok && out.slot < MP_MAX_PLAYERS && out.pos[0] == out.pos[0] && out.pos[1] == out.pos[1]
	       && out.pos[2] == out.pos[2];
}

void addTakenAt(const MpgTakenAt& t)
{
	if (g.takenAt.size() < kMaxTakenAt)
		g.takenAt.push_back(t);
}

void takeOnce(uint32_t key)
{
	for (uint32_t k : g.taken)
		if (k == key)
			return;
	g.taken.push_back(key);
}

// ------------------------------------------------------------------ goop

void forgetCleans()
{
	g.mirrorOut.clear();
	g.mirrorIn.clear();
	g.carries.clear();
	g.hitOut.clear();
	g.hitIn.clear();
	g.cleanOut.clear();
	g.cleanIn.clear();
	g.cleanLog.clear();
	g.cleanSeen.clear();
}

int16_t halved(float v)
{
	const float h = v * 0.5f;
	return (int16_t)(h > 32767.0f ? 32767.0f : h < -32768.0f ? -32768.0f : h);
}

// Whether this clean says something the spot's last one (sent or received) did not.
bool freshClean(const Controller::Clean& c)
{
	const uint32_t key = ((uint32_t)(c.x >> 5) & 0x7FF) << 21 | ((uint32_t)(c.y >> 6) & 0x3FF) << 11
	                     | ((uint32_t)(c.z >> 5) & 0x7FF);
	const uint32_t now = mp_clock_ms();
	auto it = g.cleanSeen.find(key);
	if (it != g.cleanSeen.end() && it->second.size >= c.size && now - it->second.at < kCleanFresh)
		return false;
	if (g.cleanSeen.size() > 65536)
		g.cleanSeen.clear();
	g.cleanSeen[key] = {c.size, now};
	return true;
}

void logClean(const Controller::Clean& c)
{
	if (g.role == MPG_ROLE_HOST && g.cleanLog.size() < kMaxCleanLog)
		g.cleanLog.push_back(c);
}

std::vector<uint8_t> cleanMessage(uint32_t session, const Controller::Clean* list, size_t n)
{
	std::vector<uint8_t> buf(6 + n * 8);
	Writer w(buf.data(), buf.size());
	w.u32(session);
	w.u16((uint16_t)n);
	for (size_t i = 0; i < n; i++) {
		w.s16(list[i].x);
		w.s16(list[i].y);
		w.s16(list[i].z);
		w.u16(list[i].size);
	}
	return buf;
}

// `list` to `slot` (-1: every client but `except`), in batches.
void sendCleans(int slot, int except, uint32_t session, const std::vector<Controller::Clean>& list)
{
	for (size_t at = 0; at < list.size(); at += kCleanBatch) {
		const size_t n = list.size() - at < kCleanBatch ? list.size() - at : kCleanBatch;
		const std::vector<uint8_t> buf = cleanMessage(session, list.data() + at, n);
		if (slot >= 0)
			send(slot, K_CLEAN, buf);
		else
			for (int i = 1; i < MP_MAX_PLAYERS; i++)
				if (i != except && (mp_players() & (1u << i)))
					send(i, K_CLEAN, buf);
	}
}

// This game's cleaning so far, to the others (every frame).
void flushCleans()
{
	if (g.cleanOut.empty())
		return;
	if (g.role == MPG_ROLE_HOST && g.session)
		sendCleans(-1, 0, g.session, g.cleanOut);
	else if (g.role == MPG_ROLE_CLIENT && g.localSession)
		sendCleans(0, -1, g.localSession, g.cleanOut);
	g.cleanOut.clear();
}

// Another game's cleaning: to clean here too; the host keeps it and passes it on.
void readCleans(Reader& r, int from)
{
	const uint32_t s = r.u32();
	const int n = r.u16();
	const uint32_t want = g.role == MPG_ROLE_HOST ? g.session : g.localSession;
	if (!r.ok || !s || s != want || n > (int)kCleanBatch)
		return;
	std::vector<Controller::Clean> got;
	for (int i = 0; i < n && r.ok; i++) {
		Controller::Clean c;
		c.x    = r.s16();
		c.y    = r.s16();
		c.z    = r.s16();
		c.size = r.u16();
		if (!r.ok)
			break;
		freshClean(c); // so this game's echo of it is not sent back
		got.push_back(c);
		if (g.cleanIn.size() < kMaxCleanLog)
			g.cleanIn.push_back(c);
		logClean(c);
	}
	if (g.role == MPG_ROLE_HOST && !got.empty())
		sendCleans(-1, from, s, got);
}

// ------------------------------------------------------------------ hits on shared objects

const size_t kHitBatch = 60; // 13 bytes each, within a message
const size_t kMaxHitsIn = 512;

std::vector<uint8_t> hitMessage(uint32_t session, const Controller::Hit* list, size_t n)
{
	std::vector<uint8_t> buf(5 + n * 13);
	Writer w(buf.data(), buf.size());
	w.u32(session);
	w.u8((uint8_t)n);
	for (size_t i = 0; i < n; i++) {
		const Controller::Hit& h = list[i];
		w.u8(h.slot);
		w.u8(h.manager);
		w.u16(h.index);
		w.u8(h.message);
		w.u8(h.water);
		w.u8(h.count);
		w.s16(h.x);
		w.s16(h.y);
		w.s16(h.z);
	}
	return buf;
}

void sendHits(int slot, int except, uint32_t session, const std::vector<Controller::Hit>& list)
{
	for (size_t at = 0; at < list.size(); at += kHitBatch) {
		const size_t n = list.size() - at < kHitBatch ? list.size() - at : kHitBatch;
		const std::vector<uint8_t> buf = hitMessage(session, list.data() + at, n);
		if (slot >= 0)
			send(slot, K_HIT, buf);
		else
			for (int i = 1; i < MP_MAX_PLAYERS; i++)
				if (i != except && (mp_players() & (1u << i)))
					send(i, K_HIT, buf);
	}
}

void flushHits()
{
	if (g.hitOut.empty())
		return;
	if (g.role == MPG_ROLE_HOST && g.session)
		sendHits(-1, 0, g.session, g.hitOut);
	else if (g.role == MPG_ROLE_CLIENT && g.localSession)
		sendHits(0, -1, g.localSession, g.hitOut);
	g.hitOut.clear();
}

void readHits(Reader& r, int from)
{
	const uint32_t s = r.u32();
	const int n = r.u8();
	const uint32_t want = g.role == MPG_ROLE_HOST ? g.session : g.localSession;
	if (!r.ok || !s || s != want || n > (int)kHitBatch)
		return;
	std::vector<Controller::Hit> got;
	for (int i = 0; i < n && r.ok; i++) {
		Controller::Hit h;
		h.slot    = r.u8();
		h.manager = r.u8();
		h.index   = r.u16();
		h.message = r.u8();
		h.water   = r.u8();
		h.count   = r.u8();
		h.x       = r.s16();
		h.y       = r.s16();
		h.z       = r.s16();
		if (!r.ok || h.slot >= MP_MAX_PLAYERS || !h.count)
			break;
		if (g.role == MPG_ROLE_HOST)
			h.slot = (uint8_t)from; // a client speaks for itself only
		got.push_back(h);
		if (g.hitIn.size() < kMaxHitsIn)
			g.hitIn.push_back(h);
	}
	if (g.role == MPG_ROLE_HOST && !got.empty())
		sendHits(-1, from, s, got);
}

// ------------------------------------------------------------------ carried objects

std::vector<uint8_t> carryMessage(uint32_t session, const Controller::Carry& c)
{
	std::vector<uint8_t> buf(35);
	Writer w(buf.data(), buf.size());
	w.u32(session);
	w.u8((uint8_t)c.slot);
	w.u8((uint8_t)c.manager);
	w.u16((uint16_t)c.index);
	w.u8((uint8_t)c.action);
	for (int i = 0; i < 3; i++)
		w.f32(c.pos[i]);
	for (int i = 0; i < 3; i++)
		w.f32(c.vel[i]);
	w.s16(c.yaw);
	return buf;
}

void readCarry(Reader& r, int from)
{
	const uint32_t s = r.u32();
	Controller::Carry c;
	c.slot    = r.u8();
	c.manager = r.u8();
	c.index   = r.u16();
	c.action  = r.u8();
	for (int i = 0; i < 3; i++)
		c.pos[i] = r.f32();
	for (int i = 0; i < 3; i++)
		c.vel[i] = r.f32();
	c.yaw = r.s16();
	const uint32_t want = g.role == MPG_ROLE_HOST ? g.session : g.localSession;
	if (!r.ok || !s || s != want || c.slot >= MP_MAX_PLAYERS)
		return;
	for (int i = 0; i < 3; i++)
		if (!(c.pos[i] == c.pos[i]) || !(c.vel[i] == c.vel[i]) || c.pos[i] > 1e6f || c.pos[i] < -1e6f
		    || c.vel[i] > 1e4f || c.vel[i] < -1e4f)
			return;
	if (g.role == MPG_ROLE_HOST) {
		c.slot = from; // a client speaks for itself only
		for (int i = 1; i < MP_MAX_PLAYERS; i++)
			if (i != from && (mp_players() & (1u << i)))
				send(i, K_CARRY, carryMessage(s, c));
	}
	if (g.carries.size() < 64)
		g.carries.push_back(c);
}

// ------------------------------------------------------------------ mirrored objects

const size_t kMirrorBatch = 16;
const size_t kMaxMirrorsIn = 256;

std::vector<uint8_t> mirrorMessage(uint32_t session, const Controller::Mirror* list, size_t n)
{
	size_t size = 5;
	for (size_t i = 0; i < n; i++)
		size += 5 + list[i].len;
	std::vector<uint8_t> buf(size);
	Writer w(buf.data(), buf.size());
	w.u32(session);
	w.u8((uint8_t)n);
	for (size_t i = 0; i < n; i++) {
		w.u8(list[i].slot);
		w.u8(list[i].manager);
		w.u16(list[i].index);
		w.u8(list[i].len);
		w.bytes(list[i].data, list[i].len);
	}
	return buf;
}

void sendMirrors(int slot, int except, uint32_t session, const std::vector<Controller::Mirror>& list)
{
	for (size_t at = 0; at < list.size(); at += kMirrorBatch) {
		const size_t n = list.size() - at < kMirrorBatch ? list.size() - at : kMirrorBatch;
		const std::vector<uint8_t> buf = mirrorMessage(session, list.data() + at, n);
		if (slot >= 0)
			send(slot, K_MIRROR, buf);
		else
			for (int i = 1; i < MP_MAX_PLAYERS; i++)
				if (i != except && (mp_players() & (1u << i)))
					send(i, K_MIRROR, buf);
	}
}

void flushMirrors()
{
	if (g.mirrorOut.empty())
		return;
	if (g.role == MPG_ROLE_HOST && g.session)
		sendMirrors(-1, 0, g.session, g.mirrorOut);
	else if (g.role == MPG_ROLE_CLIENT && g.localSession)
		sendMirrors(0, -1, g.localSession, g.mirrorOut);
	g.mirrorOut.clear();
}

void readMirrors(Reader& r, int from)
{
	const uint32_t s = r.u32();
	const int n = r.u8();
	const uint32_t want = g.role == MPG_ROLE_HOST ? g.session : g.localSession;
	if (!r.ok || !s || s != want || n > (int)kMirrorBatch)
		return;
	std::vector<Controller::Mirror> got;
	for (int i = 0; i < n && r.ok; i++) {
		Controller::Mirror m;
		m.slot    = r.u8();
		m.manager = r.u8();
		m.index   = r.u16();
		m.len     = r.u8();
		if (!r.ok || m.len > sizeof m.data || m.slot >= MP_MAX_PLAYERS)
			break;
		r.bytes(m.data, m.len);
		if (!r.ok)
			break;
		if (g.role == MPG_ROLE_HOST)
			m.slot = (uint8_t)from; // a client speaks for itself only
		got.push_back(m);
		if (g.mirrorIn.size() < kMaxMirrorsIn)
			g.mirrorIn.push_back(m);
	}
	if (g.role == MPG_ROLE_HOST && !got.empty())
		sendMirrors(-1, from, s, got);
}

void hostMessage(const MpEvent& ev)
{
	Reader r(ev.data, ev.size);
	switch (ev.kind) {
	case K_REPORT:
		if (ev.slot > 0 && ev.slot < MP_MAX_PLAYERS && g.reportReaders[ev.slot].add(ev.data, ev.size))
			g.reports.emplace_back(ev.slot, g.reportReaders[ev.slot].r);
		break;
	case K_READY: {
		uint32_t s = r.u32();
		if (r.ok && s == g.session && g.session) {
			sendTakenList(ev.slot);
			for (const MpgTakenAt& t : g.takenAt)
				send(ev.slot, K_TAKEN_AT, takenAtMessage(g.session, t));
			sendCleans(ev.slot, -1, g.session, g.cleanLog);
		}
		break;
	}
	case K_CLEAN:
		readCleans(r, ev.slot);
		break;
	case K_HIT:
		readHits(r, ev.slot);
		break;
	case K_CARRY:
		readCarry(r, ev.slot);
		break;
	case K_MIRROR:
		readMirrors(r, ev.slot);
		break;
	case K_SHINE_GET: {
		uint32_t s = r.u32();
		Controller::ShineGet sg;
		if (!readShineGet(r, sg) || s != g.session || !g.session)
			break;
		sg.slot = ev.slot; // a client speaks for itself only
		if (g_debug)
			fprintf(stderr, "[multiplayer] slot %d touched Shine %d\n", sg.slot, sg.eventId);
		if (g.shineGets.size() < 8)
			g.shineGets.push_back(sg);
		for (int i = 1; i < MP_MAX_PLAYERS; i++)
			if (i != ev.slot && (mp_players() & (1u << i)))
				send(i, K_SHINE_GET, shineGetMessage(s, sg.slot, sg.eventId, sg.pos));
		break;
	}
	case K_DEMO: {
		uint32_t s = r.u32();
		r.u8();
		if (!r.ok || s != g.session || !g.session || r.left() > 256)
			break;
		Controller::Demo d;
		d.slot = ev.slot;
		d.data.assign(r.p + r.pos, r.p + r.len);
		if (g.demos.size() < 4)
			g.demos.push_back(d);
		std::vector<uint8_t> buf(5 + d.data.size());
		Writer w(buf.data(), buf.size());
		w.u32(s);
		w.u8((uint8_t)ev.slot);
		w.bytes(d.data.data(), d.data.size());
		for (int i = 1; i < MP_MAX_PLAYERS; i++)
			if (i != ev.slot && (mp_players() & (1u << i)))
				send(i, K_DEMO, buf);
		break;
	}
	case K_TAKEN_AT: {
		uint32_t s = r.u32();
		MpgTakenAt t;
		if (!readTakenAt(r, t) || s != g.session || !g.session || g.takenAt.size() >= kMaxTakenAt)
			break;
		addTakenAt(t);
		for (int i = 1; i < MP_MAX_PLAYERS; i++)
			if (i != ev.slot && (mp_players() & (1u << i)))
				send(i, K_TAKEN_AT, takenAtMessage(s, t));
		break;
	}
	case K_BOSS_HIT: {
		uint32_t s = r.u32();
		Controller::BossHit h;
		h.unit  = r.u8();
		h.kind  = r.u8();
		h.count = r.u8();
		if (r.ok && s == g.session && g.session && g.bossHits.size() < 64)
			g.bossHits.push_back(h);
		break;
	}
	case K_ENEMY_KILL: {
		uint32_t s = r.u32();
		int manager = r.u8(), index = r.u16();
		if (r.ok && s == g.session && g.session)
			g.enemyKills.emplace_back(manager, index);
		break;
	}
	case K_TAKEN: {
		uint32_t s = r.u32(), key = r.u32();
		if (!r.ok || s != g.session || !g.session)
			break;
		takeOnce(key);
		g.removals.push_back(key);
		// everyone else hears of it
		uint8_t buf[8];
		Writer w(buf, sizeof buf);
		w.u32(s);
		w.u32(key);
		for (int i = 1; i < MP_MAX_PLAYERS; i++)
			if (i != ev.slot)
				mp_send(i, K_TAKEN, buf, w.len);
		break;
	}
	default:
		break;
	}
}

void clientMessage(const MpEvent& ev)
{
	Reader r(ev.data, ev.size);
	switch (ev.kind) {
	case K_STAGE: {
		int area = r.u8(), episode = r.u8();
		uint32_t s = r.u32();
		if (!r.ok)
			break;
		g.hasTarget     = true;
		g.targetArea    = area;
		g.targetEpisode = episode;
		g.targetSession = s;
		fprintf(stderr, "[multiplayer] the host is in area %d, episode %d (session %u)\n", area, episode, s);
		// we set off with the host and are there already: this is their session
		if (g.wasPlaying && !g.localSession && g.game.area == area && g.game.episode == episode) {
			g.localSession = s;
			g.takenAt.clear();
			forgetCleans();
			uint8_t buf[4];
			Writer w(buf, sizeof buf);
			w.u32(s);
			mp_send(0, K_READY, buf, w.len);
		}
		break;
	}
	case K_LEAVING: {
		int area = r.u8(), episode = r.u8();
		if (!r.ok)
			break;
		if (r.left() >= 6 && r.u8() == 1) {
			// the host is on its way now: how it went
			g.travelFlags = r.u32();
			const int movie = r.u8();
			if (r.ok) {
				g.travelKnown = true;
				g.travelMovie = movie == 0xFF ? -1 : movie;
				if (g_debug)
					fprintf(stderr, "[multiplayer] the host went with session flags %08x, movie %d\n", g.travelFlags,
					        g.travelMovie);
			}
		} else {
			g.travelKnown = false; // its travel comes when it goes
		}
		if (g.hasTarget && !g.targetSession && g.targetArea == area && g.targetEpisode == episode)
			break;
		// the host is setting off: go now, so everyone fades out together
		// (the session comes with K_STAGE once the host is there)
		g.hasTarget     = true;
		g.targetArea    = area;
		g.targetEpisode = episode;
		g.targetSession = 0;
		fprintf(stderr, "[multiplayer] the host is leaving for area %d, episode %d\n", area, episode);
		break;
	}
	case K_FLAGS:
		if (g.updateReader.add(ev.data, ev.size))
			g.clientFlags.hostUpdate(g.updateReader.u);
		break;
	case K_TAKEN: {
		uint32_t s = r.u32(), key = r.u32();
		if (r.ok && s && s == g.localSession)
			g.removals.push_back(key);
		break;
	}
	case K_TAKEN_AT: {
		uint32_t s = r.u32();
		MpgTakenAt t;
		if (readTakenAt(r, t) && s && s == g.localSession)
			addTakenAt(t);
		break;
	}
	case K_SHINE_GET: {
		uint32_t s = r.u32();
		Controller::ShineGet sg;
		if (readShineGet(r, sg) && s && s == g.localSession && g.shineGets.size() < 8)
			g.shineGets.push_back(sg);
		break;
	}
	case K_DEMO: {
		uint32_t s = r.u32();
		int slot = r.u8();
		if (!r.ok || !s || s != g.localSession || slot >= MP_MAX_PLAYERS || r.left() > 256 || g.demos.size() >= 4)
			break;
		Controller::Demo d;
		d.slot = slot;
		d.data.assign(r.p + r.pos, r.p + r.len);
		g.demos.push_back(d);
		break;
	}
	case K_CLEAN:
		readCleans(r, 0);
		break;
	case K_HIT:
		readHits(r, 0);
		break;
	case K_CARRY:
		readCarry(r, 0);
		break;
	case K_MIRROR:
		readMirrors(r, 0);
		break;
	case K_TAKEN_LIST: {
		uint32_t s = r.u32();
		uint16_t n = r.u16();
		for (int i = 0; i < n && r.ok; i++) {
			uint32_t key = r.u32();
			if (r.ok && s && s == g.localSession)
				g.removals.push_back(key);
		}
		break;
	}
	default:
		break;
	}
}

void events()
{
	MpEvent ev;
	while (mp_poll_event(&ev)) {
		switch (ev.type) {
		case MP_EV_CONNECTED:
			if (g.reconnectTries)
				notice("Back in the game as " + std::string(kCharacters[ev.slot]));
			else
				notice("Joined the game as player " + std::to_string(ev.slot + 1) + " (" + kCharacters[ev.slot] + ")");
			g.hadSlot        = true;
			g.reconnectAt    = 0;
			g.reconnectTries = 0;
			break;
		case MP_EV_FAILED: {
			char msg[256];
			mp_link_message(msg, sizeof msg);
			// the host gone quiet (not refusing, not closing): try again a few
			// times, staying where we are meanwhile
			const bool lost = strstr(msg, "Lost the connection") || (g.reconnectTries && strstr(msg, "No game answered"));
			if (g.role == MPG_ROLE_CLIENT && g.hadSlot && lost && g.reconnectTries < kReconnectTries) {
				g.reconnectAt = mp_clock_ms() + kReconnectMs;
				notice(g.reconnectTries ? "Still trying to reach the host..." : "Lost the host: reconnecting...");
				break;
			}
			g.reconnectAt = 0;
			notice(std::string("Multiplayer: ") + msg);
			g.hasTarget = false;
			break;
		}
		case MP_EV_PLAYER_JOINED:
			notice(std::string(ev.name[0] ? ev.name : "A player") + " joined as " + kCharacters[ev.slot]);
			if (g.role == MPG_ROLE_HOST) {
				g.hostFlags.forget(ev.slot);
				g.reportReaders[ev.slot] = FlagReportReader();
				// the newcomer's flags first, then where to go: not while the
				// host is between stages (that stage is being left), but once
				// it is in the next one
				sendFull(ev.slot);
				if (g.session && (g.game.playing || g.game.paused))
					sendStage(ev.slot);
				else
					g.waitingStage |= 1u << ev.slot;
			}
			break;
		case MP_EV_PLAYER_LEFT:
			g.waitingStage &= ~(1u << ev.slot);
			notice(std::string(kCharacters[ev.slot]) + " left the game");
			if (g.role == MPG_ROLE_HOST)
				g.hostFlags.forget(ev.slot);
			break;
		case MP_EV_MESSAGE:
			if (g.role == MPG_ROLE_HOST)
				hostMessage(ev);
			else
				clientMessage(ev);
			break;
		default:
			break;
		}
	}
}

void packPose(const MpgPose& p, std::vector<uint8_t>& out)
{
	out.assign(kPoseSize, 0);
	Writer w(out.data(), out.size());
	w.u32(p.time);
	w.u32(p.session);
	w.u8(p.flags);
	w.u8(p.hand);
	w.u8(p.nozzle);
	w.u8(p.btp);
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 4; c++)
			w.f32(p.mtx[r][c]);
	w.u16(p.bodyBck);
	w.u16(p.upperBck);
	w.f32(p.bodyFrame);
	w.f32(p.bodyBlend);
	w.f32(p.upperFrame);
	w.f32(p.btpFrame);
	w.u16(p.fluddBck);
	w.u16(p.yoshiBck);
	w.f32(p.fluddFrame);
	w.f32(p.yoshiFrame);
	w.u8(p.yoshiColor[0]);
	w.u8(p.yoshiColor[1]);
	w.u8(p.yoshiColor[2]);
	w.u8(0);
	w.u32(p.hiddenShapes);
	w.u8(p.emitters);
	w.u8(0);
	w.u8(0);
	w.u8(0);
	for (int e = 0; e < 2; e++)
		for (int c = 0; c < 3; c++)
			w.f32(p.emitPos[e][c]);
	for (int e = 0; e < 2; e++)
		for (int c = 0; c < 3; c++)
			w.f32(p.emitDir[e][c]);
	for (int c = 0; c < 3; c++)
		w.f32(p.emitSpeed[c]);
	w.f32(p.pressure);
	out.resize(w.len);
}

bool unpackPose(const uint8_t* d, size_t n, MpgPose& p)
{
	Reader r(d, n);
	memset(&p, 0, sizeof p);
	p.time    = r.u32();
	p.session = r.u32();
	p.flags   = r.u8();
	p.hand    = r.u8();
	p.nozzle  = r.u8();
	p.btp     = r.u8();
	for (int i = 0; i < 3; i++)
		for (int c = 0; c < 4; c++)
			p.mtx[i][c] = r.f32();
	p.bodyBck    = r.u16();
	p.upperBck   = r.u16();
	p.bodyFrame  = r.f32();
	p.bodyBlend  = r.f32();
	p.upperFrame = r.f32();
	p.btpFrame   = r.f32();
	p.fluddBck   = r.u16();
	p.yoshiBck   = r.u16();
	p.fluddFrame = r.f32();
	p.yoshiFrame = r.f32();
	p.yoshiColor[0] = r.u8();
	p.yoshiColor[1] = r.u8();
	p.yoshiColor[2] = r.u8();
	r.u8();
	p.hiddenShapes = r.u32();
	p.emitters     = r.u8();
	r.u8();
	r.u8();
	r.u8();
	for (int e = 0; e < 2; e++)
		for (int c = 0; c < 3; c++)
			p.emitPos[e][c] = r.f32();
	for (int e = 0; e < 2; e++)
		for (int c = 0; c < 3; c++)
			p.emitDir[e][c] = r.f32();
	for (int c = 0; c < 3; c++)
		p.emitSpeed[c] = r.f32();
	p.pressure = r.f32();
	if (p.emitters > 2)
		p.emitters = 2;
	return r.ok;
}

// How far behind their newest pose others are drawn: about three of their
// updates, so a late or lost one does not show.
const int32_t kDrawBehindMs = 100;
// With nothing new from a player, the moment drawn goes at most this far past
// their newest pose (then holds) rather than running on.
const double kDrawAheadMs = 80.0;

// Every pose that arrived since the last frame, into each player's buffer;
// then each player's drawing moment moves on a frame.
void takePoses()
{
	const uint32_t now = mp_clock_ms();
	for (int slot = 0; slot < MP_MAX_PLAYERS; slot++) {
		uint8_t buf[MP_MAX_STATE];
		uint32_t at = 0;
		size_t n;
		std::deque<Controller::Timed>& q = g.poses[slot];
		while ((n = mp_remote_next(slot, buf, sizeof buf, &at)) > 0) {
			Controller::Timed t;
			if (!unpackPose(buf, n, t.pose))
				continue;
			t.received = at;
			static const bool trace = getenv("SMS_MULTIPLAYER_TRACE") != nullptr;
			if (trace)
				fprintf(stderr, "[mprecv] %d time %u at %u pos %.1f %.1f\n", slot, t.pose.time, at, t.pose.mtx[0][3],
				        t.pose.mtx[2][3]);
			if (!q.empty() && (int32_t)(t.pose.time - q.back().pose.time) <= 0)
				continue; // out of order, or the same moment again
			if (!q.empty()) {
				const double gap = (double)(int32_t)(t.pose.time - q.back().pose.time);
				g.poseGap[slot]  = g.poseGap[slot] > 0.0 ? g.poseGap[slot] * 0.9 + gap * 0.1 : gap;
			}
			q.push_back(t);
			const int32_t lag = (int32_t)(at - t.pose.time);
			if (!g.lagKnown[slot]) {
				g.lagMin[slot] = g.lagNextMin[slot] = lag;
				g.lagSince[slot] = now;
				g.lagKnown[slot] = true;
			}
			if (lag < g.lagMin[slot])
				g.lagMin[slot] = lag;
			if (lag < g.lagNextMin[slot])
				g.lagNextMin[slot] = lag;
		}
		// the measure follows a clock that drifts, or a route that changes
		if (g.lagKnown[slot] && now - g.lagSince[slot] > 4000) {
			g.lagMin[slot]     = g.lagNextMin[slot];
			g.lagNextMin[slot] = 0x7FFFFFFF;
			g.lagSince[slot]   = now;
		}
		while (q.size() > 2 && now - q.front().received > 1500)
			q.pop_front();
		if (g.lagKnown[slot] && !q.empty()) {
			// behind their newest pose by about two and a half of their
			// updates (at least kDrawBehindMs), in their game's time, so the
			// pose after the moment drawn has nearly always arrived
			const double behind  = std::max<double>(kDrawBehindMs, g.poseGap[slot] * 2.5);
			const double target  = (double)q.back().pose.time - behind;
			const double error   = target - g.drawAt[slot];
			const uint32_t newest = q.back().pose.time;
			g.framesSinceNew[slot]++;
			if (!g.drawing[slot] || error > 300.0 || error < -2000.0) {
				// first sight, or fallen well behind (a stall, a loading
				// screen): start there, not fast-forward to it
				g.drawAt[slot]         = target;
				g.drawing[slot]        = true;
				g.theirsPerFrame[slot] = g.frameMs;
				g.framesSinceNew[slot] = 0;
			} else {
				// how fast their game runs against ours: measured only as their
				// poses come (a stall must not slow it), within a fifth of ours
				const double advanced = (double)(int32_t)(newest - g.newestSeen[slot]);
				if (advanced > 0.0) {
					const double perFrame  = advanced / std::max(1, g.framesSinceNew[slot]);
					const double rate      = g.theirsPerFrame[slot] * 0.9 + perFrame * 0.1;
					g.theirsPerFrame[slot] = std::min(g.frameMs * 1.2, std::max(g.frameMs * 0.8, rate));
					g.framesSinceNew[slot] = 0;
				}
				// a gentle lean toward where it should be, never a jump
				const double lean = 0.01 + std::min(0.08, std::fabs(error) / 8000.0);
				g.drawAt[slot] += g.theirsPerFrame[slot] + error * lean;
				// nothing new for a while: not past their newest by more than a little
				g.drawAt[slot] = std::min(g.drawAt[slot], (double)newest + kDrawAheadMs);
			}
			g.newestSeen[slot] = newest;
		}
		if (!(mp_players() & (1u << slot))) {
			q.clear();
			g.lagKnown[slot] = false;
			g.drawing[slot]  = false;
		}
	}
}

} // namespace
} // namespace mp

using namespace mp;

static std::string g_build = SMS_GAME_BUILD;

// The room code friends join with (SMS_MULTIPLAYER_ROOM, from the launcher),
// or "" when there is none: shown as the host plays.
static const std::string& roomCode()
{
	static const std::string code = [] {
		std::string text;
		if (const char* room = getenv("SMS_MULTIPLAYER_ROOM"))
			for (const char* c = room; *c && text.size() < 12; c++)
				if (isalnum((unsigned char)*c) || *c == '-')
					text += (char)toupper((unsigned char)*c);
		return text;
	}();
	return code;
}

extern "C" void mpg_set_build(const char* build) { g_build = build ? build : SMS_GAME_BUILD; }

extern "C" int mpg_start(void)
{
	std::lock_guard<std::mutex> hold(g_lock);
	MpConfig cfg;
	int want = mp_config_from_env(&cfg, g_build.c_str());
	if (want == 0)
		return g.role = MPG_ROLE_OFF;
	if (want < 0) {
		notice(std::string("Multiplayer is off: ") + cfg.address);
		return g.role = MPG_ROLE_OFF;
	}
	g.role = cfg.role == MP_ROLE_HOST ? MPG_ROLE_HOST : MPG_ROLE_CLIENT;
	g.cfg  = cfg;
	if (mp_start(&cfg) != 0) {
		char msg[256];
		mp_link_message(msg, sizeof msg);
		notice(std::string("Multiplayer: ") + msg);
		return g.role;
	}
	if (g.role == MPG_ROLE_HOST) {
		if (!roomCode().empty())
			notice("Hosting room " + roomCode() + ". Friends join with this code, and come to the level you are in.");
		else
			notice("Hosting on port " + std::to_string(cfg.port) + ". Players who join come to the level you are in.");
		mp_set_host_status("the title screen");
		mp_set_accepting(0); // until the host has chosen a file and is in a stage
	} else {
		char msg[256];
		mp_link_message(msg, sizeof msg);
		notice(std::string("Multiplayer: ") + msg);
	}
	return g.role;
}

extern "C" int mpg_role(void) { return g.role; }
extern "C" int mpg_player_present(int slot)
{
	return g.role != MPG_ROLE_OFF && slot >= 0 && slot < MP_MAX_PLAYERS && (mp_players() & (1u << slot));
}
extern "C" void mpg_restart_session(void) { g.restartSession = true; }
extern "C" int mpg_slot(void) { return g.role == MPG_ROLE_OFF ? -1 : mp_local_slot(); }
extern "C" int mpg_character(int slot) { return slot >= 0 && slot < MP_MAX_PLAYERS ? slot : 0; }
extern "C" const char* mpg_character_name(int slot)
{
	return slot >= 0 && slot < MP_MAX_PLAYERS ? kCharacters[slot] : "Mario";
}

extern "C" void mpg_frame(const MpgGame* game)
{
	if (g.role == MPG_ROLE_OFF)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	g.game = *game;
	g.frameMs = game->frameMs > 0.0f && game->frameMs < 200.0f ? game->frameMs : 1000.0 / 30.0;
	g.gameMs += g.frameMs;
	events();
	// reconnecting: the next try (the host then sends its flags and where it is)
	if (g.role == MPG_ROLE_CLIENT && g.reconnectAt && (int32_t)(mp_clock_ms() - g.reconnectAt) >= 0) {
		g.reconnectAt = 0;
		g.reconnectTries++;
		g.localSession = 0;
		g.targetSession = 0;
		fprintf(stderr, "[multiplayer] reconnecting (try %d)\n", g.reconnectTries);
		mp_start(&g.cfg);
	}
	takePoses();
	flushCleans();
	flushHits();
	flushMirrors();
	const bool arrived = game->playing && !g.wasPlaying;
	g.wasPlaying       = game->playing != 0;

	if (g.role == MPG_ROLE_HOST) {
		if (game->playing
		    && (game->area != g.sessionArea || game->episode != g.sessionEpisode || !g.session
		        || (arrived && g.restartSession))) {
			g.restartSession = false;
			// a new stage: everyone comes along
			g.session++;
			g.sessionArea    = game->area;
			g.sessionEpisode = game->episode;
			g.taken.clear();
			g.removals.clear();
			g.takenAt.clear();
			g.shineGets.clear();
			g.demos.clear();
			g.boss.clear();
			g.bossHits.clear();
			g.sessionRed = 0;
			g.restoreRed = false;
			g.shineExit  = false;
			g.enemies.clear();
			g.enemyNext  = 0;
			g.enemyBatch = 0;
			g.enemyKills.clear();
			forgetCleans();
			for (int i = 1; i < MP_MAX_PLAYERS; i++)
				if (mp_players() & (1u << i)) {
					sendFull(i);
					sendStage(i);
				}
			g.waitingStage = 0;
			char status[96];
			const int world = game->world >= 0 && game->world < 10 ? game->world : 1;
			snprintf(status, sizeof status, "%s", kWorlds[world]);
			mp_set_host_status(status);
			fprintf(stderr, "[multiplayer] session %u: area %d, episode %d\n", g.session, game->area, game->episode);
		} else if (arrived) {
			// the same stage again (we lost a life): put it back as it was
			for (uint32_t key : g.taken)
				g.removals.push_back(key);
			g.restoreRed = true;
			// the stage's goop is back as it loaded: clean again what was cleaned
			g.cleanIn.assign(g.cleanLog.begin(), g.cleanLog.end());
		}
		// players may join once the host has chosen a file and is in a stage
		mp_set_accepting(g.session != 0);
		// newcomers who came while the host was between stages: here it is
		if (g.waitingStage && g.session && (game->playing || game->paused)) {
			for (int i = 1; i < MP_MAX_PLAYERS; i++)
				if ((g.waitingStage & (1u << i)) && (mp_players() & (1u << i))) {
					sendFull(i);
					sendStage(i);
				}
			g.waitingStage = 0;
		}
		return;
	}

	// client
	if (arrived) {
		if (g.hasTarget && !g.targetSession) {
			g.localSession = 0; // the host is on its way too: K_STAGE says which session
		} else if (g.hasTarget && game->area == g.targetArea && game->episode == g.targetEpisode) {
			if (g.localSession != g.targetSession)
				g.takenAt.clear();
			// new or loaded again, its goop is as it loaded: K_READY brings the session's cleaning
			forgetCleans();
			g.travelKnown = false;
			g.localSession = g.targetSession;
			uint8_t buf[4];
			Writer w(buf, sizeof buf);
			w.u32(g.localSession);
			mp_send(0, K_READY, buf, w.len);
		} else {
			g.localSession = 0;
		}
		g.removals.clear();
	}
}

extern "C" int mpg_flag_count(void) { return (int)sharedFlags().size(); }

extern "C" uint32_t mpg_flag_id(int index)
{
	const std::vector<FlagSpec>& specs = sharedFlags();
	return index >= 0 && (size_t)index < specs.size() ? specs[index].id : 0;
}

extern "C" void mpg_flags_sync(int32_t* values)
{
	if (g.role == MPG_ROLE_OFF)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	const size_t n = sharedFlags().size();
	FlagValues game(values, values + n);
	if (g.role == MPG_ROLE_HOST) {
		static const int red = sharedFlagIndex(FLAG_RED_COINS);
		if (g.restoreRed) {
			game[red]    = g.sessionRed;
			g.restoreRed = false;
		}
		while (!g.reports.empty()) {
			const int slot = g.reports.front().first;
			FlagValues before = game;
			if (g_debug)
				for (const FlagReport::Change& c : g.reports.front().second.changes)
					fprintf(stderr, "[multiplayer] slot %d: flag %05x %+d\n", slot, sharedFlags()[c.index].id, c.value);
			g.hostFlags.report(slot, g.reports.front().second, game);
			g.reports.pop_front();
			for (uint32_t i = 0; i < FLAG_SHINES_N; i++)
				if (!before[i] && game[i]) {
					g.shineExit = true;
					notice(std::string(kCharacters[slot]) + " got a Shine!");
				}
		}
		g.sessionRed = game[red];
		for (int i = 1; i < MP_MAX_PLAYERS; i++) {
			if (!(mp_players() & (1u << i)))
				continue;
			FlagUpdate u;
			if (g.hostFlags.updateFor(i, game, u)) {
				std::vector<std::vector<uint8_t>> msgs;
				writeUpdate(u, MP_MAX_MESSAGE, msgs);
				for (auto& m : msgs)
					send(i, K_FLAGS, m);
			}
		}
		g.hostFlags.endFrame(game);
		g.hostValues = game;
	} else {
		if (mp_link() != MP_LINK_CONNECTED)
			return;
		FlagReport rep = g.clientFlags.frame(game);
		if (g_debug)
			for (const FlagReport::Change& c : rep.changes)
				fprintf(stderr, "[multiplayer] report flag %05x %+d\n", sharedFlags()[c.index].id, c.value);
		if (!rep.changes.empty()) {
			std::vector<std::vector<uint8_t>> msgs;
			writeReport(rep, MP_MAX_MESSAGE, msgs);
			for (auto& m : msgs)
				send(0, K_REPORT, m);
		}
	}
	memcpy(values, game.data(), n * sizeof(int32_t));
}

extern "C" int mpg_flags_ready(void)
{
	return g.role == MPG_ROLE_HOST || (g.role == MPG_ROLE_CLIENT && g.clientFlags.hasHost());
}

extern "C" int mpg_target(int* area, int* episode)
{
	if (g.role != MPG_ROLE_CLIENT || !g.hasTarget || mp_link() != MP_LINK_CONNECTED)
		return 0;
	*area    = g.targetArea;
	*episode = g.targetEpisode;
	return 1;
}

extern "C" int mpg_in_target(void)
{
	if (g.role != MPG_ROLE_CLIENT || !g.hasTarget || mp_link() != MP_LINK_CONNECTED)
		return 1;
	if (!g.targetSession) // the host is on the way: are we going there (or there) already?
		return g.game.playing && g.game.area == g.targetArea
		       && (g.targetEpisode == 0xFF || g.game.episode == g.targetEpisode);
	return g.localSession && g.localSession == g.targetSession;
}

extern "C" void mpg_host_leaving(int area, int episode)
{
	if (g.role != MPG_ROLE_HOST)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	uint8_t buf[2] = {(uint8_t)area, (uint8_t)episode};
	for (int i = 1; i < MP_MAX_PLAYERS; i++)
		if (mp_players() & (1u << i))
			mp_send(i, K_LEAVING, buf, sizeof buf);
}

extern "C" void mpg_host_going(int area, int episode, uint32_t sessionFlags, int movie)
{
	if (g.role != MPG_ROLE_HOST)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	uint8_t buf[9];
	Writer w(buf, sizeof buf);
	w.u8((uint8_t)area);
	w.u8((uint8_t)episode);
	w.u8(1);
	w.u32(sessionFlags);
	w.u8(movie < 0 || movie > 0xFE ? 0xFF : (uint8_t)movie);
	for (int i = 1; i < MP_MAX_PLAYERS; i++)
		if (mp_players() & (1u << i))
			mp_send(i, K_LEAVING, buf, w.len);
}

extern "C" int mpg_target_traveling(void)
{
	std::lock_guard<std::mutex> hold(g_lock);
	return g.hasTarget && !g.targetSession;
}

extern "C" int mpg_travel(uint32_t* sessionFlags, int* movie)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (!g.travelKnown)
		return 0;
	*sessionFlags = g.travelFlags;
	*movie        = g.travelMovie;
	return 1;
}

extern "C" void mpg_travel_done(void)
{
	std::lock_guard<std::mutex> hold(g_lock);
	g.travelKnown = false;
}

extern "C" int mpg_take_shine_exit(void)
{
	if (!g.shineExit)
		return 0;
	g.shineExit = false;
	return 1;
}

extern "C" void mpg_item_taken(uint32_t key)
{
	if (g.role == MPG_ROLE_OFF)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	uint8_t buf[8];
	Writer w(buf, sizeof buf);
	if (g.role == MPG_ROLE_HOST) {
		if (!g.session)
			return;
		takeOnce(key);
		w.u32(g.session);
		w.u32(key);
		mp_send(-1, K_TAKEN, buf, w.len);
	} else if (g.localSession) {
		w.u32(g.localSession);
		w.u32(key);
		mp_send(0, K_TAKEN, buf, w.len);
	}
}

extern "C" void mpg_item_taken_at(int type, const float pos[3])
{
	if (g.role == MPG_ROLE_OFF)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	MpgTakenAt t;
	t.type = (uint8_t)type;
	memcpy(t.pos, pos, sizeof t.pos);
	if (g.role == MPG_ROLE_HOST) {
		if (!g.session)
			return;
		addTakenAt(t);
		for (int i = 1; i < MP_MAX_PLAYERS; i++)
			if (mp_players() & (1u << i))
				send(i, K_TAKEN_AT, takenAtMessage(g.session, t));
	} else if (g.localSession) {
		addTakenAt(t);
		send(0, K_TAKEN_AT, takenAtMessage(g.localSession, t));
	}
}

extern "C" void mpg_clean(float x, float y, float z, float size)
{
	if (g.role == MPG_ROLE_OFF)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	if (!(g.role == MPG_ROLE_HOST ? g.session : g.localSession) || !(size > 0.0f))
		return;
	Controller::Clean c;
	c.x    = halved(x);
	c.y    = halved(y);
	c.z    = halved(z);
	c.size = (uint16_t)(size >= 65535.0f ? 65535.0f : size + 0.999f);
	if (!freshClean(c))
		return;
	static int sent;
	if (g_debug && ++sent % 100 == 0)
		fprintf(stderr, "[multiplayer] goop: %d spots cleaned here, for the others\n", sent);
	logClean(c);
	g.cleanOut.push_back(c);
	if (g.cleanOut.size() >= kCleanBatch)
		flushCleans();
}

extern "C" int mpg_next_clean(float* x, float* y, float* z, float* size)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (g.cleanIn.empty())
		return 0;
	const Controller::Clean& c = g.cleanIn.front();
	*x    = c.x * 2.0f;
	*y    = c.y * 2.0f;
	*z    = c.z * 2.0f;
	*size = c.size;
	g.cleanIn.pop_front();
	return 1;
}

extern "C" void mpg_hit(int manager, int index, int message, int water, const float pos[3])
{
	if (g.role == MPG_ROLE_OFF || manager < 0 || manager > 255 || index < 0 || index > 0xFFFF || message < 0
	    || message > 255)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	if (!(g.role == MPG_ROLE_HOST ? g.session : g.localSession))
		return;
	// the same hit again this frame (a stream of water): counted, not repeated
	for (Controller::Hit& h : g.hitOut)
		if (h.manager == manager && h.index == index && h.message == message && h.water == (water != 0)) {
			if (h.count < 255)
				h.count++;
			return;
		}
	if (g.hitOut.size() >= 4 * kHitBatch)
		return;
	Controller::Hit h;
	h.slot    = (uint8_t)mp_local_slot();
	h.manager = (uint8_t)manager;
	h.index   = (uint16_t)index;
	h.message = (uint8_t)message;
	h.water   = water != 0;
	h.count   = 1;
	h.x       = halved(pos[0]);
	h.y       = halved(pos[1]);
	h.z       = halved(pos[2]);
	g.hitOut.push_back(h);
}

extern "C" int mpg_next_hit(int* slot, int* manager, int* index, int* message, int* water, int* count, float pos[3])
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (g.hitIn.empty())
		return 0;
	const Controller::Hit& h = g.hitIn.front();
	*slot    = h.slot;
	*manager = h.manager;
	*index   = h.index;
	*message = h.message;
	*water   = h.water;
	*count   = h.count;
	pos[0]   = h.x * 2.0f;
	pos[1]   = h.y * 2.0f;
	pos[2]   = h.z * 2.0f;
	g.hitIn.pop_front();
	return 1;
}

extern "C" void mpg_carry(int manager, int index, int action, const float pos[3], const float vel[3], int yaw)
{
	if (g.role == MPG_ROLE_OFF)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	const uint32_t session = g.role == MPG_ROLE_HOST ? g.session : g.localSession;
	if (!session)
		return;
	Controller::Carry c;
	c.slot    = mp_local_slot();
	c.manager = manager;
	c.index   = index;
	c.action  = action;
	memcpy(c.pos, pos, sizeof c.pos);
	memcpy(c.vel, vel, sizeof c.vel);
	c.yaw = (int16_t)yaw;
	const std::vector<uint8_t> buf = carryMessage(session, c);
	if (g.role == MPG_ROLE_HOST) {
		for (int i = 1; i < MP_MAX_PLAYERS; i++)
			if (mp_players() & (1u << i))
				send(i, K_CARRY, buf);
	} else {
		send(0, K_CARRY, buf);
	}
}

extern "C" int mpg_next_carry(int* slot, int* manager, int* index, int* action, float pos[3], float vel[3], int* yaw)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (g.carries.empty())
		return 0;
	const Controller::Carry& c = g.carries.front();
	*slot    = c.slot;
	*manager = c.manager;
	*index   = c.index;
	*action  = c.action;
	memcpy(pos, c.pos, sizeof c.pos);
	memcpy(vel, c.vel, sizeof c.vel);
	*yaw = c.yaw;
	g.carries.pop_front();
	return 1;
}

extern "C" void mpg_mirror(int manager, int index, const void* data, int size)
{
	if (g.role == MPG_ROLE_OFF || manager < 0 || manager > 255 || index < 0 || index > 0xFFFF || size < 0 || size > 48)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	if (!(g.role == MPG_ROLE_HOST ? g.session : g.localSession) || g.mirrorOut.size() >= 4 * kMirrorBatch)
		return;
	Controller::Mirror m;
	m.slot    = (uint8_t)mp_local_slot();
	m.manager = (uint8_t)manager;
	m.index   = (uint16_t)index;
	m.len     = (uint8_t)size;
	memcpy(m.data, data, size);
	g.mirrorOut.push_back(m);
}

extern "C" int mpg_next_mirror(int* slot, int* manager, int* index, void* data, int* size)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (g.mirrorIn.empty())
		return 0;
	const Controller::Mirror& m = g.mirrorIn.front();
	*slot    = m.slot;
	*manager = m.manager;
	*index   = m.index;
	*size    = m.len;
	memcpy(data, m.data, m.len);
	g.mirrorIn.pop_front();
	return 1;
}

extern "C" void mpg_shine_get(int eventId, const float pos[3])
{
	if (g.role == MPG_ROLE_OFF)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	const int slot = mp_local_slot();
	if (g.role == MPG_ROLE_HOST) {
		if (!g.session)
			return;
		for (int i = 1; i < MP_MAX_PLAYERS; i++)
			if (mp_players() & (1u << i))
				send(i, K_SHINE_GET, shineGetMessage(g.session, slot, eventId, pos));
	} else if (g.localSession) {
		send(0, K_SHINE_GET, shineGetMessage(g.localSession, slot, eventId, pos));
	}
}

extern "C" int mpg_next_shine_get(int* slot, int* eventId, float pos[3])
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (g.shineGets.empty())
		return 0;
	const Controller::ShineGet& sg = g.shineGets.front();
	*slot    = sg.slot;
	*eventId = sg.eventId;
	memcpy(pos, sg.pos, sizeof sg.pos);
	g.shineGets.pop_front();
	return 1;
}

extern "C" void mpg_demo(const void* data, size_t size)
{
	if (g.role == MPG_ROLE_OFF || size > 256)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	const uint32_t session = g.role == MPG_ROLE_HOST ? g.session : g.localSession;
	if (!session)
		return;
	std::vector<uint8_t> buf(5 + size);
	Writer w(buf.data(), buf.size());
	w.u32(session);
	w.u8((uint8_t)mp_local_slot());
	w.bytes(data, size);
	if (g.role == MPG_ROLE_HOST) {
		for (int i = 1; i < MP_MAX_PLAYERS; i++)
			if (mp_players() & (1u << i))
				send(i, K_DEMO, buf);
	} else {
		send(0, K_DEMO, buf);
	}
}

extern "C" size_t mpg_next_demo(int* slot, void* data, size_t size)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (g.demos.empty())
		return 0;
	const Controller::Demo& d = g.demos.front();
	const size_t n = std::min(size, d.data.size());
	*slot = d.slot;
	memcpy(data, d.data.data(), n);
	g.demos.pop_front();
	return n ? n : 1;
}

extern "C" int mpg_taken_at(int i, MpgTakenAt* out)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (i < 0 || (size_t)i >= g.takenAt.size())
		return 0;
	*out = g.takenAt[i];
	return 1;
}

extern "C" int mpg_next_removal(uint32_t* key)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (g.removals.empty())
		return 0;
	*key = g.removals.front();
	g.removals.pop_front();
	return 1;
}

extern "C" uint32_t mpg_session(void)
{
	return g.role == MPG_ROLE_HOST ? g.session : g.localSession;
}

extern "C" void mpg_set_pose(const MpgPose* pose)
{
	if (g.role == MPG_ROLE_OFF)
		return;
	MpgPose stamped = *pose;
	stamped.time    = (uint32_t)(uint64_t)g.gameMs;
	std::vector<uint8_t> buf;
	packPose(stamped, buf);
	mp_set_local_state(buf.data(), buf.size());
}

extern "C" int mpg_pose(int slot, MpgPose* pose, float* age)
{
	if (g.role == MPG_ROLE_OFF)
		return 0;
	uint8_t buf[MP_MAX_STATE];
	size_t n = mp_remote_state(slot, buf, sizeof buf, age);
	if (!n || !unpackPose(buf, n, *pose))
		return 0;
	const uint32_t here = mpg_session();
	return here && pose->session == here;
}

extern "C" int mpg_take_notice(char* out, int size)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (g.notices.empty() || size <= 0)
		return 0;
	snprintf(out, (size_t)size, "%s", g.notices.front().c_str());
	g.notices.pop_front();
	return 1;
}

extern "C" void mpg_status(char* out, int size)
{
	if (size <= 0)
		return;
	std::string s;
	if (g.role == MPG_ROLE_OFF) {
		out[0] = 0;
		return;
	}
	const unsigned players = mp_players();
	const int me           = mp_local_slot();
	char line[128];
	snprintf(line, sizeof line, "MULTIPLAYER  %d/%d", __builtin_popcount(players), MP_MAX_PLAYERS);
	s += line;
	if (g.role == MPG_ROLE_HOST && !roomCode().empty())
		s += "  ROOM " + roomCode();
	for (int i = 0; i < MP_MAX_PLAYERS; i++) {
		if (!(players & (1u << i)))
			continue;
		char name[MP_NAME_LEN];
		mp_player_name(i, name, sizeof name);
		const int ping = i == me ? -1 : mp_ping(g.role == MPG_ROLE_HOST ? i : 0);
		if (i == me)
			snprintf(line, sizeof line, "\nP%d %-11s %s (you)", i + 1, kCharacters[i], name);
		else if (ping >= 0)
			snprintf(line, sizeof line, "\nP%d %-11s %s  %d ms", i + 1, kCharacters[i], name, ping);
		else
			snprintf(line, sizeof line, "\nP%d %-11s %s", i + 1, kCharacters[i], name);
		s += line;
	}
	if (g.role == MPG_ROLE_CLIENT && mp_link() != MP_LINK_CONNECTED) {
		char msg[256];
		mp_link_message(msg, sizeof msg);
		s += "\n";
		s += msg;
	}
	snprintf(out, (size_t)size, "%s", s.c_str());
}

extern "C" void mpg_set_enemies(const MpgEnemy* list, int count)
{
	if (g.role != MPG_ROLE_HOST || !g.session)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	if (!list) {
		// nothing to follow (the host's game is paused): the others' snapshot
		// goes stale and their games let their enemies move by themselves
		g.enemies.clear();
		mp_set_world(nullptr, 0);
		return;
	}
	g.enemies.assign(list, list + (count > 0 ? count : 0));
	// this update's share: as many as fit, from where the last one sent
	// stopped (a share not sent yet, the game running faster than the
	// updates, is made again with the newest places, not skipped)
	if (mp_world_sent())
		g.enemyNext += g.enemyBatch;
	uint8_t buf[MP_MAX_WORLD];
	Writer w(buf, sizeof buf);
	w.u32(g.session);
	const size_t per = (sizeof buf - 6 - 1 - g.boss.size()) / kEnemySize;
	const size_t n   = std::min(per, g.enemies.size());
	w.u16((uint16_t)n);
	g.enemyNext = g.enemies.empty() ? 0 : g.enemyNext % g.enemies.size();
	for (size_t k = 0; k < n; k++) {
		const MpgEnemy& e = g.enemies[(g.enemyNext + k) % g.enemies.size()];
		w.u8(e.manager);
		w.u8(e.alive);
		w.u16(e.index);
		w.f32(e.pos[0]);
		w.f32(e.pos[1]);
		w.f32(e.pos[2]);
		w.s16(e.rotY);
	}
	g.enemyBatch = n;
	w.u8((uint8_t)g.boss.size());
	w.bytes(g.boss.data(), g.boss.size());
	mp_set_world(buf, w.len);
}

extern "C" int mpg_next_enemy_kill(int* manager, int* index)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (g.enemyKills.empty())
		return 0;
	*manager = g.enemyKills.front().first;
	*index   = g.enemyKills.front().second;
	g.enemyKills.pop_front();
	return 1;
}

extern "C" int mpg_enemies(MpgEnemy* out, int max, float* age)
{
	if (g.role != MPG_ROLE_CLIENT || !g.localSession)
		return 0;
	uint8_t buf[MP_MAX_WORLD];
	size_t size = mp_world(buf, sizeof buf, age);
	Reader r(buf, size);
	if (!size || r.u32() != g.localSession)
		return 0;
	int n = r.u16(), got = 0;
	for (int k = 0; k < n && got < max && r.ok; k++) {
		MpgEnemy& e = out[got];
		e.manager   = r.u8();
		e.alive     = r.u8();
		e.index     = r.u16();
		e.pos[0]    = r.f32();
		e.pos[1]    = r.f32();
		e.pos[2]    = r.f32();
		e.rotY      = r.s16();
		e.pad       = 0;
		if (r.ok)
			got++;
	}
	return got;
}

extern "C" void mpg_set_boss(const void* data, size_t size)
{
	if (g.role != MPG_ROLE_HOST || size > kMaxBoss)
		return;
	std::lock_guard<std::mutex> hold(g_lock);
	g.boss.assign((const uint8_t*)data, (const uint8_t*)data + size);
}

extern "C" size_t mpg_boss(void* out, size_t size, float* age)
{
	if (g.role != MPG_ROLE_CLIENT || !g.localSession)
		return 0;
	uint8_t buf[MP_MAX_WORLD];
	const size_t got = mp_world(buf, sizeof buf, age);
	Reader r(buf, got);
	if (!got || r.u32() != g.localSession)
		return 0;
	const int n = r.u16();
	r.skip((size_t)n * kEnemySize);
	const size_t len = r.u8();
	const uint8_t* d = r.skip(len);
	if (!r.ok || !len || len > size)
		return 0;
	memcpy(out, d, len);
	return len;
}

extern "C" void mpg_boss_hit(int unit, int kind, int count)
{
	if (g.role != MPG_ROLE_CLIENT || !g.localSession || count <= 0)
		return;
	uint8_t buf[8];
	Writer w(buf, sizeof buf);
	w.u32(g.localSession);
	w.u8((uint8_t)unit);
	w.u8((uint8_t)kind);
	w.u8((uint8_t)(count > 255 ? 255 : count));
	mp_send(0, K_BOSS_HIT, buf, w.len);
}

extern "C" int mpg_next_boss_hit(int* unit, int* kind, int* count)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (g.bossHits.empty())
		return 0;
	*unit  = g.bossHits.front().unit;
	*kind  = g.bossHits.front().kind;
	*count = g.bossHits.front().count;
	g.bossHits.pop_front();
	return 1;
}

extern "C" void mpg_enemy_killed(int manager, int index)
{
	if (g.role != MPG_ROLE_CLIENT || !g.localSession)
		return;
	uint8_t buf[8];
	Writer w(buf, sizeof buf);
	w.u32(g.localSession);
	w.u8((uint8_t)manager);
	w.u16((uint16_t)index);
	mp_send(0, K_ENEMY_KILL, buf, w.len);
}

extern "C" int mpg_pose_pair(int slot, MpgPose* a, MpgPose* b, float* t)
{
	if (g.role == MPG_ROLE_OFF || slot < 0 || slot >= MP_MAX_PLAYERS || !g.lagKnown[slot])
		return 0;
	std::lock_guard<std::mutex> hold(g_lock);
	const std::deque<Controller::Timed>& q = g.poses[slot];
	if (q.empty())
		return 0;
	const uint32_t now = mp_clock_ms();
	if (now - q.back().received > 3000)
		return 0; // gone quiet
	// the moment to draw, on their clock (wrapping as the clock does)
	const uint32_t at = (uint32_t)(int64_t)g.drawAt[slot];
	size_t i = 0;
	while (i + 1 < q.size() && (int32_t)(q[i + 1].pose.time - at) <= 0)
		i++;
	*a = q[i].pose;
	if (i + 1 < q.size() && (int32_t)(at - q[i].pose.time) >= 0) {
		*b = q[i + 1].pose;
		const float span = (float)(int32_t)(b->time - a->time);
		*t = span > 0.0f ? (float)(int32_t)(at - a->time) / span : 0.0f;
	} else {
		*b = *a; // before the oldest, or past the newest: hold it
		*t = 0.0f;
	}
	if (*t < 0.0f)
		*t = 0.0f;
	if (*t > 1.0f)
		*t = 1.0f;
	const uint32_t here = mpg_session();
	return here && b->session == here && a->session == here;
}
