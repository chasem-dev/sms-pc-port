// OpenGL 3.3 backend: EFB as an FBO, batched primitive submission, GL state
// derived from the BP/XF registers, EFB copies (display and texture) and XFB
// presentation.
#include "gx_internal.h"
#include "gl_funcs.h"
#include "sms_gx/gx_pc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unordered_map>

namespace gx {

uint32_t g_statTexUploads, g_statShaderCompiles;
static GXPCStats s_stats;
void shaderShutdown();

enum { EFB_W = 640, EFB_H = 528 };

static int s_scale = 1;
static GLuint s_efbFbo, s_efbColor, s_efbDepth;
static GLuint s_vao, s_vbo, s_ibo, s_ubo;
static GLuint s_copyProg, s_copyVao;
static GLint s_copyUMode, s_copyURect, s_copyUAlphaOne;
static GLuint s_tmpFbo;
static bool s_ready = false;
static bool s_xfDirty = true;

static std::vector<HostVertex> s_bverts;
static std::vector<uint32_t> s_bidx;
static PrimClass s_bclass = PRIM_TRIS;

struct Xfb {
    GLuint tex;
    int w, h;
};
static std::unordered_map<const void*, Xfb> s_xfbs;
static const void* s_lastXfb = nullptr;

void markXfMemDirty() { s_xfDirty = true; }
FILE* traceFile();
void traceFrameAdvance();
void traceDraw(int prim, uint32_t nverts, uint32_t nidx, const HostVertex* v, int progId);
void traceCopy(bool disp, int x, int y, int w, int h, const void* dest, uint32_t ctrl);
void traceProbe();
void (*g_displayCopyHook)(const void* xfb) = nullptr;
bool rendererReady() { return s_ready; }

static GLuint compileProgram(const char* vs, const char* fs) {
    auto sh = [](GLenum t, const char* src) {
        GLuint s = glCreateShader(t);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            logmsg("internal shader failed: %s", log);
        }
        return s;
    };
    GLuint v = sh(GL_VERTEX_SHADER, vs), f = sh(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindFragDataLocation(p, 0, "o_color");
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// EFB -> texture conversion.  The output is what sampling the GX texture of
// the copy's format would return, so later draws can use it directly.
static const char* kCopyVs = R"(#version 330 core
uniform vec4 u_rect; // x, y, w, h in EFB texture coordinates (0..1)
out vec2 v_uv;
void main() {
  vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
  v_uv = u_rect.xy + p * u_rect.zw;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";
static const char* kCopyFs = R"(#version 330 core
uniform sampler2D u_color;
uniform sampler2D u_depth;
uniform int u_mode;     // bit 5: depth copy, bit 4: intensity (YUV) conversion, bits 0-3: format
uniform int u_alphaOne; // EFB pixel format has no alpha
in vec2 v_uv;
out vec4 o_color;
float q(float v, float bits) { float m = exp2(bits) - 1.0; return floor(v * m + 0.5) / m; }
void main() {
  int f = u_mode & 15;
  if ((u_mode & 32) != 0) {
    uint z = uint(clamp(texture(u_depth, v_uv).r, 0.0, 1.0) * 16777215.0);
    float hi = float((z >> 16) & 255u) / 255.0, mid = float((z >> 8) & 255u) / 255.0, lo = float(z & 255u) / 255.0;
    if (f == 1) o_color = vec4(hi);                        // Z8
    else if (f == 11 || f == 3) o_color = vec4(mid, mid, mid, hi); // Z16 (IA8 layout)
    else if (f == 6) o_color = vec4(hi, mid, lo, 1.0);     // Z24X8
    else if (f == 0) o_color = vec4(q(hi, 4.0));           // Z4
    else if (f == 9) o_color = vec4(mid);                  // Z8M
    else if (f == 10) o_color = vec4(lo);                  // Z8L
    else if (f == 12) o_color = vec4(lo, lo, lo, mid);     // Z16L
    else o_color = vec4(hi);
    return;
  }
  vec4 c = texture(u_color, v_uv);
  if (u_alphaOne != 0) c.a = 1.0;
  if ((u_mode & 16) != 0) {
    float y = clamp(0.257 * c.r + 0.504 * c.g + 0.098 * c.b + 16.0 / 255.0, 0.0, 1.0);
    if (f == 0) o_color = vec4(q(y, 4.0));
    else if (f == 1) o_color = vec4(y);
    else if (f == 2) o_color = vec4(vec3(q(y, 4.0)), q(c.a, 4.0));
    else if (f == 3) o_color = vec4(vec3(y), c.a);
    else o_color = c;
    return;
  }
  if (f == 0) o_color = vec4(q(c.r, 4.0));                                // R4
  else if (f == 2) o_color = vec4(vec3(q(c.r, 4.0)), q(c.a, 4.0));        // RA4
  else if (f == 3) o_color = vec4(c.rrr, c.a);                            // RA8
  else if (f == 4) o_color = vec4(q(c.r, 5.0), q(c.g, 6.0), q(c.b, 5.0), 1.0); // RGB565
  else if (f == 5) o_color = c.a > 0.99 ? vec4(q(c.r, 5.0), q(c.g, 5.0), q(c.b, 5.0), 1.0)
                                        : vec4(q(c.r, 4.0), q(c.g, 4.0), q(c.b, 4.0), q(c.a, 3.0)); // RGB5A3
  else if (f == 7) o_color = vec4(c.a);                                   // A8
  else if (f == 8) o_color = vec4(c.r);                                   // R8
  else if (f == 9) o_color = vec4(c.g);                                   // G8
  else if (f == 10) o_color = vec4(c.b);                                  // B8
  else if (f == 11) o_color = vec4(c.ggg, c.r);                           // RG8 read as IA8
  else if (f == 12) o_color = vec4(c.bbb, c.g);                           // GB8 read as IA8
  else o_color = c;                                                       // RGBA8 / YUVA8
}
)";

void rendererInit(int efbScale) {
    s_scale = efbScale < 1 ? 1 : efbScale;
    logmsg("OpenGL %s, renderer %s (%s)", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER),
           (const char*)glGetString(GL_VENDOR));
    int W = EFB_W * s_scale, H = EFB_H * s_scale;
    glGenTextures(1, &s_efbColor);
    glBindTexture(GL_TEXTURE_2D, s_efbColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &s_efbDepth);
    glBindTexture(GL_TEXTURE_2D, s_efbDepth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, W, H, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &s_efbFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s_efbFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_efbColor, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s_efbDepth, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) logmsg("EFB framebuffer incomplete");
    glClearColor(0, 0, 0, 1);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glGenFramebuffers(1, &s_tmpFbo);

    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);
    glGenBuffers(1, &s_vbo);
    glGenBuffers(1, &s_ibo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
    const GLsizei st = sizeof(HostVertex);
    auto off = [](size_t o) { return reinterpret_cast<const void*>(o); };
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, off(offsetof(HostVertex, pos)));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, off(offsetof(HostVertex, nrm)));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, st, off(offsetof(HostVertex, bin)));
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, st, off(offsetof(HostVertex, tan)));
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, off(offsetof(HostVertex, clr)));
    glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, off(offsetof(HostVertex, clr) + 4));
    for (int i = 0; i < 8; i++)
        glVertexAttribPointer(GLuint(6 + i), 2, GL_FLOAT, GL_FALSE, st, off(offsetof(HostVertex, tex) + 8 * i));
    glVertexAttribIPointer(14, 3, GL_UNSIGNED_INT, st, off(offsetof(HostVertex, mtx)));
    for (GLuint i = 0; i < 15; i++) glEnableVertexAttribArray(i);

    glGenBuffers(1, &s_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, s_ubo);
    glBufferData(GL_UNIFORM_BUFFER, 184 * 16, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, s_ubo);

    s_copyProg = compileProgram(kCopyVs, kCopyFs);
    glUseProgram(s_copyProg);
    glUniform1i(glGetUniformLocation(s_copyProg, "u_color"), 0);
    glUniform1i(glGetUniformLocation(s_copyProg, "u_depth"), 1);
    s_copyUMode = glGetUniformLocation(s_copyProg, "u_mode");
    s_copyURect = glGetUniformLocation(s_copyProg, "u_rect");
    s_copyUAlphaOne = glGetUniformLocation(s_copyProg, "u_alphaOne");
    glGenVertexArrays(1, &s_copyVao);
    s_ready = true;
}

// ------------------------------------------------------------------ batching
void onStateChange() {
    if (!s_bidx.empty()) flushBatch();
}

void addPrimitive(uint8_t op, const HostVertex* v, uint32_t n) {
    if (!s_ready || n == 0) return;
    PrimClass cls = op >= 0xB8 ? PRIM_POINTS : op >= 0xA8 ? PRIM_LINES : PRIM_TRIS;
    if (!s_bidx.empty() && cls != s_bclass) flushBatch();
    s_bclass = cls;
    uint32_t base = uint32_t(s_bverts.size());
    s_bverts.insert(s_bverts.end(), v, v + n);
    auto tri = [&](uint32_t a, uint32_t b, uint32_t c) {
        s_bidx.push_back(base + a);
        s_bidx.push_back(base + b);
        s_bidx.push_back(base + c);
    };
    switch (op & 0xF8) {
    case 0x80:
    case 0x88:  // quads
        for (uint32_t i = 0; i + 3 < n; i += 4) {
            tri(i, i + 1, i + 2);
            tri(i, i + 2, i + 3);
        }
        break;
    case 0x90:
        for (uint32_t i = 0; i + 2 < n; i += 3) tri(i, i + 1, i + 2);
        break;
    case 0x98:
        for (uint32_t i = 2; i < n; i++) {
            if (i & 1) tri(i - 1, i - 2, i);
            else tri(i - 2, i - 1, i);
        }
        break;
    case 0xA0:
        for (uint32_t i = 2; i < n; i++) tri(0, i - 1, i);
        break;
    case 0xA8:
        for (uint32_t i = 0; i + 1 < n; i += 2) {
            s_bidx.push_back(base + i);
            s_bidx.push_back(base + i + 1);
        }
        break;
    case 0xB0:
        for (uint32_t i = 1; i < n; i++) {
            s_bidx.push_back(base + i - 1);
            s_bidx.push_back(base + i);
        }
        break;
    default:
        for (uint32_t i = 0; i < n; i++) s_bidx.push_back(base + i);
        break;
    }
    s_stats.vertices += n;
}

static inline int sext11(uint32_t v) { return int32_t(v << 21) >> 21; }
static inline float xff(uint32_t r) {
    float f;
    memcpy(&f, &g.xfReg[r], 4);
    return f;
}

static void applyGlState() {
    int W = EFB_W * s_scale, H = EFB_H * s_scale;
    glBindFramebuffer(GL_FRAMEBUFFER, s_efbFbo);
    glViewport(0, 0, W, H);

    // scissor (registers hold coordinates + 342)
    uint32_t tl = g.bp[BP_SCISSOR_TL], br = g.bp[BP_SCISSOR_BR];
    int top = int(tl & 0x7FF) - 342, left = int((tl >> 12) & 0x7FF) - 342;
    int bottom = int(br & 0x7FF) - 342, right = int((br >> 12) & 0x7FF) - 342;
    int sw = right - left + 1, sh = bottom - top + 1;
    if (sw < 0) sw = 0;
    if (sh < 0) sh = 0;
    glEnable(GL_SCISSOR_TEST);
    glScissor(left * s_scale, top * s_scale, sw * s_scale, sh * s_scale);

    // culling: GX front faces are clockwise on screen, which is counter-clockwise
    // in this framebuffer's (y-down) window coordinates
    uint32_t cull = (g.bp[BP_GENMODE] >> 14) & 3;
    glFrontFace(GL_CCW);
    if (cull == 0 || s_bclass != PRIM_TRIS) glDisable(GL_CULL_FACE);
    else {
        glEnable(GL_CULL_FACE);
        glCullFace(cull == 1 ? GL_BACK : cull == 2 ? GL_FRONT : GL_FRONT_AND_BACK);
    }

    static const GLenum cmp[8] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    uint32_t z = g.bp[BP_ZMODE];
    if (z & 1) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(cmp[(z >> 1) & 7]);
        glDepthMask((z >> 4) & 1 ? GL_TRUE : GL_FALSE);
    } else {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }

    uint32_t pix = g.bp[BP_PE_CONTROL] & 7;
    bool hasAlpha = pix == 1;  // GX_PF_RGBA6_Z24
    uint32_t cm = g.bp[BP_CMODE0], cm1 = g.bp[BP_CMODE1];
    bool colorUpd = (cm >> 3) & 1, alphaUpd = ((cm >> 4) & 1) && hasAlpha;
    glColorMask(colorUpd, colorUpd, colorUpd, alphaUpd || ((cm1 >> 8) & 1 && hasAlpha));

    static const GLenum srcF[8] = {GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
                                   GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    static const GLenum dstF[8] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
                                   GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    auto noDstAlpha = [&](GLenum f) {
        if (hasAlpha) return f;
        return f == GL_DST_ALPHA ? GLenum(GL_ONE) : f == GL_ONE_MINUS_DST_ALPHA ? GLenum(GL_ZERO) : f;
    };
    bool dstAlpha = ((cm1 >> 8) & 1) != 0;
    GLenum aSrc = GL_ONE, aDst = GL_ZERO;
    if (dstAlpha) {
        glBlendColor(0, 0, 0, float(cm1 & 0xFF) / 255.0f);
        aSrc = GL_CONSTANT_ALPHA;
        aDst = GL_ZERO;
    }
    glDisable(GL_COLOR_LOGIC_OP);
    if (cm & 1) {
        glEnable(GL_BLEND);
        if ((cm >> 11) & 1) {
            glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, dstAlpha ? GL_FUNC_ADD : GL_FUNC_REVERSE_SUBTRACT);
            glBlendFuncSeparate(GL_ONE, GL_ONE, dstAlpha ? aSrc : GL_ONE, dstAlpha ? aDst : GL_ONE);
        } else {
            GLenum s = noDstAlpha(srcF[(cm >> 8) & 7]), d = noDstAlpha(dstF[(cm >> 5) & 7]);
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
            glBlendFuncSeparate(s, d, dstAlpha ? aSrc : s, dstAlpha ? aDst : d);
        }
    } else if ((cm >> 1) & 1) {
        static const GLenum lop[16] = {GL_CLEAR, GL_AND, GL_AND_REVERSE, GL_COPY, GL_AND_INVERTED, GL_NOOP,
                                       GL_XOR, GL_OR, GL_NOR, GL_EQUIV, GL_INVERT, GL_OR_REVERSE,
                                       GL_COPY_INVERTED, GL_OR_INVERTED, GL_NAND, GL_SET};
        glDisable(GL_BLEND);
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(lop[(cm >> 12) & 15]);
    } else if (dstAlpha) {
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE, GL_ZERO, aSrc, aDst);
    } else {
        glDisable(GL_BLEND);
    }

    if (g.xfReg[0x05] == 0) {
        glEnable(GL_CLIP_DISTANCE0);
        glEnable(GL_CLIP_DISTANCE0 + 1);
    } else {
        glDisable(GL_CLIP_DISTANCE0);
        glDisable(GL_CLIP_DISTANCE0 + 1);
    }
    uint32_t lp = g.bp[BP_LPSIZE];
    float pt = float((lp >> 8) & 0xFF) / 6.0f * float(s_scale);
    glPointSize(pt > 1.0f ? pt : 1.0f);
    glLineWidth(1.0f);
}

static void uploadUniforms(const ShaderProgram* sp, float texW[8], float texH[8]) {
    GLint iv[16];
    for (int i = 0; i < 4; i++) {
        uint32_t ra = g.bp[BP_TEV_REG + 2 * i], bg = g.bp[BP_TEV_REG + 2 * i + 1];
        iv[4 * i + 0] = sext11(ra & 0x7FF);
        iv[4 * i + 1] = sext11((bg >> 12) & 0x7FF);
        iv[4 * i + 2] = sext11(bg & 0x7FF);
        iv[4 * i + 3] = sext11((ra >> 12) & 0x7FF);
    }
    glUniform4iv(sp->uTevReg, 4, iv);
    for (int i = 0; i < 4; i++) {
        uint32_t ra = g.kreg[2 * i], bg = g.kreg[2 * i + 1];
        iv[4 * i + 0] = int(ra & 0xFF);
        iv[4 * i + 1] = int((bg >> 12) & 0xFF);
        iv[4 * i + 2] = int(bg & 0xFF);
        iv[4 * i + 3] = int((ra >> 12) & 0xFF);
    }
    glUniform4iv(sp->uKonst, 4, iv);

    // manual texcoord scaling: coordinates are in units of the given size
    float ts[16];
    for (int c = 0; c < 8; c++) ts[2 * c] = ts[2 * c + 1] = 1.0f;
    uint32_t nst = ((g.bp[BP_GENMODE] >> 10) & 15) + 1;
    for (uint32_t s = 0; s < nst; s++) {
        uint32_t ord = (g.bp[BP_TREF + (s >> 1)] >> ((s & 1) * 12)) & 0x3FF;
        uint32_t map = ord & 7, coord = (ord >> 3) & 7;
        if (g.tcManual[coord] && texW[map] > 0) {
            ts[2 * coord] = float((g.bp[BP_SU_SSIZE + 2 * coord] & 0xFFFF) + 1) / texW[map];
            ts[2 * coord + 1] = float((g.bp[BP_SU_SSIZE + 2 * coord + 1] & 0xFFFF) + 1) / texH[map];
        }
    }
    glUniform2fv(sp->uTexScale, 8, ts);
    float tsz[16];
    for (int m = 0; m < 8; m++) {
        tsz[2 * m] = texW[m] > 0 ? texW[m] : 1.0f;
        tsz[2 * m + 1] = texH[m] > 0 ? texH[m] : 1.0f;
    }
    glUniform2fv(sp->uTexSize, 8, tsz);
    uint32_t ac = g.bp[BP_ALPHACOMPARE];
    GLint ar[2] = {GLint(ac & 0xFF), GLint((ac >> 8) & 0xFF)};
    glUniform2iv(sp->uAlphaRef, 1, ar);

    // fog
    uint32_t f0 = g.bp[BP_FOG0], f1 = g.bp[BP_FOG1], f2 = g.bp[BP_FOG2], f3 = g.bp[BP_FOG3];
    auto fogFloat = [](uint32_t r) {
        uint32_t bits = ((r >> 19) & 1) << 31 | ((r >> 11) & 0xFF) << 23 | (r & 0x7FF) << 12;
        float f;
        memcpy(&f, &bits, 4);
        return f;
    };
    float a = fogFloat(f0), c = fogFloat(f3);
    int bs = int(f2 & 31);
    float A = ldexpf(a, bs);
    float B = ldexpf(float(f1 & 0xFFFFFF) / 8388638.0f, bs - 1);
    float fog[4] = {A, B, c, float((f3 >> 20) & 1)};
    glUniform4fv(sp->uFog, 1, fog);
    uint32_t fc = g.bp[BP_FOG_COLOR];
    float fcol[4] = {float((fc >> 16) & 255) / 255.0f, float((fc >> 8) & 255) / 255.0f, float(fc & 255) / 255.0f, 1.0f};
    glUniform4fv(sp->uFogColor, 1, fcol);

    float im[24];
    for (int m = 0; m < 3; m++) {
        uint32_t r0 = g.bp[BP_IND_MTX + 3 * m], r1 = g.bp[BP_IND_MTX + 3 * m + 1], r2 = g.bp[BP_IND_MTX + 3 * m + 2];
        int sc = int(((r0 >> 22) & 3) | ((r1 >> 22) & 3) << 2 | ((r2 >> 22) & 3) << 4) - 17;
        float* row0 = &im[8 * m];
        float* row1 = &im[8 * m + 4];
        row0[0] = sext11(r0 & 0x7FF) / 1024.0f;
        row1[0] = sext11((r0 >> 11) & 0x7FF) / 1024.0f;
        row0[1] = sext11(r1 & 0x7FF) / 1024.0f;
        row1[1] = sext11((r1 >> 11) & 0x7FF) / 1024.0f;
        row0[2] = sext11(r2 & 0x7FF) / 1024.0f;
        row1[2] = sext11((r2 >> 11) & 0x7FF) / 1024.0f;
        row0[3] = ldexpf(1.0f, sc);
        row1[3] = 0.0f;
    }
    glUniform4fv(sp->uIndMtx, 6, im);

    float efb[2] = {float(EFB_W), float(EFB_H)};
    glUniform2fv(sp->uEfb, 1, efb);
    float proj[8] = {xff(XFR_PROJ), xff(XFR_PROJ + 1), xff(XFR_PROJ + 2), xff(XFR_PROJ + 3),
                     xff(XFR_PROJ + 4), xff(XFR_PROJ + 5), float(g.xfReg[XFR_PROJ + 6] & 1), 0.0f};
    glUniform4fv(sp->uProj, 2, proj);
    float vp[8] = {xff(XFR_VIEWPORT), xff(XFR_VIEWPORT + 1), xff(XFR_VIEWPORT + 2), 0.0f,
                   xff(XFR_VIEWPORT + 3), xff(XFR_VIEWPORT + 4), xff(XFR_VIEWPORT + 5), 0.0f};
    glUniform4fv(sp->uViewport, 2, vp);
    float ch[16];
    const int regs[4] = {XFR_AMB0, XFR_AMB0 + 1, XFR_MAT0, XFR_MAT0 + 1};
    for (int i = 0; i < 4; i++) {
        uint32_t v = g.xfReg[regs[i]];
        ch[4 * i + 0] = float((v >> 24) & 255) / 255.0f;
        ch[4 * i + 1] = float((v >> 16) & 255) / 255.0f;
        ch[4 * i + 2] = float((v >> 8) & 255) / 255.0f;
        ch[4 * i + 3] = float(v & 255) / 255.0f;
    }
    glUniform4fv(sp->uAmbMat, 4, ch);
}

// SMS_GX_STATS=n: every n display frames, log draws, uploads and the wall time
// spent inside sms_gx (flushes, texture decode, copies) against the frame time.
static double s_gxSeconds = 0, s_texSeconds = 0, s_drawSeconds = 0, s_copySeconds = 0, s_peekSeconds = 0;
static double nowSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}
struct GxTimer {
    double* acc;
    double t0 = nowSeconds();
    explicit GxTimer(double* a = &s_gxSeconds) : acc(a) {}
    ~GxTimer() { *acc += nowSeconds() - t0; }
};
static void statsFrame() {
    static int every = -1;
    static uint32_t frames = 0, draws = 0, verts = 0, compiles0 = 0, uploads0 = 0;
    static double t0 = 0, gx0 = 0, tex0 = 0, draw0 = 0, copy0 = 0, peek0 = 0;
    if (every < 0) {
        const char* e = getenv("SMS_GX_STATS");
        every = e ? atoi(e) : 0;
        t0 = nowSeconds();
    }
    if (every <= 0) return;
    frames++;
    draws += s_stats.draws;
    verts += s_stats.vertices;
    if (frames < uint32_t(every)) return;
    double t = nowSeconds();
    logmsg("stats: %u frames, %.1f draws/frame, %.0f vertices/frame, %u shader compiles, %u texture uploads, "
           "%.1f ms/frame total, %.1f ms/frame in sms_gx (textures %.1f, GL draw %.1f, copies %.1f, peeks %.1f)",
           frames, double(draws) / frames, double(verts) / frames, g_statShaderCompiles - compiles0,
           g_statTexUploads - uploads0, (t - t0) * 1000.0 / frames, (s_gxSeconds - gx0) * 1000.0 / frames,
           (s_texSeconds - tex0) * 1000.0 / frames, (s_drawSeconds - draw0) * 1000.0 / frames,
           (s_copySeconds - copy0) * 1000.0 / frames, (s_peekSeconds - peek0) * 1000.0 / frames);
    tex0 = s_texSeconds;
    draw0 = s_drawSeconds;
    copy0 = s_copySeconds;
    peek0 = s_peekSeconds;
    frames = draws = verts = 0;
    compiles0 = g_statShaderCompiles;
    uploads0 = g_statTexUploads;
    t0 = t;
    gx0 = s_gxSeconds;
}

void flushBatch() {
    if (s_bidx.empty() || !s_ready) {
        s_bidx.clear();
        s_bverts.clear();
        return;
    }
    GxTimer timer;
    const ShaderProgram* sp = shaderForCurrentState();
    glUseProgram(sp->prog);
    applyGlState();

    // textures: every map referenced by an enabled TEV stage or an indirect stage
    float texW[8] = {0}, texH[8] = {0};
    uint32_t used = 0;
    uint32_t gen = g.bp[BP_GENMODE];
    uint32_t nst = ((gen >> 10) & 15) + 1, nind = (gen >> 16) & 7;
    for (uint32_t s = 0; s < nst; s++) {
        uint32_t ord = (g.bp[BP_TREF + (s >> 1)] >> ((s & 1) * 12)) & 0x3FF;
        if ((ord >> 6) & 1) used |= 1u << (ord & 7);
    }
    for (uint32_t i = 0; i < nind && i < 4; i++) used |= 1u << ((g.bp[BP_RAS1_IREF] >> (6 * i)) & 7);
    for (int m = 0; m < 8; m++) {
        if (!(used & (1u << m))) continue;
        glActiveTexture(GL_TEXTURE0 + m);
        GxTimer tt(&s_texSeconds);
        bindTextureMap(m, &texW[m], &texH[m]);
    }
    glActiveTexture(GL_TEXTURE0);

    if (s_xfDirty) {
        static uint32_t buf[736];
        memcpy(buf, &g.xfMem[0], 256 * 4);
        memcpy(buf + 256, &g.xfMem[0x400], 96 * 4);
        memcpy(buf + 352, &g.xfMem[0x500], 256 * 4);
        memcpy(buf + 608, &g.xfMem[0x600], 128 * 4);
        glBindBuffer(GL_UNIFORM_BUFFER, s_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(buf), buf);
        s_xfDirty = false;
    }
    uploadUniforms(sp, texW, texH);

    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(s_bverts.size() * sizeof(HostVertex)), s_bverts.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(s_bidx.size() * 4), s_bidx.data(), GL_STREAM_DRAW);
    GLenum mode = s_bclass == PRIM_TRIS ? GL_TRIANGLES : s_bclass == PRIM_LINES ? GL_LINES : GL_POINTS;
    {
        GxTimer td(&s_drawSeconds);
        glDrawElements(mode, GLsizei(s_bidx.size()), GL_UNSIGNED_INT, nullptr);
    }
    s_stats.draws++;
    if (traceFile()) traceDraw(int(s_bclass), uint32_t(s_bverts.size()), uint32_t(s_bidx.size()), s_bverts.data(), sp->id);
    s_bidx.clear();
    s_bverts.clear();
    if (traceFile()) traceProbe();
}

// ------------------------------------------------------------------ EFB copies
static void clearRect(int x, int y, int w, int h) {
    uint32_t ar = g.bp[BP_CLEAR_AR], gb = g.bp[BP_CLEAR_GB], z = g.bp[BP_CLEAR_Z];
    glBindFramebuffer(GL_FRAMEBUFFER, s_efbFbo);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x * s_scale, y * s_scale, w * s_scale, h * s_scale);
    bool hasAlpha = (g.bp[BP_PE_CONTROL] & 7) == 1;
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(float(ar & 255) / 255.0f, float((gb >> 8) & 255) / 255.0f, float(gb & 255) / 255.0f,
                 hasAlpha ? float((ar >> 8) & 255) / 255.0f : 1.0f);
    glClearDepth(double(z & 0xFFFFFF) / 16777215.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void executeCopy(uint32_t ctrl) {
    if (!s_ready) return;
    flushBatch();
    GxTimer timer;
    GxTimer tc(&s_copySeconds);
    uint32_t src = g.bp[BP_COPY_SRC_TL], size = g.bp[BP_COPY_SRC_WH];
    int x = int(src & 0x3FF), y = int((src >> 10) & 0x3FF);
    int w = int(size & 0x3FF) + 1, h = int((size >> 10) & 0x3FF) + 1;
    const void* dest = g.copyDest;
    bool disp = (ctrl >> 14) & 1;
    bool clear = (ctrl >> 11) & 1;
    if (traceFile()) traceCopy(disp, x, y, w, h, dest, ctrl);
    int S = s_scale;
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_CLIP_DISTANCE0);
    glDisable(GL_CLIP_DISTANCE0 + 1);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    if (disp) {
        Xfb& xfb = s_xfbs[dest];
        if (!xfb.tex || xfb.w != w * S || xfb.h != h * S) {
            if (!xfb.tex) glGenTextures(1, &xfb.tex);
            glBindTexture(GL_TEXTURE_2D, xfb.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w * S, h * S, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            xfb.w = w * S;
            xfb.h = h * S;
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_efbFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_tmpFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, xfb.tex, 0);
        glBlitFramebuffer(x * S, y * S, (x + w) * S, (y + h) * S, 0, 0, w * S, h * S, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        s_lastXfb = dest;
        s_stats.efbCopies++;
    } else {
        uint32_t fmt = ((ctrl >> 3) & 1) << 3 | ((ctrl >> 4) & 7);
        bool intensity = ((ctrl >> 15) & 3) == 3;
        bool zcopy = (g.bp[BP_PE_CONTROL] & 7) == 3;
        bool half = (ctrl >> 9) & 1;
        int ow = (half ? w / 2 : w) * S, oh = (half ? h / 2 : h) * S;
        if (ow < 1) ow = 1;
        if (oh < 1) oh = 1;
        int cw = 0, chh = 0;
        GLuint tex = efbCopyLookup(dest, &cw, &chh);
        if (!tex || cw != ow || chh != oh) {
            if (!tex) glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ow, oh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        }
        uint32_t mode = fmt | (intensity ? 16u : 0u) | (zcopy ? 32u : 0u);
        efbCopyRegister(dest, tex, ow, oh, mode);
        glBindFramebuffer(GL_FRAMEBUFFER, s_tmpFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glViewport(0, 0, ow, oh);
        glUseProgram(s_copyProg);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, s_efbDepth);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_efbColor);
        float rect[4] = {float(x) / EFB_W, float(y) / EFB_H, float(w) / EFB_W, float(h) / EFB_H};
        glUniform4fv(s_copyURect, 1, rect);
        glUniform1i(s_copyUMode, GLint(mode));
        glUniform1i(s_copyUAlphaOne, (g.bp[BP_PE_CONTROL] & 7) != 1);
        glBindVertexArray(s_copyVao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(s_vao);
        s_stats.efbCopies++;
    }
    if (clear) clearRect(x, y, w, h);
    glBindFramebuffer(GL_FRAMEBUFFER, s_efbFbo);
    if (disp) {
        traceFrameAdvance();
        statsFrame();
        s_stats.draws = s_stats.vertices = 0;
    }
    if (disp && g_displayCopyHook) g_displayCopyHook(dest);
}

}  // namespace gx

using namespace gx;

extern "C" {

int GXPC_Init(GXPCGetProcFn getProc, int efbScale) {
    if (!gl::load(getProc)) return 0;
    resetState();
    rendererInit(efbScale);
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

void GXPC_Shutdown(void) {
    if (!s_ready) return;
    textureShutdown();
    shaderShutdown();
    for (auto& kv : s_xfbs) glDeleteTextures(1, &kv.second.tex);
    s_xfbs.clear();
    s_ready = false;
}

void GXPC_InvalidateRange(const void* p, uint32_t size) { textureInvalidateRange(p, size); }

}  // extern "C"

namespace gx {
uint32_t peekColor(int x, int y) {
    flushBatch();
    GxTimer tp(&s_peekSeconds);
    uint8_t px[4] = {0, 0, 0, 0};
    glBindFramebuffer(GL_FRAMEBUFFER, s_efbFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x * s_scale, y * s_scale, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    uint32_t c = uint32_t(px[3]) << 24 | uint32_t(px[0]) << 16 | uint32_t(px[1]) << 8 | px[2];
    if (FILE* f = traceFile()) fprintf(f, "  GXPeekARGB(%d,%d) = %08X\n", x, y, c);
    return c;
}
uint32_t peekZ(int x, int y) {
    flushBatch();
    GxTimer tp(&s_peekSeconds);
    uint32_t v = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, s_efbFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(x * s_scale, y * s_scale, 1, 1, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, &v);
    if (FILE* f = traceFile()) fprintf(f, "  GXPeekZ(%d,%d) = %06X\n", x, y, v >> 8);
    return v >> 8;
}
}  // namespace gx

extern "C" {

int GXPC_PresentXFB(const void* xfb, int winW, int winH) {
    flushBatch();
    if (!xfb) xfb = s_lastXfb;
    auto it = s_xfbs.find(xfb);
    if (it == s_xfbs.end()) return 0;
    const Xfb& x = it->second;
    // letterbox to 4:3
    int vw = winW, vh = winH;
    if (vw * 3 > vh * 4) vw = vh * 4 / 3;
    else vh = vw * 3 / 4;
    int ox = (winW - vw) / 2, oy = (winH - vh) / 2;
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, s_tmpFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, x.tex, 0);
    // XFB row 0 is the top of the picture; the window's row 0 is its bottom
    glBlitFramebuffer(0, 0, x.w, x.h, ox, oy + vh, ox + vw, oy, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, s_efbFbo);
    memset(&s_stats, 0, sizeof(s_stats));
    return 1;
}

void GXPC_ReadEFB(uint8_t* rgba, int* w, int* h) {
    flushBatch();
    *w = EFB_W * s_scale;
    *h = EFB_H * s_scale;
    if (!rgba) return;
    glBindFramebuffer(GL_FRAMEBUFFER, s_efbFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, *w, *h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

int GXPC_ReadXFB(const void* xfb, uint8_t* rgba, int* w, int* h) {
    flushBatch();
    if (!xfb) xfb = s_lastXfb;
    auto it = s_xfbs.find(xfb);
    if (it == s_xfbs.end()) return 0;
    *w = it->second.w;
    *h = it->second.h;
    if (!rgba) return 1;
    glBindFramebuffer(GL_FRAMEBUFFER, s_tmpFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, it->second.tex, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, *w, *h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindFramebuffer(GL_FRAMEBUFFER, s_efbFbo);
    return 1;
}

void GXPC_GetStats(GXPCStats* out) {
    *out = s_stats;
    out->shaderCompiles = g_statShaderCompiles;
    out->textureUploads = g_statTexUploads;
}

}  // extern "C"
