// The multiplayer session's rules (mp_game_api.h): stage sessions, warps,
// the shared flags, items and players' looks, between the game half and the
// network (mp_net.h).
#include "../mp_game_api.h"
#include "../net/mp_net.h"
#include "../net/mp_wire.h"
#include "mp_flags.h"

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
};

const char* const kCharacters[MP_MAX_PLAYERS] = {"Mario", "Luigi", "Peach", "Metal Mario"};
const char* const kWorlds[] = {"the Airstrip", "Delfino Plaza", "Bianco Hills", "Ricco Harbor", "Gelato Beach",
                               "Pinna Park", "Sirena Beach", "Noki Bay", "Pianta Village", "Corona Mountain"};
const size_t kPoseSize = 4 + 4 + 48 + 4 + 16 + 4 + 8 + 4 + 4;

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

	// client
	bool hasTarget     = false;
	int targetArea     = -1, targetEpisode = -1;
	uint32_t targetSession = 0;
	uint32_t localSession  = 0;
	ClientFlags clientFlags;
	FlagUpdateReader updateReader;

	std::deque<uint32_t> removals;
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
		if (r.ok && s == g.session && g.session)
			sendTakenList(ev.slot);
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
	out.resize(w.len);
}

bool unpackPose(const uint8_t* d, size_t n, MpgPose& p)
{
	Reader r(d, n);
	memset(&p, 0, sizeof p);
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
	return r.ok;
}

} // namespace
} // namespace mp

using namespace mp;

extern "C" int mpg_start(void)
{
	std::lock_guard<std::mutex> hold(g_lock);
	MpConfig cfg;
	int want = mp_config_from_env(&cfg, SMS_GAME_BUILD);
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
		notice("Hosting a multiplayer game on port " + std::to_string(cfg.port)
		       + ". Players can join once you are in a level.");
		mp_set_host_status("the title screen");
	} else {
		char msg[256];
		mp_link_message(msg, sizeof msg);
		notice(std::string("Multiplayer: ") + msg);
	}
	return g.role;
}

extern "C" int mpg_role(void) { return g.role; }
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
	events();
	const bool arrived = game->playing && !g.wasPlaying;
	g.wasPlaying       = game->playing != 0;

	if (g.role == MPG_ROLE_HOST) {
		if (game->playing && (game->area != g.sessionArea || game->episode != g.sessionEpisode || !g.session)) {
			// a new stage: everyone comes along
			g.session++;
			g.sessionArea    = game->area;
			g.sessionEpisode = game->episode;
			g.taken.clear();
			g.removals.clear();
			g.sessionRed = 0;
			g.restoreRed = false;
			g.shineExit  = false;
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
		if (g.hasTarget && game->area == g.targetArea && game->episode == g.targetEpisode) {
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
	return g.role != MPG_ROLE_CLIENT || !g.hasTarget || mp_link() != MP_LINK_CONNECTED
	       || (g.localSession && g.localSession == g.targetSession);
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
	std::vector<uint8_t> buf;
	packPose(*pose, buf);
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
