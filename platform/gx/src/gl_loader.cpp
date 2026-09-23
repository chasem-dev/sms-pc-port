#include "gl_funcs.h"
#include "gx_internal.h"

namespace gx { namespace gl {
#define SMS_GX_DEFINE(type, name) type gx_##name = nullptr;
SMS_GX_GL_FUNCS(SMS_GX_DEFINE)
#undef SMS_GX_DEFINE

bool load(void* (*getProc)(const char*)) {
    bool ok = true;
#define SMS_GX_LOAD(type, name)                                   \
    gx_##name = reinterpret_cast<type>(getProc(#name));           \
    if (!gx_##name) { gx::logmsg("GL entry point missing: %s", #name); ok = false; }
    SMS_GX_GL_FUNCS(SMS_GX_LOAD)
#undef SMS_GX_LOAD
    return ok;
}
}}  // namespace gx::gl
