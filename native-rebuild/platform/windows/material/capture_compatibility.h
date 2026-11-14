#pragma once

#include <windows.h>

namespace lume_glass {

// A persistent capture capability/permission failure must retire the scene,
// rather than keep sessions alive behind a transparent material fallback.
struct CaptureCompatibilityFailure { HRESULT error; };
inline constexpr int CapturePermissionAllowed = 4;

template<class Permission, class ErrorCode>
void RequireCapturePermission(Permission&& permission, ErrorCode&& errorCode) {
    try {
        // CheckAccess does not prompt. A successful IsBorderRequired(false)
        // setter/getter alone is not proof that Windows granted borderless use.
        if (static_cast<int>(permission()) != CapturePermissionAllowed)
            throw CaptureCompatibilityFailure{E_ACCESSDENIED};
    } catch (const CaptureCompatibilityFailure&) {
        throw;
    } catch (...) {
        throw CaptureCompatibilityFailure{errorCode()};
    }
}

template<class Available, class ErrorCode>
void RequireCaptureBorderControl(Available&& available, ErrorCode&& errorCode) {
    try {
        if (!available())
            throw CaptureCompatibilityFailure{HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)};
    } catch (const CaptureCompatibilityFailure&) {
        throw;
    } catch (...) {
        throw CaptureCompatibilityFailure{errorCode()};
    }
}

template<class Session, class Permission, class ErrorCode>
void StartMaterialCapture(Session& session, Permission&& permission, ErrorCode&& errorCode) {
    try {
        // Respect the system's capability and permission decision. If the
        // request is rejected or ignored, this decorative effect must not
        // start a capture that marks other applications with a border.
        session.IsBorderRequired(false);
        if (session.IsBorderRequired()) throw CaptureCompatibilityFailure{E_ACCESSDENIED};
    } catch (const CaptureCompatibilityFailure&) {
        throw;
    } catch (...) {
        throw CaptureCompatibilityFailure{errorCode()};
    }
    // The permission check must be fresh here, after configuration and before
    // StartCapture. Windows can accept the setter while ignoring its request.
    RequireCapturePermission(permission, errorCode);
    // Ordinary source/device failures retain the existing retry policy.
    session.StartCapture();
}

} // namespace lume_glass
