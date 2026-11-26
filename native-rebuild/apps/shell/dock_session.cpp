#include <winsock2.h>
#include <iptypes.h>
#include "apps/shell/dock_session.h"
#include "platform/windows/control_center/control_center_window.h"
#include "platform/windows/dock/dock_sources.h"
#include "platform/windows/shell_location.h"
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
#include <shellapi.h>
#include <iphlpapi.h>
#include <imm.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <objidl.h>
#include <gdiplus.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace lume::shell {
using namespace platform::windows;
namespace {
constexpr UINT IconsUpdated = WM_APP + 0x7b1, RecycleDropped = WM_APP + 0x7b5, SummonedDockDismissed = WM_APP + 0x7b6;
constexpr UINT_PTR SettleTimer = 0x7b2, PollTimer = 0x7b3, PrepareTimer = 0x7b4;
constexpr int SummonHotkeyId = 0x7b6;
// LumeWidgetMenu command codes for the Dock menus.
enum Command : UINT {
    Close = 1, CloseWindow = 24, NoMaterial = 7, Liquid = 8, Frosted = 9, Settings = 11,
    Pin = 20, Unpin = 21, Reveal = 22, RemoveAdded = 23, AddFiles = 30, AddFolders = 31,
    NewCollectionCommand = 40, RenameCollectionCommand = 40, DeleteCollectionCommand = 41,
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
               x.target == y.target && x.runningProgram == y.runningProgram &&
               x.orderIndex == y.orderIndex && x.previewPaths == y.previewPaths && x.window == y.window &&
               x.animationWindows == y.animationWindows && x.collectionPrograms == y.collectionPrograms;
    });
}
}  // namespace

struct DockSession::Impl {
    HINSTANCE instance;
    DockSessionOptions options;
    MinimizeAnimationRuntime animation{[this] {
        auto scope = options.animationScope;
        if (options.verification && !scope.threadId) {
            scope.threadId = GetCurrentThreadId();
            scope.processId = GetCurrentProcessId();
            scope.requireBothArchitectures = false;
        }
        return scope;
    }()};
    Notify notify;
    std::function<void()> openManager, openSettings;
    std::function<bool()> managerVisible;
    MaterialSelectionHandler materialSelection;
    LocalSettingsFileStore local;
    settings::IFileStore& files;
    dock::TextKey pathKey = dock_sources::PathKey, searchKey = dock_sources::SearchKey;
    dock::Preferences prefs;
    std::vector<std::string> targets;
    std::map<std::string, std::vector<std::string>> collectionTargets;
    std::vector<dock::Item> items;
    std::string load = "fresh", error;
    std::string lumePath;
    std::string collectionId;
    std::string popupId;
    bool stopped = false, applying = false, ready = false, gameIdle = false;
    HWND helper = nullptr;
    HWINEVENTHOOK hook = nullptr, minimizeHook = nullptr;
    HHOOK summonMouseHook = nullptr;
    bool summonCapturing = false, summoned = false, summonHotkeyRegistered = false;
    // Cover of the music playing elsewhere, polled at most once a second.
    std::optional<NowPlaying> playing;
    ULONGLONG playingCheckedAt = 0;
    DockSystemTrayStatus trayStatus;
    lume::platform::windows::ControlCenterWindow controlCenter;
    lume::platform::windows::ControlCenterWindow::Material ControlCenterLook() const {
        using M = lume::platform::windows::ControlCenterWindow::Material;
        return prefs.transparent ? M::None : prefs.frosted ? M::Frosted : M::Liquid;
    }
    unsigned iconPixels = 0;
    OrganizerIconCache icons;
    std::shared_ptr<std::atomic_bool> iconNotificationPending=std::make_shared<std::atomic_bool>(false);
    std::vector<std::unique_ptr<DockWindow>> docks;
    std::vector<DisplayMonitor> screens, projected;
    ULONGLONG checkedScreens = 0;
    std::size_t activeSlot = 0, summonedSlot = 0;
    DockWindow& ActiveDock() const { return *docks[activeSlot]; }
    DockWindowPorts Ports(std::size_t slot) {
        return DockWindowPorts{[this](const dock::Item& item) { return Icon(item); },
            [this,slot](std::size_t index) { activeSlot=slot; Activate(ResolveDockItem(index)); },
            [this,slot](std::size_t index, POINT point) { activeSlot=slot; DockMenu(ResolveDockItem(index), point); },
            [this,slot](std::size_t index, std::size_t pinnedSlot, std::size_t lumePosition) {
                activeSlot=slot; Place(ResolveDockItem(index), pinnedSlot, lumePosition);
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
            [this](const dock::Item& item) { return Cover(item); }, {},
            [this](const dock::Item& item) {
                const auto& target = !item.runningProgram.empty() ? item.runningProgram :
                                     !item.target.empty() ? item.target : item.path;
                if (IsLumePath(target) && options.managerWindow) {
                    const HWND window = options.managerWindow();
                    std::vector<dock::ProgramWindow> result;
                    if (window && IsWindowVisible(window))
                        result.push_back({target, "Lume", reinterpret_cast<std::uint64_t>(window), IsIconic(window) != FALSE});
                    return result;
                }
                return dock_sources::ProgramWindowsFor(target);
            },
            [this](std::uint64_t handle) {
                EndSummon();
                dock_sources::FocusWindow(handle);
            },
            [this] { return trayStatus; }, slot == 0};
    }
    std::vector<DisplayMonitor> ReadScreens() const {
        return options.monitors ? options.monitors() : ConnectedDisplayMonitors();
    }
    static int Primary(const std::vector<DisplayMonitor>& inventory) {
        const auto found = std::find_if(inventory.begin(), inventory.end(), [](const auto& monitor) { return monitor.primary; });
        return found == inventory.end() ? 1 : static_cast<int>(found - inventory.begin()) + 1;
    }
    std::vector<DisplayMonitor> Targets() const {
        if (screens.empty()) return {};
        const auto primary = Primary(screens);
        if (prefs.screen != 0) {
            const int selected = prefs.screen > 0 && prefs.screen <= static_cast<int>(screens.size()) ? prefs.screen : primary;
            return {screens[selected - 1]};
        }
        auto result = screens;
        std::swap(result.front(), result[primary - 1]);
        return result;
    }
    void StopDocks() {
        EndSummon();
        for (auto& window : docks) window->Stop();
        projected.clear();
        activeSlot = 0;
    }
    bool DragHover(POINT point) {
        for (std::size_t slot = 0; slot < projected.size(); ++slot) {
            if (!docks[slot]->DragHover(point)) continue;
            if (slot != activeSlot) ActiveDock().DragLeave();
            activeSlot = slot;
            return true;
        }
        ActiveDock().DragLeave();
        return false;
    }
    LaunchpadWindow launchpad;
    LaunchpadWindow collectionPopup;
    static inline Impl* current = nullptr;
    Impl(HINSTANCE i, DockSessionOptions o, Notify n, std::function<void()> manager, std::function<void()> settings,
         std::function<bool()> visible, settings::IFileStore* f)
        : instance(i), options(std::move(o)), notify(std::move(n)), openManager(std::move(manager)),
          openSettings(std::move(settings)), managerVisible(std::move(visible)), files(f ? *f : local),
          launchpad(i, LaunchpadPorts{
              [this] {
                  if (!collectionId.empty()) {
                      if (const auto* collection = dock::FindCollection(prefs, collectionId))
                          return dock::CollectionItems(*collection, pathKey);
                      return std::vector<dock::Entry>{};
                  }
                  return dock::LaunchpadItems(dock_sources::DesktopPaths(), prefs.added, prefs.order, pathKey,
                                              searchKey, prefs.collections, prefs.removed);
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
              [this](POINT screen) { return DragHover(screen); },
              [this](const dock::Entry& entry, POINT screen) {
                  if (!DragHover(screen)) return false;
                  const bool accepted = ActiveDock().DropPathsAt({entry.path}, screen);
                  ActiveDock().DragLeave();
                  return accepted;
              },
              [this] { ActiveDock().DragLeave(); },
              {},
              [this](bool maximized) {
                  auto next = prefs;
                  next.launchpadMaximized = maximized;
                  Update(next);
              }}),
          collectionPopup(i, PopupPorts(), LaunchpadWindow::Presentation::CollectionPopup) {
        std::wstring module(32768, L'\0');
        module.resize(GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size())));
        if (const auto normalized = organizer::NormalizeLocalPath(OrganizerUtf8(module))) lumePath = *normalized;
        docks.push_back(std::make_unique<DockWindow>(instance, Ports(0)));
        screens = ReadScreens();
        Load();
    }
    LaunchpadPorts PopupPorts() {
        LaunchpadPorts p;
        p.entries = [this] {
            const auto* collection = dock::FindCollection(prefs, popupId);
            return collection ? dock::CollectionItems(*collection, pathKey) : std::vector<dock::Entry>{};
        };
        p.icon = [this](const std::string& path) { return icons.Find(OrganizerWide(path)); };
        p.open = [this](const dock::Entry& entry) {
            if (prefs.animation) {
                const auto key = dock::CollectionKey(popupId);
                const auto group = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.path == key; });
                if (group != items.end()) ActiveDock().RecordAppClick(group - items.begin(), dock_sources::ShortcutTarget(entry.path));
            }
            OpenOrganizerTarget(OrganizerWide(entry.path));
        };
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
        p.dragOutside = [this](POINT point) { return DragHover(point); };
        p.dropOutside = [this](const dock::Entry& entry, POINT point) { return DropFromPopup(entry, point); };
        p.leaveOutside = [this] { ActiveDock().DragLeave(); };
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
        RECT anchor = ActiveDock().ScreenBounds();
        for (std::size_t n = 0; n < items.size(); ++n)
            if (items[n].kind == dock::Kind::Collection && items[n].path == dock::CollectionKey(id)) { anchor = ActiveDock().IconBounds(n); break; }
        if (!collectionPopup.Show(anchor, prefs.frosted, prefs.edge)) { error = "collection-popup"; Changed(false); }
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
        if (!DragHover(point)) { ActiveDock().DragLeave(); return false; }
        const auto target = ActiveDock().CollectionTarget();
        const auto slot = ActiveDock().DragSlot();
        ActiveDock().DragLeave();
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
        const auto command = Menu([&] { return LumeWidgetMenu::ShowDockItem(collectionPopup.Handle(), point, false, false, true); });
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
        look.animation = value.animation;
        look.reflection = value.reflection;
        look.reflectionRatio = value.reflectionRatio;
        look.reflectionTransparency = value.reflectionTransparency;
        look.iconScale = value.iconScale;
        look.position = value.position;
        look.positionMode = value.positionMode;
        look.positionOffset = value.positionOffset;
        look.bottomOffset = value.bottomOffset;
        look.edge = value.edge;
        look.iconSpacing = value.iconSpacing;
        return look;
    }
    static void ResetLook(dock::Preferences& value) {
        value.frosted = value.transparent = value.reflection = false;
        value.reflectionRatio = dock::DefaultReflectionRatio;
        value.reflectionTransparency = dock::DefaultReflectionTransparency;
        value.showRunning = true;
        value.runningPreview = false;
        value.animation = false;
        value.animateRestore = value.animateLaunch = value.animateClose = true;
        value.animationDuration = dock::DefaultAnimationDurationMs;
        value.showStartMenu = true;
        value.launchpadEnabled = true;
        value.edge = dock::Edge::Bottom;
        value.screen = -1;
        value.iconScale = dock::DefaultIconScale;
        value.position = dock::DefaultPosition;
        value.positionMode = dock::PositionCenter;
        value.positionOffset = dock::DefaultPositionOffset;
        value.bottomOffset = dock::DefaultBottomOffset;
        value.iconSpacing = dock::DefaultIconSpacing;
        value.launchpadScale=dock::DefaultLaunchpadScale;
        value.launchpadIconScale=dock::DefaultLaunchpadIconScale;
        value.launchpadMaximized = false;
        value.lumeVisible = true;
        value.lumePosition = 0;
    }
    bool Running() const { return ready && ActiveDock().Visible(); }
    std::wstring State(bool applied = false, bool saved = false) const {
        std::size_t missingIcons=0;
        for(const auto& item:items)
            if(item.kind!=dock::Kind::Launchpad&&item.kind!=dock::Kind::Separator&&item.kind!=dock::Kind::Collection&&
               item.kind!=dock::Kind::SystemTray&&item.kind!=dock::Kind::StartMenu&&
               !icons.Find(OrganizerWide(item.kind==dock::Kind::Lume?lumePath:item.path)))++missingIcons;
        const auto inventory = ready ? screens : ReadScreens();
        const auto text = nlohmann::json{{"version", 1}, {"screen", prefs.screen < 0 ? Primary(inventory) : prefs.screen},
            {"screenCount", inventory.size()}, {"type", "dock-state"}, {"enabled", prefs.enabled},
            {"running", Running()}, {"visibleItems", items.size()}, {"layersVisible", ActiveDock().LayersVisible()},
            {"missingIcons",missingIcons}, {"iconCachePaths",icons.CachedPaths()},
            {"material", prefs.transparent ? "none" : prefs.frosted ? "frosted-glass" : "liquid-glass"},
            {"reflection", prefs.reflection}, {"showRunning", prefs.showRunning}, {"runningPreview", prefs.runningPreview},
            {"animation",prefs.animation},{"animateRestore",prefs.animateRestore},{"animateLaunch",prefs.animateLaunch},
            {"animateClose",prefs.animateClose},{"animationDuration",prefs.animationDuration}, {"iconScale", prefs.iconScale},
            {"reflectionRatio", prefs.reflectionRatio}, {"reflectionTransparency", prefs.reflectionTransparency},
            {"systemTray", dock::SystemTrayAvailable && prefs.controlCenter},
            {"summonEnabled",prefs.summonEnabled}, {"summonShortcutCode",prefs.summonShortcutCode},
            {"summonHotkeyRegistered",summonHotkeyRegistered},
            {"launchpadScale",prefs.launchpadScale},{"launchpadIconScale",prefs.launchpadIconScale},
            {"launchpadMaximized",prefs.launchpadMaximized},{"launchpadRemoved",prefs.removed.size()},
            {"position", prefs.position},
            {"positionMode",prefs.positionMode==dock::PositionLeft?"left":prefs.positionMode==dock::PositionRight?"right":"center"},
            {"positionOffset",prefs.positionOffset}, {"bottomOffset", prefs.bottomOffset}, {"iconSpacing", prefs.iconSpacing},
            {"edge", dock::EdgeName(prefs.edge)}, {"showStartMenu", prefs.showStartMenu}, {"launchpadEnabled", prefs.launchpadEnabled},
            {"pinned", prefs.pinned.size()}, {"added", prefs.added.size()},
            {"materialStatus", MaterialStatus(ActiveDock().MaterialState())}, {"loadStatus", load}, {"error", error},
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
        for (const auto& entry : dock::LaunchpadItems(dock_sources::DesktopPaths(), prefs.added, prefs.order, pathKey, searchKey,
                                                      {}, prefs.removed))
                paths.push_back(OrganizerWide(entry.path));
        unsigned dpi = 96;
        for (std::size_t slot = 0; slot < projected.size(); ++slot)
            dpi = std::max(dpi, GetDpiForWindow(docks[slot]->Handle()));
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
        if (!icons.SetPaths(unique, [target,pending=iconNotificationPending] {
                if(!pending->exchange(true)&&!PostMessageW(target,IconsUpdated,0,0))pending->store(false);
            }, iconPixels))
            OutputDebugStringW(L"Lume Dock icon cache update rejected\n");
    }
    void ResolveTargets() {
        targets.clear();
        for (const auto& path : prefs.pinned) targets.push_back(dock_sources::ShortcutTarget(path));
        collectionTargets.clear();
        if (!prefs.animation) return;
        for (const auto& id : prefs.dockCollections) {
            const auto* group = dock::FindCollection(prefs, id);
            if (!group) continue;
            auto& programs = collectionTargets[id];
            for (const auto& path : group->paths) {
                auto target = dock_sources::ShortcutTarget(path);
                const auto key = pathKey(target);
                if ((key.size() >= 4 && key.compare(key.size() - 4, 4, ".exe") == 0) || DockShellLocation(OrganizerWide(target)))
                    programs.push_back(std::move(target));
            }
        }
    }
    std::size_t ResolveDockItem(std::size_t index) const {
        const auto displayed = ActiveDock().ItemAt(index);
        if (!displayed) return DockWindow::None;
        const auto key = dock::MotionKey(*displayed);
        for (std::size_t i = 0; i < items.size(); ++i)
            if (dock::MotionKey(items[i]) == key) return i;
        return DockWindow::None;
    }
    bool Refresh(bool force = false) {
        if (!ready) return false;
        if (gameIdle && !force && !summoned) return true;
        bool inventoryChanged = false;
        const auto now = GetTickCount64();
        if (force || now - checkedScreens >= 1000) {
            checkedScreens = now;
            auto inventory = ReadScreens();
            if (!inventory.empty() && inventory != screens) { inventoryChanged = true; screens = std::move(inventory); }
        }
        auto desired = Targets();
        if (desired.empty()) return false;
        const bool moved = desired != projected;
        auto running = dock_sources::RunningApps();
        // The right side represents open apps independently of pinned shortcuts
        // and collections on the left.
        const bool managerOpen = managerVisible && managerVisible();
        if (managerOpen && options.managerWindow && !lumePath.empty())
            running.push_back({lumePath, "Lume", false, reinterpret_cast<std::uint64_t>(options.managerWindow())});
        auto next = dock::DockItemsFor(prefs, targets, running, pathKey, lumePath, managerOpen, collectionTargets);
        for (auto& item : next)
            if (item.kind == dock::Kind::Lume) item.running = managerOpen;
        // Explorer can hide one of the two layered Dock windows while
        // restarting its desktop host. Keep refreshing in that state so the
        // Dock re-shows and restacks instead of waiting for a process restart.
        if (!force && !moved && SameItems(next, items) && std::all_of(docks.begin(), docks.begin() + projected.size(),
                [](const auto& window) { return window->LayersVisible(); })) {
            if (inventoryChanged && notify) notify(State(), false);
            return true;
        }
        items = std::move(next);
        if (moved) {
            EndSummon();
            controlCenter.Close(); collectionPopup.Hide(); launchpad.Hide();
            activeSlot = 0;
        }
        while (docks.size() < desired.size()) docks.push_back(std::make_unique<DockWindow>(instance, Ports(docks.size())));
        for (std::size_t slot = desired.size(); slot < projected.size(); ++slot) docks[slot]->Stop();
        projected = std::move(desired);
        auto look = Look(prefs);
        for (std::size_t slot = 0; slot < projected.size(); ++slot) {
            look.monitor = projected[slot];
            if (!docks[slot]->Show(items, look)) { error = "runtime"; return false; }
        }
        // Ready in the background so the first click only shows the panel.
        if (prefs.controlCenter) controlCenter.Prewarm(instance, ActiveDock().Handle(), ControlCenterLook());
        UpdateIcons();
        if (inventoryChanged && !applying && notify) notify(State(), false);
        return true;
    }
    void UpdateSummonHotkey() {
        if (summonHotkeyRegistered && helper) UnregisterHotKey(helper, SummonHotkeyId);
        summonHotkeyRegistered = false;
        const auto code = prefs.summonShortcutCode;
        if (!helper || summonCapturing || !prefs.enabled || !prefs.summonEnabled || !code) return;
        const unsigned encoded = code / 256;
        UINT modifiers = MOD_NOREPEAT;
        if (encoded & 1) modifiers |= MOD_ALT;
        if (encoded & 2) modifiers |= MOD_CONTROL;
        if (encoded & 4) modifiers |= MOD_SHIFT;
        if (encoded & 8) modifiers |= MOD_WIN;
        summonHotkeyRegistered = RegisterHotKey(helper, SummonHotkeyId, modifiers, code % 256) != FALSE;
    }
    void EndSummon() {
        const bool wasSummoned = summoned;
        summoned = false;
        if (summonMouseHook) { UnhookWindowsHookEx(summonMouseHook); summonMouseHook = nullptr; }
        if (wasSummoned) docks[summonedSlot]->RestoreDesktopLayer();
    }
    void BeginSummon() {
        if (stopped || summonCapturing || !prefs.enabled || !prefs.summonEnabled ||
            !prefs.summonShortcutCode || !summonHotkeyRegistered) return;
        EndSummon();
        POINT cursor{};
        if (GetCursorPos(&cursor)) for (std::size_t slot = 0; slot < projected.size(); ++slot)
            if (PtInRect(&projected[slot].bounds, cursor)) { activeSlot = slot; break; }
        if (gameIdle) Refresh(true);
        summonedSlot = activeSlot;
        if (!ActiveDock().SummonTemporarily()) return;
        if (!summonMouseHook)
            summonMouseHook = SetWindowsHookExW(WH_MOUSE_LL, SummonMouse, GetModuleHandleW(nullptr), 0);
        if (!summonMouseHook) { ActiveDock().RestoreDesktopLayer(); return; }
        summoned = true;
    }
    static LRESULT CALLBACK SummonMouse(int code, WPARAM message, LPARAM data) {
        auto* self = current;
        const bool click = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
                           message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
        if (code >= 0 && click && self && self->summoned && self->helper) {
            RECT dockBounds{};
            const HWND dockWindow = self->docks[self->summonedSlot]->Handle();
            const auto& mouse = *reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
            if (dockWindow && GetWindowRect(dockWindow, &dockBounds) && !PtInRect(&dockBounds, mouse.pt) &&
                !self->docks[self->summonedSlot]->ContainsPreviewPoint(mouse.pt))
                PostMessageW(self->helper, SummonedDockDismissed, 0, 0);
        }
        return CallNextHookEx(self ? self->summonMouseHook : nullptr, code, message, data);
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
        if (!gameIdle) {
            SetTimer(helper, PollTimer, 3000, nullptr);
            SetTimer(helper, PrepareTimer, 2500, nullptr);
        }
        return true;
    }
    void DestroyHelper() {
        if (summonHotkeyRegistered && helper) UnregisterHotKey(helper, SummonHotkeyId);
        summonHotkeyRegistered = false;
        EndSummon();
        if (hook) { UnhookWinEvent(hook); hook = nullptr; }
        if (minimizeHook) { UnhookWinEvent(minimizeHook); minimizeHook = nullptr; }
        icons.Stop();
        iconNotificationPending=std::make_shared<std::atomic_bool>(false);
        if (helper) { DestroyWindow(helper); helper = nullptr; }
        if (current == this) current = nullptr;
    }
    static void CALLBACK WindowEvent(HWINEVENTHOOK, DWORD event, HWND window, LONG object, LONG child, DWORD, DWORD) {
        if (!current || current->gameIdle || !current->helper || object != OBJID_WINDOW || child != CHILDID_SELF || !window) return;
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
        if (self && message == WM_HOTKEY && w == SummonHotkeyId) { self->BeginSummon(); return 0; }
        if (self && message == SummonedDockDismissed) { self->EndSummon(); return 0; }
        if (self && message == RecycleDropped) {
            auto paths = std::move(self->pendingRecycle);
            self->pendingRecycle.clear();
            if (!self->stopped && !paths.empty()) dock_sources::Recycle(self->ActiveDock().Handle(), paths);
            return 0;
        }
        if (self && message == IconsUpdated) {
            self->iconNotificationPending->store(false);
            for (std::size_t slot = 0; slot < self->projected.size(); ++slot) self->docks[slot]->RequestPaint();
            self->launchpad.Invalidate(); self->collectionPopup.Invalidate(); return 0;
        }
        if (self && message == WM_TIMER && w == PrepareTimer) {
            KillTimer(hwnd, PrepareTimer);
            if (!self->gameIdle && self->ready && self->prefs.launchpadEnabled && self->ActiveDock().Visible()) self->launchpad.Prepare(self->ActiveDock().ScreenBounds(), self->prefs.frosted, self->prefs.edge);
            return 0;
        }
        if (self && message == WM_TIMER && (w == SettleTimer || w == PollTimer)) {
            if (w == SettleTimer) KillTimer(hwnd, SettleTimer);
            if (self->gameIdle && !self->summoned) return 0;
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
            StopDocks();
            DestroyHelper();
            items.clear();
            return animation.Stop();
        }
        if (!CreateHelper()) return false;
        ready = true;
        if (!Refresh(true)) return false;
        if (value.launchpadEnabled) {
            launchpad.SetFrosted(value.frosted);
            if (!launchpad.SetAppearance(value.launchpadScale, value.launchpadIconScale)) return false;
            launchpad.SetMaximized(value.launchpadMaximized);
        } else {
            launchpad.Stop();
        }
        return ActiveDock().Visible() && ConfigureMotion(value);
    }
    bool ConfigureMotion(const dock::Preferences& value) {
        minimize_animation::Preferences motion;
        motion.enabled = value.enabled && value.animation && !gameIdle;
        motion.dockOnly = true;
        motion.durationMs = value.animationDuration;
        motion.restore = value.animateRestore;
        motion.launch = value.animateLaunch;
        motion.close = value.animateClose;
        motion.unhideTaskbar = false;
        return animation.Configure(motion);
    }
    void SetGameIdle(bool idle) {
        if (gameIdle==idle) return;
        gameIdle=idle;
        if (!helper || !ready || stopped) return;
        for (std::size_t i=0;i<projected.size();++i) docks[i]->RefreshDesktopLayer();
        if (idle) {
            KillTimer(helper,PollTimer);
            KillTimer(helper,SettleTimer);
            KillTimer(helper,PrepareTimer);
            launchpad.Hide();
            collectionPopup.Hide();
            playing.reset(); playingCheckedAt=0;
        } else {
            SetTimer(helper,PollTimer,3000,nullptr);
            Refresh(true);
        }
        ConfigureMotion(prefs);
    }
    bool Restore() {
        if (stopped) return false;
        applying = true;
        ResolveTargets();
        const bool ok = Runtime(prefs);
        applying = false;
        if (!ok) { Runtime({}); error = "runtime"; }
        else { UpdateSummonHotkey(); error.clear(); }
        return ok;
    }
    bool Apply(dock::Preferences next) {
        if (stopped || applying || !dock::Valid(next, pathKey)) return false;
        applying = true;
        const auto previous = prefs;
        if (next.edge != prefs.edge) {
            collectionPopup.Hide();
            launchpad.Hide();
            controlCenter.Close();
        }
        prefs = next;
        ResolveTargets();
        const bool ok = Runtime(next);
        if (!ok || !Save(next)) {
            prefs = previous;
            ResolveTargets();
            const bool rollback = Runtime(previous);
            UpdateSummonHotkey();
            error = rollback ? (ok ? "save" : "runtime") : "rollback";
            applying = false;
            return false;
        }
        UpdateSummonHotkey();
        load = "loaded";
        error.clear();
        applying = false;
        SyncPopup();
        return true;
    }
    void Changed(bool saved) { if (notify) notify(State(saved, saved), !saved); }
    void Update(dock::Preferences next) { const bool saved = Apply(std::move(next)); Changed(saved); }
    bool ToggleLaunchpad() {
        if (!prefs.launchpadEnabled || !ActiveDock().Visible()) return false;
        collectionPopup.Hide();
        if (launchpad.Visible()) { launchpad.Hide(); return true; }
        if (launchpad.Show(ActiveDock().ScreenBounds(), prefs.frosted, prefs.edge)) return true;
        error = "launchpad"; Changed(false);
        return false;
    }
    void Activate(std::size_t index) {
        if (index >= items.size()) return;
        const auto item = items[index];
        if (item.kind != dock::Kind::Collection) collectionPopup.Hide();
        switch (item.kind) {
        case dock::Kind::Collection: ShowCollection(item.path.substr(11)); return;
        case dock::Kind::SystemTray: {
            launchpad.Hide();
            auto anchor = ActiveDock().IconBounds(index);
            const auto bounds = ActiveDock().ScreenBounds();
            if (dock::Vertical(prefs.edge)) { anchor.left = bounds.left; anchor.right = bounds.right; }
            else { anchor.top = bounds.top; anchor.bottom = bounds.bottom; }
            controlCenter.Toggle(instance, ActiveDock().Handle(), anchor, ControlCenterLook(), prefs.edge);
            return;
        }
        case dock::Kind::Launchpad: ToggleLaunchpad(); return;
        case dock::Kind::StartMenu: {
            launchpad.Hide();
            controlCenter.Close();
            EndSummon();
            if (GetAsyncKeyState(VK_LWIN) < 0 || GetAsyncKeyState(VK_RWIN) < 0 ||
                GetAsyncKeyState(VK_CONTROL) < 0 || GetAsyncKeyState(VK_MENU) < 0 || GetAsyncKeyState(VK_SHIFT) < 0) return;
            INPUT input[2]{};
            for (auto& entry : input) {
                entry.type = INPUT_KEYBOARD;
                entry.ki.wVk = VK_LWIN;
                entry.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
            }
            input[1].ki.dwFlags |= KEYEVENTF_KEYUP;
            if (SendInput(2, input, sizeof(INPUT)) == 1) SendInput(1, input + 1, sizeof(INPUT));
            return;
        }
        case dock::Kind::Lume:
            launchpad.Hide();
            ActiveDock().RecordAppClick(index, lumePath);
            if (openManager) openManager();
            return;
        case dock::Kind::Pinned: {
            launchpad.Hide();
            const auto target = !item.runningProgram.empty() ? item.runningProgram :
                item.orderIndex < targets.size() ? targets[item.orderIndex] : item.path;
            ActiveDock().RecordAppClick(index, target);
            if (!dock_sources::ActivateProgram(target,prefs.animation)) {
                if (dock_sources::IsWindowsSettings(target)) OpenOrganizerTarget(L"ms-settings:");
                else OpenOrganizerTarget(OrganizerWide(item.path));
            }
            SetTimer(helper, SettleTimer, 600, nullptr);
            return;
        }
        case dock::Kind::Running:
            launchpad.Hide();
            ActiveDock().RecordAppClick(index, item.path);
            // A running entry only switches windows. A stale entry or a focus
            // refusal must never start another instance.
            if (IsLumePath(item.path)) { if (openManager) openManager(); }
            else dock_sources::ActivateProgram(item.path,prefs.animation);
            SetTimer(helper, SettleTimer, 600, nullptr);
            return;
        case dock::Kind::Window:
            launchpad.Hide();
            ActiveDock().RecordAppClick(index, item.path);
            dock_sources::ActivateWindow(item.window,prefs.animation);
            SetTimer(helper, SettleTimer, 40, nullptr);
            return;
        case dock::Kind::Separator: return;
        }
    }
    // A Dock icon dropped on the Recycle Bin leaves the Dock, like Unpin.
    void Discard(const dock::Item& item) {
        if (stopped || !ready) return;
        auto next = prefs;
        if (item.kind == dock::Kind::StartMenu) { next.showStartMenu = false; Update(next); }
        else if (item.kind == dock::Kind::Lume) { next.lumeVisible = false; Update(next); }
        else if (item.kind == dock::Kind::Pinned && !item.path.empty() &&
                 dock::Unpin(next, item.path, pathKey) == dock::Change::Changed) Update(next);
        else if (item.kind == dock::Kind::Running && !item.path.empty() && !IsLumePath(item.path))
            dock_sources::CloseProgram(item.path);
    }
    // Answered on every drag movement, so the shortcut lookup is cached per path.
    bool RecycleBinItem(const std::string& path) {
        const auto found = recycleBins.find(path);
        if (found != recycleBins.end()) return found->second;
        if (recycleBins.size() > 64) recycleBins.clear();
        return recycleBins[path] = dock_sources::IsRecycleBin(path);
    }
    std::map<std::string, bool> recycleBins;
    bool IsLumePath(std::string_view path) const {
        return !lumePath.empty() && pathKey(path) == pathKey(lumePath);
    }
    std::vector<std::string> pendingRecycle;
    // 此电脑, 回收站 and similar have no file: they are pinned as shortcuts that
    // open and draw like the location itself.
    void AddShellLocation(UINT command) {
        const wchar_t* location = nullptr;
        switch (command) {
        case LumeWidgetMenu::AddThisPC: location = ThisPcLocation; break;
        case LumeWidgetMenu::AddRecycleBin: location = RecycleBinLocation; break;
        case LumeWidgetMenu::AddControlPanel: location = ControlPanelLocation; break;
        case LumeWidgetMenu::AddNetwork: location = NetworkLocation; break;
        case LumeWidgetMenu::AddUserFolder: location = L"::{59031A47-3F72-44A7-89C5-5595FE6B30EE}"; break;
        default: return;
        }
        const auto link = ShellLocationShortcut(location);
        if (!link) return;
        auto next = prefs;
        if (dock::Pin(next, *link, pathKey) == dock::Change::Changed) Update(next);
    }
    std::optional<std::string> PinPath(const std::string& path) {
        const auto* location = DockShellLocation(OrganizerWide(path));
        if (!location) return path;
        const auto key = pathKey(path);
        for (std::size_t slot = 0; slot < targets.size(); ++slot)
            if (pathKey(targets[slot]) == key) return prefs.pinned[slot];
        return ShellLocationShortcut(location);
    }
    bool RequestMenuMaterial(UINT command) {
        const auto request = materialSelection;
        if (!request || (command != Liquid && command != Frosted && command != NoMaterial)) return false;
        request(command == Liquid ? ui::PageCommand::DockLiquidGlass : command == Frosted ?
            ui::PageCommand::DockFrostedGlass : ui::PageCommand::DockNoMaterial, ActiveDock().Handle());
        return true;
    }
    void DockMenu(std::size_t index, POINT point) {
        if (stopped || !ready) return;
        // The modal menu pumps timers/settings updates, which may replace items.
        // Keep the clicked item's value alive until its command is handled.
        const auto item = index < items.size() ? std::optional<dock::Item>{items[index]} : std::nullopt;
        if (item && item->kind == dock::Kind::Collection) { CollectionMenu(item->path.substr(11), ActiveDock().Handle(), point); return; }
        if (item && item->kind == dock::Kind::SystemTray) controlCenter.Close();
        const bool itemMenu = item && item->kind != dock::Kind::Launchpad && item->kind != dock::Kind::Separator &&
                              item->kind != dock::Kind::SystemTray;
        const bool onDock = item && (item->kind == dock::Kind::Pinned || item->kind == dock::Kind::Lume ||
                                     item->kind == dock::Kind::StartMenu);
        const UINT command = Menu([&] {
            if (!itemMenu) return LumeWidgetMenu::ShowDock(ActiveDock().Handle(), point, prefs.frosted, prefs.transparent, prefs.showStartMenu);
            return LumeWidgetMenu::ShowDockItem(ActiveDock().Handle(), point, onDock, false, false,
                                                true, prefs.frosted, prefs.transparent,
                                                item->kind == dock::Kind::Running && !IsLumePath(item->path),
                                                prefs.showStartMenu);
        });
        if (stopped || !ready) return;
        if (RequestMenuMaterial(command)) return;
        auto next = prefs;
        switch (command) {
        case NewCollectionCommand: CreateNewCollection(true); break;
        case LumeWidgetMenu::AddStartMenu:
            if (!next.showStartMenu) { next.showStartMenu = true; Update(next); }
            break;
        case LumeWidgetMenu::AddThisPC: case LumeWidgetMenu::AddRecycleBin:
        case LumeWidgetMenu::AddControlPanel: case LumeWidgetMenu::AddNetwork:
        case LumeWidgetMenu::AddUserFolder: AddShellLocation(command); break;
        case Pin:
            if (item && IsLumePath(item->path)) { next.lumeVisible = true; Update(next); }
            else if (item && !item->path.empty()) {
                const auto path = PinPath(item->path);
                if (path && dock::Pin(next, *path, pathKey) == dock::Change::Changed) Update(next);
            }
            break;
        case Unpin:
            if (item && item->kind == dock::Kind::StartMenu) {
                next.showStartMenu = false;
                Update(next);
            } else if (item && item->kind == dock::Kind::Lume) {
                next.lumeVisible = false;
                Update(next);
            } else if (item && !item->path.empty() &&
                       dock::Unpin(next, item->path, pathKey) == dock::Change::Changed) {
                Update(next);
            }
            break;
        case CloseWindow:
            if (item && item->kind == dock::Kind::Running && !IsLumePath(item->path)) dock_sources::CloseProgram(item->path);
            SetTimer(helper, SettleTimer, 600, nullptr);
            break;
        case Liquid: next.frosted = false; next.transparent = false; Update(next); break;
        case Frosted: next.frosted = true; next.transparent = false; Update(next); break;
        case NoMaterial: next.transparent = true; Update(next); break;
        case Settings: if (openSettings) openSettings(); break;
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
            // Every launchpad item can be removed: added items leave the list,
            // desktop items are hidden from the launchpad (the file stays).
            return LumeWidgetMenu::ShowDockItem(launchpad.Handle(), point, pinned, true, inCollection);
        });
        auto next = prefs;
        const auto change = command == Pin ? dock::Pin(next, entry.path, pathKey)
                          : command == Unpin ? dock::Unpin(next, entry.path, pathKey)
                          : command == RemoveAdded ? dock::RemoveFromLaunchpad(next, entry.path, dock_sources::DesktopPaths(), pathKey)
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
        if (IsLumePath(items[index].path)) {
            next.lumeVisible = true;
            next.lumePosition = static_cast<int>(std::min(pinnedSlot, next.pinned.size()));
            if (!prefs.lumeVisible || next.lumePosition != prefs.lumePosition) Update(next);
            else Refresh(true);
            return;
        }
        auto path = items[index].path;
        if (items[index].kind == dock::Kind::Running) {
            const auto program = pathKey(path);
            const auto existing = std::find_if(items.begin(), items.end(), [&](const auto& item) {
                return item.kind == dock::Kind::Pinned && (pathKey(item.path) == program ||
                    (!item.target.empty() && pathKey(item.target) == program) ||
                    (!item.runningProgram.empty() && pathKey(item.runningProgram) == program));
            });
            if (existing != items.end()) {
                path = existing->path;
                if (existing->orderIndex < pinnedSlot) --pinnedSlot;
                if (lumePosition != DockWindow::None && existing->orderIndex < static_cast<std::size_t>(prefs.lumePosition)) --lumePosition;
            }
        }
        const auto pinnedPath = PinPath(path);
        if (!pinnedPath) { Refresh(true); return; }
        const auto change = dock::PinAt(next, *pinnedPath, pinnedSlot, pathKey);
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
        if (command == PageCommand::DockSummonCaptureBegin || command == PageCommand::DockSummonCaptureEnd) {
            summonCapturing = command == PageCommand::DockSummonCaptureBegin;
            if (summonCapturing && summonHotkeyRegistered && helper) {
                UnregisterHotKey(helper, SummonHotkeyId);
                summonHotkeyRegistered = false;
            } else if (!summonCapturing) UpdateSummonHotkey();
            return State();
        }
        auto next = prefs;
        const int numeric = static_cast<int>(command);
        if (command >= PageCommand::DockAnimationDurationFirst && command <= PageCommand::DockAnimationDurationLast) {
            next.animationDuration = 50 + numeric - static_cast<int>(PageCommand::DockAnimationDurationFirst);
            const bool saved = Apply(next); return State(saved, saved);
        }
        if (command >= PageCommand::DockScreenFirst && command <= PageCommand::DockScreenLast) {
            next.screen = numeric - static_cast<int>(PageCommand::DockScreenFirst);
            if (next.screen > static_cast<int>(ReadScreens().size())) { error = "screen"; return State(true, false); }
            const bool saved = Apply(next); return State(saved, saved);
        }
        if (command >= PageCommand::DockSummonShortcutFirst && command <= PageCommand::DockSummonShortcutLast) {
            next.summonShortcutCode = static_cast<unsigned>(numeric - static_cast<int>(PageCommand::DockSummonShortcutFirst));
            const bool saved = Apply(next); return State(saved, saved);
        }
        if(command>=PageCommand::DockLaunchpadScaleFirst&&command<=PageCommand::DockLaunchpadScaleLast){
            next.launchpadScale=60+numeric-static_cast<int>(PageCommand::DockLaunchpadScaleFirst);
            const bool saved=Apply(next);return State(saved,saved);
        }
        if(command>=PageCommand::DockLaunchpadIconScaleFirst&&command<=PageCommand::DockLaunchpadIconScaleLast){
            next.launchpadIconScale=50+numeric-static_cast<int>(PageCommand::DockLaunchpadIconScaleFirst);
            const bool saved=Apply(next);return State(saved,saved);
        }
        if (command >= PageCommand::DockPositionOffsetFirst && command <= PageCommand::DockPositionOffsetLast) {
            next.positionOffset = numeric - static_cast<int>(PageCommand::DockPositionOffsetFirst);
            const bool saved = Apply(next); return State(saved, saved);
        }
        if (command >= PageCommand::DockReflectionRatioFirst && command <= PageCommand::DockReflectionRatioLast) {
            next.reflectionRatio = numeric - static_cast<int>(PageCommand::DockReflectionRatioFirst);
            const bool saved = Apply(next); return State(saved, saved);
        }
        if (command >= PageCommand::DockReflectionTransparencyFirst && command <= PageCommand::DockReflectionTransparencyLast) {
            next.reflectionTransparency = numeric - static_cast<int>(PageCommand::DockReflectionTransparencyFirst);
            const bool saved = Apply(next); return State(saved, saved);
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
            case PageCommand::DockAnimationOn: next.animation = true; break;
            case PageCommand::DockAnimationOff: next.animation = false; break;
            case PageCommand::DockAnimateRestoreOn: next.animateRestore = true; break;
            case PageCommand::DockAnimateRestoreOff: next.animateRestore = false; break;
            case PageCommand::DockAnimateLaunchOn: next.animateLaunch = true; break;
            case PageCommand::DockAnimateLaunchOff: next.animateLaunch = false; break;
            case PageCommand::DockAnimateCloseOn: next.animateClose = true; break;
            case PageCommand::DockAnimateCloseOff: next.animateClose = false; break;
            case PageCommand::DockRunningIcons: next.runningPreview = false; break;
            case PageCommand::DockRunningPreviews: next.runningPreview = true; break;
            case PageCommand::DockShowRunningOn: next.showRunning = true; break;
            case PageCommand::DockShowRunningOff: next.showRunning = false; break;
            case PageCommand::DockSystemTrayOn:
                if constexpr (dock::SystemTrayAvailable) next.controlCenter = true;
                else return State(false, false);
                break;
            case PageCommand::DockSystemTrayOff: next.controlCenter = false; break;
            case PageCommand::DockSummonEnable: next.summonEnabled = true; break;
            case PageCommand::DockSummonDisable: next.summonEnabled = false; break;
            case PageCommand::DockPositionModeCenter: next.positionMode = dock::PositionCenter; break;
            case PageCommand::DockPositionModeLeft: next.positionMode = dock::PositionLeft; break;
            case PageCommand::DockPositionModeRight: next.positionMode = dock::PositionRight; break;
            case PageCommand::DockEdgeBottom: next.edge = dock::Edge::Bottom; break;
            case PageCommand::DockEdgeTop: next.edge = dock::Edge::Top; break;
            case PageCommand::DockEdgeLeft: next.edge = dock::Edge::Left; break;
            case PageCommand::DockEdgeRight: next.edge = dock::Edge::Right; break;
            case PageCommand::DockLaunchpadEnable: next.launchpadEnabled = true; break;
            case PageCommand::DockLaunchpadDisable: next.launchpadEnabled = false; break;
            case PageCommand::DockStartMenuOn: next.showStartMenu = true; break;
            case PageCommand::DockStartMenuOff: next.showStartMenu = false; break;
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
        animation.Stop();
        ready = false;
        launchpad.Stop();
        collectionPopup.Stop();
        StopDocks();
        DestroyHelper();
    }
};

DockSession::DockSession(HINSTANCE instance, DockSessionOptions options, Notify notify, std::function<void()> openManager,
                         std::function<void()> openSettings, std::function<bool()> managerVisible, settings::IFileStore* files)
    : impl_(std::make_unique<Impl>(instance, std::move(options), std::move(notify), std::move(openManager),
                                   std::move(openSettings), std::move(managerVisible), files)) {}
DockSession::~DockSession() = default;
bool DockSession::Restore() { return impl_->Restore(); }
void DockSession::SetMaterialSelectionHandler(MaterialSelectionHandler handler) { impl_->materialSelection=std::move(handler); }
std::optional<std::wstring> DockSession::HandlePage(ui::PageCommand command) { return impl_->HandlePage(command); }
std::wstring DockSession::State() const { return impl_->State(); }
bool DockSession::Running() const { return impl_->Running(); }
HWND DockSession::Handle() const { return impl_->ActiveDock().Handle(); }
void DockSession::SetGameIdle(bool idle) { impl_->SetGameIdle(idle); }
void DockSession::Refresh() { impl_->Refresh(); }
bool DockSession::ToggleLaunchpad() { return impl_->ToggleLaunchpad(); }
const dock::Preferences& DockSession::Snapshot() const { return impl_->prefs; }
void DockSession::Shutdown() { impl_->Shutdown(); }
}  // namespace lume::shell
