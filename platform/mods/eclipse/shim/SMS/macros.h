// Port shim over SunshineHeaderInterface's SMS/macros.h: PowerPC inline
// assembly cannot run natively, so assembly blocks compile to nothing and
// assembly-only functions become empty stubs; the patches that point at them
// are translated one by one on the port side.
#pragma once

#include_next <SMS/macros.h>

#undef SMS_ASM_FUNC
#undef SMS_ASM_BLOCK
#undef SMS_FROM_GPR
#undef SMS_TO_GPR
#undef SMS_FROM_FPR
#undef SMS_TO_FPR
#define SMS_ASM_FUNC
#define SMS_ASM_BLOCK(...)       ((void)0)
#define SMS_FROM_GPR(reg, var)   ((void)(var))
#define SMS_TO_GPR(reg, var)     ((void)(var))
#define SMS_FROM_FPR(reg, var)   ((void)(var))
#define SMS_TO_FPR(reg, var)     ((void)(var))
