#include "apps/shell/today_todo_session.h"
#include "apps/shell/todo_session.h"
#include "platform/windows/today_todo/today_todo_view.h"
#include "core/todo/todo_time.h"
#include "core/widget/widget_preferences_store.h"
#include "unique_test_directory.h"
#include <windowsx.h>
#include "platform/windows/calendar/calendar_todo_ui.h"
#include "platform/windows/widget_ui/desktop_level.h"
#include <objbase.h>
#include <map>
#include <functional>
#include <iostream>
#include <fstream>
#include <stdexcept>
using namespace lume;
using namespace lume::platform::windows;
namespace {
void Check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
class Files:public settings::IFileStore {
public:
 bool Read(const std::string& p,std::string& out)const override{auto i=data.find(p);if(i==data.end())return false;out=i->second;return true;}
 bool Exists(const std::string& p)const override{return data.count(p)!=0;}
 bool Write(const std::string& p,const std::string& text)override{if(fail)return false;data[p]=text;return true;}
 bool Rename(const std::string& a,const std::string& b)override{if(fail||!data.count(a))return false;data[b]=data[a];data.erase(a);return true;}
 bool Remove(const std::string& p)override{data.erase(p);return true;}
 std::map<std::string,std::string> data;bool fail=false;
};
void Pump(unsigned ms){const auto until=GetTickCount64()+ms;do{MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}while(GetTickCount64()<until);}
LPARAM Point(HWND h,int x,int y){const auto dpi=GetDpiForWindow(h);return MAKELPARAM(MulDiv(x,dpi,96),MulDiv(y,dpi,96));}
void Save(HWND,const std::filesystem::path&);
std::filesystem::path menuPreview;
struct MenuDrive{HWND owner;int selection;std::function<void()> before;bool driven=false;};
MenuDrive* menuDrive=nullptr;
void CALLBACK DriveMenu(HWND,UINT,UINT_PTR,DWORD){
 if(!menuDrive||menuDrive->driven)return;HWND menu=nullptr;
 EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p)->BOOL{wchar_t name[80]{};GetClassNameW(h,name,80);if(std::wstring(name)==L"LumeWidgetMenuWindow"){*reinterpret_cast<HWND*>(p)=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&menu));
 if(!menu)return;if(!menuPreview.empty()){Save(menu,menuPreview);menuPreview.clear();}auto* drive=menuDrive;drive->driven=true;if(drive->before)drive->before();
 if(!IsWindow(menu))return;PostMessageW(menu,WM_KEYDOWN,VK_HOME,0);
 for(int i=0;i<drive->selection;++i)PostMessageW(menu,WM_KEYDOWN,VK_DOWN,0);
 PostMessageW(menu,WM_KEYDOWN,drive->selection<0?VK_ESCAPE:VK_RETURN,0);
}
void Menu(HWND owner,bool row,int selection,std::function<void()> before={}){
 MenuDrive drive{owner,selection,std::move(before)};menuDrive=&drive;const auto timer=SetTimer(nullptr,0,10,DriveMenu);Check(timer!=0,"context menu driver timer");
 POINT point{MulDiv(row?60:140,GetDpiForWindow(owner),96),MulDiv(row?56:24,GetDpiForWindow(owner),96)};ClientToScreen(owner,&point);
 SendMessageW(owner,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(owner),MAKELPARAM(point.x,point.y));KillTimer(nullptr,timer);menuDrive=nullptr;Check(drive.driven,"real checklist context menu opened");
}
std::vector<HWND> Checklists(){std::vector<HWND> out;EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM p)->BOOL{wchar_t title[80]{};GetWindowTextW(h,title,80);if(std::wstring(title)==L"Lume 今日待办")reinterpret_cast<std::vector<HWND>*>(p)->push_back(h);return TRUE;},reinterpret_cast<LPARAM>(&out));return out;}
HWND Editor(HWND owner){HWND found=nullptr;while((found=FindWindowExW(nullptr,found,L"LumeCalendarTodoEditor",nullptr))){if(GetWindow(found,GW_OWNER)==owner)return found;}return nullptr;}
HWND Control(HWND parent,int id){struct Search{int id;HWND out=nullptr;} value{id};EnumChildWindows(parent,[](HWND h,LPARAM l)->BOOL{auto& s=*reinterpret_cast<Search*>(l);if(GetDlgCtrlID(h)==s.id){s.out=h;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&value));return value.out;}
void Presented(HWND hwnd){const auto deadline=GetTickCount64()+5000;while(IsWindow(hwnd)&&!IsWindowVisible(hwnd)&&GetTickCount64()<deadline)Pump(10);Check(IsWindow(hwnd)&&IsWindowVisible(hwnd),"shared calendar editor presents");}
void Click(HWND h,int x=31,int y=56){const auto p=Point(h,x,y);SendMessageW(h,WM_LBUTTONDOWN,MK_LBUTTON,p);SendMessageW(h,WM_LBUTTONUP,0,p);}
LRESULT CALLBACK Backdrop(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_PAINT){PAINTSTRUCT p{};const auto dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);
  for(int x=0;x<r.right;x+=32){RECT band{x,0,x+32,r.bottom};const auto b=CreateSolidBrush(RGB(30+x%115,83+x%70,107+x%90));FillRect(dc,&band,b);DeleteObject(b);}EndPaint(h,&p);return 0;}
 return DefWindowProcW(h,m,w,l);
}
void Save(HWND h,const std::filesystem::path& path){
 RECT r{};GetWindowRect(h,&r);const int w=r.right-r.left,ht=r.bottom-r.top;
 const auto screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);BITMAPINFO info{};
 info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-ht,1,32,BI_RGB};void* pixels=nullptr;
 const auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);Check(bitmap&&memory,"preview buffer");
 const auto old=SelectObject(memory,bitmap);Check(BitBlt(memory,0,0,w,ht,screen,r.left,r.top,SRCCOPY|CAPTUREBLT)!=FALSE,"capture own widget");
 BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+w*ht*4;
 std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&header),sizeof(header));out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));out.write(static_cast<const char*>(pixels),static_cast<std::streamsize>(w)*ht*4);
 SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);Check(out.good(),"save preview");
}
}
int main(int argc,char** argv){try{
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 Check(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM");
 const auto root=UniqueTestDirectory(std::filesystem::u8path(TODO_TEST_ARTIFACT_ROOT));
 shell::TodoSession repository((root/"calendar.json").u8string());
 auto port=repository.Port();auto now=port.now();port.now=[&]{return now;};
 const auto today=port.time->ToLocal(now)->date;
 const auto add=[&](const std::string& title,todo::Millis at){todo::Command c;c.scope=repository.Read().scope;c.patch.title=title;c.patch.schedule=todo::DefaultSchedule(port.time->ToLocal(at)->date,at,*port.time);const auto result=repository.Dispatch(c);Check(result.ok&&result.committed,"persist calendar task");return result.snapshot.tasks.back().id;};
 const auto first=add(u8"看一下最近的 AI 新闻",now);
 add(u8"优化安装文档",now);const auto completed=add(u8"装虚拟机",now);
 add(u8"整理产品文档，测试长标题不会跑出组件",now);add(u8"测试一键跳转",now);
 add("yesterday",now-86400000);add("tomorrow",now+86400000);
 todo::Command command;command.scope=repository.Read().scope;command.id=completed;command.type=todo::CommandType::Complete;
 Check(repository.Dispatch(command).ok,"calendar completes task");
 const auto deleted=add("deleted",now);command.id=deleted;command.type=todo::CommandType::Delete;Check(repository.Dispatch(command).ok,"delete task");
 const auto completeCount=[&]{const auto snap=repository.Read();return std::count_if(snap.tasks.begin(),snap.tasks.end(),[](const auto& t){return t.completed&&!t.deletedAt;});};
 {
  auto snapshot=repository.Read();todo::TaskView undated;undated.id="undated";undated.title="undated";snapshot.tasks.push_back(undated);
  TodayTodoView view;view.Update(snapshot,*port.time,now);Check(view.Date()==today&&view.Count()==5&&view.Completed()==1,"only today's dated live tasks, same completion as calendar");
  WidgetCanvas canvas;WidgetBitmap bitmap;Check(canvas.Begin(320,152,96),"canvas");view.Draw(canvas,320,152);Check(canvas.Finish(bitmap),"default render");
  Check(bitmap.pixels.front()==0&&view.Hit(18,18)==-1&&view.Hit(31,56)==0,"transparent shared material and header remains draggable");
  Check(view.ActionAt(31,56)==TodayTodoAction::Toggle&&view.ActionAt(251,56)==TodayTodoAction::None&&view.ActionAt(282,56)==TodayTodoAction::None,"only completion has a left-click target; row body is free of action buttons");
  Check(view.TaskAt(0)->id==first&&view.Scroll(10000),"default three visible rows scroll to the remaining tasks");Check(canvas.Begin(320,152,96),"scrolled canvas");view.Draw(canvas,320,152);Check(canvas.Finish(bitmap),"scrolled render");
  Check(view.TaskAt(view.Hit(31,112))->completed,"scrolling reveals final completed task");
  Check(!view.Scroll(10000)&&view.Scroll(-10000)&&!view.Scroll(-10000),"scroll clamps both ends");
  Check(view.Hit(12,76)==-1&&view.Hit(308,76)==-1,"no left or right arrow hit regions");
  Check(canvas.Begin(480,300,96),"larger canvas");view.Draw(canvas,480,300);Check(canvas.Finish(bitmap),"larger render");
  Check(view.MaximumScroll()==0&&view.Hit(31,168)==4,"enlarging the widget shows all five tasks without paging");
  view.Update(snapshot,*port.time,now+86400000);Check(view.Count()==1,"next day excludes all previous day's tasks");
  // Calendar projections appear on their future day, but retain calendar's editor-only semantics.
  auto repeating=snapshot;for(auto& task:repeating.tasks)if(task.id==first)task.recurrence=todo::Recurrence{};
  view.Update(repeating,*port.time,now+86400000);Check(view.Count()==2,"today includes calendar's recurring occurrence");
  bool projected=false;for(int i=0;i<2;++i)projected=projected||view.TaskAt(i)->projected;Check(projected,"recurrence is explicitly a preview, never silently completes an old series");
 }
 Files files;widget::PreferencesStore store(files,{320,152,320,152,720,720});
 bool failTask=false;unsigned dispatched=0;const auto dispatch=port.dispatch;
 port.dispatch=[&](const todo::Command& c){++dispatched;if(failTask){todo::CommandResult result;result.error=todo::Error::WriteFailed;return result;}return dispatch(c);};
 const bool preview=argc>1&&std::string(argv[1])=="--preview";
 if(preview)menuPreview=root/"today-todo-menu.bmp";
 HWND background=nullptr;auto prefs=widget::Defaults({320,152,320,152,720,720});
 if(preview){
  WNDCLASSW cls{};cls.lpfnWndProc=Backdrop;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"LumeTodayTodoPreviewBackdrop";RegisterClassW(&cls);
  const int x=GetSystemMetrics(SM_CXSCREEN)/2-210,y=GetSystemMetrics(SM_CYSCREEN)/2-190;
  background=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Lume preview",WS_POPUP|WS_VISIBLE,x-60,y-60,650,480,nullptr,nullptr,cls.hInstance,nullptr);
  prefs.x=x;prefs.y=y;prefs.placed=true;Check(store.Save("widget",prefs),"preview position");
 }
 unsigned changed=0;
 {
  shell::TodayTodoSession session(GetModuleHandleW(nullptr),"widget",{},[]{},port,&files);
  repository.SetChanged([&]{++changed;session.Refresh();});
  Check(session.Restore()&&!session.Running(),"disabled widget creates no window or timer");
  Check(!session.HandlePage(ui::PageCommand::WeatherEnable),"independent command boundary");
  auto reply=session.HandlePage(ui::PageCommand::TodayTodoEnable);Check(reply&&session.Running()&&session.DisplayedDate()==today,"enable today native window");
  Check(session.Snapshot().width==320&&session.Snapshot().height==152,"same weather default");
  if(preview){
   SetWindowPos(background,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
   desktop_level::Select(session.Handle());
   {desktop_level::PlacementScope placement(session.Handle());SetWindowPos(session.Handle(),HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
   Pump(900);
   SendMessageW(session.Handle(),WM_MOUSEMOVE,0,Point(session.Handle(),304,14));Pump(30);
   Save(session.Handle(),root/"today-todo.bmp");SendMessageW(session.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);Pump(60);Save(session.Handle(),root/"today-todo-completed.bmp");SendMessageW(session.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);
   {desktop_level::PlacementScope placement(session.Handle());SetWindowPos(session.Handle(),HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
   SetWindowPos(background,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
   std::cout<<"PREVIEW="<<(root/"today-todo.bmp").u8string()<<'\n';}
  const auto markComplete=[&]{todo::Command c;c.scope=repository.Read().scope;c.id=completed;c.type=todo::CommandType::Complete;Check(repository.Dispatch(c).ok,"restore completed scroll target");};
  SendMessageW(session.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);
  Click(session.Handle(),31,112);Check(completeCount()==0,"wheel scroll maps last visible checkbox to completed task");markComplete();
  SendMessageW(session.Handle(),WM_KEYDOWN,VK_HOME,0);
  SendMessageW(session.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA/2)),0);
  SendMessageW(session.Handle(),WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA/2)),0);
  Click(session.Handle(),31,112);Check(completeCount()==0,"fractional wheel deltas scroll directly without a scrollbar");markComplete();
  const auto resizeDpi=GetDpiForWindow(session.Handle());SendMessageW(session.Handle(),WM_ENTERSIZEMOVE,0,0);
  SetWindowPos(session.Handle(),nullptr,0,0,MulDiv(480,resizeDpi,96),MulDiv(300,resizeDpi,96),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  SendMessageW(session.Handle(),WM_EXITSIZEMOVE,0,0);
  Check(session.Snapshot().width==480&&session.Snapshot().height==300,"native border resize persists enlarged widget");
  Click(session.Handle(),31,168);Check(completeCount()==0,"enlarged native viewport shows all five tasks without scrolling");markComplete();
  session.HandlePage(ui::PageCommand::TodayTodoReset);changed=0;
  const auto before=completeCount();Click(session.Handle());Check(completeCount()==before+1&&changed==1,"native checkbox writes shared calendar repository");
  CalendarTodoView calendar;calendar.Update(repository.Read());Check(calendar.ForDay(today).size()==5,"calendar sees same data after checklist write");
  shell::TodoSession reload((root/"calendar.json").u8string());Check(reload.Read().tasks.front().completed,"completion survives independent session restart");
  failTask=true;const auto count=completeCount();Click(session.Handle());Check(completeCount()==count,"failed task save never checks off a row");failTask=false;
  const auto hwnd=session.Handle();const auto p=Point(hwnd,31,56);SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,p);
  now+=86400000;const auto priorDispatch=dispatched;SendMessageW(hwnd,WM_LBUTTONUP,0,p);
  Check(session.DisplayedDate()!=today&&dispatched==priorDispatch,"midnight during press cancels stale command");
  now-=86400000;SendMessageW(hwnd,WM_TIMECHANGE,0,0);Check(session.DisplayedDate()==today,"clock moving back refreshes today");
  const auto p2=Point(hwnd,31,56);SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,p2);
  todo::Command external;external.scope=repository.Read().scope;external.id=first;external.type=todo::CommandType::Restore;Check(repository.Dispatch(external).ok,"calendar-side restore synchronizes while widget visible");
  const auto prior=dispatched;SendMessageW(hwnd,WM_LBUTTONUP,0,p2);Check(dispatched==prior,"live calendar reorder during press cannot toggle another row");
  SendMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,p2);SendMessageW(hwnd,WM_CAPTURECHANGED,0,0);SendMessageW(hwnd,WM_LBUTTONUP,0,p2);Check(dispatched==prior,"lost capture cancels checkbox");
  Menu(hwnd,true,0,[&]{todo::Command change;change.scope=repository.Read().scope;change.id=first;change.type=todo::CommandType::Update;change.patch.title="changed while menu open";Check(repository.Dispatch(change).ok,"change task while modal menu is open");});
  Check(!Editor(hwnd),"stale menu target cannot edit a replaced task snapshot");
  Menu(hwnd,true,0);auto editor=Editor(hwnd);Check(editor&&!IsWindowEnabled(hwnd),"row context menu opens shared calendar editor");Presented(editor);
  auto titleControl=Control(editor,101),saveControl=Control(editor,106);Check(titleControl&&saveControl,"real shared editor controls");
  SetWindowTextW(titleControl,L"修改后的今日待办");failTask=true;SendMessageW(saveControl,BM_CLICK,0,0);Pump(60);
  Check(IsWindow(editor)&&todo_ui::Text(titleControl)==L"修改后的今日待办"&&repository.Read().tasks.front().title!=u8"修改后的今日待办","failed edit retains draft and old shared data");
  failTask=false;SendMessageW(saveControl,BM_CLICK,0,0);Pump(60);
  Check(!IsWindow(editor)&&IsWindowEnabled(hwnd)&&repository.Read().tasks.front().title==u8"修改后的今日待办","successful direct edit persists to calendar and releases editor");
  failTask=true;Menu(hwnd,true,1);Check(!repository.Read().tasks.front().deletedAt,"failed delete preserves shared task");
  failTask=false;Menu(hwnd,true,1);Check(repository.Read().tasks.front().deletedAt.has_value(),"bin deletes the original shared task");
  calendar.Update(repository.Read());Check(calendar.ForDay(today).size()==4,"calendar immediately excludes task deleted from checklist");
  const auto countBeforeCreate=repository.Read().tasks.size();Menu(hwnd,false,0);editor=Editor(hwnd);Check(editor!=nullptr,"background menu creates a new task");Presented(editor);
  SetWindowTextW(Control(editor,101),L"菜单新建的今日待办");SendMessageW(Control(editor,106),BM_CLICK,0,0);Pump(60);
  Check(!IsWindow(editor)&&repository.Read().tasks.size()==countBeforeCreate+1,"new task saves directly into shared repository");
  Check(repository.Read().tasks.back().day==std::optional<calendar::Date>{today},"new task defaults to today");
  files.fail=true;reply=session.HandlePage(ui::PageCommand::TodayTodoLiquidGlass);Check(reply&&reply->find(L"\"error\":\"save\"")!=std::wstring::npos&&session.Snapshot().frosted&&session.Running(),"preference failure rolls back material");files.fail=false;
  const auto total=repository.Read().tasks.size();session.HandlePage(ui::PageCommand::TodayTodoReset);Check(repository.Read().tasks.size()==total,"reset never removes calendar tasks");
  Menu(session.Handle(),true,0);const auto closingEditor=Editor(session.Handle());Check(closingEditor!=nullptr,"editor opened before disabling widget");
  SendMessageW(session.Handle(),WM_CLOSE,0,0);Check(!IsWindow(closingEditor),"closing checklist also releases its shared editor");Check(!session.Running()&&!session.Snapshot().enabled,"native close persists disabled");Check(repository.Read().tasks.size()==total,"close preserves tasks");
  session.HandlePage(ui::PageCommand::TodayTodoEnable);Check(session.Running(),"reopen window");
  // The menu no longer offers a new checklist; Escape still cancels it normally.
  auto main=session.Handle();Menu(main,false,-1);Check(IsWindow(main)&&Checklists().size()==1,"context menu cancels without creating checklists");
  const auto completedBefore=completeCount();Click(main);Check(completeCount()==completedBefore+1,"checkbox submits to the shared calendar data");
  session.Shutdown();Check(Checklists().empty()&&!session.HandlePage(ui::PageCommand::TodayTodoEnable),"shutdown releases every independent instance");
  repository.SetChanged({});
 }
 {
  shell::TodayTodoSession restored(GetModuleHandleW(nullptr),"widget",{},[]{},port,&files);
  Check(restored.Restore()&&restored.Running()&&Checklists().size()==1,"restart restores the enabled checklist");
  restored.Shutdown();Check(Checklists().empty(),"restored checklist closes cleanly");
 }
 if(background)DestroyWindow(background);
 CoUninitialize();std::cout<<"TODAY_TODO_PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
