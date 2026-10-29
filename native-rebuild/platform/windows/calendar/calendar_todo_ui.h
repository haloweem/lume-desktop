#pragma once
#include "platform/windows/calendar/calendar_layout.h"
#include "core/calendar/calendar_settings.h"
#include <windows.h>
#include <memory>
#include <string>
#include <vector>
namespace lume::platform::windows::todo_ui {
constexpr UINT KeyMessage=WM_APP+530, FocusMessage=WM_APP+531, ActionMessage=WM_APP+539, MouseMessage=WM_APP+540;
constexpr UINT SurfaceChangedMessage=WM_APP+541;
int Px(float value,unsigned dpi) noexcept;
std::wstring Wide(const std::string &);
std::string Utf8(const std::wstring &);
std::wstring Text(HWND);
bool Number(HWND,int minimum,int maximum,int &out);
bool Composing(HWND);
void Move(HWND,CalendarRect,unsigned dpi);
void Fence(const std::vector<HWND> &,HWND current,bool backwards);
struct Surface {
    explicit Surface(unsigned,const calendar::Preferences &);
    ~Surface();
    Surface(const Surface &)=delete;
    Surface &operator=(const Surface &)=delete;
    HWND Add(HWND parent,HWND keyTarget,const wchar_t *kind,const wchar_t *text,int id,DWORD style=0);
    void Attach(HWND,bool popoverShadow=false);
    void Show(HWND initialFocus);
    void Refresh();
    void Clear(HDC,RECT) const;
    void Background(HDC,RECT) const;
    void Label(HDC,const std::wstring &,CalendarRect,unsigned align=DT_LEFT,int size=12) const;
    enum class Appearance { Plain, Filled, Schedule, Swatch };
    void Button(const DRAWITEMSTRUCT &,bool selected=false,COLORREF swatch=CLR_INVALID,Appearance = Appearance::Plain,
                bool today=false,bool muted=false,int fontSize=13) const;
    void EditFrame(HDC,CalendarRect) const;
    void Separator(HDC,float y,float width) const;
    LRESULT Color(HDC,HWND) const;
    unsigned dpi;
    COLORREF foreground,accent;
    HFONT font=nullptr;
    HBRUSH background=nullptr,editBackground=nullptr;
private:
    struct MaterialLayer;
    std::unique_ptr<MaterialLayer> material_;
    calendar::Preferences preferences_;
    bool capturing_=false;
    bool popoverShadow_=false;
    COLORREF matte_=RGB(0,0,0);
    ULONG_PTR graphicsToken_=0;
};
}
