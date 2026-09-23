// Internal interfaces between the platform modules.
#ifndef SMS_PORT_OS_H
#define SMS_PORT_OS_H
#include <dolphin/types.h>
#ifdef __cplusplus
#include <functional>
extern "C" {
#endif
extern u8* port_mem1_base;
extern u32 port_mem1_size;
void port_os_init(void);
void port_dvd_init(void);
void port_vi_init(void);
/* Interrupt emulation: the running game thread owns the "CPU"; interrupt
 * work runs at check points (OS calls, blocking, idle). */
void port_irq_check(void);          /* deliver pending interrupts if enabled */
void port_irq_kick(void);           /* wake an idle CPU (any host thread)    */
typedef void (*port_irq_poll_fn)(void);
void port_irq_add_source(port_irq_poll_fn fn); /* polled at each delivery   */
s64 port_time_ticks(void);
#ifdef __cplusplus
}
/* Queue work to run in interrupt context at the next check point. Caller must
 * be a game thread (holds the CPU). */
void port_irq_defer(std::function<void()> fn);
#endif
#endif
