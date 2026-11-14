#pragma once
#include "core/ui/page_bridge.h"
#include "apps/shell/native_material_request.h"
#include <windows.h>
#include <functional>
#include <memory>
#include <optional>

namespace lume::shell {
// Production has no consent request port and applies selections immediately.
// Injected ports retain deferred-selection ownership for compatibility tests.
class MaterialCapturePermissionSession final {
public:
    using Completion = std::function<void(int)>;
    struct Ports {
        std::function<std::optional<int>()> readAccess;
        std::function<std::function<void()>(Completion)> request;
    };
    struct Result {
        ui::PageCommand command;
        int access;
        HWND source;
        HWND target;
        std::optional<NativeMaterialRequest> native;
    };

    MaterialCapturePermissionSession();
    explicit MaterialCapturePermissionSession(Ports);
    ~MaterialCapturePermissionSession();
    MaterialCapturePermissionSession(const MaterialCapturePermissionSession&) = delete;
    MaterialCapturePermissionSession& operator=(const MaterialCapturePermissionSession&) = delete;
    // A later material choice replaces the pending one. Other commands pass
    // through. No request is made by restoration, verification or an invalid UI.
    // source owns consent UI lifetime; target is optional native-instance
    // identity. A page command keeps target null and retains its normal scope.
    bool Defer(ui::PageCommand, HWND source, DWORD wakeThread, bool verification, HWND target = nullptr);
    bool Defer(NativeMaterialRequest, DWORD wakeThread, bool verification);
    std::optional<Result> Drain();
    void Cancel() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
