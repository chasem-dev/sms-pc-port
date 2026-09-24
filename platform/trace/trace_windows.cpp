#ifdef _WIN32
#include "trace.h"
extern "C" volatile uint32_t port_trace_anchor = 0x54524143;
extern "C" void port_trace_on_retrace(uint32_t) {}
extern "C" int port_trace_pad_read(PADStatus*) { return 0; }
extern "C" int port_trace_movie_active(void) { return 0; }
#endif
