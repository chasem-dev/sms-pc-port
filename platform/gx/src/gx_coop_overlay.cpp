// Split-screen co-op's card (ext/coop, gx_coop_overlay.h): a panel near the
// top of the game's picture, or of a player's view while the screen is split,
// that tells players how to join, leave and take over: a badge in the
// player's colour, a title, up to two lines whose [WORD]s are drawn as the
// controller's buttons, and a bar for a held button or a countdown. Set from
// the game's thread each frame with GXPC_CoopCard; it slides in when its text
// changes and fades out when hidden. Drawn in Inter (the launcher's typeface,
// third_party/inter_ascii.h) with stb_truetype, over a soft shadow.
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

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"
#include "third_party/inter_ascii.h"

namespace {

struct Rgba {
    float r, g, b, a;  // 0..255, a 0..1
};
const Rgba kPanel = {18, 20, 26, 0.88f};
const Rgba kBorder = {255, 255, 255, 0.09f};
const Rgba kShadow = {0, 0, 0, 0.45f};
const Rgba kTitle = {255, 255, 255, 1};
const Rgba kBody = {255, 255, 255, 0.74f};
const Rgba kKey = {242, 244, 247, 1};
const Rgba kKeyText = {17, 19, 24, 1};
const Rgba kTrack = {255, 255, 255, 0.12f};
const double kFadeIn = 0.2, kFadeOut = 0.3;

std::mutex s_lock;
GXPCCoopCard s_card;  // as last set
bool s_shown;         // a card is set
double s_since;       // when its text last changed (the slide in)
double s_hiddenAt;    // when it was hidden (the fade out)
GXPCCoopCard s_last;  // the last card shown, kept for its fade out

std::vector<uint8_t> s_pixels;  // premultiplied while drawn, straight when handed over

double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool sameText(const GXPCCoopCard& a, const GXPCCoopCard& b) {
    return !strcmp(a.title, b.title) && !strcmp(a.lines[0], b.lines[0]) && !strcmp(a.lines[1], b.lines[1]) &&
           !strcmp(a.badge, b.badge) && a.view == b.view && a.accent == b.accent;
}

// ---------------------------------------------------------------- the bitmap

struct Canvas {
    std::vector<uint8_t>& px;
    int w, h;
    // Over what is there: `c` with `cover` (0..1), premultiplied.
    void blend(int x, int y, const Rgba& c, float cover) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const float a = c.a * std::max(0.0f, std::min(1.0f, cover));
        if (a <= 0) return;
        uint8_t* o = &px[(size_t(y) * w + x) * 4];
        o[0] = uint8_t(c.r * a + o[0] * (1 - a) + 0.5f);
        o[1] = uint8_t(c.g * a + o[1] * (1 - a) + 0.5f);
        o[2] = uint8_t(c.b * a + o[2] * (1 - a) + 0.5f);
        o[3] = uint8_t(255 * a + o[3] * (1 - a) + 0.5f);
    }
    // A rounded rectangle by its signed distance: smooth edged, or with a
    // `soft` blur (shadows); `ring` > 0 draws only a border that wide.
    void rect(float x0, float y0, float x1, float y1, float r, const Rgba& c, float soft = 0, float ring = 0) {
        const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = (x1 - x0) / 2, hh = (y1 - y0) / 2;
        r = std::min(r, std::min(hw, hh));
        const int pad = int(ceilf(soft)) + 1;
        for (int y = std::max(0, int(y0) - pad); y < std::min(h, int(ceilf(y1)) + pad); y++)
            for (int x = std::max(0, int(x0) - pad); x < std::min(w, int(ceilf(x1)) + pad); x++) {
                const float qx = fabsf(x + 0.5f - cx) - (hw - r), qy = fabsf(y + 0.5f - cy) - (hh - r);
                const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
                const float d = sqrtf(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;  // < 0 inside
                float cover;
                if (soft > 0) {
                    const float t = std::max(0.0f, std::min(1.0f, (d + soft) / (2 * soft)));
                    cover = 1 - t * t * (3 - 2 * t);
                } else if (ring > 0) {
                    cover = std::min(0.5f - d, d + ring + 0.5f);
                } else {
                    cover = 0.5f - d;
                }
                blend(x, y, c, cover);
            }
    }
};

// ---------------------------------------------------------------- text

stbtt_fontinfo s_medium, s_bold;
bool s_fontsReady;

bool fonts() {
    if (!s_fontsReady)
        s_fontsReady = stbtt_InitFont(&s_medium, kInterMedium, 0) && stbtt_InitFont(&s_bold, kInterBold, 0);
    return s_fontsReady;
}

struct Face {
    const stbtt_fontinfo* font;
    float scale;  // font units to pixels
    int px;       // its size in pixels (the em)
};
Face face(bool bold, int px) {
    Face f;
    f.font = bold ? &s_bold : &s_medium;
    f.px = px;
    f.scale = stbtt_ScaleForMappingEmToPixels(f.font, float(px));
    return f;
}

// The text's code points: ASCII, and UTF-8's · ’ … (all the font has).
std::vector<int> codepoints(const std::string& s) {
    std::vector<int> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        int cp = c, n = 1;
        if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F), n = 2;
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size())
            cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F), n = 3;
        out.push_back(cp);
        i += n;
    }
    return out;
}

float textWidth(const Face& f, const std::string& s) {
    float w = 0;
    const std::vector<int> cps = codepoints(s);
    for (size_t i = 0; i < cps.size(); i++) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(f.font, cps[i], &adv, &lsb);
        w += adv * f.scale;
        if (i + 1 < cps.size()) w += stbtt_GetCodepointKernAdvance(f.font, cps[i], cps[i + 1]) * f.scale;
    }
    return w;
}

// The cap height in pixels (an H's top above the baseline).
float capHeight(const Face& f) {
    int x0, y0, x1, y1;
    stbtt_GetCodepointBox(f.font, 'H', &x0, &y0, &x1, &y1);
    return y1 * f.scale;
}

// Text with its baseline at `baseline`.
void drawText(Canvas& cv, const Face& f, float x, float baseline, const std::string& s, const Rgba& c) {
    static std::vector<unsigned char> glyph;
    const std::vector<int> cps = codepoints(s);
    const int by = int(floorf(baseline + 0.5f));
    for (size_t i = 0; i < cps.size(); i++) {
        const float sx = x - floorf(x);
        int gx0, gy0, gx1, gy1;
        stbtt_GetCodepointBitmapBoxSubpixel(f.font, cps[i], f.scale, f.scale, sx, 0, &gx0, &gy0, &gx1, &gy1);
        const int gw = gx1 - gx0, gh = gy1 - gy0;
        if (gw > 0 && gh > 0) {
            glyph.assign(size_t(gw) * gh, 0);
            stbtt_MakeCodepointBitmapSubpixel(f.font, glyph.data(), gw, gh, gw, f.scale, f.scale, sx, 0, cps[i]);
            const int ox = int(floorf(x)) + gx0, oy = by + gy0;
            for (int yy = 0; yy < gh; yy++)
                for (int xx = 0; xx < gw; xx++)
                    if (glyph[size_t(yy) * gw + xx]) cv.blend(ox + xx, oy + yy, c, glyph[size_t(yy) * gw + xx] / 255.0f);
        }
        int adv, lsb;
        stbtt_GetCodepointHMetrics(f.font, cps[i], &adv, &lsb);
        x += adv * f.scale;
        if (i + 1 < cps.size()) x += stbtt_GetCodepointKernAdvance(f.font, cps[i], cps[i + 1]) * f.scale;
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

// The card's measures, in pixels, for a picture `picH` high.
struct Metrics {
    Face title, body, key;
    float padX, padY, badge, gap, lineGap, titleGap, radius, keyPadX, keyH, keyR, keyGap, barH, barGap, shadow, shadowY;
    explicit Metrics(int picH) {
        const float s = std::max(0.6f, picH / 1080.0f);
        title = face(true, int(lroundf(25 * s)));
        body = face(false, int(lroundf(19 * s)));
        key = face(true, int(lroundf(14 * s)));
        padX = 22 * s, padY = 18 * s, badge = 38 * s, gap = 16 * s;
        titleGap = 10 * s, lineGap = 7 * s, radius = 16 * s;
        keyPadX = 8 * s, keyH = 25 * s, keyR = 7 * s, keyGap = 2 * s;  // (beside the text's own spaces)
        barH = 4 * s, barGap = 14 * s, shadow = 16 * s, shadowY = 6 * s;
    }
    float lineH() const { return std::max(keyH, body.px * 1.2f); }
};

float lineWidth(const Metrics& m, const char* line) {
    float w = 0;
    const std::vector<Part> ps = parts(line);
    for (size_t i = 0; i < ps.size(); i++)
        w += ps[i].key ? textWidth(m.key, ps[i].s) + 2 * m.keyPadX + (i ? m.keyGap : 0) + (i + 1 < ps.size() ? m.keyGap : 0)
                       : textWidth(m.body, ps[i].s);
    return w;
}

void drawLine(Canvas& cv, const Metrics& m, float x, float top, const char* line) {
    const float mid = top + m.lineH() / 2;
    const float bodyBase = mid + capHeight(m.body) / 2, keyBase = mid + capHeight(m.key) / 2;
    const std::vector<Part> ps = parts(line);
    for (size_t i = 0; i < ps.size(); i++) {
        const Part& p = ps[i];
        if (p.key) {
            x += i ? m.keyGap : 0;
            const float tw = textWidth(m.key, p.s);
            cv.rect(x, mid - m.keyH / 2, x + tw + 2 * m.keyPadX, mid + m.keyH / 2, m.keyR, kKey);
            drawText(cv, m.key, x + m.keyPadX, keyBase, p.s, kKeyText);
            x += tw + 2 * m.keyPadX + (i + 1 < ps.size() ? m.keyGap : 0);
        } else {
            drawText(cv, m.body, x, bodyBase, p.s, kBody);
            x += textWidth(m.body, p.s);
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
    if (winW <= 0 || winH <= 0 || (!card.title[0] && !card.lines[0][0]) || !fonts()) return;
    const double t = now();
    float alpha = 1, slide = 0;
    if (shown) {
        const float in = float(std::min(1.0, (t - since) / kFadeIn));
        alpha = 1 - (1 - in) * (1 - in);
        slide = (1 - in) * (1 - in) * (1 - in);
    } else {
        const double out = t - hiddenAt;
        if (out >= kFadeOut) return;
        alpha = float(1 - out / kFadeOut);
    }

    // the game's picture, and the part of it the card goes in
    const PresentRect pic = lastPresentRect();
    int rx = 0, ry = 0, rw = winW, rh = winH;
    if (pic.w > 0 && pic.h > 0) rx = pic.x, ry = pic.y, rw = pic.w, rh = pic.h;
    const Metrics m(rh);

    // its size: the badge, then the title over the lines
    const int nLines = (card.lines[0][0] ? 1 : 0) + (card.lines[1][0] ? 1 : 0);
    float textW = textWidth(m.title, card.title);
    for (int i = 0; i < 2; i++)
        if (card.lines[i][0]) textW = std::max(textW, lineWidth(m, card.lines[i]));
    const float titleCap = capHeight(m.title);
    const float textH = titleCap + (nLines ? m.titleGap : 0) + nLines * m.lineH() + (nLines > 1 ? m.lineGap : 0);
    const bool bar = card.progress >= 0;
    const float bodyH = std::max(m.badge, textH);
    const float cardW = ceilf(m.padX + m.badge + m.gap + textW + m.padX + 2);
    const float cardH = ceilf(m.padY + bodyH + (bar ? m.barGap + m.barH : 0) + m.padY);
    const int margin = int(ceilf(m.shadow + m.shadowY)) + 2;  // room for the shadow
    const int w = int(cardW) + 2 * margin, h = int(cardH) + 2 * margin;
    s_pixels.assign(size_t(w) * h * 4, 0);
    Canvas cv{s_pixels, w, h};
    const float x0 = float(margin), y0 = float(margin), x1 = x0 + cardW, y1 = y0 + cardH;

    // the panel over its shadow, with a hairline edge
    cv.rect(x0, y0 + m.shadowY, x1, y1 + m.shadowY, m.radius, kShadow, m.shadow);
    cv.rect(x0, y0, x1, y1, m.radius, kPanel);
    cv.rect(x0, y0, x1, y1, m.radius, kBorder, 0, 1.0f);

    // the badge: a disc in the player's colour with their number
    const Rgba accent = {float(card.accent >> 24 & 255), float(card.accent >> 16 & 255), float(card.accent >> 8 & 255), 1};
    const float bx = x0 + m.padX, by = y0 + m.padY + (bodyH - m.badge) / 2;
    cv.rect(bx, by, bx + m.badge, by + m.badge, m.badge / 2, accent);
    if (card.badge[0]) {
        const float bw = textWidth(m.key, card.badge);
        drawText(cv, m.key, bx + (m.badge - bw) / 2, by + (m.badge + capHeight(m.key)) / 2, card.badge, kTitle);
    }

    // the title and lines
    const float tx = bx + m.badge + m.gap;
    float ty = y0 + m.padY + (bodyH - textH) / 2;
    drawText(cv, m.title, tx, ty + titleCap, card.title, kTitle);
    ty += titleCap + m.titleGap;
    for (int i = 0; i < 2; i++)
        if (card.lines[i][0]) {
            drawLine(cv, m, tx, ty, card.lines[i]);
            ty += m.lineH() + m.lineGap;
        }

    // the bar: a held button's progress, or the time left
    if (bar) {
        const float bx0 = x0 + m.padX, bx1 = x1 - m.padX, b0 = y0 + m.padY + bodyH + m.barGap;
        cv.rect(bx0, b0, bx1, b0 + m.barH, m.barH / 2, kTrack);
        const float p = std::max(0.0f, std::min(1.0f, card.progress));
        if (p > 0) cv.rect(bx0, b0, bx0 + std::max(m.barH, (bx1 - bx0) * p), b0 + m.barH, m.barH / 2, accent);
    }

    // straight alpha for the panel's blending, faded
    for (size_t i = 0; i < s_pixels.size(); i += 4) {
        uint8_t* o = &s_pixels[i];
        if (!o[3]) continue;
        const float a = o[3] / 255.0f;
        for (int k = 0; k < 3; k++) o[k] = uint8_t(std::min(255.0f, o[k] / a + 0.5f));
        o[3] = uint8_t(o[3] * alpha + 0.5f);
    }

    // near the top of the game's picture, or of player 2's (or 1's) view
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
    // below the lives (the HUD's top middle) on the whole picture; near the top of player 2's
    // view, and in player 1's below the shared counters (drawn where the game has them)
    const int top = card.view == 1 ? rh / 9 : card.view == 2 ? rh * (card.stacked ? 40 : 36) / 100 : rh * 19 / 100;
    const int x = rx + (rw - int(cardW)) / 2 - margin;
    const int y = ry + top - margin - int(slide * m.padY);
    drawOverlayPanel(OVERLAY_COOP, s_pixels.data(), w, h, x, y, 1, winW, winH);
}

}  // namespace gx
