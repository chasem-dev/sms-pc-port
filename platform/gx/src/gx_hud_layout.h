#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "gx_window_layout.h"
#include "sms_gx/gx_radar_layout.h"

namespace gx {
enum class Dock { None, TopLeft, Top, TopRight, Right, BottomLeft, Bottom, BottomRight };
enum class HudPosition { Centre, Horizontal, Screen };
// GMSE01 game_6.szs / standard_1.blo. Identify the group at rest by
// its pane tag, not by the animated position of a digit or its parent.

constexpr Dock hudDock(uint32_t tag) {
    switch (tag) {
    case 'c_0': case 's_0': case 'd_0': return Dock::TopLeft;
    case 'm_0': return Dock::Top;
    case 'l_0': return Dock::TopRight;
    case 'he_0': case 'he_w': case 'he_2': return Dock::Right;
    case 'b_0': case 'r_0': case 't_0': return Dock::BottomLeft;
    case 'te_0': return Dock::Bottom; // ticker window, scrolling text and scissor
    case 'w_0': case 'j_t0': return Dock::BottomRight;
    default: return Dock::None;
    }
}
// In the EFB, vertical gain adds headroom inside the 448 displayed
// lines. Return offsets in EFB units after the reference art is scaled.
struct HudLayout { float xOffset, yScale, yOffset, xScale = 1; };
constexpr float hudFrameWidth = 640, hudFrameHeight = 448;
constexpr float hudCanvasWidth = 600, hudCanvasHeight = 480;
constexpr HudLayout hudLayout(int width, float tall, Dock dock) {
    const float padX = (width - hudFrameWidth) / 2;
    const float sy = 1 / tall;
    const float padY = hudFrameHeight / 2 * (1 - sy);
    switch (dock) {
    case Dock::TopLeft: return {0, sy, -padY};
    case Dock::Top: return {padX, sy, -padY};
    case Dock::TopRight: return {2 * padX, sy, -padY};
    case Dock::Right: return {2 * padX, sy, 0};
    case Dock::BottomLeft: return {0, sy, padY};
    case Dock::Bottom: return {padX, sy, padY};
    case Dock::BottomRight: return {2 * padX, sy, padY};
    default: return {padX, sy, 0};
    }
}

// GMSE01's 600 x 480 layout canvas; include health rays, nozzles and Yoshi.
struct HudBounds { float left, top, right, bottom; };
constexpr HudBounds healthBounds{488, 28, 586, 132};
constexpr HudBounds livesBounds{211, 40, 421, 96};
constexpr HudBounds counterBounds{13, 40, 211, 175};
constexpr HudBounds fluddBounds{485, 300, 595, 452};
constexpr float radarMargin = 22, radarTopMargin = 28, radarGap = 6;
constexpr float radarCaptionDepth = 17; // zoom label and shadow
constexpr float maxRadarShare = .16f;

// Coordinates are relative to a centred 600 x 480 projection. Reserve the
// meters' rest envelopes even when invisible, so the radar does not move
// during the health meter's slide. Spend width before shrinking the radar.
inline RadarLayout radarLayout(int width, float tall, RadarPlace::Value place, float radius) {
    const float dx = (width - hudFrameWidth) * .5f * hudCanvasWidth / hudFrameWidth;
    const float dy = (tall - 1) * hudCanvasHeight / 2;
    const HudBounds safe{-dx + radarMargin, radarTopMargin - dy, hudCanvasWidth + dx - radarMargin, hudCanvasHeight + dy - radarMargin};
    const auto capacity = [](HudBounds lane) {
        return std::max(0.f, std::min(lane.right - lane.left, lane.bottom - lane.top - radarCaptionDepth) / 2);
    };
    const float shareArea = maxRadarShare * (hudCanvasWidth + 2 * dx) * (hudCanvasHeight + 2 * dy);
    radius = std::min(radius, (std::sqrt(radarCaptionDepth * radarCaptionDepth + 4 * shareArea) - radarCaptionDepth) / 4);
    const HudBounds rightGap{safe.left, counterBounds.bottom - dy + radarGap, safe.right, fluddBounds.top + dy - radarGap};
    HudBounds lane = rightGap;
    if (place == RadarPlace::BottomLeft) {
        // Clear the top-left counters and the red-coin band as well.
        lane = {safe.left, counterBounds.bottom - dy + radarGap, fluddBounds.left + dx - radarGap, safe.bottom - 81};
    } else if (place == RadarPlace::Beside) {
        lane = {safe.left, counterBounds.bottom - dy + radarGap, std::min(healthBounds.left, fluddBounds.left) + dx - radarGap, fluddBounds.top + dy - radarGap};
    } else if (capacity(rightGap) < radius) {
        const HudBounds inset{safe.left, rightGap.top, fluddBounds.left + dx - radarGap, safe.bottom};
        if (capacity(inset) > capacity(rightGap)) lane = inset;
    }
    radius = std::min(radius, capacity(lane));
    return {place == RadarPlace::BottomLeft ? lane.left + radius : lane.right - radius,
            place == RadarPlace::BottomLeft ? lane.bottom - radarCaptionDepth - radius : lane.top + radius, radius};
}
} // namespace gx
