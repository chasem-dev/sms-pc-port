#ifndef SMS_PORT_PLATFORM_H
#define SMS_PORT_PLATFORM_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void port_init(int argc, char** argv);
void port_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
extern const char* port_disc_root;
// 1 when the command line or SMS_DISC_ROOT chose the game source.
extern int port_disc_explicit;
// A whole disc file (malloc'd, caller frees), or NULL. After port_dvd_init.
void* port_dvd_read_file(const char* path, size_t* size);
// Gives the window the game's memory-card icon (platform/misc/window_icon.cpp).
void port_window_icon_init(void);
// Memory the game can hold as a 32-bit address (below 2 GiB where the host
// allows it): 64-bit builds put every stack game code runs on here, since
// the game keeps pointers in u32 slots. Returns NULL on failure.
void* port_low_alloc(unsigned long size);
#ifdef __cplusplus
}
#endif
#endif
