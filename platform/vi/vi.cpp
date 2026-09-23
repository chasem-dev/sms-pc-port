// VI: a host timer raises the vertical-retrace interrupt at 59.94 Hz. Frame
// buffers are recorded for the GX/present layer; nothing is scanned out here.
#include "port_compat.h"
#include "port_os.h"
#include "port_platform.h"
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <pthread.h>
#include <atomic>
#include <algorithm>
#include <time.h>
#include <vector>
#include <string>
#include <sys/stat.h>

// Frame capture (SMS_SHOTS=field,field,... SMS_SHOT_DIR=dir): the XFB handed to
// VISetNextFrameBuffer is read back through the GX layer at the first VIFlush
// on or after each listed retrace ("field") count, and written as
// <dir>/field<NNNNN>.ppm (tools/shots.py converts and compares). Fields match
// the retail capture numbering (VI retraces since boot).
extern "C" __attribute__((weak)) int GXPC_ReadXFB(const void* xfb, void* rgba, int* w, int* h);
extern "C" __attribute__((weak)) u32 GXPC_FrameCount(void);
extern "C" void port_pad_autopress_field(u32 field);

namespace {
std::atomic<u32> g_pending(0);
u32 g_retrace_count;
OSThreadQueue g_retrace_queue;
VIRetraceCallback g_pre, g_post;
void* g_next_fb;
void* g_cur_fb;
bool g_black = true;
std::vector<u32> g_shots;
size_t g_next_shot;
std::string g_shot_dir;
pthread_t g_gx_thread;

// The clock for SMS_SHOTS / SMS_AUTOPRESS "fields". By default it is game
// time: two fields per display copy (the game renders at 30 Hz), so captures
// line up with retail even when host rendering runs slower than real time.
// SMS_FIELD_CLOCK=retrace uses raw VI retraces (wall-clock) instead.
bool g_clock_retrace;
u32 game_field()
{
	if (g_clock_retrace || !GXPC_FrameCount)
		return g_retrace_count;
	return GXPC_FrameCount() * 2;
}

void shots_init()
{
	if (const char* c = getenv("SMS_FIELD_CLOCK"))
		g_clock_retrace = strcmp(c, "retrace") == 0;
	g_gx_thread = pthread_self();
	const char* e = getenv("SMS_SHOTS");
	if (!e || !*e)
		return;
	for (const char* p = e; *p;) {
		g_shots.push_back((u32)strtoul(p, (char**)&p, 10));
		while (*p == ',' || *p == ' ')
			p++;
	}
	std::sort(g_shots.begin(), g_shots.end());
	const char* d = getenv("SMS_SHOT_DIR");
	g_shot_dir    = d ? d : "shots";
	mkdir(g_shot_dir.c_str(), 0777);
}

void shots_poll(u32 field, void* xfb)
{
	if (g_next_shot >= g_shots.size() || field < g_shots[g_next_shot] || !xfb || !GXPC_ReadXFB)
		return;
	if (!pthread_equal(pthread_self(), g_gx_thread))
		return; // GL calls only from the GX thread
	int w = 0, h = 0;
	if (!GXPC_ReadXFB(xfb, NULL, &w, &h) || w <= 0 || h <= 0)
		return;
	std::vector<u8> px((size_t)w * h * 4);
	GXPC_ReadXFB(xfb, px.data(), &w, &h);
	char path[1024];
	snprintf(path, sizeof path, "%s/field%05u.ppm", g_shot_dir.c_str(), g_shots[g_next_shot]);
	FILE* f = fopen(path, "wb");
	if (f) {
		fprintf(f, "P6\n%d %d\n255\n", w, h);
		for (int i = 0; i < w * h; i++)
			fwrite(&px[(size_t)i * 4], 1, 3, f);
		fclose(f);
		port_log("[vi] captured field %u (game field %u, retrace %u) -> %s\n", g_shots[g_next_shot], field,
		         g_retrace_count, path);
	}
	while (g_next_shot < g_shots.size() && g_shots[g_next_shot] <= field)
		g_next_shot++;
}

void* timer_thread(void*)
{
	const long period_ns = 16683350; // 1001/60 Hz
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	for (;;) {
		t.tv_nsec += period_ns;
		while (t.tv_nsec >= 1000000000) {
			t.tv_nsec -= 1000000000;
			t.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
		g_pending.fetch_add(1);
		port_irq_kick();
	}
	return NULL;
}

void retrace_irq()
{
	u32 n = g_pending.exchange(0);
	if (n == 0)
		return;
	if (n > 4)
		n = 1; // the game fell behind; do not replay a burst of retraces
	while (n--) {
		g_retrace_count++;
		port_pad_autopress_field(game_field());
		if (g_pre)
			g_pre(g_retrace_count);
		if (g_next_fb) {
			g_cur_fb = g_next_fb;
		}
		OSWakeupThread(&g_retrace_queue);
		if (g_post)
			g_post(g_retrace_count);
	}
}
} // namespace

extern "C" void port_vi_init(void)
{
	OSInitThreadQueue(&g_retrace_queue);
	shots_init();
	port_irq_add_source(retrace_irq);
	pthread_t th;
	pthread_create(&th, NULL, timer_thread, NULL);
	pthread_detach(th);
}

extern "C" void VIInit(void) {}
extern "C" void VIConfigure(GXRenderModeObj* rm) {}
extern "C" void VIConfigurePan(u16, u16, u16, u16) {}
extern "C" void VIFlush(void) { shots_poll(game_field(), g_next_fb); }
extern "C" void VISetNextFrameBuffer(void* fb) { g_next_fb = fb; }
extern "C" void VISetNextRightFrameBuffer(void*) {}
extern "C" void VISetBlack(BOOL black) { g_black = black != 0; }
extern "C" void VISet3D(BOOL) {}
extern "C" u32 VIGetRetraceCount(void)
{
	port_irq_check();
	return g_retrace_count;
}
extern "C" u32 VIGetNextField(void) { return 1; }
extern "C" u32 VIGetCurrentLine(void) { return 0; }
extern "C" u32 VIGetTvFormat(void) { return 0; } // VI_NTSC
extern "C" u32 VIGetDTVStatus(void) { return 0; }
extern "C" VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb)
{
	VIRetraceCallback old = g_pre;
	g_pre                 = cb;
	return old;
}
extern "C" VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb)
{
	VIRetraceCallback old = g_post;
	g_post                = cb;
	return old;
}
extern "C" void VIWaitForRetrace(void)
{
	BOOL lvl = OSDisableInterrupts();
	u32 cnt  = g_retrace_count;
	while (cnt == g_retrace_count)
		OSSleepThread(&g_retrace_queue);
	OSRestoreInterrupts(lvl);
}
extern "C" void* port_vi_current_fb(void) { return g_cur_fb; }
