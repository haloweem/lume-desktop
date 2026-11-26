#include "platform/windows/widget_ui/native_widget_window.h"
#include <objbase.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
using namespace lume::platform::windows;
namespace {
void Check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool SameGeometry(const lume::widget::Preferences& a,const lume::widget::Preferences& b){
    return a.placed==b.placed&&a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;
}
void Scenario(){
    Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"initialize isolated widget apartment");
    MONITORINFO monitor{sizeof(monitor)};
    Check(GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor)!=FALSE,"read test monitor");
    const RECT full=monitor.rcWork;RECT work=full;
    const lume::widget::Limits limits{320,180,120,100,900,720};
    unsigned commits=0;auto saved=lume::widget::Defaults(limits);
    WidgetWindowPorts ports;ports.glass=false;ports.hoverPane=false;
    ports.draw=[](WidgetCanvas& canvas,float w,float h){canvas.Rounded({0,0,w,h},12,0xffffff,.6f);};
    ports.monitorInfo=[&](HMONITOR selected,MONITORINFO& info){
        if(!GetMonitorInfoW(selected,&info))return false;info.rcWork=work;return true;
    };
    ports.action=[&](WidgetAction action,const auto& bounds){if(action==WidgetAction::Bounds){++commits;saved=bounds;}};
    {
        NativeWidgetWindow widget(GetModuleHandleW(nullptr),L"Display restore fixture",limits,ports);
        auto wanted=lume::widget::Defaults(limits);wanted.enabled=wanted.placed=true;
        wanted.x=full.right-440;wanted.y=full.bottom-280;
        Check(widget.Configure(wanted)&&widget.Start(),"start placed desktop component");
        const auto original=widget.PreferredBounds();const auto liveOriginal=widget.Bounds();
        Check(SameGeometry(original,wanted),"existing desired placement is retained at startup");
        for(unsigned round=0;round<3;++round){
            work={full.left,full.top,full.right-480,full.bottom-320};
            SendMessageW(widget.Handle(),WM_DISPLAYCHANGE,32,MAKELPARAM(work.right-work.left,work.bottom-work.top));
            const auto smallBounds=widget.Bounds();
            Check(smallBounds.x!=liveOriginal.x||smallBounds.y!=liveOriginal.y,"smaller display visibly relocates component into its work area");
            Check(SameGeometry(widget.PreferredBounds(),original)&&commits==0,"game display changes never overwrite preferred placement or request saving");
            auto look=widget.PreferredBounds();look.frosted=!look.frosted;
            Check(widget.Configure(look),"settings can still change on the smaller display");
            Check(SameGeometry(widget.PreferredBounds(),original),"saving another setting keeps original geometry");
            work=full;
            SendMessageW(widget.Handle(),WM_DISPLAYCHANGE,32,MAKELPARAM(full.right-full.left,full.bottom-full.top));
            Check(SameGeometry(widget.Bounds(),liveOriginal),"leaving game resolution restores original position and dimensions");
        }
        work={full.left,full.top,full.right-400,full.bottom-300};
        SendMessageW(widget.Handle(),WM_SETTINGCHANGE,SPI_SETWORKAREA,0);
        Check(SameGeometry(widget.PreferredBounds(),original)&&commits==0,"temporary taskbar work area cannot replace user coordinates");
        work=full;SendMessageW(widget.Handle(),WM_SETTINGCHANGE,SPI_SETWORKAREA,0);
        Check(SameGeometry(widget.Bounds(),liveOriginal),"work-area restoration returns to user placement");
        RECT suggested{full.left+20,full.top+20,full.left+500,full.top+300};
        const auto dpi=GetDpiForWindow(widget.Handle());
        SendMessageW(widget.Handle(),WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
        Check(commits==0&&SameGeometry(widget.PreferredBounds(),original)&&SameGeometry(widget.Bounds(),liveOriginal),
              "noninteractive DPI notification cannot persist or adopt transient coordinates");
        // In an actual drag the proposed DPI frame is used immediately, but
        // persistence belongs to its single completed interaction.
        SendMessageW(widget.Handle(),WM_ENTERSIZEMOVE,0,0);
        SendMessageW(widget.Handle(),WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
        Check(commits==0&&SameGeometry(widget.PreferredBounds(),original),"cross-monitor drag defers its save until completion");
        Check(SetWindowPos(widget.Handle(),nullptr,full.left+100,full.top+100,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,
              "move the isolated widget like a completed user drag");
        const auto moved=widget.Bounds();SendMessageW(widget.Handle(),WM_EXITSIZEMOVE,0,0);
        Check(commits==1&&SameGeometry(saved,moved)&&SameGeometry(widget.PreferredBounds(),moved),
              "user move becomes the new preferred and persisted placement exactly once");
        widget.Stop();Check(widget.Configure(saved)&&widget.Start(),"restart from saved user placement");
        Check(SameGeometry(widget.Bounds(),moved),"restart preserves intentional user move");
        // An unplugged monitor still has a reachable fallback; reconnecting
        // returns to the desired position instead of saving that fallback.
        wanted=widget.PreferredBounds();wanted.x=full.right+2000;wanted.y=full.bottom+1000;
        Check(widget.Configure(wanted),"accept placement from an absent display");
        const auto recovered=widget.Bounds();
        Check(recovered.x!=wanted.x&&recovered.y!=wanted.y&&SameGeometry(widget.PreferredBounds(),wanted),
              "missing display recovers a reachable window without losing its remembered position");
        widget.Stop();
        wanted=lume::widget::Defaults(limits);wanted.enabled=true;
        Check(widget.Configure(wanted)&&widget.Start()&&widget.PreferredBounds().placed,"first placement records a stable fitted baseline");
    }
    CoUninitialize();
}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto name=L"LumeWidgetDisplayRestore-"+std::to_wstring(GetCurrentProcessId());
    const auto desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    Check(desktop!=nullptr,"create non-input desktop");
    std::thread test([&]{Check(SetThreadDesktop(desktop)!=FALSE,"bind isolated desktop without changing screen modes");Scenario();});
    test.join();CloseDesktop(desktop);
    std::puts("PASS widget display restoration: game modes, settings, work areas, DPI, user moves, missing displays and restart");
}
