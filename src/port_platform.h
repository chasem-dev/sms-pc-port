#ifndef SMS_PORT_PLATFORM_H
#define SMS_PORT_PLATFORM_H
#ifdef __cplusplus
extern "C" {
#endif
void port_init(int argc, char** argv);
void port_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
extern const char* port_disc_root;
#ifdef __cplusplus
}
#endif
#endif
