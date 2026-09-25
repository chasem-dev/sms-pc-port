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
#include <signal.h>
#include <execinfo.h>
#include "port_host.h"
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/wait.h>
#ifdef __APPLE__
#include <sys/ucontext.h>
#endif
#endif

// Game source named by a bare argument or SMS_DISC_ROOT (a disc image or an
// extracted files/ folder); SMS_DISC_IMAGE overrides it. With none of them,
// the image bundled into the executable (tools/bundle_disc.py) is used, else
// on Windows the working directory as an extracted folder.
const char* port_disc_root =
#ifdef _WIN32
    ".";
#else
    NULL;
#endif
// Set when the command line or SMS_DISC_ROOT named the game source; otherwise
// a disc image bundled into the executable wins over the default above.
int port_disc_explicit = 0;
// SMS_SKIP_MOVIES=1 reports every THP movie as finished at once (patch 0016).
extern "C" int port_skip_movies;
int port_skip_movies = 0;

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

// Symbolise the backtrace with addr2line so a crash report names functions
// and source lines without the exact binary at hand (not async-signal-safe;
// acceptable on the way down).
#ifndef _WIN32
static void crash_symbolise(void** bt, int n)
{
	char exe[512];
	ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (len <= 0)
		return;
	exe[len] = 0;
	static char addrs[64][24];
	char* argv[64 + 8];
	int argc = 0;
	argv[argc++] = (char*)"addr2line";
	argv[argc++] = (char*)"-f";
	argv[argc++] = (char*)"-C";
	argv[argc++] = (char*)"-p";
	argv[argc++] = (char*)"-e";
	argv[argc++] = exe;
	for (int i = 0; i < n && argc < 64 + 7; i++) {
		Dl_info info;
		if (!dladdr(bt[i], &info) || !info.dli_fname || !info.dli_fbase)
			continue;
		char self[512];
		if (!realpath(info.dli_fname, self) || strcmp(self, exe) != 0)
			continue;
		// return addresses point after the call; step back into it
		snprintf(addrs[i], sizeof addrs[i], "0x%lx",
		         (unsigned long)((char*)bt[i] - (char*)info.dli_fbase - 1));
		argv[argc++] = addrs[i];
	}
	argv[argc] = nullptr;
	if (argc == 6)
		return;
	port_log("[port] backtrace:\n");
	pid_t pid = fork();
	if (pid == 0) {
		dup2(2, 1);
		execvp("addr2line", argv);
		_exit(127);
	}
	if (pid > 0)
		waitpid(pid, nullptr, 0);
}
#endif

#ifdef _WIN32
static void crash_handler(int sig)
{
	port_log("\n[port] fatal signal %d\n", sig);
	void* bt[64];
	int n = backtrace(bt, 64);
	backtrace_symbols_fd(bt, n, 2);
	port_stub_report();
	signal(sig, SIG_DFL);
	raise(sig);
}
#else
static void crash_handler(int sig, siginfo_t* si, void* uc)
{
	port_log("\n[port] fatal signal %d (%s) at address %p\n", sig, strsignal(sig), si ? si->si_addr : nullptr);
#if defined(__APPLE__) && defined(__x86_64__)
	if (uc) {
		// No gdb on macOS and lldb needs developer-mode approval: print the
		// faulting registers so a bad pointer can be traced from the log.
		const auto& r = ((ucontext_t*)uc)->uc_mcontext->__ss;
		port_log("[port] rip=%llx rsp=%llx rbp=%llx\n"
		         "[port] rax=%llx rbx=%llx rcx=%llx rdx=%llx rsi=%llx rdi=%llx\n"
		         "[port] r8=%llx r9=%llx r10=%llx r11=%llx r12=%llx r13=%llx r14=%llx r15=%llx\n",
		         r.__rip, r.__rsp, r.__rbp, r.__rax, r.__rbx, r.__rcx, r.__rdx, r.__rsi, r.__rdi,
		         r.__r8, r.__r9, r.__r10, r.__r11, r.__r12, r.__r13, r.__r14, r.__r15);
	}
#endif
	void* bt[64];
	int n = backtrace(bt, 64);
	backtrace_symbols_fd(bt, n, 2);
	crash_symbolise(bt, n);
	port_stub_report();
#ifdef __APPLE__
	// Under Rosetta, a translated process that dies from a re-raised fatal
	// signal hangs in the kernel's exit path (state UE, unkillable). Exit
	// with the shell's 128+signal status instead.
	_exit(128 + sig);
#else
	signal(sig, SIG_DFL);
	raise(sig);
#endif
}
#endif

// Emulated MEM1: the game's arena lives at the GameCube's own cached
// addresses (0x80000000..), so OSPhysicalToCached/OSCachedToPhysical and the
// few raw low-memory reads (e.g. *(OSModuleInfo**)0x800030C8) work unchanged.
u8* port_mem1_base;
u32 port_mem1_size;

#ifndef _WIN32
// mmap exactly at `want` without replacing an existing mapping. Linux has
// MAP_FIXED_NOREPLACE; Darwin's MAP_FIXED silently replaces whatever is there
// (dylibs, graphics driver memory: the low 4 GiB is shared with the system
// once PAGEZERO is shrunk), so pass `want` as a hint and reject any other
// placement.
static void* map_exact(void* want, size_t size)
{
#ifdef MAP_FIXED_NOREPLACE
	void* p = mmap(want, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
#else
	void* p = mmap(want, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
	if (p == want)
		return p;
	if (p != MAP_FAILED)
		munmap(p, size);
	return MAP_FAILED;
}
#endif

static void map_mem1()
{
	// 64-bit hosts: objects holding pointers are larger, and the fixed-size
	// heaps grow with them (PORT_HEAP64), so give the game more MEM1.
	u32 mb = sizeof(void*) == 8 ? 64 : 24;
	if (const char* e = getenv("SMS_MEM_MB"))
		mb = (u32)atoi(e);
	port_mem1_size = mb << 20;
	void* want = (void*)(uintptr_t)0x80000000u;
#ifdef _WIN32
	void* p = VirtualAlloc(want, port_mem1_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (p != want) {
		port_log("[port] cannot map MEM1 at 0x80000000 (got %p)\n", p);
		if (p)
			VirtualFree(p, 0, MEM_RELEASE);
		exit(1);
	}
#else
	void* p = map_exact(want, port_mem1_size);
	if (p != want) {
		port_log("[port] cannot map MEM1 at 0x80000000; falling back to a heap block\n");
		p = mmap(NULL, port_mem1_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (p == MAP_FAILED) {
			port_log("[port] out of memory for MEM1\n");
			exit(1);
		}
	}
#endif
	port_mem1_base = (u8*)p;
	port_log("[port] MEM1: %u MiB at %p\n", mb, p);
}

// A PTR32 field (4-byte pointer slot in a struct laid over file data) was
// given an address above 4 GiB: something the game can reach was allocated
// high. Fatal, since the pointer would be silently truncated.
extern "C" void port_ptr32_trap(const void* p)
{
	port_log("[port] PTR32: address %p does not fit a 32-bit slot\n", p);
	abort();
}

void* port_low_alloc(unsigned long size)
{
#if UINTPTR_MAX <= 0xFFFFFFFFu
	return malloc(size);
#elif defined(_WIN32)
	// Walk hint addresses from 256 MiB up to 2 GiB (64 KiB allocation grain).
	for (uintptr_t at = 0x10000000u; at + size <= 0x80000000u; at += 0x10000u) {
		void* p = VirtualAlloc((void*)at, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		if (p)
			return p;
	}
	return NULL;
#else
#ifdef MAP_32BIT
	void* p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (p != MAP_FAILED)
		return p;
#endif
	static uintptr_t next = 0x40000000u;
	for (int tries = 0; tries < 4096 && next + size <= 0x80000000u; tries++) {
		void* want = (void*)next;
		next += (size + 0xFFFFu) & ~(uintptr_t)0xFFFFu;
		void* q = map_exact(want, size);
		if (q == want)
			return q;
	}
	return NULL;
#endif
}

// Hardware register window. The only direct access left in game code is the
// GX write-gather pipe (GXWGFifo at 0xCC008000, written by the inline GXVert.h
// vertex/command writers). Until the GX layer redirects those writes, map the
// window as a write sink so they are harmless.
static void map_hw_sink()
{
	void* want = (void*)(uintptr_t)0xCC000000u;
#ifdef _WIN32
	void* p = VirtualAlloc(want, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
	void* p = map_exact(want, 0x10000);
#endif
	if (p != want)
		port_log("[port] cannot map the hardware register sink at 0xCC000000 (%p)\n", p);
	else
		port_log("[port] GX WG pipe 0xCC008000 is a write sink\n");
}

// Window/headless switches belong to the GX layer (weak: absent without it).
extern "C" __attribute__((weak)) int GXPC_ParseArgs(int* argc, char** argv);
extern "C" __attribute__((weak)) void GXPC_SetHeadless(int headless);

// The 32-bit NVIDIA GLX library must match the kernel module's version
// exactly, or context creation fails (X_GLXCreateContext BadValue). If this
// host's i386 NVIDIA userspace does not match, use Mesa (llvmpipe) for the
// window instead, unless the user chose a GLX vendor.
static void pick_glx_vendor()
{
#ifndef _WIN32
	if (sizeof(void*) != 4 || getenv("__GLX_VENDOR_LIBRARY_NAME"))
		return;
	FILE* f = fopen("/proc/driver/nvidia/version", "r");
	if (!f)
		return;
	char line[256] = { 0 };
	fgets(line, sizeof line, f);
	fclose(f);
	const char* k = strstr(line, "Kernel Module");
	char ver[64]  = { 0 };
	if (!k || sscanf(k, "Kernel Module %63s", ver) != 1)
		return;
	char lib[256];
	snprintf(lib, sizeof lib, "/usr/lib/i386-linux-gnu/libGLX_nvidia.so.%s", ver);
	if (access(lib, R_OK) == 0)
		return;
	setenv("__GLX_VENDOR_LIBRARY_NAME", "mesa", 1);
	port_log("[port] no 32-bit NVIDIA GLX for kernel module %s; using Mesa for the window\n", ver);
#endif
}

extern "C" void port_init(int argc, char** argv)
{
	pick_glx_vendor();
	if (const char* m = getenv("SMS_SKIP_MOVIES"))
		port_skip_movies = *m && strcmp(m, "0") != 0;
	for (int i = 1; i < argc; i++)
		if (strcmp(argv[i], "--headless") == 0) {
			port_setenv("SMS_HEADLESS", "1", 1);
			if (GXPC_SetHeadless)
				GXPC_SetHeadless(1);
		}
	if (GXPC_ParseArgs)
		GXPC_ParseArgs(&argc, argv);
	if (const char* d = getenv("SMS_DISC_ROOT")) {
		port_disc_root     = d;
		port_disc_explicit = 1;
	}
	for (int i = 1; i < argc; i++)
		if (argv[i][0] != '-') {
			port_disc_root     = argv[i];
			port_disc_explicit = 1;
		}
#ifdef _WIN32
	for (int sig : {SIGSEGV, SIGFPE, SIGILL, SIGABRT})
		signal(sig, crash_handler);
#else
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = crash_handler;
	sa.sa_flags = SA_SIGINFO;
	for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT})
		sigaction(sig, &sa, nullptr);
#endif
	atexit(port_stub_report);
	map_mem1();
	if (sizeof(void*) == 4)
		map_hw_sink();
	port_os_init();
	port_dvd_init();
	port_window_icon_init();
	port_noaudio_init();
	port_vi_init();
	port_log("[port] platform ready\n");
}
