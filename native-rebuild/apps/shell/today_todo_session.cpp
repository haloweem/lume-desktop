#include "apps/shell/today_todo_session.h"
#include "apps/shell/widget_session_common.h"
#include "core/widget/widget_preferences_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/today_todo/today_todo_view.h"
#include "platform/windows/widget_ui/native_widget_window.h"
#include "platform/windows/calendar/calendar_todo_editor.h"
#include <windowsx.h>
#include <algorithm>
#include <array>
#include "core/todo/todo_time.h"
#include "platform/windows/widget_ui/LumeWidgetMenu.h"
#include "platform/windows/widget_ui/LumeWidgetColorPicker.h"
#include <utility>
namespace lume::shell {
using namespace platform::windows;
namespace {
// It can be dragged narrower than the other widgets, down to 220 DIP.
constexpr widget::Limits Limits{320,152,220,152,720,720};
constexpr UINT_PTR DateTimer=0xA11;

}
struct TodayTodoSession::Impl {
    Impl(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,
         CalendarTodoPort todos,settings::IFileStore* files,Impl* owner=nullptr):
        root(owner?owner:this),externalFiles(files),instance(instance),path(std::move(path)),notify(std::move(notify)),openSettings(std::move(settings)),
        todos(std::move(todos)),
        store(files?*files:localFiles,Limits),loaded(store.Load(this->path)),prefs(loaded.value),
        window(instance,L"Lume 今日待办",Limits,WidgetWindowPorts{
            [this](WidgetCanvas& c,float w,float h){view.Draw(c,w,h);},
            [this](WidgetAction a,const auto& bounds){Native(a,bounds);},
            {},
            [this]{if(!stopped&&!applying&&this->notify)this->notify(State(),false);},
            [this](HWND h,UINT m,WPARAM w,LPARAM l){return Event(h,m,w,l);},
            {},
            [this](float x,float y){return view.ActionAt(x,y)!=TodayTodoAction::None;},
            false,false,false // No hover close button; close remains in the context menu/settings.
        }){}
    ~Impl(){Shutdown();}
    std::wstring State(bool applied=false,bool saved=false)const {
        if(root!=this)return root->State(applied,saved);
        auto visibleError=error;
        if(visibleError.empty())for(const auto& child:children)if(child&&child->prefs.enabled&&!child->error.empty()){visibleError=child->error;break;}
        return widget_session::StateJson({"todayTodo",Enabled(),Running(),prefs.locked,prefs.solid?"solid":prefs.frosted?"frosted-glass":"liquid-glass",window.MaterialState(),loaded.state,visibleError,prefs.width,prefs.height,applied,saved&&visibleError.empty()});
    }
    bool Enabled()const{if(prefs.enabled)return true;for(const auto& child:children)if(child&&child->prefs.enabled)return true;return false;}
    bool Running()const{if(window.Handle()&&timer)return true;for(const auto& child:children)if(child&&child->Running())return true;return false;}
    HWND Handle()const{if(window.Handle())return window.Handle();for(const auto& child:children)if(child&&child->window.Handle())return child->window.Handle();return nullptr;}
    settings::IFileStore& Files(){return externalFiles?*externalFiles:localFiles;}
    void Refresh(){
        if(root==this)for(auto& child:children)if(child)child->Refresh();
        if(stopped||refreshing||!window.Handle())return;
        refreshing=true;
        const auto previous=view.Scope();
        if(todos.time&&todos.now&&todos.read)view.Update(todos.read(),*todos.time,todos.now());
        if(previous.accountKey!=view.Scope().accountKey||previous.generation!=view.Scope().generation)editor.Close(false);
        refreshing=false;window.Paint();
    }
    bool CheckDate(){
        if(!todos.time||!todos.now)return false;
        if(todos.refreshTime)todos.refreshTime();
        const auto local=todos.time->ToLocal(todos.now());
        if(!local)return false;
        if(view.Date()!=local->date)Refresh();
        const unsigned elapsed=((local->hour*60u+local->minute)*60u+local->second)*1000u+local->millisecond;
        timer=SetTimer(window.Handle(),DateTimer,std::clamp(86400000u-std::min(elapsed,86400000u),100u,3600000u),nullptr)!=0;
        return timer;
    }
    void StopRuntime(){
        editor.Close(false);
        if(const auto hwnd=window.Handle())KillTimer(hwnd,DateTimer);
        timer=false;pressed.reset();pressedHit=-1;window.Stop();view=TodayTodoView{};
    }
    bool Runtime(const widget::Preferences& value){
        if(!value.enabled){StopRuntime();return window.Configure(value);}
        // Have real data ready for the first paint, before showing the window.
        if(!todos.time||!todos.now||!todos.read)return false;
        view.Update(todos.read(),*todos.time,todos.now());
        if(!window.Configure(value)||!window.Start())return false;
        if(!CheckDate()){StopRuntime();return false;}
        return true;
    }
    bool Restore(){
        if(stopped)return false;
        bool restored=true;
        if(root==this){
            if(!childrenLoaded&&!path.empty()){
                childrenLoaded=true;
                for(std::size_t i=0;i<children.size();++i){const auto childPath=path+".instance-"+std::to_string(i+1)+".json";
                    if(Files().Exists(childPath)||Files().Exists(childPath+".bak"))
                        children[i]=std::make_unique<Impl>(instance,childPath,notify,openSettings,todos,&Files(),this);
                }
            }
            for(auto& child:children)if(child&&!child->Restore())restored=false;
        }
        applying=true;const bool ok=Runtime(prefs);applying=false;
        if(!ok){const bool layer=window.PlacementFailed();StopRuntime();error=layer?L"layer":L"runtime";}
        return ok&&restored;
    }
    bool Apply(widget::Preferences next){
        if(stopped||applying)return false;
        applying=true;const auto previous=prefs;
        if(!Runtime(next)){
            const bool layer=window.PlacementFailed();
            const bool rollback=Runtime(previous);error=layer?L"layer":rollback?L"runtime":L"rollback";applying=false;return false;
        }
        if(next.enabled){
            const auto bounds=widget::Clamped(window.Bounds(),Limits);next.x=bounds.x;next.y=bounds.y;next.placed=bounds.placed;
            next.width=bounds.width;next.height=bounds.height;
        }
        if(!store.Save(path,next)){
            const bool rollback=Runtime(previous);error=rollback?L"save":L"rollback";applying=false;return false;
        }
        prefs=next;loaded.state=widget::LoadState::Loaded;error.clear();applying=false;return true;
    }
    std::optional<std::wstring> HandlePage(ui::PageCommand command){
        if(stopped||applying)return {};
        if(command==ui::PageCommand::Ready)return State();
        auto next=prefs;
        switch(command){
            case ui::PageCommand::TodayTodoEnable:next.enabled=true;break;
            case ui::PageCommand::TodayTodoDisable:next.enabled=false;break;
            case ui::PageCommand::TodayTodoLiquidGlass:next.frosted=false;next.solid=false;break;
            case ui::PageCommand::TodayTodoFrostedGlass:next.frosted=true;next.solid=false;break;
            case ui::PageCommand::TodayTodoSolid:next.solid=true;break;
            case ui::PageCommand::TodayTodoReset:next=widget::Defaults(Limits);next.enabled=prefs.enabled;break;
            default:return {};
        }
        bool saved=true;
        std::vector<std::pair<Impl*,widget::Preferences>> changed;
        const auto apply=[&](Impl& target,widget::Preferences value){
            const auto before=target.prefs;
            if(!target.Apply(value))return false;
            changed.push_back({&target,before});return true;
        };
        if(root==this)for(auto& child:children)if(child){
            auto value=child->prefs;
            if(command==ui::PageCommand::TodayTodoDisable)value.enabled=false;
            else if(command==ui::PageCommand::TodayTodoLiquidGlass){value.frosted=false;value.solid=false;}
            else if(command==ui::PageCommand::TodayTodoFrostedGlass){value.frosted=true;value.solid=false;}
            else if(command==ui::PageCommand::TodayTodoSolid)value.solid=true;
            else continue;
            if(!apply(*child,value)){saved=false;break;}
        }
        if(saved)saved=apply(*this,next);
        if(!saved){for(auto i=changed.rbegin();i!=changed.rend();++i)if(!i->first->Apply(i->second))error=L"rollback";if(error.empty())error=L"save";}
        return State(true,saved);
    }
    void Native(WidgetAction action,const widget::Preferences& bounds){
        if(stopped||applying)return;
        if(action==WidgetAction::Settings){if(openSettings)openSettings();return;}
        const auto next=widget_session::ActionPreferences(action,prefs,bounds,Limits,true);
        if(!next)return;
        const bool ok=Apply(*next);if(notify)notify(State(),!ok);
    }
    std::optional<LRESULT> Event(HWND hwnd,UINT message,WPARAM w,LPARAM l){
        if(stopped)return {};
        if(message==WM_CONTEXTMENU){Context(hwnd,l);return 0;}
        if(message==WM_TIMER&&w!=DateTimer)return {};
        if(message==WM_TIMECHANGE||message==WM_SETTINGCHANGE||message==WM_POWERBROADCAST||message==WM_TIMER){
            if(timer&&!CheckDate()){error=L"runtime";if(notify)notify(State(),false);}
            return message==WM_POWERBROADCAST?TRUE:0;
        }
        if(message==WM_CAPTURECHANGED||message==WM_KILLFOCUS){pressed.reset();pressedHit=-1;return {};}
        if(message==WM_KEYDOWN){
            bool changed=false;
            switch(w){
                case VK_UP:changed=view.Scroll(-28);break;case VK_DOWN:changed=view.Scroll(28);break;
                case VK_PRIOR:changed=view.Scroll(-view.VisibleHeight());break;case VK_NEXT:changed=view.Scroll(view.VisibleHeight());break;
                case VK_HOME:changed=view.Scroll(0,true);break;case VK_END:changed=view.Scroll(view.MaximumScroll(),true);break;
                default:return {};
            }
            if(changed)window.Paint();return 0;
        }
        if(message==WM_MOUSEWHEEL){if(view.Scroll(-GET_WHEEL_DELTA_WPARAM(w)*84.f/WHEEL_DELTA))window.Paint();return 0;}
        if(message==WM_MOUSEMOVE){
            const auto dpi=GetDpiForWindow(hwnd);const float scale=96.f/(dpi?dpi:96);
            if(view.SetHover(view.Hit(GET_X_LPARAM(l)*scale,GET_Y_LPARAM(l)*scale)))window.Paint();
            TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};TrackMouseEvent(&tracking);
            return {};  // The widget window still handles dragging and its own hover.
        }
        if(message==WM_MOUSELEAVE){if(view.SetHover(-1))window.Paint();return {};}
        if(message!=WM_LBUTTONDOWN&&message!=WM_LBUTTONUP&&message!=WM_LBUTTONDBLCLK)return {};
        const auto dpi=GetDpiForWindow(hwnd);const float scale=96.f/(dpi?dpi:96);
        const float x=GET_X_LPARAM(l)*scale,y=GET_Y_LPARAM(l)*scale;
        const auto hit=view.Hit(x,y);const auto action=view.ActionAt(x,y);
        if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK){
            if(hit==-1||(hit>=0&&action==TodayTodoAction::None))return {};
            pressed=view.TaskAt(hit);pressedHit=hit;pressedScope=view.Scope();pressedDate=view.Date();pressedAction=action;
            SetCapture(hwnd);return 0;
        }
        if(pressedHit==-1)return {};
        const auto saved=pressed;const auto savedHit=pressedHit;const auto savedAction=pressedAction;const auto scope=pressedScope;const auto date=pressedDate;
        pressed.reset();pressedHit=-1;if(GetCapture()==hwnd)ReleaseCapture();
        if(hit!=savedHit||action!=savedAction)return 0;
        // Refresh before using a press: a day/scope or row change must cancel it.
        CheckDate();Refresh();const auto current=view.TaskAt(hit);
        if(!saved||!current||date!=view.Date()||scope.accountKey!=view.Scope().accountKey||scope.generation!=view.Scope().generation||
           saved->id!=current->id||saved->completed!=current->completed||saved->revision!=current->revision||saved->updatedAt!=current->updatedAt)return 0;
        if(current->projected){
            OpenEditor(*current);return 0;
        }
        if(!view.Writable()||!todos.dispatch){view.SetError(L"待办暂不可修改");window.Paint();return 0;}
        todo::Command command;command.scope=view.Scope();command.id=current->id;
        command.type=current->completed?todo::CommandType::Restore:todo::CommandType::Complete;
        Submit(command);return 0;
    }
    todo::CommandResult Submit(const todo::Command& command){
        todo::CommandResult result;result.error=todo::Error::ReadOnly;
        if(stopped||!todos.dispatch)return result;
        try{result=todos.dispatch(command);}catch(...){result.error=todo::Error::WriteFailed;}
        if(!stopped){Refresh();view.SetError(result.ok?L"":L"保存失败，请重试");window.Paint();}
        return result;
    }
    void OpenEditor(std::optional<todo::TaskView> task={}){
        if(editor.IsOpen()){SetForegroundWindow(editor.Handle());return;}
        if(!todos.time||!todos.now||!todos.read)return;
        const auto snapshot=todos.read();
        const auto found=std::find_if(snapshot.tasks.begin(),snapshot.tasks.end(),[&](const auto& t){return task&&t.id==task->id&&!t.deletedAt;});
        if(!snapshot.writable||(task&&found==snapshot.tasks.end())){view.SetError(L"待办暂不可编辑");window.Paint();return;}
        todo::Command seed;seed.scope=snapshot.scope;std::optional<todo::TaskView> existing;
        if(task){existing=*found;seed.id=found->id;seed.type=todo::CommandType::Update;}
        else {const auto now=todos.now();const auto local=todos.time->ToLocal(now);if(!local)return;
            seed.patch.schedule=todo::DefaultSchedule(local->date,now,*todos.time);
            if(!seed.patch.schedule){view.SetError(L"日期不可用");window.Paint();return;}}

        const auto hwnd=window.Handle();const auto dpi=GetDpiForWindow(hwnd);
        RECT owner{};GetWindowRect(hwnd,&owner);MONITORINFO monitor{sizeof(monitor)};
        if(!GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor))return;
        // Give the shared calendar form useful space even when the checklist is compact.
        const auto work=monitor.rcWork;const int width=std::min<int>(work.right-work.left,MulDiv(480,dpi,96));
        const int height=std::min<int>(work.bottom-work.top,MulDiv(560,dpi,96));
        const int left=std::clamp<int>((owner.left+owner.right-width)/2,work.left,work.right-width);
        const int top=std::clamp<int>((owner.top+owner.bottom-height)/2,work.top,work.bottom-height);
        const RECT host{left,top,left+width,top+height};calendar::Preferences appearance;
        appearance.material=prefs.frosted?calendar::Material::FrostedGlass:calendar::Material::LiquidGlass;
        const auto guard=alive;
        const bool ok=editor.Open(instance,hwnd,host,dpi,appearance,*todos.time,seed,todos.now(),existing,
            [this,guard](const todo::Command& command){
                if(!*guard){todo::CommandResult result;result.error=todo::Error::StaleScope;return result;}
                return Submit(command);
            },[this,guard]{if(*guard&&!stopped)Refresh();},todos.now);
        if(!ok){view.SetError(L"编辑窗口打开失败");window.Paint();}
    }
    bool CanCreate()const{
        for(const auto& child:root->children)if(!child||!child->prefs.enabled)return true;return false;
    }
    void NewList(Impl& source){
        if(root!=this){root->NewList(source);return;}
        if(stopped||path.empty()||!source.window.Handle())return;
        for(std::size_t i=0;i<children.size();++i){
            if(children[i]&&children[i]->prefs.enabled)continue;
            if(!children[i])children[i]=std::make_unique<Impl>(instance,path+".instance-"+std::to_string(i+1)+".json",notify,openSettings,todos,&Files(),this);
            auto next=widget::Defaults(Limits);const auto bounds=source.window.Bounds();
            next.enabled=true;next.frosted=source.prefs.frosted;next.solid=source.prefs.solid;next.panelColor=source.prefs.panelColor;next.placed=true;
            const auto dpi=GetDpiForWindow(source.window.Handle());const int step=MulDiv(32*static_cast<int>(i+1),dpi,96);
            next.x=bounds.x+step;next.y=bounds.y+step;MONITORINFO monitor{sizeof(monitor)};
            if(GetMonitorInfoW(MonitorFromWindow(source.window.Handle(),MONITOR_DEFAULTTONEAREST),&monitor)){
                const auto work=monitor.rcWork;const int right=std::max<int>(work.left,work.right-MulDiv(next.width,dpi,96));
                const int bottom=std::max<int>(work.top,work.bottom-MulDiv(next.height,dpi,96));
                next.x=std::clamp(next.x,static_cast<int>(work.left),right);next.y=std::clamp(next.y,static_cast<int>(work.top),bottom);
                if(next.x==bounds.x)next.x=std::clamp(bounds.x-step,static_cast<int>(work.left),right);
                if(next.y==bounds.y)next.y=std::clamp(bounds.y-step,static_cast<int>(work.top),bottom);
            }
            const bool ok=children[i]->Apply(next);
            if(ok)error.clear();
            if(!ok){source.view.SetError(L"新建清单失败");source.window.Paint();error=children[i]->error;}
            if(notify)notify(State(),!ok);return;
        }
        source.view.SetError(L"清单数量已达上限");source.window.Paint();
    }
    void Context(HWND hwnd,LPARAM l){
        if(menuOpen||applying||editor.IsOpen())return;
        POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(point.x==-1&&point.y==-1)GetCursorPos(&point);
        POINT local=point;ScreenToClient(hwnd,&local);const auto dpi=GetDpiForWindow(hwnd);const float scale=96.f/(dpi?dpi:96);
        CheckDate();Refresh();const auto selected=view.TaskAt(view.Hit(local.x*scale,local.y*scale));
        const auto scope=view.Scope();const auto day=view.Date();
        menuOpen=true;const auto command=LumeWidgetMenu::ShowTodayTodo(hwnd,point,selected.has_value(),view.Writable(),CanCreate(),prefs.frosted,prefs.locked,prefs.solid);menuOpen=false;
        if(stopped||!window.Handle()||!prefs.enabled)return;
        if(command==52){OpenEditor();return;}
        if(command==53){root->NewList(*this);return;}
        if(command==50||command==51){
            CheckDate();Refresh();
            if(!selected||day!=view.Date()||scope.accountKey!=view.Scope().accountKey||scope.generation!=view.Scope().generation||!todos.read)return;
            const auto snapshot=todos.read();const auto found=std::find_if(snapshot.tasks.begin(),snapshot.tasks.end(),[&](const auto& t){return t.id==selected->id&&!t.deletedAt;});
            if(found==snapshot.tasks.end()||(found->revision!=selected->revision||found->updatedAt!=selected->updatedAt)||!snapshot.writable)return;
            if(command==50){OpenEditor(*found);return;}
            todo::Command remove;remove.scope=snapshot.scope;remove.id=found->id;remove.type=todo::CommandType::Delete;Submit(remove);return;
        }
        if(command==21){
            // Like the calendar: the panel colour, which switches to the solid material.
            LumeWidgetColor color{static_cast<BYTE>((prefs.panelColor>>16)&255),static_cast<BYTE>((prefs.panelColor>>8)&255),static_cast<BYTE>(prefs.panelColor&255),255};
            if(!LumeWidgetColorPicker::Show(hwnd,color,RGB(44,44,46))||stopped||!window.Handle()||!prefs.enabled)return;
            auto next=prefs;next.solid=true;next.panelColor=(color.red<<16)|(color.green<<8)|color.blue;
            const bool ok=Apply(next);if(notify)notify(State(),!ok);return;
        }
        switch(command){
            case 1:Native(WidgetAction::Close,window.Bounds());break;
            case 8:Native(WidgetAction::Liquid,window.Bounds());break;
            case 9:Native(WidgetAction::Frosted,window.Bounds());break;
            case 20:Native(WidgetAction::Solid,window.Bounds());break;
            case 10:Native(WidgetAction::Reset,window.Bounds());break;
            case 11:Native(WidgetAction::Settings,window.Bounds());break;
            case 12:Native(WidgetAction::Lock,window.Bounds());break;
        }
    }
    void Shutdown(){if(stopped)return;stopped=true;*alive=false;for(auto& child:children)if(child)child->Shutdown();StopRuntime();}
    Impl* root;settings::IFileStore* externalFiles;bool childrenLoaded=false,menuOpen=false;
    HINSTANCE instance;std::string path;Notify notify;std::function<void()> openSettings;CalendarTodoPort todos;
    CalendarTodoEditor editor;std::shared_ptr<bool> alive=std::make_shared<bool>(true);
    LocalSettingsFileStore localFiles;widget::PreferencesStore store;widget::LoadedPreferences loaded;widget::Preferences prefs;
    TodayTodoView view;bool stopped=false,applying=false,timer=false,refreshing=false;
    std::optional<todo::TaskView> pressed;int pressedHit=-1;TodayTodoAction pressedAction=TodayTodoAction::None;todo::Scope pressedScope;calendar::Date pressedDate{};std::wstring error;
    NativeWidgetWindow window;
    std::array<std::unique_ptr<Impl>,31> children;
};
TodayTodoSession::TodayTodoSession(HINSTANCE instance,std::string path,Notify notify,std::function<void()> settings,CalendarTodoPort todos,settings::IFileStore* files):
    impl_(std::make_unique<Impl>(instance,std::move(path),std::move(notify),std::move(settings),std::move(todos),files)){}
TodayTodoSession::~TodayTodoSession()=default;
bool TodayTodoSession::Restore(){return impl_->Restore();}
std::optional<std::wstring> TodayTodoSession::HandlePage(ui::PageCommand command){return impl_->HandlePage(command);}
std::wstring TodayTodoSession::State()const{return impl_->State();}
void TodayTodoSession::Shutdown(){impl_->Shutdown();}
bool TodayTodoSession::Running()const{return impl_->Running();}
HWND TodayTodoSession::Handle()const{return impl_->Handle();}
const widget::Preferences& TodayTodoSession::Snapshot()const{return impl_->prefs;}
calendar::Date TodayTodoSession::DisplayedDate()const{return impl_->view.Date();}
void TodayTodoSession::Refresh(){impl_->Refresh();}
}
