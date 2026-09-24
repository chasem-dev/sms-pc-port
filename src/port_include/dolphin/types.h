/* Port override of decomp/include/dolphin/types.h (same include guard, so it
 * wins whichever copy is reached first; port_compat.h includes this one).
 * MWCC's u32/s32 are (unsigned) long; on LP64 hosts that is 64 bits, so the
 * port spells them int there. On ILP32 hosts long is 32 bits and the original
 * spelling (and C++ mangling) is kept. */
#ifndef _DOLPHIN_TYPES_H_
#define _DOLPHIN_TYPES_H_

typedef signed char s8;
typedef unsigned char u8;
typedef signed short int s16;
typedef unsigned short int u16;
#if defined(__LP64__) || defined(_LP64)
typedef signed int s32;
typedef unsigned int u32;
#else
typedef signed long s32;
typedef unsigned long u32;
#endif
typedef signed long long int s64;
typedef unsigned long long int u64;

typedef float f32;
typedef double f64;

typedef char* Ptr;

typedef int BOOL;

#define FALSE 0
#define TRUE  1

/* Hardware-mapped objects (low-memory OS globals, hw_regs.h register blocks)
 * are *defined* in SDK headers. On the host they become ordinary weak
 * globals, so the per-TU definitions merge at link time; the platform layer
 * initialises the ones that matter (bus clock, memory size, TV mode). */
#ifdef _WIN32
/* COFF has no ELF-style weak data. selectany puts header definitions in
 * COMDAT sections so the linker keeps one copy shared by all game units. */
#define AT_ADDRESS(addr) __attribute__((selectany))
#else
#define AT_ADDRESS(addr) __attribute__((weak))
#endif

#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))

#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif

#endif
