#ifdef _WIN32
#include "port_host.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <io.h>
#include <direct.h>
#include <mutex>
#include <climits>
#include <windows.h>

static std::mutex s_file_position_mutex;

ssize_t port_pread(int fd, void* buffer, size_t count, unsigned long long offset)
{
	std::lock_guard<std::mutex> lock(s_file_position_mutex);
	if (_lseeki64(fd, offset, SEEK_SET) < 0)
		return -1;
	return _read(fd, buffer, (unsigned int)std::min(count, (size_t)INT_MAX));
}

int port_setenv(const char* name, const char* value, int overwrite)
{
	if (!overwrite && getenv(name))
		return 0;
	return _putenv_s(name, value);
}

int port_mkdir(const char* path, int)
{
	return _mkdir(path);
}

// port_sleep_until: by default, Sleep() of the time left rounded up to whole
// milliseconds. That wakes on the system timer's tick, 0-2.5 ms past the
// deadline, so at 120 fps the retraces (platform/vi), and the frames with them,
// come 6-11 ms apart around their 8.3 ms.
//
// Precise frame timing (precise_timer = on, SMS_PRECISE_TIMER=1, or the settings
// menu) sleeps to a little before the deadline and spins the rest, which costs
// a little CPU (about 3.6% of a core at 120 fps) for frames within a fraction
// of a millisecond of their time (but not while fast-forwarding: platform/vi
// calls port_sleep_until_coarse then):
//  - on a high-resolution waitable timer (Windows 10 1803 and later), which
//    wakes within about half a millisecond of when it is set for;
//  - else (older Windows, Wine without it) with Sleep() for the whole
//    milliseconds that fit, at 1 ms timer resolution (timeBeginPeriod, which
//    SDL asks for too).
// Both are looked up when first needed, so nothing is needed at load time.
// Each thread has its own timer: a timer set again by another thread would
// move this one's wake-up.
namespace {

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// How early to wake and spin the rest, from waking at 119.88 Hz on a Ryzen 9800X3D (Windows 11):
// the high-resolution timer wakes up to 0.5 ms late (3.6% of a core spinning, 1 wake in 10
// still 0.3 ms late); Sleep() wakes up to a tick late (10% of a core, 1 in 40 0.3 ms late).
const long long kTimerEarlyNs = 500000;
const long long kSleepEarlyNs = 1000000;

typedef HANDLE(WINAPI* CreateWaitableTimerExWFn)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
typedef UINT(WINAPI* TimeBeginPeriodFn)(UINT);

long long ns_until(const struct timespec* deadline)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (long long)(deadline->tv_sec - now.tv_sec) * 1000000000LL + (deadline->tv_nsec - now.tv_nsec);
}

// Read by the timer threads at every sleep, written by the settings menu.
std::atomic<int> s_precise(0);

thread_local bool t_timer_tried;
thread_local HANDLE t_timer; // this thread's high-resolution timer, or null

// Sleeps about `ns` on the high-resolution timer; false where there is none.
bool timer_sleep(long long ns)
{
	if (!t_timer_tried) {
		t_timer_tried = true;
		CreateWaitableTimerExWFn create = (CreateWaitableTimerExWFn)(void*)GetProcAddress(
		    GetModuleHandleW(L"kernel32.dll"), "CreateWaitableTimerExW");
		if (create)
			t_timer = create(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	}
	if (!t_timer)
		return false;
	LARGE_INTEGER due;
	due.QuadPart = -(ns / 100); // relative, in 100 ns
	if (!SetWaitableTimer(t_timer, &due, 0, NULL, NULL, FALSE) || WaitForSingleObject(t_timer, INFINITE) != WAIT_OBJECT_0) {
		CloseHandle(t_timer); // not working here: Sleep() from now on
		t_timer = NULL;
		return false;
	}
	return true;
}

void ask_1ms_resolution()
{
	static const bool asked = [] {
		if (HMODULE winmm = LoadLibraryW(L"winmm.dll"))
			if (TimeBeginPeriodFn begin = (TimeBeginPeriodFn)(void*)GetProcAddress(winmm, "timeBeginPeriod"))
				begin(1);
		return true;
	}();
	(void)asked;
}

} // namespace

int port_precise_timer(void) { return s_precise.load(); }
void port_set_precise_timer(int on) { s_precise.store(on != 0); }

void port_sleep_until_coarse(const struct timespec* deadline)
{
	const long long ns = ns_until(deadline);
	if (ns > 0)
		Sleep((DWORD)((ns + 999999LL) / 1000000LL));
}

void port_sleep_until(const struct timespec* deadline)
{
	if (!s_precise.load()) {
		port_sleep_until_coarse(deadline);
		return;
	}
	const long long ns = ns_until(deadline);
	if (ns <= 0)
		return;
	if (ns <= kTimerEarlyNs || !timer_sleep(ns - kTimerEarlyNs)) {
		const long long left = ns_until(deadline);
		if (left > kSleepEarlyNs) {
			ask_1ms_resolution();
			Sleep((DWORD)((left - kSleepEarlyNs) / 1000000LL));
		}
	}
	while (ns_until(deadline) > 0)
		YieldProcessor();
}

// For a thread that wakes on a deadline (the retrace timer): at normal priority,
// with every core busy (a slower CPU compiling shaders), its wake-up waited for
// a core for up to 30 ms; at THREAD_PRIORITY_HIGHEST, at most 0.6 ms. It runs
// for microseconds per wake-up, so it takes nothing from the game's threads.
void port_timer_thread_priority(void)
{
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
}
#endif
