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
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

namespace lume::platform::windows {
namespace {
constexpr const wchar_t* IconClass = lume::window_ids::DockIconsClass;
constexpr const wchar_t* BarClass = lume::window_ids::DockBarClass;
constexpr wchar_t GhostClass[] = L"LumeNativeDockDragIcon", PreviewClass[] = L"LumeNativeDockPreview";
constexpr UINT_PTR PreviewTimer = 0x7c3;
constexpr UINT FrameMessage = WM_APP + 0x7c1;
constexpr float DragThreshold = 5, DragLift = 12, DragScale = 1.12f;
// A dragged icon this far (DIPs) beyond the glass is removed when released.
constexpr float RemoveDistance = 36;
// A minimized window's thumbnail: at most this share of the icon height, and
// its program's icon as a badge on the lower-right corner.
constexpr float ThumbnailHeight = .82f, BadgeSize = .44f;
float ItemWidth(const dock::Item& item) { return item.kind == dock::Kind::Separator ? 1.f : DockWindow::IconSize; }
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
    DockAppearance look;
    unsigned monitorDpi = 96;  // dpi is this times the icon scale
    // Reflections cached per icon bitmap (keyed by identity, dropped with it).
    std::map<const WidgetBitmap*, std::pair<std::weak_ptr<const WidgetBitmap>, std::shared_ptr<const WidgetBitmap>>> mirrors;
    HWND preview = nullptr;
    HTHUMBNAIL previewThumb = nullptr;
    std::uint64_t previewWindow = 0, pendingPreview = 0;
    std::unique_ptr<WidgetCanvas> previewCanvas;
    WidgetBitmap previewBitmap;
    LayeredSurface previewSurface;
    // Published positions for the minimize animation (dock_slots.h).
    HANDLE slotsMapping = nullptr;
    dock_slots::Table* slots = nullptr;
    void PublishSlots() {
        if (!slots) {
            slotsMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(dock_slots::Table),
                                              ports.slotsName.empty() ? dock_slots::MappingName : ports.slotsName.c_str());
            if (!slotsMapping) return;
            slots = static_cast<dock_slots::Table*>(MapViewOfFile(slotsMapping, FILE_MAP_WRITE, 0, 0, sizeof(dock_slots::Table)));
            if (!slots) { CloseHandle(slotsMapping); slotsMapping = nullptr; return; }
        }
        const auto screen = [&](WidgetRect r) {
            return RECT{overlayOrigin.x + static_cast<LONG>(std::lround(Px(r.x))), overlayOrigin.y + static_cast<LONG>(std::lround(Px(r.y))),
                        overlayOrigin.x + static_cast<LONG>(std::lround(Px(r.x + r.width))),
                        overlayOrigin.y + static_cast<LONG>(std::lround(Px(r.y + r.height)))};
        };
        InterlockedIncrement(&slots->sequence);
        LONG count = 0;
        const bool shown = visible && !desktopLayer.Covered() && layoutX.size() == items.size();
        for (std::size_t i = 0; shown && i < items.size() && count < dock_slots::Capacity; ++i) {
            const auto& item = items[i];
            dock_slots::Slot slot{};
            RECT r{};
            if (item.kind == dock::Kind::Window && item.window) {
                const auto found = thumbnails.find(item.window);
                r = screen(WindowBox(i, found == thumbnails.end() ? SIZE{1, 1} : found->second.source));
                slot.kind = dock_slots::WindowSlot;
                slot.key = item.window;
            } else if (item.kind == dock::Kind::Pinned || item.kind == dock::Kind::Running) {
                const float size = IconSize * motion[Key(item)].scale.value, bottom = Headroom + BarHeight - IconBottom;
                r = screen({barLeft + Visual(i), bottom - size, size, size});
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
    bool removing = false;
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
        desktopLayer.Start(icons, bar);
        iconCanvas = std::make_unique<WidgetCanvas>();
        barCanvas = std::make_unique<WidgetCanvas>();
        // Files dragged from Explorer or the desktop; the drop target only reads paths.
        dropTarget.Attach(icons, [this](const OrganizerDropFeedback& feedback) { DragOver(feedback); },
                          [this](const std::vector<std::string>& paths, POINT point) { return DropPaths(paths, point); },
                          true);  // 回收站, 此电脑 and other shell locations can be pinned too.
        return true;
    }
    // The visible primary sequence is the launchpad followed by Lume and all
    // pinned icons. A dragged running icon joins that same sequence while it
    // is being previewed. Keeping one sequence here makes every movable icon
    // use the same spring and "make room" behavior.
    std::vector<std::size_t> Primary(std::size_t skip = None) const {
        std::vector<std::size_t> result;
        if (order.empty()) {
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i == skip) continue;
                if (items[i].kind == dock::Kind::Lume || items[i].kind == dock::Kind::Pinned ||
                    (i == dragged && items[i].kind == dock::Kind::Running))
                    result.push_back(i);
            }
            return result;
        }
        for (const auto i : order) {
            if (i == skip) continue;
            if (items[i].kind == dock::Kind::Lume || items[i].kind == dock::Kind::Pinned ||
                (i == dragged && items[i].kind == dock::Kind::Running))
                result.push_back(i);
        }
        return result;
    }
    std::vector<std::size_t> DisplayOrder() const {
        std::vector<std::size_t> result;
        result.reserve(items.size());
        if (dragged == None) {
            for (std::size_t i = 0; i < items.size(); ++i) result.push_back(i);
            return result;
        }

        std::vector<std::size_t> prefix, primary, tail;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i == dragged) continue;
            if (items[i].kind == dock::Kind::Launchpad || items[i].kind == dock::Kind::Collection) prefix.push_back(i);
            else if (items[i].kind == dock::Kind::Lume || items[i].kind == dock::Kind::Pinned)
                primary.push_back(i);
            else
                tail.push_back(i);
        }
        const auto target = std::min(dropSlot, primary.size());
        primary.insert(primary.begin() + static_cast<std::ptrdiff_t>(target), dragged);
        result.insert(result.end(), prefix.begin(), prefix.end());
        result.insert(result.end(), primary.begin(), primary.end());
        result.insert(result.end(), tail.begin(), tail.end());
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
            const bool pastPinned = kind == dock::Kind::Separator || kind == dock::Kind::Running || kind == dock::Kind::Window;
            const bool gapAtLume = kind == dock::Kind::Lume && items[i].orderIndex == lastGapSlot;
            if (!gapPlaced && ((kind == dock::Kind::Pinned && pinned == lastGapSlot) || gapAtLume || pastPinned)) {
                x += GapWidth();
                gapPlaced = true;
            }
            if (kind == dock::Kind::Pinned) ++pinned;
            const float scale = motion[Key(items[i])].scale.value;
            const float width = kind == dock::Kind::Separator ? 1.f : IconSize * scale;
            layoutX[i] = x;
            // An icon dragged out shrinks its slot away, spacing included.
            x += width + (i == dragged && removing ? Spacing() * std::min(1.f, scale) : Spacing());
        }
        if (!gapPlaced) x += GapWidth();
        barWidth = std::max(Padding * 2, x - Spacing() + Padding);
        Anchor();
    }
    float Centered() const { return (overlayWidth - barWidth) / 2; }
    // The rest-layout position (bar-relative) shown at animated position q.
    float RestOf(float q) const {
        float restX = Padding;
        for (std::size_t n = 0; n < order.size(); ++n) {
            const auto i = order[n];
            const float restWidth = ItemWidth(items[i]);
            const auto found = motion.find(Key(items[i]));
            const float scale = found == motion.end() ? 1.f : found->second.scale.value;
            const float width = items[i].kind == dock::Kind::Separator ? 1.f : IconSize * scale;
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
        const auto monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) return false;
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
        const int available = info.rcWork.right - info.rcWork.left;
        if (needed * static_cast<float>(dpi) / 96 > static_cast<float>(available))
            dpi = std::max(48u, static_cast<unsigned>(std::floor(static_cast<float>(available) * 96 / needed)));
        const float workWidth = static_cast<float>(info.rcWork.right - info.rcWork.left) * 96 / dpi;
        overlayWidth = std::min(RestWidth() + ExtraWidth + IconSize + Spacing(), workWidth);
        const int width = static_cast<int>(std::lround(Px(overlayWidth)));
        const int height = static_cast<int>(std::lround(Px(Headroom + BarHeight + BottomMargin)));
        const int workPixels = info.rcWork.right - info.rcWork.left;
        const int centred = info.rcWork.left + (workPixels - width) / 2;
        const int bottomOffset = MulDiv(look.bottomOffset, static_cast<int>(monitorDpi), 96);
        // Always centred: the horizontal position setting was removed (old values are ignored).
        overlayOrigin = {centred,
                         std::max(info.rcWork.top, info.rcWork.bottom - height - bottomOffset)};
        // The bar's content is constant (a near-transparent fill under the glass):
        // render it once at the widest size and show a slice of it each frame.
        if (!barCanvas->Begin(overlayWidth, BarHeight, dpi)) return false;
        barCanvas->Rounded({0, 0, overlayWidth, BarHeight}, 0, 0xffffff, 1.f / 255);
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
            // Hidden under a maximized window: release the glass too.
            hovering = false;
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
        // Keep where every surviving icon is on screen, then adopt the new items.
        std::map<std::string, float> screen;
        const float oldOrigin = static_cast<float>(overlayOrigin.x) * 96 / dpi;
        if (layoutX.size() == items.size())
            for (std::size_t i = 0; i < items.size(); ++i) screen[Key(items[i])] = barLeft + oldOrigin + Visual(i);
        const float oldLeft = barLeft + oldOrigin;
        items = std::move(next);
        look = nextLook;
        look.iconScale = std::clamp(look.iconScale, 50, 200);
        if (!dock::ValidPosition(look.position)) look.position = dock::DefaultPosition;
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
        const float newOrigin = static_cast<float>(overlayOrigin.x) * 96 / dpi;
        anchor = {visible ? oldLeft - newOrigin : 0.f, 0};
        order.clear();
        for (std::size_t i = 0; i < items.size(); ++i) order.push_back(i);
        Layout();
        if (!visible) { anchor = {Centered(), 0}; barLeft = Centered(); }
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
        moving |= anchor.Step(Centered(), dt, SlideOmega, SlideZeta, .05f, reducedMotion);
        Anchor();
        return moving;
    }
    bool Paint() {
        if (!icons || !visible || !iconCanvas) return false;
        const float height = Headroom + BarHeight + BottomMargin;
        if (!iconCanvas->Begin(overlayWidth, height, dpi)) { visible = false; return false; }
        // A near-transparent body over the bar keeps it hit-testable; the
        // headroom stays fully transparent and passes clicks through.
        iconCanvas->Rounded({barLeft, Headroom, barWidth, BarHeight}, CornerRadius, 0xffffff, 1.f / 255);
        std::size_t labelled = None;
        for (const auto i : order) {
            if (i == dragged) continue;
            DrawItem(i);
            if (i == hover && dragged == None && gapSlot == None && items[i].kind != dock::Kind::Separator) labelled = i;
        }
        if (labelled != None) DrawLabel(labelled);
        if (dragged != None) PaintGhost();
        if (!iconCanvas->Finish(iconBitmap) || !iconSurface.Present(icons, iconBitmap, &overlayOrigin)) {
            visible = false;
            return false;
        }
        const POINT barOrigin{overlayOrigin.x + static_cast<LONG>(std::lround(Px(barLeft))),
                              overlayOrigin.y + static_cast<LONG>(std::lround(Px(Headroom)))};
        const SIZE barSize{static_cast<LONG>(std::lround(Px(barWidth))), static_cast<LONG>(std::lround(Px(BarHeight)))};
        if (!barSurface.Compose(bar, barOrigin, barSize)) { visible = false; return false; }
        UpdateThumbnails(barOrigin);
        PublishSlots();
        if (look.transparent || desktopLayer.Covered()) return true;  // "无": no glass under the icons
        LumeMaterialParameters p;
        std::memset(&p, 0, sizeof(p));
        p.size = sizeof(p);
        p.owner = bar;
        p.frosted = frosted;
        // Frosted keeps the Dock frost (profile 3). Liquid uses the widgets'
        // profile 1: the soft-edge rim lens is about as tall as the 100 DIP
        // bar, so profile 3 smeared the whole Dock instead of edging it.
        p.opticalProfile = frosted ? 3 : 1;
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
            MaterialContact(bar, barWidth, BarHeight, dpi, p.contactX, p.contactY);
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
            iconCanvas->Line(x + .5f, Headroom + 12, x + .5f, Headroom + BarHeight - 12, 0xffffff, .3f, 1);
            return;
        }
        const float size = IconSize * m.scale.value;
        float left = barLeft + Visual(i);
        if (i == dragged) left = dragX - grabOffset * m.scale.value;
        const WidgetRect r{left, bottom - size - m.lift.value, size, size};
        if (item.kind == dock::Kind::Window) {
            // The window itself is a DWM thumbnail on the bar below; music
            // shows its cover here instead. The program's badge goes on top.
            const auto cover = ports.cover ? ports.cover(item) : nullptr;
            const auto found = thumbnails.find(item.window);
            const auto snapshot = cover ? nullptr : Snapshot(item.window);
            const auto box = WindowBox(i, snapshot ? SIZE{static_cast<LONG>(snapshot->width), static_cast<LONG>(snapshot->height)}
                                                   : cover || found == thumbnails.end() ? SIZE{1, 1} : found->second.source);
            if (cover) iconCanvas->ImageBitmap(cover, box);
            else if (snapshot) iconCanvas->ImageBitmap(snapshot, box);
            const float badge = size * BadgeSize;
            if (const auto icon = Icon(item))
                iconCanvas->ImageBitmap(icon, {box.x + box.width - badge * .78f, bottom - badge * .86f, badge, badge});
            return;
        }
        if (item.kind == dock::Kind::Launchpad) LaunchpadGlyph(*iconCanvas, r);
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
            if (look.reflection && i != dragged) {
                const float floor = Headroom + BarHeight + BottomMargin;
                if (iconCanvas->PushClip({left - size, bottom, size * 3, floor - bottom})) {
                    // Keep the fade inside the available strip even while the
                    // icon magnifies; the reflection must never end at a hard clip.
                    const float reflectionTop=bottom+1;
                    const float mirrorHeight=std::min(size,2*(floor-reflectionTop));
                    iconCanvas->ImageBitmap(Mirror(bitmap), {left,reflectionTop,size,mirrorHeight});
                    iconCanvas->PopClip();
                }
            }
        }
        else iconCanvas->Rounded(r, size * .22f, 0xffffff, .14f);
        if (item.running && i != dragged)
            iconCanvas->Rounded({left + size / 2 - 2, bottom + 6, 4, 4}, 2, 0xffffff, .85f);
    }
    std::shared_ptr<const WidgetBitmap> Icon(const dock::Item& item) {
        return ports.icon ? ports.icon(item) : nullptr;
    }
    // A larger live preview above a minimized window while the pointer rests on it.
    void SyncPreview() {
        const bool want = hover != None && dragged == None && hover < items.size() && items[hover].kind == dock::Kind::Window;
        const std::uint64_t target = want ? items[hover].window : 0;
        if (target && (target == previewWindow || target == pendingPreview)) return;
        HidePreview();
        if (!target || !icons) return;
        pendingPreview = target;
        SetTimer(icons, PreviewTimer, 280, nullptr);
    }
    void HidePreview() {
        if (icons) KillTimer(icons, PreviewTimer);
        pendingPreview = previewWindow = 0;
        if (previewThumb) { DwmUnregisterThumbnail(previewThumb); previewThumb = nullptr; }
        if (preview) ShowWindow(preview, SW_HIDE);
    }
    void ShowPreview(std::uint64_t handle) {
        std::size_t index = None;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].kind == dock::Kind::Window && items[i].window == handle) index = i;
        const auto source = reinterpret_cast<HWND>(handle);
        if (index == None || !IsWindow(source) || layoutX.size() != items.size()) return;
        if (!preview) {
            if (!Register(PreviewClass)) return;
            preview = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                                      PreviewClass, L"Lume Dock", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
            if (!preview) return;
            previewCanvas = std::make_unique<WidgetCanvas>();
        }
        // A minimized window shows the snapshot from just before minimizing when
        // there is one (DWM's image is grey for some apps); otherwise its live thumbnail.
        const auto snapshot = Snapshot(handle);
        SIZE sourceSize{};
        if (snapshot) sourceSize = {static_cast<LONG>(snapshot->width), static_cast<LONG>(snapshot->height)};
        else {
            if (FAILED(DwmRegisterThumbnail(preview, source, &previewThumb))) { previewThumb = nullptr; return; }
            DwmQueryThumbnailSourceSize(previewThumb, &sourceSize);
        }
        if (sourceSize.cx <= 0 || sourceSize.cy <= 0) { HidePreview(); return; }
        if (sourceSize.cx <= 0 || sourceSize.cy <= 0) { HidePreview(); return; }
        // Measured in the monitor's DIPs, independent of the Dock's icon zoom.
        const float unit = static_cast<float>(monitorDpi) / 96;
        const float fit = std::min(320.f / sourceSize.cx, 220.f / sourceSize.cy) * unit;
        const float tw = sourceSize.cx * fit / unit, th = sourceSize.cy * fit / unit;
        // Just the window, with its title in a small label above: no frame around it.
        const float title = 24, spacing = 8, width = std::max(tw, 180.f), height = title + spacing + th;
        if (!previewCanvas->Begin(width, height, monitorDpi)) { HidePreview(); return; }
        const auto text = FitText(*previewCanvas, OrganizerWide(items[index].name), std::min(width, 300.f) - 20, 12);
        const float labelWidth = std::min(width, previewCanvas->MeasureText(text, 12) + 20);
        const WidgetRect label{(width - labelWidth) / 2, 0, labelWidth, title};
        previewCanvas->Rounded(label, 8, 0x1c1c1e, .78f);
        previewCanvas->Text(text, label, 12, 0xffffff, .95f, false, WidgetTextAlign::Center);
        if (snapshot) previewCanvas->ImageBitmap(snapshot, {(width - tw) / 2, title + spacing, tw, th});
        if (!previewCanvas->Finish(previewBitmap)) { HidePreview(); return; }
        const float size = IconSize * motion[Key(items[index])].scale.value;
        const LONG centre = overlayOrigin.x + static_cast<LONG>(std::lround(Px(barLeft + Visual(index) + size / 2)));
        const LONG w = static_cast<LONG>(std::lround(width * unit)), h = static_cast<LONG>(std::lround(height * unit));
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(MonitorFromWindow(icons, MONITOR_DEFAULTTOPRIMARY), &info);
        const POINT origin{std::clamp(centre - w / 2, info.rcWork.left + 8, std::max(info.rcWork.left + 8, info.rcWork.right - w - 8)),
                           std::max(info.rcWork.top + 8, overlayOrigin.y + static_cast<LONG>(std::lround(Px(Headroom))) - h -
                                                             static_cast<LONG>(std::lround(10 * unit)))};
        if (!previewSurface.Present(preview, previewBitmap, &origin)) { HidePreview(); return; }
        DWM_THUMBNAIL_PROPERTIES p{};
        p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        const LONG left = static_cast<LONG>(std::lround((width - tw) / 2 * unit)), top = static_cast<LONG>(std::lround((title + spacing) * unit));
        p.rcDestination = {left, top, left + static_cast<LONG>(std::lround(tw * unit)), top + static_cast<LONG>(std::lround(th * unit))};
        p.fVisible = TRUE;
        p.opacity = 255;
        p.fSourceClientAreaOnly = FALSE;
        if (previewThumb) DwmUpdateThumbnailProperties(previewThumb, &p);
        ShowWindow(preview, SW_SHOWNOACTIVATE);
        previewWindow = handle;
    }
    // Where a minimized window is drawn (icon-layer DIPs): its shape fitted
    // into the icon's square, standing on the icon baseline.
    WidgetRect WindowBox(std::size_t i, SIZE source) {
        const float size = IconSize * motion[Key(items[i])].scale.value;
        const float left = barLeft + Visual(i), bottom = Headroom + BarHeight - IconBottom - motion[Key(items[i])].lift.value;
        const float aspect = source.cx > 0 && source.cy > 0 ? static_cast<float>(source.cx) / source.cy : 1.f;
        float width = size, height = size / aspect;
        if (height > size * ThumbnailHeight) { height = size * ThumbnailHeight; width = height * aspect; }
        return {left + (size - width) / 2, bottom - height, width, height};
    }
    void UpdateThumbnails(POINT barOrigin) {
        std::map<std::uint64_t, bool> live;
        const bool show = !desktopLayer.Covered();
        for (const auto i : order) {
            const auto& item = items[i];
            if (item.kind != dock::Kind::Window || !item.window) continue;
            const auto source = reinterpret_cast<HWND>(item.window);
            if (!IsWindow(source)) continue;
            live[item.window] = true;
            auto& t = thumbnails[item.window];
            if (!t.handle) {
                if (FAILED(DwmRegisterThumbnail(bar, source, &t.handle))) { t.handle = nullptr; continue; }
                DwmQueryThumbnailSourceSize(t.handle, &t.source);
            }
            const bool cover = ports.cover && ports.cover(item);
            // A snapshot from just before minimizing replaces DWM's image (grey for some apps).
            const auto snapshot = Snapshot(item.window);
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
    struct CachedSnapshot { LONG serial = -1; std::shared_ptr<const WidgetBitmap> image; ULONGLONG checked = 0; };
    std::map<std::uint64_t, CachedSnapshot> snapshots;
    std::shared_ptr<const WidgetBitmap> Snapshot(std::uint64_t window) {
        if (!window) return nullptr;
        auto& cached = snapshots[window];
        const auto now = GetTickCount64();
        if (cached.checked && now - cached.checked < 250) return cached.image;
        cached.checked = now;
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, dock_slots::SnapshotName(window).c_str());
        if (!mapping) { cached = {-1, nullptr, now}; return nullptr; }
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
            const std::wstring label = L"\x79fb\x9664";  // 移除
            const float labelWidth = ghostCanvas->MeasureText(label, 12) + 22;
            const WidgetRect pill{(width - labelWidth) / 2, 0, labelWidth, labelHeight};
            ghostCanvas->Rounded(pill, 8, 0x1c1c1e, .78f);
            ghostCanvas->Text(label, pill, 12, 0xffffff, .95f, false, WidgetTextAlign::Center);
        }
        if (!ghostCanvas->Finish(ghostBitmap)) return;
        const float left = dragX - grabOffset * DragScale - (width - size) / 2;
        const float top = dragY - grabOffsetY * DragScale - (labelHeight + labelGap);
        const POINT origin{overlayOrigin.x + static_cast<LONG>(std::lround(Px(left))),
                           overlayOrigin.y + static_cast<LONG>(std::lround(Px(top)))};
        if (ghostSurface.Present(ghost, ghostBitmap, &origin) && !IsWindowVisible(ghost))
            ShowWindow(ghost, SW_SHOWNOACTIVATE);
    }
    void HideGhost() { if (ghost) ShowWindow(ghost, SW_HIDE); }
    // The icon flipped upside down, fading out over its upper half, as on a
    // glossy shelf. Built once per icon bitmap.
    std::shared_ptr<const WidgetBitmap> Mirror(const std::shared_ptr<const WidgetBitmap>& source) {
        for (auto it = mirrors.begin(); it != mirrors.end();)
            it = it->second.first.expired() ? mirrors.erase(it) : std::next(it);
        const auto found = mirrors.find(source.get());
        if (found != mirrors.end()) return found->second.second;
        auto result=std::make_shared<const WidgetBitmap>(MakeDockReflection(*source));
        mirrors[source.get()] = {source, result};
        return result;
    }
    void DrawLabel(std::size_t i) {
        const auto& m = motion[Key(items[i])];
        const float size = IconSize * m.scale.value;
        const float center = barLeft + Visual(i) + size / 2;
        const float top = Headroom + BarHeight - IconBottom - size;
        const auto text = FitText(*iconCanvas, OrganizerWide(items[i].name), 220, 12);
        const float width = iconCanvas->MeasureText(text, 12) + 20, height = 24;
        const float x = std::clamp(center - width / 2, 0.f, std::max(0.f, overlayWidth - width));
        const float y = std::max(0.f, top - height - 8);
        iconCanvas->Rounded({x, y, width, height}, 8, 0x1c1c1e, .72f);
        iconCanvas->Text(text, {x, y, width, height}, 12, 0xffffff, .95f, false, WidgetTextAlign::Center);
    }
    // Animated hit test in icon-layer coordinates.
    std::size_t HitAt(float x, float y) {
        if (y < Headroom - IconSize * (MaxScale - 1) || y > Headroom + BarHeight || x < barLeft || x > barLeft + barWidth)
            return None;
        for (const auto i : order) {
            if (items[i].kind == dock::Kind::Separator) continue;
            const float size = IconSize * motion[Key(items[i])].scale.value;
            const float left = barLeft + Visual(i);
            if (x >= left - Spacing() / 2 && x < left + size + Spacing() / 2) return i;
        }
        return None;
    }
    float Dip(LPARAM l, bool vertical) const {
        return static_cast<float>(vertical ? GET_Y_LPARAM(l) : GET_X_LPARAM(l)) * 96 / dpi;
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
        const float scale = motion[Key(items[dragged])].scale.value;
        grabOffset = std::clamp(dragX - (barLeft + Visual(dragged)), 0.f, IconSize * scale) / scale;
        const float top = Headroom + BarHeight - IconBottom - IconSize * scale - motion[Key(items[dragged])].lift.value;
        grabOffsetY = std::clamp(dragY - top, 0.f, IconSize * scale) / scale;
        removing = false;
        dropSlot = TargetAt(DraggedCentre(), dragged);
        Reorder(DisplayOrder());
        Animate();
    }
    void Drag(float x, float y) {
        dragX = x;
        dragY = y;
        // Pinned icons and Lume dragged well clear of the glass leave the Dock
        // on release; the others close up behind them meanwhile.
        const bool outside = y < Headroom - RemoveDistance || x < barLeft - RemoveDistance ||
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
            (items[dragged].kind == dock::Kind::Pinned || items[dragged].kind == dock::Kind::Running) ? hit : None;
        if (collectTarget != None) { discardTarget = None; Animate(); return; }
        // Pinned icons and Lume can be dropped on the Recycle Bin to leave the Dock.
        discardTarget = hit != None && hit != dragged && AcceptsDrop(hit) &&
            (items[dragged].kind == dock::Kind::Pinned || items[dragged].kind == dock::Kind::Lume) ? hit : None;
        if (discardTarget != None) { Animate(); return; }
        const auto target = TargetAt(DraggedCentre(), dragged);
        if (target != dropSlot) {
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
        const bool changed = items[from].kind == dock::Kind::Lume
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
        dragged = None;
        Reorder(DisplayOrder());
        Animate();
        if (commit && discard && ports.discard) { ports.discard(discarded); return; }
        if (commit && !group.empty() && ports.collect) {
            if (!ports.collect(sourcePath, group) && hasPending) Show(std::move(pending), pendingLook);
        }
        else if (commit && changed && ports.place)
            ports.place(from, target.pinnedSlot, target.lumePosition);
        else if (hasPending) Show(std::move(pending), pendingLook);
    }
    void DragOver(const OrganizerDropFeedback& feedback) {
        if (!visible) return;
        if (feedback.phase == OrganizerDropFeedback::Phase::Leave || !feedback.supported) {
            gapSlot = None;
            dropCollection.clear();
            Animate();
            return;
        }
        const float x = static_cast<float>(feedback.point.x) * 96 / dpi;
        const auto hit = HitAt(x, static_cast<float>(feedback.point.y) * 96 / dpi);
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
        const auto hit = HitAt(static_cast<float>(point.x) * 96 / dpi, static_cast<float>(point.y) * 96 / dpi);
        if (hit != None && AcceptsDrop(hit)) {
            const auto target = items[hit];  // The port may refresh items.
            dropCollection.clear(); gapSlot = None; gap = {0, 0}; Animate();
            return ports.dropOnto && ports.dropOnto(paths, target);
        }
        const auto group = hit != None && items[hit].kind == dock::Kind::Collection ? items[hit].path : std::string{};
        dropCollection.clear();
        const auto slot = SlotAt(static_cast<float>(point.x) * 96 / dpi, None);
        gapSlot = None;
        // The new icons take the open gap's place, so it closes at once.
        gap = {0, 0};
        Animate();
        if (!group.empty()) return ports.dropCollection && ports.dropCollection(paths, group);
        return ports.drop && ports.drop(paths, slot);
    }
    void Stop() {
        visible = false;
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
                const auto target = std::exchange(self->pendingPreview, 0);
                // Only if the pointer is still resting on that window.
                const bool still = self->hover < self->items.size() && self->items[self->hover].window == target && self->hovering;
                if (target && still && self->visible) self->ShowPreview(target);
                return 0;
            }
            break;
        case FrameMessage: {
            self->clock.Consumed();
            if (!self->visible) { self->clock.Pause(); return 0; }
            const bool moving = self->Step();
            self->Paint();
            // An open drop gap only needs frames while it springs; DragHover
            // restarts the clock when the slot changes. Keeping it running
            // repainted the whole Dock at the display rate for as long as a
            // launchpad item hovered, on the thread that also moves that item.
            if (!moving && !self->hovering && self->dragged == None) self->clock.Pause();
            return 0;
        }
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
            if (wasDragging) { self->EndDrag(true); if (GetCapture() == hwnd) ReleaseCapture(); return 0; }
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
HWND DockWindow::Handle() const { return impl_->icons; }
HWND DockWindow::BarHandle() const { return impl_->bar; }
bool DockWindow::Visible() const { return impl_->visible; }
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
bool DockWindow::HasThumbnail(std::size_t index) const {
    if (index >= impl_->items.size() || !impl_->items[index].window) return false;
    const auto found = impl_->thumbnails.find(impl_->items[index].window);
    // A live DWM thumbnail, or the snapshot taken just before minimizing.
    return (found != impl_->thumbnails.end() && found->second.handle && found->second.visible) ||
           impl_->Snapshot(impl_->items[index].window) != nullptr;
}
RECT DockWindow::IconBounds(std::size_t index) const {
    auto& s = *impl_;
    if (index >= s.items.size() || s.layoutX.size() != s.items.size()) return {};
    const float size = IconSize * s.motion[Key(s.items[index])].scale.value;
    const float left = s.barLeft + s.Visual(index), bottom = Headroom + BarHeight - IconBottom;
    return {s.overlayOrigin.x + static_cast<LONG>(s.Px(left)), s.overlayOrigin.y + static_cast<LONG>(s.Px(bottom - size)),
            s.overlayOrigin.x + static_cast<LONG>(s.Px(left + size)), s.overlayOrigin.y + static_cast<LONG>(s.Px(bottom))};
}
bool DockWindow::DragHover(POINT screen) {
    auto& s = *impl_;
    if (!s.icons || !s.visible) return false;
    RECT bar{};
    GetWindowRect(s.bar, &bar);
    InflateRect(&bar, 0, static_cast<int>(s.Px(24)));
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
