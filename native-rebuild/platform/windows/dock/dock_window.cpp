#include "platform/windows/dock/dock_window.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/dock/dock_spring.h"
#include "platform/windows/dock/dock_reflection.h"
#include "platform/windows/dock/dock_desktop_layer.h"
#include "platform/windows/dock/frame_clock.h"
#include "platform/windows/widget_ui/layered_surface.h"
#include "platform/windows/widget_ui/text_fit.h"
#include "platform/windows/organizer/organizer_drop_target.h"
#include "platform/windows/organizer/organizer_text.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include "core/window/screen_fit.h"
#include "platform/windows/dock/dock_slots.h"
#include "platform/windows/dock/dock_animation_targets.h"
#include "platform/windows/dock/dock_sources.h"
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <utility>

namespace lume::platform::windows {
namespace {
constexpr const wchar_t* IconClass = lume::window_ids::DockIconsClass;
constexpr const wchar_t* BarClass = lume::window_ids::DockBarClass;
constexpr wchar_t GhostClass[] = L"LumeNativeDockDragIcon", PreviewClass[] = L"LumeNativeDockPreview";
constexpr UINT_PTR PreviewTimer = 0x7c3;
constexpr UINT PreviewHoverDelayMs = 500;
constexpr UINT_PTR PreviewLeaveTimer = 0x7c4;
constexpr UINT FrameMessage = WM_APP + 0x7c1;
constexpr UINT PaintMessage = WM_APP + 0x7c3;
constexpr double MinFrameInterval = 1.0 / 90.0;
constexpr float DragThreshold = 5, DragLift = 12, DragScale = 1.12f;
// A dragged icon this far (DIPs) beyond the glass is removed when released.
constexpr float RemoveDistance = 36;
// A minimized window's thumbnail: at most this share of the icon height, and
// its program's icon as a badge on the lower-right corner.
constexpr float ThumbnailHeight = .82f, BadgeSize = .44f;
float ItemWidth(const dock::Item& item) {
    if (item.kind == dock::Kind::Separator) return 1.f;
    return DockWindow::IconSize;
}
float ItemWidth(const dock::Item& item, float scale) {
    if (item.kind == dock::Kind::Separator) return 1.f;
    return DockWindow::IconSize * scale;
}
using dock::Movable;
using dock::Removable;
const auto Key = dock::MotionKey;
void LaunchpadGlyph(WidgetCanvas& canvas, WidgetRect r) {
    const float cell = r.width * .2f, gap = (r.width - 3 * cell) / 4;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            canvas.Rounded({r.x + gap + column * (cell + gap), r.y + gap + row * (cell + gap), cell, cell},
                           cell * .3f, 0xffffff, .92f);
}
void StartGlyph(WidgetCanvas& canvas, WidgetRect r) {
    const float tile = r.width * .374f, gap = r.width * .035f;
    const float inset = (r.width - 2 * tile - gap) / 2;
    constexpr std::uint32_t blues[2][2]{{0x32b9f4, 0x169de8}, {0x168ee0, 0x0878d5}};
    for (int row = 0; row < 2; ++row)
        for (int column = 0; column < 2; ++column)
            canvas.Rounded({r.x + inset + column * (tile + gap), r.y + inset + row * (tile + gap), tile, tile},
                           0, blues[row][column]);
}
// Control center, drawn like the launchpad glyph: two white sliders whose
// knobs sit at opposite ends, on the Dock itself without a tile.
void ControlCenterGlyph(WidgetCanvas& canvas, WidgetRect r) {
    const float w = r.width * .74f, track = r.width * .23f, x = r.x + (r.width - w) / 2, knob = track * .72f, inset = (track - knob) / 2;
    const float top = r.y + r.height * .2f, bottom = r.y + r.height * .57f;
    canvas.Rounded({x, top, w, track}, track / 2, 0xffffff, .92f);
    canvas.Ellipse({x + inset, top + inset, knob, knob}, 0x1b1c20, .85f);
    canvas.Rounded({x, bottom, w, track}, track / 2, 0xffffff, .92f);
    canvas.Ellipse({x + w - track + inset, bottom + inset, knob, knob}, 0x1b1c20, .85f);
}
}  // namespace

float DockWindow::Width(const std::vector<dock::Item>& items, float spacing) {
    float width = Padding * 2;
    for (std::size_t i = 0; i < items.size(); ++i) width += ItemWidth(items[i]) + (i ? spacing : 0.f);
    return width;
}
WidgetRect DockWindow::Slot(const std::vector<dock::Item>& items, std::size_t index, float spacing) {
    float x = Padding;
    for (std::size_t i = 0; i < index && i < items.size(); ++i) x += ItemWidth(items[i]) + spacing;
    return {x, BarHeight - IconBottom - IconSize, index < items.size() ? ItemWidth(items[index]) : 0.f, IconSize};
}
std::size_t DockWindow::Hit(const std::vector<dock::Item>& items, float x, float y) {
    if (y < 0 || y >= BarHeight) return None;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind == dock::Kind::Separator) continue;
        const auto slot = Slot(items, i);
        if (x >= slot.x - Gap / 2 && x < slot.x + slot.width + Gap / 2) return i;
    }
    return None;
}
float DockWindow::Magnification(float distance, float spacing) {
    const float t = distance / (IconSize + spacing) / Reach;
    if (t >= 1) return 1;
    const float falloff = (1 - t * t) * (1 - t * t);
    return 1 + (MaxScale - 1) * falloff;
}
std::size_t DockWindow::DropSlot(const std::vector<dock::Item>& items, float x, std::size_t skip, float spacing) {
    // Count the other pinned items whose centre lies left of x; before the
    // first pinned item (over the launchpad or Lume) the slot is 0.
    std::size_t slot = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i == skip || items[i].kind != dock::Kind::Pinned) continue;
        const auto r = Slot(items, i, spacing);
        if (r.x + r.width / 2 < x) ++slot;
    }
    return slot;
}

struct DockWindow::Impl {
    struct Motion { DockSpring scale{1, 0}, offset, lift; };
    HINSTANCE instance;
    DockWindowPorts ports;
    HWND icons = nullptr, bar = nullptr;
    unsigned dpi = 96;
    bool frosted = false, visible = false, hovering = false, reducedMotion = false;
    bool paintPending=false;
    DockAppearance look;
    unsigned monitorDpi = 96;  // dpi is this times the icon scale
    // Reflections cached per icon bitmap (keyed by identity, dropped with it).
    std::map<const WidgetBitmap*, std::pair<std::weak_ptr<const WidgetBitmap>, std::shared_ptr<const WidgetBitmap>>> mirrors;
    HWND preview = nullptr;
    struct PreviewCard { dock::ProgramWindow window; RECT rect{}; HTHUMBNAIL thumbnail = nullptr; };
    std::vector<PreviewCard> previewCards;
    std::vector<dock::ProgramWindow> previewWindows;
    std::size_t pendingPreview = None, previewIndex = None, previewPage = 0, previewPageSize = 1;
    std::uint64_t previewPressed = 0;
    std::vector<std::string> runningOrder;
    std::unique_ptr<WidgetCanvas> previewCanvas;
    WidgetBitmap previewBitmap;
    LayeredSurface previewSurface;
    POINT previewOrigin{};
    double previewSlideStart = 0;
    // Published positions for the minimize animation (dock_slots.h).
    HANDLE slotsMapping = nullptr;
    dock_slots::Table* slots = nullptr;
    dock_animation::Publisher animationTargets;
    std::vector<std::vector<ULONGLONG>> collectionProgramKeys;
    RECT AnimationRect(std::size_t index) {
        const auto window = items[index].window;
        const auto thumb = thumbnails.find(window);
        return ScreenRect(window ? WindowBox(index, thumb == thumbnails.end() ? SIZE{1,1} : thumb->second.source)
                                 : IconRect(index));
    }
    void PublishAnimationTargets() {
        if (!look.animation) { animationTargets.Reset(); return; }
        dock_animation::Targets table{};
        table.bar = HandleToLong(bar);
        table.icons = HandleToLong(icons);
        table.edge = static_cast<LONG>(look.edge);
        if (visible && !desktopLayer.Covered() && layoutX.size() == items.size()) {
            auto add = [&](LONG kind, ULONGLONG key, RECT r, LONG area = 0) {
                if (key && table.count < dock_animation::Capacity)
                    table.slots[table.count++] = {kind, area, key, r.left, r.top, r.right, r.bottom};
            };
            // Keep visible running icons first even if a very large collection fills the table.
            for (auto kind : {dock::Kind::Running, dock::Kind::Window, dock::Kind::Pinned, dock::Kind::Lume, dock::Kind::Collection}) {
                for (std::size_t i = 0; i < items.size(); ++i) {
                    const auto& item = items[i];
                    if (item.kind != kind) continue;
                    const RECT r = AnimationRect(i);
                    const LONG area = kind == dock::Kind::Running || kind == dock::Kind::Window ? dock_animation::RunningArea : 0;
                    if (kind == dock::Kind::Collection) {
                        for (const auto hwnd : item.animationWindows) add(dock_animation::CollectionWindow, hwnd, r);
                        for (const auto key : collectionProgramKeys[i]) add(dock_animation::CollectionProgram, key, r);
                        continue;
                    }
                    for (const auto window : item.animationWindows) add(dock_slots::WindowSlot, window, r, area);
                    if (item.window && item.animationWindows.empty()) add(dock_slots::WindowSlot, item.window, r, area);
                    const auto& path = !item.runningProgram.empty() ? item.runningProgram :
                                       !item.target.empty() ? item.target : item.path;
                    add(dock_slots::ProgramSlot, dock_slots::ProgramKey(OrganizerWide(path)), r, area);
                }
            }
        }
        animationTargets.Publish(MonitorFromWindow(bar, MONITOR_DEFAULTTONEAREST), table);
    }
    void PublishSlots() {
        PublishAnimationTargets();
        if (!ports.publishSlots) return;
        if (!slots) {
            slotsMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(dock_slots::Table),
                                              ports.slotsName.empty() ? dock_slots::MappingName : ports.slotsName.c_str());
            if (!slotsMapping) return;
            slots = static_cast<dock_slots::Table*>(MapViewOfFile(slotsMapping, FILE_MAP_WRITE, 0, 0, sizeof(dock_slots::Table)));
            if (!slots) { CloseHandle(slotsMapping); slotsMapping = nullptr; return; }
        }
        InterlockedIncrement(&slots->sequence);
        LONG count = 0;
        const bool shown = visible && !desktopLayer.Covered() && layoutX.size() == items.size();
        for (std::size_t i = 0; shown && i < items.size() && count < dock_slots::Capacity; ++i) {
            const auto& item = items[i];
            dock_slots::Slot slot{};
            RECT r{};
            if (item.kind == dock::Kind::Window && item.window) {
                const auto found = thumbnails.find(item.window);
                r = ScreenRect(WindowBox(i, found == thumbnails.end() ? SIZE{1, 1} : found->second.source));
                slot.kind = dock_slots::WindowSlot;
                slot.key = item.window;
            } else if (item.kind == dock::Kind::Pinned || item.kind == dock::Kind::Running) {
                const float size = IconSize * motion[Key(item)].scale.value, bottom = Headroom + BarHeight - IconBottom;
                r = ScreenRect(Physical({barLeft + Visual(i), bottom - size, size, size}));
                slot.kind = dock_slots::ProgramSlot;
                slot.key = dock_slots::ProgramKey(OrganizerWide(item.target.empty() ? item.path : item.target));
            } else continue;
            slot.left = r.left; slot.top = r.top; slot.right = r.right; slot.bottom = r.bottom;
            slots->slots[count++] = slot;
        }
        // A restored window leaves the Dock at once, but its restore animation
        // starts a moment later: keep publishing where its thumbnail was.
        const auto now = GetTickCount64();
        for (const auto& slot : lastWindowSlots) {
            const bool still = std::any_of(slots->slots, slots->slots + count, [&](const dock_slots::Slot& s) {
                return s.kind == dock_slots::WindowSlot && s.key == slot.key;
            });
            if (!still) recentWindows.push_back({slot, now});
        }
        recentWindows.erase(std::remove_if(recentWindows.begin(), recentWindows.end(),
                                           [&](const auto& r) { return now - r.second > 1500; }), recentWindows.end());
        lastWindowSlots.clear();
        for (LONG i = 0; i < count; ++i)
            if (slots->slots[i].kind == dock_slots::WindowSlot) lastWindowSlots.push_back(slots->slots[i]);
        for (const auto& r : recentWindows)
            if (count < dock_slots::Capacity) slots->slots[count++] = r.first;
        slots->count = count;
        InterlockedIncrement(&slots->sequence);
    }
    std::vector<dock_slots::Slot> lastWindowSlots;
    std::vector<std::pair<dock_slots::Slot, ULONGLONG>> recentWindows;
    void ReleaseSlots() {
        animationTargets.Reset();
        if (slots) { InterlockedIncrement(&slots->sequence); slots->count = 0; InterlockedIncrement(&slots->sequence); UnmapViewOfFile(slots); }
        slots = nullptr;
        if (slotsMapping) CloseHandle(slotsMapping);
        slotsMapping = nullptr;
    }
    std::vector<dock::Item> items;
    std::vector<std::size_t> order;  // display order (indices into items)
    std::map<std::string, Motion> motion;
    std::vector<float> layoutX;      // animated left edge per item index (bar-relative)
    float barWidth = 0, overlayWidth = 0, barLeft = 0;
    DockSpring anchor;               // bar left edge (icon-layer DIPs) once the pointer leaves
    POINT overlayOrigin{};
    double lastFrame = 0;
    float pointerX = -1e9f;
    std::size_t hover = None, pressed = None, dragged = None, dropSlot = 0;
    POINT pressPoint{};
    float dragX = 0, dragY = 0, grabOffset = 0, grabOffsetY = 0;
    // The dragged icon follows the pointer anywhere on screen in its own window.
    HWND ghost = nullptr;
    std::unique_ptr<WidgetCanvas> ghostCanvas;
    WidgetBitmap ghostBitmap;
    LayeredSurface ghostSurface;
    bool removing = false, pinningRunning = false;
    // Live DWM thumbnails of minimized windows, drawn on the bar window so the
    // icon layer above can put each program's badge over them.
    struct Thumbnail { HTHUMBNAIL handle = nullptr; SIZE source{}; RECT shown{}; bool visible = false; };
    std::map<std::uint64_t, Thumbnail> thumbnails;
    std::vector<dock::Item> pending;
    bool hasPending = false;
    DockAppearance pendingLook;
    // External drop: a spring-opened gap at a pinned slot.
    std::size_t gapSlot = None, lastGapSlot = 0;
    std::size_t collectTarget = None;
    std::size_t discardTarget = None;  // The Recycle Bin under a dragged Dock icon.
    std::string dropCollection;
    DockSpring gap;
    std::unique_ptr<WidgetCanvas> iconCanvas, barCanvas;
    WidgetBitmap iconBitmap, barBitmap;
    LayeredSurface iconSurface, barSurface;
    WidgetMaterialRuntime material;
    LumeMaterialParameters lastMaterial{};
    FrameClock clock;
    DockDesktopLayer desktopLayer;
    OrganizerDropTarget dropTarget;
    Impl(HINSTANCE i, DockWindowPorts p) : instance(i), ports(std::move(p)) {}
    ~Impl() { Stop(); }
    float Px(float dip) const { return dip * static_cast<float>(dpi) / 96; }
    static constexpr float BaseDepth = Headroom + BarHeight + BottomMargin;
    static constexpr float SurfaceDepth = BaseDepth + IconSize * MaxScale;
    bool Vertical() const { return dock::Vertical(look.edge); }
    float AlongOrigin() const { return static_cast<float>(Vertical() ? overlayOrigin.y : overlayOrigin.x) * 96 / dpi; }
    WidgetRect Physical(WidgetRect slot) const {
        switch (look.edge) {
        case dock::Edge::Top: return {slot.x, SurfaceDepth - slot.y - slot.height, slot.width, slot.height};
        case dock::Edge::Left: return {SurfaceDepth - slot.y - slot.height, slot.x, slot.height, slot.width};
        case dock::Edge::Right: return {slot.y, slot.x, slot.height, slot.width};
        default: return slot;
        }
    }
    RECT ScreenRect(WidgetRect physical) const {
        return {overlayOrigin.x + std::lround(Px(physical.x)), overlayOrigin.y + std::lround(Px(physical.y)),
                overlayOrigin.x + std::lround(Px(physical.x + physical.width)),
                overlayOrigin.y + std::lround(Px(physical.y + physical.height))};
    }
    WidgetRect IconRect(std::size_t index) {
        const auto& frame = motion[Key(items[index])];
        const float size = IconSize * frame.scale.value;
        return Physical({barLeft + Visual(index), Headroom + BarHeight - IconBottom - size - frame.lift.value, size, size});
    }
    bool Register(const wchar_t* name) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = Procedure;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = name;
        return RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    bool Create() {
        if (icons) return true;
        if (!Register(IconClass) || !Register(BarClass)) return false;
        bar = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, BarClass, L"Lume Dock",
                              WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
        icons = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, IconClass, L"Lume Dock", WS_POPUP,
                                0, 0, 1, 1, nullptr, nullptr, instance, this);
        if (!bar || !icons) { Stop(); return false; }
        if(!window_layer::Register(icons,window_layer::Kind::Desktop)||
           !window_layer::Register(bar,window_layer::Kind::Desktop,icons)){Stop();return false;}
        desktopLayer.Start(icons, bar);
        iconCanvas = std::make_unique<WidgetCanvas>();
        barCanvas = std::make_unique<WidgetCanvas>();
        // Files dragged from Explorer or the desktop; the drop target only reads paths.
        dropTarget.Attach(icons, [this](const OrganizerDropFeedback& feedback) { DragOver(feedback); },
                          [this](const std::vector<std::string>& paths, POINT point) { return DropPaths(paths, point); },
                          true);  // 回收站, 此电脑 and other shell locations can be pinned too.
        return true;
    }
    // Only the dragged section participates in insertion. Indices always refer
    // to the input snapshot; visual reordering never rewrites that snapshot.
    std::vector<std::size_t> Primary(std::size_t skip = None) const {
        std::vector<std::size_t> result;
        const bool movingRunning = !pinningRunning && dragged != None && dragged < items.size() && items[dragged].kind == dock::Kind::Running;
        for (const auto i : order) {
            if (i == skip) continue;
            if (movingRunning ? items[i].kind == dock::Kind::Running :
                items[i].kind == dock::Kind::Lume || items[i].kind == dock::Kind::Pinned)
                result.push_back(i);
        }
        return result;
    }
    std::vector<std::size_t> RestOrder() const {
        std::vector<std::size_t> result, running;
        for (std::size_t i = 0; i < items.size(); ++i) {
            result.push_back(i);
            if (items[i].kind == dock::Kind::Running) running.push_back(i);
        }
        std::stable_sort(running.begin(), running.end(), [&](auto a, auto b) {
            return std::find(runningOrder.begin(), runningOrder.end(), Key(items[a])) <
                   std::find(runningOrder.begin(), runningOrder.end(), Key(items[b]));
        });
        std::size_t n = 0;
        for (auto& i : result) if (items[i].kind == dock::Kind::Running) i = running[n++];
        return result;
    }
    std::vector<std::size_t> DisplayOrder() const {
        auto result = RestOrder();
        if (dragged == None || items[dragged].kind == dock::Kind::StartMenu) return result;
        auto primary = Primary(dragged);
        if (pinningRunning) {
            result.erase(std::find(result.begin(), result.end(), dragged));
            const auto before = dropSlot < primary.size() ? std::find(result.begin(), result.end(), primary[dropSlot]) :
                std::find_if(result.begin(), result.end(), [&](auto index) { return items[index].kind == dock::Kind::Separator; });
            result.insert(before, dragged);
            return result;
        }
        primary.insert(primary.begin() + static_cast<std::ptrdiff_t>(std::min(dropSlot, primary.size())), dragged);
        const bool running = items[dragged].kind == dock::Kind::Running;
        std::size_t n = 0;
        for (auto& i : result) {
            const auto kind = items[i].kind;
            if (running ? kind == dock::Kind::Running : kind == dock::Kind::Pinned || kind == dock::Kind::Lume)
                i = primary[n++];
        }
        return result;
    }
    float Spacing() const { return static_cast<float>(look.iconSpacing); }
    float RestWidth() const { return Width(items, Spacing()); }
    float GapWidth() const { return gap.value * (IconSize + Spacing()); }
    // Animated layout in display order from the current scales and drop gap.
    void Layout() {
        layoutX.assign(items.size(), 0);
        float x = Padding;
        std::size_t pinned = 0;
        bool gapPlaced = gap.value < .001f;
        for (std::size_t n = 0; n < order.size(); ++n) {
            const auto i = order[n];
            const auto kind = items[i].kind;
            const bool pastPinned = kind == dock::Kind::Separator || kind == dock::Kind::Running ||
                                    kind == dock::Kind::Window || kind == dock::Kind::SystemTray;
            const bool gapAtLume = kind == dock::Kind::Lume && items[i].orderIndex == lastGapSlot;
            if (!gapPlaced && ((kind == dock::Kind::Pinned && pinned == lastGapSlot) || gapAtLume || pastPinned)) {
                x += GapWidth();
                gapPlaced = true;
            }
            if (kind == dock::Kind::Pinned) ++pinned;
            const float scale = motion[Key(items[i])].scale.value;
            const float width = ItemWidth(items[i], scale);
            layoutX[i] = x;
            // An icon dragged out shrinks its slot away, spacing included.
            x += width + (i == dragged && removing ? Spacing() * std::min(1.f, scale) : Spacing());
        }
        if (!gapPlaced) x += GapWidth();
        barWidth = std::max(Padding * 2, x - Spacing() + Padding);
        Anchor();
    }
    float Centered() const { return std::max(0.f, (overlayWidth - barWidth) / 2); }
    float RestAnchor() const {
        const float edge = std::max(0.f, overlayWidth - barWidth);
        const float center = edge / 2;
        const float progress = std::clamp(look.positionOffset, 0, 50) / 50.f;
        if (look.positionMode == dock::PositionLeft) return center * progress;
        if (look.positionMode == dock::PositionRight) return edge - center * progress;
        return center;
    }
    // The rest-layout position (bar-relative) shown at animated position q.
    float RestOf(float q) const {
        float restX = Padding;
        for (std::size_t n = 0; n < order.size(); ++n) {
            const auto i = order[n];
            const float restWidth = ItemWidth(items[i]);
            const auto found = motion.find(Key(items[i]));
            const float scale = found == motion.end() ? 1.f : found->second.scale.value;
            const float width = ItemWidth(items[i], scale);
            const float ax = layoutX[i];
            if (q < ax) return restX - (ax - q);
            if (q < ax + width + Spacing() || n + 1 == order.size()) return restX + (q - ax) / (width + Spacing()) * (restWidth + Spacing());
            restX += restWidth + Spacing();
        }
        return q;
    }
    // The bar stays centred; magnified icons widen it evenly on both sides.
    void Anchor() { barLeft = std::clamp(anchor.value, 0.f, std::max(0.f, overlayWidth - barWidth)); }
    float Visual(std::size_t i) { return layoutX[i] + motion[Key(items[i])].offset.value; }
    // Changes the display order while keeping every icon where it is on screen.
    void Reorder(std::vector<std::size_t> next) {
        if (next == order && layoutX.size() == items.size()) return;
        std::vector<float> before(items.size(), 0);
        const bool had = layoutX.size() == items.size();
        if (had)
            for (std::size_t i = 0; i < items.size(); ++i) before[i] = barLeft + Visual(i);
        order = std::move(next);
        Layout();
        if (had)
            for (std::size_t i = 0; i < items.size(); ++i)
                motion[Key(items[i])].offset.value = before[i] - (barLeft + layoutX[i]);
    }
    bool Position() {
        const auto monitor = look.monitor ? MonitorFromRect(&look.monitor->bounds, MONITOR_DEFAULTTONEAREST) :
            MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) return false;
        if (look.monitor) { info.rcMonitor = look.monitor->bounds; info.rcWork = look.monitor->work; }
        UINT dpiX = 96, dpiY = 96;
        monitorDpi = SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX ? dpiX : 96;
        // The icon size zooms the whole Dock: every DIP below becomes larger.
        // 100% is the standard size for this display: smaller displays (most
        // laptops) get a proportionally smaller Dock.
        const double fit = window::FitScale((info.rcMonitor.right - info.rcMonitor.left) * 96.0 / monitorDpi,
                                            (info.rcMonitor.bottom - info.rcMonitor.top) * 96.0 / monitorDpi);
        dpi = std::max(48u, static_cast<unsigned>(std::lround(monitorDpi * look.iconScale / 100.0 * fit)));
        // With many icons, shrink the whole Dock until it fits the work area.
        const float needed = RestWidth() + IconSize + Spacing();
        const int available = Vertical() ? info.rcWork.bottom - info.rcWork.top : info.rcWork.right - info.rcWork.left;
        if (needed * static_cast<float>(dpi) / 96 > static_cast<float>(available))
            dpi = std::max(1u, static_cast<unsigned>(std::floor(static_cast<float>(available) * 96 / needed)));
        const float workWidth = static_cast<float>(available) * 96 / dpi;
        overlayWidth = std::min(RestWidth() + ExtraWidth + IconSize + Spacing(), workWidth);
        const int width = static_cast<int>(std::lround(Px(overlayWidth)));
        const int height = static_cast<int>(std::lround(Px(BaseDepth)));
        const int flippedDepth = static_cast<int>(std::lround(Px(SurfaceDepth - BaseDepth)));
        const int horizontalSpace = std::max(0, available - width);
        const int towardCenter = MulDiv(horizontalSpace, std::clamp(look.positionOffset, 0, 50), 100);
        const int along = (Vertical() ? info.rcWork.top : info.rcWork.left) +
            (look.positionMode == dock::PositionLeft ? towardCenter :
             look.positionMode == dock::PositionRight ? horizontalSpace - towardCenter : horizontalSpace / 2);
        const LONG offset = MulDiv(look.bottomOffset, static_cast<int>(monitorDpi), 96);
        switch (look.edge) {
        case dock::Edge::Top:
            overlayOrigin = {along, std::min(info.rcWork.top + offset, std::max(info.rcWork.top, info.rcWork.bottom - height)) - flippedDepth};
            break;
        case dock::Edge::Left:
            overlayOrigin = {std::min(info.rcWork.left + offset, std::max(info.rcWork.left, info.rcWork.right - height)) - flippedDepth, along};
            break;
        case dock::Edge::Right: overlayOrigin = {std::max(info.rcWork.left, info.rcWork.right - height - offset), along}; break;
        default: overlayOrigin = {along, std::max(info.rcWork.top, info.rcWork.bottom - height - offset)}; break;
        }
        // The bar's content is constant (a near-transparent fill under the glass):
        // render it once at the widest size and show a slice of it each frame.
        const auto shape = Physical({0, Headroom, overlayWidth, BarHeight});
        if (!barCanvas->Begin(shape.width, shape.height, dpi)) return false;
        barCanvas->Rounded({0, 0, shape.width, shape.height}, 0, 0xffffff, 1.f / 255);
        return barCanvas->Finish(barBitmap) && barSurface.Upload(barBitmap);
    }
    void RestackVisible() {
        if (!icons || !bar || !visible) return;
        // Explorer can hide either layered window during a desktop-host
        // restart. Reinsert both windows as one pair and explicitly show
        // them so a refresh can recover without restarting Lume.
        const bool wasCovered = desktopLayer.Covered();
        desktopLayer.Place();
        if (desktopLayer.Covered() == wasCovered) return;
        if (desktopLayer.Covered()) {
            // Fullscreen protection: release the glass too.
            hovering = false;
            HidePreview();
            material.Detach();
            lastMaterial = {};
        } else if (Paint()) desktopLayer.Place();  // The new glass surface joins the pair.
    }
    bool Show(std::vector<dock::Item> next, const DockAppearance& nextLook) {
        if (!Create()) return false;
        // A periodic refresh must not cancel a drag; apply it when the drag ends.
        if (dragged != None) { pending = std::move(next); pendingLook = nextLook; hasPending = true; return true; }
        hasPending = false;
        BOOL animate = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
        reducedMotion = !animate;
        const bool horizontalPositionChanged = !(look.monitor == nextLook.monitor) || look.edge != nextLook.edge || look.positionMode != nextLook.positionMode ||
                                              look.positionOffset != nextLook.positionOffset;
        // Keep where every surviving icon is on screen, then adopt the new items.
        std::map<std::string, float> screen;
        const float oldOrigin = AlongOrigin();
        if (!horizontalPositionChanged && layoutX.size() == items.size())
            for (std::size_t i = 0; i < items.size(); ++i) screen[Key(items[i])] = barLeft + oldOrigin + Visual(i);
        const float oldLeft = barLeft + oldOrigin;
        items = std::move(next);
        collectionProgramKeys.assign(items.size(), {});
        for (std::size_t i = 0; i < items.size(); ++i)
            for (const auto& program : items[i].collectionPrograms)
                collectionProgramKeys[i].push_back(dock_slots::ProgramKey(OrganizerWide(program)));
        PruneSnapshots();
        runningOrder.erase(std::remove_if(runningOrder.begin(), runningOrder.end(), [&](const auto& key) {
            return std::none_of(items.begin(), items.end(), [&](const auto& item) { return Key(item) == key; });
        }), runningOrder.end());
        if (look.reflectionRatio != nextLook.reflectionRatio || look.edge != nextLook.edge) mirrors.clear();
        look = nextLook;
        if (!dock::ValidEdge(look.edge)) look.edge = dock::Edge::Bottom;
        look.reflectionRatio = std::clamp(look.reflectionRatio, 0, 100);
        look.reflectionTransparency = std::clamp(look.reflectionTransparency, 0, 100);
        look.iconScale = std::clamp(look.iconScale, 50, 200);
        if (!dock::ValidPosition(look.position)) look.position = dock::DefaultPosition;
        if (!dock::ValidPositionMode(look.positionMode)) look.positionMode = dock::PositionCenter;
        if (!dock::ValidPositionOffset(look.positionOffset)) look.positionOffset = dock::DefaultPositionOffset;
        if (!dock::ValidBottomOffset(look.bottomOffset)) look.bottomOffset = dock::DefaultBottomOffset;
        if (!dock::ValidIconSpacing(look.iconSpacing)) look.iconSpacing = dock::DefaultIconSpacing;
        frosted = look.frosted;
        if (look.transparent) { material.Detach(); lastMaterial = {}; }
        pressed = hover = None;
        HidePreview();
        std::map<std::string, Motion> kept;
        for (const auto& item : items) {
            const auto key = Key(item);
            const auto found = motion.find(key);
            if (found != motion.end()) kept[key] = found->second;
            else {
                Motion fresh;
                fresh.scale.value = visible && !reducedMotion ? .3f : 1.f;  // new icons pop in
                kept[key] = fresh;
            }
        }
        motion = std::move(kept);
        if (!Position()) {
            // Let the session retry if a monitor/D2D transition temporarily
            // prevents the new surface from being prepared.
            visible = false;
            return false;
        }
        const float newOrigin = AlongOrigin();
        anchor = {visible && !horizontalPositionChanged ? oldLeft - newOrigin : RestAnchor(), 0};
        order = RestOrder();
        Layout();
        if (!visible || horizontalPositionChanged) { anchor = {RestAnchor(), 0}; barLeft = RestAnchor(); }
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto found = screen.find(Key(items[i]));
            if (found != screen.end()) motion[Key(items[i])].offset.value = found->second - (barLeft + newOrigin + layoutX[i]);
        }
        visible = true;
        if (!Paint()) {
            // A transient D2D/layered-window failure must not leave a
            // logically-running but unrendered Dock that future refreshes
            // would incorrectly skip.
            visible = false;
            return false;
        }
        RestackVisible();
        Animate();
        return true;
    }
    void Animate() {
        if (!icons || clock.Running()) return;
        lastFrame = AnimationSeconds();
        clock.Start(icons, FrameMessage);
    }
    bool Step() {
        const double now = AnimationSeconds();
        const float dt = std::clamp(static_cast<float>(now - lastFrame), .0005f, .05f);
        lastFrame = now;
        std::vector<dock::Item> rest;
        for (const auto i : order) rest.push_back(items[i]);
        const float pointerRest = layoutX.size() == items.size() ? RestOf(pointerX - barLeft) : -1e9f;
        bool moving = false;
        for (std::size_t n = 0; n < order.size(); ++n) {
            const auto i = order[n];
            auto& m = motion[Key(items[i])];
            const auto slot = Slot(rest, n, Spacing());
            float target = 1;
            if (i == dragged) target = removing ? 0.f : DragScale;
            else if (hovering && dragged == None && gapSlot == None && items[i].kind != dock::Kind::Separator)
                target = Magnification(std::abs(pointerRest - (slot.x + slot.width / 2)), Spacing());
            moving |= m.scale.Step(target, dt, MagnifyOmega, MagnifyZeta, .0005f, reducedMotion);
            moving |= m.offset.Step(0, dt, SlideOmega, SlideZeta, .05f, reducedMotion);
            moving |= m.lift.Step(i == dragged ? DragLift : 0.f, dt, SlideOmega, SlideZeta, .05f, reducedMotion);
        }
        moving |= gap.Step(gapSlot == None ? 0.f : 1.f, dt, SlideOmega, SlideZeta, .002f, reducedMotion);
        Layout();
        moving |= anchor.Step(RestAnchor(), dt, SlideOmega, SlideZeta, .05f, reducedMotion);
        Anchor();
        return moving;
    }
    bool Paint() {
        if (!icons || !visible || !iconCanvas) return false;
        if (look.reflection) {
            for (auto it = mirrors.begin(); it != mirrors.end();)
                it = it->second.first.expired() ? mirrors.erase(it) : std::next(it);
        }
        if (!iconCanvas->Begin(Vertical() ? SurfaceDepth : overlayWidth, Vertical() ? overlayWidth : SurfaceDepth, dpi)) {
            visible = false; return false;
        }
        // A near-transparent body over the bar keeps it hit-testable; the
        // headroom stays fully transparent and passes clicks through.
        const auto body = Physical({barLeft, Headroom, barWidth, BarHeight});
        iconCanvas->Rounded(body, CornerRadius, 0xffffff, 1.f / 255);
        const auto barRect = ScreenRect(body);
        const POINT barOrigin{barRect.left, barRect.top};
        const SIZE barSize{std::lround(Px(body.width)), std::lround(Px(body.height))};
        if (!barSurface.Compose(bar, barOrigin, barSize)) { visible = false; return false; }
        UpdateThumbnails(barOrigin);
        std::size_t labelled = None;
        for (const auto i : order) {
            if (i == dragged) continue;
            DrawItem(i);
            if (i == hover && dragged == None && gapSlot == None && items[i].kind != dock::Kind::Separator) labelled = i;
        }
        if (labelled != None && previewIndex == None) DrawLabel(labelled);
        if (dragged != None) PaintGhost();
        if (!iconCanvas->Finish(iconBitmap) || !iconSurface.Present(icons, iconBitmap, &overlayOrigin)) {
            visible = false;
            return false;
        }
        PublishSlots();
        if (look.transparent || desktopLayer.Covered()) return true;  // "无": no glass under the icons
        LumeMaterialParameters p;
        std::memset(&p, 0, sizeof(p));
        p.size = sizeof(p);
        p.owner = bar;
        p.frosted = frosted;
        // Profile 3 selects Dock optics in the liquid shader and the cool tint
        // in the frosted shader.
        p.opticalProfile = 3;
        p.lightForeground = 1;
        // The material measures in the monitor's DIPs, not the zoomed ones.
        p.cornerRadius = CornerRadius * look.iconScale / 100.f;
        p.reducedMotion = reducedMotion;
        // While an icon is dragged (a Dock reorder, or a launchpad item
        // hovering for a drop) the pointer sits on the bar, and a moving
        // contact point makes the engine re-render refraction every frame.
        // Keep the last contact for the gesture, as the launchpad does.
        if ((dragged != None || gapSlot != None || !dropCollection.empty()) && lastMaterial.size) {
            p.contactX = lastMaterial.contactX;
            p.contactY = lastMaterial.contactY;
        } else {
            MaterialContact(bar, body.width, body.height, dpi, p.contactX, p.contactY);
        }
        SubmitMaterial(material, p, lastMaterial);
        return true;
    }
    void DrawItem(std::size_t i) {
        const auto& item = items[i];
        auto& m = motion[Key(item)];
        const float bottom = Headroom + BarHeight - IconBottom;
        if (item.kind == dock::Kind::Separator) {
            const float x = barLeft + Visual(i);
            // Nearly the height of the glass, as on macOS.
            const auto rule = Physical({x + .5f, Headroom + 12, 0.f, BarHeight - 24});
            iconCanvas->Line(rule.x, rule.y, rule.x + rule.width, rule.y + rule.height, 0xffffff, .7f, 2);
            return;
        }
        const float size = IconSize * m.scale.value;
        float left = barLeft + Visual(i);
        if (i == dragged) left = dragX - grabOffset * m.scale.value;
        const WidgetRect r = Physical({left, bottom - size - m.lift.value, size, size});
        if (item.window) {
            const auto cover = item.kind == dock::Kind::Window && ports.cover ? ports.cover(item) : nullptr;
            const auto found = thumbnails.find(item.window);
            const auto snapshot = cover ? nullptr : BarSnapshot(item);
            const auto box = WindowBox(i, snapshot ? SIZE{static_cast<LONG>(snapshot->width), static_cast<LONG>(snapshot->height)}
                                                   : cover || found == thumbnails.end() ? SIZE{1, 1} : found->second.source);
            if (cover) iconCanvas->ImageBitmap(cover, box);
            else if (snapshot) iconCanvas->ImageBitmap(snapshot, box);
            if (cover || snapshot || (found != thumbnails.end() && found->second.visible)) {
                const float badge = size * BadgeSize;
                if (const auto icon = Icon(item))
                    iconCanvas->ImageBitmap(icon, {box.x + box.width - badge * .78f, box.y + box.height - badge * .86f, badge, badge});
                return;
            }
            // Fall back to the app icon if DWM cannot supply a thumbnail.
        }
        if (item.kind == dock::Kind::Launchpad) LaunchpadGlyph(*iconCanvas, r);
        else if (item.kind == dock::Kind::StartMenu) StartGlyph(*iconCanvas, r);
        else if (item.kind == dock::Kind::SystemTray) ControlCenterGlyph(*iconCanvas, r);
        else if (item.kind == dock::Kind::Collection) {
            const bool target = i == collectTarget || i == discardTarget || item.path == dropCollection;
            iconCanvas->Rounded(r, size * .22f, 0xffffff, target ? .28f : frosted ? .16f : .10f);
            const float mini = size * .29f, margin = size * .14f, pitch = size * .43f;
            for (std::size_t n = 0; n < item.previewPaths.size() && n < 4; ++n) {
                const WidgetRect slot{r.x + margin + static_cast<float>(n % 2) * pitch,
                                      r.y + margin + static_cast<float>(n / 2) * pitch, mini, mini};
                const auto image = Icon({dock::Kind::Pinned, item.previewPaths[n], {}});
                if (image) iconCanvas->ImageBitmap(image, slot);
                else iconCanvas->Rounded(slot, mini * .2f, 0xffffff, .2f);
            }
        }
        else if (const auto bitmap = Icon(item)) {
            iconCanvas->ImageBitmap(bitmap, r);
            // The reflection stays on the row while the icon lifts or is dragged.
            if (look.reflection && i != dragged && look.reflectionRatio > 0 && look.reflectionTransparency < 100) {
                const float reflectionTop = bottom + 1;
                const float mirrorHeight = size * look.reflectionRatio / 100.f;
                if (mirrorHeight > 0)
                    iconCanvas->ImageBitmap(Mirror(bitmap), Physical({left, reflectionTop, size, size}),
                                            (100 - look.reflectionTransparency) / 100.f);
            }
        }
        else iconCanvas->Rounded(r, size * .22f, 0xffffff, .14f);
        if (item.running && i != dragged)
            iconCanvas->Rounded(Physical({left + size / 2 - 2, bottom + 6, 4, 4}), 2, 0xffffff, .85f);
    }
    std::shared_ptr<const WidgetBitmap> Icon(const dock::Item& item) {
        return ports.icon ? ports.icon(item) : nullptr;
    }
    // A non-modal gallery for both single-window and grouped running icons.
    // Moving through the gap into the gallery gets a grace period.
    void SyncPreview() {
        const bool valid = hover < items.size() && dragged == None &&
            (items[hover].kind == dock::Kind::Window || items[hover].kind == dock::Kind::Running ||
             ((items[hover].kind == dock::Kind::Pinned || items[hover].kind == dock::Kind::Lume) && items[hover].running));
        if (valid && (hover == previewIndex || hover == pendingPreview)) {
            KillTimer(icons, PreviewLeaveTimer);
            previewLeaving = false;
            return;
        }
        if (!valid && hover == None && dragged == None && previewIndex != None) {
            if (!previewLeaving) {
                previewLeaving = true;
                SetTimer(icons, PreviewLeaveTimer, 350, nullptr);
            }
            return;
        }
        HidePreview();
        if (!valid || !icons) return;
        pendingPreview = hover;
        SetTimer(icons, PreviewTimer, PreviewHoverDelayMs, nullptr);
    }
    bool previewLeaving = false;
    void ReleasePreviewCards() {
        for (auto& card : previewCards)
            if (card.thumbnail) DwmUnregisterThumbnail(card.thumbnail);
        previewCards.clear();
    }
    void HidePreview() {
        if (icons) {
            KillTimer(icons, PreviewTimer);
            KillTimer(icons, PreviewLeaveTimer);
        }
        pendingPreview = previewIndex = None;
        previewSlideStart = 0;
        previewPressed = 0;
        previewLeaving = false;
        ReleasePreviewCards();
        previewWindows.clear();
        if (preview) ShowWindow(preview, SW_HIDE);
    }
    std::uint64_t PreviewHit(POINT point) const {
        for (const auto& card : previewCards)
            if (PtInRect(&card.rect, point)) return card.window.handle;
        return 0;
    }
    bool ContainsPreviewPoint(POINT screen) const {
        RECT bounds{};
        return preview && IsWindowVisible(preview) && GetWindowRect(preview, &bounds) && PtInRect(&bounds, screen);
    }
    void ShowPreview(std::size_t index) {
        if (index >= items.size() || layoutX.size() != items.size()) return;
        HidePreview();
        PruneSnapshots();
        const auto& item = items[index];
        if (item.kind == dock::Kind::Window)
            previewWindows.push_back({item.path, item.name, item.window, true});
        else if (ports.windowsFor) previewWindows = ports.windowsFor(item);
        else previewWindows = dock_sources::ProgramWindowsFor(item.path);
        previewWindows.erase(std::remove_if(previewWindows.begin(), previewWindows.end(), [](const auto& window) {
            return !IsWindow(reinterpret_cast<HWND>(window.handle));
        }), previewWindows.end());
        if (previewWindows.empty()) return;
        if (!preview) {
            if (!Register(PreviewClass)) return;
            // Accept input: WS_EX_TRANSPARENT would send preview clicks through.
            preview = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                PreviewClass, L"Lume Dock 窗口预览", WS_POPUP, 0, 0, 1, 1, icons, nullptr, instance, this);
            if (!preview) return;
            window_layer::Register(preview,window_layer::Kind::Popup,icons);
            previewCanvas = std::make_unique<WidgetCanvas>();
        }
        previewIndex = index;
        previewPage = 0;
        PaintPreview(true);
    }
    void PaintPreview(bool opening = false) {
        if (previewIndex >= items.size() || previewWindows.empty()) { HidePreview(); return; }
        ReleasePreviewCards();
        MONITORINFO info{sizeof(info)};
        GetMonitorInfoW(MonitorFromWindow(icons, MONITOR_DEFAULTTOPRIMARY), &info);
        const float unit = static_cast<float>(monitorDpi) / 96;
        const float availableWidth = (info.rcWork.right - info.rcWork.left) / unit - 24;
        const float availableHeight = (info.rcWork.bottom - info.rcWork.top) / unit - BarHeight - 40;
        constexpr float cardWidth = 216, cardHeight = 154, cardGap = 12, margin = 0, header = 0;
        const auto columns = std::min(previewWindows.size(),
            static_cast<std::size_t>(std::max(1, std::min(5, static_cast<int>((availableWidth - margin * 2 + cardGap) / (cardWidth + cardGap))))));
        const auto maxRows = static_cast<std::size_t>(std::max(1, std::min(3,
            static_cast<int>((availableHeight - header - margin * 2 + cardGap) / (cardHeight + cardGap)))));
        previewPageSize = columns * maxRows;
        const auto pages = (previewWindows.size() + previewPageSize - 1) / previewPageSize;
        previewPage = std::min(previewPage, pages - 1);
        const auto begin = previewPage * previewPageSize;
        const auto count = std::min(previewPageSize, previewWindows.size() - begin);
        const auto rows = (count + columns - 1) / columns;
        const float width = margin * 2 + static_cast<float>(columns) * (cardWidth + cardGap) - cardGap;
        const float height = margin * 2 + header + static_cast<float>(rows) * (cardHeight + cardGap) - cardGap;
        if (!previewCanvas->Begin(width, height, monitorDpi)) { HidePreview(); return; }
        // Only the original windows are visible. The transparent host provides
        // placement/input without adding a panel, card frame, or duplicate title.
        auto title = OrganizerWide(items[previewIndex].name) + L" · " + std::to_wstring(previewWindows.size()) + L" 个窗口";
        if (pages > 1) title += L"  (" + std::to_wstring(previewPage + 1) + L"/" + std::to_wstring(pages) + L"，滚轮切换)";
        SetWindowTextW(preview, title.c_str());
        const auto pixels = [unit](WidgetRect r) {
            return RECT{static_cast<LONG>(std::lround(r.x * unit)), static_cast<LONG>(std::lround(r.y * unit)),
                static_cast<LONG>(std::lround((r.x + r.width) * unit)), static_cast<LONG>(std::lround((r.y + r.height) * unit))};
        };
        for (std::size_t n = 0; n < count; ++n) {
            const auto& window = previewWindows[begin + n];
            const WidgetRect card{margin + static_cast<float>(n % columns) * (cardWidth + cardGap),
                margin + header + static_cast<float>(n / columns) * (cardHeight + cardGap), cardWidth, cardHeight};
            PreviewCard entry{window, pixels(card)};
            const auto snapshot = Snapshot(window.handle, window.executable);
            SIZE size{};
            if (snapshot) size = {static_cast<LONG>(snapshot->width), static_cast<LONG>(snapshot->height)};
            else if (SUCCEEDED(DwmRegisterThumbnail(preview, reinterpret_cast<HWND>(window.handle), &entry.thumbnail)))
                DwmQueryThumbnailSourceSize(entry.thumbnail, &size);
            WidgetRect image = card;
            if (size.cx > 0 && size.cy > 0) {
                const float fit = std::min(image.width / size.cx, image.height / size.cy);
                const float w = size.cx * fit, h = size.cy * fit;
                image = {image.x + (image.width - w) / 2, image.y + image.height - h, w, h};
                // DWM paints separately from the layered bitmap. A practically
                // transparent input surface keeps the thumbnail clickable.
                previewCanvas->Rounded(image, 0, 0xffffff, 1.f / 255);
                entry.rect = pixels(image);
                if (snapshot) previewCanvas->ImageBitmap(snapshot, image);
            } else {
                previewCanvas->Rounded(image, 0, 0xffffff, 1.f / 255);
                if (const auto icon = Icon(items[previewIndex]))
                    previewCanvas->ImageBitmap(icon, {image.x + (image.width - 48) / 2, image.y + 10, 48, 48});
                const auto caption = window.title.empty() ? L"点击打开窗口" : OrganizerWide(window.title);
                previewCanvas->Text(FitText(*previewCanvas, caption, image.width, 12), {image.x, image.y + image.height - 24, image.width, 24},
                    12, 0xffffff, .7f, false, WidgetTextAlign::Center);
            }
            previewCards.push_back(std::move(entry));
        }
        if (!previewCanvas->Finish(previewBitmap)) { HidePreview(); return; }
        const float size = IconSize * motion[Key(items[previewIndex])].scale.value;
        const LONG w = static_cast<LONG>(previewBitmap.width), h = static_cast<LONG>(previewBitmap.height);
        const auto anchorRect = ScreenRect(Physical({barLeft + Visual(previewIndex), std::min(Headroom, Headroom + BarHeight - IconBottom - size),
                                                      size, std::max(BarHeight, size)}));
        LONG left = (anchorRect.left + anchorRect.right - w) / 2;
        LONG top = (anchorRect.top + anchorRect.bottom - h) / 2;
        switch (look.edge) {
        case dock::Edge::Top: top = anchorRect.bottom + 8; break;
        case dock::Edge::Left: left = anchorRect.right + 8; break;
        case dock::Edge::Right: left = anchorRect.left - w - 8; break;
        default: top = anchorRect.top - h - 8; break;
        }
        previewOrigin = {
            std::clamp(left, info.rcWork.left + 8, std::max(info.rcWork.left + 8, info.rcWork.right - w - 8)),
            std::clamp(top, info.rcWork.top + 8, std::max(info.rcWork.top + 8, info.rcWork.bottom - h - 8))};
        if (!previewSurface.Upload(previewBitmap)) { HidePreview(); return; }
        if (opening) {
            window_layer::SetFullscreenBypass(preview, desktopLayer.Summoned());
            if (!reducedMotion) previewSlideStart = AnimationSeconds();
        }
        PresentPreviewFrame();
        if (previewIndex == None) return;
        if (opening) {
            const bool summoned = desktopLayer.Summoned();
            SetWindowPos(preview, summoned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
        }
        if (previewSlideStart) Animate();
    }
    void PresentPreviewFrame() {
        const double progress = previewSlideStart ? std::clamp((AnimationSeconds() - previewSlideStart) / .22, 0., 1.) : 1.;
        const double remaining = (1. - progress) * (1. - progress) * (1. - progress);
        const LONG offset = std::lround(36. * monitorDpi / 96. * remaining);
        const BYTE opacity = static_cast<BYTE>(std::lround(255. * (1. - remaining)));
        POINT position = previewOrigin;
        switch (look.edge) {
        case dock::Edge::Top: position.y -= offset; break;
        case dock::Edge::Left: position.x -= offset; break;
        case dock::Edge::Right: position.x += offset; break;
        default: position.y += offset; break;
        }
        const SIZE size{static_cast<LONG>(previewBitmap.width), static_cast<LONG>(previewBitmap.height)};
        if (!previewSurface.Compose(preview, position, size, {}, opacity)) { HidePreview(); return; }
        for (const auto& card : previewCards) {
            if (!card.thumbnail) continue;
            DWM_THUMBNAIL_PROPERTIES properties{};
            properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY;
            properties.rcDestination = card.rect;
            properties.fVisible = TRUE;
            properties.opacity = opacity;
            DwmUpdateThumbnailProperties(card.thumbnail, &properties);
        }
        if (progress >= 1.) previewSlideStart = 0;
    }
    // Where a minimized window is drawn (icon-layer DIPs): its shape fitted
    // into the icon's square, standing on the icon baseline.
    WidgetRect WindowBox(std::size_t i, SIZE source) {
        const float size = IconSize * motion[Key(items[i])].scale.value;
        const float left = barLeft + Visual(i), bottom = Headroom + BarHeight - IconBottom - motion[Key(items[i])].lift.value;
        const float aspect = source.cx > 0 && source.cy > 0 ? static_cast<float>(source.cx) / source.cy : 1.f;
        float width = size, height = size / aspect;
        if (height > size * ThumbnailHeight) { height = size * ThumbnailHeight; width = height * aspect; }
        if (Vertical()) return Physical({left + (size - height) / 2, bottom - width, height, width});
        return Physical({left + (size - width) / 2, bottom - height, width, height});
    }
    std::shared_ptr<const WidgetBitmap> BarSnapshot(const dock::Item& item) {
        if (item.kind == dock::Kind::Running && !IsIconic(reinterpret_cast<HWND>(item.window))) return nullptr;
        return Snapshot(item.window, item.path);
    }
    void UpdateThumbnails(POINT barOrigin) {
        std::map<std::uint64_t, bool> live;
        const bool show = !desktopLayer.Covered();
        for (const auto i : order) {
            const auto& item = items[i];
            if (!item.window || i == dragged) continue;
            const auto source = reinterpret_cast<HWND>(item.window);
            if (!IsWindow(source)) continue;
            live[item.window] = true;
            auto& t = thumbnails[item.window];
            if (!t.handle) {
                if (FAILED(DwmRegisterThumbnail(bar, source, &t.handle))) { t.handle = nullptr; continue; }
                DwmQueryThumbnailSourceSize(t.handle, &t.source);
            }
            const bool cover = item.kind == dock::Kind::Window && ports.cover && ports.cover(item);
            // A snapshot from just before minimizing replaces DWM's image (grey for some apps).
            const auto snapshot = BarSnapshot(item);
            const auto box = WindowBox(i, snapshot ? SIZE{static_cast<LONG>(snapshot->width), static_cast<LONG>(snapshot->height)} : t.source);
            const LONG dx = overlayOrigin.x - barOrigin.x, dy = overlayOrigin.y - barOrigin.y;
            const RECT shown{static_cast<LONG>(std::lround(Px(box.x))) + dx, static_cast<LONG>(std::lround(Px(box.y))) + dy,
                             static_cast<LONG>(std::lround(Px(box.x + box.width))) + dx,
                             static_cast<LONG>(std::lround(Px(box.y + box.height))) + dy};
            const bool visibleNow = show && !cover && !snapshot && shown.right > shown.left + 1 && shown.bottom > shown.top + 1;
            if (visibleNow == t.visible && EqualRect(&shown, &t.shown)) continue;
            DWM_THUMBNAIL_PROPERTIES p{};
            p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
            p.rcDestination = shown;
            p.fVisible = visibleNow;
            p.opacity = 255;
            p.fSourceClientAreaOnly = FALSE;
            if (SUCCEEDED(DwmUpdateThumbnailProperties(t.handle, &p))) { t.shown = shown; t.visible = visibleNow; }
        }
        for (auto it = thumbnails.begin(); it != thumbnails.end();) {
            if (live.count(it->first)) { ++it; continue; }
            if (it->second.handle) DwmUnregisterThumbnail(it->second.handle);
            it = thumbnails.erase(it);
        }
    }
    // Snapshots published by the minimize animation, re-read when their serial changes.
    struct CachedSnapshot {
        LONG serial = -1;
        std::shared_ptr<const WidgetBitmap> image;
        ULONGLONG checked = 0;
        std::string programKey;
    };
    std::map<std::uint64_t, CachedSnapshot> snapshots;
    void PruneSnapshots() {
        if (snapshots.empty()) return;
        std::set<std::uint64_t> windows;
        std::set<std::string> programs;
        for (const auto& item : items) {
            if (item.kind == dock::Kind::Window) windows.insert(item.window);
            else if (item.kind == dock::Kind::Running || item.kind == dock::Kind::Pinned) {
                programs.insert(dock_sources::PathKey(item.target.empty() ? item.path : item.target));
                if (!item.runningProgram.empty()) programs.insert(dock_sources::PathKey(item.runningProgram));
            }
        }
        // Keep a live minimized window's last image: DWM may no longer have
        // usable pixels for it. Historical windows and removed programs do
        // not need that protection, and must not accumulate for a Dock session.
        for (auto it = snapshots.begin(); it != snapshots.end();) {
            if (!IsWindow(reinterpret_cast<HWND>(it->first)) ||
                (!windows.count(it->first) && !programs.count(it->second.programKey)))
                it = snapshots.erase(it);
            else ++it;
        }
    }
    std::shared_ptr<const WidgetBitmap> Snapshot(std::uint64_t window, const std::string& program) {
        if (!window || !IsWindow(reinterpret_cast<HWND>(window))) {
            snapshots.erase(window);
            return nullptr;
        }
        const auto programKey = dock_sources::PathKey(program);
        auto& cached = snapshots[window];
        if (cached.programKey != programKey) {
            cached = {};
            cached.programKey = programKey;
        }
        const auto now = GetTickCount64();
        if (cached.checked && now - cached.checked < 250) return cached.image;
        cached.checked = now;
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, dock_slots::SnapshotName(window).c_str());
        if (!mapping) { cached.serial = -1; cached.image.reset(); return nullptr; }
        const auto* view = static_cast<const unsigned char*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, dock_slots::SnapshotBytes));
        if (view) {
            const auto& header = *reinterpret_cast<const dock_slots::SnapshotHeader*>(view);
            const LONG serial = header.serial;
            const LONG w = header.width, h = header.height;
            if (!(serial & 1) && serial != cached.serial && w > 0 && h > 0 && w <= dock_slots::SnapshotSide && h <= dock_slots::SnapshotSide) {
                auto image = std::make_shared<WidgetBitmap>();
                image->width = static_cast<unsigned>(w);
                image->height = static_cast<unsigned>(h);
                image->pixels.resize(static_cast<std::size_t>(w) * h);
                std::memcpy(image->pixels.data(), view + sizeof(dock_slots::SnapshotHeader), image->pixels.size() * 4);
                MemoryBarrier();
                if (header.serial == serial) { cached.serial = serial; cached.image = std::move(image); }
            }
            UnmapViewOfFile(view);
        }
        CloseHandle(mapping);
        return cached.image;
    }
    void ReleaseThumbnails() {
        snapshots.clear();
        for (auto& [window, t] : thumbnails)
            if (t.handle) DwmUnregisterThumbnail(t.handle);
        thumbnails.clear();
    }
    bool EnsureGhost() {
        if (ghost) return true;
        if (!Register(GhostClass)) return false;
        ghost = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                                GhostClass, L"Lume Dock", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
        if (!ghost) return false;
        window_layer::Register(ghost,window_layer::Kind::Popup,icons);
        ghostCanvas = std::make_unique<WidgetCanvas>();
        return true;
    }
    // The dragged icon under the pointer, with "移除" above it once releasing
    // would take it off the Dock.
    void PaintGhost() {
        if (dragged >= items.size() || !EnsureGhost()) return;
        const auto& item = items[dragged];
        const float size = IconSize * DragScale, labelHeight = 26, labelGap = 8;
        const float width = std::max(size, 88.f), height = labelHeight + labelGap + size;
        if (!ghostCanvas->Begin(width, height, dpi)) return;
        const WidgetRect r{(width - size) / 2, labelHeight + labelGap, size, size};
        if (const auto bitmap = Icon(item)) ghostCanvas->ImageBitmap(bitmap, r, removing ? .8f : 1.f);
        else ghostCanvas->Rounded(r, size * .22f, 0xffffff, .14f);
        if (removing) {
            const std::wstring label = item.kind == dock::Kind::Running ? L"关闭窗口" : L"移除图标";
            SetWindowTextW(ghost, label.c_str());
            const float labelWidth = ghostCanvas->MeasureText(label, 12) + 22;
            const WidgetRect pill{(width - labelWidth) / 2, 0, labelWidth, labelHeight};
            ghostCanvas->Rounded(pill, 8, 0x1c1c1e, .78f);
            ghostCanvas->Text(label, pill, 12, 0xffffff, .95f, false, WidgetTextAlign::Center);
        }
        if (!ghostCanvas->Finish(ghostBitmap)) return;
        const auto slot = Physical({dragX - grabOffset * DragScale, dragY - grabOffsetY * DragScale, size, size});
        const float left = slot.x - (width - size) / 2;
        const float top = slot.y - (labelHeight + labelGap);
        const POINT origin{overlayOrigin.x + static_cast<LONG>(std::lround(Px(left))),
                           overlayOrigin.y + static_cast<LONG>(std::lround(Px(top)))};
        if (ghostSurface.Present(ghost, ghostBitmap, &origin) && !IsWindowVisible(ghost))
            ShowWindow(ghost, SW_SHOWNOACTIVATE);
    }
    void HideGhost() { if (ghost) ShowWindow(ghost, SW_HIDE); }
    // The mirrored portion is cached for the current ratio.
    std::shared_ptr<const WidgetBitmap> Mirror(const std::shared_ptr<const WidgetBitmap>& source) {
        const auto found = mirrors.find(source.get());
        if (found != mirrors.end()) return found->second.second;
        auto result=std::make_shared<const WidgetBitmap>(MakeDockReflection(*source, look.reflectionRatio, look.edge));
        mirrors[source.get()] = {source, result};
        return result;
    }
    void DrawLabel(std::size_t i) {
        const auto& m = motion[Key(items[i])];
        const float size = IconSize * m.scale.value;
        const auto icon = IconRect(i);
        const float textSpace = Vertical() ? (look.edge == dock::Edge::Left ? SurfaceDepth - icon.x - size : icon.x) - 28 : 220;
        const auto text = FitText(*iconCanvas, OrganizerWide(items[i].name), std::max(24.f, textSpace), 12);
        const float width = iconCanvas->MeasureText(text, 12) + 20, height = 24;
        float x = icon.x + (size - width) / 2, y = icon.y + (size - height) / 2;
        switch (look.edge) {
        case dock::Edge::Top: y = icon.y + size + 8; break;
        case dock::Edge::Left: x = icon.x + size + 8; break;
        case dock::Edge::Right: x = icon.x - width - 8; break;
        default: y = icon.y - height - 8; break;
        }
        x = std::clamp(x, 0.f, std::max(0.f, (Vertical() ? SurfaceDepth : overlayWidth) - width));
        y = std::clamp(y, 0.f, std::max(0.f, (Vertical() ? overlayWidth : SurfaceDepth) - height));
        iconCanvas->Rounded({x, y, width, height}, 8, 0x1c1c1e, .72f);
        iconCanvas->Text(text, {x, y, width, height}, 12, 0xffffff, .95f, false, WidgetTextAlign::Center);
    }
    // Animated hit test in icon-layer coordinates.
    std::size_t HitAt(float x, float y) {
        if (y < Headroom - IconSize * (MaxScale - 1) || y > Headroom + BarHeight || x < barLeft || x > barLeft + barWidth)
            return None;
        for (const auto i : order) {
            if (items[i].kind == dock::Kind::Separator) continue;
            const float left = barLeft + Visual(i);
            const float width = ItemWidth(items[i], motion[Key(items[i])].scale.value);
            const float top = Headroom - IconSize * (MaxScale - 1), bottom = Headroom + BarHeight;
            if (y >= top && y <= bottom && x >= left - Spacing() / 2 && x < left + width + Spacing() / 2) return i;
        }
        return None;
    }
    float Dip(LPARAM l, bool vertical) const {
        const auto value = static_cast<float>((vertical != Vertical()) ? GET_Y_LPARAM(l) : GET_X_LPARAM(l)) * 96 / dpi;
        return vertical && (look.edge == dock::Edge::Top || look.edge == dock::Edge::Left) ? SurfaceDepth - value : value;
    }
    void Pointer(float x, float y) {
        hovering = dragged != None || (y >= Headroom && y <= Headroom + BarHeight && x >= barLeft && x <= barLeft + barWidth) ||
                   (hover != None && y >= Headroom - IconSize * (MaxScale - 1));
        pointerX = x;
        hover = hovering ? HitAt(x, y) : None;
        SyncPreview();
        Animate();
    }
    void Leave() { hovering = false; hover = None; SyncPreview(); Animate(); }
    // Pinned insertion slot for a point in icon-layer DIPs, against the rest
    // layout centred where the bar is now. Animated positions are not used: they
    // still carry hover magnification when a drag starts and skew the slot.
    std::size_t SlotAt(float x, std::size_t skip) const {
        const float restLeft = barLeft + (barWidth - RestWidth()) / 2;
        return DropSlot(items, x - restLeft, skip, Spacing());
    }
    std::size_t TargetAt(float x, std::size_t skip) const {
        // Use the current preview order so the insertion threshold follows
        // the icons that have already made room for the dragged item.
        const auto primary = Primary(skip);
        std::size_t slot = 0;
        for (const auto i : primary) {
            const auto found = motion.find(Key(items[i]));
            const float scale = found == motion.end() ? 1.f : found->second.scale.value;
            const float center = barLeft + layoutX[i] + IconSize * scale / 2;
            if (x >= center) ++slot;
        }
        return std::min(slot, primary.size());
    }
    struct PlaceTarget {
        std::size_t pinnedSlot = None;
        std::size_t lumePosition = None;
    };
    PlaceTarget PersistenceTarget(std::size_t skip, std::size_t insertion) const {
        auto primary = Primary(skip);
        insertion = std::min(insertion, primary.size());
        primary.insert(primary.begin() + static_cast<std::ptrdiff_t>(insertion), skip);
        PlaceTarget target;
        std::size_t pinnedBefore = 0;
        for (std::size_t i = 0; i < primary.size(); ++i) {
            const auto kind = items[primary[i]].kind;
            if (primary[i] == skip && (kind == dock::Kind::Pinned || kind == dock::Kind::Running))
                target.pinnedSlot = pinnedBefore;
            if (kind == dock::Kind::Lume)
                target.lumePosition = pinnedBefore;
            if (kind == dock::Kind::Pinned || (primary[i] == skip && kind == dock::Kind::Running))
                ++pinnedBefore;
        }
        return target;
    }
    // Centre of the dragged icon as drawn.
    float DraggedCentre() const {
        const auto found = motion.find(Key(items[dragged]));
        const float scale = found == motion.end() ? 1.f : found->second.scale.value;
        return dragX - grabOffset * scale + IconSize * scale / 2;
    }
    void BeginDrag() {
        dragged = pressed;
        pinningRunning = false;
        const float scale = motion[Key(items[dragged])].scale.value;
        const auto start = MAKELPARAM(pressPoint.x, pressPoint.y);
        grabOffset = std::clamp(Dip(start, false) - (barLeft + Visual(dragged)), 0.f, IconSize * scale) / scale;
        const float top = Headroom + BarHeight - IconBottom - IconSize * scale - motion[Key(items[dragged])].lift.value;
        grabOffsetY = std::clamp(Dip(start, true) - top, 0.f, IconSize * scale) / scale;
        removing = false;
        dropSlot = TargetAt(DraggedCentre(), dragged);
        Reorder(DisplayOrder());
        Animate();
    }
    void Drag(float x, float y) {
        dragX = x;
        dragY = y;
        // Shortcuts leave the Dock; running items request their windows to close.
        const bool outside = y < Headroom - RemoveDistance || y > Headroom + BarHeight + RemoveDistance || x < barLeft - RemoveDistance ||
                             x > barLeft + barWidth + RemoveDistance;
        const bool nextRemoving = outside && Removable(items[dragged]);
        if (nextRemoving != removing) {
            removing = nextRemoving;
            if (!removing) {
                dropSlot = TargetAt(DraggedCentre(), dragged);
                Reorder(DisplayOrder());
            }
        }
        if (removing) { collectTarget = discardTarget = None; Animate(); return; }
        const auto hit = HitAt(x, y);
        collectTarget = hit != None && items[hit].kind == dock::Kind::Collection &&
            items[dragged].kind == dock::Kind::Pinned ? hit : None;
        if (collectTarget != None) { discardTarget = None; Animate(); return; }
        // Pinned icons and Lume can be dropped on the Recycle Bin to leave the Dock.
        discardTarget = hit != None && hit != dragged && AcceptsDrop(hit) &&
            (items[dragged].kind == dock::Kind::Pinned || items[dragged].kind == dock::Kind::Lume ||
             items[dragged].kind == dock::Kind::StartMenu) ? hit : None;
        if (discardTarget != None) { Animate(); return; }
        if (items[dragged].kind == dock::Kind::StartMenu) { Animate(); return; }
        const bool wasPinning = pinningRunning;
        if (items[dragged].kind == dock::Kind::Running) {
            const auto divider = std::find_if(items.begin(), items.end(), [](const auto& item) { return item.kind == dock::Kind::Separator; });
            const float restLeft = barLeft + (barWidth - RestWidth()) / 2;
            pinningRunning = divider != items.end() && x < restLeft + Slot(items, static_cast<std::size_t>(divider - items.begin()), Spacing()).x;
        }
        const auto target = TargetAt(DraggedCentre(), dragged);
        if (wasPinning != pinningRunning || target != dropSlot) {
            dropSlot = target;
            Reorder(DisplayOrder());
        }
        Animate();
    }
    void EndDrag(bool commit) {
        const auto from = dragged;
        if (from == None) return;
        const auto group = collectTarget < items.size() ? items[collectTarget].path : std::string{};
        const auto sourcePath = items[from].path;
        const bool removed = removing && Removable(items[from]);
        const bool discard = discardTarget < items.size() || removed;
        const auto discarded = items[from];
        const auto committedOrder = DisplayOrder();
        collectTarget = None;
        discardTarget = None;
        removing = false;
        HideGhost();
        const auto target = PersistenceTarget(from, dropSlot);
        const auto originalPinned =
            items[from].kind == dock::Kind::Pinned ? items[from].orderIndex : None;
        const auto originalLume = [&] {
            for (const auto& item : items)
                if (item.kind == dock::Kind::Lume) return item.orderIndex;
            return None;
        }();
        const bool changed = items[from].kind == dock::Kind::Running ? pinningRunning : items[from].kind == dock::Kind::Lume
                                 ? target.lumePosition != originalLume
                                 : items[from].kind == dock::Kind::Pinned
                                       ? target.pinnedSlot != originalPinned || target.lumePosition != originalLume
                                       : target.pinnedSlot != None;
        // Leave the icon where it was released; its offset springs it home.
        auto& m = motion[Key(items[from])];
        m.offset.value = (dragX - grabOffset * m.scale.value) - (barLeft + layoutX[from]);
        // ...and from the height it was released at, so it drops back into the row.
        const float restTop = Headroom + BarHeight - IconBottom - IconSize * m.scale.value;
        m.lift.value = std::clamp(restTop - (dragY - grabOffsetY * DragScale), -60.f, 600.f);
        if (removed) m.scale.value = 0;
        if (commit && !discard && !pinningRunning && items[from].kind == dock::Kind::Running) {
            runningOrder.clear();
            for (const auto index : committedOrder)
                if (index < items.size() && items[index].kind == dock::Kind::Running)
                    runningOrder.push_back(Key(items[index]));
        }
        dragged = None;
        Reorder(commit && !discard && !pinningRunning && group.empty() ? committedOrder : RestOrder());
        pinningRunning = false;
        Animate();
        if (commit && discard && ports.discard) {
            if (hasPending) Show(std::move(pending), pendingLook);
            ports.discard(discarded);
            return;
        }
        if (commit && !group.empty() && ports.collect) {
            if (!ports.collect(sourcePath, group) && hasPending) Show(std::move(pending), pendingLook);
        }
        else if (commit && changed && ports.place)
            ports.place(from, target.pinnedSlot, target.lumePosition);
        if (hasPending) Show(std::move(pending), pendingLook);
    }
    void DragOver(const OrganizerDropFeedback& feedback) {
        if (!visible) return;
        if (feedback.phase == OrganizerDropFeedback::Phase::Leave || !feedback.supported) {
            gapSlot = None;
            dropCollection.clear();
            Animate();
            return;
        }
        const auto point = MAKELPARAM(feedback.point.x, feedback.point.y);
        const float x = Dip(point, false);
        const auto hit = HitAt(x, Dip(point, true));
        dropCollection = hit != None && (items[hit].kind == dock::Kind::Collection || AcceptsDrop(hit)) ? items[hit].path : std::string{};
        if (!dropCollection.empty()) { gapSlot = None; hover = None; Animate(); return; }
        const auto slot = SlotAt(x, None);
        gapSlot = slot;
        if (slot != lastGapSlot) {
            // Moving an open gap: icons keep their screen place and slide over.
            std::vector<float> before(items.size(), 0);
            const bool had = layoutX.size() == items.size();
            if (had)
                for (std::size_t i = 0; i < items.size(); ++i) before[i] = barLeft + Visual(i);
            lastGapSlot = slot;
            Layout();
            if (had)
                for (std::size_t i = 0; i < items.size(); ++i)
                    motion[Key(items[i])].offset.value = before[i] - (barLeft + layoutX[i]);
        }
        hovering = false;
        hover = None;
        Animate();
    }
    bool AcceptsDrop(std::size_t index) const {
        return index < items.size() && items[index].kind == dock::Kind::Pinned && ports.acceptsDrop && ports.acceptsDrop(items[index]);
    }
    bool DropPaths(const std::vector<std::string>& paths, POINT point) {
        const auto position = MAKELPARAM(point.x, point.y);
        const auto hit = HitAt(Dip(position, false), Dip(position, true));
        if (hit != None && AcceptsDrop(hit)) {
            const auto target = items[hit];  // The port may refresh items.
            dropCollection.clear(); gapSlot = None; gap = {0, 0}; Animate();
            return ports.dropOnto && ports.dropOnto(paths, target);
        }
        const auto group = hit != None && items[hit].kind == dock::Kind::Collection ? items[hit].path : std::string{};
        dropCollection.clear();
        const auto slot = SlotAt(Dip(position, false), None);
        gapSlot = None;
        // The new icons take the open gap's place, so it closes at once.
        gap = {0, 0};
        Animate();
        if (!group.empty()) return ports.dropCollection && ports.dropCollection(paths, group);
        return ports.drop && ports.drop(paths, slot);
    }
    void Stop() {
        visible = false;
        paintPending=false;
        desktopLayer.Stop();
        dragged = pressed = hover = gapSlot = None;
        collectTarget = discardTarget = None; dropCollection.clear();
        removing = false;
        ReleaseThumbnails();
        ReleaseSlots();
        HidePreview();
        if (preview) { auto old = preview; preview = nullptr; DestroyWindow(old); }
        previewSurface.Release();
        previewCanvas.reset();
        if (ghost) { auto old = ghost; ghost = nullptr; DestroyWindow(old); }
        ghostSurface.Release();
        ghostCanvas.reset();
        clock.Pause();
        dropTarget.Detach();
        material.Detach();
        iconSurface.Release();
        barSurface.Release();
        if (icons) { auto old = icons; icons = nullptr; DestroyWindow(old); }
        if (bar) { auto old = bar; bar = nullptr; DestroyWindow(old); }
        iconCanvas.reset();
        barCanvas.reset();
        iconBitmap = {};
        barBitmap = {};
        motion.clear();
        layoutX.clear();
        order.clear();
    }
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, w, l);
        if (message == WM_NCDESTROY) { SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); return DefWindowProcW(hwnd, message, w, l); }
        if (message == WM_MOUSEACTIVATE) {
            // A click on the Dock moves on from a widget lifted over an application.
            if (hwnd == self->icons) desktop_level::Deselect();
            return MA_NOACTIVATE;
        }
        if (message == WM_WINDOWPOSCHANGING && (hwnd == self->icons || hwnd == self->bar))
            desktop_level::Changing(l);
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) { PAINTSTRUCT paint{}; BeginPaint(hwnd, &paint); EndPaint(hwnd, &paint); return 0; }
        if (message == desktop_level::Message()) {
            if (self->visible) self->RestackVisible();
            return 0;
        }
        if (hwnd == self->preview) {
            switch (message) {
            case WM_MOUSEMOVE: {
                KillTimer(self->icons, PreviewLeaveTimer);
                self->previewLeaving = false;
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
                return 0;
            }
            case WM_MOUSELEAVE:
                self->previewLeaving = true;
                SetTimer(self->icons, PreviewLeaveTimer, 350, nullptr);
                return 0;
            case WM_LBUTTONDOWN:
                self->previewPressed = self->PreviewHit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
                return 0;
            case WM_LBUTTONUP: {
                const auto handle = self->PreviewHit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
                if (handle && handle == self->previewPressed) {
                    self->HidePreview();
                    if (self->ports.focusWindow) self->ports.focusWindow(handle);
                    else dock_sources::FocusWindow(handle);
                }
                return 0;
            }
            case WM_MOUSEWHEEL: {
                if (GET_WHEEL_DELTA_WPARAM(w) < 0) ++self->previewPage;
                else if (self->previewPage) --self->previewPage;
                self->PaintPreview();
                return 0;
            }
            }
            return DefWindowProcW(hwnd, message, w, l);
        }
        if (hwnd != self->icons) return DefWindowProcW(hwnd, message, w, l);
        if (message == DockDesktopLayer::Message || (message == WM_TIMER && w == DockDesktopLayer::FallbackTimer)) {
            self->desktopLayer.Consumed();
            if (self->visible) self->RestackVisible();
            return 0;
        }
        switch (message) {
        case WM_TIMER:
            if (w == PreviewTimer) {
                KillTimer(hwnd, PreviewTimer);
                const auto index = std::exchange(self->pendingPreview, None);
                if (index < self->items.size() && self->hover == index && self->hovering &&
                    self->dragged == None && self->visible) self->ShowPreview(index);
                return 0;
            }
            if (w == PreviewLeaveTimer) {
                KillTimer(hwnd, PreviewLeaveTimer);
                self->previewLeaving = false;
                POINT cursor{};
                if (!GetCursorPos(&cursor) || !self->ContainsPreviewPoint(cursor)) self->HidePreview();
                return 0;
            }
            break;
        case WM_CANCELMODE:
            if(self->dragged!=None)self->EndDrag(false);
            self->HidePreview();self->HideGhost();self->hovering=false;self->hover=None;
            self->clock.Pause();return 0;
        case FrameMessage: {
            self->clock.Consumed();
            if (!self->visible) { self->clock.Pause(); return 0; }
            // The bitmap path does a software draw, readback and layered-window
            // update. On high-refresh displays, skip surplus vblanks.
            if (AnimationSeconds() - self->lastFrame < MinFrameInterval) return 0;
            const bool moving = self->Step();
            self->Paint();
            // An open drop gap only needs frames while it springs; DragHover
            // restarts the clock when the slot changes. Keeping it running
            // repainted the whole Dock at the display rate for as long as a
            // launchpad item hovered, on the thread that also moves that item.
            // Pointer messages restart the clock when a dragged icon moves.
            // Holding it still needs no software bitmap or vblank polling.
            if (self->previewSlideStart) self->PresentPreviewFrame();
            if (!moving && !self->previewSlideStart) self->clock.Pause();
            return 0;
        }
        case PaintMessage:self->paintPending=false;if(self->visible)self->Paint();return 0;
        case WM_MOUSEMOVE: {
            const float x = self->Dip(l, false), y = self->Dip(l, true);
            if (self->pressed != None && Movable(self->items[self->pressed]) && (w & MK_LBUTTON)) {
                const POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                if (self->dragged == None && (std::abs(point.x - self->pressPoint.x) > self->Px(DragThreshold) ||
                                              std::abs(point.y - self->pressPoint.y) > self->Px(DragThreshold))) {
                    self->dragX = x;
                    self->dragY = y;
                    self->BeginDrag();
                }
                if (self->dragged != None) self->Drag(x, y);
            }
            self->Pointer(x, y);
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (self->dragged == None) self->Leave();
            return 0;
        case WM_LBUTTONDOWN: {
            const auto hit = self->HitAt(self->Dip(l, false), self->Dip(l, true));
            self->pressed = hit;
            self->HidePreview();
            self->pressPoint = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (hit != None && Movable(self->items[hit])) SetCapture(hwnd);
            return 0;
        }
        case WM_LBUTTONUP: {
            const bool wasDragging = self->dragged != None;
            const auto pressed = self->pressed;
            self->pressed = None;
            // End the drag before releasing capture, whose notification cancels drags.
            if (wasDragging) {
                const float x = self->Dip(l, false), y = self->Dip(l, true);
                if (x != self->dragX || y != self->dragY) self->Drag(x, y);
                self->EndDrag(true);
                if (GetCapture() == hwnd) ReleaseCapture();
                return 0;
            }
            if (GetCapture() == hwnd) ReleaseCapture();
            const auto hit = self->HitAt(self->Dip(l, false), self->Dip(l, true));
            if (hit != None && hit == pressed && self->ports.activate) self->ports.activate(hit);
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (self->dragged != None && reinterpret_cast<HWND>(l) != hwnd) self->EndDrag(false);
            self->pressed = None;
            return 0;
        case WM_KEYDOWN:
            if (w == VK_ESCAPE && self->dragged != None) { ReleaseCapture(); return 0; }
            break;
        case WM_RBUTTONUP: {
            self->HidePreview();
            POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            const auto hit = self->HitAt(self->Dip(l, false), self->Dip(l, true));
            ClientToScreen(hwnd, &point);
            if (self->ports.context) self->ports.context(hit, point);
            return 0;
        }
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            if (self->visible) {
                if (!self->Position()) {
                    self->visible = false;
                    return message == WM_DPICHANGED ? 0 : DefWindowProcW(hwnd, message, w, l);
                }
                self->Layout();
                if (self->Paint()) self->RestackVisible();
            }
            return message == WM_DPICHANGED ? 0 : DefWindowProcW(hwnd, message, w, l);
        case WM_SETTINGCHANGE:
            if (w == SPI_SETWORKAREA && self->visible) {
                if (!self->Position()) { self->visible = false; break; }
                if (self->Paint()) self->RestackVisible();
            }
            break;
        }
        return DefWindowProcW(hwnd, message, w, l);
    }
};

DockWindow::DockWindow(HINSTANCE instance, DockWindowPorts ports) : impl_(std::make_unique<Impl>(instance, std::move(ports))) {}
DockWindow::~DockWindow() = default;
bool DockWindow::Show(std::vector<dock::Item> items, bool frosted) {
    DockAppearance look;
    look.frosted = frosted;
    return impl_->Show(std::move(items), look);
}
bool DockWindow::Show(std::vector<dock::Item> items, const DockAppearance& look) { return impl_->Show(std::move(items), look); }
void DockWindow::Stop() { impl_->Stop(); }
bool DockWindow::Paint() { return impl_->Paint(); }
bool DockWindow::SummonTemporarily() {
    if (!impl_->visible || !impl_->icons || !impl_->bar || !impl_->desktopLayer.Summon()) return false;
    if (impl_->Paint()) return true;
    impl_->desktopLayer.Restore();
    return false;
}
bool DockWindow::ContainsPreviewPoint(POINT screen) const { return impl_->ContainsPreviewPoint(screen); }
void DockWindow::RestoreDesktopLayer() {
    if (impl_->desktopLayer.Summoned() && impl_->preview) {
        impl_->HidePreview();
        window_layer::SetFullscreenBypass(impl_->preview, false);
    }
    impl_->desktopLayer.Restore();
}
void DockWindow::RequestPaint() {
    if(impl_->icons&&!impl_->paintPending)impl_->paintPending=PostMessageW(impl_->icons,PaintMessage,0,0)!=FALSE;
}
HWND DockWindow::Handle() const { return impl_->icons; }
HWND DockWindow::BarHandle() const { return impl_->bar; }
bool DockWindow::Visible() const { return impl_->visible; }
void DockWindow::RefreshDesktopLayer() { impl_->RestackVisible(); }
bool DockWindow::LayersVisible() const {
    // Hidden under a maximized window still counts: it returns by itself.
    return impl_->visible && impl_->icons && impl_->bar &&
           (impl_->desktopLayer.Covered() || (IsWindowVisible(impl_->icons) && IsWindowVisible(impl_->bar)));
}
RECT DockWindow::ScreenBounds() const {
    RECT r{};
    if (impl_->bar) GetWindowRect(impl_->bar, &r);
    return r;
}
WidgetMaterialState DockWindow::MaterialState() const { return impl_->material.State(); }
bool DockWindow::Animating() const { return impl_->clock.Running(); }
float DockWindow::ScaleOf(std::size_t index) const {
    if (index >= impl_->items.size()) return 0;
    const auto found = impl_->motion.find(Key(impl_->items[index]));
    return found == impl_->motion.end() ? 0 : found->second.scale.value;
}
float DockWindow::GapOpen() const { return impl_->gap.value; }
bool DockWindow::Removing() const { return impl_->removing; }
std::optional<dock::Item> DockWindow::ItemAt(std::size_t index) const {
    return index < impl_->items.size() ? std::optional<dock::Item>{impl_->items[index]} : std::nullopt;
}
bool DockWindow::HasThumbnail(std::size_t index) const {
    if (index >= impl_->items.size() || !impl_->items[index].window) return false;
    const auto found = impl_->thumbnails.find(impl_->items[index].window);
    // A live DWM thumbnail, or the snapshot taken just before minimizing.
    return (found != impl_->thumbnails.end() && found->second.handle && found->second.visible) ||
           impl_->Snapshot(impl_->items[index].window, impl_->items[index].path) != nullptr;
}
std::size_t DockWindow::CachedSnapshotCount() const { return impl_->snapshots.size(); }
bool DockWindow::HasCachedSnapshot(std::uint64_t window) const {
    const auto found = impl_->snapshots.find(window);
    return found != impl_->snapshots.end() && found->second.image != nullptr;
}
RECT DockWindow::IconBounds(std::size_t index) const {
    auto& s = *impl_;
    if (index >= s.items.size() || s.layoutX.size() != s.items.size()) return {};
    return s.ScreenRect(s.IconRect(index));
}
void DockWindow::RecordAppClick(std::size_t index, const std::string& program) {
    auto& s = *impl_;
    if (!s.look.animation || !s.visible || index >= s.items.size()) return;
    const auto key = dock_slots::ProgramKey(OrganizerWide(program));
    const auto& item = s.items[index];
    const auto window = item.kind == dock::Kind::Collection ? 0 :
        item.window ? item.window : item.animationWindows.empty() ? 0 : item.animationWindows.front();
    if (item.kind != dock::Kind::Running && item.kind != dock::Kind::Window) {
        for (std::size_t i = 0; i < s.items.size(); ++i) {
            const auto& candidate = s.items[i];
            if (candidate.kind != dock::Kind::Running && candidate.kind != dock::Kind::Window) continue;
            const auto& path = candidate.runningProgram.empty() ? candidate.path : candidate.runningProgram;
            const bool sameWindow = window && (candidate.window == window ||
                std::find(candidate.animationWindows.begin(), candidate.animationWindows.end(), window) != candidate.animationWindows.end());
            if ((sameWindow || (key && key == dock_slots::ProgramKey(OrganizerWide(path)))) &&
                s.animationTargets.RecordClick(key, window, s.AnimationRect(i))) return;
        }
    }
    s.animationTargets.RecordClick(key, window, s.AnimationRect(index));
}
bool DockWindow::DragHover(POINT screen) {
    auto& s = *impl_;
    if (!s.icons || !s.visible) return false;
    RECT bar{};
    GetWindowRect(s.bar, &bar);
    const int margin = static_cast<int>(s.Px(24));
    InflateRect(&bar, s.Vertical() ? margin : 0, s.Vertical() ? 0 : margin);
    if (!PtInRect(&bar, screen)) { DragLeave(); return false; }
    POINT client = screen;
    ScreenToClient(s.icons, &client);
    s.DragOver({OrganizerDropFeedback::Phase::Over, client, 1, true});
    return true;
}
void DockWindow::DragLeave() {
    auto& s = *impl_;
    if (s.gapSlot == None && s.dropCollection.empty()) return;
    s.DragOver({OrganizerDropFeedback::Phase::Leave, {}, 0, false});
}
std::size_t DockWindow::DragSlot() const { return impl_->gapSlot; }
std::string DockWindow::CollectionTarget() const { return impl_->dropCollection; }
bool DockWindow::DropPathsAt(const std::vector<std::string>& paths, POINT screen) {
    if (!impl_->visible || !impl_->icons) return false;
    ScreenToClient(impl_->icons, &screen);
    return impl_->DropPaths(paths, screen);
}
}  // namespace lume::platform::windows
