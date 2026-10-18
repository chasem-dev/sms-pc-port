// Extensions' text in the window (gx_ext_overlay.h), drawn with the debug
// overlay's renderer: the notice ("Connecting to 1.2.3.4...") centred near the
// top, and the status panel (who is playing) at the right. Each is one or
// more lines, wrapped to the window; an empty text hides it. And labels on
// the game's picture: the names over other players. The font has ASCII only.
#include "gx_ext_overlay.h"
#include "gx_internal.h"

#include "sms_gx/gx_pc.h"

#include <math.h>
#include <string.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace {

const int kCapH = 7;  // stb_easy_font's capitals are 7 font pixels high
const int kLineH = 12;
const uint8_t kPanelBg[4] = {15, 15, 15, 220};
const uint8_t kText[4] = {255, 255, 255, 255};
const uint8_t kAccent[4] = {250, 190, 40, 255};

struct Panel {
    std::string text;      // set by any thread
    std::string shown;     // what the bitmap holds (GL thread)
    std::vector<uint8_t> pixels;
    int w = 0, h = 0, glyph = 0;
};

std::mutex s_lock;
Panel s_panels[GXPC_EXT_OVERLAY_PANELS];

// The names over players: the list as last set (any thread), and each slot's
// bitmap as drawn (GL thread), made again when its text, size or fade changes.
GXPCLabel s_labels[GXPC_MAX_LABELS];
int s_labelCount = 0;
struct LabelBitmap {
    std::string text;
    int glyph = 0, fade = -1, w = 0, h = 0;
    std::vector<uint8_t> pixels;
};
LabelBitmap s_labelBitmaps[GXPC_MAX_LABELS];
const int kLabelFades = 16;  // fade steps (a bitmap each)

// The text's lines, each wrapped at spaces to at most `maxWidth` font pixels.
std::vector<std::string> lines(const std::string& text, int maxWidth) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        while (gx::overlayTextWidth(line.c_str()) > maxWidth) {
            size_t cut = line.size();
            // the last space that leaves a line short enough
            while (cut > 0) {
                cut = line.rfind(' ', cut - 1);
                if (cut == std::string::npos || cut == 0) break;
                if (gx::overlayTextWidth(line.substr(0, cut).c_str()) <= maxWidth) break;
            }
            if (cut == std::string::npos || cut == 0) break;  // one long word: leave it
            out.push_back(line.substr(0, cut));
            line = line.substr(cut + 1);
        }
        out.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

void renderPanel(int index, gx::OverlaySlot slot, int winW, int winH) {
    Panel& p = s_panels[index];
    std::string text;
    {
        std::lock_guard<std::mutex> hold(s_lock);
        text = p.text;
    }
    if (text.empty()) {
        p.shown.clear();
        return;
    }
    const int glyph = std::max(2, (winH + 360) / 720);
    const bool changed = text != p.shown || glyph != p.glyph;
    if (changed) {
        p.shown = text;
        p.glyph = glyph;
        const std::vector<std::string> rows = lines(text, winW * 7 / 10 / glyph);
        const int pad = 6 * glyph;
        int width = 0;
        for (const std::string& r : rows) width = std::max(width, gx::overlayTextWidth(r.c_str()));
        p.w = width * glyph + 2 * pad;
        p.h = (kCapH + kLineH * int(rows.size() - 1)) * glyph + 2 * pad;
        p.pixels.assign(size_t(p.w) * p.h * 4, 0);
        gx::overlayFillRect(p.pixels, p.w, p.h, 0, 0, p.w, p.h, kPanelBg);
        gx::overlayFillRect(p.pixels, p.w, p.h, 0, 0, 2 * glyph, p.h, kAccent);
        for (size_t i = 0; i < rows.size(); i++)
            gx::overlayText(p.pixels, p.w, p.h, pad, pad + int(i) * kLineH * glyph, rows[i].c_str(), kText, glyph);
    }
    // the notice centred just below the HUD's top row (the lives), the status
    // panel at the right below the counters (rows count from the top)
    const int margin = winH / 30;
    const int x = index == GXPC_EXT_OVERLAY_NOTICE ? (winW - p.w) / 2 : winW - p.w - margin;
    const int y = index == GXPC_EXT_OVERLAY_NOTICE ? winH * 17 / 100 : winH * 27 / 100;
    gx::drawOverlayPanel(slot, changed ? p.pixels.data() : nullptr, p.w, p.h, x, y, 1, winW, winH);
}

// A name: white on a dark rounded pill, its bottom centre at the label's
// place, fading out by the label's alpha.
void renderLabels(int winW, int winH) {
    GXPCLabel labels[GXPC_MAX_LABELS];
    int count;
    {
        std::lock_guard<std::mutex> hold(s_lock);
        count = s_labelCount;
        memcpy(labels, s_labels, sizeof labels);
    }
    const gx::PresentRect r = gx::lastPresentRect();
    if (r.w <= 0 || r.h <= 0) return;
    const int glyph = std::max(2, (r.h + 270) / 540);  // font pixels: 2 up to 1080 lines, then more
    for (int i = 0; i < count; i++) {
        const GXPCLabel& l = labels[i];
        const int fade = std::max(0, std::min(kLabelFades, int(l.alpha * kLabelFades + 0.5f)));
        if (!fade || !l.text[0]) continue;
        LabelBitmap& b = s_labelBitmaps[i];
        std::string text(l.text, strnlen(l.text, sizeof l.text));
        const bool changed = text != b.text || glyph != b.glyph || fade != b.fade;
        if (changed) {
            b.text = text;
            b.glyph = glyph;
            b.fade = fade;
            const int padX = 4 * glyph, padY = 3 * glyph;
            b.w = gx::overlayTextWidth(text.c_str()) * glyph + 2 * padX;
            b.h = kCapH * glyph + 2 * padY;
            b.pixels.assign(size_t(b.w) * b.h * 4, 0);
            const uint8_t bg[4] = {12, 20, 28, uint8_t(170 * fade / kLabelFades)};
            const uint8_t fg[4] = {255, 255, 255, uint8_t(255 * fade / kLabelFades)};
            // rounded ends: the corners' texels left clear
            const int round = std::min(b.h / 2, 3 * glyph);
            for (int y = 0; y < b.h; y++) {
                const int dy = y < round ? round - y : y >= b.h - round ? y - (b.h - round - 1) : 0;
                const int inset = dy ? round - int(sqrtf(float(round * round - dy * dy)) + 0.5f) : 0;
                gx::overlayFillRect(b.pixels, b.w, b.h, inset, y, b.w - inset, y + 1, bg);
            }
            gx::overlayText(b.pixels, b.w, b.h, padX, padY, text.c_str(), fg, glyph);
        }
        const int x = r.x + int(l.x * float(r.w)) - b.w / 2;
        const int y = r.y + int(l.y * float(r.h)) - b.h;
        if (x + b.w < 0 || y + b.h < 0 || x > winW || y > winH) continue;
        gx::drawOverlayPanel(gx::OverlaySlot(gx::OVERLAY_EXT_LABEL + i), changed ? b.pixels.data() : nullptr, b.w, b.h, x, y,
                             1, winW, winH);
    }
}

}  // namespace

extern "C" void GXPC_ExtOverlayLabels(const GXPCLabel* labels, int count) {
    std::lock_guard<std::mutex> hold(s_lock);
    s_labelCount = labels ? std::max(0, std::min(count, GXPC_MAX_LABELS)) : 0;
    if (s_labelCount) memcpy(s_labels, labels, sizeof(GXPCLabel) * size_t(s_labelCount));
}

extern "C" void GXPC_ExtOverlaySet(int panel, const char* text) {
    if (panel < 0 || panel >= GXPC_EXT_OVERLAY_PANELS) return;
    std::lock_guard<std::mutex> hold(s_lock);
    s_panels[panel].text = text ? text : "";
}

namespace gx {

void extOverlayRender(int winW, int winH) {
    if (winW <= 0 || winH <= 0) return;
    renderLabels(winW, winH);
    renderPanel(GXPC_EXT_OVERLAY_NOTICE, OVERLAY_EXT_NOTICE, winW, winH);
    renderPanel(GXPC_EXT_OVERLAY_STATUS, OVERLAY_EXT_STATUS, winW, winH);
}

}  // namespace gx
