#include "platform/windows/calendar/native_calendar_window.h"
#include "todo_test_support.h"
#include <windowsx.h>
using namespace lume;
using namespace lume::platform::windows;
using namespace todo_test;
namespace {
void Pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
HWND Field(HWND parent,int id){struct Search{int id;HWND found=nullptr;}s{id};EnumChildWindows(parent,[](HWND h,LPARAM p)->BOOL{auto& s=*reinterpret_cast<Search*>(p);if(GetDlgCtrlID(h)==s.id){s.found=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&s));return s.found;}
void Click(NativeCalendarWindow& window,CalendarRect r){auto dpi=GetDpiForWindow(window.Handle());SendMessageW(window.Handle(),WM_LBUTTONDOWN,0,MAKELPARAM(static_cast<int>((r.x+r.width/2)*dpi/96),static_cast<int>((r.y+r.height/2)*dpi/96)));Pump();}
void Button(HWND parent,int id){
    // A user can only click after the async first-frame presentation. Clicking
    // a hidden owner races its activation against the child picker activation.
    const auto deadline=GetTickCount64()+5000;
    while(IsWindow(parent)&&!IsWindowVisible(parent)&&GetTickCount64()<deadline){Pump();Sleep(5);}
    Expect(IsWindowVisible(parent)!=FALSE,"button owner has presented before user interaction");
    auto h=Field(parent,id);Expect(h!=nullptr,"editor field exists");SendMessageW(h,BM_CLICK,0,0);Pump();
}
HWND Picker(HWND owner){struct Search{HWND owner;HWND found=nullptr;}s{owner};EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p)->BOOL{auto& s=*reinterpret_cast<Search*>(p);wchar_t cls[80]{};GetClassNameW(h,cls,80);if(GetWindow(h,GW_OWNER)==s.owner&&std::wstring(cls)==L"LumeCalendarTodoPicker"){s.found=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&s));return s.found;}
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Utc time;Files files;int ids=0,refreshes=0;bool changeZone=false;
    todo::TodoRepository repository(files,time,"isolated",[&]{return std::to_string(++ids);});repository.SwitchAccount("local");
    calendar::Preferences preferences;preferences.enabled=true;preferences.Active()->hasPosition=true;preferences.Active()->x=80;preferences.Active()->y=80;
    NativeCalendarWindow window(GetModuleHandleW(nullptr),[](calendar::Action){},[]{},[]{return calendar::Date{2026,9,25};});
    CalendarTodoPort port{&time,[&]{return repository.Read();},[&](const todo::Command& c){return repository.Dispatch(c,At(2026,9,25,12));},[]{return At(2026,9,25,12);},[&]{++refreshes;if(changeZone){repository.SwitchAccount("local");changeZone=false;}}};
    window.BindTodos(port);Expect(window.Configure(preferences)&&window.Start(),"integrated native calendar starts");Pump();Expect(refreshes==1,"start synchronizes timezone");
    Click(window,window.Layout().cells[12]);Expect(!window.AgendaOpen(),"select date does not open agenda");
    Click(window,window.Layout().create);Expect(window.TodoEditor().IsOpen(),"create header opens real editor");
    SendMessageW(Field(window.TodoEditor().Handle(),101),WM_KEYDOWN,VK_ESCAPE,0);Pump();
    Expect(!window.TodoEditor().IsOpen()&&GetFocus()==window.Handle(),"Escape closes create editor and restores owner focus");
    SendMessageW(window.Handle(),WM_KEYDOWN,VK_RETURN,0);Pump();Expect(window.TodoEditor().IsOpen(),"cancel restores logical Create focus");
    SetWindowTextW(Field(window.TodoEditor().Handle(),101),L"保留草稿");
    for(auto message:{WM_TIMECHANGE,WM_DISPLAYCHANGE}){SendMessageW(window.Handle(),message,0,0);Pump();Expect(window.TodoEditor().IsOpen(),"ordinary time or display refresh retains draft");}
    SendMessageW(window.Handle(),WM_TIMER,1,0);Pump();Expect(window.TodoEditor().IsOpen(),"midnight timer retains draft");
    changeZone=true;SendMessageW(window.Handle(),WM_TIMECHANGE,0,0);Pump();Expect(!window.TodoEditor().IsOpen(),"scope change cancels old timezone draft");
    Click(window,window.Layout().create);auto editor=window.TodoEditor().Handle();SetWindowTextW(Field(editor,101),L"跨月日程");
    Button(editor,102);auto picker=Picker(editor);Expect(picker!=nullptr,"date picker from integrated editor");
    SetWindowTextW(Field(picker,201),L"2026");SetWindowTextW(Field(picker,202),L"10");Button(picker,208);Button(editor,106);
    Expect(!window.TodoEditor().IsOpen()&&window.AgendaOpen()&&window.Model().Selected().month==10,"save switches month and opens agenda");
    Expect(GetFocus()==window.Handle(),"save restores keyboard focus to calendar");
    Expect(repository.Read().tasks.size()==1&&window.AgendaLayout().rows.size()==1,"saved task visible");
    Click(window,window.AgendaLayout().rows[0].edit);SendMessageW(Field(window.TodoEditor().Handle(),101),WM_KEYDOWN,VK_ESCAPE,0);Pump();
    Expect(!window.TodoEditor().IsOpen()&&GetFocus()==window.Handle(),"Escape closes row editor and restores owner focus");
    SendMessageW(window.Handle(),WM_KEYDOWN,VK_RETURN,0);Pump();Expect(window.TodoEditor().IsOpen(),"cancel restores logical row Edit focus");window.CancelTodoEditor();
    Click(window,window.AgendaLayout().rows[0].check);Expect(repository.Read().tasks[0].completed,"checkbox completes");
    Click(window,window.AgendaLayout().rows[0].check);Expect(!repository.Read().tasks[0].completed,"checkbox restores");
    Click(window,window.AgendaLayout().rows[0].edit);editor=window.TodoEditor().Handle();SetWindowTextW(Field(editor,101),L"修改成功");Button(editor,106);Expect(repository.Read().tasks[0].title==u8"修改成功","row editor commits update");
    Click(window,window.AgendaLayout().rows[0].remove);Expect(repository.Read().tasks[0].deletedAt.has_value()&&window.AgendaLayout().rows.empty(),"delete removes row with tombstone");
    SendMessageW(window.Handle(),WM_KEYDOWN,VK_RETURN,0);Pump();Expect(!window.AgendaOpen(),"last deletion restores actual List keyboard target");
    Click(window,window.Layout().list);
    // Long rows must reveal the focused operation, not the entire row bottom.
    const auto selected=window.Model().Selected();
    for(int i=0;i<3;++i){auto c=Add(repository.Read().scope);c.patch.schedule=todo::Schedule{*time.FromLocal({selected,9+i}),*time.FromLocal({selected,10+i})};c.patch.notes=std::string(600,'W');Expect(repository.Dispatch(c,At(2026,9,25,12)).ok,"long row fixture");}
    window.RefreshTodos();preferences.Active()->width=320;preferences.Active()->height=320;Expect(window.Configure(preferences),"compact configure");
    // Clicking List sets keyboard index 46; next Tab enters first checkbox.
    if(window.AgendaOpen())Click(window,window.Layout().list);
    Click(window,window.Layout().list);
    SendMessageW(window.Handle(),WM_KEYDOWN,VK_TAB,0);SendMessageW(window.Handle(),WM_KEYDOWN,VK_DOWN,0);Pump();
    const auto agenda=window.AgendaLayout();Expect(agenda.rows.size()==3&&agenda.rows[1].row.height>agenda.list.height,"fixture row exceeds viewport");
    Expect(agenda.rows[1].check.y>=agenda.list.y-.1f&&agenda.rows[1].check.Bottom()<=agenda.list.Bottom()+.1f,"arrow focus checkbox remains visible on tall row");
    Click(window,window.Layout().create);Expect(window.TodoEditor().IsOpen(),"editor before stop");window.Stop();Pump();Expect(!window.TodoEditor().IsOpen(),"stop releases editor");
    changeZone=true;const auto old=repository.Read().scope.generation;Expect(window.Start(),"restart calendar");Expect(repository.Read().scope.generation>old,"restart synchronizes changed timezone");
    Click(window,window.Layout().create);const auto hwnd=window.Handle();DestroyWindow(hwnd);Pump();Expect(!window.IsRunning()&&!window.TodoEditor().IsOpen(),"owner destruction releases editor and port view");
    // Public value ports may synchronously destroy their caller.
    for(int mode=0;mode<4;++mode){
        auto dying=std::make_unique<NativeCalendarWindow>(GetModuleHandleW(nullptr),[](calendar::Action){},[]{});
        bool armed=false;auto dangerous=port;
        if(mode==0)dangerous.read=[&]{if(armed)dying.reset();return repository.Read();};
        else if(mode==1)dangerous.refreshTime=[&]{if(armed)dying.reset();};
        else dangerous.now=[&]{if(armed)dying.reset();return At(2026,9,25,12);};
        dying->BindTodos(dangerous);Expect(dying->Configure(preferences),"destruction test configure");
        if(mode==3)Expect(dying->Start(),"start before clock destruction on toggle");
        armed=true;
        if(mode==0)dying->RefreshTodos();
        else if(mode==3){const auto target=dying->Handle();const auto r=dying->Layout().list;const auto dpi=GetDpiForWindow(target);SendMessageW(target,WM_LBUTTONDOWN,0,MAKELPARAM(static_cast<int>((r.x+r.width/2)*dpi/96),static_cast<int>((r.y+r.height/2)*dpi/96)));}
        else Expect(!dying->Start(),"start reports destroyed port callback");
        Expect(!dying,"port callback safely destroys native window");
    }
    std::cout<<"CALENDAR_TODO_WINDOW_PASS\n";
}
