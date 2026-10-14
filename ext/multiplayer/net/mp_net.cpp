// Multiplayer sessions over UDP (mp_net.h): joining, slots, the reliable
// channel and the relay of player states.
//
// Every packet starts "SMMP", the protocol version and its type:
//   PROBE   nonce                          anyone -> host: is this a game?
//   INFO    nonce, players, max, open, build, name, status   host's answer
//   CONNECT nonce, build, name             client -> host, until answered
//   ACCEPT  nonce, slot, token             host -> client
//   REJECT  nonce, reason, message         host -> client
//   DATA    token, ack, time, echo, states, messages   both ways, ~30 a second
//   BYE     token                          either side, when leaving
// DATA carries the newest player states (unreliable: a lost one is replaced
// by the next) and every reliable message the other side has not
// acknowledged, resent until it is. `ack` is the next reliable sequence
// number the sender expects, so everything before it has arrived.
#include "mp_net.h"
#include "mp_socket.h"
#include "mp_wire.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>
#include <vector>

namespace mp {
namespace {

const uint32_t kMagic    = 0x504D4D53; // "SMMP"
const uint16_t kProtocol = 1;
enum PacketType : uint8_t {
	PK_PROBE = 1,
	PK_INFO,
	PK_CONNECT,
	PK_ACCEPT,
	PK_REJECT,
	PK_DATA,
	PK_BYE,
};
enum RejectReason : uint8_t {
	REJECT_FULL = 1,
	REJECT_BUILD,
	REJECT_CLOSED,
};
// Messages the session itself sends (above the game's kinds).
enum SystemKind : uint8_t {
	SYS_PLAYER_JOINED = 0xF0, // slot, name
	SYS_PLAYER_LEFT,          // slot
};

const size_t kPacketMax    = 1400;
const int kSendIntervalMs  = 33;  // states ~30 a second
const int kResendMs        = 150; // an unacknowledged message goes again
const int kConnectRetryMs  = 500;
const int kConnectGiveUpMs = 10000;

typedef std::chrono::steady_clock Clock;

uint32_t nowMs()
{
	static const Clock::time_point start = Clock::now();
	return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

struct Outgoing {
	uint32_t seq;
	uint8_t kind;
	std::vector<uint8_t> data;
	uint32_t lastSent; // 0: not sent yet
};

struct Incoming {
	uint8_t kind;
	std::vector<uint8_t> data;
};

struct Peer {
	bool active = false;
	Address addr;
	uint32_t token = 0;
	uint32_t nonce = 0;
	char name[MP_NAME_LEN] = {};
	uint32_t lastHeard     = 0;
	uint32_t nextSeq       = 1; // ours, outgoing
	std::deque<Outgoing> out;
	uint32_t recvNext = 1; // theirs, the next one to deliver
	std::map<uint32_t, Incoming> early;
	uint32_t peerTime  = 0; // their clock in their newest DATA, echoed back
	uint32_t peerTimeAt = 0;
	int rtt            = -1;
	bool flush         = false; // a new reliable message: send now
};

struct PlayerSlot {
	bool present = false;
	char name[MP_NAME_LEN] = {};
	uint8_t state[MP_MAX_STATE];
	size_t stateSize  = 0;
	uint32_t stateSeq = 0;
	uint32_t stateAt  = 0;
};

struct Session {
	std::mutex lock;
	MpConfig cfg;
	MpLinkState link = MP_LINK_OFF;
	std::string message;
	Socket sock;
	Address hostAddr; // client
	int localSlot = -1;
	uint32_t connectNonce = 0, connectStarted = 0, connectLastTry = 0;
	Peer peers[MP_MAX_PLAYERS]; // host: by slot (0 unused); client: [0] is the host
	PlayerSlot players[MP_MAX_PLAYERS];
	uint32_t localStateSeq = 0;
	std::deque<MpEvent> events;
	std::string status;
	std::thread thread;
	std::atomic<bool> running{false};
	std::mt19937 rng{std::random_device{}()};
	uint32_t lastSend = 0;
};

Session* g_session;
std::mutex g_sessionLock; // guards g_session itself

void copyName(char (&dst)[MP_NAME_LEN], const char* src)
{
	size_t n = strnlen(src, MP_NAME_LEN - 1);
	memmove(dst, src, n);
	dst[n] = 0;
}

void header(Writer& w, PacketType type)
{
	w.u32(kMagic);
	w.u16(kProtocol);
	w.u8(type);
}

void pushEvent(Session& s, MpEventType type, int slot, uint8_t kind = 0, const void* data = nullptr,
               size_t size = 0, const char* name = nullptr)
{
	s.events.emplace_back();
	MpEvent& ev = s.events.back();
	memset(&ev, 0, offsetof(MpEvent, data));
	ev.type = type;
	ev.slot = slot;
	ev.kind = kind;
	ev.size = (uint16_t)size;
	if (size)
		memcpy(ev.data, data, size);
	if (name)
		snprintf(ev.name, sizeof ev.name, "%s", name);
}

void queueReliable(Peer& p, uint8_t kind, const void* data, size_t size)
{
	Outgoing o;
	o.seq      = p.nextSeq++;
	o.kind     = kind;
	o.lastSent = 0;
	o.data.assign((const uint8_t*)data, (const uint8_t*)data + size);
	p.out.push_back(std::move(o));
	p.flush = true;
}

int peerIndexForSlot(Session& s, int slot)
{
	if (s.cfg.role == MP_ROLE_HOST)
		return slot >= 1 && slot < MP_MAX_PLAYERS && s.peers[slot].active ? slot : -1;
	return s.peers[0].active ? 0 : -1;
}

void sendData(Session& s, Peer& p, int toSlot, uint32_t now, bool force)
{
	uint8_t buf[kPacketMax];
	Writer w(buf, sizeof buf);
	header(w, PK_DATA);
	w.u32(p.token);
	w.u32(p.recvNext);
	w.u32(now);
	w.u32(p.peerTime + (now - p.peerTimeAt));

	// player states: the client sends its own; the host sends everyone else's
	size_t countAt = w.len;
	uint8_t count  = 0;
	w.u8(0);
	for (int i = 0; i < MP_MAX_PLAYERS; i++) {
		const PlayerSlot& pl = s.players[i];
		if (!pl.present || !pl.stateSize || i == toSlot)
			continue;
		if (s.cfg.role == MP_ROLE_CLIENT && i != s.localSlot)
			continue;
		w.u8((uint8_t)i);
		w.u32(pl.stateSeq);
		w.u8((uint8_t)pl.stateSize);
		w.bytes(pl.state, pl.stateSize);
		count++;
	}
	buf[countAt] = count;

	// reliable messages not acknowledged, oldest first, as many as fit
	countAt = w.len;
	count   = 0;
	w.u8(0);
	for (Outgoing& o : p.out) {
		if (count == 255)
			break;
		if (o.lastSent && now - o.lastSent < (uint32_t)kResendMs && !force)
			continue;
		if (w.len + 7 + o.data.size() > sizeof buf)
			break;
		w.u32(o.seq);
		w.u8(o.kind);
		w.u16((uint16_t)o.data.size());
		w.bytes(o.data.data(), o.data.size());
		o.lastSent = now;
		count++;
	}
	buf[countAt] = count;
	p.flush      = false;
	if (w.ok)
		s.sock.send(buf, w.len, p.addr);
}

void dropPeer(Session& s, int idx, const char* why)
{
	Peer& p = s.peers[idx];
	if (!p.active)
		return;
	if (s.cfg.role == MP_ROLE_HOST) {
		fprintf(stderr, "[multiplayer] %s (slot %d) %s\n", p.name, idx, why);
		p          = Peer();
		s.players[idx] = PlayerSlot();
		pushEvent(s, MP_EV_PLAYER_LEFT, idx);
		uint8_t slot = (uint8_t)idx;
		for (int i = 1; i < MP_MAX_PLAYERS; i++)
			if (s.peers[i].active)
				queueReliable(s.peers[i], SYS_PLAYER_LEFT, &slot, 1);
	} else {
		p      = Peer();
		s.link = MP_LINK_FAILED;
		s.message = why;
		for (int i = 0; i < MP_MAX_PLAYERS; i++)
			if (i != s.localSlot && s.players[i].present) {
				s.players[i] = PlayerSlot();
				pushEvent(s, MP_EV_PLAYER_LEFT, i);
			}
		pushEvent(s, MP_EV_FAILED, -1);
		fprintf(stderr, "[multiplayer] %s\n", why);
	}
}

void deliver(Session& s, int fromSlot, uint8_t kind, const uint8_t* data, size_t size)
{
	if (kind < SYS_PLAYER_JOINED) {
		pushEvent(s, MP_EV_MESSAGE, fromSlot, kind, data, size);
		return;
	}
	if (s.cfg.role != MP_ROLE_CLIENT)
		return; // clients never send system messages
	Reader r(data, size);
	uint8_t slot = r.u8();
	if (!r.ok || slot >= MP_MAX_PLAYERS || (int)slot == s.localSlot)
		return;
	if (kind == SYS_PLAYER_JOINED) {
		PlayerSlot& pl = s.players[slot];
		pl             = PlayerSlot();
		pl.present     = true;
		r.str(pl.name, sizeof pl.name);
		pushEvent(s, MP_EV_PLAYER_JOINED, slot, 0, nullptr, 0, pl.name);
	} else if (kind == SYS_PLAYER_LEFT && s.players[slot].present) {
		s.players[slot] = PlayerSlot();
		pushEvent(s, MP_EV_PLAYER_LEFT, slot);
	}
}

void readData(Session& s, int idx, Reader& r, uint32_t now)
{
	Peer& p     = s.peers[idx];
	uint32_t ack  = r.u32();
	uint32_t time = r.u32();
	uint32_t echo = r.u32();
	if (!r.ok)
		return;
	p.lastHeard  = now;
	p.peerTime   = time;
	p.peerTimeAt = now;
	if (echo && now >= echo && now - echo < 10000)
		p.rtt = (int)(now - echo);
	while (!p.out.empty() && (int32_t)(p.out.front().seq - ack) < 0)
		p.out.pop_front();

	const int fromSlot = s.cfg.role == MP_ROLE_HOST ? idx : 0;
	uint8_t nStates    = r.u8();
	for (int i = 0; i < nStates && r.ok; i++) {
		uint8_t slot  = r.u8();
		uint32_t seq  = r.u32();
		uint8_t size  = r.u8();
		const uint8_t* d = r.skip(size);
		if (!d || slot >= MP_MAX_PLAYERS || size > MP_MAX_STATE)
			return;
		// a client speaks only for itself; the host for anyone but us
		if (s.cfg.role == MP_ROLE_HOST ? slot != idx : slot == s.localSlot)
			continue;
		PlayerSlot& pl = s.players[slot];
		if (!pl.present || (pl.stateSize && (int32_t)(seq - pl.stateSeq) <= 0))
			continue;
		memcpy(pl.state, d, size);
		pl.stateSize = size;
		pl.stateSeq  = seq;
		pl.stateAt   = now;
	}

	uint8_t nMsgs = r.u8();
	bool got      = false;
	for (int i = 0; i < nMsgs && r.ok; i++) {
		uint32_t seq     = r.u32();
		uint8_t kind     = r.u8();
		uint16_t size    = r.u16();
		const uint8_t* d = r.skip(size);
		if (!d || size > MP_MAX_MESSAGE)
			return;
		got = true;
		if ((int32_t)(seq - p.recvNext) < 0 || p.early.count(seq))
			continue; // already have it
		Incoming in;
		in.kind = kind;
		in.data.assign(d, d + size);
		p.early.emplace(seq, std::move(in));
		if (p.early.size() > 4096) {
			dropPeer(s, idx, "sent too much at once");
			return;
		}
	}
	// deliver in order
	for (auto it = p.early.find(p.recvNext); it != p.early.end(); it = p.early.find(p.recvNext)) {
		deliver(s, fromSlot, it->second.kind, it->second.data.data(), it->second.data.size());
		p.early.erase(it);
		p.recvNext++;
	}
	if (got)
		p.flush = true; // acknowledge soon
}

void hostPacket(Session& s, const uint8_t* buf, int n, const Address& from, uint32_t now)
{
	Reader r(buf, (size_t)n);
	if (r.u32() != kMagic)
		return;
	uint16_t version = r.u16();
	uint8_t type     = r.u8();
	if (!r.ok)
		return;
	uint8_t out[kPacketMax];
	Writer w(out, sizeof out);

	if (type == PK_PROBE) {
		uint32_t nonce = r.u32();
		int count      = 0;
		for (const PlayerSlot& pl : s.players)
			count += pl.present;
		header(w, PK_INFO);
		w.u32(nonce);
		w.u8((uint8_t)count);
		w.u8(MP_MAX_PLAYERS);
		w.u8(version == kProtocol && count < MP_MAX_PLAYERS);
		w.str(s.cfg.build, 63);
		w.str(s.cfg.name, MP_NAME_LEN - 1);
		w.str(s.status.c_str(), 63);
		w.u16(kProtocol);
		s.sock.send(out, w.len, from);
		return;
	}
	if (type == PK_CONNECT) {
		uint32_t nonce = r.u32();
		char build[64], name[MP_NAME_LEN];
		r.str(build, sizeof build);
		r.str(name, sizeof name);
		if (!r.ok)
			return;
		// a retry whose ACCEPT went missing
		for (int i = 1; i < MP_MAX_PLAYERS; i++) {
			Peer& p = s.peers[i];
			if (p.active && p.addr == from && p.nonce == nonce) {
				header(w, PK_ACCEPT);
				w.u32(nonce);
				w.u8((uint8_t)i);
				w.u32(p.token);
				s.sock.send(out, w.len, from);
				return;
			}
		}
		const char* why = nullptr;
		uint8_t reason  = 0;
		int slot        = -1;
		for (int i = 1; i < MP_MAX_PLAYERS && slot < 0; i++)
			if (!s.peers[i].active)
				slot = i;
		if (version != kProtocol || strcmp(build, s.cfg.build) != 0) {
			reason = REJECT_BUILD;
			why    = "The host is playing a different version of the game. Update both games to the same version.";
		} else if (slot < 0) {
			reason = REJECT_FULL;
			why    = "The game is full (4 players).";
		}
		if (why) {
			header(w, PK_REJECT);
			w.u32(nonce);
			w.u8(reason);
			w.str(why, 200);
			s.sock.send(out, w.len, from);
			return;
		}
		// a player reconnecting from the same address replaces their old slot
		for (int i = 1; i < MP_MAX_PLAYERS; i++)
			if (s.peers[i].active && s.peers[i].addr == from)
				dropPeer(s, i, "reconnected");
		Peer& p     = s.peers[slot];
		p           = Peer();
		p.active    = true;
		p.addr      = from;
		p.nonce     = nonce;
		p.token     = (uint32_t)s.rng() | 1;
		p.lastHeard = now;
		snprintf(p.name, sizeof p.name, "%s", name[0] ? name : "Player");
		PlayerSlot& pl = s.players[slot];
		pl             = PlayerSlot();
		pl.present     = true;
		copyName(pl.name, p.name);
		fprintf(stderr, "[multiplayer] %s joined from %s as player %d\n", p.name, from.text().c_str(), slot + 1);

		header(w, PK_ACCEPT);
		w.u32(nonce);
		w.u8((uint8_t)slot);
		w.u32(p.token);
		s.sock.send(out, w.len, from);

		// the newcomer hears of everyone; everyone hears of the newcomer
		for (int i = 0; i < MP_MAX_PLAYERS; i++) {
			if (i == slot || !s.players[i].present)
				continue;
			uint8_t msg[1 + MP_NAME_LEN];
			Writer m(msg, sizeof msg);
			m.u8((uint8_t)i);
			m.str(s.players[i].name, MP_NAME_LEN - 1);
			queueReliable(p, SYS_PLAYER_JOINED, msg, m.len);
			if (i > 0 && s.peers[i].active) {
				Writer n(msg, sizeof msg);
				n.u8((uint8_t)slot);
				n.str(pl.name, MP_NAME_LEN - 1);
				queueReliable(s.peers[i], SYS_PLAYER_JOINED, msg, n.len);
			}
		}
		pushEvent(s, MP_EV_PLAYER_JOINED, slot, 0, nullptr, 0, pl.name);
		return;
	}
	if (type == PK_DATA || type == PK_BYE) {
		uint32_t token = r.u32();
		for (int i = 1; i < MP_MAX_PLAYERS; i++) {
			Peer& p = s.peers[i];
			if (!p.active || p.token != token || p.addr != from)
				continue;
			if (type == PK_BYE)
				dropPeer(s, i, "left");
			else
				readData(s, i, r, now);
			return;
		}
	}
}

void clientPacket(Session& s, const uint8_t* buf, int n, const Address& from, uint32_t now)
{
	if (from != s.hostAddr)
		return;
	Reader r(buf, (size_t)n);
	if (r.u32() != kMagic)
		return;
	r.u16();
	uint8_t type = r.u8();
	if (!r.ok)
		return;
	if (type == PK_ACCEPT && s.link == MP_LINK_CONNECTING) {
		uint32_t nonce = r.u32();
		uint8_t slot   = r.u8();
		uint32_t token = r.u32();
		if (!r.ok || nonce != s.connectNonce || slot == 0 || slot >= MP_MAX_PLAYERS)
			return;
		Peer& h     = s.peers[0];
		h           = Peer();
		h.active    = true;
		h.addr      = from;
		h.token     = token;
		h.lastHeard = now;
		s.localSlot = slot;
		s.link      = MP_LINK_CONNECTED;
		PlayerSlot& me = s.players[slot];
		me.present     = true;
		snprintf(me.name, sizeof me.name, "%s", s.cfg.name);
		char msg[160];
		snprintf(msg, sizeof msg, "Connected to %s as player %d", s.hostAddr.text().c_str(), slot + 1);
		s.message = msg;
		fprintf(stderr, "[multiplayer] %s\n", msg);
		pushEvent(s, MP_EV_CONNECTED, slot);
		return;
	}
	if (type == PK_REJECT && s.link == MP_LINK_CONNECTING) {
		uint32_t nonce = r.u32();
		r.u8();
		char why[201];
		r.str(why, sizeof why);
		if (!r.ok || nonce != s.connectNonce)
			return;
		s.link    = MP_LINK_FAILED;
		s.message = why;
		fprintf(stderr, "[multiplayer] the host refused: %s\n", why);
		pushEvent(s, MP_EV_FAILED, -1);
		return;
	}
	if ((type == PK_DATA || type == PK_BYE) && s.peers[0].active) {
		uint32_t token = r.u32();
		if (token != s.peers[0].token)
			return;
		if (type == PK_BYE)
			dropPeer(s, 0, "The host closed the game.");
		else
			readData(s, 0, r, now);
	}
}

void tick(Session& s, uint32_t now)
{
	if (s.cfg.role == MP_ROLE_CLIENT && s.link == MP_LINK_CONNECTING) {
		if (now - s.connectStarted > (uint32_t)kConnectGiveUpMs) {
			s.link = MP_LINK_FAILED;
			char msg[256];
			snprintf(msg, sizeof msg,
			         "No game answered at %s. Check the address and port, and that the host has started "
			         "Host Game and forwarded the port.",
			         s.hostAddr.text().c_str());
			s.message = msg;
			fprintf(stderr, "[multiplayer] %s\n", msg);
			pushEvent(s, MP_EV_FAILED, -1);
		} else if (!s.connectLastTry || now - s.connectLastTry >= (uint32_t)kConnectRetryMs) {
			uint8_t out[256];
			Writer w(out, sizeof out);
			header(w, PK_CONNECT);
			w.u32(s.connectNonce);
			w.str(s.cfg.build, 63);
			w.str(s.cfg.name, MP_NAME_LEN - 1);
			s.sock.send(out, w.len, s.hostAddr);
			s.connectLastTry = now;
		}
		return;
	}
	const uint32_t timeoutMs = (uint32_t)(s.cfg.timeout * 1000.0f);
	const bool sendTick      = now - s.lastSend >= (uint32_t)kSendIntervalMs;
	if (sendTick)
		s.lastSend = now;
	for (int i = 0; i < MP_MAX_PLAYERS; i++) {
		Peer& p = s.peers[i];
		if (!p.active)
			continue;
		if (now - p.lastHeard > timeoutMs) {
			dropPeer(s, i, s.cfg.role == MP_ROLE_HOST ? "timed out" : "Lost the connection to the host.");
			continue;
		}
		if (sendTick || p.flush)
			sendData(s, p, s.cfg.role == MP_ROLE_HOST ? i : -1, now, false);
	}
}

void run(Session* s)
{
	uint8_t buf[2048];
	std::uniform_real_distribution<float> chance(0.0f, 1.0f);
	while (s->running.load()) {
		s->sock.wait(5);
		std::lock_guard<std::mutex> hold(s->lock);
		const uint32_t now = nowMs();
		Address from;
		int n;
		while ((n = s->sock.recv(buf, sizeof buf, from)) >= 0) {
			if (s->cfg.dropRate > 0 && chance(s->rng) < s->cfg.dropRate)
				continue;
			if (s->cfg.role == MP_ROLE_HOST)
				hostPacket(*s, buf, n, from, now);
			else
				clientPacket(*s, buf, n, from, now);
		}
		tick(*s, now);
	}
	// say goodbye so the others need not wait for a timeout
	std::lock_guard<std::mutex> hold(s->lock);
	for (Peer& p : s->peers)
		if (p.active) {
			uint8_t out[32];
			Writer w(out, sizeof out);
			header(w, PK_BYE);
			w.u32(p.token);
			s->sock.send(out, w.len, p.addr);
		}
}

template <class F>
auto withSession(F f, decltype(f(*(Session*)nullptr)) fallback) -> decltype(f(*(Session*)nullptr))
{
	std::lock_guard<std::mutex> g(g_sessionLock);
	if (!g_session)
		return fallback;
	std::lock_guard<std::mutex> hold(g_session->lock);
	return f(*g_session);
}

} // namespace
} // namespace mp

using namespace mp;

extern "C" int mp_config_from_env(MpConfig* cfg, const char* build)
{
	memset(cfg, 0, sizeof *cfg);
	cfg->timeout = 10.0f;
	cfg->port    = MP_DEFAULT_PORT;
	snprintf(cfg->build, sizeof cfg->build, "%s", build ? build : "");
	const char* mode = getenv("SMS_MULTIPLAYER");
	if (!mode || !*mode || !strcmp(mode, "0") || !strcmp(mode, "off"))
		return 0;
	if (!strcmp(mode, "host"))
		cfg->role = MP_ROLE_HOST;
	else if (!strcmp(mode, "join"))
		cfg->role = MP_ROLE_CLIENT;
	else {
		snprintf(cfg->address, sizeof cfg->address, "SMS_MULTIPLAYER must be host or join, not %s", mode);
		return -1;
	}
	if (const char* port = getenv("SMS_MULTIPLAYER_PORT")) {
		long v = strtol(port, nullptr, 10);
		if (v < 1 || v > 65535) {
			snprintf(cfg->address, sizeof cfg->address, "SMS_MULTIPLAYER_PORT must be 1 to 65535");
			return -1;
		}
		cfg->port = (uint16_t)v;
	}
	if (cfg->role == MP_ROLE_CLIENT) {
		const char* addr = getenv("SMS_MULTIPLAYER_ADDRESS");
		if (!addr || !*addr) {
			snprintf(cfg->address, sizeof cfg->address, "SMS_MULTIPLAYER_ADDRESS is needed to join");
			return -1;
		}
		snprintf(cfg->address, sizeof cfg->address, "%s", addr);
	}
	const char* name = getenv("SMS_MULTIPLAYER_NAME");
	snprintf(cfg->name, sizeof cfg->name, "%s",
	         name && *name ? name : cfg->role == MP_ROLE_HOST ? "Host" : "Player");
	if (const char* drop = getenv("SMS_MULTIPLAYER_DROP"))
		cfg->dropRate = (float)atof(drop);
	if (const char* t = getenv("SMS_MULTIPLAYER_TIMEOUT"))
		cfg->timeout = (float)atof(t) > 1.0f ? (float)atof(t) : cfg->timeout;
	return 1;
}

extern "C" int mp_start(const MpConfig* cfg)
{
	mp_stop();
	Session* s = new Session();
	s->cfg     = *cfg;
	if (s->cfg.timeout <= 0)
		s->cfg.timeout = 10.0f;
	std::string error;
	if (cfg->role == MP_ROLE_HOST) {
		if (!s->sock.open(cfg->port, error)) {
			std::lock_guard<std::mutex> g(g_sessionLock);
			s->link    = MP_LINK_FAILED;
			s->message = "Could not host on port " + std::to_string(cfg->port) + ": " + error;
			fprintf(stderr, "[multiplayer] %s\n", s->message.c_str());
			g_session = s;
			return -1;
		}
		s->localSlot          = 0;
		s->link               = MP_LINK_CONNECTED;
		s->players[0].present = true;
		snprintf(s->players[0].name, sizeof s->players[0].name, "%s", cfg->name);
		s->message = "Hosting on port " + std::to_string(s->sock.localPort());
	} else {
		if (!Socket::resolve(cfg->address, cfg->port, s->hostAddr, error) || !s->sock.openFor(s->hostAddr, error)) {
			std::lock_guard<std::mutex> g(g_sessionLock);
			s->link    = MP_LINK_FAILED;
			s->message = error;
			fprintf(stderr, "[multiplayer] %s\n", error.c_str());
			g_session = s;
			return -1;
		}
		s->link           = MP_LINK_CONNECTING;
		s->connectNonce   = (uint32_t)s->rng() | 1;
		s->connectStarted = nowMs();
		s->message        = "Connecting to " + s->hostAddr.text() + "…";
	}
	fprintf(stderr, "[multiplayer] %s\n", s->message.c_str());
	s->running = true;
	{
		std::lock_guard<std::mutex> g(g_sessionLock);
		g_session = s;
	}
	s->thread = std::thread(run, s);
	return 0;
}

extern "C" void mp_stop(void)
{
	Session* s;
	{
		std::lock_guard<std::mutex> g(g_sessionLock);
		s         = g_session;
		g_session = nullptr;
	}
	if (!s)
		return;
	s->running = false;
	if (s->thread.joinable())
		s->thread.join();
	delete s;
}

extern "C" MpRole mp_role(void)
{
	return withSession([](Session& s) { return s.cfg.role; }, MP_ROLE_OFF);
}

extern "C" MpLinkState mp_link(void)
{
	return withSession([](Session& s) { return s.link; }, MP_LINK_OFF);
}

extern "C" void mp_link_message(char* out, size_t size)
{
	if (!size)
		return;
	out[0] = 0;
	withSession(
	    [&](Session& s) {
		    snprintf(out, size, "%s", s.message.c_str());
		    return 0;
	    },
	    0);
}

extern "C" int mp_local_slot(void)
{
	return withSession([](Session& s) { return s.localSlot; }, -1);
}

extern "C" unsigned mp_players(void)
{
	return withSession(
	    [](Session& s) {
		    unsigned mask = 0;
		    for (int i = 0; i < MP_MAX_PLAYERS; i++)
			    if (s.players[i].present)
				    mask |= 1u << i;
		    return mask;
	    },
	    0u);
}

extern "C" void mp_player_name(int slot, char* out, size_t size)
{
	if (!size)
		return;
	out[0] = 0;
	if (slot < 0 || slot >= MP_MAX_PLAYERS)
		return;
	withSession(
	    [&](Session& s) {
		    snprintf(out, size, "%s", s.players[slot].name);
		    return 0;
	    },
	    0);
}

extern "C" int mp_ping(int slot)
{
	return withSession(
	    [&](Session& s) {
		    int idx = peerIndexForSlot(s, slot);
		    return idx < 0 ? -1 : s.peers[idx].rtt;
	    },
	    -1);
}

extern "C" void mp_set_local_state(const void* data, size_t size)
{
	if (size > MP_MAX_STATE)
		return;
	withSession(
	    [&](Session& s) {
		    if (s.localSlot < 0)
			    return 0;
		    PlayerSlot& me = s.players[s.localSlot];
		    memcpy(me.state, data, size);
		    me.stateSize = size;
		    me.stateSeq  = ++s.localStateSeq;
		    me.stateAt   = nowMs();
		    return 0;
	    },
	    0);
}

extern "C" size_t mp_remote_state(int slot, void* data, size_t size, float* age)
{
	if (slot < 0 || slot >= MP_MAX_PLAYERS)
		return 0;
	return withSession(
	    [&](Session& s) -> size_t {
		    const PlayerSlot& pl = s.players[slot];
		    if (!pl.present || !pl.stateSize || slot == s.localSlot)
			    return 0;
		    memcpy(data, pl.state, pl.stateSize < size ? pl.stateSize : size);
		    if (age)
			    *age = (nowMs() - pl.stateAt) / 1000.0f;
		    return pl.stateSize;
	    },
	    (size_t)0);
}

extern "C" int mp_send(int slot, uint8_t kind, const void* data, size_t size)
{
	if (size > MP_MAX_MESSAGE || kind >= SYS_PLAYER_JOINED)
		return -1;
	return withSession(
	    [&](Session& s) {
		    if (s.link != MP_LINK_CONNECTED)
			    return -1;
		    if (s.cfg.role == MP_ROLE_CLIENT) {
			    queueReliable(s.peers[0], kind, data, size);
			    return 0;
		    }
		    int sent = 0;
		    for (int i = 1; i < MP_MAX_PLAYERS; i++)
			    if (s.peers[i].active && (slot < 0 || slot == i)) {
				    queueReliable(s.peers[i], kind, data, size);
				    sent++;
			    }
		    return sent || slot < 0 ? 0 : -1;
	    },
	    -1);
}

extern "C" int mp_poll_event(MpEvent* ev)
{
	return withSession(
	    [&](Session& s) {
		    if (s.events.empty())
			    return 0;
		    *ev = s.events.front();
		    s.events.pop_front();
		    return 1;
	    },
	    0);
}

extern "C" void mp_set_host_status(const char* status)
{
	withSession(
	    [&](Session& s) {
		    s.status = status ? status : "";
		    return 0;
	    },
	    0);
}
