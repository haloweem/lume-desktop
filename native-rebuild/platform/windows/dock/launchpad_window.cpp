#include "platform/windows/dock/launchpad_window.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/dock/dock_sources.h"
#include "platform/windows/dock/dock_spring.h"
#include "platform/windows/dock/frame_clock.h"
#include "platform/windows/widget_ui/layered_surface.h"
#include "platform/windows/widget_ui/text_fit.h"
#include "platform/windows/organizer/organizer_text.h"
#include "platform/windows/organizer/organizer_drop_target.h"
#include "platform/windows/organizer/shell_drag.h"
#include "platform/windows/widget_ui/direct_write_edit.h"
#include "platform/windows/widget_ui/direct_write_text.h"
#include "platform/windows/widget_ui/ui_font.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
#include <map>
#include <utility>
#include "platform/windows/ui_stall_trace.h"
#include <cstdio>

namespace lume::platform::windows {
namespace {
constexpr const wchar_t* ClassName = lume::window_ids::LaunchpadClass;
// The dragged item's own layered window, as the Dock's icon reorder uses.
constexpr const wchar_t* DragGhostClass = L"LumeLaunchpadDragIcon";
constexpr UINT DesktopChanged = WM_APP + 0x7a1, PresentSearchMessage = WM_APP + 0x7a2, FrameMessage = WM_APP + 0x7a5,
              RewarmMessage = WM_APP + 0x7a6;
constexpr UINT_PTR CaretTimer = 0x7a3, RefreshTimer = 0x7a4, SearchTimer = 0x7a7;
// The live search field, whose first paint costs about a frame, takes over
// only once the opened panel has been still for a moment.
constexpr double SearchSettleSeconds = .12;
// The panel unfolds from the island's droplet (50% wide, 72% tall). Width and
// height share one spring, so its shape changes evenly and never wobbles, and
// it is damped just short of critical: no visible overshoot to pull back from.
// The fade keeps the island's fade spring (300/34).
constexpr float SpreadXOmega = 17, SpreadXZeta = .97f, SpreadYOmega = SpreadXOmega, SpreadYZeta = SpreadXZeta,
                FadeOmega = 17.32f, FadeZeta = .98f, DropletX = .5f, DropletY = .72f, CloseSpread = .92f,
                MaxSpread = 1.06f;  // headroom for the fixed-size animation canvas
constexpr float IconSize = 52, LabelSize = 12, LabelHeight = 18, DragThreshold = 6, EdgeBand = 32;
constexpr ULONGLONG EdgeDwellMs = 550;
// Open: the panel unfolds from its centre while rising and fading in; close
// reverses it. Text, icons and panels share the same retained-frame scale.
constexpr float OpenOmega = 18, OpenZeta = .8f, CloseOmega = 30, CloseZeta = .9f, OpenDrop = 12;
struct Dib {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    std::uint32_t* pixels = nullptr;
    Dib(int width, int height) {
        dc = CreateCompatibleDC(nullptr);
        if (!dc) return;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bitmap) { previous = SelectObject(dc, bitmap); pixels = static_cast<std::uint32_t*>(bits); }
    }
    ~Dib() { if (previous) SelectObject(dc, previous); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); }
    Dib(const Dib&) = delete;
    Dib& operator=(const Dib&) = delete;
};
bool Inside(const WidgetRect& r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height; }
// The launchpad frame is already a premultiplied BGRA bitmap. A small
// separable box blur gives the collection overlay a genuine blurred backdrop
// without another window or a D2D effect graph.
WidgetBitmap Blur(const WidgetBitmap& source, int radius) {
    if (radius <= 0 || !source.width || !source.height ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height) return {};
    // Blur a reduced snapshot rather than several million full-DPI pixels on
    // the UI thread. Premultiplied averaging keeps transparent text edges clean.
    if (std::max(source.width, source.height) > 512) {
        WidgetBitmap reduced{(source.width + 1) / 2, (source.height + 1) / 2, {}};
        reduced.pixels.resize(static_cast<std::size_t>(reduced.width) * reduced.height);
        for (unsigned y = 0; y < reduced.height; ++y)
            for (unsigned x = 0; x < reduced.width; ++x) {
                const auto a = source.pixels[static_cast<std::size_t>(y * 2) * source.width + x * 2];
                const auto b = source.pixels[static_cast<std::size_t>(y * 2) * source.width + std::min(x * 2 + 1, source.width - 1)];
                const auto c = source.pixels[static_cast<std::size_t>(std::min(y * 2 + 1, source.height - 1)) * source.width + x * 2];
                const auto d = source.pixels[static_cast<std::size_t>(std::min(y * 2 + 1, source.height - 1)) * source.width + std::min(x * 2 + 1, source.width - 1)];
                std::uint32_t pixel = 0;
                for (int shift : {0, 8, 16, 24})
                    pixel |= (((a >> shift & 255) + (b >> shift & 255) + (c >> shift & 255) + (d >> shift & 255)) / 4) << shift;
                reduced.pixels[static_cast<std::size_t>(y) * reduced.width + x] = pixel;
            }
        return Blur(reduced, std::max(1, (radius + 1) / 2));
    }
    const int width = static_cast<int>(source.width), height = static_cast<int>(source.height);
    const int window = radius * 2 + 1;
    std::vector<std::uint32_t> horizontal(source.pixels.size()), output(source.pixels.size());
    const auto channel = [](std::uint32_t p, int shift) { return static_cast<int>((p >> shift) & 255u); };
    for (int y = 0; y < height; ++y) {
        int b = 0, g = 0, r = 0, a = 0;
        const auto add = [&](int x, int sign) {
            const auto p = source.pixels[static_cast<std::size_t>(y) * width + std::clamp(x, 0, width - 1)];
            b += sign * channel(p, 0); g += sign * channel(p, 8); r += sign * channel(p, 16); a += sign * channel(p, 24);
        };
        for (int d = -radius; d <= radius; ++d) add(d, 1);
        for (int x = 0; x < width; ++x) {
            horizontal[static_cast<std::size_t>(y) * width + x] =
                static_cast<std::uint32_t>((a / window) << 24 | (r / window) << 16 | (g / window) << 8 | (b / window));
            add(x - radius, -1); add(x + radius + 1, 1);
        }
    }
    for (int x = 0; x < width; ++x) {
        int b = 0, g = 0, r = 0, a = 0;
        const auto add = [&](int y, int sign) {
            const auto p = horizontal[static_cast<std::size_t>(std::clamp(y, 0, height - 1)) * width + x];
            b += sign * channel(p, 0); g += sign * channel(p, 8); r += sign * channel(p, 16); a += sign * channel(p, 24);
        };
        for (int d = -radius; d <= radius; ++d) add(d, 1);
        for (int y = 0; y < height; ++y) {
            output[static_cast<std::size_t>(y) * width + x] =
                static_cast<std::uint32_t>((a / window) << 24 | (r / window) << 16 | (g / window) << 8 | (b / window));
            add(y - radius, -1); add(y + radius + 1, 1);
        }
    }
    WidgetBitmap result{source.width, source.height, std::move(output)};
    return result;
}
}  // namespace

LaunchpadWindow::Layout LaunchpadWindow::LayoutFor(float width, float height, bool collection,int iconScale) {
    Layout layout;
    layout.width = width;
    layout.height = height;
    if (collection) {
        // The collection replaces the grid while the launchpad remains as a
        // full-window blurred backdrop.
        layout.columns = 3;
        layout.rows = 3;
        layout.panel = {0, 0, width, height};
        layout.back = {22, 20, 38, 38};
        layout.search = {width / 2 - 120, height - 44, 240, 30};
        layout.add = {-1000, -1000, 0, 0};
        layout.grid = {width * .12f, 74, width * .76f, std::max(120.f, height - 174)};
        layout.page = {width / 2 - 50, height - 82, 100, 28};
        layout.previous = {layout.page.x - 46, layout.page.y - 1, 30, 30};
        layout.next = {layout.page.x + 116, layout.page.y - 1, 30, 30};
        const float icon=IconSize*static_cast<float>(std::clamp(iconScale,50,120))/100;
        layout.columns=std::clamp(static_cast<int>(layout.grid.width/(icon+32)),1,3);
        layout.rows=std::clamp(static_cast<int>(layout.grid.height/(icon+38)),1,3);
        return layout;
    }
    layout.panel = {0, 0, width, height};
    layout.columns = dock::Columns;
    layout.rows = dock::Rows;
    const float addSize = 38, searchWidth = std::max(160.f, std::min(640.f, width - 96 - addSize - 12));
    const float left = (width - searchWidth - 12 - addSize) / 2;
    layout.back = {left, 23, 38, 38};
    layout.search = {left, 24, searchWidth, 36};
    layout.add = {left + searchWidth + 12, 23, addSize, addSize};
    layout.grid = {36, 80, width - 72, std::max(60.f, height - 80 - 56)};
    layout.page = {width / 2 - 50, height - 46, 100, 30};
    layout.previous = {width / 2 - 96, height - 46, 30, 30};
    layout.next = {width / 2 + 66, height - 46, 30, 30};
    const float icon=IconSize*static_cast<float>(std::clamp(iconScale,50,120))/100;
    layout.columns=std::clamp(static_cast<int>(layout.grid.width/(icon+32)),1,dock::Columns);
    layout.rows=std::clamp(static_cast<int>(layout.grid.height/(icon+38)),1,dock::Rows);
    return layout;
}
WidgetRect LaunchpadWindow::Cell(const Layout& layout, int index) {
    const int columns = std::max(1, layout.columns);
    const int rows = std::max(1, layout.rows);
    const float width = layout.grid.width / columns, height = layout.grid.height / rows;
    return {layout.grid.x + static_cast<float>(index % columns) * width,
            layout.grid.y + static_cast<float>(index / columns) * height, width, height};
}
LaunchpadWindow::Layout LaunchpadWindow::PopupLayoutFor(float width, float height) {
    auto layout = LayoutFor(width, height, true);
    layout.columns=3;layout.rows=3;
    layout.back = {-1000, -1000, 0, 0};
    layout.search = {24, 16, width - 48, 32};
    layout.grid = {18, 60, width - 36, height - 108};
    layout.page = {width / 2 - 50, height - 40, 100, 28};
    layout.previous = {layout.page.x - 46, layout.page.y, 30, 28};
    layout.next = {layout.page.x + 116, layout.page.y, 30, 28};
    return layout;
}
LaunchpadWindow::HitResult LaunchpadWindow::Hit(const Layout& layout, std::size_t itemsOnPage, int page, int pages,
                                                float x, float y, bool collection) {
    if (collection) {
        if (Inside(layout.back, x, y)) return {HitKind::Back, -1};
    } else if (Inside(layout.add, x, y)) return {HitKind::Add, -1};
    if (pages > 1 && page > 0 && Inside(layout.previous, x, y)) return {HitKind::Previous, -1};
    if (pages > 1 && page + 1 < pages && Inside(layout.next, x, y)) return {HitKind::Next, -1};
    for (std::size_t i = 0; i < itemsOnPage; ++i) {
        const auto cell = Cell(layout, static_cast<int>(i));
        // The icon and its label, not the empty margin around them.
        if (Inside({cell.x + 6, cell.y + 4, cell.width - 12, cell.height - 8}, x, y)) return {HitKind::Item, static_cast<int>(i)};
    }
    return {};
}

struct LaunchpadWindow::Impl {
    const bool compact;
    struct ItemMotion {
        DockSpring x, y, scale{1, 0};
        bool placed = false;
    };
    HINSTANCE instance;
    LaunchpadPorts ports;
    HWND window = nullptr, search = nullptr;
    WNDPROC searchProcedure = nullptr;
    HFONT font = nullptr;
    unsigned dpi = 96, searchDpi = 0;
    bool visible = false, closing = false, frosted = false, modal = false, composing = false, reducedMotion = false;
    bool searchPainting = false, searchPaintPending = false, searchShown = false, preparingSearch = false;
    // The live EDIT is shown and focused as the opening starts but draws
    // nothing yet (the panel draws its raster); the hand-off at rest then only
    // swaps pixels, instead of showing and activating a window mid-glide.
    bool searchMasked = false;
    std::wstring presentedText;  // the EDIT text of the last PresentSearch
    bool collectionView = false, collectionClosing = false, collectionMoving = false, renaming = false, renameTile = false;
    int mainPage = 0, renamePage = 0;
    std::string mainQuery, renameQuery;
    std::string collectionId, dropCollectionId, renameId;
    std::wstring collectionTitle;
    OrganizerDropTarget dropTarget;
    bool caretVisible = true, itemsMoving = false, dirty = false;
    UINT caretInterval = 0;
    COLORREF matte = 0;
    ULONG watch = 0;
    int page = 0, hover = -1;
    HitKind hoverKind = HitKind::None;
    std::string query;
    std::wstring typed;  // keys pressed before the search field appears
    std::vector<dock::Entry> all, shown;
    // Final (fully open) window rectangle in screen pixels, and its size in DIPs.
    RECT final{};
    float width = 0, height = 0;
    int panelScale=100,iconScale=100;RECT dockAnchor{};
    DockSpring presence, spreadX{1, 0}, spreadY{1, 0};
    DockSpring collectionExpand;
    WidgetRect collectionFrom;
    std::shared_ptr<const WidgetBitmap> collectionBackground, collectionSharp;
    std::map<std::string, ItemMotion> motion;
    double lastFrame = 0, holdUntil = 0;
    bool started = false, presenceMoving = false;
    // Staged opening: while the panel opens, the glass presents it together
    // with its content (one picture per frame); otherwise the glass window and
    // the layered foreground reach the screen independently and can land a frame
    // apart, which reads as stutter. Only with ready glass; see Compose.
    bool stage = false, stageForeground = false, stageHandoff = false;
    // The close stages the same way: the glass takes the frozen content over
    // (first frame drawn by both), then the foreground steps back.
    bool closeStage = false;
    int closeStageFrames = 0;
    // Close target and vertical sink of the current motion (see Sink).
    float closeSpread = CloseSpread;
    bool pendingClose = false;
    float sinkSpreadFrom = DropletX, sinkSpreadTo = 1, sinkFrom = OpenDrop, sinkTo = 0;
    std::uint32_t bitmapGeneration = 0;  // bumps whenever `bitmap` is re-rendered
    std::uint32_t composedGeneration = 0;  // the generation the staged foreground shows
    bool openingPainted = false;  // Show() has rendered the content the opening plays
    // Opening trace (ui_stall): dropped frames go to the local frame log.
    bool tracing = false;
    double traceStart = 0, traceLast = 0, traceRefresh = 1000.0 / 60;
    std::string traceGaps;
    void BeginTrace() {
        tracing = !reducedMotion;
        traceStart = ui_stall::Now();
        traceLast = 0;
        traceGaps.clear();
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        MONITORINFOEXW monitor{};
        monitor.cbSize = sizeof(monitor);
        traceRefresh = GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) &&
                EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1
            ? 1000.0 / mode.dmDisplayFrequency : 1000.0 / 60;
    }
    void TraceFrame() {
        if (!tracing) return;
        const double now = ui_stall::Now();
        char gap[48]{};
        if (!traceLast) std::snprintf(gap, sizeof(gap), " first=%.0f", now - traceStart);
        else if (now - traceLast > traceRefresh * 1.6) std::snprintf(gap, sizeof(gap), " %.0f@%.0f", now - traceLast, traceLast - traceStart);
        traceGaps += gap;
        traceLast = now;
    }
    void ReportOpen() {
        if (!tracing) return;
        tracing = false;
        // The first frame alone is not a drop.
        if (traceGaps.find('@') != std::string::npos) ui_stall::Report("launchpad-open", traceStart, traceRefresh, traceGaps);
    }
    // Set while a hidden prepare pass paints: the glass is warmed at opacity 0
    // so its last committed frame cannot flash at full strength when the
    // window is later shown, before the first opening frame is rendered.
    bool preparing = false;
    // The glass worker tears its scene down when its owner hides after being
    // shown, and rebuilding it on the next open takes long enough that the
    // icons fade in before the glass. After each close the glass is released
    // and prepared again on the hidden window, as before the first open.
    bool rewarm = false;
    RECT lastDock{};
    // Dragging (indices into `shown`).
    int pressed = -1, dragged = -1, drop = -1;
    bool outside = false;  // the dragged item is over the Dock
    bool refreshPending = false;
    POINT pressPoint{};
    float dragX = 0, dragY = 0, grabX = 0, grabY = 0;
    ULONGLONG edgeSince = 0;
    int edgeDirection = 0;
    std::unique_ptr<WidgetCanvas> canvas;
    WidgetBitmap bitmap;
    std::unique_ptr<WidgetCanvas> animationCanvas;
    WidgetBitmap animationBitmap;
    LayeredSurface animationSurface;
    std::shared_ptr<const WidgetBitmap> animationSource, searchFrame;
    std::unique_ptr<Dib> searchBlack, searchWhite;
    int searchWidth = 0, searchHeight = 0;
    LayeredSurface surface;
    // While the glass presents the panel the window shows this instead: every
    // pixel at alpha 1 and full window opacity, invisible yet hit-testable.
    // (A window opacity of 1 makes Windows treat every pixel as transparent to
    // the pointer: wheel, clicks and drags fell through to the desktop.)
    LayeredSurface hitSurface;
    WidgetBitmap hitBitmap;
    bool ComposeHitSurface(POINT at, SIZE size) {
        if (hitBitmap.width != bitmap.width || hitBitmap.height != bitmap.height) {
            hitBitmap.width = bitmap.width;
            hitBitmap.height = bitmap.height;
            hitBitmap.pixels.assign(static_cast<std::size_t>(bitmap.width) * bitmap.height, 0x01000000u);
            if (!hitSurface.Upload(hitBitmap)) { hitBitmap = {}; return false; }
        }
        return hitSurface.Compose(window, at, size, {}, 255);
    }
    // The dragged item follows the pointer in its own topmost layered window:
    // inside the panel it is no longer clipped by the client edge, and over the
    // Dock it stays above it. Present once per gesture, then only move it.
    HWND ghost = nullptr;
    std::unique_ptr<WidgetCanvas> ghostCanvas;
    WidgetBitmap ghostBitmap;
    LayeredSurface ghostSurface;
    float ghostSize = 0;
    bool ghostPlaced = false;
    WidgetMaterialRuntime material;
    LumeMaterialParameters lastMaterial{};
    LumeMaterialPanel flatPanels[8]{};
    unsigned flatPanelCount = 0;
    // The staged open or close as a timeline the glass follows by display time
    // (see LumeMaterialParameters::timeline): the springs are closed-form, so
    // sampling them ahead gives exactly the states Step() reaches.
    static constexpr float TimelineStepMs = 2;
    std::vector<LumeMaterialKeyframe> timeline;
    std::vector<std::array<DockSpring, 3>> timelineSprings;  // spreadX, spreadY, presence per frame
    LONGLONG timelineStart = 0;
    // The springs as the glass shows them `aheadMs` from now (it presents for the
    // next refresh), from the running timeline; false without one.
    bool TimelineState(double aheadMs, DockSpring& x, DockSpring& y, DockSpring& fade) const {
        if (timelineSprings.empty()) return false;
        LARGE_INTEGER now{}, frequency{};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&frequency);
        const double t = (static_cast<double>(now.QuadPart - timelineStart) * 1000.0 / static_cast<double>(frequency.QuadPart) + aheadMs) / TimelineStepMs;
        const double last = static_cast<double>(timelineSprings.size() - 1);
        const double clamped = std::clamp(t, 0.0, last);
        const auto index = static_cast<std::size_t>(clamped);
        const auto& a = timelineSprings[index];
        const auto& b = timelineSprings[std::min(index + 1, timelineSprings.size() - 1)];
        const float f = static_cast<float>(clamped - static_cast<double>(index));
        const auto mix = [f](const DockSpring& p, const DockSpring& q) {
            return DockSpring{p.value + (q.value - p.value) * f, p.velocity + (q.velocity - p.velocity) * f};
        };
        x = mix(a[0], b[0]); y = mix(a[1], b[1]); fade = mix(a[2], b[2]);
        return true;
    }
    double RefreshMs() const {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        MONITORINFOEXW monitor{};
        monitor.cbSize = sizeof(monitor);
        return GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) &&
                EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1
            ? 1000.0 / mode.dmDisplayFrequency : 1000.0 / 60;
    }
    LumeMaterialKeyframe Keyframe(float x, float y, float fade) const {
        const float sx = std::clamp(x, .05f, MaxSpread), sy = std::clamp(y, .05f, MaxSpread);
        const float s = std::max(.05f, std::min(sx, sy));
        const float frameWidth = width * sx, frameHeight = height * sy;
        const float offsetX = (frameWidth - width * s) / 2, offsetY = (frameHeight - height * s) / 2;
        const float sink = Sink(x);
        LumeMaterialKeyframe k{};
        k.lensX = (width - frameWidth) / 2;
        k.lensY = (height - frameHeight) / 2 + sink;
        k.lensWidth = frameWidth;
        k.lensHeight = frameHeight;
        k.contentX = offsetX;
        k.contentY = offsetY;
        k.contentWidth = width * s;
        k.contentHeight = height * s;
        k.opacity = std::clamp(fade * 1.6f, 0.f, 1.f);
        // The labels and icons follow the panel: faint frost hardly shows over
        // the wallpaper, so content on the same curve looked as if the text
        // arrived first. It starts once the panel is half in and leaves first.
        const float c = std::clamp((fade - .3f) / .6f, 0.f, 1.f);
        k.contentOpacity = c * c * (3 - 2 * c);
        k.cornerRadius = CornerRadius * std::min(s, 1.f);
        k.panelCount = flatPanelCount;
        for (unsigned i = 0; i < flatPanelCount; ++i) {
            const auto& r = flatPanels[i];
            k.panels[i] = {offsetX + r.x * s, offsetY + r.y * s, r.width * s, r.height * s, r.radius < 0 ? r.radius : r.radius * s};
        }
        return k;
    }
    // `aheadMs`: the first frame belongs that far in the future (a resumed motion
    // starts where the glass will show its previous one at the next refresh).
    void BuildTimeline(bool closingMotion, double aheadMs = 0) {
        timeline.clear();
        timelineSprings.clear();
        LARGE_INTEGER now{}, frequency{};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&frequency);
        timelineStart = now.QuadPart + static_cast<LONGLONG>(aheadMs * static_cast<double>(frequency.QuadPart) / 1000.0);
        DockSpring x = spreadX, y = spreadY, fade = presence;
        const float dt = TimelineStepMs / 1000;
        timeline.push_back(Keyframe(x.value, y.value, fade.value));
        timelineSprings.push_back({x, y, fade});
        for (int i = 0; i < 1500; ++i) {
            bool moving;
            if (closingMotion) {
                x.Step(closeSpread, dt, CloseOmega, CloseZeta, .002f, false);
                y.Step(closeSpread, dt, CloseOmega, CloseZeta, .002f, false);
                moving = fade.Step(0, dt, CloseOmega, CloseZeta, .002f, false) && fade.value >= .02f;
            } else {
                moving = x.Step(1, dt, SpreadXOmega, SpreadXZeta, .001f, false) |
                         y.Step(1, dt, SpreadYOmega, SpreadYZeta, .001f, false) |
                         fade.Step(1, dt, FadeOmega, FadeZeta, .002f, false);
            }
            timeline.push_back(Keyframe(x.value, y.value, fade.value));
            timelineSprings.push_back({x, y, fade});
            if (!moving) break;
        }
    }
    FrameClock clock;
    Impl(HINSTANCE i, LaunchpadPorts p, Presentation presentation)
        : compact(presentation == Presentation::CollectionPopup), instance(i), ports(std::move(p)) {}
    ~Impl() { Stop(); }
    Layout CurrentLayout() const { return compact ? PopupLayoutFor(width, height) : LayoutFor(width, height, collectionView,iconScale); }
    float IconSide(WidgetRect cell)const{return std::max(1.f,std::min({IconSize*static_cast<float>(compact?100:iconScale)/100,cell.width-20,cell.height-LabelHeight-14}));}
    WidgetRect CollectionPanel() const {
        const float e = std::clamp(collectionExpand.value, 0.f, 1.f);
        const auto target = LayoutFor(width, height, true).panel;
        return {collectionFrom.x + (target.x - collectionFrom.x) * e,
                collectionFrom.y + (target.y - collectionFrom.y) * e,
                collectionFrom.width + (target.width - collectionFrom.width) * e,
                collectionFrom.height + (target.height - collectionFrom.height) * e};
    }
    WidgetRect MapCollectionRect(WidgetRect r) const {
        if (!collectionView) return r;
        const auto target = LayoutFor(width, height, true).panel;
        const auto panel = CollectionPanel();
        const float sx = panel.width / target.width, sy = panel.height / target.height;
        return {panel.x + (r.x - target.x) * sx, panel.y + (r.y - target.y) * sy, r.width * sx, r.height * sy};
    }
    float CollectionScale() const {
        if (!collectionView) return 1;
        const auto target = LayoutFor(width, height, true).panel;
        const auto panel = CollectionPanel();
        return std::min(panel.width / target.width, panel.height / target.height);
    }
    float CollectionAlpha() const { return collectionView ? std::clamp(collectionExpand.value, 0.f, 1.f) : 1.f; }
    // During a drag the dragged item is shown at its drop position.
    std::vector<dock::Entry> Preview() const {
        if (dragged < 0 || drop < 0) return shown;
        auto result = shown;
        const auto moving = result[static_cast<std::size_t>(dragged)];
        result.erase(result.begin() + dragged);
        result.insert(result.begin() + std::min<std::ptrdiff_t>(drop, static_cast<std::ptrdiff_t>(result.size())), moving);
        if (!collectionView && !moving.collection) {
            // Collection tiles are drop targets, not insertion gaps. Keep them
            // at their original slots throughout a continuous drag.
            std::vector<dock::Entry> ordinary;
            for (const auto& entry : result) if (!entry.collection) ordinary.push_back(entry);
            std::size_t next = 0;
            for (std::size_t i = 0; i < result.size(); ++i)
                result[i] = shown[i].collection ? shown[i] : ordinary[next++];
        }
        return result;
    }
    int PageSize() const {const auto layout=CurrentLayout();return std::max(1,layout.columns*layout.rows);}
    int Pages() const { return std::max(1, static_cast<int>((shown.size() + PageSize() - 1) / PageSize())); }
    std::vector<dock::Entry> PageEntries() const {
        const auto preview = Preview();
        const std::size_t begin = std::min(preview.size(), static_cast<std::size_t>(std::max(0, page)) * PageSize());
        const std::size_t end = std::min(preview.size(), begin + PageSize());
        return {preview.begin() + static_cast<std::ptrdiff_t>(begin), preview.begin() + static_cast<std::ptrdiff_t>(end)};
    }
    // Content scale: uniform, the smaller spread, so icons never stretch.
    // The rise (OpenDrop) follows the unfolding itself, not the fade, so the
    // panel moves on one rhythm: open from the droplet, close toward CloseSpread.
    // Each motion carries the sink from where the panel sits when it starts, so a
    // reversal (close mid-opening, reopen mid-closing) never shifts the panel.
    float Sink(float spread) const {
        const float span = sinkSpreadTo - sinkSpreadFrom;
        const float t = std::abs(span) < .001f ? 1.f : std::clamp((spread - sinkSpreadFrom) / span, 0.f, 1.f);
        return sinkFrom + (sinkTo - sinkFrom) * t;
    }
    void AimSink(float spread, bool closingMotion) {
        sinkFrom = Sink(spread);
        sinkSpreadFrom = spread;
        sinkSpreadTo = closingMotion ? closeSpread : 1.f;
        sinkTo = closingMotion ? static_cast<float>(OpenDrop) : 0.f;
    }
    float Scale() const { return std::max(.05f, std::min(spreadX.value, spreadY.value)); }
    // Frame size relative to the final panel, per axis (may overshoot 1).
    float SpreadX() const { return std::clamp(spreadX.value, .05f, MaxSpread); }
    float SpreadY() const { return std::clamp(spreadY.value, .05f, MaxSpread); }
    bool IsDragged(const dock::Entry& entry) const {
        return dragged >= 0 && entry.path == shown[static_cast<std::size_t>(dragged)].path;
    }
    bool EnsureGhost() {
        if (ghost) return true;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = DefWindowProcW;
        wc.lpszClassName = DragGhostClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        ghost = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                                DragGhostClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
        if (!ghost) return false;
        ghostCanvas = std::make_unique<WidgetCanvas>();
        return true;
    }
    // Render the dragged icon once per gesture; moving it only repositions the
    // window, so a drag costs one small layered update per pointer message
    // instead of a full panel render per frame.
    bool PresentGhost() {
        if (dragged < 0 || static_cast<std::size_t>(dragged) >= shown.size() || !EnsureGhost()) return false;
        ghostPlaced = false;
        const auto& entry = shown[static_cast<std::size_t>(dragged)];
        const auto cell = Cell(CurrentLayout(), dragged % PageSize());
        ghostSize = IconSide(cell) * 1.12f;
        const float padding = 6, side = ghostSize + padding * 2;
        if (!ghostCanvas->Begin(side, side, dpi)) return false;
        const WidgetRect icon{padding, padding, ghostSize, ghostSize};
        // No backing shadow: a hard square behind icons with transparent margins
        // read as a dark tile under the dragged icon.
        if (entry.collection) ghostCanvas->Rounded(icon, ghostSize * .22f, 0xffffff, .14f);
        else if (const auto iconBitmap = ports.icon ? ports.icon(entry.path) : nullptr)
            ghostCanvas->ImageBitmap(iconBitmap, icon, 1.f);
        else ghostCanvas->Rounded(icon, ghostSize * .22f, 0xffffff, .12f);
        return ghostCanvas->Finish(ghostBitmap);
    }
    void MoveGhost() {
        if (!ghost || dragged < 0) return;
        POINT origin{static_cast<LONG>(std::lround((dragX - grabX) * dpi / 96)) - 6,
                     static_cast<LONG>(std::lround((dragY - grabY) * dpi / 96)) - 6};
        ClientToScreen(window, &origin);
        if (!ghostPlaced) {
            if (!ghostSurface.Present(ghost, ghostBitmap, &origin)) return;
            ghostPlaced = true;
            ShowWindow(ghost, SW_SHOWNOACTIVATE);
            return;
        }
        // Moving an already presented layered window reuses its pixels; a
        // per-message UpdateLayeredWindow would re-upload and sync the
        // compositor on every pointer sample.
        SetWindowPos(ghost, nullptr, origin.x, origin.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    }
    void HideGhost() { if (ghost) ShowWindow(ghost, SW_HIDE); }
    void DestroyGhost() {
        HideGhost();
        if (ghost) { const auto old = ghost; ghost = nullptr; DestroyWindow(old); }
        ghostCanvas.reset();
        ghostSurface.Release();
        ghostSize = 0;
        ghostPlaced = false;
    }
    bool Create() {
        if (window) return true;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = Procedure;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = compact ? lume::window_ids::CollectionPopupClass : ClassName;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, wc.lpszClassName, compact ? L"Lume 收纳" : L"Lume 启动台", WS_POPUP,
                                 0, 0, 1, 1, nullptr, nullptr, instance, this);
        if (!window) return false;
        // Drop files onto a collection tile, or onto the flat grid to add them.
        // The target is attached to this window only; it never touches Explorer.
        dropTarget.Attach(window,
            [this](const OrganizerDropFeedback& feedback) { DropFeedback(feedback); },
            [this](const std::vector<std::string>& paths, POINT point) { return DropExternal(paths, point); });
        canvas = std::make_unique<WidgetCanvas>();
        return true;
    }
    void Filter() {
        shown = ports.filter ? ports.filter(all, query) : all;
        page = std::clamp(page, 0, Pages() - 1);
        hover = -1;
    }
    void Refresh() {
        if (collectionClosing) return;
        // A desktop change during a drag must not renumber the items under the
        // pointer; apply it once the drag ends.
        if (dragged >= 0) { refreshPending = true; return; }
        all = ports.entries ? ports.entries() : std::vector<dock::Entry>{};
        Filter();
    }
    void SetCollectionView(std::string id, std::string title) {
        if (compact) {
            const bool changed = collectionId != id;
            collectionId = std::move(id); collectionTitle = OrganizerWide(title);
            collectionView = true; collectionClosing = false; collectionExpand = {1, 0};
            if (changed) { page = 0; query.clear(); }
            Refresh();
            if (visible) { Animate(); Paint(); }
            return;
        }
        if (id.empty() && collectionView && visible) {
            collectionClosing = true;
            if (search) ShowWindow(search, SW_HIDE);
            searchShown = searchMasked = false;
            SetFocus(window);
            Animate();
            if (reducedMotion) FinishCollectionClose();
            Paint();
            return;
        }
        const bool opening = !id.empty() && !collectionView;
        if (opening) {
            mainPage = page; mainQuery = query;
            FreezeSearch();
            Paint(); // Include the exact input text in the collection backdrop.
            collectionSharp = std::make_shared<const WidgetBitmap>(bitmap);
            // Snapshot the main grid before switching content. The expanded
            // collection is drawn over this genuinely blurred frame.
            auto blurred = Blur(bitmap, std::max(1, MulDiv(20, static_cast<int>(dpi), 96)));
            collectionBackground = blurred.pixels.empty()
                ? nullptr : std::make_shared<const WidgetBitmap>(std::move(blurred));
            collectionExpand = {0, 0};
            collectionFrom = {width / 2, height / 2, 1, 1};
            const auto mainLayout = LayoutFor(width, height, false,iconScale);
            const auto entries = PageEntries();
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (!entries[i].collection || entries[i].collectionId != id) continue;
                const auto cell = Cell(mainLayout, static_cast<int>(i));
                const float base = IconSide(cell);
                const float top = cell.y + (cell.height - base - LabelHeight - 6) / 2;
                collectionFrom = {cell.x + (cell.width - base) / 2, top, base, base};
                break;
            }
        } else if (id.empty()) {
            collectionBackground.reset();
            collectionSharp.reset();
            collectionExpand = {0, 0};
            collectionFrom = {};
        }
        collectionId = std::move(id);
        collectionClosing = false;
        collectionView = !collectionId.empty();
        collectionTitle = OrganizerWide(title);
        if (search) ShowWindow(search, SW_HIDE);
        searchShown = searchMasked = false;
        renaming = false;
        renameTile = false;
        renameId.clear();
        query.clear();
        typed.clear();
        page = 0;
        Refresh();
        if (visible) {
            SetFocus(window);
            Animate();
            Paint();
        }
    }
    void FinishCollectionClose() {
        collectionClosing = collectionView = false;
        collectionId.clear(); collectionTitle.clear();
        collectionExpand = {}; collectionFrom = {};
        collectionBackground.reset(); collectionSharp.reset();
        query = mainQuery; page = mainPage;
        Refresh();
        if (search) {
            preparingSearch = true;
            SetWindowTextW(search, OrganizerWide(query).c_str());
            preparingSearch = false;
            PositionSearch();
            PresentSearch();
        }
    }
    // The tile label is where a desktop-style inline rename belongs. Reuse the
    // native EDIT and move it over that label instead of the top search field.
    void PositionRename() {
        if (!search || !window) return;
        const auto layout = CurrentLayout();
        const auto entries = PageEntries();
        int index = -1;
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i].collection && entries[i].collectionId == renameId) { index = static_cast<int>(i); break; }
        if (index < 0) { PositionSearch(); return; }
        const auto cell = Cell(layout, index);
        const float base = IconSide(cell);
        const float top = cell.y + (cell.height - base - LabelHeight - 6) / 2;
        const WidgetRect r{cell.x + 4, top + base + 6, cell.width - 8, LabelHeight};
        const float scale = static_cast<float>(dpi) / 96;
        SetWindowPos(search, nullptr, final.left + static_cast<int>(r.x * scale),
                     final.top + static_cast<int>(r.y * scale),
                     static_cast<int>(r.width * scale), static_cast<int>(r.height * scale),
                     SWP_NOACTIVATE | SWP_NOZORDER);
        if (searchDpi != dpi) {
            searchDpi = dpi;
            const auto replacement = CreateFontW(-MulDiv(static_cast<int>(LabelSize), static_cast<int>(dpi), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                                 DEFAULT_PITCH, ui_font::Family(L"搜索"));
            if (replacement) {
                SendMessageW(search, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), FALSE);
                if (font) DeleteObject(font);
                font = replacement;
            }
        }
        if (searchShown) RequestSearchPaint();
    }
    void BeginRename(std::string id, std::string title) {
        if (renaming) EndRename(false);
        if (id.empty() || !PrepareSearch()) return;
        renameQuery = query;
        renamePage = page;
        renameId = std::move(id);
        renameTile = !collectionView;
        renaming = true;
        SetWindowLongPtrW(search, GWL_STYLE, (GetWindowLongPtrW(search, GWL_STYLE) & ~(ES_CENTER | ES_RIGHT)) | ES_CENTER);
        searchDpi = 0;
        preparingSearch = true;
        const bool set = SetWindowTextW(search, OrganizerWide(title).c_str()) != FALSE;
        preparingSearch = false;
        if (!set) { EndRename(false); return; }
        typed.clear();
        searchShown = true;
        if (renameTile) PositionRename();
        else PositionSearch();
        Paint();
        ShowWindow(search, SW_SHOW);
        SetFocus(search);
        SendMessageW(search, EM_SETSEL, 0, -1);
    }
    void EndRename(bool commit) {
        if (!renaming) return;
        const auto title = OrganizerUtf8(SearchText());
        const auto id = renameId;
        // The rename callback can refresh/reset the collection view. Preserve
        // navigation locally and restore the reused search EDIT afterwards.
        const auto previousQuery = renameQuery;
        const int previousPage = renamePage;
        searchShown = searchMasked = false;
        if (search) ShowWindow(search, SW_HIDE);
        if (commit && !title.empty() && !id.empty() && ports.rename) ports.rename(id, title);
        renaming = false;
        renameTile = false;
        renameId.clear();
        renameQuery.clear();
        query = previousQuery;
        page = previousPage;
        Refresh();
        if (search) {
            preparingSearch = true;
            SetWindowTextW(search, OrganizerWide(query).c_str());
            preparingSearch = false;
            SetWindowLongPtrW(search, GWL_STYLE, GetWindowLongPtrW(search, GWL_STYLE) & ~(ES_CENTER | ES_RIGHT));
            SendMessageW(search, EM_SETSEL, 0, 0);
            searchDpi = 0;
            PositionSearch();
        }
        if (visible) {
            if (!collectionView) BeginSearch();
            else SetFocus(window);
        }
        Paint();
    }
    std::string CollectionAt(POINT client) const {
        if (collectionView) return collectionId;
        const float x = static_cast<float>(client.x) * 96 / dpi / Scale();
        const float y = static_cast<float>(client.y) * 96 / dpi / Scale();
        const auto hit = Hit(CurrentLayout(), PageEntries().size(), page, Pages(), x, y, false);
        if (hit.kind != HitKind::Item || hit.index < 0) return {};
        const auto items = PageEntries();
        if (static_cast<std::size_t>(hit.index) >= items.size()) return {};
        const auto& entry = items[static_cast<std::size_t>(hit.index)];
        return entry.collection ? entry.collectionId : std::string{};
    }
    void DropFeedback(const OrganizerDropFeedback& feedback) {
        const auto id = feedback.phase == OrganizerDropFeedback::Phase::Leave ? std::string{}
                                                                              : CollectionAt(feedback.point);
        if (id != dropCollectionId) {
            dropCollectionId = id;
            Paint();
        }
    }
    bool DropExternal(const std::vector<std::string>& paths, POINT client) {
        const auto id = CollectionAt(client);
        dropCollectionId.clear();
        Paint();
        if (!id.empty() && ports.dropIntoCollection) return ports.dropIntoCollection(paths, id);
        return ports.dropPaths ? ports.dropPaths(paths) : false;
    }
    bool Geometry(RECT dockBounds){
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(MonitorFromRect(&dockBounds, MONITOR_DEFAULTTOPRIMARY), &info)) return false;
        if (!visible&&!closing) SetWindowPos(window, nullptr, info.rcWork.left, info.rcWork.top, 1, 1, SWP_NOACTIVATE | SWP_NOZORDER);
        dpi = GetDpiForWindow(window);
        if (!dpi) dpi = 96;
        const auto px = [&](float dip) { return static_cast<int>(std::lround(dip * static_cast<float>(dpi) / 96)); };
        const int workWidth = info.rcWork.right - info.rcWork.left;
        const int w = std::min(px(compact ? 390.f : PreferredWidth*static_cast<float>(panelScale)/100), workWidth - px(40));
        const int gap = px(16);
        const int below = dockBounds.bottom > dockBounds.top ? dockBounds.top - gap : info.rcWork.bottom - gap;
        const int h = std::min(px(compact ? 400.f : PreferredHeight*static_cast<float>(panelScale)/100), static_cast<int>(below - info.rcWork.top - gap));
        if (w < px(compact ? 280.f : 480.f) || h < px(compact ? 280.f : 320.f)) return false;
        const int x = compact ? std::clamp(static_cast<int>((dockBounds.left + dockBounds.right - w) / 2),
                                         static_cast<int>(info.rcWork.left) + px(12), static_cast<int>(info.rcWork.right) - w - px(12))
                              : info.rcWork.left + (workWidth - w) / 2;
        const int y = std::max(static_cast<int>(info.rcWork.top) + gap, below - h);
        final = {x, y, x + w, y + h};
        width = static_cast<float>(w) * 96 / dpi;
        height = static_cast<float>(h) * 96 / dpi;
        dockAnchor=dockBounds;return true;
    }
    bool SetAppearance(int nextPanel,int nextIcon){
        if(nextPanel<dock::MinLaunchpadScale||nextPanel>dock::MaxLaunchpadScale||
           nextIcon<dock::MinLaunchpadIconScale||nextIcon>dock::MaxLaunchpadIconScale)return false;
        if(panelScale==nextPanel&&iconScale==nextIcon)return true;
        const int oldPanel=panelScale,oldIcon=iconScale;panelScale=nextPanel;iconScale=nextIcon;
        if(!window)return true;
        if(!Geometry(dockAnchor)){panelScale=oldPanel;iconScale=oldIcon;return false;}
        if(dragged>=0){dragged=drop=pressed=-1;if(GetCapture()==window)ReleaseCapture();}
        collectionSharp.reset();collectionBackground.reset();motion.clear();
        page=std::clamp(page,0,Pages()-1);
        if(!visible)return true;
        PositionSearch();return Paint();
    }
    bool Show(RECT dockBounds, bool nextFrosted, bool prepareOnly = false) {
        if (!Create()) return false;
        if (prepareOnly && (visible || closing)) return true;
        frosted = nextFrosted;
        BOOL animate = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0);
        reducedMotion = !animate;
        if(!Geometry(dockBounds))return false;
        lastDock = dockBounds;
        if (!prepareOnly) rewarm = true;
        openingPainted = false;
        // Reopened while still closing on the glass: continue from where the close
        // is now, on a fresh opening timeline (the close's must not play on).
        const bool resume = !prepareOnly && closing && closeStage && !reducedMotion &&
                            material.State() == WidgetMaterialState::Ready;
        // Continue from the state the glass shows at the next refresh, not from the
        // springs here, which trail the screen by a frame or two.
        const double ahead = resume ? RefreshMs() : 0;
        if (resume) TimelineState(ahead, spreadX, spreadY, presence);
        timeline.clear();
        timelineSprings.clear();
        closeStage = pendingClose = false;
        if (!closing) {
            query.clear();
            typed.clear();
            page = 0;
            presence = {};
            spreadX = {DropletX, 0};
            spreadY = {DropletY, 0};
            motion.clear();
        } else {
            AimSink(spreadX.value, false);  // reopened mid-close: continue from here
        }
        if (presence.value <= 0) {
            sinkSpreadFrom = DropletX; sinkSpreadTo = 1; sinkFrom = OpenDrop; sinkTo = 0;
        }
        closing = false;
        pressed = dragged = drop = -1;
        Refresh();
        visible = true;
        if (reducedMotion) { presence = {1, 0}; spreadX = spreadY = {1, 0}; }
        if (prepareOnly) { presence = {1, 0}; spreadX = spreadY = {1, 0}; }
        // A warm material starts on the next frame. Cold capture gets a bounded
        // readiness window, with no compulsory three-frame/90ms delay.
        holdUntil = AnimationSeconds() + .12;
        started = reducedMotion || prepareOnly;
        // With warm glass the stage starts now, during the invisible hold, so its
        // one-off set-up (content upload, buffers, sampled scene) never costs a
        // frame of the visible motion.
        stage = !started && !closing && material.State() == WidgetMaterialState::Ready;
        stageForeground = stageHandoff = false;
        if (resume) started = stage = true;  // the glass is live: no hold
        presenceMoving = !started || resume;
        searchShown = searchMasked = false;
        if (search) ShowWindow(search, SW_HIDE);
        // Initialize the native edit and its first bitmap before any opening
        // frame is visible, instead of blocking the spring's last frame.
        preparing = prepareOnly;
        const bool painted = PrepareSearch() && Paint();
        preparing = false;
        if (!painted) { DestroySearch(); Finish(); return false; }
        openingPainted = true;
        if (resume) { BuildTimeline(false, ahead); Compose(); }
        if (prepareOnly) {
            // Glass attached to a hidden owner idles until the first Show.
            visible = false;
            presence = {};
            spreadX = {DropletX, 0};
            spreadY = {DropletY, 0};
            return true;
        }
        SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindow(window, SW_SHOW);
        holdUntil = AnimationSeconds() + .12;
        SetForegroundWindow(window);
        SetFocus(window);
        if (!reducedMotion && search) {
            // Show and activate the (still blank) live EDIT now, while the
            // first frame is held: showing it at rest cost two to three frames.
            // Blank it before it is shown: it still holds the fully drawn
            // placeholder from preparing (or the last close), and DWM could show
            // that for a frame ahead of the panel.
            searchMasked = true;
            PresentSearch();
            ShowWindow(search, SW_SHOW);
            SetFocus(search);
        }
        watch = dock_sources::WatchDesktop(window, DesktopChanged);
        BeginTrace();
        if (reducedMotion && !BeginSearch()) { Hide(); return false; }
        Animate();
        if (ports.visibility) ports.visibility();
        return true;
    }
    // Closed mid-opening the panel stops growing and shrinks from where it is,
    // instead of first unfolding on to the full close size.
    void AimClose() {
        closeSpread = std::min(CloseSpread, std::min(spreadX.value, spreadY.value) - .04f);
        spreadX.velocity = std::min(spreadX.velocity, 0.f);
        spreadY.velocity = std::min(spreadY.velocity, 0.f);
        AimSink(spreadX.value, true);
    }
    void Hide() {
        if (!visible) return;
        stage = stageForeground = stageHandoff = false;
        closeStage = !reducedMotion && window && material.State() == WidgetMaterialState::Ready;
        closeStageFrames = 0;
        // The close timeline is built in Compose, right before it reaches the glass:
        // the paint below can take a frame or two, and a close timed from here would
        // start behind what the glass has shown meanwhile (a visible snap back).
        pendingClose = closeStage;
        if (!closeStage) {
            timeline.clear();
            timelineSprings.clear();
            AimClose();
        }
        HideGhost();
        FreezeSearch();
        // Retain the same text/selection as the live EDIT throughout closing.
        Paint();
        visible = false;
        closing = true;
        pressed = dragged = drop = -1;
        renaming = false;
        renameTile = false;
        renameId.clear();
        if (collectionView) {
            collectionView = false;
            collectionId.clear();
            collectionTitle.clear();
            if (ports.back) ports.back();
        }
        if (GetCapture() == window) ReleaseCapture();
        dock_sources::StopWatching(watch);
        watch = 0;
        if (window) KillTimer(window, RefreshTimer);
        if (reducedMotion || !window) Finish();
        else Animate();
        if (ports.visibility) ports.visibility();
    }
    // Ends a close: hides the window and releases the glass.
    void Finish() {
        stage = stageForeground = stageHandoff = false;
        closeStage = pendingClose = false;
        timeline.clear();
        timelineSprings.clear();
        closing = false;
        visible = false;
        clock.Pause();
        if (window) ShowWindow(window, SW_HIDE);
        presenceMoving = false;
        hover = -1;
        hoverKind = HitKind::None;
        if (window && std::exchange(rewarm, false)) PostMessageW(window, RewarmMessage, 0, 0);
    }
    void Stop() {
        rewarm = false;
        const bool was = visible;
        visible = false;
        renaming = false;
        renameTile = false;
        renameId.clear();
        dock_sources::StopWatching(watch);
        watch = 0;
        DestroySearch();
        Finish();
        material.Detach();
        surface.Release();
        animationSurface.Release();
        hitSurface.Release();
        hitBitmap = {};
        animationCanvas.reset();
        animationBitmap = {};
        animationSource.reset(); searchFrame.reset();
        dropTarget.Detach();
        DestroyGhost();
        if (window) { auto old = window; window = nullptr; DestroyWindow(old); }
        canvas.reset();
        bitmap = {};
        collectionView = collectionClosing = false;
        collectionBackground.reset(); collectionSharp.reset();
        if (was && ports.visibility) ports.visibility();
    }
    void Animate() {
        if (!window || clock.Running()) return;
        lastFrame = AnimationSeconds();
        clock.Start(window, FrameMessage);
    }
    // Item targets in final-layout DIPs for the current page.
    bool StepItems(float dt) {
        const auto layout = CurrentLayout();
        const auto items = PageEntries();
        bool moving = false;
        for (std::size_t i = 0; i < items.size(); ++i) {
            auto& m = motion[items[i].path];
            const auto cell = Cell(layout, static_cast<int>(i));
            if (!m.placed) {
                m.x = {cell.x, 0};
                m.y = {cell.y, 0};
                m.scale = {1, 0};
                m.placed = true;
            }
            const bool hovered = hoverKind == HitKind::Item && hover == static_cast<int>(i) && dragged < 0;
            moving |= m.x.Step(cell.x, dt, SlideOmega, SlideZeta, .05f, reducedMotion);
            moving |= m.y.Step(cell.y, dt, SlideOmega, SlideZeta, .05f, reducedMotion);
            moving |= m.scale.Step(IsDragged(items[i]) ? 1.12f : hovered ? 1.06f : 1.f, dt, MagnifyOmega, MagnifyZeta, .0005f,
                                   reducedMotion);
        }
        return moving;
    }
    bool Step() {
        const double now = AnimationSeconds();
        const double interval = now - lastFrame;
        const float dt = std::clamp(static_cast<float>(interval), .0005f, .05f);
        lastFrame = now;
        itemsMoving = false;
        // Closing depends only on the panel spring, so it always finishes.
        if (closing) {
            spreadX.Step(closeSpread, dt, CloseOmega, CloseZeta, .002f, reducedMotion);
            spreadY.Step(closeSpread, dt, CloseOmega, CloseZeta, .002f, reducedMotion);
            return presence.Step(0, dt, CloseOmega, CloseZeta, .002f, reducedMotion);
        }
        if (!started) {
            const auto status = material.Status();
            if (!status.failed && !status.frameReady && now < holdUntil) return true;
            started = true;
            if (!stage) {
                stage = !reducedMotion && !preparing && material.State() == WidgetMaterialState::Ready;
                stageForeground = stageHandoff = false;
            }
            if (stage) BuildTimeline(false);
        }
        // As the dynamic island opens: width and height spring out on their own
        // springs from a smaller droplet, with a slight overshoot, while the
        // panel fades in on a firmer spring.
        const bool spreading = spreadX.Step(1, dt, SpreadXOmega, SpreadXZeta, .001f, reducedMotion) |
                               spreadY.Step(1, dt, SpreadYOmega, SpreadYZeta, .001f, reducedMotion);
        presenceMoving = presence.Step(1, dt, FadeOmega, FadeZeta, .002f, reducedMotion) | spreading;
        bool moving = presenceMoving;
        collectionMoving = collectionExpand.Step(collectionView && !collectionClosing ? 1.f : 0.f, dt,
                                                 OpenOmega, OpenZeta, .002f, reducedMotion);
        moving |= collectionMoving;
        if (collectionClosing && !collectionMoving) { FinishCollectionClose(); dirty = true; }
        // While dragging, the grid springs to the preview order exactly as
        // before; the pointer's icon is separate and costs nothing here.
        itemsMoving = StepItems(dt);
        moving |= itemsMoving;
        if (dragged >= 0) { EdgeTurn(); moving = true; }
        return moving;
    }
    void Settle() {
        if (closing) { presence = {0, 0}; spreadX = spreadY = {1, 0}; Finish(); return; }
        presence = {1, 0};
        spreadX = spreadY = {1, 0};
        presenceMoving = false;
        started = true;
        if (collectionClosing) FinishCollectionClose();
        collectionExpand = {collectionView ? 1.f : 0.f, 0};
        const auto layout = CurrentLayout();
        const auto items = PageEntries();
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto cell = Cell(layout, static_cast<int>(i));
            auto& m = motion[items[i].path];
            m.x = {cell.x, 0}; m.y = {cell.y, 0}; m.scale = {1, 0}; m.placed = true;
        }
        if (visible && !searchShown && !collectionView) { if (!BeginSearch()) Paint(); }
        else Paint();
    }
    // Screen rectangle for the current presence, scaled about the final centre.
    RECT Frame() const {
        const int w = std::max(1, static_cast<int>(std::lround((final.right - final.left) * SpreadX())));
        const int h = std::max(1, static_cast<int>(std::lround((final.bottom - final.top) * SpreadY())));
        const float sink = Sink(SpreadX()) * static_cast<float>(dpi) / 96;
        const int x = final.left + (final.right - final.left - w) / 2;
        const int y = final.top + (final.bottom - final.top - h) / 2 + static_cast<int>(std::lround(sink));
        return {x, y, x + w, y + h};
    }
    void DrawContent() {
        const float s = CollectionScale(), alpha = CollectionAlpha();
        const auto R = [this](const WidgetRect& r) { return MapCollectionRect(r); };
        const auto layout = CurrentLayout();
        if (collectionView && !compact) {
            if (collectionSharp) canvas->ImageFrame(collectionSharp, {0, 0, width, height}, 1 - alpha);
            if (collectionBackground) canvas->ImageFrame(collectionBackground, {0, 0, width, height}, alpha);
            else canvas->Rounded({0, 0, width, height}, 0, 0x07111f, .72f);
            canvas->Rounded({0, 0, width, height}, 0, 0x000000, .34f * alpha);
        }
        canvas->Rounded({0, 0, width * s, height * s}, CornerRadius * s, 0xffffff, 1.f / 255);
        const auto field = R(layout.search);
        if (collectionView) {
            const auto back = R(layout.back);
            const float cx = back.x + back.width / 2, cy = back.y + back.height / 2;
            if (!compact) {
                if (hoverKind == HitKind::Back) canvas->Rounded(back, back.width / 2, 0xffffff, .10f);
                canvas->Line(cx + 5 * s, cy - 8 * s, cx - 3 * s, cy, 0xffffff, .9f, 1.8f * s);
                canvas->Line(cx - 3 * s, cy, cx + 5 * s, cy + 8 * s, 0xffffff, .9f, 1.8f * s);
            }
            if (!(renaming && !renameTile))
                canvas->Text(collectionTitle, {field.x + 4 * s, field.y, field.width - 8 * s, field.height}, 17 * s,
                             0xffffff, .95f * alpha, false, WidgetTextAlign::Center);
        } else {
            // Search field and add button sit on frosted glass panels (as the calendar's
            // round buttons do); only their glyphs are drawn here.
            const float gx = field.x + 22 * s, gy = field.y + field.height / 2 - 1 * s;
            canvas->AnnularSector(gx, gy, 5.2f * s, 6.6f * s, 0, 360, 0xffffff, .8f);
            canvas->Line(gx + 4.6f * s, gy + 4.6f * s, gx + 8.6f * s, gy + 8.6f * s, 0xffffff, .8f, 1.5f * s);
            const auto add = R(layout.add);
            const float cx = add.x + add.width / 2, cy = add.y + add.height / 2, arm = 7.5f * s;
            if (hoverKind == HitKind::Add) canvas->Rounded(add, add.width / 2, 0xffffff, .10f);
            canvas->Line(cx - arm, cy, cx + arm, cy, 0xffffff, .92f, 1.7f * s);
            canvas->Line(cx, cy - arm, cx, cy + arm, 0xffffff, .92f, 1.7f * s);
            if (!searchShown || SearchInPanel() || (renaming && renameTile)) {
                // Where PositionSearch puts the EDIT, so its raster lands 1:1.
                const WidgetRect text = compact ? WidgetRect{field.x, field.y + 2 * s, field.width, field.height - 4 * s}
                                                : WidgetRect{field.x + 40 * s, field.y + 8 * s, field.width - 56 * s, field.height - 16 * s};
                if (searchFrame && typed.empty() && !renaming) canvas->ImageFrame(searchFrame, text);
                else if (renaming && !query.empty()) canvas->Text(OrganizerWide(query), text, 14 * s, 0xffffff, .95f);
                else if (!typed.empty()) canvas->Text(typed, text, 14 * s, 0xffffff, .95f);
                else canvas->Text(L"搜索桌面项目", text, 14 * s, 0xffffff, .55f);
            }
        }
        // Items; the dragged one floats above the rest.
        const auto items = PageEntries();
        if (items.empty())
            canvas->Text(collectionView ? L"这个收纳还是空的" : query.empty() ? L"桌面上没有项目" : L"没有找到匹配的项目",
                         R(layout.grid), 15 * s, 0xffffff, .6f, false, WidgetTextAlign::Center);
        int floating = -1;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (IsDragged(items[i])) { floating = static_cast<int>(i); continue; }
            DrawItem(items[i], i, layout, s, false);
        }
        // Pager.
        const int pages = Pages();
        if (pages > 1) {
            const auto chevron = [&](WidgetRect r, bool left, float alpha) {
                r = R(r);
                const float x = r.x + r.width / 2, y = r.y + r.height / 2, d = (left ? 4.f : -4.f) * s;
                canvas->Line(x + d, y - 7 * s, x - d, y, 0xffffff, alpha, 1.6f * s);
                canvas->Line(x - d, y, x + d, y + 7 * s, 0xffffff, alpha, 1.6f * s);
            };
            chevron(layout.previous, true, page > 0 ? (hoverKind == HitKind::Previous ? 1.f : .7f) : .2f);
            chevron(layout.next, false, page + 1 < pages ? (hoverKind == HitKind::Next ? 1.f : .7f) : .2f);
            canvas->Text(std::to_wstring(page + 1) + L" / " + std::to_wstring(pages), R(layout.page), 14 * s, 0xffffff, .8f,
                         false, WidgetTextAlign::Center);
        }
        // The ghost window paints the dragged item; the in-panel copy is only a
        // fallback for a ghost that could not be created.
        if (floating >= 0 && !ghost)
            DrawItem(items[static_cast<std::size_t>(floating)], static_cast<std::size_t>(floating), layout, s, true);
    }
    void DrawItem(const dock::Entry& entry, std::size_t index, const Layout& layout, float s, bool floating) {
        const auto cell = Cell(layout, static_cast<int>(index));
        auto& m = motion[entry.path];
        float x = m.placed ? m.x.value : cell.x, y = m.placed ? m.y.value : cell.y;
        if (floating) { x = dragX - grabX; y = dragY - grabY; }
        const float base = IconSide(cell);
        const float icon = base * (m.placed ? m.scale.value : 1.f);
        const float top = y + (cell.height - base - LabelHeight - 6) / 2 - (icon - base);
        const WidgetRect localIcon{x + (cell.width - icon) / 2, top, icon, icon};
        const auto iconRect = floating ? localIcon : MapCollectionRect(localIcon);
        const float alpha = floating ? 1.f : CollectionAlpha();
        if (entry.collection) {
            // A launchpad collection uses the add button's frosted-glass tile
            // language instead of a file icon, so it never looks like a folder
            // that opens Explorer.
            canvas->Rounded(iconRect, icon * .22f * s, 0xffffff, entry.collectionId == dropCollectionId ? .18f : .04f);
            const float inset = iconRect.width * .15f, gap = iconRect.width * .08f;
            const float side = (iconRect.width - inset * 2 - gap) / 2;
            const auto count = entry.previewPaths.empty() ? 4 : std::min<std::size_t>(4, entry.previewPaths.size());
            for (std::size_t i = 0; i < count; ++i) {
                const WidgetRect slot{iconRect.x + inset + (i % 2) * (side + gap),
                                      iconRect.y + inset + (i / 2) * (side + gap), side, side};
                const auto image = !entry.previewPaths.empty() && ports.icon ? ports.icon(entry.previewPaths[i]) : nullptr;
                if (image) canvas->ImageBitmap(image, slot, alpha);
                else canvas->Rounded(slot, side * .25f, 0xffffff, .42f * alpha);
            }
        } else if (const auto image = ports.icon ? ports.icon(entry.path) : nullptr) {
            canvas->ImageBitmap(image, iconRect, alpha);
        } else {
            canvas->Rounded(iconRect, icon * .22f * s, 0xffffff, .14f * alpha);
        }
        if (floating || (renaming && renameTile && entry.collection && entry.collectionId == renameId)) return;
        const auto label = FitText(*canvas, OrganizerWide(entry.name), (cell.width - 16) * s, LabelSize * s);
        canvas->Text(label, MapCollectionRect({x + 4, top + icon + 6, cell.width - 8, LabelHeight}), LabelSize * s,
                     0xffffff, .95f * alpha, false, WidgetTextAlign::Center);
    }
    // Renders the full-size content, then composes it.
    bool Paint() {
        if (!window || (!visible && !closing) || !canvas) return false;
        if (closing) return Compose(); // Async icon updates must not replace the frozen closing frame.
        // While the glass plays the opening, its content stays the picture taken at
        // Show: a re-render (an icon that finished loading, a hover) re-uploads the
        // whole panel to the glass mid-motion and the icons visibly shake. It
        // lands once the panel has settled.
        if (stage && presenceMoving && openingPainted) { dirty = true; return Compose(); }
        if (!canvas->Begin(width, height, dpi)) return false;
        DrawContent();
        if (!canvas->Finish(bitmap) || !surface.Upload(bitmap)) return false;
        ++bitmapGeneration;
        animationSource.reset();
        return Compose();
    }
    // Resample one retained frame, so icons and text move with the outer frame.
    bool Compose() {
        if (!window || (!visible && !closing) || !surface.Width()) return false;
        if (pendingClose && closeStage) {
            // Closed mid-opening: start from the state the glass shows next refresh.
            pendingClose = false;
            const double ahead = timelineSprings.empty() ? 0 : RefreshMs();
            if (ahead > 0) TimelineState(ahead, spreadX, spreadY, presence);
            AimClose();
            BuildTimeline(true, ahead);
        }
        const float s = Scale();
        const RECT frame = Frame();
        const POINT origin{frame.left, frame.top};
        const SIZE size{frame.right - frame.left, frame.bottom - frame.top};
        const float alpha = std::clamp(presence.value * 1.6f, 0.f, 1.f);
        // Never fully transparent while open: DWM skips composing an invisible
        // layered window, which would move its first-composition stall into the motion.
        const BYTE opacity = static_cast<BYTE>(std::max(visible ? 1L : 0L, std::lround(alpha * 255)));
        // The frame (and its glass) spreads per axis; the content keeps its
        // proportions at the smaller scale, centred in the frame.
        const float frameWidth = width * SpreadX(), frameHeight = height * SpreadY();
        const float offsetX = (frameWidth - width * s) / 2, offsetY = (frameHeight - height * s) / 2;
        const bool full = std::abs(SpreadX() - 1) < .0005f && std::abs(SpreadY() - 1) < .0005f;
        // Staged opening: the glass presents the panel and its content as one
        // picture (see `stage`). The window holds still at its final rect with
        // a near-invisible foreground; it takes over once the opening settles,
        // while the glass keeps drawing the same content for one more frame so
        // the two never leave a frame with neither.
        // The glass stays the only picture while open, too: handing back to the
        // foreground drew the content twice for a frame (a visible flash), so
        // the foreground stays near-invisible and only receives the pointer.
        // Should the glass drop out, the foreground shows again at once.
        if (stage && material.State() != WidgetMaterialState::Ready) {
            stage = stageForeground = false;
            timeline.clear();
            timelineSprings.clear();
        }
        const bool staging = stage && visible && !closing;
        bool glassContent = staging || stageHandoff;
        if (staging) {
            const POINT at{final.left, final.top};
            const SIZE finalSize{final.right - final.left, final.bottom - final.top};
            // Recompose (still near-invisible) only when the pixels changed, so
            // the pointer hit-tests the current content.
            if (!stageForeground || composedGeneration != bitmapGeneration) {
                if (!ComposeHitSurface(at, finalSize)) return false;
                composedGeneration = bitmapGeneration;
            }
            stageForeground = true;
            if (!presenceMoving && !timeline.empty()) { timeline.clear(); timelineSprings.clear(); }
        } else if (closeStage) {  // from Hide()'s own last paint on
            glassContent = true;
            // Frame 0: the glass draws the content too; from frame 1 on only the glass.
            if (closeStageFrames++ == 1) {
                const POINT at{final.left, final.top};
                const SIZE finalSize{final.right - final.left, final.bottom - final.top};
                // Closing: let the pointer through to what lies beneath.
                if (!surface.Compose(window, at, finalSize, {}, 1)) return false;
            }
        } else if (stageHandoff) {
            stageHandoff = glassContent = false;  // the foreground already shows it
        } else if (!full) {
            if (!animationSource) animationSource = std::make_shared<const WidgetBitmap>(bitmap);
            if (!animationCanvas) animationCanvas = std::make_unique<WidgetCanvas>();
            // One fixed-size canvas for the whole motion (a frame-sized one was
            // reallocated every frame and halved the frame rate); only its
            // top-left frame-sized part is composed.
            if (!animationCanvas->Begin(width * MaxSpread, height * MaxSpread, dpi) ||
                !animationCanvas->ImageFrame(animationSource, {offsetX, offsetY, width * s, height * s}) ||
                !animationCanvas->Finish(animationBitmap) || !animationSurface.Upload(animationBitmap) ||
                !animationSurface.Compose(window, origin, size, {}, opacity)) return false;
        } else if (!surface.Compose(window, origin, size, {}, opacity)) return false;
        LumeMaterialParameters p;
        std::memset(&p, 0, sizeof(p));
        p.size = sizeof(p);
        p.owner = window;
        p.frosted = frosted;
        // Frosted: exactly the desktop widgets' material profile, so the
        // launchpad and the widgets cannot drift apart. Liquid: the original
        // Dock profile, because the liquid shader derives refraction from it.
        p.opticalProfile = frosted ? 1 : 3;
        p.lightForeground = 1;
        p.cornerRadius = CornerRadius * std::min(s, 1.f);
        p.reducedMotion = reducedMotion;
        p.opacityEnabled = 1;
        p.opacity = preparing ? 0.f : alpha;
        // The glass itself does not move while an item is dragged, and the
        // pointer's contact point would make the engine re-render refraction on
        // every gap update. Keep the last material for the gesture -- except
        // when the glass also draws the content: then the grid moving apart must
        // reach it, so only the contact point stays put.
        if (dragged >= 0 && !glassContent) return true;
        if (dragged >= 0) { p.contactX = lastMaterial.contactX; p.contactY = lastMaterial.contactY; }
        else MaterialContact(window, frameWidth, frameHeight, dpi, p.contactX, p.contactY);
        const auto layout = CurrentLayout();
        const auto& q = layout.search;
        const auto& a = layout.add;
        if (!collectionView && !closing) {
            flatPanelCount = 2;
            flatPanels[0] = {q.x, q.y, q.width, q.height, q.height / 2};
            flatPanels[1] = {a.x, a.y, a.width, a.height, -1};
            // A collection tile uses the same frosted panel material as the
            // search field and add button, instead of a plain canvas fill.
            const auto entries = PageEntries();
            for (std::size_t i = 0; i < entries.size() && flatPanelCount < 8; ++i) {
                if (!entries[i].collection) continue;
                const auto cell = Cell(layout, static_cast<int>(i));
                const float base = IconSide(cell);
                const float top = cell.y + (cell.height - base - LabelHeight - 6) / 2;
                const float left = cell.x + (cell.width - base) / 2;
                flatPanels[flatPanelCount++] = {left, top, base, base, base * .22f};
            }
        }
        p.panelCount = flatPanelCount;
        for (unsigned i = 0; i < p.panelCount; ++i) {
            const auto& r = flatPanels[i];
            p.panels[i] = {offsetX + r.x * s, offsetY + r.y * s, r.width * s, r.height * s, r.radius < 0 ? r.radius : r.radius * s};
        }
        if (glassContent) {
            // The lens and content, in DIP from the final window's client origin;
            // settled, exactly the window and its foreground pixels.
            const bool settled = !presenceMoving && !closing;
            const float sink = settled ? 0.f : Sink(SpreadX());
            p.lensOverride = 1;
            p.lensX = settled ? 0.f : (width - frameWidth) / 2;
            p.lensY = settled ? 0.f : (height - frameHeight) / 2 + sink;
            p.lensWidth = settled ? width : frameWidth;
            p.lensHeight = settled ? height : frameHeight;
            p.contentEnabled = 1;
            p.contentX = settled ? 0.f : offsetX;
            p.contentY = settled ? 0.f : offsetY;
            p.contentWidth = settled ? width : width * s;
            p.contentHeight = settled ? height : height * s;
            p.contentPixels = bitmap.pixels.data();
            p.contentPixelWidth = bitmap.width;
            p.contentPixelHeight = bitmap.height;
            p.contentGeneration = bitmapGeneration;
            if (!timeline.empty()) {
                // The glass follows the timeline; these fields stay at its first
                // frame, so this call repeats unchanged instead of every frame.
                const auto& k = timeline.front();
                p.lensX = k.lensX; p.lensY = k.lensY; p.lensWidth = k.lensWidth; p.lensHeight = k.lensHeight;
                p.contentX = k.contentX; p.contentY = k.contentY; p.contentWidth = k.contentWidth; p.contentHeight = k.contentHeight;
                p.opacity = k.opacity;
                p.cornerRadius = k.cornerRadius;
                p.panelCount = k.panelCount;
                std::copy(k.panels, k.panels + 8, p.panels);
                p.timeline = timeline.data();
                p.timelineCount = static_cast<std::uint32_t>(timeline.size());
                p.timelineStepMs = TimelineStepMs;
                p.timelineStart = timelineStart;
            }
        }
        SubmitMaterial(material, p, lastMaterial);
        return true;
    }
    void Turn(int delta) {
        const int next = std::clamp(page + delta, 0, Pages() - 1);
        if (next == page) return;
        page = next;
        hover = -1;
        // The new page slides in from the side it came from.
        const auto layout = CurrentLayout();
        const auto items = PageEntries();
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (IsDragged(items[i])) continue;
            const auto cell = Cell(layout, static_cast<int>(i));
            auto& m = motion[items[i].path];
            m.x = {cell.x + static_cast<float>(delta) * layout.grid.width * .35f, 0};
            m.y = {cell.y, 0};
            m.scale = {reducedMotion ? 1.f : .9f, 0};
            m.placed = true;
        }
        Animate();
        Paint();
    }
    // While dragging, resting near a grid edge turns the page.
    void EdgeTurn() {
        const auto layout = CurrentLayout();
        const int direction = dragX < layout.grid.x + EdgeBand ? -1 : dragX > layout.grid.x + layout.grid.width - EdgeBand ? 1 : 0;
        if (direction != edgeDirection) { edgeDirection = direction; edgeSince = GetTickCount64(); return; }
        if (!direction || GetTickCount64() - edgeSince < EdgeDwellMs) return;
        edgeSince = GetTickCount64();
        const int before = page;
        Turn(direction);
        if (page != before) UpdateDrop();
    }
    void UpdateDrop() {
        const auto layout = CurrentLayout();
        const auto entries = PageEntries();
        std::string under;
        if (!collectionView && dragged >= 0 && static_cast<std::size_t>(dragged) < shown.size() &&
            !shown[static_cast<std::size_t>(dragged)].collection) {
            const auto hit = Hit(layout, entries.size(), page, Pages(), dragX, dragY, false);
            if (hit.kind == HitKind::Item && hit.index >= 0 &&
                static_cast<std::size_t>(hit.index) < entries.size() && entries[static_cast<std::size_t>(hit.index)].collection)
                under = entries[static_cast<std::size_t>(hit.index)].collectionId;
        }
        if (!under.empty()) {
            // Do not open an insertion gap for a collection target. The gap
            // moves the collection tile away from the pointer, so the release
            // would miss it; keep the grid still and let the tile itself show
            // the drop highlight.
            const bool changed = drop != dragged || dropCollectionId != under;
            drop = dragged;
            if (changed) {
                dropCollectionId = under;
                Animate();
                Paint();
            }
            return;
        }
        if (!dropCollectionId.empty()) {
            dropCollectionId.clear();
            Paint();
        }
        int best = 0;
        float bestDistance = 1e9f;
        for (int i = 0; i < PageSize(); ++i) {
            const auto cell = Cell(layout, i);
            const float dx = cell.x + cell.width / 2 - dragX, dy = cell.y + cell.height / 2 - dragY;
            const float distance = dx * dx + dy * dy;
            if (distance < bestDistance) { bestDistance = distance; best = i; }
        }
        const int next = std::min(page * PageSize() + best, static_cast<int>(shown.size()) - 1);
        if (next != drop) { drop = next; Animate(); }
    }
    void BeginDrag(float x, float y) {
        dragged = pressed;
        drop = pressed;
        const auto layout = CurrentLayout();
        const auto cell = Cell(layout, pressed % PageSize());
        auto& m = motion[shown[static_cast<std::size_t>(pressed)].path];
        grabX = x - (m.placed ? m.x.value : cell.x);
        grabY = y - (m.placed ? m.y.value : cell.y);
        dragX = x;
        dragY = y;
        edgeDirection = 0;
        // The pointer's icon takes over from the grid; the panel only keeps the
        // insertion gap, so it no longer renders the floating copy.
        if (PresentGhost()) MoveGhost();
        Animate();
        Paint();
    }
    bool DragOutOfCollection() {
        if (dragged < 0 || !ports.removeFromCollection) return false;
        const auto entry = shown[static_cast<std::size_t>(dragged)];
        const auto oldCell = Cell(CurrentLayout(), dragged % PageSize());
        const float oldLeft = dragX - grabX, oldTop = dragY - grabY;
        // Retire indices before a callback refreshes/saves the collection.
        const int previousDrag = dragged, previousDrop = drop;
        dragged = drop = -1;
        if (!ports.removeFromCollection(entry)) {
            dragged = previousDrag; drop = previousDrop;
            return false;
        }
        FinishCollectionClose();
        const auto found = std::find_if(shown.begin(), shown.end(), [&](const auto& value) { return value.path == entry.path; });
        if (found == shown.end()) { pressed = -1; HideGhost(); if (GetCapture() == window) ReleaseCapture(); return true; }
        dragged = pressed = drop = static_cast<int>(found - shown.begin());
        page = dragged / PageSize();
        const auto cell = Cell(CurrentLayout(), dragged % PageSize());
        grabX = dragX - (oldLeft + (oldCell.width - cell.width) / 2);
        grabY = dragY - (oldTop + (oldCell.height - cell.height) / 2);
        dropCollectionId.clear(); edgeDirection = 0;
        UpdateDrop(); Animate();
        return true;
    }
    void EndDrag(bool commit) {
        if (dragged < 0) return;
        HideGhost();
        dirty = true;
        const int from = dragged, to = drop;
        const auto source = shown[static_cast<std::size_t>(from)];
        const auto targetCollection = dropCollectionId;
        const auto preview = Preview();
        // Land from where it was released.
        auto& m = motion[shown[static_cast<std::size_t>(from)].path];
        m.x = {dragX - grabX, 0};
        m.y = {dragY - grabY, 0};
        dragged = drop = -1;
        const auto entries = PageEntries();
        const auto under = Hit(CurrentLayout(), entries.size(), page, Pages(), dragX, dragY, false);
        if (commit && !collectionView && !targetCollection.empty() && !source.collection && ports.moveIntoCollection) {
            ports.moveIntoCollection(source, targetCollection);
            dropCollectionId.clear(); Animate();
            return;
        }
        if (commit && !collectionView && under.kind == HitKind::Item && under.index >= 0 &&
            static_cast<std::size_t>(under.index) < entries.size() &&
            static_cast<std::size_t>(from) < shown.size()) {
            const auto& target = entries[static_cast<std::size_t>(under.index)];
            if (!source.collection && target.collection && ports.moveIntoCollection &&
                ports.moveIntoCollection(source, target.collectionId)) {
                dropCollectionId.clear();
                Animate();
                return;
            }
        }
        if (commit && to >= 0 && to != from) {
            std::vector<std::string> order;
            for (const auto& entry : preview) order.push_back(entry.path);
            // Adopt the arrangement locally; the session saves it.
            std::vector<dock::Entry> arranged;
            for (const auto& path : order)
                for (const auto& entry : shown)
                    if (entry.path == path) { arranged.push_back(entry); break; }
            all = arranged;
            Filter();
            if (ports.arrange) ports.arrange(order);
        }
        dropCollectionId.clear();
        Animate();
    }
    // Native search field: an owned layered EDIT composited above the glass,
    // keeping selection, clipboard and IME (as the organizer search does).
    bool PrepareSearch() {
        if (search) {
            preparingSearch = true;
            SetWindowTextW(search, OrganizerWide(query).c_str());
            preparingSearch = false;
            SetWindowLongPtrW(search, GWL_STYLE, GetWindowLongPtrW(search, GWL_STYLE) & ~(ES_CENTER | ES_RIGHT));
            searchDpi = 0;
            PositionSearch();
            return PresentSearch();
        }
        search = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"EDIT", L"", WS_POPUP | ES_AUTOHSCROLL, 0, 0, 1, 1,
                                 window, nullptr, instance, nullptr);
        if (!search) return false;
        if (!direct_write_edit::Attach(search)) { DestroyWindow(search); search = nullptr; return false; }
        SetPropW(search, L"LumeLaunchpadSearch", reinterpret_cast<HANDLE>(this));
        searchProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(search, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SearchProcedure)));
        SendMessageW(search, EM_SETLIMITTEXT, 120, 0);
        PositionSearch();
        if (!PresentSearch()) { DestroySearch(); return false; }
        return true;
    }
    bool BeginSearch() {
        if (collectionView) return true;
        if (searchShown) return true;
        if (searchMasked && search) {
            // Already shown and focused: reveal its pixels and drop the panel's copy.
            searchMasked = false;
            if (!PresentSearch()) return false;
            searchShown = true;
            if (GetFocus() != search) SetFocus(search);
            if (!Paint()) { searchShown = false; ShowWindow(search, SW_HIDE); return false; }
            return true;
        }
        if (!search && !PrepareSearch()) return false;
        if (!typed.empty()) {
            preparingSearch = true;
            const bool set = SetWindowTextW(search, typed.c_str()) != FALSE;
            preparingSearch = false;
            if (!set) return false;
            query = OrganizerUtf8(typed);
            page = 0;
            Filter();
            if (!PresentSearch()) return false;
        }
        // Use the same native raster for the final animation frame and the
        // live EDIT. It takes over when the panel settles, even during hover.
        typed.clear();
        if (!PresentSearch()) return false;
        ShowWindow(search, SW_SHOW);
        SetFocus(search);
        searchShown = true;
        if (!Paint()) { searchShown = searchMasked = false; ShowWindow(search, SW_HIDE); return false; }
        const auto length = GetWindowTextLengthW(search);
        SendMessageW(search, EM_SETSEL, length, length);
        return true;
    }
    void FreezeSearch() {
        if (!search || (!searchShown && !searchMasked)) return;
        searchMasked = false;
        PresentSearch();
        searchShown = searchMasked = false;
        ShowWindow(search, SW_HIDE);
        KillTimer(search, CaretTimer);
        caretInterval = 0;
    }
    void DestroySearch() {
        const auto old = search;
        search = nullptr;
        if (old && IsWindow(old)) { KillTimer(old, CaretTimer); DestroyWindow(old); }
        if (font) { DeleteObject(font); font = nullptr; }
        searchDpi = 0;
        caretInterval = 0;
        searchPaintPending = false;
        searchPainting = false;
        searchShown = searchMasked = false;
        preparingSearch = false;
        searchBlack.reset();
        searchWhite.reset();
        searchWidth = searchHeight = 0;
        searchFrame.reset();
        composing = false;
    }
    void PositionSearch() {
        if (!search || !window) return;
        const auto layout = CurrentLayout();
        const float scale = static_cast<float>(dpi) / 96;
        const WidgetRect r = compact ? WidgetRect{layout.search.x, layout.search.y + 2, layout.search.width, layout.search.height - 4}
                                    : WidgetRect{layout.search.x + 40, layout.search.y + 8, layout.search.width - 56, layout.search.height - 16};
        SetWindowPos(search, nullptr, final.left + static_cast<int>(r.x * scale), final.top + static_cast<int>(r.y * scale),
                     static_cast<int>(r.width * scale), static_cast<int>(r.height * scale), SWP_NOACTIVATE | SWP_NOZORDER);
        if (searchDpi != dpi) {
            searchDpi = dpi;
            const auto replacement = CreateFontW(-MulDiv(compact ? 17 : 14, static_cast<int>(dpi), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                                 DEFAULT_PITCH, ui_font::Family(L"搜索"));
            if (replacement) {
                SendMessageW(search, WM_SETFONT, reinterpret_cast<WPARAM>(replacement), FALSE);
                if (font) DeleteObject(font);
                font = replacement;
            }
        }
        if (searchShown) RequestSearchPaint();
    }
    void RequestSearchPaint(bool resetCaret = false) {
        if (!search || (!searchShown && !searchMasked) || searchPainting) return;
        if (resetCaret) caretVisible = true;
        if (!searchPaintPending) searchPaintPending = PostMessageW(search, PresentSearchMessage, 0, 0) != FALSE;
    }
    // The search field's text (placeholder, typing, caret) is always drawn by
    // the panel itself, in the same picture as the field and the add button.
    // The live EDIT stays blank and only takes input. Handing its pixels over
    // to the EDIT window once the opening settled made the field change subtly
    // apart from the panel. Renaming a collection still shows the EDIT.
    bool SearchInPanel() const { return !collectionView && !renaming; }
    bool PresentSearch() {
        if (!search || searchPainting) return false;
        RECT rect{};
        GetClientRect(search, &rect);
        const int w = rect.right, h = rect.bottom;
        if (w <= 0 || h <= 0) return false;
        if (!searchBlack || !searchWhite || searchWidth != w || searchHeight != h) {
            searchBlack = std::make_unique<Dib>(w, h);
            searchWhite = std::make_unique<Dib>(w, h);
            if (!searchBlack->pixels || !searchWhite->pixels) return false;
            searchWidth = w;
            searchHeight = h;
        }
        auto& black = *searchBlack;
        auto& white = *searchWhite;
        const bool focused = GetFocus() == search && !composing;
        const auto blink = GetCaretBlinkTime();
        const UINT interval = focused && blink && blink != INFINITE ? std::max(250u, blink) : 0;
        if (interval != caretInterval) {
            KillTimer(search, CaretTimer);
            caretInterval = interval;
            caretVisible = true;
            if (interval) SetTimer(search, CaretTimer, interval, nullptr);
        }
        searchPainting = true;
        // Foreground alpha is derived from black and white WM_PRINTCLIENT passes.
        for (int pass = 0; pass < 2; ++pass) {
            auto& dib = pass ? white : black;
            matte = pass ? RGB(255, 255, 255) : RGB(0, 0, 0);
            FillRect(dib.dc, &rect, static_cast<HBRUSH>(GetStockObject(pass ? WHITE_BRUSH : BLACK_BRUSH)));
            CallWindowProcW(searchProcedure, search, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dib.dc), PRF_CLIENT | PRF_ERASEBKGND);
            if (!renaming && !GetWindowTextLengthW(search) && !composing) {
                RECT text{};
                SendMessageW(search, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&text));
                text.top = rect.top;
                text.bottom = rect.bottom;
                const BYTE channel = pass ? 255 : 140;
                direct_write::Style style;
                style.size = 14.f * static_cast<float>(dpi) / 96.f;
                direct_write::Draw(dib.dc, L"搜索桌面项目", text, RGB(channel, channel, channel), style);
            }
            GUITHREADINFO caret{};
            caret.cbSize = sizeof(caret);
            if (focused && caretVisible && GetGUIThreadInfo(GetCurrentThreadId(), &caret) && caret.hwndCaret == search)
                FillRect(dib.dc, &caret.rcCaret, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        }
        searchPainting = false;
        GdiFlush();
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const auto index = static_cast<std::size_t>(y) * w + x;
                const auto b = black.pixels[index], wh = white.pixels[index];
                int alpha = 0;
                for (const int shift : {0, 8, 16})
                    alpha = std::max(alpha, 255 - static_cast<int>((wh >> shift) & 255) + static_cast<int>((b >> shift) & 255));
                alpha = std::clamp(alpha, 0, 255);
                auto pixel = static_cast<std::uint32_t>(alpha) << 24;
                for (const int shift : {0, 8, 16})
                    pixel |= static_cast<std::uint32_t>(std::min(static_cast<int>((b >> shift) & 255), alpha)) << shift;
                // Empty pixels must still accept mouse selection and focus.
                if (!(pixel >> 24)) pixel = 0x01000000u;
                black.pixels[index] = pixel;
            }
        POINT origin{};
        SIZE size{w, h};
        auto frame = std::make_shared<WidgetBitmap>();
        frame->width = static_cast<unsigned>(w); frame->height = static_cast<unsigned>(h);
        frame->pixels.assign(black.pixels, black.pixels + static_cast<std::size_t>(w) * h);
        if (searchMasked || SearchInPanel()) {
            // The panel draws this raster; the EDIT stays blank but hit-testable.
            if (!searchFrame || searchFrame->pixels != frame->pixels) {
                dirty = true;
                // At rest nothing else repaints: one frame of the clock does.
                if (visible && !presenceMoving && window) Animate();
            }
            std::fill(black.pixels, black.pixels + static_cast<std::size_t>(w) * h, 0x01000000u);
        }
        searchFrame = std::move(frame);
        presentedText = SearchText();
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        return UpdateLayeredWindow(search, nullptr, nullptr, &size, black.dc, &origin, 0, &blend, ULW_ALPHA) != FALSE;
    }
    std::wstring SearchText() const {
        std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(search)) + 1, L'\0');
        text.resize(static_cast<std::size_t>(GetWindowTextW(search, text.data(), static_cast<int>(text.size()))));
        return text;
    }
    void OpenEntry(const dock::Entry& entry) {
        const auto value = entry; // callbacks can replace the current entries
        if (!value.collection) Hide();
        if (ports.open) Modal([&] { ports.open(value); });
    }
    void OpenFirst() {
        const auto items = PageEntries();
        if (!items.empty()) OpenEntry(items.front());
    }
    static LRESULT CALLBACK SearchProcedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetPropW(hwnd, L"LumeLaunchpadSearch"));
        if (!self) return DefWindowProcW(hwnd, message, w, l);
        const auto original = self->searchProcedure;
        if (message == PresentSearchMessage) {
            self->searchPaintPending = false;
            // Still blank under the opening glide: focus and caret changes wait for
            // the hand-off, which presents anyway; only typed text redraws now.
            if (self->searchMasked && self->presenceMoving && self->SearchText() == self->presentedText) return 0;
            if (self->searchShown || self->searchMasked) self->PresentSearch();
            return 0;
        }
        if (message == WM_TIMER && w == CaretTimer) { self->caretVisible = !self->caretVisible; self->RequestSearchPaint(); return 0; }
        if (message == WM_PAINT) { PAINTSTRUCT paint{}; BeginPaint(hwnd, &paint); EndPaint(hwnd, &paint); self->RequestSearchPaint(); return 0; }
        if (message == WM_ERASEBKGND) return 1;
        // Enter/Escape were handled on key-down. Their queued characters must
        // not edit the restored search text after ending an inline rename.
        if (message == WM_CHAR && (w == L'\r' || w == VK_ESCAPE)) return 0;
        if (message == WM_MOUSEWHEEL) { self->Turn(GET_WHEEL_DELTA_WPARAM(w) > 0 ? -1 : 1); return 0; }
        if (message == WM_IME_STARTCOMPOSITION) self->composing = true;
        if (message == WM_IME_ENDCOMPOSITION) self->composing = false;
        if (message == WM_KEYDOWN && !self->composing) {
            if (self->renaming) {
                if (w == VK_ESCAPE) { self->EndRename(false); return 0; }
                if (w == VK_RETURN) { self->EndRename(true); return 0; }
            } else {
                if (w == VK_ESCAPE) { self->Hide(); return 0; }
                if (w == VK_RETURN) { self->OpenFirst(); return 0; }
                if (w == VK_PRIOR || w == VK_NEXT) { self->Turn(w == VK_PRIOR ? -1 : 1); return 0; }
            }
        }
        const auto result = CallWindowProcW(original, hwnd, message, w, l);
        if (message == WM_NCDESTROY) { KillTimer(hwnd, CaretTimer); RemovePropW(hwnd, L"LumeLaunchpadSearch"); return result; }
        if (self->search != hwnd) return result;
        switch (message) {
        case WM_CHAR: case WM_SETTEXT: case WM_PASTE: case WM_CUT: case WM_CLEAR: case WM_UNDO:
        case EM_REPLACESEL: case WM_IME_COMPOSITION: case WM_IME_ENDCOMPOSITION: {
            if (self->renaming) break;
            const auto next = OrganizerUtf8(self->SearchText());
            if (!self->preparingSearch && next != self->query) { self->query = next; self->page = 0; self->Filter(); self->Animate(); self->Paint(); }
            break;
        }
        default: break;
        }
        switch (message) {
        case WM_SETTEXT: case WM_SETFONT: case WM_SIZE: case WM_SETFOCUS: case WM_KILLFOCUS:
        case WM_CHAR: case WM_KEYDOWN: case WM_KEYUP: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case EM_SETSEL: case EM_REPLACESEL: case WM_CUT: case WM_PASTE: case WM_CLEAR: case WM_UNDO:
        case WM_IME_STARTCOMPOSITION: case WM_IME_COMPOSITION: case WM_IME_ENDCOMPOSITION:
            self->RequestSearchPaint(true);
            break;
        case WM_MOUSEMOVE: if (w & MK_LBUTTON) self->RequestSearchPaint(true); break;
        default: break;
        }
        return result;
    }
    // Pointer in final-layout DIPs.
    float LayoutX(LPARAM l) const { return static_cast<float>(GET_X_LPARAM(l)) * 96 / dpi / Scale(); }
    float LayoutY(LPARAM l) const { return static_cast<float>(GET_Y_LPARAM(l)) * 96 / dpi / Scale(); }
    HitResult HitAt(LPARAM data) const {
        if (collectionClosing || (collectionView && collectionExpand.value < .95f)) return {};
        return Hit(CurrentLayout(), PageEntries().size(), page, Pages(), LayoutX(data), LayoutY(data), collectionView);
    }
    void Modal(const std::function<void()>& action) {
        modal = true;
        action();
        modal = false;
        if (visible) SetFocus(collectionView || (!searchShown && !searchMasked) ? window : search);
    }
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->window = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, w, l);
        switch (message) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: { PAINTSTRUCT paint{}; BeginPaint(hwnd, &paint); EndPaint(hwnd, &paint); return 0; }
        case WM_ACTIVATE:
            if (self->compact && LOWORD(w) == WA_INACTIVE && self->visible && !self->modal && !self->renaming && self->dragged < 0) {
                const auto next = reinterpret_cast<HWND>(l);
                wchar_t name[64]{}; GetClassNameW(next, name, 64);
                if (GetWindow(next, GW_OWNER) != hwnd && wcscmp(name, lume::window_ids::WidgetMenuClass) != 0) self->Hide();
            }
            break;
        case WM_ACTIVATEAPP:
            // Another program took the foreground (the desktop included).
            if (!w && self->visible && !self->modal) self->Hide();
            return 0;
        case WM_TIMER:
            if (w == SearchTimer) {
                KillTimer(hwnd, SearchTimer);
                if (self->visible && !self->searchShown && !self->collectionView && !self->presenceMoving) self->BeginSearch();
                return 0;
            }
            if (w == RefreshTimer) {
                KillTimer(hwnd, RefreshTimer);
                if (self->visible && self->presenceMoving) { SetTimer(hwnd, RefreshTimer, 100, nullptr); return 0; }
                if (self->visible && self->dragged < 0) { self->Refresh(); self->Animate(); self->Paint(); }
                return 0;
            }
            break;
        case RewarmMessage:
            if (!self->visible && !self->closing && self->window) {
                self->material.Detach();
                self->lastMaterial = {};
                self->Show(self->lastDock, self->frosted, true);
            }
            return 0;
        case FrameMessage: {
            self->clock.Consumed();
            if (!self->visible && !self->closing) { self->clock.Pause(); return 0; }
            // A refresh deferred by a drag lands as soon as it ends.
            if (self->refreshPending && self->dragged < 0 && !self->presenceMoving) {
                self->refreshPending = false; self->Refresh(); self->Animate(); self->dirty = true;
            }
            self->TraceFrame();
            const bool moving = self->Step();
            // Only item motion and drags need a re-render; the open/close spring
            // just recomposes the retained frame. A drag repaints only when the
            // gap moves: its icon lives in the ghost window. Other repaints
            // (icons that finished loading, desktop changes) wait until the
            // opening has settled: a full re-render costs a frame or two and
            // showed as a stop in the middle of the glide.
            const bool opening = self->presenceMoving && !self->closing;
            if (self->itemsMoving || self->collectionMoving || (self->dirty && !opening)) {
                if (self->Paint()) self->dirty = false;
            }
            else if (self->dragged < 0) self->Compose();
            if (self->closing && !moving && self->presence.value < .02f) { self->Finish(); return 0; }
            if (!moving && !self->closing) {
                self->ReportOpen();
                // One more frame: the glass drops the content it drew while staged.
                if (self->stageHandoff) return 0;
                self->clock.Pause();
                if (self->refreshPending && self->dragged < 0) {
                    // A refresh held back during the opening: its entries may move, so animate.
                    self->refreshPending = false; self->dirty = false;
                    self->Refresh(); self->Animate(); self->Paint();
                    return 0;
                }
                if (self->dirty) { self->dirty = false; self->Paint(); }
            }
            if (self->visible && !self->searchShown && !self->collectionView && !self->presenceMoving)
                SetTimer(hwnd, SearchTimer, static_cast<UINT>(SearchSettleSeconds * 1000), nullptr);
            return 0;
        }
        case WM_CHAR:
            // Typed before the search field appears (during the opening spring).
            if (self->searchMasked && self->search && w >= 32) { SendMessageW(self->search, WM_CHAR, w, l); return 0; }
            if (!self->searchShown && self->visible && w >= 32) { self->typed.push_back(static_cast<wchar_t>(w)); self->Paint(); }
            return 0;
        case WM_CTLCOLOREDIT:
            if (self->search && reinterpret_cast<HWND>(l) == self->search) {
                const auto dc = reinterpret_cast<HDC>(w);
                SetTextColor(dc, RGB(255, 255, 255));
                SetBkColor(dc, self->matte);
                SetDCBrushColor(dc, self->matte);
                return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
            }
            break;
        case WM_MOUSEMOVE: {
            if (!self->visible) return 0;
            const float x = self->LayoutX(l), y = self->LayoutY(l);
            if (self->pressed >= 0 && (w & MK_LBUTTON) && self->query.empty()) {
                const POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                const int threshold = MulDiv(static_cast<int>(DragThreshold), static_cast<int>(self->dpi), 96);
                if (self->dragged < 0 && (std::abs(point.x - self->pressPoint.x) > threshold ||
                                          std::abs(point.y - self->pressPoint.y) > threshold))
                    self->BeginDrag(x, y);
                if (self->dragged >= 0) {
                    self->dragX = x;
                    self->dragY = y;
                    POINT screen{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                    ClientToScreen(hwnd, &screen);
                    RECT client{};
                    GetClientRect(hwnd, &client);
                    const POINT inside{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                    // Leaving the panel towards the Dock hands the drop to it.
                    const bool wasOutside = self->outside;
                    self->outside = !PtInRect(&client, inside) && self->ports.dragOutside && self->ports.dragOutside(screen);
                    if (!wasOutside && self->outside) {
                        // The Dock owns the insertion gap now; collapse the panel's
                        // own slot so the grid does not keep a stale preview.
                        self->drop = self->dragged;
                        self->dropCollectionId.clear();
                        self->Animate();
                    }
                    // Leaving it sideways or upwards drags the file to other
                    // programs, as from the desktop; downwards stays for the Dock.
                    RECT frame{};
                    GetWindowRect(hwnd, &frame);
                    if (!self->compact && !self->outside && !PtInRect(&client, inside) && screen.y < frame.bottom) {
                        const auto entry = self->shown[static_cast<std::size_t>(self->dragged)];
                        const auto path = OrganizerWide(entry.path);
                        if (wasOutside && self->ports.leaveOutside) self->ports.leaveOutside();
                        self->EndDrag(false);
                        self->pressed = -1;
                        if (GetCapture() == hwnd) ReleaseCapture();
                        // Dragging out of a collection also returns the item to
                        // the flat grid before the shell drag starts.
                        if (self->collectionView && self->ports.removeFromCollection &&
                            !self->ports.removeFromCollection(entry)) { self->Paint(); return 0; }
                        // Close first so windows behind the panel can take the drop.
                        self->Hide();
                        DragPathsOut(hwnd, {path});
                        return 0;
                    }
                    if (wasOutside && !self->outside && self->ports.leaveOutside) self->ports.leaveOutside();
                    if (!self->compact && self->collectionView && !self->collectionClosing && !self->outside &&
                        PtInRect(&client, inside) && !Inside(self->CurrentLayout().grid, x, y)) {
                        if (self->DragOutOfCollection()) { self->Paint(); return 0; }
                    }
                    if (!self->outside) self->UpdateDrop();
                    // Only a crossing changes the panel; the pointer's icon lives
                    // in its own window and the gap springs repaint themselves.
                    if (wasOutside != self->outside) { self->dirty = true; self->Paint(); }
                    self->MoveGhost();
                    return 0;
                }
            }
            // Hover effects wait until the panel has opened; while it springs
            // open, a resting pointer must not force re-renders.
            const auto hit = self->started && !self->presenceMoving ? self->HitAt(l) : HitResult{};
            if (hit.kind != self->hoverKind || hit.index != self->hover) {
                self->hoverKind = hit.kind;
                self->hover = hit.index;
                self->Animate();
                self->Paint();
            }
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            if (self->dragged < 0) { self->hoverKind = HitKind::None; self->hover = -1; self->Animate(); self->Paint(); }
            return 0;
        case WM_MOUSEWHEEL: if (self->visible) self->Turn(GET_WHEEL_DELTA_WPARAM(w) > 0 ? -1 : 1); return 0;
        case WM_LBUTTONDOWN: {
            if (!self->visible) return 0;
            const auto hit = self->HitAt(l);
            self->pressed = hit.kind == HitKind::Item ? self->page * self->PageSize() + hit.index : -1;
            self->pressPoint = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (self->pressed >= 0) SetCapture(hwnd);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!self->visible) return 0;
            if (!self->compact && self->collectionView && !self->outside && self->dragged >= 0 && self->ports.removeFromCollection) {
                if (!Inside(self->CurrentLayout().grid, self->dragX, self->dragY)) {
                    const auto entry = self->shown[static_cast<std::size_t>(self->dragged)];
                    self->EndDrag(false);
                    if (GetCapture() == hwnd) ReleaseCapture();
                    if (self->ports.removeFromCollection(entry)) self->FinishCollectionClose();
                    self->Paint();
                    return 0;
                }
            }
            const int pressed = self->pressed;
            self->pressed = -1;
            if (self->dragged >= 0 && self->outside) {
                const auto entry = self->shown[static_cast<std::size_t>(self->dragged)];
                POINT screen{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ClientToScreen(hwnd, &screen);
                self->outside = false;
                self->EndDrag(false);
                if (GetCapture() == hwnd) ReleaseCapture();
                if (self->collectionView && self->ports.removeFromCollection &&
                    !self->ports.removeFromCollection(entry)) { self->Paint(); return 0; }
                if (self->collectionClosing) self->FinishCollectionClose();
                if (self->ports.dropOutside) self->ports.dropOutside(entry, screen);
                self->Paint();
                return 0;
            }
            if (self->dragged >= 0) {
                // End the drag before releasing capture, whose notification cancels drags.
                self->EndDrag(true);
                if (GetCapture() == hwnd) ReleaseCapture();
                self->Paint();
                return 0;
            }
            if (GetCapture() == hwnd) ReleaseCapture();
            const auto hit = self->HitAt(l);
            if (!self->compact && self->collectionView && hit.kind == HitKind::None &&
                self->ports.back) {
                self->Modal([&] { self->ports.back(); });
                return 0;
            }
            if (hit.kind == HitKind::Previous || hit.kind == HitKind::Next) self->Turn(hit.kind == HitKind::Previous ? -1 : 1);
            else if (hit.kind == HitKind::Back && self->ports.back) self->Modal([&] { self->ports.back(); });
            else if (hit.kind == HitKind::Item) {
                const auto items = self->PageEntries();
                const int index = self->page * self->PageSize() + hit.index;
                if (index == pressed && hit.index >= 0 && static_cast<std::size_t>(hit.index) < items.size())
                    self->OpenEntry(items[static_cast<std::size_t>(hit.index)]);
            } else if (hit.kind == HitKind::Add && self->ports.add) {
                const auto layout = self->CurrentLayout();
                const float scale = static_cast<float>(self->dpi) / 96;
                POINT point{static_cast<LONG>(layout.add.x * scale), static_cast<LONG>((layout.add.y + layout.add.height + 4) * scale)};
                ClientToScreen(hwnd, &point);
                self->Modal([&] { self->ports.add(point); });
            } else if (self->search) SetFocus(self->search);
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (self->dragged >= 0 && reinterpret_cast<HWND>(l) != hwnd) {
                if (self->outside && self->ports.leaveOutside) self->ports.leaveOutside();
                self->outside = false;
                self->EndDrag(false);
                self->Paint();
            }
            self->pressed = -1;
            return 0;
        case WM_RBUTTONUP: {
            if (!self->visible || self->dragged >= 0) return 0;
            const auto hit = self->HitAt(l);
            const auto items = self->PageEntries();
            if (hit.kind == HitKind::Item && hit.index >= 0 && static_cast<std::size_t>(hit.index) < items.size() && self->ports.context) {
                POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ClientToScreen(hwnd, &point);
                const auto entry = items[static_cast<std::size_t>(hit.index)];
                self->Modal([&] { self->ports.context(entry, point); });
            }
            else if (self->compact && self->ports.backgroundContext) {
                POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; ClientToScreen(hwnd, &point);
                self->Modal([&] { self->ports.backgroundContext(point); });
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (w == VK_ESCAPE) {
                if (self->dragged >= 0) { ReleaseCapture(); return 0; }
                if (self->collectionView && self->ports.back) self->Modal([&] { self->ports.back(); });
                else self->Hide();
                return 0;
            }
            if (w == VK_PRIOR || w == VK_LEFT) { self->Turn(-1); return 0; }
            if (w == VK_NEXT || w == VK_RIGHT) { self->Turn(1); return 0; }
            break;
        case DesktopChanged:
            // Coalesce bursts of shell notifications into one re-read.
            if (self->visible) SetTimer(hwnd, RefreshTimer, 300, nullptr);
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            if (self->visible) { self->Hide(); self->Finish(); }
            return 0;
        case WM_CLOSE: self->Hide(); return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (self->window == hwnd) self->window = nullptr;
            break;
        }
        return DefWindowProcW(hwnd, message, w, l);
    }
};

LaunchpadWindow::LaunchpadWindow(HINSTANCE instance, LaunchpadPorts ports, Presentation presentation)
    : impl_(std::make_unique<Impl>(instance, std::move(ports), presentation)) {}
LaunchpadWindow::~LaunchpadWindow() = default;
bool LaunchpadWindow::Show(RECT dock, bool frosted) { return impl_->Show(dock, frosted); }
bool LaunchpadWindow::Prepare(RECT dock, bool frosted) { return impl_->Show(dock, frosted, true); }
void LaunchpadWindow::Hide() { impl_->Hide(); }
bool LaunchpadWindow::Visible() const { return impl_->visible; }
void LaunchpadWindow::Refresh() {
    if (!impl_->visible) return;
    // Mid-opening, the new entries land once the glide settles (FrameMessage).
    if (impl_->clock.Running() && impl_->presenceMoving && !impl_->closing) { impl_->refreshPending = true; return; }
    impl_->Refresh(); impl_->Animate(); impl_->Paint();
}
void LaunchpadWindow::SetFrosted(bool frosted) { impl_->frosted = frosted; if (impl_->visible) impl_->Paint(); }
bool LaunchpadWindow::SetAppearance(int panelScale,int iconScale){return impl_->SetAppearance(panelScale,iconScale);}
bool LaunchpadWindow::Paint() { return impl_->Paint(); }
void LaunchpadWindow::Stop() { impl_->Stop(); }
HWND LaunchpadWindow::Handle() const { return impl_->window; }
HWND LaunchpadWindow::SearchHandle() const { return impl_->search; }
WidgetMaterialState LaunchpadWindow::MaterialState() const { return impl_->material.State(); }
void LaunchpadWindow::SetCollection(std::string id, std::string title) {
    impl_->SetCollectionView(std::move(id), std::move(title));
}
void LaunchpadWindow::BeginRename(std::string id, std::string title) {
    impl_->BeginRename(std::move(id), std::move(title));
}
int LaunchpadWindow::Page() const { return impl_->page; }
int LaunchpadWindow::Pages() const { return impl_->Pages(); }
std::vector<dock::Entry> LaunchpadWindow::PageEntries() const { return impl_->PageEntries(); }
void LaunchpadWindow::Turn(int delta) { impl_->Turn(delta); }
void LaunchpadWindow::Invalidate() {
    if (!impl_->visible && !impl_->closing) return;
    // While opening, repaint once the glide settles (see FrameMessage).
    if (impl_->clock.Running() && (impl_->presenceMoving || std::abs(impl_->presence.velocity) > .05f)) impl_->dirty = true;
    else impl_->Paint();
}
bool LaunchpadWindow::Animating() const { return impl_->clock.Running(); }
void LaunchpadWindow::Settle() { impl_->Settle(); }
float LaunchpadWindow::Presence() const { return impl_->presence.value; }
float LaunchpadWindow::CollectionPresence() const { return impl_->collectionExpand.value; }
WidgetRect LaunchpadWindow::CollectionBounds() const { return impl_->CollectionPanel(); }
}  // namespace lume::platform::windows

