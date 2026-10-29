#pragma once
#include "core/radial/radial_menu.h"
#include "platform/windows/material/widget_material_runtime.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <functional>
#include <memory>
namespace lume::platform::windows {
class RadialWindow final {
public:
    static constexpr UINT Screenshot=100,Toggle=200,Dismiss=201;
    RadialWindow(HINSTANCE,std::function<void(UINT)> action,std::function<void()> changed);
    ~RadialWindow();
    bool Create();
    bool Show(POINT,bool frosted);
    void Hide();
    void Stop();
    bool Paint(bool frosted);
    HWND Handle()const;
    bool Visible()const;
    WidgetMaterialState MaterialState()const;
    static void Draw(WidgetCanvas&,int hover);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
