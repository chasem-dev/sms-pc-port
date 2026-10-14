// Host test for the multiplayer session layer (mp_net.h): a host and several
// clients as separate processes on this machine, as four games would be.
//   make run
//
// Checks: three clients get the three free slots and hear of each other; a
// fifth player and a different build are refused; player states reach every
// other player through the host; reliable messages arrive complete and in
// order with a third of all packets dropped; a client that leaves is gone at
// once, one that dies after the timeout; a probe gets the host's answer.
#include "mp_net.h"
#include "../mp_socket.h"
#include "../mp_wire.h"

#include <chrono>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

static int g_port;
static const char* kBuild = "test-build";

#define CHECK(c)                                                                     \
	do {                                                                             \
		if (!(c)) {                                                                  \
			fprintf(stderr, "[%d] FAILED %s:%d: %s\n", (int)getpid(), __FILE__, __LINE__, #c); \
			_exit(1);                                                                \
		}                                                                            \
	} while (0)

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static double now()
{
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

static MpConfig config(MpRole role, const char* name, const char* build = kBuild, float drop = 0.0f)
{
	MpConfig c;
	memset(&c, 0, sizeof c);
	c.role = role;
	c.port = (uint16_t)g_port;
	snprintf(c.address, sizeof c.address, "127.0.0.1:%d", g_port);
	snprintf(c.name, sizeof c.name, "%s", name);
	snprintf(c.build, sizeof c.build, "%s", build);
	c.dropRate = drop;
	c.timeout  = 2.0f;
	return c;
}

// Sees every event first, whichever pump() is running (counts messages).
static void (*g_onEvent)(const MpEvent*);

// Polls events until `pred` says done or `seconds` pass.
template <class F> static bool pump(double seconds, F pred)
{
	double end = now() + seconds;
	MpEvent ev;
	while (now() < end) {
		while (mp_poll_event(&ev)) {
			if (g_onEvent)
				g_onEvent(&ev);
			if (pred(&ev))
				return true;
		}
		if (pred(nullptr))
			return true;
		sleepMs(2);
	}
	return false;
}

struct State {
	uint32_t slot, counter;
};

const int kMessages = 300;
static int g_next;            // client: the host's messages so far
static int g_got[MP_MAX_PLAYERS]; // host: each client's messages so far

static void clientEvent(const MpEvent* ev)
{
	if (ev->type == MP_EV_MESSAGE && ev->kind == 8) {
		uint32_t v;
		memcpy(&v, ev->data, 4);
		CHECK(ev->slot == 0);
		CHECK(v == (uint32_t)g_next);
		g_next++;
	}
}

static void hostEvent(const MpEvent* ev)
{
	if (ev->type == MP_EV_MESSAGE && ev->kind == 7) {
		uint32_t m[2];
		memcpy(m, ev->data, sizeof m);
		CHECK(m[0] == (uint32_t)ev->slot);
		CHECK(m[1] == (uint32_t)g_got[ev->slot]);
		g_got[ev->slot]++;
	}
}

// A client: joins, checks slots, states and messages, then leaves (or hangs
// around to be killed).
static int client(int index, bool lingerToBeKilled)
{
	char name[16];
	snprintf(name, sizeof name, "client%d", index);
	g_onEvent  = clientEvent;
	MpConfig c = config(MP_ROLE_CLIENT, name, kBuild, 0.33f);
	CHECK(mp_start(&c) == 0);
	CHECK(pump(5, [](MpEvent*) { return mp_link() == MP_LINK_CONNECTED; }));
	const int slot = mp_local_slot();
	CHECK(slot >= 1 && slot <= 3);

	// everyone (host + 3 clients) shows up
	CHECK(pump(10, [](MpEvent*) { return mp_players() == 0xF; }));
	char host[MP_NAME_LEN];
	mp_player_name(0, host, sizeof host);
	CHECK(!strcmp(host, "host"));

	// our state goes out; everyone else's comes in
	State mine = {(uint32_t)slot, 0};
	bool seen[MP_MAX_PLAYERS] = {};
	CHECK(pump(10, [&](MpEvent*) {
		mine.counter++;
		mp_set_local_state(&mine, sizeof mine);
		int all = 1;
		for (int i = 0; i < MP_MAX_PLAYERS; i++) {
			if (i == slot)
				continue;
			State st;
			float age;
			if (mp_remote_state(i, &st, sizeof st, &age) == sizeof st && st.slot == (uint32_t)i && age < 1.0f)
				seen[i] = true;
			all &= seen[i];
		}
		return all != 0;
	}));

	// reliable messages, numbered, both ways
	for (int i = 0; i < kMessages; i++) {
		uint32_t m[2] = {(uint32_t)slot, (uint32_t)i};
		CHECK(mp_send(0, 7, m, sizeof m) == 0);
	}
	CHECK(pump(30, [&](MpEvent*) { return g_next == kMessages; }));
	fprintf(stderr, "[client %d] slot %d: states and %d messages each way ok, ping %d ms\n", index, slot, kMessages,
	        mp_ping(0));
	if (lingerToBeKilled) {
		for (;;)
			sleepMs(100);
	}
	// stay until the host says it got everything (message kind 9)
	CHECK(pump(30, [](MpEvent* ev) { return ev && ev->type == MP_EV_MESSAGE && ev->kind == 9; }));
	mp_stop();
	return 0;
}

static int refused(const char* build, const char* expect)
{
	MpConfig c = config(MP_ROLE_CLIENT, "late", build);
	CHECK(mp_start(&c) == 0);
	CHECK(pump(5, [](MpEvent*) { return mp_link() == MP_LINK_FAILED; }));
	char msg[256];
	mp_link_message(msg, sizeof msg);
	fprintf(stderr, "[refused] %s\n", msg);
	CHECK(strstr(msg, expect) != nullptr);
	mp_stop();
	return 0;
}

static void probe()
{
	mp::Address to;
	std::string err;
	char addr[64];
	snprintf(addr, sizeof addr, "127.0.0.1:%d", g_port);
	CHECK(mp::Socket::resolve(addr, 1, to, err));
	mp::Socket s;
	CHECK(s.openFor(to, err));
	uint8_t buf[512];
	mp::Writer w(buf, sizeof buf);
	w.u32(0x504D4D53);
	w.u16(1);
	w.u8(1);
	w.u32(1234);
	// the host drops a third of what it hears: ask until it answers
	mp::Address from;
	int n = -1;
	for (int tries = 0; tries < 20 && n <= 0; tries++) {
		CHECK(s.send(buf, w.len, to));
		if (s.wait(200))
			n = s.recv(buf, sizeof buf, from);
	}
	CHECK(n > 0);
	mp::Reader r(buf, (size_t)n);
	CHECK(r.u32() == 0x504D4D53);
	r.u16();
	CHECK(r.u8() == 2);
	CHECK(r.u32() == 1234);
	int players = r.u8(), max = r.u8(), open = r.u8();
	char build[64], name[32], status[64];
	r.str(build, sizeof build);
	r.str(name, sizeof name);
	r.str(status, sizeof status);
	CHECK(r.ok);
	fprintf(stderr, "[probe] %d/%d players, open %d, build %s, host %s, status \"%s\"\n", players, max, open, build,
	        name, status);
	CHECK(max == 4 && !strcmp(build, kBuild) && !strcmp(name, "host") && !strcmp(status, "Delfino Plaza"));
}

static pid_t spawn(int (*fn)(int, bool), int index, bool linger)
{
	pid_t pid = fork();
	if (pid == 0)
		_exit(fn(index, linger));
	return pid;
}

static pid_t spawnRefused(const char* build, const char* expect)
{
	pid_t pid = fork();
	if (pid == 0)
		_exit(refused(build, expect));
	return pid;
}

static bool exitedOk(pid_t pid)
{
	int status = 0;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main()
{
	srand((unsigned)time(nullptr) ^ (unsigned)getpid());
	g_port = 30000 + rand() % 20000;
	g_onEvent = hostEvent;
	MpConfig h = config(MP_ROLE_HOST, "host", kBuild, 0.33f);
	CHECK(mp_start(&h) == 0);
	CHECK(mp_link() == MP_LINK_CONNECTED && mp_local_slot() == 0);
	mp_set_host_status("Delfino Plaza");
	probe();

	pid_t kids[3];
	kids[0] = spawn(client, 1, false);
	kids[1] = spawn(client, 2, false);
	kids[2] = spawn(client, 3, true); // killed later, to time out

	CHECK(pump(10, [](MpEvent*) { return mp_players() == 0xF; }));
	fprintf(stderr, "[host] 3 clients joined\n");
	// the fifth player and a different build are refused
	CHECK(exitedOk(spawnRefused(kBuild, "full")));
	CHECK(exitedOk(spawnRefused("other-build", "different version")));

	// relay states and take every client's messages in order
	State mine = {0, 0};
	bool sentAll = false;
	CHECK(pump(40, [&](MpEvent*) {
		mine.counter++;
		mp_set_local_state(&mine, sizeof mine);
		if (!sentAll) {
			for (int i = 0; i < kMessages; i++) {
				uint32_t v = (uint32_t)i;
				CHECK(mp_send(-1, 8, &v, sizeof v) == 0);
			}
			sentAll = true;
		}
		return g_got[1] == kMessages && g_got[2] == kMessages && g_got[3] == kMessages;
	}));
	fprintf(stderr, "[host] %d messages from each client, in order\n", kMessages);

	// find which slot the lingering client got, then kill it: it must time out
	sleepMs(1500); // its own checks finish (it reads our 300 messages)
	State st;
	int lingering = -1;
	for (int i = 1; i < MP_MAX_PLAYERS; i++)
		if (mp_remote_state(i, &st, sizeof st, nullptr))
			lingering = lingering < 0 ? i : lingering;
	kill(kids[2], SIGKILL);
	waitpid(kids[2], nullptr, 0);
	double killedAt = now();
	int left        = -1;
	CHECK(pump(6, [&](MpEvent* ev) {
		if (ev && ev->type == MP_EV_PLAYER_LEFT)
			left = ev->slot;
		return left >= 0;
	}));
	double waited = now() - killedAt;
	fprintf(stderr, "[host] killed client left after %.1f s (slot %d)\n", waited, left);
	CHECK(waited > 1.5 && waited < 4.0);
	(void)lingering;

	// the others leave politely: gone at once
	CHECK(mp_send(-1, 9, "", 0) == 0);
	int leftCount = 0;
	double start  = now();
	CHECK(pump(10, [&](MpEvent* ev) {
		if (ev && ev->type == MP_EV_PLAYER_LEFT)
			leftCount++;
		return leftCount == 2;
	}));
	fprintf(stderr, "[host] two clients said goodbye, gone after %.2f s\n", now() - start);
	CHECK(exitedOk(kids[0]));
	CHECK(exitedOk(kids[1]));
	CHECK(mp_players() == 1);
	mp_stop();

	// a client with nobody to join gives up with a message
	pid_t pid = fork();
	if (pid == 0) {
		MpConfig c = config(MP_ROLE_CLIENT, "lonely");
		CHECK(mp_start(&c) == 0);
		CHECK(pump(15, [](MpEvent*) { return mp_link() == MP_LINK_FAILED; }));
		char msg[256];
		mp_link_message(msg, sizeof msg);
		fprintf(stderr, "[lonely] %s\n", msg);
		CHECK(strstr(msg, "No game answered"));
		_exit(0);
	}
	CHECK(exitedOk(pid));
	// bad settings are reported, not guessed at
	setenv("SMS_MULTIPLAYER", "join", 1);
	unsetenv("SMS_MULTIPLAYER_ADDRESS");
	MpConfig bad;
	CHECK(mp_config_from_env(&bad, kBuild) == -1);
	setenv("SMS_MULTIPLAYER_ADDRESS", "example.invalid:99999", 1);
	CHECK(mp_config_from_env(&bad, kBuild) == 1);
	CHECK(mp_start(&bad) == -1 && mp_link() == MP_LINK_FAILED);
	char msg[256];
	mp_link_message(msg, sizeof msg);
	fprintf(stderr, "[config] %s\n", msg);
	CHECK(strstr(msg, "port"));
	mp_stop();
	fprintf(stderr, "PASS\n");
	return 0;
}
