// Port runtime: logging, stub accounting, and the boot sequence that stands in
// for the GameCube IPL/__start/OSInit before SMS_main runs.
#include "port_compat.h"
#include "port_platform.h"
#include "port_stub.h"
#include "port_os.h"
#include <dolphin/os.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <execinfo.h>

const char* port_disc_root = "/home/netflix/sms/orig/GMSE01/files";

extern "C" void port_log(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	std::vfprintf(stderr, fmt, ap);
	va_end(ap);
	std::fflush(stderr);
}

static port_stub_rec* s_stub_list;
static unsigned s_stub_distinct;
extern "C" void port_stub_hit(port_stub_rec* rec)
{
	if (rec->count++ == 0) {
		rec->next   = s_stub_list;
		s_stub_list = rec;
		++s_stub_distinct;
		if (!getenv("SMS_QUIET_STUBS"))
			port_log("[stub] %s (first call; %u distinct stubs hit)\n", rec->name, s_stub_distinct);
	}
}

extern "C" void port_stub_report(void)
{
	port_log("[stub] summary: %u distinct SDK stubs were called\n", s_stub_distinct);
	for (port_stub_rec* r = s_stub_list; r; r = r->next)
		port_log("[stub]   %-32s %lu\n", r->name, r->count);
}

static void crash_handler(int sig)
{
	port_log("\n[port] fatal signal %d (%s)\n", sig, strsignal(sig));
	void* bt[64];
	int n = backtrace(bt, 64);
	backtrace_symbols_fd(bt, n, 2);
	port_stub_report();
	signal(sig, SIG_DFL);
	raise(sig);
}

// Emulated MEM1: the game's arena lives at the GameCube's own cached
// addresses (0x80000000..), so OSPhysicalToCached/OSCachedToPhysical and the
// few raw low-memory reads (e.g. *(OSModuleInfo**)0x800030C8) work unchanged.
u8* port_mem1_base;
u32 port_mem1_size;

static void map_mem1()
{
	u32 mb = 24;
	if (const char* e = getenv("SMS_MEM_MB"))
		mb = (u32)atoi(e);
	port_mem1_size = mb << 20;
	void* want = (void*)(uintptr_t)0x80000000u;
	void* p = mmap(want, port_mem1_size, PROT_READ | PROT_WRITE,
	               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (p != want) {
		port_log("[port] cannot map MEM1 at 0x80000000 (got %p); falling back to a heap block\n", p);
		if (p != MAP_FAILED)
			munmap(p, port_mem1_size);
		p = mmap(NULL, port_mem1_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p == MAP_FAILED) {
			port_log("[port] out of memory for MEM1\n");
			exit(1);
		}
	}
	port_mem1_base = (u8*)p;
	port_log("[port] MEM1: %u MiB at %p\n", mb, p);
}

extern "C" void port_init(int argc, char** argv)
{
	if (const char* d = getenv("SMS_DISC_ROOT"))
		port_disc_root = d;
	for (int i = 1; i < argc; i++)
		if (argv[i][0] != '-')
			port_disc_root = argv[i];
	signal(SIGSEGV, crash_handler);
	signal(SIGBUS, crash_handler);
	signal(SIGFPE, crash_handler);
	signal(SIGILL, crash_handler);
	signal(SIGABRT, crash_handler);
	atexit(port_stub_report);
	map_mem1();
	port_os_init();
	port_dvd_init();
	port_vi_init();
	port_log("[port] platform ready (disc root %s)\n", port_disc_root);
}
