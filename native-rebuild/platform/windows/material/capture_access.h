#pragma once
#include "capture_compatibility.h"
#include "material_diagnostics.h"
#include <windows.h>
#include <appmodel.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>
#include <cstdio>

namespace lume_glass {
struct CaptureAccessObservation {
    int status = -1;
    LONG identity = APPMODEL_ERROR_NO_PACKAGE;
    HRESULT error = S_OK;
};
struct CaptureAccessCache {
    CaptureAccessObservation observation;
    ULONGLONG checkedAt = 0;
    bool valid = false, logged = false;
};
inline thread_local CaptureAccessCache captureAccessCache;

// Material workers own WinRT apartments. Keep only plain status metadata in
// their short-lived caches, never apartment-bound capability objects.
inline void ResetMaterialCaptureAccess() { captureAccessCache = {}; }
inline int MaterialCaptureAccessStatus(bool fresh = false) {
    auto& cache = captureAccessCache;
    const auto now = GetTickCount64();
    if (fresh || !cache.valid || now - cache.checkedAt >= 1000) {
        CaptureAccessObservation next;
        UINT32 length = 0;
        next.identity = GetCurrentPackageFullName(&length, nullptr);
        try {
            using winrt::Windows::Foundation::Metadata::ApiInformation;
            using winrt::Windows::Security::Authorization::AppCapabilityAccess::AppCapability;
            if (!ApiInformation::IsTypePresent(L"Windows.Security.Authorization.AppCapabilityAccess.AppCapability"))
                next.error = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            else
                next.status = static_cast<int>(AppCapability::Create(L"graphicsCaptureWithoutBorder").CheckAccess());
        } catch (...) { next.error = static_cast<HRESULT>(winrt::to_hresult()); }
        if (!cache.logged || next.status != cache.observation.status || next.error != cache.observation.error ||
            next.identity != cache.observation.identity) {
            char details[128]{};
            std::snprintf(details, sizeof(details), "access=%d identityResult=%ld", next.status, next.identity);
            lume::platform::windows::material_diagnostics::Write("CaptureBorderlessPermission",
                FAILED(next.error) ? next.error : next.status == CapturePermissionAllowed ? S_OK : E_ACCESSDENIED, details);
            cache.logged = true;
        }
        cache.observation = next; cache.checkedAt = GetTickCount64(); cache.valid = true;
    }
    if (FAILED(cache.observation.error)) throw CaptureCompatibilityFailure{cache.observation.error};
    return cache.observation.status;
}
inline void RequireMaterialCaptureAccess(bool fresh = false) {
    RequireCapturePermission([fresh] { return MaterialCaptureAccessStatus(fresh); },
        [] { return static_cast<HRESULT>(winrt::to_hresult()); });
}
} // namespace lume_glass
