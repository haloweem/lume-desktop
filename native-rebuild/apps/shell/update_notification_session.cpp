#include "apps/shell/update_notification_session.h"
#include "platform/windows/application_settings/application_settings_ports.h"
#include "platform/windows/update_notification/update_presenter.h"
#include <shellapi.h>
#include <optional>
#include <utility>

namespace lume::shell {
namespace {
constexpr wchar_t WindowClass[] = L"Lume.UpdateNotification.Schedule.v1";
constexpr UINT WakeMessage = WM_APP + 0x392;
constexpr UINT_PTR Timer = 1;
constexpr std::int64_t Minute = 60000, RetryNetwork = 15 * Minute;
using application_settings::ManifestResult;
using update_notification::ClockSample;
using update_notification::Notice;
using update_notification::NoticeKind;

class MemoryFile final : public application_settings::IPreferencesFile {
    application_settings::FileRead value_;
public:
    application_settings::FileRead Read() override { return value_; }
    bool WriteAtomically(const std::string& bytes) override {
        value_ = {application_settings::ReadStatus::Loaded, bytes}; return true;
    }
};
ClockSample Now() {
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER ticks{}; ticks.LowPart = time.dwLowDateTime; ticks.HighPart = time.dwHighDateTime;
    SYSTEMTIME local{}; GetLocalTime(&local);
    return {static_cast<std::int64_t>(ticks.QuadPart / 10000) - 11644473600000LL,
        local.wYear * 10000 + local.wMonth * 100 + local.wDay};
}
std::wstring Wide(const std::string& value) {
    // Version fields have already passed the numeric-version parser.
    return {value.begin(), value.end()};
}
std::wstring Detail(const Notice& notice, bool english) {
    switch (notice.kind) {
    case NoticeKind::Current:
        return english ? L"No newer version was found on the official website. Current version: " + Wide(notice.currentVersion)
            : L"\u5b98\u7f51\u6682\u65e0\u66f4\u9ad8\u7248\u672c\u3002\u5f53\u524d\u7248\u672c\uff1a" + Wide(notice.currentVersion);
    case NoticeKind::Incompatible:
        return english ? L"A newer version is listed on the official website. Its compatibility with this native build has not been verified, so it cannot be installed directly. You can visit the official website for details. Nothing has been downloaded."
            : L"\u5b98\u7f51\u5df2\u6709\u66f4\u9ad8\u7248\u672c\uff0c\u4f46\u5c1a\u672a\u901a\u8fc7\u4e0e\u5f53\u524d\u539f\u751f\u7248\u672c\u7684\u517c\u5bb9\u5ba1\u6838\uff0c\u5f53\u524d\u4e0d\u80fd\u76f4\u63a5\u5b89\u88c5\u3002\u53ef\u4e3b\u52a8\u8bbf\u95ee\u5b98\u7f51\u4e86\u89e3\u8be6\u60c5\uff1b\u672a\u4e0b\u8f7d\u4efb\u4f55\u5b89\u88c5\u5305\u3002";
    case NoticeKind::Disabled:
        return english ? L"A newer native version is listed on the official website. Direct installation is not enabled in this build. You can visit the official website for details. Nothing has been downloaded."
            : L"\u5b98\u7f51\u5df2\u6709\u66f4\u9ad8\u539f\u751f\u7248\u672c\uff0c\u5f53\u524d\u6784\u5efa\u5c1a\u672a\u5f00\u653e\u76f4\u63a5\u5b89\u88c5\u3002\u53ef\u4e3b\u52a8\u8bbf\u95ee\u5b98\u7f51\u4e86\u89e3\u8be6\u60c5\uff1b\u672a\u4e0b\u8f7d\u4efb\u4f55\u5b89\u88c5\u5305\u3002";
    case NoticeKind::Available:
        return english ? L"A new version is available. Download and install it directly in Lume." : L"新版本已发布，可以直接在 Lume 中下载并安装。";
    case NoticeKind::Error:
        if (notice.error == "update-offline" || notice.error == "update-timeout")
            return english ? L"Could not reach the official update service. Check your connection and try again."
                : L"\u6682\u65f6\u65e0\u6cd5\u8fde\u63a5\u5b98\u7f51\u66f4\u65b0\u670d\u52a1\uff0c\u8bf7\u68c0\u67e5\u7f51\u7edc\u540e\u91cd\u8bd5\u3002";
        return english ? L"The update check could not be completed or its result could not be verified. Please try again later. Nothing has been downloaded or installed."
            : L"\u66f4\u65b0\u68c0\u67e5\u672a\u5b8c\u6210\u6216\u7ed3\u679c\u672a\u80fd\u901a\u8fc7\u9a8c\u8bc1\uff0c\u8bf7\u7a0d\u540e\u91cd\u8bd5\u3002\u672a\u4e0b\u8f7d\u6216\u5b89\u88c5\u4efb\u4f55\u5185\u5bb9\u3002";
    }
    return {};
}
struct Backoff {
    ClockSample since{};
    std::int64_t duration = 0;
    bool Due(ClockSample now) const {
        return !duration || now.localDay != since.localDay || now.utcMillis < since.utcMillis ||
            now.utcMillis - since.utcMillis >= duration;
    }
};
}
struct UpdateNotificationSession::Impl : std::enable_shared_from_this<Impl> {
    HINSTANCE instance;
    const DWORD owner = GetCurrentThreadId();
    UpdateNotificationOptions options;
    RequestCheck request;
    Changed changed;
    std::shared_ptr<application_settings::IPreferencesFile> file;
    std::unique_ptr<update_notification::Policy> policy;
    std::unique_ptr<platform::windows::UpdatePresenter> presenter;
    HWND window = nullptr;
    bool started = false, stopped = false, enabled = false, wakePending = false;
    bool inFlight = false, manualPending = false, delivering = false, english = false;
    struct Pending { bool manual; ManifestResult result; };
    std::optional<Pending> pending;
    Backoff networkBackoff, presentationBackoff;
    std::optional<ClockSample> lastProbe;
    std::string error;

    Impl(HINSTANCE module, UpdateNotificationOptions value, RequestCheck check, Changed callback)
        : instance(module ? module : GetModuleHandleW(nullptr)), options(std::move(value)),
          request(options.testPorts.requestCheck ? options.testPorts.requestCheck :
                  options.verification ? RequestCheck{} : std::move(check)), changed(std::move(callback)) {}
    ~Impl() { Stop(); }
    bool OnThread() const { return GetCurrentThreadId() == owner; }
    ClockSample Clock() const { return options.testPorts.now ? options.testPorts.now() : Now(); }
    void ChangedError(std::string value) {
        if (error == value) return;
        error = std::move(value);
        const auto callback = changed;
        if (!stopped && callback) callback();
    }
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (message == WM_NCDESTROY) SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (self && (message == WakeMessage || (message == WM_TIMER && wparam == Timer) ||
            message == WM_TIMECHANGE || message == WM_SETTINGCHANGE ||
            (message == WM_POWERBROADCAST && (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND)))) {
            const auto keep = self->shared_from_this();
            if (message == WakeMessage) keep->wakePending = false;
            try { keep->Tick(); } catch (...) { keep->ChangedError("notification-internal"); }
            return message == WM_POWERBROADCAST ? TRUE : 0;
        }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    bool Start() {
        if (!OnThread() || stopped) return false;
        if (started) return true;
        started = true;
        // Verification is inert unless its fake transport is explicitly supplied.
        enabled = static_cast<bool>(request);
        if (!enabled) return true;
        file = options.testPorts.file ? options.testPorts.file : options.verification
            ? std::static_pointer_cast<application_settings::IPreferencesFile>(std::make_shared<MemoryFile>())
            : platform::windows::CreateApplicationSettingsFile(options.statePath);
        policy = std::make_unique<update_notification::Policy>(*file, options.currentVersion);
        policy->Load();
        WNDCLASSEXW cls{}; cls.cbSize = sizeof(cls); cls.hInstance = instance;
        cls.lpfnWndProc = Procedure; cls.lpszClassName = WindowClass;
        if (!RegisterClassExW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            enabled = false; ChangedError("notification-window"); return false;
        }
        // A hidden top-level window receives power/time broadcasts; HWND_MESSAGE does not.
        window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, WindowClass, L"", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (!window || !SetTimer(window, Timer, static_cast<UINT>(Minute), nullptr)) {
            if (window) DestroyWindow(window);
            window = nullptr; enabled = false; ChangedError("notification-window"); return false;
        }
        ChangedError(policy->Error());
        Tick(); return !stopped;
    }
    bool Show(const Notice& notice) {
        if (stopped) return false;
        // Background checks only refresh application-settings state. Consume the
        // result without a presenter or a deferred popup, including after resume.
        if (!notice.manual) return true;
        if (!options.testPorts.show && options.showInApp) {
            const bool shown = options.showInApp(notice);
            return shown && !stopped;
        }
        const bool newer = notice.kind == NoticeKind::Incompatible || notice.kind == NoticeKind::Disabled || notice.kind == NoticeKind::Available;
        const platform::windows::UpdateNotice content{
            newer ? Wide(notice.version) : std::wstring{}, Detail(notice, english), notice.manual};
        bool visible = false;
        if (options.testPorts.show) visible = options.testPorts.show(content);
        else if (!options.verification) {
            if (!presenter) {
                std::weak_ptr<Impl> weak = shared_from_this();
                presenter = std::make_unique<platform::windows::UpdatePresenter>(instance, [weak] {
                    if (const auto self = weak.lock(); self && !self->stopped && !self->options.verification)
                        ShellExecuteW(nullptr, L"open", L"https://lumedesktop.com/", nullptr, nullptr, SW_SHOWNORMAL);
                });
            }
            presenter->SetLanguage(english);
            visible = presenter->Show(content);
        }
        // A presenter callback may stop or destroy the public session synchronously.
        return visible && !stopped;
    }
    void Deliver(Pending value, ClockSample now) {
        delivering = true;
        bool classifiedError = false;
        const bool ok = policy->Complete(value.manual, value.result, now, [&](const Notice& notice) {
            classifiedError = notice.kind == NoticeKind::Error;
            return Show(notice);
        });
        delivering = false;
        if (stopped) return;
        const auto policyError = policy->Error();
        if (!ok && policyError == "notification-present") {
            pending = std::move(value); presentationBackoff = {now, Minute};
        } else if (classifiedError || policy->AutomaticDue(now)) {
            networkBackoff = {now, RetryNetwork};
        } else networkBackoff = {};
        ChangedError(policyError);
    }
    void Complete(bool manual, const ManifestResult& result) {
        if (!OnThread() || stopped || !enabled || !started || delivering) return;
        // The observed manual HandlePage route can be external to this scheduler.
        if (!inFlight && !manual) return;
        manual = manual || manualPending;
        inFlight = false; manualPending = false; pending.reset();
        Deliver({manual, result}, Clock());
    }
    void Tick() {
        if (!OnThread() || stopped || !enabled || delivering) return;
        const auto now = Clock();
        policy->Flush();
        ChangedError(policy->Error());
        if (stopped || inFlight) return;
        if (pending) {
            if (manualPending || now.localDay != presentationBackoff.since.localDay) {
                manualPending = manualPending || pending->manual;
                pending.reset();
                networkBackoff = {};
            } else {
                if (!presentationBackoff.Due(now)) return;
                auto value = std::move(*pending); pending.reset();
                Deliver(std::move(value), now); return;
            }
        }
        const bool manual = manualPending;
        const bool pollDue = !lastProbe || now.localDay != lastProbe->localDay ||
            now.utcMillis - lastProbe->utcMillis >= 15 * Minute;
        if (!manual && (!networkBackoff.Due(now) || (!policy->AutomaticDue(now) && !pollDue))) return;
        inFlight = true;
        // Set flight before calling the port: injected ports can complete synchronously.
        bool accepted = false;
        try { accepted = request(manual); } catch (...) { ChangedError("notification-request"); }
        if (stopped) return;
        if (!accepted) inFlight = false;
        else lastProbe = now;
    }
    bool Manual() {
        if (!OnThread() || stopped || !started || !enabled) return false;
        manualPending = true; Tick(); return !stopped;
    }
    void Wake() {
        if (!OnThread() || stopped || !window || wakePending) return;
        wakePending = PostMessageW(window, WakeMessage, 0, 0) != FALSE;
    }
    void RetryPresentation() {
        if (!OnThread() || stopped || !pending || delivering) return;
        presentationBackoff.duration = 0;
        Tick();
    }
    void Stop() {
        if (stopped) return;
        stopped = true; enabled = false; pending.reset(); changed = {};
        inFlight = false; manualPending = false;
        const auto hwnd = window; window = nullptr;
        if (hwnd) {
            KillTimer(hwnd, Timer);
            MSG message{};
            while (PeekMessageW(&message, hwnd, WakeMessage, WakeMessage, PM_REMOVE)) {}
            DestroyWindow(hwnd);
        }
        if (presenter) presenter->Shutdown();
        const auto callback = options.testPorts.shutdownPresenter;
        if (started && callback) callback();
    }
    bool Visible() const {
        if (stopped || !enabled) return false;
        return options.testPorts.hasVisible ? options.testPorts.hasVisible() : presenter && presenter->HasVisible();
    }
};
UpdateNotificationSession::UpdateNotificationSession(HINSTANCE instance, UpdateNotificationOptions options,
    RequestCheck check, Changed changed)
    : impl_(std::make_shared<Impl>(instance, std::move(options), std::move(check), std::move(changed))) {}
UpdateNotificationSession::~UpdateNotificationSession() { Shutdown(); }
bool UpdateNotificationSession::Start() { const auto self = impl_; return self->Start(); }
bool UpdateNotificationSession::RequestManual() { const auto self = impl_; return self->Manual(); }
void UpdateNotificationSession::OnCheckCompleted(bool manual, const ManifestResult& result) {
    const auto self = impl_; self->Complete(manual, result);
}
void UpdateNotificationSession::Wake() { const auto self = impl_; self->Wake(); }
void UpdateNotificationSession::RetryPresentation() { const auto self = impl_; self->RetryPresentation(); }
void UpdateNotificationSession::Tick() { const auto self = impl_; self->Tick(); }
void UpdateNotificationSession::SetLanguage(bool english) {
    const auto self = impl_;
    if (self->OnThread() && !self->stopped) self->english = english;
}
void UpdateNotificationSession::Shutdown() { const auto self = impl_; self->Stop(); }
bool UpdateNotificationSession::HasWork() const { const auto self = impl_; return self->enabled && !self->stopped; }
bool UpdateNotificationSession::HasVisible() const { const auto self = impl_; return self->Visible(); }
HWND UpdateNotificationSession::Handle() const { return impl_->window; }
std::string UpdateNotificationSession::Error() const { return impl_->error; }
}
