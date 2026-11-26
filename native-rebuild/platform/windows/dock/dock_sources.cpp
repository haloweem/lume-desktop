#include "platform/windows/dock/dock_sources.h"
#include "platform/windows/dock/dock_animation_targets.h"
#include "platform/windows/lume_window_ids.h"
#include "platform/windows/organizer/organizer_text.h"
#include "platform/windows/shell_location.h"
#include "platform/windows/windows_settings.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <exdisp.h>
#include <shldisp.h>
#include <wrl/client.h>
#include <dwmapi.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>

namespace lume::platform::windows::dock_sources {
namespace {
std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) result = raw;
    CoTaskMemFree(raw);
    return result;
}
bool EndsWithLnk(const std::wstring& path) {
    return path.size() > 4 && _wcsicmp(path.c_str() + path.size() - 4, L".lnk") == 0;
}
std::wstring ImagePath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    const bool ok = QueryFullProcessImageNameW(process, 0, path.data(), &size) != FALSE;
    CloseHandle(process);
    if (!ok) return {};
    path.resize(size);
    return path;
}
std::wstring HostedSettings(HWND frame, DWORD hostPid) {
    if (WindowsSettingsImage().empty()) return {};
    // A minimized Settings frame retains its AppUserModelID even when Windows
    // detaches the CoreWindow and suspends the app.
    Microsoft::WRL::ComPtr<IPropertyStore> properties;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(frame, IID_PPV_ARGS(&properties)))) {
        PROPVARIANT id{};
        const bool settings = SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID, &id)) &&
            id.vt == VT_LPWSTR && id.pwszVal && _wcsicmp(id.pwszVal, WindowsSettingsAppId) == 0;
        PropVariantClear(&id);
        if (settings) return WindowsSettingsImage();
    }
    struct Search { DWORD host; bool found = false; } search{hostPid};
    EnumChildWindows(frame, [](HWND child, LPARAM data) -> BOOL {
        auto& result = *reinterpret_cast<Search*>(data);
        DWORD pid = 0;
        GetWindowThreadProcessId(child, &pid);
        if (!pid || pid == result.host || _wcsicmp(ImagePath(pid).c_str(), WindowsSettingsImage().c_str()) != 0) return TRUE;
        result.found = true;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found ? WindowsSettingsImage() : std::wstring{};
}
std::wstring Description(const std::wstring& path) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size) return {};
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
    struct Translation { WORD language, codePage; }* translations = nullptr;
    UINT bytes = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &bytes) ||
        bytes < sizeof(Translation)) return {};
    for (const auto* field : {L"FileDescription", L"ProductName"}) {
        wchar_t query[96]{};
        swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s", translations[0].language, translations[0].codePage, field);
        wchar_t* value = nullptr;
        UINT length = 0;
        if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &length) && value && length > 1)
            return std::wstring(value, wcsnlen(value, length));
    }
    return {};
}
bool TaskbarWindow(HWND window) {
    if (!IsWindowVisible(window)) return false;
    const auto extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (GetWindow(window, GW_OWNER) && !(extended & WS_EX_APPWINDOW)) return false;
    if ((extended & WS_EX_TOOLWINDOW) && !(extended & WS_EX_APPWINDOW)) return false;
    if ((extended & WS_EX_NOACTIVATE) && !(extended & WS_EX_APPWINDOW)) return false;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked) {
        // Keep the same Dock item while its real window is hidden behind
        // the Genie surface. Shell cloaks (e.g. another desktop) still win.
        if (cloaked != DWM_CLOAKED_APP || !GetPropW(window,window_ids::AnimationOwnerProperty)) return false;
    }
    if (!GetWindowTextLengthW(window)) return false;
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    for (const auto* shell : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"})
        if (!wcscmp(name, shell)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid && pid != GetCurrentProcessId();
}
using Microsoft::WRL::ComPtr;
struct Window { HWND handle; std::string key, target, name; bool explorer; };

void ResolveExplorerLocations(std::vector<Window>& windows) {
    std::map<HWND, Window*> explorer;
    for (auto& window : windows)
        if (window.explorer) explorer.emplace(window.handle, &window);
    if (explorer.empty()) return;
    ComPtr<IShellWindows> shell;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                               IID_PPV_ARGS(&shell)))) return;
    long count = 0;
    if (FAILED(shell->get_Count(&count))) return;
    std::map<HWND, std::pair<const wchar_t*, std::string>> locations;
    for (long index = 0; index < count; ++index) {
        VARIANT item{};
        item.vt = VT_I4;
        item.lVal = index;
        ComPtr<IDispatch> entry;
        ComPtr<IWebBrowserApp> browser;
        SHANDLE_PTR handle = 0;
        if (shell->Item(item, &entry) != S_OK || !entry || FAILED(entry.As(&browser)) ||
            FAILED(browser->get_HWND(&handle))) continue;
        const HWND frame = GetAncestor(reinterpret_cast<HWND>(handle), GA_ROOT);
        const auto found = explorer.find(frame);
        if (found == explorer.end()) continue;
        ComPtr<IServiceProvider> services;
        ComPtr<IShellBrowser> shellBrowser;
        HWND tab = nullptr;
        if (SUCCEEDED(entry.As(&services)) &&
            SUCCEEDED(services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&shellBrowser))) &&
            SUCCEEDED(shellBrowser->GetWindow(&tab)) && tab && !IsWindowVisible(tab)) continue;
        auto [candidate, unique] = locations.try_emplace(frame);
        if (!unique) {
            candidate->second = {};
            continue;
        }
        ComPtr<IDispatch> document;
        ComPtr<IShellFolderViewDual> view;
        ComPtr<Folder> folder;
        ComPtr<Folder2> folderDetails;
        ComPtr<FolderItem> location;
        if (FAILED(browser->get_Document(&document)) || !document || FAILED(document.As(&view)) ||
            FAILED(view->get_Folder(&folder)) || !folder || FAILED(folder.As(&folderDetails)) ||
            FAILED(folderDetails->get_Self(&location)) || !location) continue;
        BSTR path = nullptr;
        location->get_Path(&path);
        const auto* system = path ? DockShellLocation(path) : nullptr;
        SysFreeString(path);
        if (!system) continue;
        BSTR name = nullptr;
        location->get_Name(&name);
        candidate->second = {system, name ? OrganizerUtf8(name) : std::string{}};
        SysFreeString(name);
    }
    for (auto& [frame, location] : locations) {
        if (!location.first || location.second.empty()) continue;
        auto& window = *explorer.at(frame);
        window.target = OrganizerUtf8(location.first);
        window.key = PathKey(window.target);
        window.name = std::move(location.second);
    }
}

std::vector<Window> Windows() {
    std::vector<Window> windows;
    std::map<DWORD, std::wstring> images;
    std::pair<std::vector<Window>*, std::map<DWORD, std::wstring>*> context{&windows, &images};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& state = *reinterpret_cast<std::pair<std::vector<Window>*, std::map<DWORD, std::wstring>*>*>(data);
        if (!TaskbarWindow(window)) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        auto found = state.second->find(pid);
        if (found == state.second->end()) found = state.second->emplace(pid, ImagePath(pid)).first;
        const auto& process = found->second;
        const bool hosted = _wcsicmp(std::filesystem::path(process).filename().c_str(), L"ApplicationFrameHost.exe") == 0;
        const auto image = hosted ? HostedSettings(window, pid) : process;
        if (image.empty()) return TRUE;
        const auto normalized = organizer::NormalizeLocalPath(OrganizerUtf8(image));
        if (!normalized) return TRUE;
        const bool explorer = _wcsicmp(std::filesystem::path(image).filename().c_str(), L"explorer.exe") == 0;
        wchar_t name[64]{};
        if (explorer) GetClassNameW(window, name, ARRAYSIZE(name));
        state.first->push_back({window, PathKey(*normalized), *normalized, {},
                                explorer && (wcscmp(name, L"CabinetWClass") == 0 || wcscmp(name, L"ExploreWClass") == 0)});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    ResolveExplorerLocations(windows);
    return windows;
}
}  // namespace

std::string PathKey(std::string_view path) { return OrganizerPathKey(path); }
std::string SearchKey(std::string_view text) { return OrganizerSearchKey(text); }
bool IsWindowsSettings(const std::string& path) {
    return !WindowsSettingsImage().empty() && _wcsicmp(OrganizerWide(path).c_str(), WindowsSettingsImage().c_str()) == 0;
}

std::vector<std::string> DesktopPaths() {
    std::vector<std::string> paths;
    for (const auto& folder : {KnownFolder(FOLDERID_Desktop), KnownFolder(FOLDERID_PublicDesktop)}) {
        if (folder.empty()) continue;
        std::error_code error;
        for (std::filesystem::directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, error), end;
             !error && it != end; it.increment(error)) {
            const auto path = it->path().wstring();
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                continue;
            if (const auto normalized = organizer::NormalizeLocalPath(OrganizerUtf8(path))) paths.push_back(*normalized);
        }
    }
    return paths;
}

std::string ShortcutTarget(const std::string& path) {
    const auto wide = OrganizerWide(path);
    if (!EndsWithLnk(wide)) return path;
    IShellLinkW* link = nullptr;
    std::string result = path;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
        IPersistFile* file = nullptr;
        if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
            wchar_t target[MAX_PATH * 4]{};
            const bool loaded = SUCCEEDED(file->Load(wide.c_str(), STGM_READ));
            if (loaded && SUCCEEDED(link->GetPath(target, ARRAYSIZE(target), nullptr, 0)) && target[0]) {
                if (const auto normalized = organizer::NormalizeLocalPath(OrganizerUtf8(target))) result = *normalized;
            } else if (loaded) {
                PIDLIST_ABSOLUTE id = nullptr;
                if (SUCCEEDED(link->GetIDList(&id)) && id) {
                    PWSTR parsing = nullptr;
                    if (SUCCEEDED(SHGetNameFromIDList(id, SIGDN_DESKTOPABSOLUTEPARSING, &parsing)) && parsing)
                        if (const auto* location = DockShellLocation(parsing)) result = OrganizerUtf8(location);
                    CoTaskMemFree(parsing);
                }
                CoTaskMemFree(id);
            }
            file->Release();
        }
        link->Release();
    }
    return result;
}

std::vector<dock::RunningApp> RunningApps() {
    // Windows() is top-down Z-order and changes whenever a window is
    // activated. Assign each executable a stable first-seen sequence instead,
    // so the Dock does not reshuffle running icons on every foreground change.
    static std::map<std::string, std::uint64_t> firstSeen;
    static std::uint64_t nextSequence = 1;
    std::vector<std::pair<std::uint64_t, dock::RunningApp>> ordered;
    std::map<std::string, std::size_t> seen;
    const auto windows = Windows();
    for (const auto& window : windows) {
        const auto [entry, first] = seen.emplace(window.key, ordered.size());
        if (!first) {
            ordered[entry->second].second.windows.push_back(reinterpret_cast<std::uint64_t>(window.handle));
            continue;
        }
        auto sequence = firstSeen.find(window.key);
        if (sequence == firstSeen.end()) {
            if (firstSeen.size() > 512) {
                firstSeen.clear();
                nextSequence = 1;
            }
            sequence = firstSeen.emplace(window.key, nextSequence++).first;
        }
        const auto frontWindow = reinterpret_cast<std::uint64_t>(window.handle);
        if (!window.name.empty()) {
            ordered.push_back({sequence->second, {window.target, window.name, false, frontWindow, {frontWindow}}});
            continue;
        }
        // Version resources are file reads; the Dock refreshes every few seconds.
        static std::map<std::string, std::wstring> descriptions;
        auto cached = descriptions.find(window.target);
        if (cached == descriptions.end()) {
            if (descriptions.size() > 512) descriptions.clear();
            cached = descriptions.emplace(window.target, Description(OrganizerWide(window.target))).first;
        }
        const auto& name = cached->second;
        dock::RunningApp app{window.target, name.empty() ? dock::EntryName(window.target) : OrganizerUtf8(name), false, frontWindow, {frontWindow}};
        ordered.push_back({sequence->second, std::move(app)});
    }
    std::stable_sort(ordered.begin(), ordered.end(),
        [](const auto& left, const auto& right) { return left.first < right.first; });
    std::vector<dock::RunningApp> apps;
    apps.reserve(ordered.size());
    for (auto& entry : ordered) apps.push_back(std::move(entry.second));
    return apps;
}

std::vector<dock::ProgramWindow> ProgramWindowsFor(const std::string& executable) {
    const auto key = PathKey(executable);
    // Each window keeps the place it first appeared in while it exists, so
    // thumbnails do not reshuffle when windows are activated.
    static std::map<HWND, std::uint64_t> since;
    static std::uint64_t nextSequence = 1;
    std::vector<std::pair<std::uint64_t, dock::ProgramWindow>> ordered;
    std::map<HWND, std::uint64_t> still;
    for (const auto& window : Windows()) {
        const auto found = since.find(window.handle);
        const auto sequence = found != since.end() ? found->second : nextSequence++;
        still[window.handle] = sequence;
        if ((!executable.empty() && window.key != key) || ordered.size() >= 48) continue;
        wchar_t title[256]{};
        GetWindowTextW(window.handle, title, 256);
        ordered.push_back({sequence, {window.target, OrganizerUtf8(title), reinterpret_cast<std::uint64_t>(window.handle),
                                      IsIconic(window.handle) != FALSE}});
    }
    since = std::move(still);
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<dock::ProgramWindow> result;
    for (auto& entry : ordered) result.push_back(std::move(entry.second));
    return result;
}
std::vector<dock::ProgramWindow> ProgramWindows() { return ProgramWindowsFor({}); }

namespace {
bool BringForward(HWND window, bool animateActivation = false);
bool MinimizeFromDock(HWND window, bool animate) {
    // A foreign-thread ShowWindow can minimize without a transition on the window's GUI thread.
    if (animate && PostMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0)) return true;
    ShowWindow(window, SW_MINIMIZE);
    return true;
}
}
bool FocusWindow(std::uint64_t handle) {
    const auto window = reinterpret_cast<HWND>(handle);
    return IsWindow(window) && BringForward(window);
}

bool ActivateWindow(std::uint64_t handle, bool animateActivation) {
    const auto window = reinterpret_cast<HWND>(handle);
    if (!IsWindow(window)) return false;
    if (!IsIconic(window) && GetForegroundWindow() == window) return MinimizeFromDock(window, animateActivation);
    return BringForward(window, animateActivation);
}

namespace {
// Brings a window forward, preferring the popup the program last used (a
// launcher's real UI is often an owned window). Returns whether the window
// is now on screen; false means it could not even be restored (for example
// an elevated program), so the caller may start the program instead.
bool BringForward(HWND window, bool animateActivation) {
    HWND target = GetLastActivePopup(window);
    if (!target || !IsWindowVisible(target) || !IsWindowEnabled(target)) target = window;
    if (animateActivation && (IsIconic(window) || IsIconic(target) || GetForegroundWindow() != target)) {
        // Prepare both minimized and background windows before their native activation.
        SendMessageTimeoutW(window, dock_animation::ActivatedMessage(), 0, 0, SMTO_ABORTIFHUNG, 200, nullptr);
    }
    const auto restore = [animateActivation](HWND hwnd) {
        if (!IsIconic(hwnd)) return;
        // Let the app's own UI thread perform its restore, as with a title-bar command.
        if (animateActivation) {
            SendMessageTimeoutW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0, SMTO_ABORTIFHUNG, 250, nullptr);
            if (!IsIconic(hwnd)) return;
        }
        ShowWindow(hwnd, SW_RESTORE);
    };
    restore(window);
    if (target != window) restore(target);
    if (SetForegroundWindow(target) || GetForegroundWindow() == target) return true;
    // The foreground lock can refuse a request that came from the Dock click;
    // join the input queue of the current foreground thread and retry once.
    const DWORD self = GetCurrentThreadId();
    const DWORD foreground = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const bool attached = foreground && foreground != self && AttachThreadInput(self, foreground, TRUE);
    BringWindowToTop(target);
    SetForegroundWindow(target);
    if (attached) AttachThreadInput(self, foreground, FALSE);
    if (GetForegroundWindow() == target || GetAncestor(GetForegroundWindow(), GA_ROOTOWNER) == window) return true;
    return IsWindowVisible(target) && !IsIconic(target) && !IsIconic(window);
}
}  // namespace

bool ActivateProgram(const std::string& executable, bool animateActivation) {
    const auto key = PathKey(executable);
    std::vector<HWND> mine;
    for (const auto& window : Windows())
        if (window.key == key) mine.push_back(window.handle);
    if (mine.empty()) return false;
    const HWND foreground = GetForegroundWindow();
    const auto current = std::find(mine.begin(), mine.end(), foreground);
    HWND target = mine.front();
    if (current != mine.end()) {
        if (mine.size() == 1) return MinimizeFromDock(foreground, animateActivation);
        // EnumWindows is top-down: the next window is the one behind the current.
        target = std::next(current) == mine.end() ? mine.front() : *std::next(current);
    }
    return BringForward(target, animateActivation);
}

bool CloseProgram(const std::string& executable) {
    const auto key = PathKey(executable);
    bool closed = false;
    for (const auto& window : Windows())
        if (window.key == key) closed = PostMessageW(window.handle, WM_CLOSE, 0, 0) != FALSE || closed;
    return closed;
}

bool IsRecycleBin(const std::string& path) {
    const auto wide = OrganizerWide(path);
    if (wide == RecycleBinLocation) return true;
    PIDLIST_ABSOLUTE recycle = nullptr;
    if (FAILED(SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder, 0, nullptr, &recycle)) || !recycle) return false;
    PIDLIST_ABSOLUTE target = nullptr;
    if (EndsWithLnk(wide)) {
        IShellLinkW* link = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
            IPersistFile* file = nullptr;
            if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))) {
                if (SUCCEEDED(file->Load(wide.c_str(), STGM_READ))) link->GetIDList(&target);
                file->Release();
            }
            link->Release();
        }
    }
    const bool same = target && ILIsEqual(target, recycle);
    CoTaskMemFree(target);
    CoTaskMemFree(recycle);
    return same;
}

bool Recycle(HWND owner, const std::vector<std::string>& paths) {
    std::vector<PIDLIST_ABSOLUTE> ids;
    for (const auto& path : paths) {
        PIDLIST_ABSOLUTE id = nullptr;
        // The Recycle Bin shortcut itself and anything already gone are skipped.
        if (IsRecycleBin(path) || FAILED(SHParseDisplayName(OrganizerWide(path).c_str(), nullptr, &id, 0, nullptr)) || !id) continue;
        ids.push_back(id);
    }
    struct Free { std::vector<PIDLIST_ABSOLUTE>& ids; ~Free() { for (auto id : ids) CoTaskMemFree(id); } } free{ids};
    if (ids.empty()) return false;
    IShellItemArray* items = nullptr;
    if (FAILED(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.size()),
            const_cast<PCIDLIST_ABSOLUTE_ARRAY>(reinterpret_cast<const PCIDLIST_ABSOLUTE*>(ids.data())), &items)) || !items) return false;
    IFileOperation* operation = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&operation))) && operation) {
        BOOL aborted = FALSE;
        ok = SUCCEEDED(operation->SetOwnerWindow(owner)) &&
             SUCCEEDED(operation->SetOperationFlags(FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE | FOFX_ADDUNDORECORD)) &&
             SUCCEEDED(operation->DeleteItems(items)) && SUCCEEDED(operation->PerformOperations()) &&
             SUCCEEDED(operation->GetAnyOperationsAborted(&aborted)) && !aborted;
        operation->Release();
    }
    items->Release();
    return ok;
}

ULONG WatchDesktop(HWND window, UINT message) {
    PIDLIST_ABSOLUTE user = nullptr, common = nullptr;
    SHGetKnownFolderIDList(FOLDERID_Desktop, 0, nullptr, &user);
    SHGetKnownFolderIDList(FOLDERID_PublicDesktop, 0, nullptr, &common);
    SHChangeNotifyEntry entries[2]{{user, FALSE}, {common, FALSE}};
    const int count = user && common ? 2 : 1;
    if (!user) entries[0] = entries[1];
    ULONG registration = 0;
    if (user || common)
        registration = SHChangeNotifyRegister(window, SHCNRF_ShellLevel | SHCNRF_InterruptLevel,
            SHCNE_CREATE | SHCNE_DELETE | SHCNE_MKDIR | SHCNE_RMDIR | SHCNE_RENAMEITEM | SHCNE_RENAMEFOLDER |
                SHCNE_UPDATEDIR | SHCNE_ATTRIBUTES, message, count, entries);
    CoTaskMemFree(user);
    CoTaskMemFree(common);
    return registration;
}

void StopWatching(ULONG registration) {
    if (registration) SHChangeNotifyDeregister(registration);
}
}  // namespace lume::platform::windows::dock_sources
