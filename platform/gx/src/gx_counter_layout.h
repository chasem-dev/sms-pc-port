#pragma once
#include <algorithm>

namespace gx {
struct CounterLayout { int x, y, scale; };
inline CounterLayout counterLayout(int width, int height, int w, int h) {
    // Use the debug panel's 8-pixel inset. The readout is one line, capped
    // at two font pixels per bitmap pixel so it stays smaller than the HUD.
    constexpr int inset = 8;
    int scale = height >= 720 && 2 * (w + inset) <= width ? 2 : 1;
    return {std::min(inset, std::max(0, width - w * scale)),
            std::min(inset, std::max(0, height - h * scale)), scale};
}
} // namespace gx
