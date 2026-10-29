#include "platform/windows/calendar/calendar_todo_editor.h"
#include "platform/windows/calendar/calendar_todo_ui.h"
#include "todo_test_support.h"
#include <windows.h>
#include <fstream>
#include <memory>
#include <vector>
using namespace lume::platform::windows;
using namespace todo_test;
namespace calendar=lume::calendar;
namespace todo=lume::todo;
namespace {
std::ofstream trace;
LRESULT CALLBACK TraceMessages(int code,WPARAM wp,LPARAM lp) {
 if(code>=0&&trace) {
  const auto& message=*reinterpret_cast<CWPSTRUCT*>(lp);
  switch(message.message) {
  case WM_MOUSEACTIVATE:case WM_ACTIVATE:case WM_ACTIVATEAPP:
  case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_COMMAND:
  case todo_ui::ActionMessage:case todo_ui::MouseMessage: {
   char name[64]{};GetClassNameA(message.hwnd,name,64);
   trace<<GetTickCount64()<<" hwnd="<<message.hwnd<<" class="<<name<<" id="<<GetDlgCtrlID(message.hwnd)
        <<" msg="<<message.message<<" wp="<<message.wParam<<" lp="<<message.lParam
        <<" focus="<<GetFocus()<<" active="<<GetActiveWindow()<<'\n';trace.flush();break;}
  default:break;
  }
 }
 return CallNextHookEx(nullptr,code,wp,lp);
}
void Pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
void Presented(HWND window){
 const auto deadline=GetTickCount64()+5000;
 while(IsWindow(window)&&!IsWindowVisible(window)&&GetTickCount64()<deadline){Pump();Sleep(5);}
 Expect(IsWindow(window)&&IsWindowVisible(window),"interaction target has presented its first frame");Pump();
}
HWND Find(HWND parent,int id){struct Search{int id;HWND out=nullptr;}data{id};EnumChildWindows(parent,[](HWND w,LPARAM p)->BOOL{auto *s=reinterpret_cast<Search*>(p);if(GetDlgCtrlID(w)==s->id){s->out=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&data));return data.out;}
HWND Popup(HWND owner){HWND w=nullptr;while((w=FindWindowExW(nullptr,w,L"LumeCalendarTodoPicker",nullptr))){if(GetWindow(w,GW_OWNER)==owner)return w;}return nullptr;}
void Click(HWND w,int id){Presented(w);auto b=Find(w,id);Expect(b!=nullptr,"control exists");SendMessageW(b,BM_CLICK,0,0);Pump();if(auto popup=Popup(w))Presented(popup);}
void MouseClick(HWND w,int id){Presented(w);auto b=Find(w,id);Expect(b!=nullptr,"mouse control exists");SendMessageW(b,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(4,4));SendMessageW(b,WM_LBUTTONUP,0,MAKELPARAM(4,4));Pump();if(auto popup=Popup(w))Presented(popup);}
void ExpectPrintClient(HWND window) {
 RECT r{};GetClientRect(window,&r);auto dc=CreateCompatibleDC(nullptr);
 BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;
 info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;
 auto image=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,image);
 PatBlt(dc,0,0,r.right,r.bottom,BLACKNESS);
 SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);GdiFlush();
 bool painted=false;for(int i=0;i<r.right*r.bottom;++i)if((static_cast<unsigned*>(bits)[i]&0xffffffu)!=0){painted=true;break;}
 SelectObject(dc,old);DeleteObject(image);DeleteDC(dc);
 Expect(painted,"popup foreground supports WM_PRINTCLIENT for material composition");
}
void Capture(HWND owner,HWND editor,const std::string &path){
 RECT host{};GetWindowRect(owner,&host);int width=host.right-host.left,height=host.bottom-host.top;HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void *bits=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);RECT rect{0,0,width,height};auto brush=CreateSolidBrush(RGB(24,26,30));FillRect(dc,&rect,brush);DeleteObject(brush);
 for(HWND w:{editor,Popup(editor)}){if(!w)continue;RECT r{};GetWindowRect(w,&r);HDC child=CreateCompatibleDC(screen);auto image=CreateCompatibleBitmap(screen,r.right-r.left,r.bottom-r.top);auto original=SelectObject(child,image);PrintWindow(w,child,0);auto region=CreateRectRgn(0,0,0,0);if(GetWindowRgn(w,region)!=ERROR){OffsetRgn(region,r.left-host.left,r.top-host.top);SelectClipRgn(dc,region);}BitBlt(dc,r.left-host.left,r.top-host.top,r.right-r.left,r.bottom-r.top,child,0,0,SRCCOPY);SelectClipRgn(dc,nullptr);DeleteObject(region);SelectObject(child,original);DeleteObject(image);DeleteDC(child);}
 BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(info.bmiHeader);header.bfSize=header.bfOffBits+static_cast<DWORD>(width*height*4);std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(&header),sizeof(header));out.write(reinterpret_cast<char*>(&info.bmiHeader),sizeof(info.bmiHeader));out.write(static_cast<char*>(bits),width*height*4);Expect(out.good(),"screenshot written");SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);
}
}
int main(int argc,char **argv){
 char tracePath[MAX_PATH]{};
 HHOOK traceHook=nullptr;
 if(GetEnvironmentVariableA("LUME_TODO_TRACE",tracePath,MAX_PATH)) {
  trace.open(tracePath,std::ios::trunc);
  traceHook=SetWindowsHookExW(WH_CALLWNDPROC,TraceMessages,nullptr,GetCurrentThreadId());
 }
 SetProcessDPIAware();const auto instance=GetModuleHandleW(nullptr);Utc time;calendar::Preferences prefs;
 WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"LumeTodoEditorIsolatedTest";RegisterClassW(&wc);
 auto owner=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"Isolated calendar editor test",WS_POPUP|WS_VISIBLE,40,40,820,500,nullptr,nullptr,instance,nullptr);Expect(owner!=nullptr,"isolated owner");
 RECT bounds{};GetWindowRect(owner,&bounds);CalendarTodoEditor editor;int writes=0,closed=0;todo::Command published;bool fail=true;
 auto save=[&](const todo::Command &c){++writes;published=c;todo::CommandResult r;r.ok=!fail;r.committed=!fail;return r;};
 auto seed=Add({"isolated",7});seed.patch.title=u8"中文日程 🗓";
 Expect(editor.Open(instance,owner,bounds,96,prefs,time,seed,At(2026,9,25,12),{},save,[&]{++closed;}),"editor opens");Presented(editor.Handle());
 ExpectPrintClient(editor.Handle());
 Expect(!IsWindowEnabled(owner),"owner disabled while editor open");
 Expect(((GetWindowLongPtrW(editor.Handle(),GWL_EXSTYLE)&WS_EX_LAYERED)!=0)==
        (GetPropW(editor.Handle(),L"LumeTodoGlassActive")!=nullptr),"popup layers only after shared glass is ready");
 Expect(!editor.Open(instance,owner,bounds,96,prefs,time,seed,At(2026,9,25),{},save),"second open rejected");
 auto title=Find(editor.Handle(),101),notes=Find(editor.Handle(),105);wchar_t name[16]{};GetClassNameW(title,name,16);Expect(lstrcmpW(name,L"Edit")==0||lstrcmpW(name,L"EDIT")==0,"real Unicode EDIT title");Expect(SendMessageW(title,EM_GETLIMITTEXT,0,0)==500&&SendMessageW(notes,EM_GETLIMITTEXT,0,0)==10000,"native UTF16 text limits");
 SetWindowTextW(title,L"中文新日程 🚀");SetWindowTextW(notes,L"中文备注\r\n第二行");
 SendMessageW(title,WM_IME_STARTCOMPOSITION,0,0);SendMessageW(title,WM_KEYDOWN,VK_RETURN,0);SendMessageW(title,WM_KEYDOWN,VK_ESCAPE,0);Expect(editor.IsOpen()&&writes==0,"IME Enter/Escape not stolen");SendMessageW(title,WM_IME_ENDCOMPOSITION,0,0);
 SendMessageW(title,EM_SETSEL,0,-1);SendMessageW(title,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"改动"));SendMessageW(title,WM_UNDO,0,0);Expect(todo_ui::Text(title)==L"中文新日程 🚀","native undo preserves Unicode");
 SendMessageW(editor.Handle(),WM_ACTIVATEAPP,FALSE,0);Expect(editor.IsOpen()&&writes==0,"app deactivation retains main draft");
 if(argc>1)Capture(owner,editor.Handle(),std::string(argv[1])+"/editor-820.bmp");
 Click(editor.Handle(),104);auto lead=Popup(editor.Handle());Expect(lead!=nullptr,"lead picker opens");SetWindowTextW(Find(lead,210),L"37");
 if(argc>1)Capture(owner,editor.Handle(),std::string(argv[1])+"/lead-820.bmp");
 Click(lead,208);Expect(!Popup(editor.Handle()),"lead done closes picker");Expect(writes==0,"picker change does not persist");
 MouseClick(editor.Handle(),104);lead=Popup(editor.Handle());Expect(lead!=nullptr,"mouse messages open picker");
 auto leadTrigger=Find(editor.Handle(),104);
 SendMessageW(leadTrigger,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(editor.Handle()),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));
 SendMessageW(lead,WM_ACTIVATE,WA_INACTIVE,reinterpret_cast<LPARAM>(editor.Handle()));
 MouseClick(editor.Handle(),104);Expect(!Popup(editor.Handle()),"queued same-trigger click closes without reopening");
 MouseClick(editor.Handle(),104);lead=Popup(editor.Handle());Expect(lead!=nullptr,"next trigger click opens normally");
 auto startTrigger=Find(editor.Handle(),102);
 SendMessageW(startTrigger,WM_MOUSEACTIVATE,reinterpret_cast<WPARAM>(editor.Handle()),MAKELPARAM(HTCLIENT,WM_LBUTTONDOWN));
 SendMessageW(lead,WM_ACTIVATE,WA_INACTIVE,reinterpret_cast<LPARAM>(editor.Handle()));
 MouseClick(editor.Handle(),102);auto switched=Popup(editor.Handle());Expect(switched&&Find(switched,201),"different trigger switches to date picker");
 SendMessageW(Find(switched,201),WM_KEYDOWN,VK_ESCAPE,0);Pump();
 Click(editor.Handle(),102);auto date=Popup(editor.Handle());Expect(date!=nullptr,"date picker opens");Expect(Find(date,201)&&Find(date,202)&&Find(date,205)&&Find(date,206),"real year month hour minute fields");
 if(argc>1)Capture(owner,editor.Handle(),std::string(argv[1])+"/date-820.bmp");
 SetWindowTextW(Find(date,201),L"0");Click(date,208);Expect(Popup(editor.Handle())==date,"invalid year remains editable");SetWindowTextW(Find(date,201),L"2026");
 Click(date,203);Expect(IsWindowVisible(Find(date,408))&&!IsWindowVisible(Find(date,300)),"month mode uses true month grid");Click(date,408);Expect(IsWindowVisible(Find(date,300)),"select month returns day grid");
 SendMessageW(Find(date,201),WM_KEYDOWN,VK_ESCAPE,0);Expect(!Popup(editor.Handle())&&editor.IsOpen(),"Escape closes child picker first");
 Click(editor.Handle(),106);Expect(writes==1&&editor.IsOpen(),"failed save keeps editor open");Expect(todo_ui::Text(title)==L"中文新日程 🚀"&&todo_ui::Text(notes)==L"中文备注\r\n第二行","failed save preserves complete text");Expect(published.patch.reminder&&published.patch.reminder->leadMilliseconds==37*60000,"custom reminder dispatched");
 auto saveButton=Find(editor.Handle(),106);SendMessageW(saveButton,BM_CLICK,0,0);SendMessageW(saveButton,BM_CLICK,0,0);Pump();
 Expect(writes==2&&editor.IsOpen(),"duplicate queued save notification is coalesced");writes=1;
 fail=false;Click(editor.Handle(),106);Expect(writes==2&&!editor.IsOpen()&&IsWindowEnabled(owner)&&closed==1,"successful save closes and restores owner");editor.Close();Expect(closed==1,"close idempotent");
 MoveWindow(owner,40,40,320,320,TRUE);GetWindowRect(owner,&bounds);Expect(editor.Open(instance,owner,bounds,96,prefs,time,seed,At(2026,9,25,12),{},save),"compact editor opens");Presented(editor.Handle());
 if(argc>1)Capture(owner,editor.Handle(),std::string(argv[1])+"/editor-320.bmp");
 SetFocus(Find(editor.Handle(),105));Pump();RECT editRect{},viewRect{};GetWindowRect(Find(editor.Handle(),105),&editRect);GetWindowRect(GetParent(GetParent(Find(editor.Handle(),105))),&viewRect);if(editRect.bottom>viewRect.bottom)std::cerr<<"notes="<<editRect.top<<","<<editRect.bottom<<" viewport="<<viewRect.top<<","<<viewRect.bottom<<" focus="<<GetDlgCtrlID(GetFocus())<<'\n';Expect(editRect.bottom<=viewRect.bottom,"Tab or focus scrolls notes into compact viewport");
 SendMessageW(Find(editor.Handle(),105),WM_KEYDOWN,VK_TAB,0);Expect(GetFocus()==Find(editor.Handle(),106),"tab skips hidden cancel for new task");
 Click(editor.Handle(),102);date=Popup(editor.Handle());if(argc>1)Capture(owner,editor.Handle(),std::string(argv[1])+"/date-320.bmp");
 SendMessageW(Find(date,201),WM_KEYDOWN,VK_ESCAPE,0);Click(editor.Handle(),104);lead=Popup(editor.Handle());if(argc>1)Capture(owner,editor.Handle(),std::string(argv[1])+"/lead-320.bmp");Click(lead,209);Expect(writes==2,"cancel reminder does not persist");editor.Close();
 EnableWindow(owner,FALSE);Expect(editor.Open(instance,owner,bounds,96,prefs,time,seed,At(2026,9,25),{},save),"disabled owner supported");editor.Close();Expect(!IsWindowEnabled(owner),"previous disabled owner remains disabled");EnableWindow(owner,TRUE);
 const int beforeOutside=writes;
 Expect(editor.Open(instance,owner,bounds,96,prefs,time,seed,At(2026,9,25),{},save),"outside click case opens");
 PostMessageW(owner,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(2,2));Pump();
 Expect(!editor.IsOpen()&&IsWindowEnabled(owner)&&writes==beforeOutside,"host background click cancels without dispatch");
 todo::TaskView old;old.title="old";old.schedule=seed.patch.schedule;old.reminder={todo::ReminderKind::LegacyAbsolute,0,At(2026,9,24),false};auto update=seed;update.type=todo::CommandType::Update;update.id="old";update.patch={};
 Expect(editor.Open(instance,owner,bounds,96,prefs,time,update,At(2026,9,25),old,save),"legacy edit opens");Presented(editor.Handle());Expect(IsWindowVisible(Find(editor.Handle(),107)),"cancel visible while editing");Click(editor.Handle(),106);Expect(!published.patch.reminder,"untouched old reminder remains omitted through UI");
 const DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 for(int i=0;i<20;++i){Expect(editor.Open(instance,owner,bounds,96,prefs,time,seed,At(2026,9,25),{},save),"repeat open");auto trigger=Find(editor.Handle(),102);SendMessageW(trigger,BM_CLICK,0,0);Pump();Expect(Popup(editor.Handle()),"repeat pending picker opens");editor.Close(false);Pump();}
 Expect(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=before+2,"repeated editor and picker close releases GDI");
 MoveWindow(owner,40,40,480,480,TRUE);GetWindowRect(owner,&bounds);Expect(editor.Open(instance,owner,bounds,144,prefs,time,seed,At(2026,9,25),{},save),"150 percent editor opens");RECT scaled{};GetWindowRect(editor.Handle(),&scaled);Expect(scaled.right-scaled.left==435&&scaled.bottom-scaled.top==447,"DPI scales DIP geometry");editor.Close(false);
 todo::Millis current=At(2026,9,25,12);
 Expect(editor.Open(instance,owner,bounds,96,prefs,time,seed,current,{},save,{},[&]{return current;}),"live now editor opens");
 Click(editor.Handle(),102);date=Popup(editor.Handle());current=At(2026,9,26,16,42);Click(date,207);
 Expect(todo_ui::Text(Find(date,205))==L"16"&&todo_ui::Text(Find(date,206))==L"42","now samples provider at click time");
 Click(date,208);Click(editor.Handle(),106);Expect(published.patch.schedule->startAt==current,"current timestamp reaches command");
 auto dyingClock=std::make_unique<CalendarTodoEditor>();bool destroyOnNow=false;
 Expect(dyingClock->Open(instance,owner,bounds,96,prefs,time,seed,current,{},save,{},[&]{if(destroyOnNow)dyingClock.reset();return current;}),"self destroying now provider opens");
 Click(dyingClock->Handle(),102);date=Popup(dyingClock->Handle());destroyOnNow=true;Click(date,207);
 Expect(!dyingClock&&IsWindowEnabled(owner),"now callback self destruction is safe");
 auto dying=std::make_unique<CalendarTodoEditor>();Expect(dying->Open(instance,owner,bounds,96,prefs,time,seed,At(2026,9,25),{},[&](const todo::Command&){dying.reset();todo::CommandResult r;r.ok=true;return r;}),"self destruction case opens");Click(dying->Handle(),106);Expect(!dying&&IsWindowEnabled(owner),"synchronous callback destruction safe");
 Expect(editor.Open(instance,owner,bounds,144,prefs,time,seed,At(2026,9,25),{},[&](const todo::Command&){DestroyWindow(owner);todo::CommandResult r;r.ok=true;return r;}),"destroy owner callback opens");Click(editor.Handle(),106);Expect(!editor.IsOpen(),"synchronous owner destruction safe");
 Pump();if(traceHook)UnhookWindowsHookEx(traceHook);std::cout<<"calendar todo editor tests passed\n";
}
