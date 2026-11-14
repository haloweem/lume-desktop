#pragma once
#include <windows.h>
#include <dwmapi.h>

namespace lume::platform::windows::desktop_peek {
// EXCLUDED_FROM_PEEK keeps a desktop surface visible while another app is
// previewed. DISALLOW_PEEK would instead disable that window's own preview.
// The documented contract supports setting this attribute. Keep our successful
// setting on the HWND so a worker-owned glass surface can inherit it without
// relying on DwmGetWindowAttribute supporting this set-only attribute.
inline constexpr wchar_t ExcludedProp[] = L"LumeDesktopPeekExcluded";
inline bool Excluded(HWND window) {
    return window && GetPropW(window, ExcludedProp) != nullptr;
}
inline bool SetExcluded(HWND window, bool excluded,
                        decltype(&DwmSetWindowAttribute) setAttribute = &DwmSetWindowAttribute) {
    const BOOL value = excluded ? TRUE : FALSE;
    if (!window || FAILED(setAttribute(window, DWMWA_EXCLUDED_FROM_PEEK, &value, sizeof(value)))) return false;
    if (excluded) return SetPropW(window, ExcludedProp, reinterpret_cast<HANDLE>(1)) != FALSE;
    RemovePropW(window, ExcludedProp);
    return true;
}
inline bool SyncFromOwner(HWND window, HWND owner) {
    const bool excluded = Excluded(owner);
    return window && (Excluded(window) == excluded || SetExcluded(window, excluded));
}
inline void Forget(HWND window) { RemovePropW(window, ExcludedProp); }
}  // namespace lume::platform::windows::desktop_peek
