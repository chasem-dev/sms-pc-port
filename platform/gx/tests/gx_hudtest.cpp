#include "gx_hud_layout.h"
#include <cstdio>
#include <cstdlib>

using namespace gx;

static bool overlaps(HudBounds a, HudBounds b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

// Also emits the same resting envelopes the renderer uses, for offline review.
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        int w, h;
        if (std::sscanf(argv[i], "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) return 1;
        const auto view = windowView(w, h);
        const int width = int(hudFrameWidth * view.wide + 1) & ~1;
        const float dx = (width - hudFrameWidth) / 2 * hudCanvasWidth / hudFrameWidth;
        const float dy = (view.tall - 1) * hudCanvasHeight / 2;
        const auto onScreen = [&](HudBounds b) {
            return b.left >= -dx - .001f && b.right <= hudCanvasWidth + dx + .001f &&
                   b.top >= -dy - .001f && b.bottom <= hudCanvasHeight + dy + .001f;
        };
        const auto dock = [&](HudBounds b, Dock side) {
            const auto p = hudLayout(width, view.tall, side);
            const float x = p.xOffset * hudCanvasWidth / hudFrameWidth - dx;
            const float y = p.yOffset / p.yScale * hudCanvasHeight / hudFrameHeight;
            return HudBounds{b.left * p.xScale + x, b.top + y, b.right * p.xScale + x, b.bottom + y};
        };
        const auto emit = [&](const char* name, HudBounds b) {
            if (!onScreen(b)) std::abort();
            std::printf("%s %f %f %f %f %f %f %s\n", argv[i], hudCanvasWidth + 2*dx, hudCanvasHeight + 2*dy,
                        b.left + dx, b.top + dy, b.right - b.left, b.bottom - b.top, name);
        };
        const auto health = dock(healthBounds, hudDock('l_0'));
        const auto fludd = dock(fluddBounds, hudDock('w_0'));
        const auto lives = dock(livesBounds, hudDock('m_0'));
        const auto counters = dock(counterBounds, hudDock('s_0'));
        emit("Health", health);
        emit("FLUDD", fludd);
        emit("Lives", lives);
        emit("Counters", counters);
        for (auto place : {RadarPlace::Corner, RadarPlace::Yield, RadarPlace::Beside, RadarPlace::Below, RadarPlace::BottomLeft}) {
            for (int pct = 40; pct <= 250; ++pct) {
                const float want = .6f * pct;
                const auto p = radarLayout(width, view.tall, place, want);
                const HudBounds radar{p.x - p.radius, p.y - p.radius,
                                      p.x + p.radius, p.y + p.radius + radarCaptionDepth};
                if (p.radius < 24 || p.radius > want || !onScreen(radar) || overlaps(health, radar) ||
                    overlaps(fludd, radar) || overlaps(lives, radar) || overlaps(counters, radar)) {
                    std::fprintf(stderr, "%s: radar placement %d at %d%% overlaps the HUD or does not fit\n", argv[i], place, pct);
                    return 1;
                }
                if (place == RadarPlace::Below && pct == 250) emit("Radar + zoom", radar);
            }
        }
    }
    return argc > 1 ? 0 : 1;
}
