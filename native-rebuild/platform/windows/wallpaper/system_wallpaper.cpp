#include "platform/windows/wallpaper/system_wallpaper.h"
#include <objbase.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace lume::platform::windows::wallpaper {
namespace {
using Microsoft::WRL::ComPtr;
struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    bool Usable() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};
// IDesktopWallpaper names monitors by device path; find the one at `bounds`.
bool Monitor(IDesktopWallpaper& wallpaper, const RECT& bounds, std::wstring& id) {
    UINT count = 0;
    if (FAILED(wallpaper.GetMonitorDevicePathCount(&count))) return false;
    for (UINT i = 0; i < count; ++i) {
        LPWSTR path = nullptr;
        if (FAILED(wallpaper.GetMonitorDevicePathAt(i, &path)) || !path) continue;
        RECT rect{};
        const bool match = SUCCEEDED(wallpaper.GetMonitorRECT(path, &rect)) && EqualRect(&rect, &bounds);
        if (match) id = path;
        CoTaskMemFree(path);
        if (match) return true;
    }
    return false;
}
}  // namespace

std::optional<std::wstring> SystemWallpaper(const RECT& monitor) {
    Apartment apartment;
    if (!apartment.Usable()) return std::nullopt;
    ComPtr<IDesktopWallpaper> wallpaper;
    std::wstring id;
    if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&wallpaper))) ||
        !Monitor(*wallpaper.Get(), monitor, id)) return std::nullopt;
    LPWSTR path = nullptr;
    if (FAILED(wallpaper->GetWallpaper(id.c_str(), &path))) return std::nullopt;
    std::wstring result = path ? path : L"";
    CoTaskMemFree(path);
    return result;
}

bool SetSystemWallpaper(const RECT& monitor, const std::wstring& image, bool cover) {
    Apartment apartment;
    if (!apartment.Usable()) return false;
    ComPtr<IDesktopWallpaper> wallpaper;
    std::wstring id;
    if (FAILED(CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&wallpaper))) ||
        !Monitor(*wallpaper.Get(), monitor, id)) return false;
    // Position is one setting for every monitor; the last one set wins.
    wallpaper->SetPosition(cover ? DWPOS_FILL : DWPOS_FIT);
    return SUCCEEDED(wallpaper->SetWallpaper(id.c_str(), image.c_str()));
}
}  // namespace lume::platform::windows::wallpaper
