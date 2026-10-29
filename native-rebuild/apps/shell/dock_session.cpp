#include "apps/shell/dock_session.h"
#include "platform/windows/dock/dock_sources.h"
#include "platform/windows/dock/dock_window.h"
#include "platform/windows/dock/launchpad_window.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/organizer/organizer_icon_client.h"
#include "platform/windows/organizer/organizer_icons.h"
#include "platform/windows/organizer/organizer_drop_target.h"
#include "platform/windows/organizer/organizer_text.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "vendor/nlohmann/json.hpp"
#include <shlobj.h>
#include <shobjidl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <utility>

namespace lume::shell {
using namespace platform::windows;
namespace {
constexpr UINT IconsUpdated = WM_APP + 0x7b1, RecycleDropped = WM_APP + 0x7b5;
constexpr UINT_PTR SettleTimer = 0x7b2, PollTimer = 0x7b3, PrepareTimer = 0x7b4;
// Minimized windows as live thumbnails at the end of the Dock. Off: running
// programs show as icons instead. Turning it on also needs RunningApp::asWindows.
constexpr bool WindowThumbnails = false;
// LumeWidgetMenu command codes for the Dock menus.
enum Command : UINT {
    Close = 1, NoMaterial = 7, Liquid = 8, Frosted = 9, Reset = 10, Settings = 11,
    Pin = 20, Unpin = 21, Reveal = 22, RemoveAdded = 23, AddFiles = 30, AddFolders = 31,
    NewCollectionCommand = 40, RenameCollectionCommand = 40, DeleteCollectionCommand = 41, AddSystemIcon = 46,
    RemoveFromCollectionCommand = 42
};
const char* MaterialStatus(WidgetMaterialState state) {
    switch (state) {
    case WidgetMaterialState::Ready: return "native-ready";
    case WidgetMaterialState::Starting: return "native-starting";
    case WidgetMaterialState::TransparentFallback: return "transparent-fallback";
    default: return "inactive";
    }
}
// The shared widget menu draws with GDI+; it is started only around a menu.
template<class Show> UINT Menu(Show show) {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return 0;
    const UINT result = show();
    Gdiplus::GdiplusShutdown(token);
    return result;
}
void RevealPath(const std::string& path) {
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(OrganizerWide(path).c_str());
    if (!item) return;
    SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
}
std::vector<std::string> Choose(HWND owner, bool folders) {
    std::vector<std::string> paths;
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return paths;
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    options |= FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT | FOS_PATHMUSTEXIST | (folders ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST);
    dialog->SetOptions(options);
    dialog->SetTitle(folders ? L"添加文件夹到启动台" : L"添加文件到启动台");
    IShellItemArray* results = nullptr;
    if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResults(&results))) {
        DWORD count = 0;
        results->GetCount(&count);
        for (DWORD i = 0; i < count; ++i) {
            IShellItem* item = nullptr;
            PWSTR name = nullptr;
            if (SUCCEEDED(results->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) && name) {
                if (const auto normalized = organizer::NormalizeLocalPath(OrganizerUtf8(name))) paths.push_back(*normalized);
            }
            CoTaskMemFree(name);
            if (item) item->Release();
        }
        results->Release();
    }
    dialog->Release();
    return paths;
}
bool SameItems(const std::vector<dock::Item>& a, const std::vector<dock::Item>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) {
        return x.kind == y.kind && x.path == y.path && x.name == y.name && x.running == y.running &&
               x.orderIndex == y.orderIndex && x.previewPaths == y.previewPaths && x.window == y.window;
    });
}
}  // namespace

struct DockSession::Impl {
    HINSTANCE instance;
    DockSessionOptions options;
    Notify notify;
    std::function<void()> openManager, openSettings;
    std::function<bool()> managerVisible;
    LocalSettingsFileStore local;
    settings::IFileStore& files;
    dock::TextKey pathKey = dock_sources::PathKey, searchKey = dock_sources::SearchKey;
    dock::Preferences prefs;
    std::vector<std::string> targets;
    std::set<std::string> groupedTargets;
    std::vector<dock::Item> items;
    std::string load = "fresh", error;
    std::string lumePath;
    std::string collectionId;
    std::string popupId;
    bool stopped = false, applying = false, ready = false;
    HWND helper = nullptr;
    HWINEVENTHOOK hook = nullptr, minimizeHook = nullptr;
    // Cover of the music playing elsewhere, polled at most once a second.
    std::optional<NowPlaying> playing;
    ULONGLONG playingCheckedAt = 0;
    unsigned iconPixels = 0;
    OrganizerIconCache icons;
    DockWindow dock;
    LaunchpadWindow launchpad;
    LaunchpadWindow collectionPopup;
    static inline Impl* current = nullptr;
    Impl(HINSTANCE i, DockSessionOptions o, Notify n, std::function<void()> manager, std::function<void()> settings,
         std::function<bool()> visible, settings::IFileStore* f)
        : instance(i), options(std::move(o)), notify(std::move(n)), openManager(std::move(manager)),
          openSettings(std::move(settings)), managerVisible(std::move(visible)), files(f ? *f : local),
          dock(i, DockWindowPorts{[this](const dock::Item& item) { return Icon(item); },
                                  [this](std::size_t index) { Activate(index); },
                                  [this](std::size_t index, POINT point) { DockMenu(index, point); },
                                  [this](std::size_t index, std::size_t pinnedSlot, std::size_t lumePosition) {
                                      Place(index, pinnedSlot, lumePosition);
                                  },
                                  [this](const std::vector<std::string>& paths, std::size_t slot) { return DropPaths(paths, slot); },
                                  [this](const std::string& path, const std::string& key) {
                                      if (!dock::IsCollectionKey(key)) return false;
                                      return DropIntoCollection({path}, key.substr(11));
                                  },
                                  [this](const std::vector<std::string>& paths, const std::string& key) {
                                      return dock::IsCollectionKey(key) && DropIntoCollection(paths, key.substr(11));
                                  },
                                  [this](const dock::Item& item) { return RecycleBinItem(item.path); },
                                  [this](const std::vector<std::string>& paths, const dock::Item&) {
                                      // Recycle after the drop returns, so Explorer's drag loop never
                                      // waits on a long delete or the shell's confirmation.
                                      pendingRecycle.insert(pendingRecycle.end(), paths.begin(), paths.end());
                                      return helper && PostMessageW(helper, RecycleDropped, 0, 0) != FALSE;
                                  },
                                  [this](const dock::Item& item) { Discard(item); },
                                  [this](const dock::Item& item) { return Cover(item); }}),
          launchpad(i, LaunchpadPorts{
              [this] {
                  if (!collectionId.empty()) {
                      if (const auto* collection = dock::FindCollection(prefs, collectionId))
                          return dock::CollectionItems(*collection, pathKey);
                      return std::vector<dock::Entry>{};
                  }
                  return dock::LaunchpadItems(dock_sources::DesktopPaths(), prefs.added, prefs.order, pathKey,
                                              searchKey, prefs.collections);
              },
              [this](const std::vector<dock::Entry>& all, const std::string& query) { return dock::Filter(all, query, searchKey); },
              [this](const std::string& path) { return icons.Find(OrganizerWide(path)); },
              [this](const dock::Entry& entry) { OpenEntry(entry); },
              [this](const dock::Entry& entry, POINT point) { EntryMenu(entry, point); },
              [this](POINT point) { AddMenu(point); },
              [this] { UpdateIcons(); },
              [this] { BackCollection(); },
              [this](const std::string& id, const std::string& title) { RenameCollectionEntry(id, title); },
              [this](const std::vector<std::string>& paths, const std::string& id) { return DropIntoCollection(paths, id); },
              [this](const std::vector<std::string>& paths) { return DropPathsToLaunchpad(paths); },
              [this](const dock::Entry& entry, const std::string& id) { return MoveIntoCollection(entry, id); },
              [this](const dock::Entry& entry) { return RemoveCollectionMember(entry); },
              [this](std::vector<std::string> order) { ArrangeLaunchpad(std::move(order)); },
              [this](POINT screen) { return dock.DragHover(screen); },
              [this](const dock::Entry& entry, POINT screen) {
                  if (!dock.DragHover(screen)) return false;
                  const bool accepted = dock.DropPathsAt({entry.path}, screen);
                  dock.DragLeave();
                  return accepted;
              },
              [this] { dock.DragLeave(); }}),
          collectionPopup(i, PopupPorts(), LaunchpadWindow::Presentation::CollectionPopup) {
        std::wstring module(32768, L'\0');
        module.resize(GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size())));
        if (const auto normalized = organizer::NormalizeLocalPath(OrganizerUtf8(module))) lumePath = *normalized;
        Load();
    }
    LaunchpadPorts PopupPorts() {
        LaunchpadPorts p;
        p.entries = [this] {
            const auto* collection = dock::FindCollection(prefs, popupId);
            return collection ? dock::CollectionItems(*collection, pathKey) : std::vector<dock::Entry>{};
        };
        p.icon = [this](const std::string& path) { return icons.Find(OrganizerWide(path)); };
        p.open = [](const dock::Entry& entry) { OpenOrganizerTarget(OrganizerWide(entry.path)); };
        p.context = [this](const dock::Entry& entry, POINT point) { PopupEntryMenu(entry, point); };
        p.backgroundContext = [this](POINT point) { PopupMenu(point); };
        p.back = [this] { collectionPopup.Hide(); };
        p.rename = [this](const std::string& id, const std::string& title) { RenameCollectionEntry(id, title); };
        p.dropIntoCollection = [this](const std::vector<std::string>& paths, const std::string& id) { return DropIntoCollection(paths, id); };
        p.dropPaths = [this](const std::vector<std::string>& paths) { return DropIntoCollection(paths, popupId); };
        p.arrange = [this](std::vector<std::string> paths) {
            auto next = prefs;
            if (dock::ArrangeCollection(next, popupId, std::move(paths), pathKey) == dock::Change::Changed) Update(next);
        };
        p.dragOutside = [this](POINT point) { return dock.DragHover(point); };
        p.dropOutside = [this](const dock::Entry& entry, POINT point) { return DropFromPopup(entry, point); };
        p.leaveOutside = [this] { dock.DragLeave(); };
        return p;
    }
    bool OnDock(const std::string& id) const {
        return std::find(prefs.dockCollections.begin(), prefs.dockCollections.end(), id) != prefs.dockCollections.end();
    }
    void ShowCollection(const std::string& id) {
        const auto* collection = dock::FindCollection(prefs, id);
        if (!collection) return;
        if (collectionPopup.Visible() && popupId == id) { collectionPopup.Hide(); return; }
        launchpad.Hide();
        popupId = id;
        collectionPopup.SetCollection(id, collection->title);
        RECT anchor = dock.ScreenBounds();
        for (std::size_t n = 0; n < items.size(); ++n)
            if (items[n].kind == dock::Kind::Collection && items[n].path == dock::CollectionKey(id)) { anchor = dock.IconBounds(n); break; }
        if (!collectionPopup.Show(anchor, prefs.frosted)) { error = "collection-popup"; Changed(false); }
    }
    void SyncPopup() {
        if (!collectionPopup.Visible()) return;
        const auto* collection = dock::FindCollection(prefs, popupId);
        if (!collection || !OnDock(popupId)) { collectionPopup.Hide(); return; }
        collectionPopup.SetCollection(popupId, collection->title);
        collectionPopup.SetFrosted(prefs.frosted);
    }
    void CollectionMenu(const std::string& id, HWND owner, POINT point) {
        const auto* found = dock::FindCollection(prefs, id);
        if (!found) return;
        const auto title = found->title;
        const UINT command = Menu([&] { return LumeWidgetMenu::ShowLaunchpadCollection(owner, point); });
        if (stopped || !ready) return;
        if (command == RenameCollectionCommand) {
            if (!collectionPopup.Visible() || popupId != id) ShowCollection(id);
            collectionPopup.BeginRename(id, title);
        } else if (command == DeleteCollectionCommand) DeleteCollection(id);
    }
    void PopupMenu(POINT point) { const auto id = popupId; CollectionMenu(id, collectionPopup.Handle(), point); }
    bool DropFromPopup(const dock::Entry& entry, POINT point) {
        if (!dock.DragHover(point)) { dock.DragLeave(); return false; }
        const auto target = dock.CollectionTarget();
        const auto slot = dock.DragSlot();
        dock.DragLeave();
        if (!target.empty()) return DropIntoCollection({entry.path}, target.substr(11));
        if (slot == DockWindow::None) return false;
        auto next = prefs;
        const bool isLume = !lumePath.empty() && pathKey(entry.path) == pathKey(lumePath);
        const auto change = isLume
            ? dock::RemoveFromCollection(next, popupId, entry.path, pathKey)
            : dock::MoveFromDockCollection(next, popupId, entry.path, slot, pathKey);
        if (change != dock::Change::Changed) { error = "limit"; Changed(false); return false; }
        if (isLume) {
            next.lumeVisible = true;
            next.lumePosition = static_cast<int>(std::min(slot, next.pinned.size()));
        }
        const bool ok = Apply(std::move(next)); Changed(ok); return ok;
    }
    void PopupEntryMenu(const dock::Entry& entry, POINT point) {
        const auto path = entry.path;
        const auto id = popupId;
        const auto command = Menu([&] { return LumeWidgetMenu::ShowDockItem(collectionPopup.Handle(), point, false, false, true, true); });
        if (stopped || !ready) return;
        if (command == Reveal) { collectionPopup.Hide(); RevealPath(path); return; }
        if (command != Pin && command != RemoveFromCollectionCommand) return;
        auto next = prefs;
        if (dock::MoveFromDockCollection(next, id, path, 0, pathKey) == dock::Change::Changed) Update(next);
    }
    ~Impl() { Shutdown(); }
    // Preferences: backup-protected atomic replacement, as the radial menu does.
    void Load() {
        if (options.preferencesPath.empty()) { load = "failed"; return; }
        std::string text;
        for (const auto& path : {options.preferencesPath, options.preferencesPath + ".bak"}) {
            if (!files.Exists(path)) continue;
            if (files.ReadBounded(path, dock::MaxPreferencesBytes, text) && dock::Decode(text, prefs, pathKey)) {
                load = path == options.preferencesPath ? "loaded" : "recovered";
                return;
            }
            load = "failed";
        }
    }
    bool Save(const dock::Preferences& value) {
        const auto& path = options.preferencesPath;
        std::string text;
        if (path.empty() || !dock::Encode(value, text)) return false;
        std::string original;
        bool valid = false;
        if (files.Exists(path)) {
            dock::Preferences old;
            valid = files.ReadBounded(path, dock::MaxPreferencesBytes, original) && dock::Decode(original, old, pathKey);
            if (!valid) {
                bool isolated = false;
                for (unsigned i = 0; i < 1024; ++i) {
                    const auto destination = path + ".corrupt" + (i ? "." + std::to_string(i) : "");
                    if (!files.Exists(destination)) { isolated = files.Rename(path, destination); break; }
                }
                if (!isolated) return false;
            }
        }
        const auto temporary = path + ".tmp", backup = path + ".bak.tmp";
        const auto failed = [&] { files.Remove(temporary); files.Remove(backup); return false; };
        if (!files.Write(temporary, text)) return failed();
        if (valid && (!files.Write(backup, original) || !files.Rename(backup, path + ".bak"))) return failed();
        if (!files.Rename(temporary, path)) return failed();
        return true;
    }
    static DockAppearance Look(const dock::Preferences& value) {
        DockAppearance look;
        look.frosted = value.frosted;
        look.transparent = value.transparent;
        look.reflection = value.reflection;
        look.iconScale = value.iconScale;
        look.position = value.position;
        look.bottomOffset = value.bottomOffset;
        look.iconSpacing = value.iconSpacing;
        return look;
    }
    static void ResetLook(dock::Preferences& value) {
        value.frosted = value.transparent = value.reflection = false;
        value.iconScale = dock::DefaultIconScale;
        value.position = dock::DefaultPosition;
        value.bottomOffset = dock::DefaultBottomOffset;
        value.iconSpacing = dock::DefaultIconSpacing;
        value.launchpadScale=dock::DefaultLaunchpadScale;
        value.launchpadIconScale=dock::DefaultLaunchpadIconScale;
        value.lumeVisible = true;
        value.lumePosition = 0;
    }
    bool Running() const { return ready && dock.Visible(); }
    std::wstring State(bool applied = false, bool saved = false) const {
        std::size_t missingIcons=0;
        for(const auto& item:items)
            if(item.kind!=dock::Kind::Launchpad&&item.kind!=dock::Kind::Separator&&item.kind!=dock::Kind::Collection&&
               !icons.Find(OrganizerWide(item.kind==dock::Kind::Lume?lumePath:item.path)))++missingIcons;
        const auto text = nlohmann::json{{"version", 1}, {"type", "dock-state"}, {"enabled", prefs.enabled},
            {"running", Running()}, {"visibleItems", items.size()}, {"layersVisible", dock.LayersVisible()},
            {"missingIcons",missingIcons}, {"iconCachePaths",icons.CachedPaths()},
            {"material", prefs.transparent ? "none" : prefs.frosted ? "frosted-glass" : "liquid-glass"},
            {"reflection", prefs.reflection}, {"iconScale", prefs.iconScale},
            {"launchpadScale",prefs.launchpadScale},{"launchpadIconScale",prefs.launchpadIconScale},
            {"position", prefs.position}, {"bottomOffset", prefs.bottomOffset}, {"iconSpacing", prefs.iconSpacing},
            {"pinned", prefs.pinned.size()}, {"added", prefs.added.size()},
            {"materialStatus", MaterialStatus(dock.MaterialState())}, {"loadStatus", load}, {"error", error},
            {"result", applied ? "applied" : "snapshot"}, {"saved", saved}}.dump();
        return OrganizerWide(text);
    }
    std::shared_ptr<const WidgetBitmap> Icon(const dock::Item& item) {
        const auto& path = item.kind == dock::Kind::Lume ? lumePath : item.path;
        return path.empty() ? nullptr : icons.Find(OrganizerWide(path));
    }
    void UpdateIcons() {
        if (!helper) return;
        std::vector<std::wstring> paths;
        if (!lumePath.empty()) paths.push_back(OrganizerWide(lumePath));
        for (const auto& item : items)
            if (item.kind != dock::Kind::Collection && !item.path.empty()) paths.push_back(OrganizerWide(item.path));
        // Keep collection previews warm before the rest of a large launchpad.
        for (const auto& collection : prefs.collections) {
            const auto members = dock::CollectionItems(collection, pathKey);
            for (std::size_t i = 0; i < std::min<std::size_t>(4, members.size()); ++i)
                paths.push_back(OrganizerWide(members[i].path));
        }
        for (const auto& entry : dock::LaunchpadItems(dock_sources::DesktopPaths(), prefs.added, prefs.order, pathKey, searchKey))
                paths.push_back(OrganizerWide(entry.path));
        unsigned dpi = dock.Handle() ? GetDpiForWindow(dock.Handle()) : 96;
        if (!dpi) dpi = 96;
        // Large enough for a fully magnified icon, so hovering never enlarges a small bitmap.
        iconPixels = std::min(256u, static_cast<unsigned>(std::ceil(DockWindow::IconSize * DockWindow::MaxScale * dpi / 96 * prefs.iconScale / 100)));
        iconPixels=std::max(iconPixels,std::min(256u,static_cast<unsigned>(std::ceil(52.f*dpi/96*prefs.launchpadIconScale/100))));
        // Keep the Dock's own icons first when the launchpad pushes the unique
        // path count past the cache bound. The cache validates before mutating,
        // so a rejected update still retains the previous visible icons.
        std::set<std::wstring> seen;
        std::vector<std::wstring> unique;
        unique.reserve(std::min(paths.size(), MaxIconPaths));
        for (auto& path : paths) {
            if (!seen.insert(path).second) continue;
            if (unique.size() >= MaxIconPaths) break;
            unique.push_back(std::move(path));
        }
        const HWND target = helper;
        if (!icons.SetPaths(unique, [target] { PostMessageW(target, IconsUpdated, 0, 0); }, iconPixels))
            OutputDebugStringW(L"Lume Dock icon cache update rejected\n");
    }
    void ResolveTargets() {
        targets.clear();
        for (const auto& path : prefs.pinned) targets.push_back(dock_sources::ShortcutTarget(path));
        groupedTargets.clear();
        for (const auto& id : prefs.dockCollections)
            if (const auto* collection = dock::FindCollection(prefs, id))
                for (const auto& path : collection->paths) groupedTargets.insert(pathKey(dock_sources::ShortcutTarget(path)));
    }
    void Refresh(bool force = false) {
        if (!ready) return;
        auto running = dock_sources::RunningApps();
        running.erase(std::remove_if(running.begin(), running.end(), [&](const auto& app) {
            return groupedTargets.count(pathKey(app.executable)) != 0;
        }), running.end());
        auto next = dock::DockItemsFor(prefs, targets, running, pathKey, lumePath);
        // Minimized windows as thumbnails at the end (macOS style) are kept but
        // switched off: running programs show as icons after the separator.
        if (WindowThumbnails) {
            auto windows = dock_sources::ProgramWindows();
            windows.erase(std::remove_if(windows.begin(), windows.end(), [](const auto& window) { return !window.minimized; }),
                          windows.end());
            dock::AppendWindows(next, windows);
        }
        for (auto& item : next)
            if (item.kind == dock::Kind::Lume) item.running = managerVisible && managerVisible();
        // Explorer can hide one of the two layered Dock windows while
        // restarting its desktop host. Keep refreshing in that state so the
        // Dock re-shows and restacks instead of waiting for a process restart.
        if (!force && SameItems(next, items) && dock.LayersVisible()) return;
        items = std::move(next);
        if (!dock.Show(items, Look(prefs))) { error = "runtime"; return; }
        UpdateIcons();
    }
    bool CreateHelper() {
        if (helper) return true;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = HelperProcedure;
        wc.lpszClassName = L"LumeDockSession";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        helper = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
        if (!helper) return false;
        current = this;
        hook = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, WindowEvent, 0, 0,
                               WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        // Minimized windows join and leave the Dock promptly.
        minimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, WindowEvent, 0, 0,
                                       WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        SetTimer(helper, PollTimer, 3000, nullptr);
        SetTimer(helper, PrepareTimer, 2500, nullptr);
        return true;
    }
    void DestroyHelper() {
        if (hook) { UnhookWinEvent(hook); hook = nullptr; }
        if (minimizeHook) { UnhookWinEvent(minimizeHook); minimizeHook = nullptr; }
        icons.Stop();
        if (helper) { DestroyWindow(helper); helper = nullptr; }
        if (current == this) current = nullptr;
    }
    static void CALLBACK WindowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
        if (!current || !current->helper || object != OBJID_WINDOW || child != CHILDID_SELF || !window) return;
        if (GetAncestor(window, GA_PARENT) != GetDesktopWindow()) return;
        const bool minimize = event == EVENT_SYSTEM_MINIMIZESTART || event == EVENT_SYSTEM_MINIMIZEEND;
        SetTimer(current->helper, SettleTimer, minimize ? 40 : 250, nullptr);
    }
    std::shared_ptr<const WidgetBitmap> Cover(const dock::Item& item) {
        if (item.kind != dock::Kind::Window || !options.nowPlaying) return nullptr;
        const auto now = GetTickCount64();
        if (!playingCheckedAt || now - playingCheckedAt >= 1000) { playingCheckedAt = now; playing = options.nowPlaying(); }
        if (!playing || !playing->cover || playing->app.empty()) return nullptr;
        // Desktop players publish their exe name or path ("QQMusic.exe"), Store
        // apps "Family!App": compare whole names, so QQ never takes QQMusic's cover.
        const auto lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        };
        const auto stem = lower(std::filesystem::u8path(item.path).stem().u8string());
        auto app = lower(playing->app);
        if (const auto slash = app.find_last_of("\\/"); slash != std::string::npos) app.erase(0, slash + 1);
        if (app.size() > 4 && app.compare(app.size() - 4, 4, ".exe") == 0) app.resize(app.size() - 4);
        const auto bang = app.find('!');
        const auto family = app.substr(0, std::min(app.find('_'), bang));
        const bool same = !stem.empty() && (app == stem || (bang != std::string::npos && (family == stem || app.substr(bang + 1) == stem)));
        return same ? playing->cover : nullptr;
    }
    static LRESULT CALLBACK HelperProcedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == RecycleDropped) {
            auto paths = std::move(self->pendingRecycle);
            self->pendingRecycle.clear();
            if (!self->stopped && !paths.empty()) dock_sources::Recycle(self->dock.Handle(), paths);
            return 0;
        }
        if (self && message == IconsUpdated) {
            self->dock.Paint(); self->launchpad.Invalidate(); self->collectionPopup.Invalidate(); return 0;
        }
        if (self && message == WM_TIMER && w == PrepareTimer) {
            KillTimer(hwnd, PrepareTimer);
            if (self->ready && self->dock.Visible()) self->launchpad.Prepare(self->dock.ScreenBounds(), self->prefs.frosted);
            return 0;
        }
        if (self && message == WM_TIMER && (w == SettleTimer || w == PollTimer)) {
            if (w == SettleTimer) KillTimer(hwnd, SettleTimer);
            self->Refresh();
            if(self->ready&&!self->icons.RetryMissing())OutputDebugStringW(L"Lume Dock icon recovery rejected\n");
            return 0;
        }
        return DefWindowProcW(hwnd, message, w, l);
    }
    bool Runtime(const dock::Preferences& value) {
        if (!value.enabled) {
            ready = false;
            launchpad.Stop();
            collectionPopup.Stop();
            dock.Stop();
            DestroyHelper();
            items.clear();
            return true;
        }
        if (!CreateHelper()) return false;
        ready = true;
        Refresh(true);
        launchpad.SetFrosted(value.frosted);
        if(!launchpad.SetAppearance(value.launchpadScale,value.launchpadIconScale))return false;
        return dock.Visible();
    }
    bool Restore() {
        if (stopped) return false;
        applying = true;
        ResolveTargets();
        const bool ok = Runtime(prefs);
        applying = false;
        if (!ok) { Runtime({}); error = "runtime"; }
        else error.clear();
        return ok;
    }
    bool Apply(dock::Preferences next) {
        if (stopped || applying || !dock::Valid(next, pathKey)) return false;
        applying = true;
        const auto previous = prefs;
        prefs = next;
        ResolveTargets();
        const bool ok = Runtime(next);
        if (!ok || !Save(next)) {
            prefs = previous;
            ResolveTargets();
            const bool rollback = Runtime(previous);
            error = rollback ? (ok ? "save" : "runtime") : "rollback";
            applying = false;
            return false;
        }
        load = "loaded";
        error.clear();
        applying = false;
        SyncPopup();
        return true;
    }
    void Changed(bool saved) { if (notify) notify(State(saved, saved), !saved); }
    void Update(dock::Preferences next) { const bool saved = Apply(std::move(next)); Changed(saved); }
    bool ToggleLaunchpad() {
        if (!dock.Visible()) return false;
        collectionPopup.Hide();
        if (launchpad.Visible()) { launchpad.Hide(); return true; }
        if (launchpad.Show(dock.ScreenBounds(), prefs.frosted)) return true;
        error = "launchpad"; Changed(false);
        return false;
    }
    void Activate(std::size_t index) {
        if (index >= items.size()) return;
        const auto item = items[index];
        if (item.kind != dock::Kind::Collection) collectionPopup.Hide();
        switch (item.kind) {
        case dock::Kind::Collection: ShowCollection(item.path.substr(11)); return;
        case dock::Kind::Launchpad: ToggleLaunchpad(); return;
        case dock::Kind::Lume:
            launchpad.Hide();
            if (openManager) openManager();
            return;
        case dock::Kind::Pinned: {
            launchpad.Hide();
            const auto target = !item.runningProgram.empty() ? item.runningProgram :
                item.orderIndex < targets.size() ? targets[item.orderIndex] : item.path;
            if (!item.running || !dock_sources::ActivateProgram(target)) OpenOrganizerTarget(OrganizerWide(item.path));
            SetTimer(helper, SettleTimer, 600, nullptr);
            return;
        }
        case dock::Kind::Running:
            launchpad.Hide();
            // A program whose window cannot be brought forward is started
            // again, so its own single-instance handling shows it.
            if (!dock_sources::ActivateProgram(item.path)) OpenOrganizerTarget(OrganizerWide(item.path));
            SetTimer(helper, SettleTimer, 600, nullptr);
            return;
        case dock::Kind::Window:
            launchpad.Hide();
            dock_sources::ActivateWindow(item.window);
            SetTimer(helper, SettleTimer, 40, nullptr);
            return;
        case dock::Kind::Separator: return;
        }
    }
    // A Dock icon dropped on the Recycle Bin leaves the Dock, like Unpin.
    void Discard(const dock::Item& item) {
        if (stopped || !ready) return;
        auto next = prefs;
        if (item.kind == dock::Kind::Lume) { next.lumeVisible = false; Update(next); }
        else if (item.kind == dock::Kind::Pinned && !item.path.empty() &&
                 dock::Unpin(next, item.path, pathKey) == dock::Change::Changed) Update(next);
    }
    // Answered on every drag movement, so the shortcut lookup is cached per path.
    bool RecycleBinItem(const std::string& path) {
        const auto found = recycleBins.find(path);
        if (found != recycleBins.end()) return found->second;
        if (recycleBins.size() > 64) recycleBins.clear();
        return recycleBins[path] = dock_sources::IsRecycleBin(path);
    }
    std::map<std::string, bool> recycleBins;
    std::vector<std::string> pendingRecycle;
    // 此电脑, 回收站 and similar have no file: they are pinned as shortcuts that
    // open and draw like the location itself.
    void AddShellLocation(POINT point) {
        struct Location { const wchar_t* label; const wchar_t* parsing; };
        static constexpr Location locations[] = {
            {L"此电脑", L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}"},
            {L"回收站", L"::{645FF040-5081-101B-9F08-00AA002F954E}"},
            {L"控制面板", L"::{26EE0668-A00A-44D7-9371-BEB064C98683}"},
            {L"网络", L"::{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}"},
            {L"用户文件夹", L"::{59031A47-3F72-44A7-89C5-5595FE6B30EE}"},
        };
        std::vector<LumeWidgetMenu::Action> actions;
        for (std::size_t i = 0; i < std::size(locations); ++i) actions.push_back({locations[i].label, static_cast<UINT>(i + 1)});
        const UINT choice = Menu([&] { return LumeWidgetMenu::ShowItems(dock.Handle(), point, actions); });
        if (stopped || !ready || !choice || choice > std::size(locations)) return;
        const auto link = ShellLocationShortcut(locations[choice - 1].parsing);
        if (!link) return;
        auto next = prefs;
        if (dock::Pin(next, *link, pathKey) == dock::Change::Changed) Update(next);
    }
    void DockMenu(std::size_t index, POINT point) {
        if (stopped || !ready) return;
        // The modal menu pumps timers/settings updates, which may replace items.
        // Keep the clicked item's value alive until its command is handled.
        const auto item = index < items.size() ? std::optional<dock::Item>{items[index]} : std::nullopt;
        if (item && item->kind == dock::Kind::Collection) { CollectionMenu(item->path.substr(11), dock.Handle(), point); return; }
        const bool itemMenu = item && item->kind != dock::Kind::Launchpad && item->kind != dock::Kind::Separator;
        const bool onDock = item && (item->kind == dock::Kind::Pinned || item->kind == dock::Kind::Lume);
        const UINT command = Menu([&] {
            if (!itemMenu) return LumeWidgetMenu::ShowDock(dock.Handle(), point, prefs.frosted, prefs.transparent);
            const auto& revealPath = item->kind == dock::Kind::Lume ? lumePath : item->path;
            return LumeWidgetMenu::ShowDockItem(dock.Handle(), point, onDock, false, !revealPath.empty(), false,
                                                true, prefs.frosted, prefs.transparent);
        });
        if (stopped || !ready) return;
        auto next = prefs;
        switch (command) {
        case NewCollectionCommand: CreateNewCollection(true); break;
        case AddSystemIcon: AddShellLocation(point); break;
        case Pin:
            if (item && !item->path.empty() && dock::Pin(next, item->path, pathKey) == dock::Change::Changed) Update(next);
            break;
        case Unpin:
            if (item && item->kind == dock::Kind::Lume) {
                next.lumeVisible = false;
                Update(next);
            } else if (item && !item->path.empty() &&
                       dock::Unpin(next, item->path, pathKey) == dock::Change::Changed) {
                Update(next);
            }
            break;
        case Reveal:
            if (item) {
                const auto path = item->kind == dock::Kind::Lume ? lumePath : item->path;
                if (!path.empty()) RevealPath(path);
            }
            break;
        case Liquid: next.frosted = false; next.transparent = false; Update(next); break;
        case Frosted: next.frosted = true; next.transparent = false; Update(next); break;
        case NoMaterial: next.transparent = true; Update(next); break;
        case Settings: if (openSettings) openSettings(); break;
        case Reset: ResetLook(next); Update(next); break;
        case Close: next.enabled = false; Update(next); break;
        default: break;
        }
    }
    void EntryMenu(const dock::Entry& entry, POINT point) {
        if (entry.collection) {
            const UINT command = Menu([&] { return LumeWidgetMenu::ShowLaunchpadCollection(launchpad.Handle(), point, true, OnDock(entry.collectionId)); });
            if (command == 44) { auto next = prefs; if (dock::PinCollection(next, entry.collectionId, pathKey) == dock::Change::Changed) Update(next); }
            else if (command == 45) {
                auto next = prefs;
                next.dockCollections.erase(std::remove(next.dockCollections.begin(), next.dockCollections.end(), entry.collectionId), next.dockCollections.end());
                Update(next);
            }
            if (command == RenameCollectionCommand) {
                launchpad.BeginRename(entry.collectionId, entry.name);
            } else if (command == DeleteCollectionCommand) {
                DeleteCollection(entry.collectionId);
            }
            return;
        }
        const bool pinned = dock::IsPinned(prefs, entry.path, pathKey);
        const bool inCollection = !collectionId.empty();
        const UINT command = Menu([&] {
            return LumeWidgetMenu::ShowDockItem(launchpad.Handle(), point, pinned, entry.added, true, inCollection);
        });
        auto next = prefs;
        const auto change = command == Pin ? dock::Pin(next, entry.path, pathKey)
                          : command == Unpin ? dock::Unpin(next, entry.path, pathKey)
                          : command == RemoveAdded ? dock::RemoveAdded(next, entry.path, pathKey)
                          : command == RemoveFromCollectionCommand
                              ? dock::RemoveFromCollection(next, collectionId, entry.path, pathKey)
                          : dock::Change::Unchanged;
        if (command == Reveal) { launchpad.Hide(); RevealPath(entry.path); return; }
        if (change == dock::Change::Limit) { error = "limit"; Changed(false); return; }
        if (change != dock::Change::Changed) return;
        Update(next);
        launchpad.Refresh();
    }
    void OpenEntry(const dock::Entry& entry) {
        if (!entry.collection) {
            OpenOrganizerTarget(OrganizerWide(entry.path));
            return;
        }
        if (const auto* collection = dock::FindCollection(prefs, entry.collectionId)) {
            collectionId = entry.collectionId;
            launchpad.SetCollection(collectionId, collection->title);
        }
    }
    void BackCollection() {
        if (collectionId.empty()) return;
        collectionId.clear();
        launchpad.SetCollection("", "");
    }
    void CreateNewCollection(bool onDock = false) {
        static unsigned sequence = 0;
        const std::string id = "c" + std::to_string(GetTickCount64()) + "_" + std::to_string(++sequence);
        auto next = prefs;
        const auto change = dock::CreateCollection(next, id, "新建收纳");
        if (change != dock::Change::Changed) { error = "limit"; Changed(false); return; }
        // A new collection is the user's current organizing target; put it at
        // the front instead of sorting its title after every existing item.
        next.order.insert(next.order.begin(), dock::CollectionKey(id));
        if (onDock && dock::PinCollection(next, id, pathKey) != dock::Change::Changed) return;
        if (!Apply(std::move(next))) { Changed(false); return; }
        Changed(true);
        launchpad.Refresh();
    }
    void DeleteCollection(const std::string& id) {
        auto next = prefs;
        const auto* existing = dock::FindCollection(prefs, id);
        const auto members = existing ? existing->paths : std::vector<std::string>{};
        const bool wasDock = OnDock(id);
        const auto change = dock::DeleteCollection(next, id);
        if (change == dock::Change::Limit) { error = "limit"; Changed(false); return; }
        if (change != dock::Change::Changed) return;
        if (wasDock) {
            std::size_t slot = 0;
            for (const auto& path : members) {
                if (dock::PinAt(next, path, slot, pathKey) == dock::Change::Limit) break;
                ++slot;
            }
        }
        if (!Apply(std::move(next))) { Changed(false); return; }
        Changed(true);
        if (collectionId == id) {
            collectionId.clear();
            launchpad.SetCollection("", "");
        }
        launchpad.Refresh();
    }
    void RenameCollectionEntry(const std::string& id, const std::string& title) {
        if (id.empty() || title.empty()) return;
        auto next = prefs;
        if (dock::RenameCollection(next, id, title) != dock::Change::Changed) return;
        if (!Apply(std::move(next))) { Changed(false); return; }
        Changed(true);
        if (collectionId == id) {
            if (const auto* collection = dock::FindCollection(prefs, id))
                launchpad.SetCollection(id, collection->title);
        }
        launchpad.Refresh();
    }
    void ArrangeLaunchpad(std::vector<std::string> order) {
        auto next = prefs;
        const auto change = collectionId.empty()
            ? dock::Arrange(next, std::move(order), pathKey)
            : dock::ArrangeCollection(next, collectionId, std::move(order), pathKey);
        if (change == dock::Change::Limit) { error = "limit"; Changed(false); return; }
        if (change != dock::Change::Changed) return;
        if (!Apply(std::move(next))) { Changed(false); return; }
        Changed(true);
        launchpad.Refresh();
    }
    bool MoveIntoCollection(const dock::Entry& entry, const std::string& id) {
        if (entry.collection || id.empty()) return false;
        return DropIntoCollection({entry.path}, id);
    }
    bool RemoveCollectionMember(const dock::Entry& entry) {
        if (collectionId.empty()) return false;
        auto next = prefs;
        if (dock::RemoveFromCollection(next, collectionId, entry.path, pathKey) != dock::Change::Changed) return false;
        if (!Apply(std::move(next))) { Changed(false); return false; }
        Changed(true);
        BackCollection();
        launchpad.Refresh();
        return true;
    }
    bool DropIntoCollection(const std::vector<std::string>& paths, const std::string& id) {
        auto next = prefs;
        const auto isLumePath = [this](const std::string& path) {
            return !lumePath.empty() && pathKey(path) == pathKey(lumePath);
        };
        const bool containsLume = std::any_of(paths.begin(), paths.end(), isLumePath);
        const auto change = OnDock(id) ? dock::MoveToDockCollection(next, id, paths, pathKey)
                                      : dock::AddToCollection(next, id, paths, pathKey);
        if (change == dock::Change::Limit) { error = "limit"; Changed(false); return false; }
        if (change != dock::Change::Changed) return false;
        if (containsLume) {
            next.lumeVisible = false;
            next.added.erase(std::remove_if(next.added.begin(), next.added.end(), isLumePath), next.added.end());
        }
        if (!Apply(std::move(next))) { Changed(false); return false; }
        Changed(true);
        launchpad.Refresh();
        UpdateIcons();
        return true;
    }
    bool DropPathsToLaunchpad(const std::vector<std::string>& paths) {
        auto next = prefs;
        const auto change = dock::Add(next, paths, pathKey);
        if (change == dock::Change::Limit) { error = "limit"; Changed(false); return false; }
        if (change != dock::Change::Changed) return false;
        if (!Apply(std::move(next))) { Changed(false); return false; }
        Changed(true);
        launchpad.Refresh();
        UpdateIcons();
        return true;
    }
    // A Lume, pinned, or running Dock icon dropped into the primary sequence.
    // DockWindow has already resolved both persisted slots from that preview.
    void Place(std::size_t index, std::size_t pinnedSlot, std::size_t lumePosition) {
        if (index >= items.size()) return;
        auto next = prefs;
        if (items[index].kind == dock::Kind::Lume) {
            if (lumePosition == DockWindow::None) { Refresh(true); return; }
            next.lumePosition = static_cast<int>(std::min(lumePosition, next.pinned.size()));
            if (next.lumePosition != prefs.lumePosition) Update(next);
            else Refresh(true);
            return;
        }
        if (items[index].kind != dock::Kind::Pinned && items[index].kind != dock::Kind::Running) {
            Refresh(true);
            return;
        }
        const auto change = dock::PinAt(next, items[index].path, pinnedSlot, pathKey);
        if (change == dock::Change::Limit) { error = "limit"; Changed(false); Refresh(true); return; }
        if (change == dock::Change::Invalid) { Refresh(true); return; }
        if (prefs.lumeVisible && lumePosition != DockWindow::None)
            next.lumePosition = static_cast<int>(std::min(lumePosition, next.pinned.size()));
        if (change == dock::Change::Changed || next.lumePosition != prefs.lumePosition)
            Update(next);
        else
            Refresh(true);
    }
    // Files or folders dragged in from outside.
    bool DropPaths(const std::vector<std::string>& paths, std::size_t slot) {
        auto next = prefs;
        bool changed = false;
        for (std::size_t i = 0; i < paths.size(); ++i) {
            const auto change = dock::PinAt(next, paths[i], slot + i, pathKey);
            if (change == dock::Change::Limit) { error = "limit"; Changed(false); break; }
            changed |= change == dock::Change::Changed;
        }
        if (!changed) { Refresh(true); return false; }
        Update(next);
        return true;
    }
    void Arrange(std::vector<std::string> order) {
        auto next = prefs;
        // An over-long arrangement keeps its first MaxOrder entries.
        if (dock::Arrange(next, std::move(order), pathKey) == dock::Change::Changed) Update(next);
    }
    void AddMenu(POINT point) {
        UINT command = Menu([&] { return LumeWidgetMenu::ShowLaunchpadAdd(launchpad.Handle(), point); });
        if (command == NewCollectionCommand) { CreateNewCollection(); return; }
        if (command != AddFiles) return;
        command = Menu([&] { return LumeWidgetMenu::ShowLaunchpadAddKind(launchpad.Handle(), point); });
        if (command != AddFiles && command != AddFolders) return;
        const auto paths = Choose(launchpad.Handle(), command == AddFolders);
        if (paths.empty()) return;
        auto next = prefs;
        const auto change = dock::Add(next, paths, pathKey);
        if (change == dock::Change::Limit) { error = "limit"; Changed(false); return; }
        if (change != dock::Change::Changed) return;
        Update(next);
        launchpad.Refresh();
        UpdateIcons();
    }
    std::optional<std::wstring> HandlePage(ui::PageCommand command) {
        if (stopped || applying) return {};
        using ui::PageCommand;
        if (command == PageCommand::Ready) return State();
        auto next = prefs;
        const int numeric = static_cast<int>(command);
        if(command>=PageCommand::DockLaunchpadScaleFirst&&command<=PageCommand::DockLaunchpadScaleLast){
            next.launchpadScale=60+numeric-static_cast<int>(PageCommand::DockLaunchpadScaleFirst);
            const bool saved=Apply(next);return State(saved,saved);
        }
        if(command>=PageCommand::DockLaunchpadIconScaleFirst&&command<=PageCommand::DockLaunchpadIconScaleLast){
            next.launchpadIconScale=50+numeric-static_cast<int>(PageCommand::DockLaunchpadIconScaleFirst);
            const bool saved=Apply(next);return State(saved,saved);
        }
        const bool isNumeric = (numeric >= static_cast<int>(PageCommand::DockIconScaleFirst) &&
                                numeric <= static_cast<int>(PageCommand::DockIconScaleLast)) ||
                               (numeric >= static_cast<int>(PageCommand::DockPositionFirst) &&
                                numeric <= static_cast<int>(PageCommand::DockPositionLast)) ||
                               (numeric >= static_cast<int>(PageCommand::DockBottomOffsetFirst) &&
                                numeric <= static_cast<int>(PageCommand::DockBottomOffsetLast)) ||
                               (numeric >= static_cast<int>(PageCommand::DockIconSpacingFirst) &&
                                numeric <= static_cast<int>(PageCommand::DockIconSpacingLast));
        if (isNumeric) {
            if (numeric >= static_cast<int>(PageCommand::DockIconScaleFirst) &&
                numeric <= static_cast<int>(PageCommand::DockIconScaleLast))
                next.iconScale = 50 + numeric - static_cast<int>(PageCommand::DockIconScaleFirst);
            else if (numeric >= static_cast<int>(PageCommand::DockPositionFirst) &&
                     numeric <= static_cast<int>(PageCommand::DockPositionLast))
                next.position = numeric - static_cast<int>(PageCommand::DockPositionFirst);
            else if (numeric >= static_cast<int>(PageCommand::DockBottomOffsetFirst) &&
                     numeric <= static_cast<int>(PageCommand::DockBottomOffsetLast))
                next.bottomOffset = numeric - static_cast<int>(PageCommand::DockBottomOffsetFirst);
            else
                next.iconSpacing = 4 + numeric - static_cast<int>(PageCommand::DockIconSpacingFirst);
        } else {
            switch (command) {
            case PageCommand::DockEnable: next.enabled = true; break;
            case PageCommand::DockDisable: next.enabled = false; break;
            case PageCommand::DockLiquidGlass: next.frosted = false; next.transparent = false; break;
            case PageCommand::DockFrostedGlass: next.frosted = true; next.transparent = false; break;
            case PageCommand::DockNoMaterial: next.transparent = true; break;
            case PageCommand::DockReflectionOn: next.reflection = true; break;
            case PageCommand::DockReflectionOff: next.reflection = false; break;
            case PageCommand::DockReset: ResetLook(next); break;
            default: return {};
            }
        }
        const bool saved = Apply(next);
        return State(saved, saved);
    }
    void Shutdown() {
        if (stopped) return;
        stopped = true;
        ready = false;
        launchpad.Stop();
        collectionPopup.Stop();
        dock.Stop();
        DestroyHelper();
    }
};

DockSession::DockSession(HINSTANCE instance, DockSessionOptions options, Notify notify, std::function<void()> openManager,
                         std::function<void()> openSettings, std::function<bool()> managerVisible, settings::IFileStore* files)
    : impl_(std::make_unique<Impl>(instance, std::move(options), std::move(notify), std::move(openManager),
                                   std::move(openSettings), std::move(managerVisible), files)) {}
DockSession::~DockSession() = default;
bool DockSession::Restore() { return impl_->Restore(); }
std::optional<std::wstring> DockSession::HandlePage(ui::PageCommand command) { return impl_->HandlePage(command); }
std::wstring DockSession::State() const { return impl_->State(); }
bool DockSession::Running() const { return impl_->Running(); }
HWND DockSession::Handle() const { return impl_->dock.Handle(); }
void DockSession::Refresh() { impl_->Refresh(); }
bool DockSession::ToggleLaunchpad() { return impl_->ToggleLaunchpad(); }
const dock::Preferences& DockSession::Snapshot() const { return impl_->prefs; }
void DockSession::Shutdown() { impl_->Shutdown(); }
}  // namespace lume::shell
