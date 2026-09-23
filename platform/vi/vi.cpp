// VI: a host timer raises the vertical-retrace interrupt at 59.94 Hz. Frame
// buffers are recorded for the GX/present layer; nothing is scanned out here.
#include "port_compat.h"
#include "port_os.h"
#include "port_platform.h"
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <pthread.h>
#include <atomic>
#include <time.h>

namespace {
std::atomic<u32> g_pending(0);
u32 g_retrace_count;
OSThreadQueue g_retrace_queue;
VIRetraceCallback g_pre, g_post;
void* g_next_fb;
void* g_cur_fb;
bool g_black = true;

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
	port_irq_add_source(retrace_irq);
	pthread_t th;
	pthread_create(&th, NULL, timer_thread, NULL);
	pthread_detach(th);
}

extern "C" void VIInit(void) {}
extern "C" void VIConfigure(GXRenderModeObj* rm) {}
extern "C" void VIConfigurePan(u16, u16, u16, u16) {}
extern "C" void VIFlush(void) {}
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
