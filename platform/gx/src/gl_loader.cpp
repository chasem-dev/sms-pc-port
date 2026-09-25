#include "gl_funcs.h"
#include <cstdint>
#include <cstdlib>
#include "gx_internal.h"

namespace gx { namespace gl {
#define SMS_GX_DEFINE(type, name) type gx_##name = nullptr;
SMS_GX_GL_FUNCS(SMS_GX_DEFINE)
#undef SMS_GX_DEFINE

// SMS_GX_STATS counts the GL calls the renderer makes: each entry point is
// wrapped in a trampoline that bumps g_statGlCalls (not on 32-bit Windows,
// whose GL entry points are __stdcall).
uint64_t g_statGlCalls = 0;
#if !(defined(_WIN32) && !defined(_WIN64))
template <typename F> struct Counted;
template <typename R, typename... A> struct Counted<R (*)(A...)> {
    template <R (**Real)(A...)> static R call(A... a) {
        g_statGlCalls++;
        return (*Real)(a...);
    }
};
#endif

bool load(void* (*getProc)(const char*)) {
    bool ok = true;
#define SMS_GX_LOAD(type, name)                                   \
    gx_##name = reinterpret_cast<type>(getProc(#name));           \
    if (!gx_##name) { gx::logmsg("GL entry point missing: %s", #name); ok = false; }
    SMS_GX_GL_FUNCS(SMS_GX_LOAD)
#undef SMS_GX_LOAD
#if !(defined(_WIN32) && !defined(_WIN64))
    if (getenv("SMS_GX_STATS")) {
#define SMS_GX_WRAP(type, name)                                   \
        {                                                         \
            static type real_##name;                              \
            real_##name = gx_##name;                              \
            gx_##name = &Counted<type>::template call<&real_##name>; \
        }
        SMS_GX_GL_FUNCS(SMS_GX_WRAP)
#undef SMS_GX_WRAP
    }
#endif
    return ok;
}
}}  // namespace gx::gl
