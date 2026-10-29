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
struct LaunchpadPorts {
    std::function<std::vector<dock::Entry>()> entries;  // desktop and added items, arranged
    std::function<std::vector<dock::Entry>(const std::vector<dock::Entry>&, const std::string& query)> filter;
    std::function<std::shared_ptr<const WidgetBitmap>(const std::string& path)> icon;
    std::function<void(const dock::Entry&)> open;
    std::function<void(const dock::Entry&, POINT screen)> context;
    std::function<void(POINT screen)> add;
    std::function<void()> visibility;  // shown or hidden
    // Collection sub-page: back to the flat grid, rename the open collection,
    // and accept files dropped on a collection tile / into the open collection.
    std::function<void()> back;
    std::function<void(const std::string& id, const std::string& title)> rename;
    std::function<bool(const std::vector<std::string>&, const std::string&)> dropIntoCollection;
    std::function<bool(const std::vector<std::string>&)> dropPaths;
    std::function<bool(const dock::Entry&, const std::string&)> moveIntoCollection;
    // Drag an item out of the open collection: remove it from the collection
    // and return to the flat grid.
    std::function<bool(const dock::Entry&)> removeFromCollection;
    // The full arrangement (paths) after a drag; only offered without a search.
    std::function<void(std::vector<std::string>)> arrange;
    // A dragged item over the screen point outside the panel (the Dock): returns
    // true when it would be accepted there; drop commits it, leave cancels.
    std::function<bool(POINT screen)> dragOutside;
    std::function<bool(const dock::Entry&, POINT screen)> dropOutside;
    std::function<void()> leaveOutside;
    std::function<void(POINT screen)> backgroundContext;
};
// A paged glass grid above the Dock with a native search box. It springs open
// from the Dock and fades out on close. Items can be dragged to rearrange,
// turning pages at the grid edges. It is an activated window: it closes on
// Escape or when another program activates.
class LaunchpadWindow final {
public:
    static constexpr float CornerRadius = 32, PreferredWidth = 900, PreferredHeight = 540;
    struct Layout {
        float width = 0, height = 0;
        int columns = 7, rows = 3;
        WidgetRect panel, search, add, grid, previous, next, page, back;
    };
    enum class HitKind { None, Item, Add, Previous, Next, Back };
    struct HitResult {
        HitKind kind = HitKind::None;
        int index = -1;
    };
    enum class Presentation { Launchpad, CollectionPopup };
    LaunchpadWindow(HINSTANCE, LaunchpadPorts, Presentation = Presentation::Launchpad);
    ~LaunchpadWindow();
    bool Show(RECT dock, bool frosted);
    // Creates the window and its glass while hidden, so the first Show does not
    // pay the material start-up during its opening animation.
    bool Prepare(RECT dock, bool frosted);
    // Animated close; Visible() is false immediately.
    void Hide();
    bool Visible() const;
    // Re-reads entries, keeping the query and clamping the page.
    void Refresh();
    void SetFrosted(bool);
    bool SetAppearance(int panelScale,int iconScale);
    bool Paint();
    // Re-renders now, or once the open/close animation settles.
    void Invalidate();
    void Stop();
    HWND Handle() const;
    HWND SearchHandle() const;
    WidgetMaterialState MaterialState() const;
    // Empty id returns to the flat grid. The title is drawn in place of the
    // search field while the collection is open.
    void SetCollection(std::string id, std::string title);
    // Rename a collection tile in the flat grid, or the open collection. The
    // native edit is positioned over the tile label for a flat entry.
    void BeginRename(std::string id, std::string title);
    int Page() const;
    int Pages() const;
    std::vector<dock::Entry> PageEntries() const;
    void Turn(int delta);
    // Testing: true while the open/close or item springs are moving.
    bool Animating() const;
    // Testing: opening progress (0 closed, 1 open).
    float Presence() const;
    float CollectionPresence() const;
    WidgetRect CollectionBounds() const;
    // Testing: skip to the end of running animations.
    void Settle();
    static Layout LayoutFor(float width, float height, bool collection = false,int iconScale = 100);
    static Layout PopupLayoutFor(float width, float height);
    static WidgetRect Cell(const Layout&, int index);
    static HitResult Hit(const Layout&, std::size_t itemsOnPage, int page, int pages, float x, float y,
                         bool collection = false);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace lume::platform::windows
