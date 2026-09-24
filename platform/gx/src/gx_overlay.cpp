// Debug overlay: backtick (`) in the window toggles a panel with frame rate,
// frame times, sms_gx counters, the GL renderer and the default key bindings.
// Text is rasterised on the CPU with stb_easy_font and blended on top of the
// presented frame (GXPC_DrawOverlay), so it touches no game-visible GL state.
#include "gx_internal.h"
#include "gl_funcs.h"
#include "sms_gx/gx_pc.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

#include "third_party/stb_easy_font.h"

namespace {

bool s_visible = false;
const int kSpeeds[] = {1, 2, 4, 10};
int s_speedIndex = 0;

double nowSeconds() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}

// Frame timing over a sliding one-second window, refreshed once a second so
// the numbers are readable.
struct FrameClock {
    double start = 0, windowStart = 0, last = 0, gxAtWindow = 0;
    int frames = 0;
    double worst = 0;
    // shown values
    double fps = 0, avgMs = 0, maxMs = 0, gxMs = 0;
    uint32_t total = 0;

    void tick() {
        double t = nowSeconds();
        if (start == 0) {
            start = windowStart = last = t;
            gxAtWindow = GXPC_GxSeconds();
            return;
        }
        double dt = t - last;
        last = t;
        if (dt > worst) worst = dt;
        frames++;
        total++;
        double span = t - windowStart;
        if (span >= 1.0) {
            double gx = GXPC_GxSeconds();
            fps = frames / span;
            avgMs = span * 1000.0 / frames;
            maxMs = worst * 1000.0;
            gxMs = (gx - gxAtWindow) * 1000.0 / frames;
            gxAtWindow = gx;
            windowStart = t;
            frames = 0;
            worst = 0;
        }
    }
} s_clock;

std::string s_renderer;

void fillRect(std::vector<uint8_t>& px, int w, int h, int x0, int y0, int x1, int y1, const uint8_t c[4]) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) memcpy(&px[(size_t(y) * w + x) * 4], c, 4);
}

// stb_easy_font emits axis-aligned quads (4 vertices of x, y, z, rgba).
void drawText(std::vector<uint8_t>& px, int w, int h, int x, int y, const char* text, const uint8_t c[4]) {
    static char buf[256 * 1024];  // 64 bytes per quad
    int quads = stb_easy_font_print(float(x), float(y), const_cast<char*>(text), nullptr, buf, sizeof buf);
    for (int q = 0; q < quads; q++) {
        const float* v = reinterpret_cast<const float*>(buf + q * 64);
        float minX = v[0], maxX = v[0], minY = v[1], maxY = v[1];
        for (int i = 1; i < 4; i++) {
            const float* p = reinterpret_cast<const float*>(buf + q * 64 + i * 16);
            if (p[0] < minX) minX = p[0];
            if (p[0] > maxX) maxX = p[0];
            if (p[1] < minY) minY = p[1];
            if (p[1] > maxY) maxY = p[1];
        }
        fillRect(px, w, h, int(minX + 0.5f), int(minY + 0.5f), int(maxX + 0.5f), int(maxY + 0.5f), c);
    }
}

}  // namespace

extern "C" {

void GXPC_OverlayToggle(void) { s_visible = !s_visible; }
int GXPC_OverlayVisible(void) { return s_visible; }

void GXPC_CycleSpeed(void) { s_speedIndex = (s_speedIndex + 1) % int(sizeof kSpeeds / sizeof kSpeeds[0]); }
int GXPC_GetSpeed(void) { return kSpeeds[s_speedIndex]; }

void GXPC_OverlayDraw(int winW, int winH) {
    s_clock.tick();
    if (!s_visible) return;
    if (s_renderer.empty()) {
        const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        s_renderer = r ? r : "?";
        if (s_renderer.size() > 44) s_renderer.resize(44);
    }
    GXPCStats st;
    GXPC_GetLastFrameStats(&st);
    double up = s_clock.last - s_clock.start;

    char text[2048];
    snprintf(text, sizeof text,
             "FPS %.1f   frame %.1f ms avg, %.1f ms worst   speed x%d\n"
             "sms_gx %.1f ms/frame\n"
             "draws %u   vertices %u   EFB copies %u\n"
             "texture uploads %u   shader compiles %u\n"
             "frames %u   up %d:%02d\n"
             "window %dx%d\n"
             "GL %s\n"
             "\n"
             "KEYS (defaults, see bindings.txt)\n"
             "Stick: arrows or WASD (hold LCtrl for half)\n"
             "C-stick: I J K L\n"
             "A: Space or X     B: Shift or C\n"
             "X: V     Y: F     Z: Z\n"
             "L: Q     R: E     Start: Enter\n"
             "D-pad: 1 2 3 4\n"
             "`: this overlay     F7: speed x1/x2/x4/x10\n"
             "Esc: quit",
             s_clock.fps, s_clock.avgMs, s_clock.maxMs, GXPC_GetSpeed(), s_clock.gxMs, st.draws, st.vertices, st.efbCopies,
             st.textureUploads, st.shaderCompiles, s_clock.total, int(up) / 60, int(up) % 60, winW, winH,
             s_renderer.c_str());

    const int pad = 4;
    int w = stb_easy_font_width(text) + pad * 2;
    int h = stb_easy_font_height(text) + pad * 2;
    std::vector<uint8_t> px(size_t(w) * h * 4);
    const uint8_t bg[4] = {16, 16, 24, 128};
    const uint8_t fg[4] = {255, 255, 255, 255};
    const uint8_t hi[4] = {255, 220, 64, 255};
    fillRect(px, w, h, 0, 0, w, h, bg);
    // first line (the frame rate) highlighted, the rest plain
    const char* nl = strchr(text, '\n');
    std::string first(text, nl ? size_t(nl - text) : strlen(text));
    drawText(px, w, h, pad, pad, first.c_str(), hi);
    if (nl) drawText(px, w, h, pad, pad + 12, nl + 1, fg);

    int scale = winH >= 720 ? 3 : 2;
    while (scale > 1 && (w * scale > winW || h * scale > winH)) scale--;
    GXPC_DrawOverlay(px.data(), w, h, 8, 8, scale, winW, winH);
}

}  // extern "C"
