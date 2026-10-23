// Achievements (ext/achievements/README.md): the shared state and helpers of
// achievements.h. Whether the play counts: a save state loaded or the warp
// menu used since the file was chosen, or a cheat on during this stage,
// keeps achievements from unlocking. The host half (host/ach_store.cpp) keeps
// what was unlocked and tells the launcher.
#include "achievements.h"

#include "port_ext.h"

#include <Player/Mario.hpp>
#include <System/FlagManager.hpp>
#include <System/Application.hpp>
#include <System/MarDirector.hpp>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
// host/ach_store.cpp
int sms_ach_store_on(void);
int sms_ach_store_has(int id);
int sms_ach_store_add(int id);
// platform/misc/port_cheats.cpp
extern int port_cheat_god_mode, port_cheat_movement_speed, port_cheat_jump_height, port_cheat_infinite_lives,
	port_cheat_infinite_fludd, port_cheat_coin_multiplier;
// Counts warps (the warp menu) and save state loads (platform/misc/port_cheats.cpp).
extern int port_play_unchecked;
}

namespace ach {

namespace {

unsigned s_visit;
int s_uncheckedAtFile = -1; // port_play_unchecked when the file was chosen
bool s_cheatedThisStage;
int s_debug = -1;

// Stages or the game's code replaced (the world editor's levels, DVD mods, code mods): nothing counts.
bool modded()
{
	static int s_modded = -1;
	if (s_modded < 0) {
		const char* stage = getenv("SMS_MOD");
		const char* code  = getenv("SMS_CODE_MODS");
		s_modded = (stage && *stage && strcmp(stage, "none") != 0 && strcmp(stage, "0") != 0)
		        || (code && *code && strcmp(code, "none") != 0 && strcmp(code, "0") != 0);
		if (s_modded && on())
			log("the stages or the game's code are modded (SMS_MOD, SMS_CODE_MODS): achievements don't unlock");
	}
	return s_modded > 0;
}

// What require() saw, for the tour's summary: each (achievement, thing) once.
struct Need {
	int id;
	char what[48];
	int checks, misses;
};
Need s_needs[256];
int s_needCount;
unsigned s_warnedVisit[256];

// The tour (a test aid, with SMS_ACHIEVEMENTS_DEBUG): SMS_ACHIEVEMENTS_TEST=tour
// warps through every stage and episode where an achievement needs something
// (Episode N is scenario N-1), a second and a half each, and says what was missing.
struct Stop {
	int area, episode;
};
const Stop kTour[] = {
	{ PLAZA, 0 }, { PLAZA, 1 }, { PLAZA, 2 }, { PLAZA, 5 }, { PLAZA, 6 }, { PLAZA, 7 }, { PLAZA, 8 }, { PLAZA, 9 },
	{ LILY_PAD_RIDE, 0 }, { BIANCO, 4 }, { BIANCO, 7 }, { HILLSIDE_CAVE, 0 }, { DIRTY_LAKE_SECRET, 0 }, { RICCO, 4 },
	{ RICCO, 7 }, { RICCO_TOWER_SECRET, 0 }, { GELATO, 0 }, { GELATO, 1 }, { GELATO, 2 }, { GELATO, 3 }, { GELATO, 4 },
	{ GELATO, 5 }, { GELATO, 6 }, { GELATO, 7 }, { SAND_CASTLE_SECRET, 0 }, { SAND_BIRD, 0 }, { PINNA_BEACH, 0 },
	{ PINNA_BEACH, 1 }, { PINNA_BEACH, 2 }, { PINNA_BEACH, 3 }, { PINNA_BEACH, 4 }, { PINNA_BEACH, 5 }, { PINNA_BEACH, 6 },
	{ PINNA_BEACH, 7 }, { PINNA_PARK, 0 }, { PINNA_PARK, 1 }, { PINNA_PARK, 2 }, { PINNA_PARK, 3 }, { PINNA_PARK, 4 },
	{ PINNA_PARK, 5 }, { BEACH_CANNON, 0 }, { YOSHI_GO_ROUND, 0 }, { ROLLER_COASTER, 1 }, { SIRENA_BEACH, 0 },
	{ SIRENA_BEACH, 1 }, { SIRENA_BEACH, 2 }, { SIRENA_BEACH, 3 }, { SIRENA_BEACH, 4 }, { SIRENA_BEACH, 5 },
	{ SIRENA_BEACH, 6 }, { SIRENA_BEACH, 7 }, { CASINO, 0 }, { CASINO, 1 }, { HOTEL_LOBBY_SECRET, 0 },
	{ CASINO_SECRET, 0 }, { KING_BOO_FIGHT, 0 }, { NOKI, 0 }, { NOKI, 1 }, { NOKI, 3 }, { NOKI, 4 }, { NOKI, 5 },
	{ NOKI, 6 }, { NOKI, 7 }, { SHELL_SECRET, 0 }, { PIANTA, 0 }, { PIANTA, 2 }, { PIANTA, 4 }, { VILLAGE_UNDERSIDE, 0 },
	{ CORONA, 0 },
};
const int kTourStops = sizeof kTour / sizeof kTour[0];
bool s_tour;
int s_tourNext = -1, s_tourTicks;

void tourReport()
{
	int checks = 0, misses = 0;
	for (int i = 0; i < s_needCount; ++i) {
		checks += s_needs[i].checks;
		misses += s_needs[i].misses;
	}
	log("tour done: %d stops, %d checks of %d things achievements need, %d missing", kTourStops, checks, s_needCount, misses);
	for (int i = 0; i < s_needCount; ++i)
		if (s_needs[i].misses)
			log("tour: MISSING achievement %d: %s (%d of %d stops)", s_needs[i].id, s_needs[i].what, s_needs[i].misses,
			    s_needs[i].checks);
}

void tourTick()
{
	static unsigned s_warpedAt; // the visit when the last warp was asked for
	if (!s_tour || s_tourNext > kTourStops || !gpMarDirector)
		return;
	// wait for the stage asked for to load, then give it a second and a half
	if (s_tourNext > 0 && s_visit == s_warpedAt)
		return;
	if (++s_tourTicks < 180)
		return;
	s_tourTicks = 0;
	if (s_tourNext > 0) {
		const Stop& was = kTour[s_tourNext - 1];
		if (area() != was.area || episode() != was.episode)
			log("tour: asked for area %#x scenario %d, got %#x %d", was.area, was.episode, area(), episode());
	}
	if (s_tourNext < 0)
		s_tourNext = 0;
	if (s_tourNext == kTourStops) {
		tourReport();
		++s_tourNext;
		return;
	}
	const Stop& to = kTour[s_tourNext++];
	s_warpedAt     = s_visit;
	gpMarDirector->setNextStage((u16)(((to.area + 1) << 8) | to.episode), 0);
}

bool cheating()
{
	return port_cheat_god_mode || port_cheat_movement_speed != 1 || port_cheat_jump_height != 1
	    || port_cheat_infinite_lives || port_cheat_infinite_fludd || port_cheat_coin_multiplier != 1;
}

void frame()
{
	if (on() && cheating())
		s_cheatedThisStage = true;
}

void stageSetup()
{
	++s_visit;
	s_cheatedThisStage = cheating();
	if (debug())
		log("stage %#x scenario %d (visit %u)%s", area(), episode(), s_visit, tainted() ? ", doesn't count" : "");
}

// A test aid (with SMS_ACHIEVEMENTS_DEBUG): SMS_ACHIEVEMENTS_TEST=setflag@N:ID sets
// save flag ID (hex) after N ticks of the first stage, as if the player had.
int s_testTick = -1, s_testFlag, s_ticks;

void afterMovement()
{
	tourTick();
	if (s_testTick < 0 || !playing())
		return;
	if (++s_ticks == s_testTick) {
		log("test: setting flag %#x", s_testFlag);
		TFlagManager::getInstance()->setFlag(s_testFlag, 1);
	}
}

void afterFileSelect()
{
	s_uncheckedAtFile = port_play_unchecked;
}

PortExtension s_extension;

struct Register {
	Register()
	{
		s_extension.name            = "achievements";
		s_extension.frame           = frame;
		s_extension.stageSetup      = stageSetup;
		s_extension.afterFileSelect = afterFileSelect;
		s_extension.afterMovement   = afterMovement;
		port_ext_register(&s_extension);
		const char* test = getenv("SMS_ACHIEVEMENTS_TEST");
		if (test && debug()) {
			s_tour = strcmp(test, "tour") == 0;
			sscanf(test, "setflag@%d:%x", &s_testTick, &s_testFlag);
			int id = 0;
			if (sscanf(test, "banner:%d", &id) == 1)
				banner(id); // shown once a stage is playing, as an unlock's is
		}
	}
} s_register;

} // namespace

bool on()
{
	return sms_ach_store_on() != 0;
}

bool tainted()
{
	// before any file select (a warp straight into a stage, SMS_WARP) nothing counts
	return s_uncheckedAtFile < 0 || port_play_unchecked != s_uncheckedAtFile || s_cheatedThisStage || cheating() || modded();
}

void unlock(int id)
{
	if (id < 1 || id > COUNT || !on() || sms_ach_store_has(id))
		return;
	if (tainted()) {
		if (debug())
			log("%d earned, but this play doesn't count (save state, warp or cheat)", id);
		return;
	}
	if (!sms_ach_store_add(id))
		return;
	banner(id);
}

void require(int id, const char* what, bool found)
{
	if (!on())
		return;
	int i = 0;
	while (i < s_needCount && !(s_needs[i].id == id && strcmp(s_needs[i].what, what) == 0))
		++i;
	if (i == s_needCount && s_needCount < (int)(sizeof s_needs / sizeof s_needs[0])) {
		s_needs[i].id = id;
		snprintf(s_needs[i].what, sizeof s_needs[i].what, "%s", what);
		++s_needCount;
	}
	if (i < s_needCount) {
		++s_needs[i].checks;
		s_needs[i].misses += !found;
	}
	if (!found && !(i < s_needCount && s_warnedVisit[i] == s_visit)) {
		if (i < s_needCount)
			s_warnedVisit[i] = s_visit;
		log("warning: achievement %d can't find %s in area %#x scenario %d, so it can't be earned here", id, what,
		    area(), episode());
	}
}

bool unlocked(int id)
{
	return on() && sms_ach_store_has(id);
}

int area()
{
	return SMSGetApplication()->mCurrArea.getStage();
}

int episode()
{
	return SMSGetApplication()->mCurrArea.getScenario();
}

bool playing()
{
	return gpMarDirector && gpMarioOriginal && area() != TITLE;
}

unsigned visit()
{
	return s_visit;
}

TMario* mario()
{
	return gpMarioOriginal;
}

bool debug()
{
	if (s_debug < 0) {
		const char* v = getenv("SMS_ACHIEVEMENTS_DEBUG");
		s_debug       = v && *v && strcmp(v, "0") != 0;
	}
	return s_debug > 0;
}

void log(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fputs("[achievements] ", stderr);
	vfprintf(stderr, fmt, args);
	fputc('\n', stderr);
	va_end(args);
}

} // namespace ach
