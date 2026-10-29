#include "platform/windows/mpv_process_controller.h"
#include "platform/windows/text_util.h"
#include "platform/windows/diagnostics/runtime_log.h"

#include "platform/windows/wallpaper/static_image.h"
#include "platform/windows/wallpaper/wallpaper_pipe.h"
#include "vendor/nlohmann/json.hpp"
#include <psapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace lume::platform::windows {
namespace {
using Json = nlohmann::json;
constexpr wchar_t kHostClass[] = L"Lume.Native.WallpaperHost";
constexpr DWORD kStartupTimeout = 10000;
constexpr DWORD kCommandTimeout = 1200;
constexpr DWORD kBalancedTimeout = 2400;
constexpr std::array<const char*, 4> kSamplingOptions = {"scale", "cscale", "dscale", "tscale"};
constexpr std::size_t kMaxReplyBytes = 65536;

class Handle {
public:
    Handle() = default;
    explicit Handle(HANDLE handle) : handle_(handle) {}
    ~Handle() { Reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : handle_(other.Release()) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) Reset(other.Release());
        return *this;
    }
    HANDLE Get() const { return handle_; }
    explicit operator bool() const { return handle_ && handle_ != INVALID_HANDLE_VALUE; }
    void Reset(HANDLE value = nullptr) {
        if (*this) CloseHandle(handle_);
        handle_ = value;
    }
private:
    HANDLE Release() { const auto result = handle_; handle_ = nullptr; return result; }
    HANDLE handle_ = nullptr;
};

bool OwnedWindow(HWND window) {
    DWORD processId = 0;
    return window && IsWindow(window) &&
        GetWindowThreadProcessId(window, &processId) != 0 &&
        processId == GetCurrentProcessId();
}

std::optional<std::wstring> FromUtf8(const std::string& value) {
    if (value.empty() || value.size() > 32767 || value.find('\0') != std::string::npos) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length)) return {};
    return result;
}

using lume::text::Utf8;

std::optional<std::filesystem::path> LocalFile(const std::wstring& value) {
    if (value.empty() || value.find(L'\0') != std::wstring::npos) return {};
    if (value.find(L"\\\\") == 0 || value.find(L"//") == 0 ||
        value.find(L"://") != std::wstring::npos || value.find(L':', 2) != std::wstring::npos) return {};
    std::error_code error;
    auto path = std::filesystem::weakly_canonical(std::filesystem::path(value), error);
    if (error || !path.is_absolute() || path.wstring().find(L"\\\\") == 0) return {};
    const auto drive = GetDriveTypeW(path.root_path().c_str());
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK &&
        drive != DRIVE_CDROM) return {};
    if (!std::filesystem::is_regular_file(path, error) || error) return {};
    return path;
}

bool SamePath(const std::string& left, const std::string& right) {
    if (left.find("http://") == 0 || right.find("http://") == 0) return left == right;
    const auto a = FromUtf8(left);
    const auto b = FromUtf8(right);
    if (!a || !b) return false;
    const auto ap = std::filesystem::path(*a).lexically_normal().wstring();
    const auto bp = std::filesystem::path(*b).lexically_normal().wstring();
    return CompareStringOrdinal(ap.c_str(), -1, bp.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring QuoteArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    std::size_t slashes = 0;
    for (const auto character : value) {
        if (character == L'\\') {
            ++slashes;
        } else {
            quoted.append(slashes * (character == L'"' ? 2 : 1), L'\\');
            slashes = 0;
            if (character == L'"') quoted += L'\\';
            quoted += character;
        }
    }
    quoted.append(slashes * 2, L'\\');
    quoted += L'"';
    return quoted;
}

void PumpHostMessages(HWND host) {
    if (!OwnedWindow(host)) return;
    MSG message{};
    while (PeekMessageW(&message, host, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void BriefWait(HWND host, HANDLE signal, DWORD milliseconds = 15) {
    PumpHostMessages(host);
    if (signal) WaitForSingleObject(signal, milliseconds);
    else Sleep(milliseconds);
}

// Native static hosts store their fitted bitmap in GWLP_USERDATA; mpv hosts
// leave it null. The Player owning the bitmap destroys the host first.
const wallpaper::StaticImage* HostImage(HWND window) {
    return reinterpret_cast<const wallpaper::StaticImage*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

LRESULT CALLBACK WallpaperHostProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_ERASEBKGND) return 1;
    if (const auto* image = HostImage(window)) {
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{};
            if (const auto dc = BeginPaint(window, &paint)) {
                RECT client{};
                GetClientRect(window, &client);
                wallpaper::PaintStaticImage(dc, client, *image);
                EndPaint(window, &paint);
            }
            return 0;
        }
        if (message == WM_PRINTCLIENT) {
            RECT client{};
            GetClientRect(window, &client);
            wallpaper::PaintStaticImage(reinterpret_cast<HDC>(wparam), client, *image);
            return 0;
        }
    }
    if (message == WM_SIZE) {
        RECT client{};
        GetClientRect(window, &client);
        for (auto child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            SetWindowPos(child, nullptr, 0, 0, client.right, client.bottom,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool RegisterHostClass() {
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(GetModuleHandleW(nullptr), kHostClass, &existing)) return true;
    WNDCLASSEXW descriptor{};
    descriptor.cbSize = sizeof(descriptor);
    descriptor.hInstance = GetModuleHandleW(nullptr);
    descriptor.lpfnWndProc = WallpaperHostProcedure;
    descriptor.lpszClassName = kHostClass;
    return RegisterClassExW(&descriptor) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

struct DesktopParent {
    HWND parent = nullptr;
    HWND shellView = nullptr;
    HWND worker = nullptr;
    bool raised = false;
};

DesktopParent FindDesktopParent(bool refresh = false) {
    DesktopParent result;
    const auto progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return result;
    const auto spawn = [&] {
        DWORD_PTR reply = 0;
        SendMessageTimeoutW(progman, 0x052C, 0xD, 1,
                            SMTO_ABORTIFHUNG | SMTO_NORMAL, 1000, &reply);
    };
    if (refresh) spawn();
    const auto discover = [&] {
        result = {};
        result.shellView = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
        EnumWindows([](HWND top, LPARAM context) -> BOOL {
            auto& target = *reinterpret_cast<DesktopParent*>(context);
            const auto view = FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr);
            if (view) {
                target.shellView = view;
                target.worker = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&result));
        if ((GetWindowLongPtrW(progman, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0 &&
            result.shellView) {
            const auto worker = FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
            if (worker) {
                result.worker = worker;
                result.parent = progman;
                result.raised = true;
                return;
            }
        }
        result.parent = result.worker;
    };
    discover();
    if (!result.parent && !refresh) {
        spawn();
        discover();
    }
    return result;
}

bool IsStaticImage(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
    return extension == L".jpg" || extension == L".jpeg" || extension == L".png" ||
        extension == L".bmp" || extension == L".gif" || extension == L".webp";
}

bool IsVideoMedia(const std::string& value) {
    const auto dot = value.find_last_of('.');
    if (dot == std::string::npos) return false;
    auto extension = value.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".mp4" || extension == ".webm" || extension == ".mov" ||
        extension == ".m4v" || extension == ".mkv" || extension == ".avi";
}

std::wstring NewPipeName() {
    static std::atomic<std::uint64_t> sequence{0};
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return L"\\\\.\\pipe\\lume-native-mpv-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(counter.QuadPart) + L"-" + std::to_wstring(++sequence);
}

bool ValidMediaPrefix(const std::string& prefix) {
    constexpr char origin[] = "http://127.0.0.1:";
    if (prefix.size() > 256 || prefix.compare(0, sizeof(origin) - 1, origin) != 0 ||
        prefix.back() != '/') return false;
    const auto start = sizeof(origin) - 1;
    const auto slash = prefix.find('/', start);
    if (slash == std::string::npos || slash == start || slash - start > 5) return false;
    unsigned port = 0;
    for (auto index = start; index < slash; ++index) {
        if (prefix[index] < '0' || prefix[index] > '9') return false;
        port = port * 10u + static_cast<unsigned>(prefix[index] - '0');
    }
    if (port == 0 || port > 65535 || slash + 17 > prefix.size()) return false;
    for (auto index = slash + 1; index < prefix.size() - 1; ++index) {
        const auto character = prefix[index];
        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '-' || character == '_')) return false;
    }
    return true;
}

bool AllowedMediaExtension(const std::string& value) {
    auto extension = value.substr(value.find_last_of('.') == std::string::npos
        ? value.size() : value.find_last_of('.'));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension == ".mp4" || extension == ".webm" || extension == ".mov" ||
        extension == ".m4v" || extension == ".mkv" || extension == ".avi" ||
        extension == ".jpg" || extension == ".jpeg" || extension == ".png" ||
        extension == ".bmp" || extension == ".gif" || extension == ".webp";
}

std::uint64_t DisplayId(const std::wstring& name) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto character : name) {
        const auto value = static_cast<std::uint16_t>(std::towupper(character));
        hash ^= value & 0xffu;
        hash *= 1099511628211ull;
        hash ^= value >> 8u;
        hash *= 1099511628211ull;
    }
    return hash == 0 ? 1 : hash;
}

struct Player {
    Handle process;
    Handle job;
    Handle pipe;
    DWORD processId = 0;
    HWND host = nullptr;
    std::wstring pipeName;
    std::string replies;
    std::string media;
    bool videoMedia = false;
    bool loaded = false;
    bool streamPlugin = false;  // mpv loaded lume-mpv-stream.dll (lume:// URLs)
    bool paused = false;
    int volume = 0;
    bool cover = true;
    // Native static backend: no process, pipe or job. The source is kept only
    // to re-fit the bitmap after fit or size changes; it holds no plaintext.
    std::unique_ptr<wallpaper::StaticImage> image;
    std::optional<wallpaper::StaticImageSource> source;

    bool Native() const { return image != nullptr; }
    // mpv process state only; native static hosts never count as a process.
    bool Running() const {
        return process && WaitForSingleObject(process.Get(), 0) == WAIT_TIMEOUT;
    }
    bool Active() const { return Native() ? OwnedWindow(host) : Running(); }
    ~Player() {
        pipe.Reset();
        // This job contains only this display's suspended-then-assigned player.
        job.Reset();
        if (process) WaitForSingleObject(process.Get(), 500);
        process.Reset();
        if (OwnedWindow(host)) {
            if (GetWindowThreadProcessId(host, nullptr) == GetCurrentThreadId()) DestroyWindow(host);
            else {
                DWORD_PTR ignored = 0;
                SendMessageTimeoutW(host, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 300, &ignored);
            }
        }
    }
};

bool IsExpectedWindowParent(HWND window) {
    std::array<wchar_t, 64> className{};
    if (!IsWindow(window) || !GetClassNameW(window, className.data(),
                                           static_cast<int>(className.size()))) return false;
    return _wcsicmp(className.data(), L"WorkerW") == 0 || _wcsicmp(className.data(), L"Progman") == 0;
}
}  // namespace

std::vector<MpvDisplay> EnumerateMpvDisplays() {
    std::vector<MpvDisplay> displays;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            auto& values = *reinterpret_cast<std::vector<MpvDisplay>*>(context);
            const std::wstring name(info.szDevice);
            values.push_back({DisplayId(name), name, info.rcMonitor,
                              (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&displays));
    std::sort(displays.begin(), displays.end(), [](const MpvDisplay& a, const MpvDisplay& b) {
        return a.primary != b.primary ? a.primary : a.id < b.id;
    });
    return displays;
}

struct MpvProcessController::Impl {
    std::wstring executable;
    HWND verificationParent = nullptr;
    DWORD ownerThread = 0;
    const std::atomic<bool>* cancellation = nullptr;
    std::vector<std::string> trustedMediaPrefixes;
    std::map<std::string, wallpaper::StaticImageSource> staticSources;
    std::uint64_t nextRequest = 0;
    std::map<std::uint64_t, std::unique_ptr<Player>> players;
    std::string error;
    bool balancedMode = false;
    bool rollingBack = false;

    bool Fail(const char* value) { error = value; return false; }
    bool Cancelled() const {
        return !rollingBack && cancellation && cancellation->load(std::memory_order_relaxed);
    }
    bool CheckCancellation() {
        return !Cancelled() || Fail("MPV_CANCELLED");
    }
    bool CheckThread() {
        if (!ownerThread) ownerThread = GetCurrentThreadId();
        return ownerThread == GetCurrentThreadId() || Fail("MPV_WRONG_THREAD");
    }
    std::optional<MpvDisplay> FindDisplay(std::uint64_t id) {
        for (const auto& display : EnumerateMpvDisplays()) {
            if (id && display.id == id) return display;
        }
        Fail("MPV_DISPLAY_NOT_FOUND");
        return {};
    }
    Player* FindPlayer(std::uint64_t id) {
        const auto found = players.find(id);
        if (found == players.end() || !found->second->Active() ||
            (!found->second->Native() && !found->second->pipe)) {
            Fail("MPV_NOT_RUNNING");
            return nullptr;
        }
        return found->second.get();
    }

    // Size of the host client area Attach() will produce for this display.
    bool TargetSize(const MpvDisplay& display, int& width, int& height) {
        RECT target = display.bounds;
        if (verificationParent) {
            if (!OwnedWindow(verificationParent)) return Fail("MPV_VERIFICATION_PARENT_NOT_OWNED");
            if (!GetClientRect(verificationParent, &target)) return Fail("MPV_HOST_BOUNDS_FAILED");
        }
        width = target.right - target.left;
        height = target.bottom - target.top;
        return (width > 0 && height > 0) || Fail("MPV_HOST_BOUNDS_FAILED");
    }

    // Decodes `media` for a native host. Unsupported means the compatibility
    // player should be used; Failed has already set `error`.
    wallpaper::StaticImageLoad LoadNative(const std::string& media,
        const wallpaper::StaticImageSource* source, int width, int height, bool cover,
        std::unique_ptr<wallpaper::StaticImage>& output) {
        const auto hint = FromUtf8(media);
        if (!hint) { Fail("STATIC_IMAGE_PATH_INVALID"); return wallpaper::StaticImageLoad::Failed; }
        auto image = std::make_unique<wallpaper::StaticImage>();
        std::string detail;
        const auto result = wallpaper::LoadStaticImage(*hint, source, width, height, cover,
            rollingBack ? nullptr : cancellation, *image, detail);
        if (result == wallpaper::StaticImageLoad::Loaded) output = std::move(image);
        else if (result == wallpaper::StaticImageLoad::Failed)
            Fail(Cancelled() ? "MPV_CANCELLED" : "STATIC_IMAGE_DECODE_FAILED");
        return result;
    }

    void ShowNative(Player& player) {
        SetWindowLongPtrW(player.host, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(player.image.get()));
        InvalidateRect(player.host, nullptr, FALSE);
        UpdateWindow(player.host);
    }

    // Re-fits a native bitmap after a fit or size change. The old bitmap stays
    // on screen (GDI scales it) if the new one cannot be prepared.
    bool RefitNative(Player& player, bool cover, bool force) {
        RECT client{};
        if (!OwnedWindow(player.host) || !GetClientRect(player.host, &client))
            return Fail("MPV_HOST_WINDOW_LOST");
        if (!force && cover == player.cover && client.right == player.image->canvasWidth &&
            client.bottom == player.image->canvasHeight) return true;
        std::unique_ptr<wallpaper::StaticImage> image;
        if (LoadNative(player.media, player.source ? &*player.source : nullptr,
                       client.right, client.bottom, cover, image) !=
            wallpaper::StaticImageLoad::Loaded) {
            if (error.empty()) Fail("STATIC_IMAGE_DECODE_FAILED");
            return false;
        }
        // Publish the new pointer before the old bitmap is released.
        auto previous = std::move(player.image);
        player.image = std::move(image);
        player.cover = cover;
        ShowNative(player);
        return true;
    }

    // Shows an already decoded bitmap. A new host is attached before the old
    // backend (native or mpv) is stopped, so failures keep the old wallpaper.
    bool StartNative(std::uint64_t id, const MpvDisplay& display, const std::string& media,
                     std::unique_ptr<wallpaper::StaticImage> image,
                     std::optional<wallpaper::StaticImageSource> source) {
        const auto found = players.find(id);
        Player* existing = found == players.end() ? nullptr : found->second.get();
        if (existing && existing->Native() && OwnedWindow(existing->host)) {
            if (!Attach(*existing, display)) return false;
            auto previous = std::move(existing->image);
            existing->image = std::move(image);
            existing->source = std::move(source);
            existing->media = media;
            ShowNative(*existing);
            error.clear();
            return true;
        }
        if (!CheckCancellation()) return false;
        if (!RegisterHostClass()) return Fail("MPV_HOST_CLASS_FAILED");
        auto player = std::make_unique<Player>();
        if (existing) {
            player->volume = existing->volume;
            player->cover = existing->cover;
            player->paused = existing->paused;
        }
        player->host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kHostClass, L"",
                                       WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                       -32000, -32000, 1, 1, nullptr, nullptr,
                                       GetModuleHandleW(nullptr), nullptr);
        if (!player->host) return Fail("MPV_HOST_WINDOW_FAILED");
        player->image = std::move(image);
        player->source = std::move(source);
        player->media = media;
        player->loaded = true;
        // Paint content before the host becomes visible inside the desktop.
        SetWindowLongPtrW(player->host, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(player->image.get()));
        if (!Attach(*player, display)) return false;  // ~Player destroys the new host.
        ShowNative(*player);
        if (existing) StopPlayer(*existing);
        players[id] = std::move(player);
        error.clear();
        return true;
    }

    bool Attach(Player& player, const MpvDisplay& display) {
        if (!OwnedWindow(player.host)) return Fail("MPV_HOST_WINDOW_LOST");
        DesktopParent desktop;
        RECT target = display.bounds;
        if (verificationParent) {
            if (!OwnedWindow(verificationParent)) return Fail("MPV_VERIFICATION_PARENT_NOT_OWNED");
            desktop.parent = verificationParent;
            if (!GetClientRect(verificationParent, &target)) return Fail("MPV_HOST_BOUNDS_FAILED");
        } else {
            // Explorer can hide or replace the wallpaper layer at any time
            // (theme or slideshow change, Explorer refresh, leaving some full
            // screen programs). A player left in a hidden or stale layer still
            // "runs" while the desktop shows the Windows wallpaper, so every
            // check finds the layer Explorer uses now, not just any WorkerW.
            const auto parent = GetParent(player.host);
            desktop = FindDesktopParent();
            if (desktop.parent && !IsWindowVisible(desktop.parent)) {
                const auto hidden = desktop.parent;
                desktop = FindDesktopParent(true);
                if (desktop.parent && !IsWindowVisible(desktop.parent)) {
                    // The wallpaper layer is only ever ours to show.
                    ShowWindow(desktop.parent, SW_SHOWNA);
                    diag::Log("wallpaper", "desktop wallpaper layer was hidden; shown again");
                } else if (desktop.parent != hidden) {
                    diag::Log("wallpaper", "desktop wallpaper layer was hidden; Explorer made a new one");
                }
            }
            if (!desktop.parent && IsExpectedWindowParent(parent) && IsWindowVisible(parent)) {
                // Mid-change Explorer may briefly have none: keep the current one.
                desktop.parent = parent;
                std::array<wchar_t, 64> className{};
                GetClassNameW(parent, className.data(), static_cast<int>(className.size()));
                desktop.raised = _wcsicmp(className.data(), L"Progman") == 0;
            }
            if (!desktop.parent) return Fail("MPV_DESKTOP_NOT_READY");
            if (parent && parent != desktop.parent && IsExpectedWindowParent(parent))
                diag::Log("wallpaper", "player moved to the current desktop wallpaper layer");
            RECT parentBounds{};
            if (!GetWindowRect(desktop.parent, &parentBounds)) return Fail("MPV_HOST_BOUNDS_FAILED");
            OffsetRect(&target, -parentBounds.left, -parentBounds.top);
        }
        if (target.right <= target.left || target.bottom <= target.top) return Fail("MPV_HOST_BOUNDS_FAILED");
        if (GetParent(player.host) != desktop.parent) {
            auto style = GetWindowLongPtrW(player.host, GWL_STYLE);
            style = (style | WS_CHILD) & ~static_cast<LONG_PTR>(WS_POPUP);
            SetWindowLongPtrW(player.host, GWL_STYLE, style);
            auto extended = GetWindowLongPtrW(player.host, GWL_EXSTYLE);
            if (desktop.raised) extended |= WS_EX_LAYERED;
            else extended &= ~static_cast<LONG_PTR>(WS_EX_LAYERED);
            SetWindowLongPtrW(player.host, GWL_EXSTYLE, extended);
            if (desktop.raised && !SetLayeredWindowAttributes(player.host, 0, 255, LWA_ALPHA))
                return Fail("MPV_DESKTOP_ATTACH_FAILED");
            SetLastError(ERROR_SUCCESS);
            if (!SetParent(player.host, desktop.parent) && GetLastError() != ERROR_SUCCESS)
                return Fail("MPV_DESKTOP_ATTACH_FAILED");
        }
        if (!SetWindowPos(player.host, nullptr, target.left, target.top,
                          target.right - target.left, target.bottom - target.top,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW))
            return Fail("MPV_DESKTOP_ATTACH_FAILED");
        if (desktop.raised && desktop.shellView) {
            SetWindowPos(player.host, desktop.shellView, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (desktop.worker) SetWindowPos(desktop.worker, HWND_BOTTOM, 0, 0, 0, 0,
                                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        if (GetParent(player.host) != desktop.parent) return Fail("MPV_DESKTOP_ATTACH_FAILED");
        RECT actual{};
        if (!GetWindowRect(player.host, &actual)) return Fail("MPV_DESKTOP_ATTACH_FAILED");
        MapWindowPoints(nullptr, desktop.parent, reinterpret_cast<POINT*>(&actual), 2);
        if (actual.left != target.left || actual.top != target.top ||
            actual.right != target.right || actual.bottom != target.bottom)
            return Fail("MPV_DESKTOP_ATTACH_FAILED");
        return true;
    }

    bool Transfer(Player& player, bool write, void* buffer, DWORD count,
                  DWORD& transferred, ULONGLONG deadline) {
        if (!CheckCancellation()) return false;
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event) return Fail("MPV_IPC_EVENT_FAILED");
        OVERLAPPED pending{};
        pending.hEvent = event.Get();
        const auto started = write
            ? WriteFile(player.pipe.Get(), buffer, count, &transferred, &pending)
            : ReadFile(player.pipe.Get(), buffer, count, &transferred, &pending);
        if (started) return true;
        if (GetLastError() != ERROR_IO_PENDING) return Fail("MPV_IPC_DISCONNECTED");
        bool complete = false;
        while (GetTickCount64() < deadline && player.Running() && !Cancelled()) {
            PumpHostMessages(player.host);
            if (WaitForSingleObject(event.Get(), 15) == WAIT_OBJECT_0) {
                complete = true;
                break;
            }
        }
        if (!complete) {
            // Await cancellation completion before the stack OVERLAPPED/buffer go away.
            CancelIoEx(player.pipe.Get(), &pending);
            DWORD ignored = 0;
            GetOverlappedResult(player.pipe.Get(), &pending, &ignored, TRUE);
            return Fail(Cancelled() ? "MPV_CANCELLED" :
                player.Running() ? "MPV_IPC_TIMEOUT" : "MPV_PROCESS_EXITED");
        }
        return GetOverlappedResult(player.pipe.Get(), &pending, &transferred, FALSE) != FALSE ||
            Fail("MPV_IPC_DISCONNECTED");
    }

    bool Request(Player& player, const Json& command, Json* data = nullptr,
                 ULONGLONG deadline = 0) {
        if (!CheckCancellation()) return false;
        if (!player.Running() || !player.pipe) return Fail("MPV_NOT_RUNNING");
        if (!deadline) deadline = GetTickCount64() + kCommandTimeout;
        const auto requestId = ++nextRequest;
        auto payload = Json{{"command", command}, {"request_id", requestId}}.dump() + "\n";
        DWORD written = 0;
        if (!Transfer(player, true, payload.data(), static_cast<DWORD>(payload.size()), written, deadline)) return false;
        if (written != payload.size()) return Fail("MPV_IPC_WRITE_FAILED");
        while (GetTickCount64() < deadline) {
            if (!CheckCancellation()) return false;
            auto newline = player.replies.find('\n');
            if (newline == std::string::npos) {
                std::array<char, 4096> buffer{};
                DWORD read = 0;
                if (!Transfer(player, false, buffer.data(), static_cast<DWORD>(buffer.size()), read, deadline))
                    return false;
                if (!read) return Fail("MPV_IPC_DISCONNECTED");
                player.replies.append(buffer.data(), read);
                if (player.replies.size() > kMaxReplyBytes) return Fail("MPV_IPC_REPLY_TOO_LARGE");
                continue;
            }
            const auto line = player.replies.substr(0, newline);
            player.replies.erase(0, newline + 1);
            if (line.empty()) continue;
            Json reply;
            try {
                reply = Json::parse(line, [](int depth, Json::parse_event_t, Json&) {
                    if (depth > 32) throw std::runtime_error("reply depth");
                    return true;
                }, false);
            } catch (const std::exception&) { return Fail("MPV_IPC_INVALID_REPLY"); }
            if (!reply.is_object()) return Fail("MPV_IPC_INVALID_REPLY");
            const auto event = reply.find("event");
            if (event != reply.end() && event->is_string()) {
                if (*event == "file-loaded") player.loaded = true;
                if (*event == "start-file") player.loaded = false;
            }
            const auto id = reply.find("request_id");
            if (id == reply.end()) continue;
            if (!id->is_number_unsigned() && !id->is_number_integer()) return Fail("MPV_IPC_INVALID_REPLY");
            if (id->get<std::uint64_t>() != requestId) continue;
            const auto status = reply.find("error");
            if (status == reply.end() || !status->is_string()) return Fail("MPV_IPC_INVALID_REPLY");
            if (*status != "success") return Fail("MPV_COMMAND_REJECTED");
            if (data) {
                const auto value = reply.find("data");
                *data = value == reply.end() ? Json{} : *value;
            }
            return true;
        }
        return Fail("MPV_IPC_TIMEOUT");
    }

    bool Connect(Player& player, ULONGLONG deadline) {
        while (GetTickCount64() < deadline && player.Running() && !Cancelled()) {
            player.pipe.Reset(CreateFileW(player.pipeName.c_str(), GENERIC_READ | GENERIC_WRITE,
                                          0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
            if (player.pipe) {
                ULONG server = 0;
                if (!GetNamedPipeServerProcessId(player.pipe.Get(), &server) || server != player.processId) {
                    player.pipe.Reset();
                    return Fail("MPV_IPC_OWNER_MISMATCH");
                }
                return true;
            }
            const auto failure = GetLastError();
            if (failure != ERROR_FILE_NOT_FOUND && failure != ERROR_PIPE_BUSY)
                return Fail("MPV_IPC_CONNECT_FAILED");
            BriefWait(player.host, player.process.Get());
        }
        return Fail(Cancelled() ? "MPV_CANCELLED" :
            player.Running() ? "MPV_IPC_CONNECT_TIMEOUT" : "MPV_PROCESS_EXITED");
    }

    bool Ready(Player& player, const std::string& media, ULONGLONG deadline) {
        while (GetTickCount64() < deadline && player.Running() && !Cancelled()) {
            Json path;
            Json frame;
            const auto commandDeadline = (std::min)(deadline, GetTickCount64() + kCommandTimeout);
            if (Request(player, Json::array({"get_property", "path"}), &path, commandDeadline) &&
                path.is_string() && SamePath(path.get<std::string>(), media) &&
                Request(player, Json::array({"get_property", "video-frame-info"}), &frame,
                        (std::min)(deadline, GetTickCount64() + kCommandTimeout)) &&
                player.loaded && frame.is_object() && !frame.empty()) {
                PumpHostMessages(player.host);
                error.clear();
                return true;
            }
            if (error == "MPV_IPC_TIMEOUT" || error == "MPV_IPC_DISCONNECTED" ||
                error == "MPV_PROCESS_EXITED" || error == "MPV_IPC_INVALID_REPLY" ||
                error == "MPV_CANCELLED") return false;
            BriefWait(player.host, player.process.Get(), 20);
        }
        return Fail(Cancelled() ? "MPV_CANCELLED" :
            player.Running() ? "MPV_FIRST_FRAME_TIMEOUT" : "MPV_PROCESS_EXITED");
    }

    bool Load(Player& player, const std::string& media) {
        const auto deadline = GetTickCount64() + kStartupTimeout;
        player.loaded = false;
        // Protected media goes through the stream plugin's named pipe rather
        // than loopback HTTP, which some security software blocks for mpv.
        const auto piped = player.streamPlugin ? wallpaper_pipe::PlayerUrl(media) : std::string{};
        const auto& target = piped.empty() ? media : piped;
        if (!Request(player, Json::array({"loadfile", target, "replace"}), nullptr,
                     (std::min)(deadline, GetTickCount64() + kCommandTimeout))) return false;
        return Ready(player, target, deadline);
    }

    bool SwitchMedia(Player& player, const std::string& media,
                     bool& previousMediaPreserved) {
        previousMediaPreserved = false;
        const auto previousMedia = player.media;
        const auto previousVideoMedia = player.videoMedia;
        const auto previousPaused = player.paused;
        const bool videoMedia = IsVideoMedia(media);
        const bool mediaKindChanged = player.videoMedia != videoMedia;
        if ((!mediaKindChanged || !player.videoMedia ||
             ConfigureBalancedFilter(player, false, GetTickCount64() + kCommandTimeout)) &&
            Load(player, media)) {
            player.videoMedia = videoMedia;
            if ((!mediaKindChanged ||
                 ConfigureBalancedFilter(player, balancedMode && videoMedia,
                                         GetTickCount64() + kCommandTimeout)) &&
                Pause(player, previousPaused)) {
                player.media = media;
                return true;
            }
            player.videoMedia = previousVideoMedia;
        } else {
            player.videoMedia = previousVideoMedia;
        }

        const auto failure = error;
        if (!Cancelled()) {
            // Ready must evaluate restoration, not the previous switch failure.
            error.clear();
            // A timed-out filter write can arrive late. Drain it and remove the
            // filter before loading the old image, then restore the old policy.
            previousMediaPreserved =
                ConfigureBalancedFilter(player, false, GetTickCount64() + kCommandTimeout) &&
                Load(player, previousMedia);
            if (previousMediaPreserved) {
                player.videoMedia = previousVideoMedia;
                previousMediaPreserved =
                    ConfigureBalancedFilter(player, balancedMode && previousVideoMedia,
                                            GetTickCount64() + kCommandTimeout) &&
                    Pause(player, previousPaused);
            }
        }
        error = failure;
        return false;
    }

    bool Pause(Player& player, bool pause) {
        if (!Request(player, Json::array({"set_property", "pause", pause}))) return false;
        Json actual;
        if (!Request(player, Json::array({"get_property", "pause"}), &actual)) return false;
        if (!actual.is_boolean() || actual.get<bool>() != pause) return Fail("MPV_PAUSE_NOT_APPLIED");
        player.paused = pause;
        return true;
    }

    bool Volume(Player& player, int volume) {
        if (volume < 0 || volume > 100) return Fail("MPV_VOLUME_OUT_OF_RANGE");
        if (!Request(player, Json::array({"set_property", "mute", true})) ||
            !Request(player, Json::array({"set_property", "volume", volume})) ||
            !Request(player, Json::array({"set_property", "aid", volume ? "auto" : "no"})) ||
            (volume && !Request(player, Json::array({"set_property", "mute", false})))) return false;
        player.volume = volume;
        return true;
    }

    bool Fit(Player& player, bool cover) {
        if (!Request(player, Json::array({"set_property", "panscan", cover ? 1.0 : 0.0}))) return false;
        player.cover = cover;
        return true;
    }

    struct BalancedState {
        Player* player = nullptr;
        bool filtered = false;
        std::array<std::string, 4> sampling;
    };

    bool BalancedRequest(Player& player, const Json& command, Json* data, ULONGLONG deadline) {
        if (GetTickCount64() >= deadline) return Fail("MPV_IPC_TIMEOUT");
        return Request(player, command, data, (std::min)(deadline, GetTickCount64() + kCommandTimeout));
    }

    bool BalancedFilter(Player& player, bool& present, ULONGLONG deadline) {
        Json filters;
        if (!BalancedRequest(player, Json::array({"get_property", "vf"}), &filters, deadline)) return false;
        if (!filters.is_array()) return Fail("MPV_IPC_INVALID_REPLY");
        present = false;
        for (const auto& filter : filters) {
            if (!filter.is_object()) return Fail("MPV_IPC_INVALID_REPLY");
            const auto label = filter.find("label");
            if (label != filter.end() && label->is_string() && *label == "lume-balanced") {
                if (present) return Fail("MPV_BALANCED_FILTER_INVALID");
                present = true;
            }
        }
        return true;
    }

    bool ConfigureBalancedFilter(Player& player, bool enabled, ULONGLONG deadline) {
        bool present = false;
        if (!BalancedFilter(player, present, deadline)) return false;
        if (present &&
            !BalancedRequest(player, Json::array({"vf", "remove", "@lume-balanced"}), nullptr, deadline))
            return false;
        const bool shouldBePresent = enabled && player.videoMedia;
        if (shouldBePresent &&
            !BalancedRequest(player, Json::array({"vf", "add", "@lume-balanced:fps=30"}), nullptr, deadline))
            return false;
        bool actual = false;
        return BalancedFilter(player, actual, deadline) &&
            (actual == shouldBePresent || Fail("MPV_BALANCED_FILTER_INVALID"));
    }

    bool ApplyBalanced(const BalancedState& state, ULONGLONG deadline) {
        auto& player = *state.player;
        if (!ConfigureBalancedFilter(player, state.filtered, deadline)) return false;
        for (std::size_t index = 0; index < kSamplingOptions.size(); ++index)
            if (!BalancedRequest(player, Json::array({"set", kSamplingOptions[index], state.sampling[index]}),
                                 nullptr, deadline)) return false;
        return true;
    }

    bool SetBalancedMode(bool enabled) {
        error.clear();
        if (!CheckThread() || !CheckCancellation()) return false;
        if (balancedMode == enabled) return true;
        const auto deadline = GetTickCount64() + kBalancedTimeout;
        std::vector<BalancedState> previous;
        std::size_t changed = 0;
        bool success = false;
        try {
            // Snapshot every display before writing, including its actual
            // samplers, so partial failures do not replace unrelated options.
            for (auto& item : players) {
                if (!item.second->Running()) continue;
                BalancedState state; state.player = item.second.get();
                if (!BalancedFilter(*state.player, state.filtered, deadline)) return false;
                for (std::size_t index = 0; index < kSamplingOptions.size(); ++index) {
                    Json value;
                    if (!BalancedRequest(*state.player, Json::array({"get_property", kSamplingOptions[index]}),
                                         &value, deadline)) return false;
                    if (!value.is_string()) return Fail("MPV_IPC_INVALID_REPLY");
                    state.sampling[index] = value.get<std::string>();
                }
                previous.push_back(std::move(state));
            }
            success = true;
            for (const auto& state : previous) {
                BalancedState next = state;
                next.filtered = enabled && state.player->videoMedia;
                next.sampling = {"bilinear", "bilinear", enabled ? "bilinear" : "mitchell",
                                 enabled ? "linear" : "oversample"};
                ++changed; // The failing display may already have accepted some commands.
                if (!ApplyBalanced(next, deadline)) { success = false; break; }
            }
            success = success && CheckCancellation();
        } catch (...) {
            success = false;
            Fail("MPV_BALANCED_FAILED");
        }
        if (success) { balancedMode = enabled; return true; }
        const auto failure = error;
        const auto rollbackDeadline = GetTickCount64() + kBalancedTimeout;
        // Shutdown cancellation must not suppress the compensating writes.
        rollingBack = true;
        while (changed) {
            auto& state = previous[--changed];
            bool restored = false;
            try { restored = ApplyBalanced(state, rollbackDeadline); } catch (...) {}
            if (!restored) StopPlayer(*state.player, true);
        }
        rollingBack = false;
        error = failure;
        return false;
    }

    bool ResolveMedia(const std::string& path, std::string& media, bool& localVideo) {
        if (path.find("http://") == 0 || path.find("https://") == 0) {
            const auto prefix = std::find_if(trustedMediaPrefixes.begin(), trustedMediaPrefixes.end(),
                [&](const std::string& item) { return path.compare(0, item.size(), item) == 0; });
            if (prefix == trustedMediaPrefixes.end() || path.size() > 2048 || !AllowedMediaExtension(path))
                return Fail("MPV_MEDIA_URL_REJECTED");
            const auto tail = path.substr(prefix->size());
            if (tail.empty() || tail == "." || tail == ".." || tail.find("..") != std::string::npos)
                return Fail("MPV_MEDIA_URL_REJECTED");
            for (const auto character : tail) {
                if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') || character == '-' || character == '_' ||
                      character == '.')) return Fail("MPV_MEDIA_URL_REJECTED");
            }
            media = path;
            localVideo = false;
            return true;
        }
        const auto wide = FromUtf8(path);
        const auto source = wide ? LocalFile(*wide) : std::nullopt;
        if (!source) return Fail("MPV_MEDIA_NOT_FOUND");
        media = Utf8(source->wstring());
        if (media.empty() || !AllowedMediaExtension(media)) return Fail("MPV_MEDIA_PATH_INVALID");
        localVideo = !IsStaticImage(*source);
        return true;
    }

    void StopPlayer(Player& player, bool force = false) {
        if (player.Native()) {
            // Keep media and playback options so Recover/Start can reuse them.
            if (OwnedWindow(player.host)) DestroyWindow(player.host);
            player.host = nullptr;
            player.image.reset();
            player.source.reset();
            player.loaded = false;
            return;
        }
        if (player.Running() && player.pipe && !Cancelled() && !force) {
            const auto savedError = error;
            Request(player, Json::array({"quit"}), nullptr, GetTickCount64() + 250);
            error = savedError;
        }
        const auto deadline = GetTickCount64() + (Cancelled() || force ? 0 : 400);
        while (player.Running() && GetTickCount64() < deadline)
            BriefWait(player.host, player.process.Get(), 15);
        if (player.Running() && player.job) TerminateJobObject(player.job.Get(), 0);
        player.pipe.Reset();
        player.job.Reset();
        if (player.process) WaitForSingleObject(player.process.Get(), 500);
        player.process.Reset();
        if (OwnedWindow(player.host)) DestroyWindow(player.host);
        player.host = nullptr;
        player.replies.clear();
        player.loaded = false;
    }

    bool Launch(Player& player, const MpvDisplay& display, const std::string& media,
                bool localVideo, const std::filesystem::path& program) {
        if (!CheckCancellation()) return false;
        if (verificationParent && !OwnedWindow(verificationParent))
            return Fail("MPV_VERIFICATION_PARENT_NOT_OWNED");
        if (!RegisterHostClass()) return Fail("MPV_HOST_CLASS_FAILED");
        player.host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kHostClass, L"",
                                      WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                      -32000, -32000, 1, 1, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), nullptr);
        if (!player.host || !Attach(player, display)) return false;
        player.videoMedia = IsVideoMedia(media);
        player.job.Reset(CreateJobObjectW(nullptr, nullptr));
        if (!player.job) return Fail("MPV_JOB_CREATE_FAILED");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        // Backstop for a decoder or driver that leaks: past this, allocations
        // fail and mpv exits, and the session restarts it. The session's own
        // watchdog restarts the player well before (wallpaper_session.cpp).
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit = static_cast<SIZE_T>(4) << 30;
        if (!SetInformationJobObject(player.job.Get(), JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits))) return Fail("MPV_JOB_CONFIGURE_FAILED");
        player.pipeName = NewPipeName();
        // Only Lume's own stream plugin, from beside mpv.exe; no user scripts.
        // Installed beside mpv.exe; a build tree has it beside Lume's executable.
        std::error_code pluginError;
        auto plugin = program.parent_path() / L"lume-mpv-stream.dll";
        if (!std::filesystem::is_regular_file(plugin, pluginError)) {
            std::wstring self(32768, L'\0');
            self.resize(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
            plugin = std::filesystem::path(self).parent_path() / L"lume-mpv-stream.dll";
        }
        player.streamPlugin = std::filesystem::is_regular_file(plugin, pluginError);
        std::vector<std::wstring> arguments = {
            L"--no-config", L"--no-terminal", L"--title=Lume Wallpaper",
            L"--audio=no", L"--volume=0", L"--mute=yes", L"--msg-level=all=warn",
            L"--loop-file=inf", L"--keep-open=yes", L"--image-display-duration=inf",
            L"--demuxer-max-bytes=8MiB", L"--demuxer-max-back-bytes=2MiB",
            L"--media-controls=no", L"--show-in-taskbar=no", L"--force-window=yes",
            L"--no-window-dragging", L"--cursor-autohide=no", L"--stop-screensaver=no",
            L"--input-default-bindings=no", L"--no-border", L"--input-cursor=no", L"--no-osc",
            L"--access-references=no", L"--autoload-files=no", L"--ytdl=no", L"--load-scripts=no",
            player.streamPlugin ? L"--scripts=" + plugin.wstring() : std::wstring(L"--scripts="),
            L"--demuxer-lavf-o=protocol_whitelist=[file,http,tcp],max_redirects=0,http_proxy=[]",
            L"--stream-lavf-o=protocol_whitelist=[file,http,tcp],max_redirects=0,http_proxy=[]",
            L"--video-unscaled=no", L"--keepaspect=yes", L"--panscan=1.0",
            L"--hwdec=auto-safe", L"--hwdec-extra-frames=2", L"--idle=yes",
            L"--input-ipc-server=" + player.pipeName,
            L"--wid=" + std::to_wstring(reinterpret_cast<std::uintptr_t>(player.host))
        };
        if (localVideo) arguments.push_back(L"--d3d11va-zero-copy=yes");
        if (balancedMode && player.videoMedia) arguments.push_back(L"--vf-add=@lume-balanced:fps=30");
        arguments.push_back(L"--scale=bilinear");
        arguments.push_back(L"--cscale=bilinear");
        arguments.push_back(balancedMode ? L"--dscale=bilinear" : L"--dscale=mitchell");
        arguments.push_back(balancedMode ? L"--tscale=linear" : L"--tscale=oversample");
        auto commandLine = QuoteArgument(program.wstring());
        for (const auto& argument : arguments) commandLine += L" " + QuoteArgument(argument);
        if (commandLine.size() >= 32767) return Fail("MPV_COMMAND_LINE_TOO_LONG");
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(program.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                            nullptr, program.parent_path().c_str(), &startup, &process))
            return Fail("MPV_PROCESS_START_FAILED");
        player.process.Reset(process.hProcess);
        player.processId = process.dwProcessId;
        Handle thread(process.hThread);
        if (!AssignProcessToJobObject(player.job.Get(), player.process.Get())) {
            TerminateProcess(player.process.Get(), 1);
            return Fail("MPV_JOB_ASSIGN_FAILED");
        }
        if (!CheckCancellation()) return false;
        if (ResumeThread(thread.Get()) == static_cast<DWORD>(-1))
            return Fail("MPV_PROCESS_RESUME_FAILED");
        if (!Connect(player, GetTickCount64() + kStartupTimeout)) return false;
        if (!Load(player, media)) return false;
        player.media = media;
        return true;
    }

    bool Start(std::uint64_t id, const std::string& path) {
        error.clear();
        if (!CheckThread() || !CheckCancellation()) return false;
        const auto display = FindDisplay(id);
        if (!display) return false;
        std::string media;
        bool localVideo = false;
        if (!ResolveMedia(path, media, localVideo)) return false;
        if (!IsVideoMedia(media)) {
            // Single-frame images are decoded at the target size and shown by a
            // native GDI host. Loopback images need their exact registered
            // reader; images without a usable system decoder (animated GIF,
            // missing WebP codec, unregistered URLs) keep the mpv path.
            const bool remote = media.find("http://") == 0;
            const auto registered = remote ? staticSources.find(media) : staticSources.end();
            if (!remote || registered != staticSources.end()) {
                const auto existing = players.find(id);
                const bool cover = existing == players.end() || existing->second->cover;
                int width = 0, height = 0;
                if (!TargetSize(*display, width, height)) return false;
                std::optional<wallpaper::StaticImageSource> source;
                if (registered != staticSources.end()) source = registered->second;
                std::unique_ptr<wallpaper::StaticImage> image;
                const auto load = LoadNative(media, source ? &*source : nullptr,
                                             width, height, cover, image);
                if (load == wallpaper::StaticImageLoad::Loaded)
                    return StartNative(id, *display, media, std::move(image), std::move(source));
                // A corrupt image or cancellation leaves the current wallpaper as is.
                if (load == wallpaper::StaticImageLoad::Failed) return false;
                error.clear();
            }
        }
        const auto program = LocalFile(executable);
        if (!program) return Fail("MPV_EXECUTABLE_NOT_FOUND");
        auto found = players.find(id);
        if (found != players.end() && found->second->Native()) {
            // Native → mpv: the new player must be ready before the native host closes.
            auto& native = *found->second;
            auto player = std::make_unique<Player>();
            if (!Launch(*player, *display, media, localVideo, *program) ||
                (native.volume && !Volume(*player, native.volume)) ||
                (!native.cover && !Fit(*player, false)) ||
                (native.paused && !Pause(*player, true))) {
                const auto failure = error;
                StopPlayer(*player, true);
                error = failure;
                return false;
            }
            StopPlayer(native);
            found->second = std::move(player);
            error.clear();
            return true;
        }
        if (found != players.end() && found->second->Running() && OwnedWindow(found->second->host)) {
            auto& player = *found->second;
            const bool mediaChanged = !SamePath(player.media, media);
            bool previousMediaPreserved = false;
            if (!Attach(player, *display) ||
                (mediaChanged ? !SwitchMedia(player, media, previousMediaPreserved)
                              : !Pause(player, player.paused))) {
                if (!previousMediaPreserved) StopPlayer(player);
                return false;
            }
            error.clear();
            return true;
        }
        int volume = 0;
        bool cover = true;
        bool paused = false;
        if (found != players.end()) {
            volume = found->second->volume;
            cover = found->second->cover;
            paused = found->second->paused;
            StopPlayer(*found->second);
        }
        auto player = std::make_unique<Player>();
        if (!Launch(*player, *display, media, localVideo, *program) ||
            (volume && !Volume(*player, volume)) || (!cover && !Fit(*player, false)) ||
            (paused && !Pause(*player, true))) {
            // Existing remembered media can still be explicitly recovered.
            return false;
        }
        players[id] = std::move(player);
        error.clear();
        return true;
    }
};

MpvProcessController::MpvProcessController(std::wstring executable, HWND verificationParent)
    : impl_(std::make_unique<Impl>()) {
    impl_->executable = std::move(executable);
    impl_->verificationParent = verificationParent;
}

MpvProcessController::~MpvProcessController() {
    for (auto& item : impl_->players) impl_->StopPlayer(*item.second);
}

bool MpvProcessController::Start(std::uint64_t id, const std::string& mediaPath) {
    return impl_->Start(id, mediaPath);
}

bool MpvProcessController::Command(std::uint64_t id, lume::wallpaper::MpvCommand command) {
    impl_->error.clear();
    if (!impl_->CheckThread()) return false;
    const auto player = impl_->FindPlayer(id);
    if (!player) return false;
    if (command != lume::wallpaper::MpvCommand::Pause && command != lume::wallpaper::MpvCommand::Resume)
        return impl_->Fail("MPV_COMMAND_INVALID");
    const bool pause = command == lume::wallpaper::MpvCommand::Pause;
    if (player->Native()) {
        // A still image has nothing to pause; remember it for a later mpv switch.
        player->paused = pause;
        return true;
    }
    return impl_->Pause(*player, pause);
}

void MpvProcessController::Stop(std::uint64_t id) {
    if (!impl_->CheckThread()) return;
    const auto found = impl_->players.find(id);
    if (found == impl_->players.end()) return;
    impl_->StopPlayer(*found->second);
    impl_->players.erase(found);
}

bool MpvProcessController::Recover(std::uint64_t id) {
    impl_->error.clear();
    if (!impl_->CheckThread()) return false;
    const auto found = impl_->players.find(id);
    if (found == impl_->players.end() || found->second->media.empty())
        return impl_->Fail("MPV_NOTHING_TO_RECOVER");
    const auto media = found->second->media;
    impl_->StopPlayer(*found->second);
    return impl_->Start(id, media);
}

bool MpvProcessController::IsRunning(std::uint64_t id) const {
    const auto found = impl_->players.find(id);
    return found != impl_->players.end() && found->second->Active();
}

std::uint64_t MpvProcessController::PrivateBytes(std::uint64_t id) const {
    const auto found = impl_->players.find(id);
    if (found == impl_->players.end() || !found->second->Running()) return 0;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!K32GetProcessMemoryInfo(found->second->process.Get(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) return 0;
    return counters.PrivateUsage;
}

bool MpvProcessController::SaveFrame(std::uint64_t id, const std::wstring& path) {
    impl_->error.clear();
    if (!impl_->CheckThread()) return false;
    const auto player = impl_->FindPlayer(id);
    if (!player) return false;
    if (player->Native()) return player->image && wallpaper::SaveStaticImageJpeg(*player->image, path);
    if (!player->loaded) return impl_->Fail("MPV_NOT_LOADED");
    return impl_->Request(*player, Json::array({"screenshot-to-file", Utf8(path), "video"}), nullptr,
                          GetTickCount64() + 5000);
}

std::optional<unsigned long> MpvProcessController::ExitCode(std::uint64_t id) const {
    const auto found = impl_->players.find(id);
    if (found == impl_->players.end() || !found->second->process || found->second->Running()) return std::nullopt;
    DWORD code = 0;
    if (!GetExitCodeProcess(found->second->process.Get(), &code)) return std::nullopt;
    return code;
}

std::string MpvProcessController::LastError() const { return impl_->error; }

void MpvProcessController::SetCancellation(const std::atomic<bool>* cancellation) {
    if (impl_->CheckThread()) impl_->cancellation = cancellation;
}

bool MpvProcessController::SetVolume(std::uint64_t id, int volume) {
    impl_->error.clear();
    if (!impl_->CheckThread()) return false;
    const auto player = impl_->FindPlayer(id);
    if (!player) return false;
    if (player->Native()) {
        if (volume < 0 || volume > 100) return impl_->Fail("MPV_VOLUME_OUT_OF_RANGE");
        player->volume = volume;  // Silent image; kept for a later mpv switch.
        return true;
    }
    return impl_->Volume(*player, volume);
}

bool MpvProcessController::SetFit(std::uint64_t id, bool cover) {
    impl_->error.clear();
    if (!impl_->CheckThread()) return false;
    const auto player = impl_->FindPlayer(id);
    if (!player) return false;
    if (player->Native()) return impl_->RefitNative(*player, cover, false);
    return impl_->Fit(*player, cover);
}

bool MpvProcessController::SetBalancedMode(bool enabled) {
    return impl_->SetBalancedMode(enabled);
}

bool MpvProcessController::AllowMediaUrlPrefix(const std::string& trustedPrefix) {
    impl_->error.clear();
    if (!impl_->CheckThread()) return false;
    if (!ValidMediaPrefix(trustedPrefix)) return impl_->Fail("MPV_MEDIA_PREFIX_REJECTED");
    if (std::find(impl_->trustedMediaPrefixes.begin(), impl_->trustedMediaPrefixes.end(), trustedPrefix) ==
        impl_->trustedMediaPrefixes.end()) {
        if (impl_->trustedMediaPrefixes.size() >= 4096) return impl_->Fail("MPV_MEDIA_PREFIX_LIMIT");
        impl_->trustedMediaPrefixes.push_back(trustedPrefix);
    }
    return true;
}

bool MpvProcessController::AllowStaticImageSource(const std::string& url,
                                                  wallpaper::StaticImageSource source) {
    impl_->error.clear();
    if (!impl_->CheckThread()) return false;
    if (!source.read || source.bytes == 0 || IsVideoMedia(url))
        return impl_->Fail("STATIC_IMAGE_SOURCE_REJECTED");
    std::string media;
    bool localVideo = false;
    if (url.find("http://") != 0 || !impl_->ResolveMedia(url, media, localVideo))
        return impl_->Fail("STATIC_IMAGE_SOURCE_REJECTED");
    auto& sources = impl_->staticSources;
    if (sources.find(url) == sources.end() && sources.size() >= 4096)
        return impl_->Fail("STATIC_IMAGE_SOURCE_LIMIT");
    sources[url] = std::move(source);
    return true;
}

void MpvProcessController::RevokeMediaUrlPrefix(const std::string& trustedPrefix) {
    if (!impl_->CheckThread()) return;
    auto& prefixes = impl_->trustedMediaPrefixes;
    prefixes.erase(std::remove(prefixes.begin(), prefixes.end(), trustedPrefix), prefixes.end());
    // A running native host keeps its own copy of the reader for re-fitting.
    auto& sources = impl_->staticSources;
    for (auto item = sources.begin(); item != sources.end();) {
        if (item->first.compare(0, trustedPrefix.size(), trustedPrefix) == 0) item = sources.erase(item);
        else ++item;
    }
}

std::vector<std::uint64_t> MpvProcessController::ActiveDisplays() const {
    std::vector<std::uint64_t> result;
    for (const auto& item : impl_->players) {
        if (item.second->Active()) result.push_back(item.first);
    }
    return result;
}

bool MpvProcessController::EnsureAttached(std::uint64_t id) {
    impl_->error.clear();
    if (!impl_->CheckThread()) return false;
    const auto player = impl_->FindPlayer(id);
    if (!player) return false;
    const auto display = impl_->FindDisplay(id);
    if (!display || !impl_->Attach(*player, *display)) return false;
    if (player->Native()) return impl_->RefitNative(*player, player->cover, false);
    Json pause;
    return impl_->Request(*player, Json::array({"get_property", "pause"}), &pause) &&
        (pause.is_boolean() || impl_->Fail("MPV_IPC_INVALID_REPLY"));
}

}  // namespace lume::platform::windows
