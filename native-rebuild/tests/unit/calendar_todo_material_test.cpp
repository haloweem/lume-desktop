#include "platform/windows/calendar/calendar_todo_editor.h"
#include "platform/windows/calendar/calendar_todo_ui.h"
#include "platform/windows/material/material_api.h"
#include "platform/windows/material/widget_material_runtime.h"
#include "todo_test_support.h"
#include <dwmapi.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>
using namespace lume::platform::windows;
using namespace todo_test;
namespace {
bool alternate=false;
bool anchorAlternate=false;
bool allowFallback=false,closeOnShow=false;
unsigned firstShows=0;
ULONGLONG slowestFirstFrame=0;
LRESULT CALLBACK ObserveFirstShow(int code,WPARAM wp,LPARAM lp) {
    if(code>=0) {
        const auto& message=*reinterpret_cast<CWPSTRUCT*>(lp);
        if(message.message==WM_SHOWWINDOW&&message.wParam) {
            wchar_t name[64]{};GetClassNameW(message.hwnd,name,64);
            if(lstrcmpW(name,L"LumeCalendarTodoEditor")==0||lstrcmpW(name,L"LumeCalendarTodoPicker")==0) {
                ++firstShows;
                Expect(allowFallback||GetPropW(message.hwnd,L"LumeTodoGlassActive"),
                    "first show already carries composed glass, including fast initialization");
                if(closeOnShow){closeOnShow=false;SendMessageW(message.hwnd,WM_CLOSE,0,0);}
            }
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
LRESULT CALLBACK AnchorForeground(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    if(m==WM_PAINT) {
        PAINTSTRUCT ps;auto dc=BeginPaint(w,&ps);RECT r{};GetClientRect(w,&r);
        auto brush=CreateSolidBrush(anchorAlternate?RGB(10,240,70):RGB(240,20,220));
        FillRect(dc,&r,brush);DeleteObject(brush);EndPaint(w,&ps);return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
LRESULT CALLBACK Background(HWND w,UINT m,WPARAM wp,LPARAM lp) {
    if(m==WM_PAINT) {
        PAINTSTRUCT ps;auto dc=BeginPaint(w,&ps);RECT r{};GetClientRect(w,&r);
        for(int y=0;y<r.bottom;y+=48)for(int x=0;x<r.right;x+=48) {
            const bool light=((x/48+y/48)%2==0)^alternate;
            auto brush=CreateSolidBrush(light?RGB(205,166,66):RGB(34,92,141));
            RECT cell{x,y,x+48,y+48};FillRect(dc,&cell,brush);DeleteObject(brush);
        }
        EndPaint(w,&ps);return 0;
    }
    return DefWindowProcW(w,m,wp,lp);
}
void Pump(DWORD milliseconds) {
    const auto end=GetTickCount64()+milliseconds;
    do {MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}while(GetTickCount64()<end);
}
HWND Field(HWND parent,int id) {
    struct Search {int id;HWND found=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND w,LPARAM p)->BOOL {
        auto& search=*reinterpret_cast<Search*>(p);
        if(GetDlgCtrlID(w)==search.id){search.found=w;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&search));return search.found;
}
void Click(HWND window,int id){Expect(Field(window,id)!=nullptr,"native field exists");SendMessageW(Field(window,id),BM_CLICK,0,0);Pump(100);}
HWND Picker(HWND owner) {
    HWND found=nullptr;
    while((found=FindWindowExW(nullptr,found,L"LumeCalendarTodoPicker",nullptr)))
        if(GetWindow(found,GW_OWNER)==owner)return found;
    return nullptr;
}
HWND Shadow(HWND owner) {
    HWND found=nullptr;
    while((found=FindWindowExW(nullptr,found,L"LumeTodoCssShadow",nullptr)))
        if(GetWindow(found,GW_OWNER)==owner)return found;
    return nullptr;
}
void Ready(HWND window,bool fallback) {
    const auto start=GetTickCount64();
    for(int i=0;i<1600&&!GetPropW(window,L"LumeTodoGlassActive")&&!fallback;++i) {
        Expect(!IsWindowVisible(window),"pending first frame never exposes the opaque legacy panel");
        Pump(5);
    }
    Expect((GetPropW(window,L"LumeTodoGlassActive")!=nullptr)==!fallback,"glass is presented or opaque fallback is used");
    Expect(IsWindowVisible(window)!=FALSE,"ready or failed material presents a usable popup");
    Expect(((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED)!=0)==!fallback,"native fallback does not retain layered composition");
    const auto elapsed=GetTickCount64()-start;
    slowestFirstFrame=std::max(slowestFirstFrame,elapsed);
    std::cout<<"FIRST_FRAME_READY_MS "<<elapsed<<'\n';
    Pump(150);
}
std::vector<unsigned> Capture(HWND background,const std::string& path) {
    DwmFlush();RECT r{};GetWindowRect(background,&r);const int width=r.right-r.left,height=r.bottom-r.top;
    auto screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* bits=nullptr;auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    Expect(bitmap&&bits,"screen capture allocated");auto old=SelectObject(dc,bitmap);
    Expect(BitBlt(dc,0,0,width,height,screen,r.left,r.top,SRCCOPY|CAPTUREBLT)!=FALSE,"real composited screen capture");GdiFlush();
    std::vector<unsigned> pixels(static_cast<unsigned*>(bits),static_cast<unsigned*>(bits)+width*height);
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);
    header.bfSize=header.bfOffBits+width*height*4;
    std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<char*>(&header),sizeof(header));
    file.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader));
    file.write(static_cast<char*>(bits),width*height*4);Expect(file.good(),"screen artifact written");
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);return pixels;
}
template<class T> T Resolve(HMODULE module,const char* name) {
    const auto address=GetProcAddress(module,name);T result=nullptr;
    static_assert(sizeof(result)==sizeof(address));std::memcpy(&result,&address,sizeof(result));return result;
}
}
int main(int argc,char**argv) {
    Expect(argc>=2,"artifact directory supplied");const std::string output=argv[1];
    const bool fallback=argc>2&&std::string(argv[2])=="--fallback";
    std::cout.setf(std::ios::unitbuf);
    allowFallback=fallback;
    const auto showHook=SetWindowsHookExW(WH_CALLWNDPROC,ObserveFirstShow,nullptr,GetCurrentThreadId());
    Expect(showHook!=nullptr,"observe synchronous first show");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto instance=GetModuleHandleW(nullptr);WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Background;
    wc.lpszClassName=L"LumeTodoMaterialIsolatedBackdrop";RegisterClassW(&wc);
    auto owner=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Isolated todo material verification",
        WS_POPUP|WS_VISIBLE,80,80,820,500,nullptr,nullptr,instance,nullptr);
    Expect(owner!=nullptr,"isolated backdrop");SetForegroundWindow(owner);Pump(150);
    RECT bounds{};GetWindowRect(owner,&bounds);Utc time;lume::calendar::Preferences prefs;
    CalendarTodoEditor editor;auto seed=Add({"isolated-material-no-storage",1});seed.patch.title=u8"原生输入 · 玻璃日程";
    auto save=[](const lume::todo::Command&){lume::todo::CommandResult result;result.ok=true;return result;};
    HMODULE module=fallback?nullptr:LoadLibraryW(L"LumeGlass.dll");
    const auto stats=module?Resolve<LumeMaterialStatisticsFn>(module,"LumeMaterialStatistics"):nullptr;
    Expect(fallback||stats,"material statistics available");
    LumeMaterialStatisticsData status{};status.size=sizeof(status);
    for(auto material:{lume::calendar::Material::LiquidGlass,lume::calendar::Material::FrostedGlass}) {
        prefs.material=material;const std::string prefix=material==lume::calendar::Material::LiquidGlass?"liquid":"frosted";
        Expect(editor.Open(instance,owner,bounds,96,prefs,time,seed,At(2026,9,25,12),{},save),"isolated editor opens");
        Ready(editor.Handle(),fallback);
        Expect(!Shadow(editor.Handle()),"original ready editor has no CSS box shadow");
        if(!fallback) {
            auto close=Field(editor.Handle(),108);
            SendMessageW(close,BM_SETSTATE,TRUE,0);Pump(100);
            const auto pressed=Capture(owner,output+"/"+prefix+"-close-pressed.bmp");
            RECT r{};GetWindowRect(close,&r);unsigned opaqueDark=0;
            for(int y=r.top-bounds.top+5;y<r.bottom-bounds.top-5;++y)
                for(int x=r.left-bounds.left+5;x<r.right-bounds.left-5;++x) {
                    const auto pixel=pressed[static_cast<std::size_t>(y)*820+x]&0xffffffu;
                    if(pixel==0x003f444eu)++opaqueDark;
                }
            SendMessageW(close,BM_SETSTATE,FALSE,0);Pump(50);
            Expect(opaqueDark<10,"pressed close button does not introduce an opaque dark material");
        }
        auto title=Field(editor.Handle(),101),notes=Field(editor.Handle(),105);
        SetWindowTextW(title,L"中文编辑 ABC 123");SetWindowTextW(notes,L"保留原生多行 EDIT\r\n第二行备注");
        SetFocus(title);SendMessageW(title,EM_SETSEL,5,8);Pump(150);
        const auto before=Capture(owner,output+"/"+prefix+"-editor-selection.bmp");
        SendMessageW(title,WM_CHAR,L'Z',0);Expect(todo_ui::Text(title)==L"中文编辑 Z 123","native selection replacement survives composition");
        SendMessageW(title,WM_IME_STARTCOMPOSITION,0,0);SendMessageW(title,WM_KEYDOWN,VK_ESCAPE,0);
        Expect(editor.IsOpen(),"IME escape preserves editor");SendMessageW(title,WM_IME_ENDCOMPOSITION,0,0);Pump(150);
        Capture(owner,output+"/"+prefix+"-editor-edited.bmp");
        alternate=!alternate;InvalidateRect(owner,nullptr,TRUE);Pump(500);
        const auto after=Capture(owner,output+"/"+prefix+"-editor-background-change.bmp");
        RECT er{};GetWindowRect(editor.Handle(),&er);
        unsigned changed=0;
        // Blank top band avoids text/caret and proves the panel responds to a
        // changed captured scene rather than displaying a static opaque fill.
        for(int y=er.top-bounds.top+6;y<er.top-bounds.top+15;++y)
            for(int x=er.left-bounds.left+30;x<er.right-bounds.left-30;++x)
                if((before[static_cast<std::size_t>(y)*820+x]&0xffffffu)!=(after[static_cast<std::size_t>(y)*820+x]&0xffffffu))++changed;
        Expect(fallback?changed==0:changed>100,"blank panel pixels follow real backdrop only with glass");
        Click(editor.Handle(),102);auto date=Picker(editor.Handle());Expect(date!=nullptr,"date dropdown");
        Ready(date,fallback);const auto shadowed=Capture(owner,output+"/"+prefix+"-date.bmp");
        Expect(fallback||Shadow(date),"original date popover has its CSS shadow");
        if(!fallback) {
            const auto style=GetWindowLongPtrW(Shadow(date),GWL_EXSTYLE);
            Expect((style&(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE),
                "shadow cannot activate or become a material capture source");
            ShowWindow(Shadow(date),SW_HIDE);DwmFlush();
            const auto unshadowed=Capture(owner,output+"/"+prefix+"-date-shadow-hidden.bmp");
            RECT r{};GetWindowRect(date,&r);unsigned darker=0;
            for(int y=r.bottom-bounds.top+2;y<r.bottom-bounds.top+16&&y<500;++y)
                for(int x=r.left-bounds.left+30;x<r.right-bounds.left-30&&x<820;++x) {
                    const auto i=static_cast<std::size_t>(y)*820+x;
                    unsigned shadowedBrightness=0,unshadowedBrightness=0;
                    for(int shift:{0,8,16}){shadowedBrightness+=(shadowed[i]>>shift)&255;unshadowedBrightness+=(unshadowed[i]>>shift)&255;}
                    if(unshadowedBrightness>shadowedBrightness+3)++darker;
                }
            Expect(darker>100,"CSS outer shadow darkens real screen pixels outside popup bounds");
            ShowWindow(Shadow(date),SW_SHOWNOACTIVATE);
        }
        if(stats){Expect(stats(&status)&&status.users==2&&status.devices==1,"editor and date share material device");}
        SendMessageW(Field(date,201),WM_KEYDOWN,VK_ESCAPE,0);Pump(100);
        if(stats)Expect(stats(&status)&&status.users==1,"date close releases its material lease");
        Click(editor.Handle(),104);auto lead=Picker(editor.Handle());Expect(lead!=nullptr,"reminder dropdown");
        Ready(lead,fallback);SetWindowTextW(Field(lead,210),L"37");Pump(100);
        Expect(fallback||Shadow(lead),"original reminder popover has its CSS shadow");
        Capture(owner,output+"/"+prefix+"-reminder.bmp");Click(lead,208);
        Expect(!Picker(editor.Handle())&&todo_ui::Text(Field(editor.Handle(),104))==L"37 分钟前","native reminder commits to draft");
        Click(editor.Handle(),108);Pump(100);
        Expect(!editor.IsOpen()&&IsWindowEnabled(owner)&&GetFocus()==owner,"close button restores owner and focus");
        Expect(!FindWindowW(L"LumeTodoCssShadow",nullptr),"closing popup destroys cached shadow");
        if(stats)Expect(stats(&status)&&status.users==0&&status.devices==0&&status.captures==0,"last editor lease releases device and captures");
    }
    int writes=0,closes=0;
    auto noSave=[&](const lume::todo::Command&){++writes;return lume::todo::CommandResult{};};
    for(int action=0;action<5;++action) {
        auto command=seed;
        lume::todo::TaskView existing;existing.title="Existing";existing.schedule=seed.patch.schedule;
        const bool editing=action==2;
        if(editing){command.type=lume::todo::CommandType::Update;command.id="existing";command.patch={};}
        closeOnShow=action==4;
        Expect(editor.Open(instance,owner,bounds,96,prefs,time,command,At(2026,9,25,12),
            editing?std::optional<lume::todo::TaskView>{existing}:std::nullopt,noSave,[&]{++closes;}),"close regression editor opens");
        const auto old=editor.Handle();
        if(action==0) {
            // Close before any queued material-ready notification is dispatched.
            SendMessageW(old,WM_CLOSE,0,0);
        } else if(action==4) {
            const auto deadline=GetTickCount64()+8000;
            while(editor.IsOpen()&&GetTickCount64()<deadline)Pump(5);
            Expect(!closeOnShow,"activation-time close was exercised");
        } else {
            Ready(old,fallback);
            if(action==1)SendMessageW(Field(old,101),WM_KEYDOWN,VK_ESCAPE,0);
            if(action==2)Click(old,107);
            if(action==3) {
                // The editor X must also retire a child awaiting its first frame.
                SendMessageW(Field(old,102),BM_CLICK,0,0);
                SendMessageW(old,todo_ui::ActionMessage,MAKEWPARAM(102,BN_CLICKED),reinterpret_cast<LPARAM>(Field(old,102)));
                Expect(Picker(old)!=nullptr,"pending child exists before closing parent");
                SendMessageW(Field(old,108),BM_CLICK,0,0);
                SendMessageW(old,todo_ui::ActionMessage,MAKEWPARAM(108,BN_CLICKED),reinterpret_cast<LPARAM>(Field(old,108)));
            }
        }
        Pump(200);editor.Close();
        Expect(!IsWindow(old)&&!editor.IsOpen()&&IsWindowEnabled(owner),"close leaves no window and re-enables owner");
        Expect(writes==0&&closes==action+1,"closing cancels without writing and notifies exactly once");
        Expect(!FindWindowW(L"LumeTodoCssShadow",nullptr),"close leaves no shadow");
        if(stats)Expect(stats(&status)&&status.users==0&&status.devices==0&&status.captures==0,
            "pending and ready close release all material resources");
    }
    if(!fallback) {
        wc.lpfnWndProc=AnchorForeground;wc.lpszClassName=L"LumeTodoMaterialAnchorForeground";RegisterClassW(&wc);
        auto anchor=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Calendar foreground scene anchor",
            WS_POPUP|WS_VISIBLE,bounds.left,bounds.top,820,500,owner,nullptr,instance,nullptr);
        WidgetMaterialRuntime calendarMaterial;
        LumeMaterialParameters parameters{};parameters.size=sizeof(parameters);parameters.owner=anchor;
        parameters.frosted=1;parameters.opticalProfile=1;parameters.lightForeground=1;
        Expect(calendarMaterial.Attach(parameters),"calendar-like ancestor material attaches");
        for(int i=0;i<160&&calendarMaterial.State()!=WidgetMaterialState::Ready;++i)Pump(50);
        Expect(calendarMaterial.State()==WidgetMaterialState::Ready&&GetPropW(anchor,L"LumeGlassOwner"),
            "calendar-like scene anchor ready");
        Expect(editor.Open(instance,anchor,bounds,96,prefs,time,seed,At(2026,9,25,12),{},save),"anchored editor opens");
        Ready(editor.Handle(),false);Click(editor.Handle(),102);
        auto date=Picker(editor.Handle());Expect(date!=nullptr,"anchored picker opens");Ready(date,false);
        Expect(stats(&status)&&status.users==3&&status.devices==1,"calendar editor and picker share one device");
        Pump(500);const auto before=Capture(owner,output+"/scene-anchor-before.bmp");
        anchorAlternate=true;InvalidateRect(anchor,nullptr,TRUE);Pump(700);
        const auto after=Capture(owner,output+"/scene-anchor-after.bmp");
        // The ancestor's magenta/green foreground covers each panel's source
        // rectangle. Shared scene anchoring must exclude it from both panels.
        for(HWND panel:{editor.Handle(),date}) {
            RECT r{};GetWindowRect(panel,&r);unsigned changed=0,total=0;
            for(int y=r.top-bounds.top+6;y<r.top-bounds.top+12;++y)
                for(int x=r.left-bounds.left+30;x<r.right-bounds.left-30;++x) {
                    const auto i=static_cast<std::size_t>(y)*820+x;
                    if((before[i]&0xffffffu)!=(after[i]&0xffffffu))++changed;
                    ++total;
                }
            Expect(total>500&&changed<total/100,"panel pixels exclude ancestor foreground from shared scene");
        }
        editor.Close();calendarMaterial.Detach();DestroyWindow(anchor);Pump(100);
        Expect(stats(&status)&&status.users==0&&status.devices==0&&status.captures==0,
            "anchored hierarchy releases all material resources");
    }
    Expect(firstShows>=8,"first-show observer covered editor and picker");
    UnhookWindowsHookEx(showHook);
    DestroyWindow(owner);if(module)FreeLibrary(module);
    std::cout<<"SLOWEST_FIRST_FRAME_READY_MS "<<slowestFirstFrame<<'\n';
    std::cout<<(fallback?"TODO_MATERIAL_FALLBACK_PASS":"TODO_MATERIAL_COMPOSITION_PASS")<<'\n';
}
