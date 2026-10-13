#include "gx_savestate_menu.h"
#include "gx_internal.h"

#include "port_include/port_savestate.h"
#include "sms_gx/gx_pc.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <time.h>
#include <vector>

#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "third_party/stb_image.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

namespace {

// Layout units, as the warp menu's (kCapH: stb_easy_font's capitals are 7 font pixels high).
const int kCapH = 7;
const int kPad = 6, kTitleH = 17, kRowH = 24, kFooterH = 18, kNoteH = 14;
const int kRows = 7;                         // rows shown at once; the list scrolls
const int kListW = 220, kPictureW = 168;     // the two columns
const int kWidth = kListW + kPictureW + kPad;
const float kSizeScale = 1.20f;
const int kMaxStates = 500;

const uint8_t kPanelBg[4] = {15, 15, 15, 240};
const uint8_t kLine[4] = {80, 80, 92, 240};
const uint8_t kTitleBg[4] = {41, 74, 122, 255};
const uint8_t kText[4] = {255, 255, 255, 255};
const uint8_t kTextDim[4] = {128, 128, 128, 255};
const uint8_t kNote[4] = {240, 200, 90, 255};
const uint8_t kRowSelected[4] = {34, 60, 94, 240};
const uint8_t kAccent[4] = {66, 150, 250, 255};
const uint8_t kPictureBg[4] = {0, 0, 0, 255};

// The port's side (platform/savestate); none in the GX tests.
const PortSaveStateMenuApi* s_api;

std::vector<PortSaveStateInfo> s_states;
bool s_visible = false;
int s_selected = 0, s_scroll = 0;
std::string s_note;          // why the last press did nothing, or a question
bool s_confirmDelete = false;
std::atomic<bool> s_openWanted(false);
bool s_heldKey[SDL_NUM_SCANCODES];
bool s_heldButton[SDL_CONTROLLER_BUTTON_MAX];

int s_padDir = 0, s_stickDir = 0;
uint32_t s_repeatAt = 0;
const uint32_t kRepeatDelay = 350, kRepeatRate = 90;

// The selected state's picture, decoded
std::string s_pictureFor;
std::vector<uint8_t> s_picture;
int s_pictureW = 0, s_pictureH = 0;

// GL thread: the bitmaps last uploaded and what they show
typedef std::array<int, 9> Shown;
std::vector<uint8_t> s_pixels;
Shown s_shown;
bool s_shownValid = false;
int s_listGen = 0;
std::vector<uint8_t> s_toastPixels;
std::string s_toastShown;
int s_toastW = 0, s_toastH = 0, s_toastGlyph = 0;

void loadPicture() {
    const std::string path = s_states.empty() ? std::string() : s_states[s_selected].path;
    if (path == s_pictureFor) return;
    s_pictureFor = path;
    s_picture.clear();
    s_pictureW = s_pictureH = 0;
    if (path.empty()) return;
    size_t size = 0;
    uint8_t* png = s_api->thumbnail_png(path.c_str(), &size);
    if (!png) return;
    int w, h, n;
    if (stbi_uc* rgba = stbi_load_from_memory(png, int(size), &w, &h, &n, 4)) {
        s_picture.assign(rgba, rgba + size_t(w) * h * 4);
        s_pictureW = w;
        s_pictureH = h;
        stbi_image_free(rgba);
    }
    free(png);
}

void keepInView() {
    const int n = int(s_states.size());
    s_selected = std::clamp(s_selected, 0, std::max(0, n - 1));
    if (s_selected < s_scroll) s_scroll = s_selected;
    if (s_selected >= s_scroll + kRows) s_scroll = s_selected - kRows + 1;
    s_scroll = std::clamp(s_scroll, 0, std::max(0, n - kRows));
}

void reload() {
    s_states.resize(kMaxStates);
    const int n = s_api->list(s_states.data(), kMaxStates);
    s_states.resize(std::min(n, kMaxStates));
    s_listGen++;
    keepInView();
    loadPicture();
}

void move(int delta, bool wrap) {
    const int n = int(s_states.size());
    if (!n) return;
    if (wrap) s_selected = ((s_selected + delta) % n + n) % n;
    else s_selected = std::clamp(s_selected + delta, 0, n - 1);
    s_note.clear();
    s_confirmDelete = false;
    keepInView();
    loadPicture();
}

void open() {
    s_visible = true;
    s_note.clear();
    s_confirmDelete = false;
    s_selected = s_scroll = 0;
    reload();
}

void close() {
    s_visible = false;
    s_padDir = s_stickDir = 0;
}

void activate() {
    if (s_states.empty()) return;
    const PortSaveStateInfo& st = s_states[s_selected];
    s_confirmDelete = false;
    if (!st.sameGame) {
        s_note = "This state is from another game";
        return;
    }
    if (!s_api->ready()) {
        s_note = "States load in a level, once it has loaded";
        return;
    }
    s_api->request_load(st.path);
    close();
}

void deleteSelected() {
    if (s_states.empty()) return;
    if (!s_confirmDelete) {
        s_confirmDelete = true;
        s_note = "Press Y or Delete again to delete this state";
        return;
    }
    s_confirmDelete = false;
    s_note = s_api->remove(s_states[s_selected].path) ? "State deleted" : "Could not delete the state";
    reload();
}

std::string fit(const std::string& str, int maxPx) {
    std::string s = str;
    if (gx::overlayTextWidth(s.c_str()) <= maxPx) return s;
    while (!s.empty() && gx::overlayTextWidth((s + "...").c_str()) > maxPx) s.pop_back();
    return s + "...";
}

std::string when(int64_t created) {
    time_t t = time_t(created);
    struct tm lt;
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    char out[32];
    strftime(out, sizeof out, "%b %d %H:%M", &lt);
    return out;
}

struct Rect {
    int x0, y0, x1, y1;
    bool contains(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
};

int rowTop(int i) { return kTitleH + 3 + i * kRowH; }
int footerTop() { return rowTop(kRows) + 3; }
int logicalHeight() { return footerTop() + kFooterH + kNoteH; }

struct Layout {
    int glyph;
    float unit;
    int width, height, x, y;
    int at(float units) const { return int(std::lround(units * unit)); }
    int textY(int y0, int y1) const { return y0 + (y1 - y0 - kCapH * glyph) / 2; }
    Rect title() const { return {0, 0, width, at(kTitleH)}; }
    Rect closeBox() const { return {at(kWidth - kTitleH), 0, width, at(kTitleH)}; }
    Rect row(int i) const { return {0, at(rowTop(i)), at(kListW), at(rowTop(i) + kRowH)}; }
    Rect picture() const { return {at(kListW + kPad / 2), at(rowTop(0) + 2), at(kWidth - kPad), at(rowTop(0) + 2 + (kPictureW - 1.5f * kPad) * 3 / 4)}; }
};

Layout placedAt(int glyph, int winW, int winH) {
    Layout l;
    l.glyph = glyph;
    l.unit = glyph * kSizeScale;
    l.width = l.at(kWidth);
    l.height = l.at(logicalHeight());
    l.x = (winW - l.width) / 2;
    l.y = std::max(0, (winH - l.height) / 3);
    return l;
}

Layout layoutFor(int winW, int winH) {
    int glyph = std::max(2, (winH + 360) / 720);
    Layout l = placedAt(glyph, winW, winH);
    while (glyph > 1 && (l.x < 0 || l.height > winH)) l = placedAt(--glyph, winW, winH);
    return l;
}

// The picture scaled into r, keeping its shape.
void drawPicture(std::vector<uint8_t>& px, int w, int h, const Rect& r) {
    gx::overlayFillRect(px, w, h, r.x0, r.y0, r.x1, r.y1, kPictureBg);
    if (s_picture.empty()) return;
    const int rw = r.x1 - r.x0, rh = r.y1 - r.y0;
    int dw = rw, dh = int(int64_t(rw) * s_pictureH / s_pictureW);
    if (dh > rh) dh = rh, dw = int(int64_t(rh) * s_pictureW / s_pictureH);
    const int ox = r.x0 + (rw - dw) / 2, oy = r.y0 + (rh - dh) / 2;
    for (int y = 0; y < dh; y++) {
        const int sy = y * s_pictureH / dh;
        for (int x = 0; x < dw; x++) {
            const int sx = x * s_pictureW / dw;
            const int tx = ox + x, ty = oy + y;
            if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
            memcpy(&px[(size_t(ty) * w + tx) * 4], &s_picture[(size_t(sy) * s_pictureW + sx) * 4], 3);
            px[(size_t(ty) * w + tx) * 4 + 3] = 255;
        }
    }
}

void rasterize(const Layout& l) {
    const int w = l.width, h = l.height, g = l.glyph;
    s_pixels.assign(size_t(w) * h * 4, 0);
    auto fill = [&](const Rect& r, const uint8_t c[4]) { gx::overlayFillRect(s_pixels, w, h, r.x0, r.y0, r.x1, r.y1, c); };
    auto text = [&](int x, int y, const std::string& str, const uint8_t c[4]) {
        gx::overlayText(s_pixels, w, h, x, y, str.c_str(), c, g);
    };
    auto textWidth = [&](const std::string& str) { return gx::overlayTextWidth(str.c_str()) * g; };
    const int listPx = int((kListW - 2 * kPad) * kSizeScale);
    fill(Rect{0, 0, w, h}, kPanelBg);
    const Rect title = l.title();
    fill(title, kTitleBg);
    text(l.at(kPad), l.textY(title.y0, title.y1), "Save States", kText);
    text(l.at(kWidth - kPad - 5), l.textY(title.y0, title.y1), "X", kText);
    if (s_states.empty()) {
        const Rect r = l.row(0);
        text(l.at(kPad), l.textY(r.y0, r.y1), "No save states yet", kText);
        const Rect r2 = l.row(1);
        text(l.at(kPad), l.textY(r2.y0, r2.y1), fit("Save one with L1 + R1 or F5", listPx), kTextDim);
    }
    for (int i = 0; i < kRows && s_scroll + i < int(s_states.size()); i++) {
        const int index = s_scroll + i;
        const PortSaveStateInfo& st = s_states[index];
        const Rect r = l.row(i);
        if (index == s_selected) {
            fill(r, kRowSelected);
            fill(Rect{0, r.y0, 2 * g, r.y1}, kAccent);
        }
        const int mid = (r.y0 + r.y1) / 2;
        const std::string stage = st.stageName[0] ? st.stageName : "Save state";
        const std::string tag = !st.sameGame ? "other game" : st.exact ? "" : "position";
        const int tagPx = tag.empty() ? 0 : gx::overlayTextWidth(tag.c_str()) + 8;
        text(l.at(kPad), l.textY(r.y0 + l.at(2), mid), fit(stage, listPx - tagPx), st.sameGame ? kText : kTextDim);
        if (!tag.empty()) text(l.at(kListW - kPad) - textWidth(tag), l.textY(r.y0 + l.at(2), mid), tag, kNote);
        std::string second = when(st.created);
        if (st.episodeName[0]) second += "  " + std::string(st.episodeName);
        text(l.at(kPad), l.textY(mid, r.y1 - l.at(2)), fit(second, listPx), kTextDim);
    }
    if (!s_states.empty()) {
        const Rect pic = l.picture();
        drawPicture(s_pixels, w, h, pic);
        const PortSaveStateInfo& st = s_states[s_selected];
        const int pictureTextPx = int((kPictureW - 2 * kPad) * kSizeScale);
        int y = pic.y1 + l.at(5);
        text(pic.x0, y, fit(st.stageName, pictureTextPx), kText);
        y += l.at(11);
        if (st.episodeName[0]) {
            text(pic.x0, y, fit(st.episodeName, pictureTextPx), kTextDim);
            y += l.at(11);
        }
        if (st.sameGame && !st.exact) {
            text(pic.x0, y, fit("Saved by another version:", pictureTextPx), kNote);
            text(pic.x0, y + l.at(10), fit("loads Mario's place only", pictureTextPx), kNote);
        }
    }
    const int footerY = l.at(footerTop());
    std::string position;
    if (int(s_states.size()) > kRows) position = std::to_string(s_selected + 1) + "/" + std::to_string(s_states.size());
    if (!position.empty()) text(w - l.at(kPad) - textWidth(position), footerY + l.at(4), position, kTextDim);
    const std::string hint = s_states.empty() ? "B/Esc Back" : "A/Enter Load   Y/Del Delete   B/Esc Back";
    text(l.at(kPad), footerY + l.at(4), hint, kTextDim);
    std::string note = s_note;
    if (note.empty() && !s_api->ready()) note = "States load in a level, once it has loaded";
    if (!note.empty()) text(l.at(kPad), footerY + l.at(kFooterH + 2), note, s_note.empty() ? kTextDim : kNote);
    const int t = std::max(1, g / 2);
    fill(Rect{0, footerY, w, footerY + t}, kLine);
    fill(Rect{l.at(kListW), l.at(kTitleH), l.at(kListW) + t, footerY}, kLine);
    fill(Rect{0, 0, w, t}, kLine);
    fill(Rect{0, h - t, w, h}, kLine);
    fill(Rect{0, 0, t, h}, kLine);
    fill(Rect{w - t, 0, w, h}, kLine);
}

Shown shownState(int winW, int winH) {
    return Shown{winW, winH, s_selected, s_scroll, s_listGen, int(std::hash<std::string>()(s_note) & 0x7FFFFFFF),
                 s_api->ready(), int(s_picture.size()), s_confirmDelete};
}

// The message ("State saved") in a small panel low in the window, between the
// HUD's corners.
void renderToast(int winW, int winH) {
    char message[128];
    if (s_api->message(message, sizeof message) <= 0) {
        s_toastShown.clear();
        return;
    }
    const int glyph = std::max(2, (winH + 360) / 720);
    const bool changed = message != s_toastShown || glyph != s_toastGlyph;
    if (changed) {
        s_toastShown = message;
        s_toastGlyph = glyph;
        const int pad = 6 * glyph;
        s_toastW = gx::overlayTextWidth(message) * glyph + 2 * pad;
        s_toastH = kCapH * glyph + 2 * pad;
        s_toastPixels.assign(size_t(s_toastW) * s_toastH * 4, 0);
        gx::overlayFillRect(s_toastPixels, s_toastW, s_toastH, 0, 0, s_toastW, s_toastH, kPanelBg);
        gx::overlayFillRect(s_toastPixels, s_toastW, s_toastH, 0, 0, 2 * glyph, s_toastH, kAccent);
        gx::overlayText(s_toastPixels, s_toastW, s_toastH, pad, pad, message, kText, glyph);
    }
    gx::drawOverlayPanel(gx::OVERLAY_STATE_TOAST, changed ? s_toastPixels.data() : nullptr, s_toastW, s_toastH,
                         (winW - s_toastW) / 2, winH - s_toastH - winH / 10, 1, winW, winH);
}

bool menuKey(SDL_Scancode key, bool repeat) {
    switch (key) {
    case SDL_SCANCODE_UP: move(-1, !repeat); return true;
    case SDL_SCANCODE_DOWN: move(1, !repeat); return true;
    case SDL_SCANCODE_PAGEUP: move(-kRows, false); return true;
    case SDL_SCANCODE_PAGEDOWN: move(kRows, false); return true;
    case SDL_SCANCODE_HOME: move(-int(s_states.size()), false); return true;
    case SDL_SCANCODE_END: move(int(s_states.size()), false); return true;
    default: break;
    }
    if (repeat) return s_heldKey[key];
    switch (key) {
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_SPACE: activate(); return true;
    case SDL_SCANCODE_DELETE: deleteSelected(); return true;
    case SDL_SCANCODE_BACKSPACE:
    case SDL_SCANCODE_ESCAPE: close(); return true;
    default: return false;
    }
}

bool onKey(const SDL_KeyboardEvent& ev) {
    const SDL_Scancode key = ev.keysym.scancode;
    if (unsigned(key) >= SDL_NUM_SCANCODES) return false;
    if (ev.type == SDL_KEYUP) {
        const bool held = s_heldKey[key];
        s_heldKey[key] = false;
        return held;
    }
    if (ev.repeat) {
        if (!s_heldKey[key]) return false;
        if (s_visible) menuKey(key, true);
        return true;  // still the menu's after it closed: a repeating Escape would quit the game
    }
    if (!s_visible || !menuKey(key, false)) return false;
    s_heldKey[key] = true;
    return true;
}

void stepHeld(int dir) {
    move(dir, true);
    s_repeatAt = SDL_GetTicks() + kRepeatDelay;
}

// Every button pressed while the menu is open is the menu's, and so is its
// release: closing the menu with B must not also press B in the game.
bool onButton(const SDL_ControllerButtonEvent& ev) {
    const int b = ev.button;
    if (b < 0 || b >= SDL_CONTROLLER_BUTTON_MAX) return false;
    if (ev.type == SDL_CONTROLLERBUTTONUP) {
        const bool held = s_heldButton[b];
        s_heldButton[b] = false;
        if ((b == SDL_CONTROLLER_BUTTON_DPAD_UP && s_padDir < 0) || (b == SDL_CONTROLLER_BUTTON_DPAD_DOWN && s_padDir > 0))
            s_padDir = 0;
        return held;
    }
    if (!s_visible) return false;
    s_heldButton[b] = true;
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        s_padDir = -1;
        stepHeld(-1);
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        s_padDir = 1;
        stepHeld(1);
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: move(-kRows, false); break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: move(kRows, false); break;
    case SDL_CONTROLLER_BUTTON_A: activate(); break;
    case SDL_CONTROLLER_BUTTON_Y: deleteSelected(); break;
    case SDL_CONTROLLER_BUTTON_B:
    case SDL_CONTROLLER_BUTTON_START: close(); break;
    default: break;
    }
    return true;
}

void onAxis(const SDL_ControllerAxisEvent& ev) {
    if (!s_visible || ev.axis != SDL_CONTROLLER_AXIS_LEFTY) return;
    const int dir = ev.value < -16000 ? -1 : ev.value > 16000 ? 1 : 0;
    if (dir == s_stickDir) return;
    s_stickDir = dir;
    if (dir) stepHeld(dir);
}

bool onMouse(const SDL_Event& ev) {
    if (!s_visible) return false;
    const bool motion = ev.type == SDL_MOUSEMOTION;
    const bool wheel = ev.type == SDL_MOUSEWHEEL;
    const Uint32 windowID = motion ? ev.motion.windowID : wheel ? ev.wheel.windowID : ev.button.windowID;
    int winW = 0, winH = 0, drawW = 0, drawH = 0;
    if (SDL_Window* window = SDL_GetWindowFromID(windowID)) {
        SDL_GetWindowSize(window, &winW, &winH);
        SDL_GL_GetDrawableSize(window, &drawW, &drawH);
    }
    if (winW <= 0 || winH <= 0) return true;
    if (wheel) {
        move(ev.wheel.y > 0 ? -1 : ev.wheel.y < 0 ? 1 : 0, false);
        return true;
    }
    const int px = (motion ? ev.motion.x : ev.button.x) * drawW / winW;
    const int py = (motion ? ev.motion.y : ev.button.y) * drawH / winH;
    const Layout l = layoutFor(drawW, drawH);
    const int x = px - l.x, y = py - l.y;
    const bool inside = x >= 0 && y >= 0 && x < l.width && y < l.height;
    int row = -1;
    for (int i = 0; inside && i < kRows && s_scroll + i < int(s_states.size()); i++)
        if (l.row(i).contains(x, y)) row = s_scroll + i;
    if (row >= 0 && (motion || ev.type == SDL_MOUSEBUTTONDOWN) && row != s_selected) move(row - s_selected, false);
    if (ev.type != SDL_MOUSEBUTTONDOWN) return true;
    if (ev.button.button == SDL_BUTTON_RIGHT) close();
    else if (ev.button.button == SDL_BUTTON_LEFT) {
        if (inside && l.closeBox().contains(x, y)) close();
        else if (row >= 0) activate();
    }
    return true;
}

}  // namespace

extern "C" void GXPC_SetSaveStateApi(const PortSaveStateMenuApi* api) { s_api = api; }
// From the game's thread (the pause menu): opened at the next event pump.
extern "C" void GXPC_OpenSaveStateMenu(void) { s_openWanted = true; }

// A picture of the last frame shown for a new state: at most 320 pixels wide,
// as a PNG (platform/savestate). The caller frees it.
extern "C" uint8_t* GXPC_SaveStateThumbnail(size_t* size) {
    *size = 0;
    int w = 0, h = 0;
    if (!GXPC_ReadXFB(nullptr, nullptr, &w, &h) || w <= 0 || h <= 0) return nullptr;
    std::vector<uint8_t> full(size_t(w) * h * 4);
    if (!GXPC_ReadXFB(nullptr, full.data(), &w, &h)) return nullptr;
    const int tw = std::min(w, 320), th = int(int64_t(h) * tw / w);
    if (th <= 0) return nullptr;
    std::vector<uint8_t> small(size_t(tw) * th * 4);
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            // the average of the pixels it covers
            const int x0 = x * w / tw, x1 = std::max(x0 + 1, (x + 1) * w / tw);
            const int y0 = y * h / th, y1 = std::max(y0 + 1, (y + 1) * h / th);
            unsigned sum[3] = {0, 0, 0}, n = 0;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1; sx++, n++)
                    for (int c = 0; c < 3; c++) sum[c] += full[(size_t(sy) * w + sx) * 4 + c];
            uint8_t* o = &small[(size_t(y) * tw + x) * 4];
            for (int c = 0; c < 3; c++) o[c] = uint8_t(sum[c] / n);
            o[3] = 255;
        }
    int len = 0;
    unsigned char* png = stbi_write_png_to_mem(small.data(), tw * 4, tw, th, 4, &len);
    if (!png) return nullptr;
    uint8_t* out = (uint8_t*)malloc(size_t(len));
    if (out) {
        memcpy(out, png, size_t(len));
        *size = size_t(len);
    }
    STBIW_FREE(png);
    return out;
}

namespace gx {

bool savestateMenuVisible() { return s_visible; }

bool savestateMenuHandleEvent(const SDL_Event& ev) {
    switch (ev.type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        return onKey(ev.key);
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        return onButton(ev.cbutton);
    case SDL_CONTROLLERAXISMOTION:
        onAxis(ev.caxis);
        return false;
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
        return onMouse(ev);
    default:
        return false;
    }
}

void savestateMenuTick() {
    if (s_openWanted.exchange(false) && !s_visible && s_api) open();
    if (!s_visible) return;
    const int dir = s_padDir ? s_padDir : s_stickDir;
    if (dir && int32_t(SDL_GetTicks() - s_repeatAt) >= 0) {
        move(dir, false);
        s_repeatAt = SDL_GetTicks() + kRepeatRate;
    }
}

void savestateMenuRender(int winW, int winH) {
    if (winW <= 0 || winH <= 0 || !s_api) return;
    renderToast(winW, winH);
    if (!s_visible) {
        s_shownValid = false;
        return;
    }
    const Layout l = layoutFor(winW, winH);
    const Shown state = shownState(winW, winH);
    const bool changed = !s_shownValid || state != s_shown;
    if (changed) {
        rasterize(l);
        s_shown = state;
        s_shownValid = true;
    }
    drawOverlayPanel(OVERLAY_STATE_MENU, changed ? s_pixels.data() : nullptr, l.width, l.height, l.x, l.y, 1, winW,
                     winH);
}

}  // namespace gx
