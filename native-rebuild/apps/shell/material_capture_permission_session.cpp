#include "apps/shell/material_capture_permission_session.h"
#include "apps/shell/material_capture_permission.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include "platform/windows/lume_window_ids.h"
#include <atomic>
#include <utility>
#include <variant>
#include <winrt/base.h>

namespace lume::shell {
namespace {
constexpr wchar_t SourceProperty[] = L"Lume.MaterialConsent.Source.v1";
}

struct MaterialCapturePermissionSession::Impl {
    struct Pending {
        std::atomic<int> access{-1};
        std::atomic<bool> completed{false};
        std::atomic<DWORD> wakeThread{0};
    };
    explicit Impl(Ports value) : ports(std::move(value)) {}
    Ports ports;
    using Selection = std::variant<ui::PageCommand, NativeMaterialRequest>;
    std::optional<Selection> choice;
    std::shared_ptr<Pending> pending;
    std::function<void()> cancel;
    HWND source = nullptr;
    HWND target = nullptr;
    std::unique_ptr<char> sourceToken;

    bool SourceAlive() const noexcept {
        return sourceToken && IsWindow(source) && GetPropW(source, SourceProperty) == sourceToken.get();
    }
    void ReleaseSource() noexcept {
        if (SourceAlive()) RemovePropW(source, SourceProperty);
        sourceToken.reset(); source = nullptr; target = nullptr;
    }
    bool BindSource(HWND window, HWND selected) {
        ReleaseSource();
        if (!IsWindow(window)) return false;
        auto token = std::make_unique<char>();
        if (!SetPropW(window, SourceProperty, token.get())) return false;
        source = window; target = selected; sourceToken = std::move(token);
        return true;
    }

    void Cancel() noexcept {
        if (choice) platform::windows::diag::Log("material-permission", "pending request cancelled");
        if (pending) pending->wakeThread.store(0);
        choice.reset();
        // Retire ownership before cancellation, which may invoke a completion.
        auto stop = std::move(cancel);
        pending.reset(); ReleaseSource();
        try { if (stop) stop(); } catch (...) {}
    }
    bool Defer(Selection selection, bool material, bool glass, HWND window, DWORD wakeThread, bool verification, HWND selected) {
        try {
            if (choice && material) {
                choice = std::move(selection);
                if (!BindSource(window, selected)) Cancel();
                return true;
            }
            if (verification || !wakeThread || !IsWindow(window) || !glass) return false;
            const auto access = ports.readAccess ? ports.readAccess() : std::nullopt;
            if (!access || *access != MaterialCaptureAccessPromptRequired || !ports.request) return false;
            platform::windows::diag::Log("material-permission", "request borderless access=" + std::to_string(*access));
            if (!BindSource(window, selected)) return false;
            auto result = std::make_shared<Pending>();
            result->wakeThread = wakeThread;
            choice = std::move(selection); pending = result;
            cancel = ports.request([result](int access) {
                result->access.store(access, std::memory_order_relaxed);
                result->completed.store(true, std::memory_order_release);
                if (const auto target = result->wakeThread.load())
                    PostThreadMessageW(target, lume::window_ids::ShellWakeMessage, 0, 0);
            });
            return true;
        } catch (...) {
            platform::windows::diag::Log("material-permission", "request failed hr=" +
                std::to_string(static_cast<long>(winrt::to_hresult())));
            Cancel(); return false;
        }
    }

};

MaterialCapturePermissionSession::MaterialCapturePermissionSession()
    // The renderer uses existing access or its compatibility background.
    : MaterialCapturePermissionSession(Ports{}) {}
MaterialCapturePermissionSession::MaterialCapturePermissionSession(Ports ports)
    : impl_(std::make_unique<Impl>(std::move(ports))) {}
MaterialCapturePermissionSession::~MaterialCapturePermissionSession() { Cancel(); }

bool MaterialCapturePermissionSession::Defer(ui::PageCommand command, HWND source, DWORD wakeThread, bool verification, HWND target) {
    return impl_->Defer(command, IsMaterialSelection(command), IsGlassMaterialSelection(command), source, wakeThread, verification, target);
}
bool MaterialCapturePermissionSession::Defer(NativeMaterialRequest request, DWORD wakeThread, bool verification) {
    const auto source = request.source;
    return impl_->Defer(std::move(request), true, true, source, wakeThread, verification, source);
}

std::optional<MaterialCapturePermissionSession::Result> MaterialCapturePermissionSession::Drain() {
    auto& state = *impl_;
    const auto pending = state.pending;
    if (!pending || !pending->completed.load(std::memory_order_acquire)) return std::nullopt;
    const auto access = pending->access.load(std::memory_order_relaxed);
    platform::windows::diag::Log("material-permission", "completed access=" + std::to_string(access));
    pending->wakeThread.store(0);
    state.cancel = {}; state.pending.reset();
    const bool sourceAlive = state.SourceAlive();
    const auto source = state.source;
    const auto target = state.target;
    state.ReleaseSource();
    auto choice = std::exchange(state.choice, std::nullopt);
    if (!choice || !sourceAlive) return std::nullopt;
    if (const auto command = std::get_if<ui::PageCommand>(&*choice))
        return Result{*command, access, source, target, std::nullopt};
    return Result{ui::PageCommand::Invalid, access, source, target, std::get<NativeMaterialRequest>(std::move(*choice))};
}
void MaterialCapturePermissionSession::Cancel() noexcept { impl_->Cancel(); }
}
