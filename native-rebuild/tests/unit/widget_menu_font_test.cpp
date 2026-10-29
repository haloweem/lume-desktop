#include "platform/windows/widget_ui/StdAfx.h"
#include "platform/windows/widget_ui/ui_font.h"
#include <iostream>
#include <filesystem>

// Reproduce a Windows 10 machine without the Windows 11 variable family,
// without installing/removing fonts or changing the user's Windows settings.
namespace Gdiplus {
class MissingVariableFamily : public FontFamily {
public:
    explicit MissingVariableFamily(const WCHAR* name)
        : FontFamily(wcscmp(name, L"Segoe UI Variable Text") == 0
            ? L"Lume deliberately missing font 96342" : name) {}
};
}
#define FontFamily MissingVariableFamily
#include "platform/windows/widget_ui/LumeWidgetMenu.cpp"
#undef FontFamily

int main(int argc,char** argv) {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return 1;
    int failures = 0;
    for (const double scale : {1.0, 1.25, 1.5, 2.0}) {
        for (const auto kind : {MenuKind::Root, MenuKind::Size, MenuKind::Position,
                MenuKind::Material, MenuKind::CalendarTheme, MenuKind::TimeStyle}) {
            MenuState state;
            state.kind = kind;
            state.scale = scale;
            state.width = Scale(&state, kind==MenuKind::Root?kRootWidth:
                IsActionSubmenu(kind)?190:kSubmenuWidth);
            state.rowHeight = kind == MenuKind::Root ? 42 : 32;
            switch(kind){
            case MenuKind::Root:AddRootItems(&state);break;
            case MenuKind::Size:AddSizeItems(&state);break;
            case MenuKind::Position:AddPositionItems(&state);break;
            case MenuKind::Material:AddMaterialItems(&state);break;
            case MenuKind::CalendarTheme:AddCalendarThemeItems(&state);break;
            case MenuKind::TimeStyle:AddTimeStyleItems(&state);break;
            }
            state.visibleRows = static_cast<int>(state.items.size());
            state.height = ContentHeight(&state);
            state.window = CreateWindowExW(0, L"STATIC", L"font-test", WS_POPUP,
                0, 0, state.width, state.height, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            HDC dc = CreateCompatibleDC(nullptr);
            BITMAPINFO info{};
            info.bmiHeader = {sizeof(BITMAPINFOHEADER), state.width, -state.height, 1, 32, BI_RGB};
            void* bits = nullptr;
            HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!state.window || !dc || !bitmap) return 2;
            const auto previous = SelectObject(dc, bitmap);
            DrawMenu(&state, dc);
            GdiFlush();
            if(argc==2){
                const auto output=std::filesystem::path(argv[1]);std::filesystem::create_directories(output);
                Gdiplus::Bitmap preview(state.width,state.height,state.width*4,PixelFormat32bppRGB,static_cast<BYTE*>(bits));
                const CLSID bmpEncoder{0x557cf400,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
                const auto path=output/("menu-"+std::to_string(static_cast<int>(kind))+"-"+std::to_string(static_cast<int>(scale*96))+".bmp");
                if(preview.Save(path.c_str(),&bmpEncoder)!=Gdiplus::Ok)++failures;
            }
            for (int row = 0; row < state.visibleRows; ++row) {
                if(state.items[row].separator)continue;
                const auto rect = ItemRect(&state, row);
                unsigned ink = 0;
                for (int y = rect.top; y < rect.bottom; ++y)
                    for (int x = Scale(&state, 47); x < state.width - Scale(&state, 23); ++x) {
                        const auto pixel = static_cast<const DWORD*>(bits)[y * state.width + x];
                        if ((pixel & 255) != ((pixel >> 8) & 255) || (pixel & 255) != ((pixel >> 16) & 255)) {
                            std::cerr << "FAIL colored text fringe\n";
                            ++failures;
                            break;
                        }
                        if ((pixel & 255) < 160 && ((pixel >> 8) & 255) < 160 && ((pixel >> 16) & 255) < 160) ++ink;
                    }
                if (ink < 20) {
                    std::cerr << "FAIL menu label ink: scale=" << scale << " row=" << row << '\n';
                    ++failures;
                }
            }
            SelectObject(dc, previous);
            DeleteObject(bitmap);
            DeleteDC(dc);
            DestroyWindow(state.window);
        }
    }
    {
        MenuState state;
        state.items = {{L"禁用", 9, MenuKind::Root, MenuIcon::None, false, false, false},
            {L"", 0, MenuKind::Root, MenuIcon::None, true}, {L"复制", 2}, {L"关闭", 3}};
        state.visibleRows = 4;
        state.window = CreateWindowExW(0, L"STATIC", L"menu-keys", WS_POPUP,
            0, 0, 250, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowLongPtrW(state.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
        if (HitTest(&state, POINT{50, 20}) != -1) ++failures;
        WindowProc(state.window, WM_KEYDOWN, VK_DOWN, 0);
        if (state.hover != 2) ++failures;
        WindowProc(state.window, WM_KEYDOWN, VK_RETURN, 0);
        if (!state.done || state.result != 2) ++failures;
        DestroyWindow(state.window);
    }
    Gdiplus::GdiplusShutdown(token);
    if (failures) return 1;
    std::cout << "PASS root and all five submenu families at 96/120/144/192 DPI\n";
    return 0;
}
