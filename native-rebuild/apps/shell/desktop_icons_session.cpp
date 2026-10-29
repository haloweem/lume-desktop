#include "apps/shell/desktop_icons_session.h"
#include "platform/windows/local_settings_file_store.h"
#include "vendor/nlohmann/json.hpp"
#include <atomic>

namespace lume::shell {
using namespace platform::windows;
namespace {
constexpr UINT ChangedMessage = WM_APP + 137;
const char* ErrorName(DesktopIconsError error) {
    switch (error) {
        case DesktopIconsError::None: return "";
        case DesktopIconsError::Save: return "save";
        case DesktopIconsError::Rollback: return "rollback";
        case DesktopIconsError::InputBusy: return "input-busy";
        case DesktopIconsError::Visibility: return "visibility";
        case DesktopIconsError::Startup: return "runtime";
    }
    return "runtime";
}
}
struct DesktopIconsSession::Impl {
    Impl(HINSTANCE instance, std::string path, Notify notify, settings::IFileStore* files,
         DesktopIconsRuntime::Discover discover)
        : path(std::move(path)), notify(std::move(notify)), store(files ? *files : localFiles),
          loaded(store.Load(this->path)), preferences(loaded.value), runtime(std::move(discover)) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.hInstance = instance;
        wc.lpfnWndProc = WindowProc; wc.lpszClassName = L"LumeDesktopIconsSession";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            error = "runtime"; return;
        }
        window = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, instance, this);
        if (!window) error = "runtime";
    }
    ~Impl() { notify = {}; Shutdown(); }
    DesktopIconsCallbacks Callbacks() {
        const HWND receiver = window;
        return {
            [this](const auto& next) {
                const bool saved = store.Save(path, next);
                if (saved) ++saves;
                return saved;
            },
            [this, receiver] {
                if (!posted.exchange(true) && !PostMessageW(receiver, ChangedMessage, 0, 0))
                    posted = false;
            }
        };
    }
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == ChangedMessage) {
            self->posted = false;
            if (!self->applying && !self->stopped && self->notify) {
                const auto state = self->runtime.Snapshot();
                (void)state;  // Background runtime errors show in the page without opening it.
                self->notify(self->State(), false);
            }
            return 0;
        }
        return DefWindowProcW(hwnd, message, w, l);
    }
    bool Restore() {
        if (stopped || !window) return false;
        if (restored) return error.empty();
        restored = true;
        if (!preferences.enabled) return true;
        applying = true;
        const bool ok = runtime.Start(preferences, Callbacks());
        applying = false;
        if (!ok) error = ErrorName(runtime.Snapshot().error);
        return ok;
    }
    std::wstring State(bool completed = false, bool saved = false) const {
        const auto state = runtime.Snapshot();
        const auto p = state.running ? state.preferences : preferences;
        const auto load = saves.load() ? desktop_icons::LoadState::Loaded : loaded.state;
        const char* loadName = load == desktop_icons::LoadState::Loaded ? "loaded" :
            load == desktop_icons::LoadState::Recovered ? "recovered" :
            load == desktop_icons::LoadState::Failed ? "failed" : "fresh";
        const auto reportedError = !error.empty() ? error :
            p.enabled || state.pendingRestores ? std::string(ErrorName(state.error)) : std::string{};
        const auto value = nlohmann::json{
            {"version", 1}, {"type", "icons-state"}, {"enabled", p.enabled}, {"running", state.running},
            {"enableAutoHide", p.enableAutoHide}, {"autoHideDelay", p.autoHideDelay},
            {"showIconsOnAnyInput", p.showIconsOnAnyInput}, {"manuallyHidden", p.manuallyHidden},
            {"desktops", state.desktops}, {"autoHidden", state.autoHidden},
            {"pendingRestores", state.pendingRestores},
            {"loadStatus", loadName}, {"error", reportedError},
            // "applied" is the command reply discriminator, not a success flag.
            // saved reports persistence; error reports the independent runtime result.
            {"result", completed ? "applied" : "snapshot"}, {"saved", saved}
        }.dump();
        return std::wstring(value.begin(), value.end()); // Protocol contains ASCII only.
    }
    bool Apply(desktop_icons::Preferences next) {
        const auto before = runtime.Snapshot();
        const auto previous = before.running ? before.preferences : preferences;
        next.manuallyHidden = previous.manuallyHidden;
        applying = true;
        bool saved = false;
        if (next.enabled) {
            if (before.running) saved = runtime.Configure(next);
            else saved = runtime.Start(next, Callbacks(), true);
            if (!saved) error = ErrorName(runtime.Snapshot().error);
        } else {
            if (!runtime.Stop()) error = "rollback";
            else if (store.Save(path, next)) { ++saves; saved = true; }
            else {
                error = "save";
                if (before.running && !runtime.Start(previous, Callbacks())) error = "rollback";
            }
        }
        if (saved) {
            preferences = next;
            if (next.enabled) preferences = runtime.Snapshot().preferences;
            error.clear();
        } else {
            preferences = previous;
            if (error.empty()) error = "runtime";
        }
        applying = false;
        return saved;
    }
    std::optional<std::wstring> HandlePage(ui::PageCommand command) {
        if (stopped || applying) return {};
        if (command == ui::PageCommand::Ready) return State();
        const auto state = runtime.Snapshot();
        auto next = state.running ? state.preferences : preferences;
        const auto numeric = static_cast<int>(command);
        if (numeric >= static_cast<int>(ui::PageCommand::IconsDelayFirst) &&
            numeric <= static_cast<int>(ui::PageCommand::IconsDelayLast)) {
            next.autoHideDelay = numeric - static_cast<int>(ui::PageCommand::IconsDelayFirst) + 3;
        } else switch (command) {
            case ui::PageCommand::IconsEnable: next.enabled = true; break;
            case ui::PageCommand::IconsDisable: next.enabled = false; break;
            case ui::PageCommand::IconsAutoHideOn: next.enableAutoHide = true; break;
            case ui::PageCommand::IconsAutoHideOff: next.enableAutoHide = false; break;
            case ui::PageCommand::IconsRestoreOn: next.showIconsOnAnyInput = true; break;
            case ui::PageCommand::IconsRestoreOff: next.showIconsOnAnyInput = false; break;
            case ui::PageCommand::IconsReset: {
                const bool enabled = next.enabled, manual = next.manuallyHidden;
                next = {}; next.enabled = enabled; next.manuallyHidden = manual; break;
            }
            default: return {};
        }
        const bool saved = window && Apply(next);
        if (!window) error = "runtime";
        return State(true, saved);
    }
    bool Shutdown() {
        stopped = true;
        const bool ok = runtime.Stop();
        if (window) { DestroyWindow(window); window = nullptr; }
        return ok;
    }
    std::string path, error;
    Notify notify;
    LocalSettingsFileStore localFiles;
    desktop_icons::PreferencesStore store;
    desktop_icons::LoadedPreferences loaded;
    desktop_icons::Preferences preferences;
    std::atomic_uint saves{0};
    std::atomic_bool posted{false};
    DesktopIconsRuntime runtime;
    HWND window = nullptr;
    bool applying = false, stopped = false, restored = false;
};
DesktopIconsSession::DesktopIconsSession(HINSTANCE i, std::string p, Notify n, settings::IFileStore* f,
                                         DesktopIconsRuntime::Discover d)
    : impl_(std::make_unique<Impl>(i, std::move(p), std::move(n), f, std::move(d))) {}
DesktopIconsSession::~DesktopIconsSession() = default;
bool DesktopIconsSession::Restore() { return impl_->Restore(); }
std::optional<std::wstring> DesktopIconsSession::HandlePage(ui::PageCommand c) { return impl_->HandlePage(c); }
std::wstring DesktopIconsSession::State() const { return impl_->State(); }
bool DesktopIconsSession::Running() const { return impl_->runtime.Snapshot().running; }
bool DesktopIconsSession::HasRecovery() const { return impl_->runtime.Snapshot().pendingRestores != 0; }
bool DesktopIconsSession::Shutdown() { return impl_->Shutdown(); }
}
