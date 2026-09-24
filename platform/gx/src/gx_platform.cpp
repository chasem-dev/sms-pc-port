// Window / context bring-up for sms_gx.
//
// Default: an SDL2 window with an OpenGL 3.3 core context whenever a display
// is available.  Headless (tests, CI, no display): an EGL context without a
// surface.  GXInit brings this up automatically if the host has not called
// GXPC_Init itself, and every GXCopyDisp presents the XFB to the window.
#include "gx_internal.h"
#include "sms_gx/gx_pc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#ifdef SMS_GX_HAVE_SDL2
#include <SDL.h>
#endif
#ifdef SMS_GX_HAVE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

namespace gx {
extern void (*g_displayCopyHook)(const void* xfb);
bool rendererReady();
}

using namespace gx;

namespace {
enum Mode { MODE_NONE, MODE_WINDOW, MODE_HEADLESS, MODE_EXTERNAL };
Mode s_mode = MODE_NONE;
int s_forceHeadless = -1;  // -1: decide from the environment
bool s_autoPresent = true;
int s_vsync = 0;
uint32_t s_frame = 0;

#ifdef SMS_GX_HAVE_SDL2
SDL_Window* s_window = nullptr;
SDL_GLContext s_glctx = nullptr;
void (*s_eventCb)(const SDL_Event*) = nullptr;
std::vector<SDL_GameController*> s_pads;
#endif

bool envTrue(const char* name) {
    const char* v = getenv(name);
    return v && *v && strcmp(v, "0") != 0;
}

#ifdef SMS_GX_HAVE_SDL2
void* sdlGetProc(const char* name) { return SDL_GL_GetProcAddress(name); }

bool openWindow(int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        logmsg("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    int ws = scale < 1 ? 1 : scale;
    if (const char* e = getenv("SMS_WINDOW_SCALE")) ws = atoi(e) > 0 ? atoi(e) : ws;
    s_window = SDL_CreateWindow("Super Mario Sunshine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640 * ws,
                                480 * ws, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!s_window) {
        logmsg("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }
    s_glctx = SDL_GL_CreateContext(s_window);
    if (!s_glctx) {
        logmsg("OpenGL 3.3 core context failed: %s", SDL_GetError());
        SDL_DestroyWindow(s_window);
        s_window = nullptr;
        SDL_Quit();
        return false;
    }
    SDL_GL_MakeCurrent(s_window, s_glctx);
    SDL_GL_SetSwapInterval(s_vsync);
    if (!GXPC_Init(sdlGetProc, scale)) {
        SDL_GL_DeleteContext(s_glctx);
        SDL_DestroyWindow(s_window);
        s_glctx = nullptr;
        s_window = nullptr;
        SDL_Quit();
        return false;
    }
    logmsg("window %dx%d, OpenGL context ready", 640 * ws, 480 * ws);
    return true;
}
#endif

#ifdef SMS_GX_HAVE_EGL
void* eglGetProc(const char* name) { return reinterpret_cast<void*>(eglGetProcAddress(name)); }

bool tryEglDisplay(EGLDisplay dpy) {
    EGLint major, minor;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) return false;
    if (!eglBindAPI(EGL_OPENGL_API)) return false;
    const EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) return false;
    const EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                              EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (ctx == EGL_NO_CONTEXT) return false;
    if (eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) return true;
    // no surfaceless support: fall back to a tiny pbuffer
    const EGLint pbAttr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pbAttr);
    return pb != EGL_NO_SURFACE && eglMakeCurrent(dpy, pb, pb, ctx);
}

bool openHeadless(int scale) {
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    auto queryDevices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(eglGetProcAddress("eglQueryDevicesEXT"));
    bool ok = false;
    // Mesa crashes initialising a hardware EGL device when software rendering
    // is forced; go straight to the surfaceless platform then.
    if (getPlatformDisplay && queryDevices && !envTrue("LIBGL_ALWAYS_SOFTWARE")) {
        EGLDeviceEXT devs[8];
        EGLint n = 0;
        if (queryDevices(8, devs, &n))
            for (EGLint i = 0; i < n && !ok; i++) ok = tryEglDisplay(getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devs[i], nullptr));
    }
    if (!ok && getPlatformDisplay)
        ok = tryEglDisplay(getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr));
    if (!ok) ok = tryEglDisplay(eglGetDisplay(EGL_DEFAULT_DISPLAY));
    if (!ok) {
        logmsg("headless: no EGL OpenGL 3.3 context (try LIBGL_ALWAYS_SOFTWARE=1)");
        return false;
    }
    return GXPC_Init(eglGetProc, scale) != 0;
}
#endif

void dumpFrame(const void* xfb) {
    static int every = -1;
    static const char* dir = nullptr;
    if (every < 0) {
        const char* e = getenv("SMS_GX_DUMP_EVERY");
        every = e ? atoi(e) : 0;
        dir = getenv("SMS_GX_DUMP_DIR");
        if (!dir) dir = ".";
    }
    if (every <= 0 || s_frame % uint32_t(every) != 0) return;
    int w = 0, h = 0;
    if (!GXPC_ReadXFB(xfb, nullptr, &w, &h)) return;
    std::vector<uint8_t> px(size_t(w) * h * 4);
    GXPC_ReadXFB(xfb, px.data(), &w, &h);
    char path[1024];
    snprintf(path, sizeof path, "%s/frame_%06u.ppm", dir, s_frame);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) fwrite(&px[size_t(i) * 4], 1, 3, f);
    fclose(f);
}

void onDisplayCopy(const void* xfb) {
    s_frame++;
    dumpFrame(xfb);
    if (s_autoPresent) GXPC_Present(xfb);
}
}  // namespace

extern "C" {

int GXPC_ParseArgs(int* argc, char** argv) {
    int out = 1;
    for (int i = 1; i < *argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "--headless") == 0) s_forceHeadless = 1;
        else if (strcmp(a, "--window") == 0) s_forceHeadless = 0;
        else if (strcmp(a, "--vsync") == 0) s_vsync = 1;
        else {
            argv[out++] = argv[i];
            continue;
        }
    }
    int removed = *argc - out;
    *argc = out;
    argv[out] = nullptr;
    return removed;
}

void GXPC_SetHeadless(int headless) { s_forceHeadless = headless ? 1 : 0; }
void GXPC_SetAutoPresent(int enable) { s_autoPresent = enable != 0; }
int GXPC_IsHeadless(void) { return s_mode != MODE_WINDOW; }
uint32_t GXPC_FrameCount(void) { return s_frame; }

int GXPC_InitAuto(int efbScale) {
    if (rendererReady()) return 1;
    if (const char* e = getenv("SMS_GX_SCALE")) efbScale = atoi(e) > 0 ? atoi(e) : efbScale;
    if (envTrue("SMS_VSYNC")) s_vsync = 1;
    bool headless;
    if (s_forceHeadless >= 0) headless = s_forceHeadless != 0;
    else if (envTrue("SMS_HEADLESS")) headless = true;
    else {
        const char* d = getenv("DISPLAY");
        const char* w = getenv("WAYLAND_DISPLAY");
        headless = !(d && *d) && !(w && *w);
#if defined(_WIN32) || defined(__APPLE__)
        headless = false;
#endif
    }
    g_displayCopyHook = onDisplayCopy;
#ifdef SMS_GX_HAVE_SDL2
    if (!headless) {
        if (openWindow(efbScale)) {
            s_mode = MODE_WINDOW;
            return 1;
        }
        logmsg("no window available, continuing headless");
    }
#endif
#ifdef SMS_GX_HAVE_EGL
    if (openHeadless(efbScale)) {
        s_mode = MODE_HEADLESS;
        logmsg("headless OpenGL context ready");
        return 1;
    }
#endif
    (void)headless;
    logmsg("no OpenGL context could be created; rendering is disabled");
    return 0;
}

void GXPC_Present(const void* xfb) {
#ifdef SMS_GX_HAVE_SDL2
    if (s_mode == MODE_WINDOW && s_window) {
        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(s_window, &w, &h);
        GXPC_PresentXFB(xfb, w, h);
        GXPC_OverlayDraw(w, h);
        SDL_GL_SwapWindow(s_window);
        sms_gx_pump_events();
    }
#else
    (void)xfb;
#endif
}

void sms_gx_set_event_callback(void (*cb)(const union SDL_Event*)) {
#ifdef SMS_GX_HAVE_SDL2
    s_eventCb = cb;
#else
    (void)cb;
#endif
}

void sms_gx_pump_events(void) {
#ifdef SMS_GX_HAVE_SDL2
    if (s_mode != MODE_WINDOW) return;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_CONTROLLERDEVICEADDED:
            if (SDL_IsGameController(ev.cdevice.which)) {
                if (SDL_GameController* c = SDL_GameControllerOpen(ev.cdevice.which)) s_pads.push_back(c);
            }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            for (size_t i = 0; i < s_pads.size(); i++)
                if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pads[i])) == ev.cdevice.which) {
                    SDL_GameControllerClose(s_pads[i]);
                    s_pads.erase(s_pads.begin() + long(i));
                    break;
                }
            break;
        default:
            break;
        }
        // backtick toggles the debug overlay and is kept from the pad layer
        if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && ev.key.keysym.scancode == SDL_SCANCODE_GRAVE) {
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) GXPC_OverlayToggle();
            continue;
        }
        // F7 with the overlay open cycles the game speed
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_F7 && GXPC_OverlayVisible()) {
            if (!ev.key.repeat) GXPC_CycleSpeed();
            continue;
        }
        if (s_eventCb) s_eventCb(&ev);
        if (ev.type == SDL_QUIT ||
            (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE)) {
            logmsg("window closed, exiting");
            GXPC_Shutdown();
            exit(0);
        }
    }
#endif
}

}  // extern "C"
