#ifdef _WIN32
#include "crash.h"
#include "crash_line.h"
#include <windows.h>
#include <stdio.h>
#include <io.h>

namespace {
HANDLE error_output = INVALID_HANDLE_VALUE;
volatile LONG reporting;
// Drivers and system DLLs may raise and handle exceptions of their own; cap
// the reports so they cannot flood the log before a real crash. C++ throws
// have their own budget and never use up the one for faults.
volatile LONG fault_reports = 16;
volatile LONG throw_reports = 4;

void write_line(PortCrashLine& line)
{
	DWORD written;
	WriteFile(error_output, line.bytes, (DWORD)line.length, &written, NULL);
	line.length = 0;
}

// The module holding `address` (base and file name without its folder). The
// previous answer is kept: a backtrace stays in a few modules, and each new
// one asks the loader.
bool find_module(uintptr_t address, uintptr_t* base, const char** name)
{
	static uintptr_t cached_base;
	static char cached_name[MAX_PATH];
	MEMORY_BASIC_INFORMATION memory;
	if (!VirtualQuery((void*)address, &memory, sizeof memory) || memory.State != MEM_COMMIT)
		return false;
	uintptr_t found = (uintptr_t)memory.AllocationBase;
	if (found != cached_base) {
		cached_name[0] = 0;
		GetModuleFileNameA((HMODULE)found, cached_name, sizeof cached_name);
		cached_name[sizeof cached_name - 1] = 0;
		cached_base = found;
	}
	const char* file = cached_name;
	for (const char* c = cached_name; *c; c++)
		if (*c == '\\' || *c == '/') file = c + 1;
	*base = found;
	*name = *file ? file : "<unknown>";
	return true;
}

void report_frame(int index, uintptr_t pc)
{
	PortCrashLine line;
	line.text("[port]   #"); line.number(index, 10);
	line.text(" "); line.hex(pc);
	uintptr_t base;
	const char* name;
	if (find_module(pc, &base, &name)) {
		line.text(" "); line.text(name);
		line.text("+"); line.hex(pc - base);
	}
	line.text("\n");
	write_line(line);
}

void report_stop(const char* why)
{
	PortCrashLine line;
	line.text("[port]   backtrace stops: "); line.text(why); line.text("\n");
	write_line(line);
}

// Walk the faulting thread's stack within the TEB's bounds, which follow the
// switch onto a game thread's low stack. Never guess past a frame Windows
// cannot unwind: where the walk stops is part of the report.
void report_backtrace(const CONTEXT* start)
{
	enum { kMaxFrames = 48 };
	NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
	const uintptr_t low = (uintptr_t)tib->StackLimit, high = (uintptr_t)tib->StackBase;
	PortCrashLine line;
	line.text("[port] backtrace (stack "); line.hex(low); line.text("-"); line.hex(high); line.text("):\n");
	write_line(line);
#ifdef _WIN64
	// Frame pointers do not chain on Windows x64 (rbp points into the frame),
	// so unwind with the modules' own unwind data, one frame at a time.
	CONTEXT context = *start;
	for (int frame = 0; frame < kMaxFrames; frame++) {
		report_frame(frame, context.Rip);
		const DWORD64 sp = context.Rsp;
		DWORD64 image;
		PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image, NULL);
		if (function) {
			void* handler_data;
			DWORD64 establisher;
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler_data, &establisher, NULL);
		} else if (frame == 0 && sp >= low && sp + 8 <= high) {
			// A leaf function, or a call to a bad address: the return address
			// is on top of the stack.
			context.Rip = *(DWORD64*)sp;
			context.Rsp = sp + 8;
		} else {
			// port_win64_stack_call's entry thunk has no unwind data; it sits
			// just below the top of a game thread's low stack.
			report_stop(high - sp <= 64 ? "entry to the game thread's low stack"
			                            : "no unwind data for this address");
			return;
		}
		if (!context.Rip)
			return; // the thread's first frame
		if (context.Rsp <= sp || context.Rsp < low || context.Rsp >= high) {
			report_stop("the unwound stack pointer leaves the stack");
			return;
		}
	}
#else
	// 32-bit: every function the port builds keeps the ebp frame chain.
	report_frame(0, start->Eip);
	uintptr_t frame_pointer = start->Ebp;
	for (int frame = 1; frame < kMaxFrames; frame++) {
		if (frame_pointer < low || frame_pointer + 8 > high || (frame_pointer & 3)) {
			report_stop("the frame pointer leaves the stack");
			return;
		}
		const uintptr_t* saved = (const uintptr_t*)frame_pointer;
		if (!saved[1])
			return;
		report_frame(frame, saved[1]);
		if (saved[0] <= frame_pointer) {
			if (saved[0])
				report_stop("the frame chain does not go up the stack");
			return;
		}
		frame_pointer = saved[0];
	}
#endif
	report_stop("frame limit");
}

LONG CALLBACK report_exception(EXCEPTION_POINTERS* exception)
{
	const EXCEPTION_RECORD* record = exception->ExceptionRecord;
	const DWORD code = record->ExceptionCode;
	volatile LONG* budget;
	if (code == 0xE06D7363 || code == 0x20474343) // MSVC and GCC/libunwind C++ throws
		budget = &throw_reports;
	else if ((code & 0xC0000000) == 0xC0000000)
		// Error severity: every hardware fault and fatal status, including
		// unwinding failures (0xC00000FF STATUS_BAD_FUNCTION_TABLE, 0xC0000028
		// STATUS_BAD_STACK). Lower severities are breakpoints, guard pages
		// (stack growth), debug output and thread names.
		budget = &fault_reports;
	else
		return EXCEPTION_CONTINUE_SEARCH;
	// MinGW can lazily allocate emulated thread_local storage. Use a process
	// guard instead so first-time crash reporting cannot touch a damaged heap.
	if (InterlockedCompareExchange(&reporting, 1, 0)) return EXCEPTION_CONTINUE_SEARCH;
	if (InterlockedDecrement(budget) < 0) {
		InterlockedExchange(&reporting, 0);
		return EXCEPTION_CONTINUE_SEARCH;
	}
	PortCrashLine line;
	line.text("[port] Windows exception "); line.hex(code);
	line.text(" at address "); line.hex((uintptr_t)record->ExceptionAddress);
	line.text(" (first chance)\n");
	write_line(line);
	line.text("[port] exception thread "); line.number(GetCurrentThreadId(), 10);
#ifdef _WIN64
	line.text(" pc "); line.hex(exception->ContextRecord->Rip);
	line.text(" sp "); line.hex(exception->ContextRecord->Rsp);
	line.text(" bp "); line.hex(exception->ContextRecord->Rbp);
#else
	line.text(" pc "); line.hex(exception->ContextRecord->Eip);
	line.text(" sp "); line.hex(exception->ContextRecord->Esp);
	line.text(" bp "); line.hex(exception->ContextRecord->Ebp);
#endif
	line.text("\n");
	write_line(line);

	// Report the first lines before asking the loader for module information.
	uintptr_t base;
	const char* name;
	if (find_module((uintptr_t)record->ExceptionAddress, &base, &name)) {
		line.text("[port] exception module "); line.text(name);
		line.text(" base "); line.hex(base);
		line.text(" offset "); line.hex((uintptr_t)record->ExceptionAddress - base);
		line.text("\n");
		write_line(line);
	}
	if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
		line.text("[port] access "); line.number(record->ExceptionInformation[0], 10);
		line.text(" target "); line.hex(record->ExceptionInformation[1]);
		line.text("\n");
		write_line(line);
	} else if (code == EXCEPTION_IN_PAGE_ERROR && record->NumberParameters >= 3) {
		line.text("[port] I/O status "); line.hex(record->ExceptionInformation[2]);
		line.text("\n");
		write_line(line);
	} else if (record->NumberParameters) {
		line.text("[port] exception parameters");
		for (DWORD i = 0; i < record->NumberParameters && i < 4; i++) {
			line.text(" "); line.hex(record->ExceptionInformation[i]);
		}
		line.text("\n");
		write_line(line);
	}
	// The walk itself needs stack (a CONTEXT is over 1 KiB); an overflowed
	// stack has none to give.
	if (code != EXCEPTION_STACK_OVERFLOW)
		report_backtrace(exception->ContextRecord);
	InterlockedExchange(&reporting, 0);
	// This is a first-chance diagnostic, not proof the exception is unhandled.
	// Let Windows retain its normal handling and original process exit status.
	// __fastfail/RaiseFailFastException may bypass this logger entirely.
	return EXCEPTION_CONTINUE_SEARCH;
}
}

void port_install_windows_exception_logger()
{
	static bool installed;
	if (installed) return;
	error_output = (HANDLE)_get_osfhandle(_fileno(stderr));
	if (error_output == INVALID_HANDLE_VALUE) error_output = GetStdHandle(STD_ERROR_HANDLE);
	installed = AddVectoredExceptionHandler(1, report_exception) != NULL;
	if (!installed) fprintf(stderr, "[port] cannot install Windows exception logger: %lu\n", GetLastError());
}
#endif
