#pragma once

#include <windows.h>

namespace lume_glass {

// A persistent capture capability/permission failure must retire the scene,
// rather than keep sessions alive behind a transparent material fallback.
struct CaptureCompatibilityFailure { HRESULT error; };

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

template<class Session, class ErrorCode>
void StartMaterialCapture(Session& session, ErrorCode&& errorCode) {
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
    // Ordinary source/device failures retain the existing retry policy.
    session.StartCapture();
}

} // namespace lume_glass
