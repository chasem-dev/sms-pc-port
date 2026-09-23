/* Force-included (-include) into every decomp translation unit in the port
 * build. It stands in for the parts of MWCC and MSL the game relies on
 * implicitly, so that the game/JSystem sources compile unmodified with the
 * host g++/gcc and the host libc/libstdc++. */
#ifndef SMS_PORT_COMPAT_H
#define SMS_PORT_COMPAT_H

#ifndef TARGET_PC
#define TARGET_PC 1
#endif

/* Host C library first, before any macro below can disturb it. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>
#include <float.h>
#include <wchar.h>
#ifdef __cplusplus
#include <new>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <iterator>
#include <utility>
#endif

#include <dolphin/types.h> /* the port override (see port_include/) */

/* JSUStreamEnum.hpp declares `enum EIoState { GOOD, EOF }`. Nothing in the
 * decomp scope uses stdio's EOF. */
#undef EOF

/* MWCC-only syntax. */
#define __declspec(x)
/* Whole-function `asm` definitions keep their Gekko bodies inside
 * #ifdef __MWERKS__, so dropping the keyword leaves a (C-less) body. The host
 * headers above use __asm__, never bare asm. */
#define asm
#define __sync() ((void)0)
#define __isync() ((void)0)

/* The game's entry point is `void main(void)`. */
#define main SMS_main

/* Gekko intrinsics. The hardware estimates are ~12-bit; these are exact. */
#ifdef __cplusplus
extern "C++" {
static inline u32 __cntlzw(u32 x) { return x ? (u32)__builtin_clz(x) : 32u; }
static inline double __frsqrte(double x) { return 1.0 / ::sqrt(x); }
static inline float __fres(float x) { return 1.0f / x; }
}
#else
static inline u32 __cntlzw(u32 x) { return x ? (u32)__builtin_clz(x) : 32u; }
static inline double __frsqrte(double x) { return 1.0 / sqrt(x); }
static inline float __fres(float x) { return 1.0f / x; }
#endif

/* Non-standard names MSL's math.h provides. MSL spells M_PI as a float. */
#undef M_PI
#define M_PI       3.14159265358979323846f
#define LONG_TAU   6.2831854820251465
#define TAU        6.2831855f
#define HALF_PI    1.5707964f
#define THIRD_PI   1.0471976f
#define QUARTER_PI 0.7853982f
#define SIN_2_5    0.43633234f
#define M_SQRT3    1.73205f
#define DEG_TO_RAD(degrees) (degrees * (M_PI / 180.0f))
#define RAD_TO_DEG(radians) (radians * (180.0f / M_PI))

#ifdef __cplusplus
/* MSL puts the C99 float functions in namespace std; libstdc++ in C++03 mode
 * does not. */
namespace std {
using ::sqrtf; using ::powf; using ::fmodf; using ::fabsf; using ::sinf;
using ::cosf; using ::tanf; using ::atanf; using ::atan2f; using ::acosf;
using ::asinf; using ::expf; using ::logf; using ::floorf; using ::ceilf;
using ::log10f; using ::snprintf; using ::vsnprintf;
}
/* MSL C++ adds float overloads of abs/sin/cos/atan2 at global scope. */
using std::abs;
using std::sin;
using std::cos;
using std::atan2;
using std::sqrt;
using std::fabs;
using std::floor;
using std::fmod;
using std::pow;
#endif

#endif /* SMS_PORT_COMPAT_H */
