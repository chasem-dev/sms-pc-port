// Extensions' text in the window (gx_ext_overlay.h), drawn with the debug
// overlay's renderer: the notice ("Connecting to 1.2.3.4…") centred near the
// top, and the status panel (who is playing) in the top right corner. Each is
// one or more lines; an empty text hides it.
#include "gx_ext_overlay.h"
#include "gx_internal.h"

#include "sms_gx/gx_pc.h"

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

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t nl = text.find('\n', start);
        out.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
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
        const std::vector<std::string> rows = lines(text);
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
    const int margin = winH / 30;
    // GL's window rows count from the bottom
    const int x = index == GXPC_EXT_OVERLAY_NOTICE ? (winW - p.w) / 2 : winW - p.w - margin;
    const int y = index == GXPC_EXT_OVERLAY_NOTICE ? winH - p.h - winH / 6 : winH - p.h - winH / 4;
    gx::drawOverlayPanel(slot, changed ? p.pixels.data() : nullptr, p.w, p.h, x, y, 1, winW, winH);
}

}  // namespace

extern "C" void GXPC_ExtOverlaySet(int panel, const char* text) {
    if (panel < 0 || panel >= GXPC_EXT_OVERLAY_PANELS) return;
    std::lock_guard<std::mutex> hold(s_lock);
    s_panels[panel].text = text ? text : "";
}

namespace gx {

void extOverlayRender(int winW, int winH) {
    if (winW <= 0 || winH <= 0) return;
    renderPanel(GXPC_EXT_OVERLAY_NOTICE, OVERLAY_EXT_NOTICE, winW, winH);
    renderPanel(GXPC_EXT_OVERLAY_STATUS, OVERLAY_EXT_STATUS, winW, winH);
}

}  // namespace gx
