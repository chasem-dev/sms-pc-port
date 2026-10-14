// The multiplayer session's rules (mp_game_api.h): stage sessions, warps,
// the shared flags, items and players' looks, between the game half and the
// network (mp_net.h).
#include "../mp_game_api.h"
#include "../net/mp_net.h"
#include "../net/mp_wire.h"
#include "mp_flags.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

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
	K_LEAVING,      // host -> client: area, episode (0xFF: the game decides) the host is setting off for
	K_DEMO,         // either way: session, slot, the game's description of a cutscene (mp_game.cpp)
	K_BOSS_HIT,     // client -> host: session, kind, count (a hit on its copy of the boss, for the real one)
};
// The host's boss rides at the end of the world snapshot, after the enemies.
const size_t kMaxBoss = 48;
// Items that appear during a stage (from a box, a defeated enemy, a red coin
// switch) are named by their type and where they are, not their place in the
// item manager (every game fills that differently); at most this many a stage.
const size_t kMaxTakenAt = 512;

const char* const kCharacters[MP_MAX_PLAYERS] = {"Mario", "Luigi", "Peach", "Metal Mario"};
const char* const kWorlds[] = {"the Airstrip", "Delfino Plaza", "Bianco Hills", "Ricco Harbor", "Gelato Beach",
                               "Pinna Park", "Sirena Beach", "Noki Bay", "Pianta Village", "Corona Mountain"};
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
	std::deque<std::pair<int, int>> bossHits;
	// enemies: the host's list and where the next share starts; a client's
	// newest share; kills reported to the host
	std::vector<MpgEnemy> enemies;
	size_t enemyNext = 0;
	std::deque<std::pair<int, int>> enemyKills;

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
		}
		break;
	}
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
		int kind = r.u8(), count = r.u8();
		if (r.ok && s == g.session && g.session && g.bossHits.size() < 64)
			g.bossHits.emplace_back(kind, count);
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
			uint8_t buf[4];
			Writer w(buf, sizeof buf);
			w.u32(s);
			mp_send(0, K_READY, buf, w.len);
		}
		break;
	}
	case K_LEAVING: {
		int area = r.u8(), episode = r.u8();
		if (!r.ok || (g.hasTarget && !g.targetSession && g.targetArea == area && g.targetEpisode == episode))
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
			notice("Joined the game as player " + std::to_string(ev.slot + 1) + " (" + kCharacters[ev.slot] + ")");
			break;
		case MP_EV_FAILED: {
			char msg[256];
			mp_link_message(msg, sizeof msg);
			notice(std::string("Multiplayer: ") + msg);
			g.hasTarget = false;
			break;
		}
		case MP_EV_PLAYER_JOINED:
			notice(std::string(ev.name[0] ? ev.name : "A player") + " joined as " + kCharacters[ev.slot]);
			if (g.role == MPG_ROLE_HOST) {
				g.hostFlags.forget(ev.slot);
				g.reportReaders[ev.slot] = FlagReportReader();
				// the newcomer's flags first, then where to go
				sendFull(ev.slot);
				if (g.session)
					sendStage(ev.slot);
			}
			break;
		case MP_EV_PLAYER_LEFT:
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
			if (!g.drawing[slot] || error > 2000.0 || error < -2000.0) {
				g.drawAt[slot]         = target; // first sight, or far off: start there
				g.drawing[slot]        = true;
				g.theirsPerFrame[slot] = g.frameMs;
			} else {
				const double advanced  = (double)(int32_t)(newest - g.newestSeen[slot]);
				g.theirsPerFrame[slot] = g.theirsPerFrame[slot] * 0.98 + (advanced > 0.0 ? advanced : 0.0) * 0.02;
				// leaning harder the further off it is, never by a jump
				const double lean = 0.015 + std::min(0.25, std::fabs(error) / 4000.0);
				g.drawAt[slot] += g.theirsPerFrame[slot] + error * lean;
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
	if (mp_start(&cfg) != 0) {
		char msg[256];
		mp_link_message(msg, sizeof msg);
		notice(std::string("Multiplayer: ") + msg);
		return g.role;
	}
	if (g.role == MPG_ROLE_HOST) {
		notice("Hosting on port " + std::to_string(cfg.port) + ". Players who join come to the level you are in.");
		mp_set_host_status("the title screen");
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
	takePoses();
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
			g.enemyNext = 0;
			g.enemyKills.clear();
			for (int i = 1; i < MP_MAX_PLAYERS; i++)
				if (mp_players() & (1u << i)) {
					sendFull(i);
					sendStage(i);
				}
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
	// this update's share: as many as fit, from where the last one stopped
	uint8_t buf[MP_MAX_WORLD];
	Writer w(buf, sizeof buf);
	w.u32(g.session);
	const size_t per = (sizeof buf - 6 - 1 - g.boss.size()) / kEnemySize;
	const size_t n   = std::min(per, g.enemies.size());
	w.u16((uint16_t)n);
	if (g.enemyNext >= g.enemies.size())
		g.enemyNext = 0;
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
	g.enemyNext = g.enemies.empty() ? 0 : (g.enemyNext + n) % g.enemies.size();
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

extern "C" void mpg_boss_hit(int kind, int count)
{
	if (g.role != MPG_ROLE_CLIENT || !g.localSession || count <= 0)
		return;
	uint8_t buf[8];
	Writer w(buf, sizeof buf);
	w.u32(g.localSession);
	w.u8((uint8_t)kind);
	w.u8((uint8_t)(count > 255 ? 255 : count));
	mp_send(0, K_BOSS_HIT, buf, w.len);
}

extern "C" int mpg_next_boss_hit(int* kind, int* count)
{
	std::lock_guard<std::mutex> hold(g_lock);
	if (g.bossHits.empty())
		return 0;
	*kind  = g.bossHits.front().first;
	*count = g.bossHits.front().second;
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
