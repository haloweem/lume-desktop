#include "core/minimize_animation/minimize_animation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lume::minimize_animation {
bool operator==(const Preferences& a, const Preferences& b) noexcept {
    return a.enabled == b.enabled && a.durationMs == b.durationMs && a.restore == b.restore &&
        a.launch == b.launch && a.multiMonitor == b.multiMonitor &&
        a.unhideTaskbar == b.unhideTaskbar && a.unhideDurationMs == b.unhideDurationMs &&
        a.engine == b.engine;
}
bool Valid(const Preferences& p) noexcept {
    return p.durationMs >= 50 && p.durationMs <= 2000 && p.unhideDurationMs >= 0 &&
        p.unhideDurationMs <= 5000 && (p.engine == Engine::Modern || p.engine == Engine::Classic);
}
Point LampVertex(float tx, float ty, float p, Rect w, Rect i) noexcept {
    p = std::clamp(p, 0.f, 1.f);
    const float k = p <= .3f ? p/.3f : 1.f;
    const float j = p > .3f ? (p-.3f)/.7f : 0.f;
    const float expand = i.y - w.y - w.height;
    const float full = i.y - w.y - expand*(1-k);
    const float height = full*(1-j), y = ty*height;
    const float x = tx*i.width + tx*(w.width-i.width)*(1-j)*(1-ty) +
                    tx*(w.width-i.width)*(1-k)*ty;
    // The original curve divides by fullHeight. A dock level with the window
    // top must remain finite, even when the final mesh is deliberately degenerate.
    const float fullSafe = std::abs(full) < .001f ? (full < 0 ? -.001f : .001f) : full;
    const float offset = std::abs(full+.1f) < .001f ? .001f : full+.1f;
    const float ox = (i.x-w.x)*(y/offset)*k + (i.x-w.x)*j;
    const float oy = i.y-w.y-height-expand*(1-k);
    constexpr float pi = 3.14159265f;
    const float wave = std::sin(((height-y)/fullSafe)*2*pi+pi) *
                       (w.x+w.width*tx-i.x-i.width*tx)/7*k;
    return {w.x+x+ox+wave, w.y+y+oy};
}
Strip ClassicRow(float row, float p, Rect w, float dockX, float dockY) noexcept {
    const float m = std::clamp(p*1.65f-(1-row)*.65f,0.f,1.f);
    const float e = m*m*(3-2*m);
    const float neck = std::clamp(w.width*.03f,12.f,60.f);
    const float y = w.y+w.height*row, center = w.x+w.width*.5f;
    return {y+(dockY-y)*e, w.width+(neck-w.width)*e, center+(dockX-center)*e};
}
std::size_t SnapshotBytes(int w, int h, std::size_t limit) noexcept {
    if (w <= 0 || h <= 0) return 0;
    const auto bytes = static_cast<std::uint64_t>(w)*static_cast<std::uint64_t>(h)*4;
    return bytes <= limit ? static_cast<std::size_t>(bytes) : 0;
}
unsigned ShowCommand(std::uintptr_t parameter) noexcept { return static_cast<unsigned>(parameter&0xffffu); }
}
