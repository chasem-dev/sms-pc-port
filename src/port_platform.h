#ifndef SMS_PORT_PLATFORM_H
#define SMS_PORT_PLATFORM_H
#ifdef __cplusplus
extern "C" {
#endif
void port_init(int argc, char** argv);
void port_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
extern const char* port_disc_root;
// 1 when the command line or SMS_DISC_ROOT chose the game source.
extern int port_disc_explicit;
#ifdef __cplusplus
}
#endif
#endif
