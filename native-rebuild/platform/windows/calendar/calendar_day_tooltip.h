#pragma once
#include "platform/windows/widget_ui/direct_write_tooltip.h"
#include "core/calendar/calendar_model.h"
#include "core/calendar/lunar_calendar.h"
#include "platform/windows/calendar/calendar_layout.h"
#include <windows.h>
#include <commctrl.h>
#include <array>
#include <cmath>
#include <optional>
#include <string>

namespace lume::platform::windows {
// The original grid keeps the complete day description in cell.title. This
// owner-local tooltip restores it without timers, polling, or another renderer.
class CalendarDayTooltip final {
public:
    ~CalendarDayTooltip(){Stop();}
    CalendarDayTooltip()=default;
    CalendarDayTooltip(const CalendarDayTooltip&)=delete;
    CalendarDayTooltip& operator=(const CalendarDayTooltip&)=delete;
    bool Attach(HWND owner){
        if(window_&&IsWindow(window_)&&owner_==owner)return true;
        Stop();if(!owner)return false;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};
        if(!InitCommonControlsEx(&controls))return false;
        window_=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,TOOLTIPS_CLASSW,nullptr,
            WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,
            owner,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window_)return false;
        if(!direct_write_tooltip::Attach(window_)){Stop();return false;}
        owner_=owner;
        for(std::size_t i=0;i<text_.size();++i){
            TTTOOLINFOW info{};info.cbSize=TTTOOLINFOW_V2_SIZE;info.hwnd=owner_;info.uId=i+1;
            info.uFlags=TTF_SUBCLASS|TTF_TRANSPARENT;info.lpszText=text_[i].data();
            if(!SendMessageW(window_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&info))){Stop();return false;}
        }
        return true;
    }
    void Update(const calendar::CalendarModel& model,const CalendarLayout& layout,unsigned dpi,bool pickerVisible){
        if(!window_||!dpi)return;
        if(dpi_!=dpi){dpi_=dpi;SendMessageW(window_,TTM_SETMAXTIPWIDTH,0,MulDiv(480,static_cast<int>(dpi),96));}
        const auto& grid=model.Grid();
        if(!firstDate_||*firstDate_!=grid.front().date){
            firstDate_=grid.front().date;
            for(std::size_t i=0;i<grid.size();++i){
                text_[i]=Description(grid[i].date);
                TTTOOLINFOW info{};info.cbSize=TTTOOLINFOW_V2_SIZE;info.hwnd=owner_;info.uId=i+1;info.lpszText=text_[i].data();
                SendMessageW(window_,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&info));
            }
            Hide();
        }
        const auto px=[dpi](float value){return static_cast<LONG>(std::lround(static_cast<double>(value)*dpi/96));};
        for(std::size_t i=0;i<grid.size();++i){
            const auto& cell=layout.cells[i];const auto& picker=layout.picker;
            const bool covered=pickerVisible&&cell.x<picker.Right()&&cell.Right()>picker.x&&cell.y<picker.Bottom()&&cell.Bottom()>picker.y;
            const bool visible=grid[i].selectable&&!(model.CurrentTheme()!=calendar::Theme::Spacious&&!grid[i].currentMonth)&&!covered;
            const RECT rect=visible?RECT{px(cell.x),px(cell.y),px(cell.Right()),px(cell.Bottom())}:RECT{};
            if(EqualRect(&rect,&rects_[i]))continue;
            rects_[i]=rect;
            TTTOOLINFOW info{};info.cbSize=TTTOOLINFOW_V2_SIZE;info.hwnd=owner_;info.uId=i+1;info.rect=rect;
            SendMessageW(window_,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&info));
        }
    }
    void Hide(){if(window_&&IsWindow(window_))SendMessageW(window_,TTM_POP,0,0);}
    void Stop(){
        if(window_&&IsWindow(window_))DestroyWindow(window_);
        window_=owner_=nullptr;firstDate_.reset();text_={};rects_={};dpi_=0;
    }
private:
    static std::wstring Wide(std::string_view value){
        if(value.empty()||value.size()>4096)return {};
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
        if(length<=0)return {};
        std::wstring out(static_cast<std::size_t>(length),L'\0');
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),length);return out;
    }
    static std::wstring Description(calendar::Date date){
        if(!calendar::IsValidDate(date))return {};
        std::wstring result=std::to_wstring(date.year)+L"年"+std::to_wstring(date.month)+L"月"+std::to_wstring(date.day)+L"日";
        const auto append=[&](std::wstring value){if(!value.empty())result+=L"，"+value;};
        const auto detail=calendar::LookupLunar(date);
        append(Wide(detail.lunarDate));
        for(const auto value:detail.festivals)append(Wide(value));
        append(Wide(detail.solarTerm));
        if(detail.holiday)append(Wide(detail.holiday->name)+(detail.holiday->work?L"补班":L"放假"));
        return result;
    }
    HWND owner_=nullptr,window_=nullptr;
    std::optional<calendar::Date> firstDate_;
    std::array<std::wstring,42> text_;
    std::array<RECT,42> rects_{};
    unsigned dpi_=0;
};
}
