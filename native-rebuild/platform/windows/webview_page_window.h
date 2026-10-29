#pragma once
#include "core/ui/page_bridge.h"
#include "core/ui/wallpaper_page_protocol.h"
#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lume::platform::windows {
// Owns only the presentation window and WebView lifetime. Business state stays in Shell.
class WebViewPageWindow final {
public:
    using CommandHandler = std::function<std::wstring(ui::PageCommand)>;
    using WallpaperHandler = std::function<void(const ui::WallpaperPageCommand&)>;
    using WallpaperDropHandler = std::function<void(std::vector<std::wstring>)>;
    using MusicDropHandler = std::function<void(std::vector<std::wstring>)>;
    using ApplicationCommandHandler = std::function<void(std::wstring_view)>;
    using VisibilityHandler = std::function<void(bool)>;
    // UI-thread, non-reentrant manifest lookup: exact filename -> expected bytes.
    // Zero, missing callback or exception denies access. Never return a file path.
    using MusicFontAssetLookup = std::function<std::uint64_t(std::wstring_view)>;
    // The composition root owns the exact domain parsers. This window only
    // validates the page origin, bounds the envelope, and defers delivery.
    void ConfigureApplicationCommands(ApplicationCommandHandler handler);
    void ConfigureVisibility(VisibilityHandler handler);
    void ConfigureThreadWake(UINT message, std::function<void()> handler);
    void ConfigureWallpaper(WallpaperHandler handler,std::wstring mediaDirectory,
        WallpaperDropHandler dropHandler = {});
    void ConfigureMusicBackground(std::wstring mediaDirectory);
    void ConfigureMusicFonts(std::wstring fontDirectory, MusicFontAssetLookup lookup = {});
    void ConfigureMusicDrop(MusicDropHandler handler);
    int Run(HINSTANCE instance, CommandHandler handler, bool verification = false,
        const std::wstring& verificationReadyScript = {},unsigned verificationRasterDpi = 0);
    void RequestClose();
    void Post(const std::wstring& message);
    void Present();
    HWND Handle() const;
private:
    struct State;
    std::shared_ptr<State> state_;
    WallpaperHandler wallpaperHandler_;
    WallpaperDropHandler wallpaperDropHandler_;
    MusicDropHandler musicDropHandler_;
    std::wstring wallpaperMediaDirectory_;
    std::wstring musicBackgroundMediaDirectory_;
    std::wstring musicFontDirectory_;
    MusicFontAssetLookup musicFontAssetLookup_;
    ApplicationCommandHandler applicationHandler_;
    VisibilityHandler visibilityHandler_;
    UINT wakeMessage_ = 0;
    std::function<void()> wakeHandler_;
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
};
}
