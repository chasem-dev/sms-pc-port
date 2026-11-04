// Split-screen co-op's card (ext/coop, gx_coop_overlay.h): a panel near the
// top of the game's picture, or of player 2's view while the screen is split,
// that tells players how to join, leave and take over: a badge in the
// player's colour, a title, up to two lines whose [WORD]s are drawn as the
// controller's buttons, and a bar for a held button or a countdown. Set from
// the game's thread each frame with GXPC_CoopCard; it slides in when its text
// changes and fades out when hidden. Drawn with the debug overlay's renderer.
#include "gx_coop_overlay.h"
#include "gx_internal.h"

#include "sms_gx/gx_pc.h"

#include <math.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

namespace {

const int kCapH = 7;  // stb_easy_font's capitals are 7 font pixels high
const uint8_t kPanelBg[4] = {12, 14, 18, 228};
const uint8_t kTitle[4] = {255, 255, 255, 255};
const uint8_t kBody[4] = {214, 218, 224, 255};
const uint8_t kKeyBg[4] = {240, 242, 245, 255};
const uint8_t kKeyText[4] = {20, 22, 26, 255};
const uint8_t kTrack[4] = {255, 255, 255, 40};
const double kFadeIn = 0.18, kFadeOut = 0.3;

std::mutex s_lock;
GXPCCoopCard s_card;  // as last set
bool s_shown;         // a card is set
double s_since;       // when its text last changed (the slide in)
double s_hiddenAt;    // when it was hidden (the fade out)
GXPCCoopCard s_last;  // the last card shown, kept for its fade out

std::vector<uint8_t> s_pixels;

double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool sameText(const GXPCCoopCard& a, const GXPCCoopCard& b) {
    return !strcmp(a.title, b.title) && !strcmp(a.lines[0], b.lines[0]) && !strcmp(a.lines[1], b.lines[1]) &&
           !strcmp(a.badge, b.badge) && a.view == b.view && a.accent == b.accent;
}

// Over what the bitmap has: colour `c` with coverage `cover` (0..1).
void blend(std::vector<uint8_t>& px, int w, int x, int y, const uint8_t c[4], float cover) {
    if (cover <= 0) return;
    uint8_t* o = &px[(size_t(y) * w + x) * 4];
    const float a = c[3] / 255.0f * std::min(1.0f, cover);
    const float oa = o[3] / 255.0f;
    const float outA = a + oa * (1 - a);
    if (outA <= 0) return;
    for (int k = 0; k < 3; k++) o[k] = uint8_t((c[k] * a + o[k] * oa * (1 - a)) / outA + 0.5f);
    o[3] = uint8_t(outA * 255 + 0.5f);
}

// A rectangle with round corners, smooth edged.
void roundRect(std::vector<uint8_t>& px, int w, int h, float x0, float y0, float x1, float y1, float r, const uint8_t c[4]) {
    r = std::min(r, std::min(x1 - x0, y1 - y0) / 2);
    for (int y = std::max(0, int(y0)); y < std::min(h, int(ceilf(y1))); y++)
        for (int x = std::max(0, int(x0)); x < std::min(w, int(ceilf(x1))); x++) {
            const float cx = x + 0.5f, cy = y + 0.5f;
            const float dx = std::max(0.0f, std::max(x0 + r - cx, cx - (x1 - r)));
            const float dy = std::max(0.0f, std::max(y0 + r - cy, cy - (y1 - r)));
            float cover = r > 0 ? r + 0.5f - sqrtf(dx * dx + dy * dy) : 1.0f;
            // the straight edges' partial pixels
            cover = std::min(cover, std::min(cx - x0 + 0.5f, x1 - cx + 0.5f));
            cover = std::min(cover, std::min(cy - y0 + 0.5f, y1 - cy + 0.5f));
            blend(px, w, x, y, c, std::max(0.0f, std::min(1.0f, cover)));
        }
}

// Text over what the bitmap has (overlayText copies: drawn on a blank copy, then blended).
void text(std::vector<uint8_t>& px, int w, int h, int x, int y, const char* s, const uint8_t c[4], int scale) {
    const int tw = gx::overlayTextWidth(s) * scale + scale, th = 12 * scale;
    if (tw <= 0) return;
    static std::vector<uint8_t> scratch;
    scratch.assign(size_t(tw) * th * 4, 0);
    gx::overlayText(scratch, tw, th, 0, 0, s, c, scale);
    for (int yy = 0; yy < th; yy++)
        for (int xx = 0; xx < tw; xx++) {
            const uint8_t* p = &scratch[(size_t(yy) * tw + xx) * 4];
            if (p[3] && x + xx >= 0 && x + xx < w && y + yy >= 0 && y + yy < h) blend(px, w, x + xx, y + yy, p, 1.0f);
        }
}

// A line's parts: text, and [WORD]s drawn as buttons.
struct Part {
    std::string s;
    bool key;
};
std::vector<Part> parts(const char* line) {
    std::vector<Part> out;
    std::string cur;
    for (const char* p = line; *p; p++) {
        const char* end = *p == '[' ? strchr(p, ']') : nullptr;
        if (end && end > p + 1) {
            if (!cur.empty()) out.push_back({cur, false}), cur.clear();
            out.push_back({std::string(p + 1, end), true});
            p = end;
        } else {
            cur += *p;
        }
    }
    if (!cur.empty()) out.push_back({cur, false});
    return out;
}
int keyPad(int g) { return 3 * g; }
int lineWidth(const char* line, int g) {
    int w = 0;
    for (const Part& p : parts(line)) w += gx::overlayTextWidth(p.s.c_str()) * g + (p.key ? 2 * keyPad(g) : 0);
    return w;
}
void drawLine(std::vector<uint8_t>& px, int w, int h, int x, int y, const char* line, int g) {
    const int keyH = (kCapH + 6) * g;  // a button's height; text sits in its middle
    for (const Part& p : parts(line)) {
        const int tw = gx::overlayTextWidth(p.s.c_str()) * g;
        if (p.key) {
            roundRect(px, w, h, x, y, x + tw + 2 * keyPad(g), y + keyH, 2.5f * g, kKeyBg);
            text(px, w, h, x + keyPad(g), y + 3 * g, p.s.c_str(), kKeyText, g);
            x += tw + 2 * keyPad(g);
        } else {
            text(px, w, h, x, y + 3 * g, p.s.c_str(), kBody, g);
            x += tw;
        }
    }
}

}  // namespace

extern "C" void GXPC_CoopCard(const GXPCCoopCard* card) {
    std::lock_guard<std::mutex> lock(s_lock);
    if (!card) {
        if (s_shown) s_hiddenAt = now();
        s_shown = false;
        return;
    }
    if (!s_shown || !sameText(*card, s_card)) s_since = now();
    s_card = *card;
    s_card.title[sizeof s_card.title - 1] = 0;
    s_card.lines[0][sizeof s_card.lines[0] - 1] = s_card.lines[1][sizeof s_card.lines[1] - 1] = 0;
    s_card.badge[sizeof s_card.badge - 1] = 0;
    s_shown = true;
}

namespace gx {

void coopOverlayRender(int winW, int winH) {
    GXPCCoopCard card;
    double since, hiddenAt;
    bool shown;
    {
        std::lock_guard<std::mutex> lock(s_lock);
        shown = s_shown;
        since = s_since;
        hiddenAt = s_hiddenAt;
        if (shown) s_last = s_card;
        card = s_last;
    }
    if (winW <= 0 || winH <= 0 || (!card.title[0] && !card.lines[0][0])) return;
    const double t = now();
    float alpha = 1, slide = 0;
    if (shown) {
        const float in = float(std::min(1.0, (t - since) / kFadeIn));
        alpha = in;
        slide = (1 - in) * (1 - in);
    } else {
        const double out = t - hiddenAt;
        if (out >= kFadeOut) return;
        alpha = float(1 - out / kFadeOut);
    }

    // sized to the window, a step larger than the save state messages
    const int g = std::max(2, (winH + 360) / 640);
    const int tg = g + 1;  // the title's
    const int pad = 6 * g, badge = 20 * g, gap = 6 * g;
    const int lineH = (kCapH + 6) * g, lineGap = 4 * g;
    const int nLines = (card.lines[0][0] ? 1 : 0) + (card.lines[1][0] ? 1 : 0);
    int textW = overlayTextWidth(card.title) * tg;
    for (int i = 0; i < 2; i++)
        if (card.lines[i][0]) textW = std::max(textW, lineWidth(card.lines[i], g));
    const bool bar = card.progress >= 0;
    const int barH = 3 * g;
    const int textH = kCapH * tg + nLines * (lineGap + lineH);
    const int bodyH = std::max(badge, textH);
    const int w = pad + badge + gap + textW + pad + g;
    const int h = pad + bodyH + (bar ? 4 * g + barH : 0) + pad;
    s_pixels.assign(size_t(w) * h * 4, 0);
    roundRect(s_pixels, w, h, 0, 0, w, h, 5.0f * g, kPanelBg);
    // the badge: a disc in the player's colour, their number on it
    const uint8_t accent[4] = {uint8_t(card.accent >> 24), uint8_t(card.accent >> 16), uint8_t(card.accent >> 8), 255};
    const float bx = pad + badge / 2.0f, by = pad + bodyH / 2.0f;
    for (int y = int(by - badge / 2.0f) - 1; y <= int(by + badge / 2.0f) + 1; y++)
        for (int x = int(bx - badge / 2.0f) - 1; x <= int(bx + badge / 2.0f) + 1; x++) {
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            const float d = sqrtf((x + 0.5f - bx) * (x + 0.5f - bx) + (y + 0.5f - by) * (y + 0.5f - by));
            blend(s_pixels, w, x, y, accent, badge / 2.0f + 0.5f - d);
        }
    if (card.badge[0]) {
        const int bw = overlayTextWidth(card.badge) * tg;
        text(s_pixels, w, h, int(bx - bw / 2.0f + 0.5f), int(by - kCapH * tg / 2.0f + 0.5f), card.badge, kTitle, tg);
    }
    // the title and lines
    const int tx = pad + badge + gap;
    int ty = pad + (bodyH - textH) / 2;
    text(s_pixels, w, h, tx, ty, card.title, kTitle, tg);
    ty += kCapH * tg;
    for (int i = 0; i < 2; i++)
        if (card.lines[i][0]) {
            ty += lineGap;
            drawLine(s_pixels, w, h, tx, ty, card.lines[i], g);
            ty += lineH;
        }
    // the bar: a held button's progress, or the time left
    if (bar) {
        const int y0 = pad + bodyH + 4 * g, x0 = pad, x1 = w - pad;
        roundRect(s_pixels, w, h, x0, y0, x1, y0 + barH, g, kTrack);
        const float p = std::max(0.0f, std::min(1.0f, card.progress));
        if (p > 0) roundRect(s_pixels, w, h, x0, y0, x0 + (x1 - x0) * p, y0 + barH, g, accent);
    }
    if (alpha < 1)
        for (size_t i = 3; i < s_pixels.size(); i += 4) s_pixels[i] = uint8_t(s_pixels[i] * alpha);

    // near the top of the game's picture, or of player 2's (or 1's) view
    const PresentRect pic = lastPresentRect();
    int rx = 0, ry = 0, rw = winW, rh = winH;
    if (pic.w > 0 && pic.h > 0) rx = pic.x, ry = pic.y, rw = pic.w, rh = pic.h;
    if (card.view == 1 || card.view == 2) {
        const bool second = card.view == 1;
        if (card.stacked) {
            rh /= 2;
            if (second) ry += rh;
        } else {
            rw /= 2;
            if (second) rx += rw;
        }
    }
    const int x = rx + (rw - w) / 2;
    // below the lives (the HUD's top middle) on the whole picture; near the top of player 2's
    // view, and in player 1's below the shared counters (drawn where the game has them)
    const int top = card.view == 1 ? rh / 9 : card.view == 2 ? rh * (card.stacked ? 40 : 36) / 100 : rh * 19 / 100;
    const int y = ry + top - int(slide * 10 * g);
    drawOverlayPanel(OVERLAY_COOP, s_pixels.data(), w, h, x, y, 1, winW, winH);
}

}  // namespace gx
