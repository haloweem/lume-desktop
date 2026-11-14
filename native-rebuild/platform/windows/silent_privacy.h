#pragma once
#include <roapi.h>
#include <winrt/base.h>
#include <winrt/Windows.Security.Authorization.AppCapabilityAccess.h>

namespace lume::platform::windows {
// CheckAccess never asks for consent. Unknown and unsupported capabilities
// stay unavailable, so the caller can use its non-sensitive fallback.
inline bool HasExistingCapabilityAccess(const wchar_t* capability) noexcept {
    const auto initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return false;
    struct Apartment {
        HRESULT result;
        ~Apartment() { if (SUCCEEDED(result)) RoUninitialize(); }
    } apartment{initialized};
    try {
        using namespace winrt::Windows::Security::Authorization::AppCapabilityAccess;
        return AppCapability::Create(capability).CheckAccess() == AppCapabilityAccessStatus::Allowed;
    } catch (...) { return false; }
}
}
