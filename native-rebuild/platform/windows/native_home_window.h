#pragma once

#include "core/ui/home_screen_model.h"
#include "platform/windows/lume_resource.h"

#include <windows.h>

#include <functional>

namespace lume::platform::windows {

class NativeHomeWindow final {
public:
    ~NativeHomeWindow();
    bool Create(HINSTANCE instance, ui::HomeScreenModel& model,
                std::function<void(const ui::HomeScreenSnapshot&)> onChanged);
    HWND Handle() const noexcept { return window_; }
    void Close();
    int Run(HINSTANCE instance, ui::HomeScreenModel& model,
            std::function<void(const ui::HomeScreenSnapshot&)> onChanged);

private:
    enum ControlId : int {
        HomeButton = 100,
        BeautyButton = 101,
        WallpaperButton = 102,
        ClockCardButton = 110,
        ClockEnabledButton = 120,
        WelfareButton = 130,
        MondButton = 131,
        InsideDreamButton = 132,
        BackButton = 140,
        QuickBeautyButton = 150,
        QuickWallpaperButton = 151,
        QuickClockButton = 152,
        TimeCardButton = 153,
    };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void CreateControls();
    void LayoutControls();
    void UpdateControls();
    void Draw(HWND window, HDC dc);
    void DrawButton(const DRAWITEMSTRUCT& item) const;
    void ApplyAction(int controlId);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND controls_[13]{};
    int lastMinute_ = -1;
    ui::HomeScreenModel* model_ = nullptr;
    std::function<void(const ui::HomeScreenSnapshot&)> onChanged_;
};

}  // namespace lume::platform::windows
