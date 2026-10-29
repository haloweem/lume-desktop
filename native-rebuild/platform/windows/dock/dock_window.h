#pragma once
#include "core/dock/dock_model.h"
#include "platform/windows/material/widget_material_runtime.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lume::platform::windows {
// Looks the user chooses. iconScale zooms the whole Dock (icons, bar, gaps)
// uniformly; 100 is the standard size.
struct DockAppearance {
    bool frosted = false;
    bool transparent = false;  // no glass: only the icons are drawn
    bool reflection = false;   // mirrored, fading icons below the row
    int iconScale = 100;
    int position = dock::DefaultPosition;
    int bottomOffset = dock::DefaultBottomOffset;
    int iconSpacing = dock::DefaultIconSpacing;
};
struct DockWindowPorts {
    std::function<std::shared_ptr<const WidgetBitmap>(const dock::Item&)> icon;
    std::function<void(std::size_t index)> activate;
    // index is npos for the empty background.
    std::function<void(std::size_t index, POINT screen)> context;
    // items[index] dropped at a primary Dock insertion slot. `pinnedSlot`
    // is the resulting slot in the persisted pinned list; `lumePosition`
    // is the resulting position of the Lume item, or None when it is hidden.
    std::function<void(std::size_t index, std::size_t pinnedSlot, std::size_t lumePosition)> place;
    // Files or folders dragged in from outside, inserted at pinned slot `slot`.
    std::function<bool(const std::vector<std::string>& paths, std::size_t slot)> drop;
    std::function<bool(const std::string& path, const std::string& collectionKey)> collect;
    std::function<bool(const std::vector<std::string>& paths, const std::string& collectionKey)> dropCollection;
    // A pinned item that takes dropped files itself (the Recycle Bin): the
    // drop highlights it instead of opening a gap, then calls dropOnto.
    std::function<bool(const dock::Item&)> acceptsDrop;
    std::function<bool(const std::vector<std::string>& paths, const dock::Item&)> dropOnto;
    // A Dock icon dragged onto such an item (the Recycle Bin): removes the
    // icon from the Dock. Never touches the program or file itself.
    // Also called when a pinned icon or Lume is dragged out of the Dock and released.
    std::function<void(const dock::Item& dragged)> discard;
    // A minimized window whose program is playing music shows the cover
    // instead of the window; null otherwise.
    std::function<std::shared_ptr<const WidgetBitmap>(const dock::Item&)> cover = {};
    // Shared table of icon positions for the minimize animation; tests use their own.
    std::wstring slotsName = {};
};
// The Dock: a glass bar at desktop level, positioned in the primary work
// area (bottom-centre by default), with a transparent icon layer above it so icons can magnify past the
// bar (springy, Nexus-like). Pinned and running icons can be dragged to new
// places, and files dragged in from outside open a gap where they will land.
// Animation steps once per composed display frame. Layout is in DIPs.
class DockWindow final {
public:
    // The glass is larger than the icons: 21 DIP of margin above and below
    // (BarHeight = IconSize + 2 * IconBottom) and Padding on each side.
    static constexpr float BarHeight = 100, Headroom = 72, IconSize = 58, Gap = 10, Padding = 24,
                           CornerRadius = 34, BottomMargin = 10, IconBottom = 21, MaxScale = 1.5f,
                           ExtraWidth = 200;
    // Magnification reaches two icons on each side and none beyond.
    static constexpr float Reach = 2.8f;
    static constexpr std::size_t None = static_cast<std::size_t>(-1);
    DockWindow(HINSTANCE, DockWindowPorts);
    ~DockWindow();
    // Updates the items; animations continue from their current positions.
    bool Show(std::vector<dock::Item>, bool frosted);
    bool Show(std::vector<dock::Item>, const DockAppearance&);
    void Stop();
    bool Paint();
    HWND Handle() const;       // icon layer (input)
    HWND BarHandle() const;    // glass bar
    bool Visible() const;
    // Logical visibility is kept separately from the native windows because
    // Explorer can hide/recreate its desktop band while the Dock is running.
    bool LayersVisible() const;
    RECT ScreenBounds() const; // the glass bar
    WidgetMaterialState MaterialState() const;
    // Width of the bar at rest.
    static float Width(const std::vector<dock::Item>&, float spacing = Gap);
    // Magnification for an icon whose resting centre is `distance` DIPs from the pointer
    // (compact: 1 beyond Reach icon slots).
    static float Magnification(float distance, float spacing = Gap);
    // Pinned insertion slot (0..pinned count, excluding items[skip]) for a centre x in rest layout.
    static std::size_t DropSlot(const std::vector<dock::Item>&, float x, std::size_t skip = None, float spacing = Gap);
    // Item under x in the rest layout (bar-relative DIPs).
    static std::size_t Hit(const std::vector<dock::Item>&, float x, float y);
    static WidgetRect Slot(const std::vector<dock::Item>&, std::size_t index, float spacing = Gap);
    // A drag from another Lume window (the launchpad) over the Dock: opens a gap
    // at the slot under the screen point and returns true while over the bar.
    bool DragHover(POINT screen);
    void DragLeave();
    // The pinned slot of the open gap, or None when no gap is open.
    std::size_t DragSlot() const;
    std::string CollectionTarget() const;
    bool DropPathsAt(const std::vector<std::string>& paths, POINT screen);
    // Testing: true while springs are still settling.
    bool Animating() const;
    // Testing: an item's current animated scale.
    float ScaleOf(std::size_t index) const;
    // Testing: the current external drop gap (0..1) and its slot.
    float GapOpen() const;
    // Testing: an icon's current on-screen rectangle (animated).
    RECT IconBounds(std::size_t index) const;
    // Testing: the dragged icon is outside the Dock and would be removed on release.
    bool Removing() const;
    // Testing: the window drawn as a live thumbnail for items[index], if any.
    bool HasThumbnail(std::size_t index) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace lume::platform::windows
