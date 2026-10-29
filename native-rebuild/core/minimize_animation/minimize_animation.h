#pragma once
#include <cstddef>
#include <cstdint>

namespace lume::minimize_animation {
enum class Engine { Modern, Classic };
struct Preferences {
    bool enabled = false;
    int durationMs = 450;
    bool restore = true;
    bool launch = true;  // "展开动画": new windows open out of the Dock (or taskbar)
    bool multiMonitor = false;
    bool unhideTaskbar = true;
    int unhideDurationMs = 450;
    Engine engine = Engine::Modern;
};
bool operator==(const Preferences&, const Preferences&) noexcept;
inline bool operator!=(const Preferences& a, const Preferences& b) noexcept { return !(a == b); }
bool Valid(const Preferences&) noexcept;
struct Rect { float x, y, width, height; };
struct Point { float x, y; };
// Source 3.1.2 modern mesh, in desktop coordinates. Progress 0 is the window;
// progress 1 is the dock. A restore runs the same curve backwards.
Point LampVertex(float tx, float ty, float progress, Rect window, Rect dock) noexcept;
struct Strip { float y, width, center; };
Strip ClassicRow(float row, float progress, Rect window, float dockX, float dockY) noexcept;
constexpr std::size_t MaxSnapshotBytes = 64u * 1024u * 1024u;
constexpr std::size_t MaxCanvasBytes = 96u * 1024u * 1024u;
std::size_t SnapshotBytes(int width, int height, std::size_t limit = MaxSnapshotBytes) noexcept;
// HCBT_MINMAX documents only the low word; the high word is undefined.
unsigned ShowCommand(std::uintptr_t parameter) noexcept;
}
