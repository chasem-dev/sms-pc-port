// Window / context bring-up for sms_gx.
//
// Default: an SDL2 window with an OpenGL 3.3 core context whenever a display
// is available.  Headless (tests, CI, no display): an EGL context without a
// surface.  GXInit brings this up automatically if the host has not called
// GXPC_Init itself, and every GXCopyDisp presents the XFB to the window.
#include "gx_internal.h"
#include "gx_cheat_menu.h"
#include "gx_warp_menu.h"
#include "gx_savestate_overlay.h"
#include "gx_glthread.h"
#include "gx_window_layout.h"
#include "sms_gx/gx_pc.h"

#include <atomic>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
#if !defined(_WIN32) && !defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifdef SMS_GX_HAVE_SDL2
#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#include "gx_borderless_window.h"
#endif
#endif
#ifdef SMS_GX_HAVE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

// USB ids of the GameCube adapters the PAD layer reads itself, which SDL must
// leave alone (platform/pad/gc_adapter.cpp); none without it.
extern "C" __attribute__((weak)) const char* port_gc_adapter_sdl_ignore(void) { return nullptr; }

namespace gx {
extern void (*g_displayCopyHook)(const void* xfb);
bool rendererReady();
extern double g_presentSeconds, g_swapSeconds;
}

using namespace gx;

namespace {
enum Mode { MODE_NONE, MODE_WINDOW, MODE_HEADLESS, MODE_EXTERNAL };
Mode s_mode = MODE_NONE;
int s_forceHeadless = -1;  // -1: decide from the environment
bool s_autoPresent = true;
bool s_skipPresent = false;
// Average seconds a present takes (drawing the XFB, the overlay and the swap).
// A skip request (GXPC_SkipNextPresent) is only followed when presenting is
// that slow: then the swap waits for the display and skipping one catches the
// game up; a quick present gains nothing by being left out.
double s_presentCost = 0;
const double kSkipPresentCost = 0.002;
double s_lastPresent = 0;  // the last display copy's present, 0 when skipped
int s_vsync = 0;
std::atomic<int> s_fullscreenRequest{-1};  // the settings menu's: 1 fullscreen, 0 window, -1 none; the event pump applies it
// The same, for the rest of what the settings menu changes in the window: the fullscreen type (1 exclusive,
// 0 borderless), the exclusive size (width << 16 | height, 0 for the desktop's), the display to be on, and
// mouse look (1 on, 0 off); -1 for none. The event pump applies them.
std::atomic<int> s_exclusiveRequest{-1}, s_sizeRequest{-1}, s_displayRequest{-1}, s_mouseLookRequest{-1};
std::atomic<bool> s_settingsMenuOpen{false};  // GXPC_SettingsMenuOpen: the game's settings menu is up
uint32_t s_frame = 0;

#ifdef SMS_GX_HAVE_SDL2
SDL_Window* s_window = nullptr;
SDL_GLContext s_glctx = nullptr;
void (*s_eventCb)(const SDL_Event*) = nullptr;
std::vector<SDL_GameController*> s_pads;
// sms_gx_rumble: the controller it goes to (the one last pressed or pushed,
// else the first opened; -1 for none), the one rumbling now, and the last
// strength sent with when.
SDL_JoystickID s_rumblePad = -1, s_rumbling = -1;
Uint16 s_rumbleSent = 0;
Uint32 s_rumbleSentAt = 0;
#endif
std::vector<uint8_t> s_icon;  // GXPC_SetWindowIcon, RGBA8
int s_iconW = 0, s_iconH = 0;

double nowSeconds() { return gx::monoSeconds(); }

bool envTrue(const char* name) {
    const char* v = getenv(name);
    return v && *v && strcmp(v, "0") != 0;
}

#ifdef SMS_GX_HAVE_SDL2
void* sdlGetProc(const char* name) { return SDL_GL_GetProcAddress(name); }

// SMS_PRESENT_HZ=<rate> (testing): each swap also waits for the next refresh
// of a display at that rate, as a compositor that forces vsync does (macOS,
// the Steam Deck's gamescope), whatever the vsync setting.
void waitSimulatedRefresh() {
    static double period = -1;
    if (period < 0) {
        const char* e = getenv("SMS_PRESENT_HZ");
        period = e && atof(e) > 0 ? 1.0 / atof(e) : 0;
    }
    if (period == 0) return;
    const double next = (double(int64_t(nowSeconds() / period)) + 1) * period;
    for (double now; (now = nowSeconds()) < next;)
        if (next - now > 0.002) SDL_Delay(Uint32((next - now) * 1000) - 1);
}

// SMS_MOUSE_CAMERA=1: mouse look. The mouse is captured (relative mode) while
// the window has focus; F10 releases it, a click in the window takes it back,
// and losing focus always frees it.
bool s_mouseCamera = false, s_mouseCaptured = false, s_mouseReleased = false;

void captureMouse(bool on) {
    if (!s_mouseCamera) on = false;
    if (on == s_mouseCaptured) return;
    if (SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE) == 0) s_mouseCaptured = on;
}

// SMS_FULLSCREEN: 1 or desktop (borderless, at the desktop's resolution), or
// exclusive (the display switches to SMS_FULLSCREEN_MODE=WxH[@Hz], else the
// desktop's mode). F11 or Alt+Enter toggles between the window and that
// fullscreen (desktop when SMS_FULLSCREEN is unset).
bool s_exclusive = false, s_isFullscreen = false;
// The size exclusive fullscreen switches the display to (SMS_FULLSCREEN_MODE, or the settings menu's): 0 x 0
// is the desktop's, and the refresh rate is the desktop's unless SMS_FULLSCREEN_MODE named one.
int s_fsW = 0, s_fsH = 0, s_fsHz = 0;
#ifdef _WIN32
BorderlessWindow s_borderless;
#endif

#ifdef _WIN32
// Whether Windows HDR ("advanced colour") is on for the monitor showing the
// window. An exclusive mode switch (ChangeDisplaySettingsEx) on an HDR display
// can leave the desktop's colours wrong after the game, until HDR is turned off
// and on again, so the game stays borderless there.
bool windowsHdrOn() {
    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(s_window, &wm) || wm.subsystem != SDL_SYSWM_WINDOWS) return false;
    MONITORINFOEXW monitor = {};
    monitor.cbSize = sizeof monitor;
    if (!GetMonitorInfoW(MonitorFromWindow(wm.info.win.window, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return false;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
        return false;
    for (UINT32 i = 0; i < pathCount; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof source;
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName, monitor.szDevice) != 0)
            continue;
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color = {};
        color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        color.header.size = sizeof color;
        color.header.adapterId = paths[i].targetInfo.adapterId;
        color.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS && color.advancedColorEnabled) return true;
    }
    return false;
}
#else
bool windowsHdrOn() { return false; }
#endif

// Quitting (the quit key, closing the window) leaves exclusive fullscreen
// first, so the display returns to the desktop's mode before the process ends
// rather than Windows restoring it after.
void leaveFullscreenAtExit() {
    if (!s_window || !s_isFullscreen) return;
#ifdef _WIN32
    if (s_borderless.active) {
        s_borderless.leave(s_window);
        return;
    }
#endif
    SDL_SetWindowFullscreen(s_window, 0);
}

void setFullscreen(bool on) {
    if (!s_window) return;
    Uint32 flags = 0;
    if (on && s_exclusive && windowsHdrOn()) {
        static bool told = false;
        if (!told) logmsg("Windows HDR is on: borderless fullscreen instead of exclusive, which can leave HDR's colours wrong");
        told = true;
        flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
    } else if (on && s_exclusive) {
        SDL_DisplayMode want = {}, got = {};
        const int display = std::max(0, SDL_GetWindowDisplayIndex(s_window));
        SDL_GetDesktopDisplayMode(display, &want);
        if (s_fsW > 0 && s_fsH > 0) {
            want.w = s_fsW;
            want.h = s_fsH;
            if (s_fsHz > 0) want.refresh_rate = s_fsHz;
        }
        if (SDL_GetClosestDisplayMode(display, &want, &got)) SDL_SetWindowDisplayMode(s_window, &got);
        flags = SDL_WINDOW_FULLSCREEN;
        logmsg("exclusive fullscreen %dx%d@%dHz", got.w, got.h, got.refresh_rate);
    } else if (on) {
        flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
#ifdef _WIN32
    // Borderless and the HDR fallback never enter SDL fullscreen on Windows.
    // Remember the decorated window so F11/Alt+Enter restores its size and position.
    if (flags == SDL_WINDOW_FULLSCREEN_DESKTOP) {
        if (!s_borderless.enter(s_window)) {
            logmsg("borderless change failed: %s; continuing as before", SDL_GetError());
            return;
        }
    } else if (s_borderless.active && !on) {
        s_borderless.leave(s_window);
    } else
#endif
    if (SDL_SetWindowFullscreen(s_window, flags) != 0) {
        logmsg("fullscreen change failed: %s; continuing as before", SDL_GetError());
        return;
    }
    s_isFullscreen = on;
    SDL_ShowCursor(on ? SDL_DISABLE : SDL_ENABLE);
}

// An open menu (cheats, warps or the game's settings menu) has a free mouse; closing it gives mouse
// look back the capture it had. The cheat and warp menus also show the cursor, which they click with.
bool s_menuOpen = false, s_capturedBeforeMenu = false, s_menuCursor = false;

bool menuVisible() { return cheatMenuVisible() || warpMenuVisible(); }
bool mouseFree() { return menuVisible() || s_settingsMenuOpen; }
// Fullscreen (borderless or exclusive) hides the cursor. When F10 releases
// mouse look the cursor must come back, or the release looks like it did
// nothing; capturing it again hides it as before.
void showReleasedCursor() {
    SDL_ShowCursor(s_mouseReleased || !s_isFullscreen ? SDL_ENABLE : SDL_DISABLE);
}
// F8 and the Back button ([0] and [1]) went down to open or close the warp
// menu: their release is the menu's too.
bool s_warpToggleHeld[2];

void syncMenuInput() {
    const bool cursor = menuVisible();
    if (cursor != s_menuCursor) {
        s_menuCursor = cursor;
        SDL_ShowCursor(cursor || !s_isFullscreen ? SDL_ENABLE : SDL_DISABLE);
    }
    const bool open = mouseFree();
    if (open == s_menuOpen) return;
    s_menuOpen = open;
    if (open) {
        s_capturedBeforeMenu = s_mouseCaptured;
        captureMouse(false);
        return;
    }
    if (s_capturedBeforeMenu && (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS)) captureMouse(true);
}

// Mouse look on or off while the game runs (the settings menu's). Turned on in a menu, it takes the mouse
// when the menu closes.
void applyMouseLook(bool on) {
    if (on == s_mouseCamera) return;
    s_mouseCamera = on;
    s_mouseReleased = false;
    if (on) {
        logmsg("mouse look on (F10 releases the mouse)");
        if (mouseFree()) s_capturedBeforeMenu = true;
        else captureMouse((SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0);
        return;
    }
    logmsg("mouse look off");
    captureMouse(false);  // frees it: with mouse look off there is nothing to capture for
    s_capturedBeforeMenu = false;
    if (!s_menuCursor) SDL_ShowCursor(s_isFullscreen ? SDL_DISABLE : SDL_ENABLE);
}

// Puts the window, not fullscreen, in the middle of another display's usable area, smaller if it does not fit.
void moveToDisplay(int display) {
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(display, &usable) != 0 && SDL_GetDisplayBounds(display, &usable) != 0) return;
    WindowBorders borders = {48, 8, 8, 8};
    if (SDL_GetWindowBordersSize(s_window, &borders.top, &borders.left, &borders.bottom, &borders.right) != 0)
        borders = {48, 8, 8, 8};
    int w = 0, h = 0;
    SDL_GetWindowSize(s_window, &w, &h);
    const int roomW = std::max(1, usable.w - borders.left - borders.right);
    const int roomH = std::max(1, usable.h - borders.top - borders.bottom);
    if (w > roomW || h > roomH) {
        const double fit = std::min(double(roomW) / w, double(roomH) / h);
        w = std::max(1, int(w * fit));
        h = std::max(1, int(h * fit));
        SDL_SetWindowSize(s_window, w, h);
    }
    SDL_SetWindowPosition(s_window,
        usable.x + (usable.w - w - borders.left - borders.right) / 2 + borders.left,
        usable.y + (usable.h - h - borders.top - borders.bottom) / 2 + borders.top);
    SDL_PumpEvents();  // where the window is now decides the display fullscreen then uses
}

// What the settings menu asked of the window and the mouse since the last pump (gx_settings.cpp). A
// fullscreen window that changes type, size or display leaves fullscreen and enters it again.
void applyWindowRequests() {
    const int fullscreen = s_fullscreenRequest.exchange(-1);
    const int exclusive = s_exclusiveRequest.exchange(-1);
    const int size = s_sizeRequest.exchange(-1);
    const int display = s_displayRequest.exchange(-1);
    const int look = s_mouseLookRequest.exchange(-1);
    bool restyle = false;
    if (exclusive >= 0 && (exclusive != 0) != s_exclusive) {
        s_exclusive = exclusive != 0;
        restyle = true;
    }
    if (size >= 0 && ((size >> 16) != s_fsW || (size & 0xffff) != s_fsH)) {
        s_fsW = size >> 16;
        s_fsH = size & 0xffff;
        s_fsHz = 0;
        restyle = restyle || s_exclusive;
    }
    const bool wasFullscreen = s_isFullscreen;
    const bool moving = display >= 0 && display < SDL_GetNumVideoDisplays() && display != SDL_GetWindowDisplayIndex(s_window);
    if (wasFullscreen && (restyle || moving)) setFullscreen(false);
    if (moving && !s_isFullscreen) moveToDisplay(display);
    const bool want = fullscreen >= 0 ? fullscreen != 0 : wasFullscreen;
    if (want != s_isFullscreen) setFullscreen(want);
    syncMenuInput();
    if (look >= 0) applyMouseLook(look != 0);
}

void applyIcon() {
    if (!s_window || s_icon.empty()) return;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(s_icon.data(), s_iconW, s_iconH, 32, s_iconW * 4,
                                                        SDL_PIXELFORMAT_RGBA32);
    if (!s) return;
    SDL_SetWindowIcon(s_window, s);
    SDL_FreeSurface(s);
}

// Why the last openWindow failed: no display to open a window on (SDL's video
// init failed), or a display whose window or OpenGL context could not be made.
enum WindowFailure { WF_NONE, WF_NO_DISPLAY, WF_WINDOW };
WindowFailure s_windowFailure = WF_NONE;
char s_videoDriver[32];     // the SDL video driver of the last attempt ("x11", "wayland", ...)
char s_windowError[256];    // and its SDL error

// SMS_FSR_MODE (with SMS_PRESENT_FILTER=fsr or nis) sets the internal resolution
// from the picture's width on screen (outW x outH pixels): native renders it
// at that width, quality at 1/1.5 of it, balanced 1/1.7, performance 1/2 and
// ultraperformance 1/3, and FSR 1 or NIS upscales the rest of the way. Never below
// the GameCube's own resolution, nor above 8 times it. The settings menu does the same
// for a mode it picks (platformFsrScale), from the window as it is then.
const struct { const char* name; float ratio; } kFsrModes[] = {
    {"native", 1.0f}, {"quality", 1.5f}, {"balanced", 1.7f}, {"performance", 2.0f}, {"ultraperformance", 3.0f}};
const int kFsrModeCount = int(sizeof kFsrModes / sizeof kFsrModes[0]);

// The internal resolution that draws the picture at 1/ratio of its width on screen; the picture is as
// wide as the window, or as the window's height allows at the game's shape unless it is stretched.
float fsrInternalScale(float ratio, float outW, float outH, bool stretch, float* picWidth) {
    const float wide = GXPC_GetWidescreen();
    const float aspect = 4.0f / 3.0f * wide;
    const float picW = stretch ? outW : std::min(outW, outH * aspect);
    if (picWidth) *picWidth = picW;
    return std::max(1.0f, std::min(8.0f, picW / (640.0f * wide * ratio)));
}

int fsrModeByName(const char* mode) {
    for (int i = 0; i < kFsrModeCount; i++)
        if (mode && !strcmp(mode, kFsrModes[i].name)) return i;
    return -1;
}

static float fsrScale(float scale, float outW, float outH) {
    const char* filter = getenv("SMS_PRESENT_FILTER");
    const char* mode = getenv("SMS_FSR_MODE");
    if (!filter || (strcmp(filter, "fsr") != 0 && strcmp(filter, "nis") != 0) || !mode || !*mode) return scale;
    const int index = fsrModeByName(mode);
    if (index < 0 || outW <= 0.0f || outH <= 0.0f) return scale;
    const char* fit = getenv("SMS_ASPECT");
    float picW = 0.0f;
    const float s = fsrInternalScale(kFsrModes[index].ratio, outW, outH, fit && !strcmp(fit, "stretch"), &picW);
    logmsg("%s %s: the picture is %d pixels wide on screen, so internal resolution scale %.2f",
           strcmp(filter, "nis") == 0 ? "NIS" : "FSR 1", mode,
           int(picW + 0.5f), double(s));
    return s;
}

bool openWindow(float scale) {
#ifdef SDL_HINT_WINDOWS_DPI_SCALING
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
    s_windowFailure = WF_NONE;
    s_videoDriver[0] = 0;
    // GameCube adapters: SDL's HIDAPI driver turns each plugged port into a
    // mapped game controller (EVORETRO and other PC-mode adapters). Set before
    // SDL_Init; an SDL_JOYSTICK_HIDAPI_GAMECUBE environment value wins.
    SDL_SetHint("SDL_JOYSTICK_HIDAPI", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_GAMECUBE", "1");
    // Wii U-mode adapters (057e:0337) are the PAD layer's own
    // (platform/pad/gc_adapter.cpp): SDL leaves them alone, so they are not read
    // twice or taken from under it. Added to any SDL_HIDAPI_IGNORE_DEVICES set.
    if (const char* ids = port_gc_adapter_sdl_ignore()) {
        const char* had = getenv("SDL_HIDAPI_IGNORE_DEVICES");
        char list[512];
        snprintf(list, sizeof list, "%s%s%s", had && *had ? had : "", had && *had ? "," : "", ids);
        SDL_SetHintWithPriority("SDL_HIDAPI_IGNORE_DEVICES", list, SDL_HINT_OVERRIDE);
    }
    SDL_SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        logmsg("SDL_Init failed: %s", SDL_GetError());
        snprintf(s_windowError, sizeof s_windowError, "%s", SDL_GetError());
        s_windowFailure = WF_NO_DISPLAY;
        return false;
    }
    if (const char* d = SDL_GetCurrentVideoDriver()) snprintf(s_videoDriver, sizeof s_videoDriver, "%s", d);
    // from here on a display exists: a failure is the window's or the context's
    s_windowFailure = WF_WINDOW;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // Open on the monitor containing the pointer (usually the launcher's Play
    // button). Fall back to the primary display if global coordinates are unavailable.
    SDL_Point pointer = {};
    SDL_GetGlobalMouseState(&pointer.x, &pointer.y);
    int display = 0;
    for (int i = 0; i < SDL_GetNumVideoDisplays(); ++i) {
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(i, &bounds) == 0 && SDL_PointInRect(&pointer, &bounds)) {
            display = i;
            break;
        }
    }
    // SMS_DISPLAY=n picks the monitor instead (0 is the primary one)
    if (const char* e = getenv("SMS_DISPLAY")) {
        const int want = atoi(e);
        if (*e && want >= 0 && want < SDL_GetNumVideoDisplays()) display = want;
    }
    SDL_Rect desktop = {0, 0, 1280, 800};
    if (SDL_GetDisplayUsableBounds(display, &desktop) != 0 &&
        SDL_GetDisplayBounds(display, &desktop) != 0) desktop = {0, 0, 1280, 800};
    int windowScale = 0;
    if (const char* e = getenv("SMS_WINDOW_SCALE")) {
        const long requested = strtol(e, nullptr, 10);
        if (requested > 0) windowScale = int(std::min(requested, 16L));
    }
    const WindowArea layout = initialWindowLayout({desktop.x, desktop.y, desktop.w, desktop.h},
                                                  640.0 * GXPC_GetWidescreen() / 480.0, windowScale);
    s_window = SDL_CreateWindow("Super Mario Sunshine", layout.x, layout.y, layout.w, layout.h,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                                SDL_WINDOW_HIDDEN);
    if (!s_window) {
        logmsg("SDL_CreateWindow failed (%s): %s", s_videoDriver, SDL_GetError());
        snprintf(s_windowError, sizeof s_windowError, "%s", SDL_GetError());
        SDL_Quit();
        return false;
    }
    SDL_SetWindowMinimumSize(s_window, std::min(320, layout.w), std::min(240, layout.h));
    static bool atExit = false;
    if (!atExit) atExit = atexit(leaveFullscreenAtExit) == 0;
    applyIcon();
    s_glctx = SDL_GL_CreateContext(s_window);
    if (!s_glctx) {
        logmsg("OpenGL 3.3 core context failed (%s): %s", s_videoDriver, SDL_GetError());
        snprintf(s_windowError, sizeof s_windowError, "OpenGL 3.3: %s", SDL_GetError());
        SDL_DestroyWindow(s_window);
        s_window = nullptr;
        SDL_Quit();
        return false;
    }
    SDL_GL_MakeCurrent(s_window, s_glctx);
    // adaptive vsync (-1) tears only when a frame is late; not every driver has it
    if (SDL_GL_SetSwapInterval(s_vsync) != 0 && s_vsync < 0) SDL_GL_SetSwapInterval(1);
    {  // the picture's size on screen, for FSR 1's modes: the window, or the display it will fill
        int drawW = layout.w, drawH = layout.h;
        SDL_GL_GetDrawableSize(s_window, &drawW, &drawH);
        float outW = float(drawW), outH = float(drawH);
        if (envTrue("SMS_FULLSCREEN")) {
            const float dpi = layout.w > 0 ? float(drawW) / float(layout.w) : 1.0f;
            SDL_Rect bounds;
            if (SDL_GetDisplayBounds(display, &bounds) == 0) {
                outW = float(bounds.w) * dpi;
                outH = float(bounds.h) * dpi;
            }
            int mw = 0, mh = 0;
            const char* fs = getenv("SMS_FULLSCREEN");
            const char* fm = getenv("SMS_FULLSCREEN_MODE");
            if (fs && !strcmp(fs, "exclusive") && fm && sscanf(fm, "%dx%d", &mw, &mh) == 2 && mw > 0 && mh > 0) {
                outW = float(mw);
                outH = float(mh);
            }
        }
        scale = fsrScale(scale, outW, outH);
    }
    if (!GXPC_Init(sdlGetProc, scale)) {
        snprintf(s_windowError, sizeof s_windowError, "the renderer could not start on this OpenGL context");
        SDL_GL_DeleteContext(s_glctx);
        SDL_DestroyWindow(s_window);
        s_glctx = nullptr;
        s_window = nullptr;
        SDL_Quit();
        return false;
    }
    SDL_ShowWindow(s_window);
#ifdef _WIN32
    // A launcher that starts the game with its console hidden (SW_HIDE in the
    // startup info, as SMS Launcher does) makes Windows apply that to the first
    // ShowWindow too, which leaves the game window hidden. Windows honours the
    // next one, so show it again.
    STARTUPINFOW startup = {};
    startup.cb = sizeof startup;
    GetStartupInfoW(&startup);
    if ((startup.dwFlags & STARTF_USESHOWWINDOW) && startup.wShowWindow == SW_HIDE) {
        SDL_HideWindow(s_window);
        SDL_ShowWindow(s_window);
    }
#endif
    // Some window managers choose their own placement when mapping a hidden
    // window. Center the decorated frame after showing it, using a conservative
    // title-bar allowance if the platform cannot report its borders yet.
    WindowBorders borders = {48, 8, 8, 8};
    if (SDL_GetWindowBordersSize(s_window, &borders.top, &borders.left, &borders.bottom, &borders.right) != 0)
        borders = {48, 8, 8, 8};
    SDL_SetWindowPosition(s_window,
        desktop.x + (desktop.w - layout.w - borders.left - borders.right) / 2 + borders.left,
        desktop.y + (desktop.h - layout.h - borders.top - borders.bottom) / 2 + borders.top);
    // Apply the launcher's choice after normal placement so fullscreen uses
    // the same monitor. Desktop fullscreen keeps the display's native mode;
    // rendering quality and aspect ratio are still handled by the renderer.
    // Exclusive fullscreen switches the display to the mode asked for.
    const char* fullscreen = getenv("SMS_FULLSCREEN");
    s_exclusive = fullscreen && strcmp(fullscreen, "exclusive") == 0;
    if (const char* e = getenv("SMS_FULLSCREEN_MODE")) {
        int w = 0, h = 0, hz = 0;
        if (sscanf(e, "%dx%d@%d", &w, &h, &hz) >= 2 && w > 0 && h > 0) {
            s_fsW = std::min(w, 0xffff);
            s_fsH = std::min(h, 0xffff);
            s_fsHz = std::max(hz, 0);
        }
    }
    if (windowsHdrOn()) logmsg("Windows HDR is on for this monitor");
    if (envTrue("SMS_FULLSCREEN")) setFullscreen(true);
#ifdef _WIN32
    {  // SMS_HDR: present in HDR through Direct3D (gx_hdr.cpp)
        SDL_SysWMinfo wm;
        SDL_VERSION(&wm.version);
        if (SDL_GetWindowWMInfo(s_window, &wm) && wm.subsystem == SDL_SYSWM_WINDOWS) hdrInit(wm.info.win.window);
    }
#else
    hdrInit(nullptr);
#endif
    if (s_isFullscreen)
        logmsg("%s fullscreen on display %d (%s), internal resolution scale %g, OpenGL context ready",
               (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN) &&
               (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != SDL_WINDOW_FULLSCREEN_DESKTOP ? "exclusive" : "desktop",
               display, s_videoDriver, scale);
    else
        logmsg("window %dx%d centered on display %d (%s), internal resolution scale %g, OpenGL context ready",
               layout.w, layout.h, display, s_videoDriver, scale);
    s_windowFailure = WF_NONE;
    s_mouseCamera = envTrue("SMS_MOUSE_CAMERA");
    if (s_mouseCamera) {
        logmsg("mouse look on (F10 releases the mouse)");
        captureMouse((SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0);
    }
    // the context moves to the GL thread (gx_glthread.h)
    SDL_GL_MakeCurrent(s_window, nullptr);
    if (glt::start([](void*) { SDL_GL_MakeCurrent(s_window, s_glctx); }, nullptr)) glt::installProxies();
    else SDL_GL_MakeCurrent(s_window, s_glctx);
    return true;
}

#if !defined(_WIN32) && !defined(__APPLE__)
// A program started without DISPLAY and WAYLAND_DISPLAY (by some desktop
// launchers, services and sandboxes) still has the user's desktop when its
// sockets are there: point SDL at them instead of deciding there is no screen.
void findDesktop() {
    const char* d = getenv("DISPLAY");
    const char* w = getenv("WAYLAND_DISPLAY");
    if ((d && *d) || (w && *w)) return;
    struct stat st;
    std::string runtime;
    if (const char* x = getenv("XDG_RUNTIME_DIR")) runtime = x;
    if (runtime.empty()) {
        char buf[64];
        snprintf(buf, sizeof buf, "/run/user/%u", (unsigned)getuid());
        runtime = buf;
        if (stat(runtime.c_str(), &st) == 0) setenv("XDG_RUNTIME_DIR", runtime.c_str(), 1);
    }
    if (stat((runtime + "/wayland-0").c_str(), &st) == 0) {
        setenv("WAYLAND_DISPLAY", "wayland-0", 1);
        logmsg("WAYLAND_DISPLAY was not set; using the desktop's Wayland socket %s/wayland-0", runtime.c_str());
    }
    if (stat("/tmp/.X11-unix/X0", &st) == 0) {
        setenv("DISPLAY", ":0", 1);
        logmsg("DISPLAY was not set; using the desktop's X11 display :0");
    }
}

// SDL_VIDEODRIVER for SDL2, SDL_VIDEO_DRIVER for SDL3 (and sdl2-compat on it)
void forceVideoDriver(const char* name) {
    setenv("SDL_VIDEODRIVER", name, 1);
    setenv("SDL_VIDEO_DRIVER", name, 1);
}
#endif

// openWindow, and when a display's window fails, again on its other backend
// (Wayland and X11: a desktop session usually has both, and a driver or
// library problem often affects only one).
bool openWindowAnyBackend(float scale) {
#if !defined(_WIN32) && !defined(__APPLE__)
    findDesktop();
#endif
    if (openWindow(scale)) return true;
#if !defined(_WIN32) && !defined(__APPLE__)
    if (s_windowFailure != WF_WINDOW) return false;
    const char* other = !strcmp(s_videoDriver, "wayland") ? "x11" : !strcmp(s_videoDriver, "x11") ? "wayland" : nullptr;
    if (other && getenv(!strcmp(other, "x11") ? "DISPLAY" : "WAYLAND_DISPLAY")) {
        const std::string first = s_videoDriver, firstError = s_windowError;
        logmsg("no window on %s (%s); trying %s", first.c_str(), firstError.c_str(), other);
        forceVideoDriver(other);
        if (openWindow(scale)) return true;
        snprintf(s_windowError, sizeof s_windowError, "%s: %s; %s: %s", first.c_str(), firstError.c_str(), other,
                 std::string(s_windowError).c_str());
        s_windowFailure = WF_WINDOW;
    }
#endif
    return false;
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

bool openHeadless(float scale) {
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
    if (!GXPC_Init(eglGetProc, scale)) return false;
    // the context moves to the GL thread (gx_glthread.h)
    static EGLDisplay dpy;
    static EGLSurface surf;
    static EGLContext ctx;
    dpy = eglGetCurrentDisplay();
    surf = eglGetCurrentSurface(EGL_DRAW);
    ctx = eglGetCurrentContext();
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (glt::start([](void*) { eglMakeCurrent(dpy, surf, surf, ctx); }, nullptr)) glt::installProxies();
    else eglMakeCurrent(dpy, surf, surf, ctx);
    return true;
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
    GXPC_OverlayFrame();
    dumpFrame(xfb);
    const bool skip = s_skipPresent && s_presentCost >= kSkipPresentCost;
    s_skipPresent = false;
    s_lastPresent = 0;
    if (!s_autoPresent) return;
    if (skip) sms_gx_pump_events();
    else GXPC_Present(xfb);
}
}  // namespace

namespace gx {
// GXPC_Shutdown: the GL thread ends and the context is current on the
// calling thread again, so the caller may destroy it.
void releaseGlThread() {
    if (!glt::active()) return;
#ifdef SMS_GX_HAVE_SDL2
    if (s_mode == MODE_WINDOW && s_window) {
        glt::sync([] { SDL_GL_MakeCurrent(s_window, nullptr); });
        glt::stop();
        SDL_GL_MakeCurrent(s_window, s_glctx);
        return;
    }
#endif
#ifdef SMS_GX_HAVE_EGL
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLContext ctx = EGL_NO_CONTEXT;
    glt::sync([&] {
        dpy = eglGetCurrentDisplay();
        surf = eglGetCurrentSurface(EGL_DRAW);
        ctx = eglGetCurrentContext();
        if (dpy != EGL_NO_DISPLAY) eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    });
    glt::stop();
    if (dpy != EGL_NO_DISPLAY) eglMakeCurrent(dpy, surf, surf, ctx);
#else
    glt::stop();
#endif
}

// What the settings menu (gx_settings.cpp) changes in the window. 0 when there is no window.
bool platformHasWindow() { return s_mode == MODE_WINDOW; }
int platformVsync() { return s_vsync; }  // 0 off, 1 on, -1 adaptive
// The GL thread: the swap interval is the context's. Adaptive is on where the driver has no adaptive
// vsync, and the call says so by returning false.
bool platformSetVsync(int mode) {
    s_vsync = mode;
#ifdef SMS_GX_HAVE_SDL2
    if (s_mode != MODE_WINDOW || hdrActive()) return true;  // HDR waits for the display itself (hdrFramePresent)
    if (SDL_GL_SetSwapInterval(mode) != 0 && mode < 0) {
        SDL_GL_SetSwapInterval(1);
        s_vsync = 1;
        return false;
    }
#endif
    return true;
}
// What the window is, or will be when the event pump has done what was asked of it.
bool platformFullscreen() {
#ifdef SMS_GX_HAVE_SDL2
    const int asked = s_fullscreenRequest;
    return asked >= 0 ? asked != 0 : s_isFullscreen;
#else
    return false;
#endif
}
// The window is the event pump's: this only asks, and the next pump does it.
void platformRequestFullscreen(bool on) { s_fullscreenRequest = on ? 1 : 0; }

// The fullscreen type: exclusive (the display switches to a size) or borderless. As with fullscreen, what
// is asked counts until the pump has done it.
bool platformExclusive() {
#ifdef SMS_GX_HAVE_SDL2
    const int asked = s_exclusiveRequest;
    return asked >= 0 ? asked != 0 : s_exclusive;
#else
    return false;
#endif
}
void platformRequestExclusive(bool on) { s_exclusiveRequest = on ? 1 : 0; }

// The size exclusive fullscreen switches to; 0 x 0 is the desktop's.
void platformFullscreenSize(int* w, int* h) {
#ifdef SMS_GX_HAVE_SDL2
    const int asked = s_sizeRequest;
    *w = asked >= 0 ? asked >> 16 : s_fsW;
    *h = asked >= 0 ? asked & 0xffff : s_fsH;
#else
    *w = *h = 0;
#endif
}
void platformRequestFullscreenSize(int w, int h) {
    s_sizeRequest = (std::min(std::max(w, 0), 0xffff) << 16) | std::min(std::max(h, 0), 0xffff);
}
// The sizes a display can switch to at its desktop's refresh rate (every mode when the driver names none
// at it), each once, ascending.
std::vector<DisplaySize> platformFullscreenSizes(int display) {
    std::vector<DisplaySize> sizes;
#ifdef SMS_GX_HAVE_SDL2
    if (s_mode != MODE_WINDOW) return sizes;
    SDL_DisplayMode desktop = {};
    if (SDL_GetDesktopDisplayMode(display, &desktop) != 0) return sizes;
    const int modes = SDL_GetNumDisplayModes(display);
    for (int pass = 0; pass < 2 && sizes.empty(); pass++)
        for (int i = 0; i < modes; i++) {
            SDL_DisplayMode m = {};
            if (SDL_GetDisplayMode(display, i, &m) != 0 || m.w <= 0 || m.h <= 0) continue;
            if (pass == 0 && m.refresh_rate != desktop.refresh_rate) continue;
            sizes.push_back({m.w, m.h});
        }
    std::sort(sizes.begin(), sizes.end(),
              [](const DisplaySize& a, const DisplaySize& b) { return a.w != b.w ? a.w < b.w : a.h < b.h; });
    sizes.erase(std::unique(sizes.begin(), sizes.end(),
                            [](const DisplaySize& a, const DisplaySize& b) { return a.w == b.w && a.h == b.h; }),
                sizes.end());
#else
    (void)display;
#endif
    return sizes;
}

// The displays (0 is the primary one, SDL's order), and the one the window is on, or will be.
int platformDisplayCount() {
#ifdef SMS_GX_HAVE_SDL2
    return s_mode == MODE_WINDOW ? std::max(0, SDL_GetNumVideoDisplays()) : 0;
#else
    return 0;
#endif
}
int platformDisplay() {
#ifdef SMS_GX_HAVE_SDL2
    const int asked = s_displayRequest;
    if (asked >= 0) return asked;
    return s_mode == MODE_WINDOW && s_window ? std::max(0, SDL_GetWindowDisplayIndex(s_window)) : 0;
#else
    return 0;
#endif
}
void platformRequestDisplay(int index) { s_displayRequest = std::max(0, index); }

// Mouse look (SMS_MOUSE_CAMERA).
bool platformMouseLook() {
#ifdef SMS_GX_HAVE_SDL2
    const int asked = s_mouseLookRequest;
    return asked >= 0 ? asked != 0 : s_mouseCamera;
#else
    return false;
#endif
}
void platformRequestMouseLook(bool on) { s_mouseLookRequest = on ? 1 : 0; }

// FSR 1 and NIS modes (see fsrScale): the one SMS_FSR_MODE named, or -1; and the internal resolution a mode
// makes for the picture as it is on screen now, 0 where there is no window to measure.
int platformFsrModeAtStart() {
#ifdef SMS_GX_HAVE_SDL2
    return fsrModeByName(getenv("SMS_FSR_MODE"));
#else
    return -1;
#endif
}
float platformFsrScale(int mode) {
#ifdef SMS_GX_HAVE_SDL2
    if (s_mode != MODE_WINDOW || !s_window || mode < 0 || mode >= kFsrModeCount) return 0.0f;
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(s_window, &w, &h);
    if (w <= 0 || h <= 0) return 0.0f;
    return fsrInternalScale(kFsrModes[mode].ratio, float(w), float(h), postSettings().aspect == 1, nullptr);
#else
    (void)mode;
    return 0.0f;
#endif
}
}  // namespace gx

extern "C" {

int GXPC_ParseArgs(int* argc, char** argv) {
    int out = 1;
    for (int i = 1; i < *argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "--headless") == 0) s_forceHeadless = 1;
        else if (strcmp(a, "--window") == 0) s_forceHeadless = 0;
        else if (strcmp(a, "--vsync") == 0) s_vsync = 1;
        else if (strcmp(a, "--display-info") == 0) {  // for the launcher: what Windows reports for each display
            GXPC_PrintDisplayInfo();
            exit(0);
        }
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

void GXPC_SetWindowIcon(const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return;
    s_icon.assign(rgba, rgba + (size_t)w * h * 4);
    s_iconW = w;
    s_iconH = h;
#ifdef SMS_GX_HAVE_SDL2
    applyIcon();
#endif
}
void GXPC_SetAutoPresent(int enable) { s_autoPresent = enable != 0; }
void GXPC_SkipNextPresent(int skip) { s_skipPresent = skip != 0; }
double GXPC_LastPresentSeconds(void) { return s_lastPresent; }
int GXPC_MouseCaptured(void) {
#ifdef SMS_GX_HAVE_SDL2
    return s_mouseCaptured;
#else
    return 0;
#endif
}
int GXPC_GameInputBlocked(void) { return menuVisible(); }
// The game's settings menu is up (1) or closed (0): like the cheat and warp menus, it frees the mouse
// that mouse look holds, and mouse look takes it back when the menu closes. The event pump does it.
void GXPC_SettingsMenuOpen(int open) { s_settingsMenuOpen = open != 0; }
int GXPC_IsHeadless(void) { return s_mode != MODE_WINDOW; }
uint32_t GXPC_FrameCount(void) { return s_frame; }

int GXPC_InitAuto(float efbScale) {
    if (rendererReady()) return 1;
    if (const char* e = getenv("SMS_GX_SCALE")) efbScale = atof(e) >= 1.0 ? std::min(8.0f, float(atof(e))) : efbScale;
    if (envTrue("SMS_VSYNC")) s_vsync = strcmp(getenv("SMS_VSYNC"), "adaptive") == 0 ? -1 : 1;
    bool headless;
    if (s_forceHeadless >= 0) headless = s_forceHeadless != 0;
    else if (envTrue("SMS_HEADLESS")) headless = true;
    // Otherwise a window: SDL finds out whether there is a display (see
    // findDesktop for a session that lost DISPLAY / WAYLAND_DISPLAY).
    else headless = false;
    g_displayCopyHook = onDisplayCopy;
#ifdef SMS_GX_HAVE_SDL2
    if (!headless) {
        if (openWindowAnyBackend(efbScale)) {
            s_mode = MODE_WINDOW;
            return 1;
        }
        if (s_windowFailure == WF_WINDOW) {
            // A display is there but no window could be made. Running on
            // invisibly (sound, no picture) would leave the player nothing to
            // close; say why and stop instead. --headless / SMS_HEADLESS=1
            // still run without a window.
            char msg[512];
            snprintf(msg, sizeof msg,
                     "Super Mario Sunshine could not open its window (%s).\n\nUpdating your graphics drivers, or "
                     "setting SDL_VIDEODRIVER=x11 or =wayland, may help.",
                     s_windowError);
            logmsg("no window could be opened on this display (%s); exiting", s_windowError);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super Mario Sunshine", msg, nullptr);
            exit(1);
        }
        logmsg("no display (%s), continuing headless", s_windowError);
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
        double t0 = 0, t1 = 0, tSwapped = 0;
        // HDR (gx_hdr.cpp): Direct3D's swap chain is used on this thread, the window's, because DXGI may wait
        // for the window's thread, which must not be the one waiting for the GL thread below. So the picture's
        // size is settled here, and Direct3D presents here once the GL thread has drawn the picture. Settling
        // it can end HDR (then this frame goes to the window as usual).
        const bool hdrReady = hdrActive() && hdrFramePrepare(w, h);
        const bool hdr = hdrActive();
        // on the GL thread, after what the frame queued before (the frame's
        // own work, which is not timed as presenting); this thread waits for
        // the swap, as it did when it made it
        bool shown = true;
        const double waited0 = glt::waitedSeconds();
        glt::sync([&] {
            t0 = nowSeconds();
            if (hdr && !(hdrReady && hdrFrameBegin())) {  // minimised: nothing to show
                shown = false;
                return;
            }
            GXPC_PresentXFB(xfb, w, h);
            GXPC_OverlayDraw(w, h);
            cheatMenuRender(w, h);
            warpMenuRender(w, h);
            savestateOverlayRender(w, h);
            captureWindow(s_frame, w, h);
            t1 = nowSeconds();
            if (hdr) hdrFrameEnd();
            else SDL_GL_SwapWindow(s_window);
            tSwapped = nowSeconds();
        });
        {  // the wait for the frame's queued work counts as GX's, the rest is presenting
            const double waited = glt::waitedSeconds() - waited0;
            g_presentWaited += waited;
            g_presentDrain += std::max(0.0, waited - (shown ? tSwapped - t0 : 0.0));
        }
        if (!shown) return;
        if (hdr) {  // Direct3D's part of the swap, with its wait for the display
            const double tPresent = nowSeconds();
            hdrFramePresent(s_vsync != 0);
            tSwapped += nowSeconds() - tPresent;
        }
        const double tBack = nowSeconds();
        waitSimulatedRefresh();
        double t2 = nowSeconds();
        g_presentSeconds += t1 - t0;
        g_swapSeconds += (tSwapped - t1) + (t2 - tBack);
        s_lastPresent = (tSwapped - t0) + (t2 - tBack);
        s_presentCost += (s_lastPresent - s_presentCost) * 0.1;
        GXPC_EndPresent();
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

#ifdef SMS_GX_HAVE_SDL2
extern "C" void GXPC_NoteInputDevice(int controller, int sdlType, const char* name);

// Tell the button prompts which device the player last used: a key, mouse
// click or mouse movement means keyboard and mouse; a button or a stick pushed
// well off centre means that controller.
static void noteInputDevice(const SDL_Event& ev) {
    SDL_JoystickID which = -1;
    if (ev.type == SDL_CONTROLLERBUTTONDOWN) which = ev.cbutton.which;
    else if (ev.type == SDL_CONTROLLERAXISMOTION && (ev.caxis.value > 16000 || ev.caxis.value < -16000))
        which = ev.caxis.which;
    else if (ev.type == SDL_KEYDOWN || ev.type == SDL_MOUSEBUTTONDOWN ||
             (ev.type == SDL_MOUSEMOTION && ev.motion.xrel * ev.motion.xrel + ev.motion.yrel * ev.motion.yrel > 25)) {
        GXPC_NoteInputDevice(0, 0, "");
        return;
    }
    if (which < 0) return;
    s_rumblePad = which;
    SDL_GameController* c = SDL_GameControllerFromInstanceID(which);
    if (!c) return;
    const char* name = SDL_GameControllerName(c);
    GXPC_NoteInputDevice(1, int(SDL_GameControllerGetType(c)), name ? name : "");
}
#endif

void sms_gx_pump_events(void) {
#ifdef SMS_GX_HAVE_SDL2
    if (s_mode != MODE_WINDOW) return;
    applyWindowRequests();
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (!menuVisible()) noteInputDevice(ev);
        switch (ev.type) {
        case SDL_CONTROLLERDEVICEADDED:
            if (SDL_IsGameController(ev.cdevice.which)) {
                if (SDL_GameController* c = SDL_GameControllerOpen(ev.cdevice.which)) {
                    s_pads.push_back(c);
                    if (s_rumblePad < 0) s_rumblePad = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c));
                }
            }
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            for (size_t i = 0; i < s_pads.size(); i++)
                if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pads[i])) == ev.cdevice.which) {
                    SDL_GameControllerClose(s_pads[i]);
                    s_pads.erase(s_pads.begin() + long(i));
                    break;
                }
            if (s_rumbling == ev.cdevice.which) {
                s_rumbling = -1;
                s_rumbleSent = 0;
            }
            if (s_rumblePad == ev.cdevice.which)
                s_rumblePad = s_pads.empty() ? -1 : SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pads[0]));
            break;
        default:
            break;
        }
        // backtick toggles the debug overlay and is kept from the pad layer
        if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && ev.key.keysym.scancode == SDL_SCANCODE_GRAVE) {
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) GXPC_OverlayToggle();
            continue;
        }
        // F9 opens and closes the cheat menu and is kept from the pad layer
        if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && ev.key.keysym.scancode == SDL_SCANCODE_F9) {
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                if (warpMenuVisible()) warpMenuToggle();
                cheatMenuToggle();
                syncMenuInput();
            }
            continue;
        }
        // F8 or a controller's Back (Select) button opens the warp menu while
        // the debug overlay is open, and closes it; a press that does either
        // is kept from the pad layer, release included. Otherwise they reach
        // the pad layer as any key or button does (PAD_BACK can be bound).
        const bool warpKey = (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && ev.key.keysym.scancode == SDL_SCANCODE_F8;
        const bool warpButton = (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP) &&
                                ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK;
        if (warpKey || warpButton) {
            bool& held = s_warpToggleHeld[warpButton];
            const bool down = ev.type == SDL_KEYDOWN || ev.type == SDL_CONTROLLERBUTTONDOWN;
            const bool repeat = warpKey && ev.key.repeat;
            if (down && !repeat && (warpMenuVisible() || GXPC_OverlayVisible())) {
                if (cheatMenuVisible()) cheatMenuToggle();
                warpMenuToggle();
                syncMenuInput();
                held = true;
                continue;
            }
            if (held) {
                if (!down) held = false;
                continue;
            }
        }
        if (s_mouseCamera && !mouseFree()) {
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) captureMouse(false);
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED && !s_mouseReleased)
                captureMouse(true);
            if (ev.type == SDL_MOUSEBUTTONDOWN && !s_mouseCaptured) {
                s_mouseReleased = false;
                captureMouse(true);
                showReleasedCursor();
                continue;
            }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_F10) {
                if (!ev.key.repeat) {
                    s_mouseReleased = s_mouseCaptured;
                    captureMouse(!s_mouseCaptured);
                    showReleasedCursor();
                }
                continue;
            }
        }
        // F11 or Alt+Enter toggles fullscreen and is kept from the pad layer
        if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) &&
            (ev.key.keysym.scancode == SDL_SCANCODE_F11 ||
             ((ev.key.keysym.scancode == SDL_SCANCODE_RETURN || ev.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) &&
              (ev.key.keysym.mod & KMOD_ALT)))) {
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat) setFullscreen(!s_isFullscreen);
            continue;
        }
        // F7 with the overlay open cycles the game speed
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_F7 && GXPC_OverlayVisible()) {
            if (!ev.key.repeat) GXPC_CycleSpeed();
            continue;
        }
        // The menus' own keys, buttons and clicks never reach the pad layer (Escape would quit)
        if (cheatMenuHandleEvent(ev) || warpMenuHandleEvent(ev)) {
            syncMenuInput();
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
    warpMenuTick();
#endif
}

// Rumble on the controller in use (both motors at one strength). A call that
// changes nothing sends nothing, as the game sets its motor every frame; a
// rumble still on is sent again each second, each send lasting two, so a motor
// left on by a stalled game stops by itself.
int sms_gx_rumble(unsigned short strength) {
#ifdef SMS_GX_HAVE_SDL2
    SDL_GameController* c = s_rumblePad >= 0 ? SDL_GameControllerFromInstanceID(s_rumblePad) : nullptr;
    if (s_rumbling >= 0 && s_rumbling != s_rumblePad) {  // the player picked up another controller
        if (SDL_GameController* old = SDL_GameControllerFromInstanceID(s_rumbling))
            SDL_GameControllerRumble(old, 0, 0, 0);
        s_rumbling = -1;
        s_rumbleSent = 0;
    }
    if (!c) return 0;
    const Uint32 now = SDL_GetTicks();
    if (strength == s_rumbleSent && (!strength || now - s_rumbleSentAt < 1000)) return 1;
    s_rumbleSent = strength;
    s_rumbleSentAt = now;
    s_rumbling = strength ? s_rumblePad : -1;
    return SDL_GameControllerRumble(c, strength, strength, strength ? 2000 : 0) == 0;
#else
    (void)strength;
    return 0;
#endif
}

}  // extern "C"
