// Achievements (ext/achievements/README.md): the host half keeps what was
// unlocked and tells the launcher. SMS_ACHIEVEMENTS names the file they are
// kept in (the launcher's, one per player, not per save file); unset, they are
// off. Each unlock is a line "<id> <unix time>" added to it, and a line
//   [achievement] {"id":12,"time":1761170000}
// on stdout, which the launcher reads as it does [presence] lines and shows at
// once. A Super Mario Eclipse build has its own story, so they are off there.
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef SMS_HAVE_GAME_BUILD_H
#include "sms_game_build.h"
#endif
#ifndef SMS_GAME_BUILD
#define SMS_GAME_BUILD "unknown"
#endif

namespace {

enum { COUNT = 148 };

std::mutex s_lock;
int s_on = -1;
bool s_have[COUNT + 1];
char s_path[1024];

void load()
{
	const char* path = getenv("SMS_ACHIEVEMENTS");
	s_on             = path && *path && !strstr(SMS_GAME_BUILD, "-eclipse");
	if (!s_on)
		return;
	snprintf(s_path, sizeof s_path, "%s", path);
	FILE* f = fopen(s_path, "r");
	int n   = 0;
	if (f) {
		char line[128];
		while (fgets(line, sizeof line, f)) {
			int id = atoi(line);
			if (id >= 1 && id <= COUNT && !s_have[id]) {
				s_have[id] = true;
				++n;
			}
		}
		fclose(f);
	}
	fprintf(stderr, "[achievements] on: %d of %d unlocked (%s)\n", n, COUNT, s_path);
}

} // namespace

extern "C" int sms_ach_store_on(void)
{
	std::lock_guard<std::mutex> l(s_lock);
	if (s_on < 0)
		load();
	return s_on;
}

extern "C" int sms_ach_store_has(int id)
{
	if (!sms_ach_store_on() || id < 1 || id > COUNT)
		return 0;
	std::lock_guard<std::mutex> l(s_lock);
	return s_have[id];
}

// 1 when `id` is newly unlocked (kept and announced), 0 when it was already.
extern "C" int sms_ach_store_add(int id)
{
	if (!sms_ach_store_on() || id < 1 || id > COUNT)
		return 0;
	std::lock_guard<std::mutex> l(s_lock);
	if (s_have[id])
		return 0;
	s_have[id]     = true;
	const long now = (long)time(nullptr);
	if (FILE* f = fopen(s_path, "a")) {
		fprintf(f, "%d %ld\n", id, now);
		fclose(f);
	} else {
		fprintf(stderr, "[achievements] couldn't write %s: %d is unlocked for this session only\n", s_path, id);
	}
	printf("[achievement] {\"id\":%d,\"time\":%ld}\n", id, now);
	fflush(stdout);
	return 1;
}
