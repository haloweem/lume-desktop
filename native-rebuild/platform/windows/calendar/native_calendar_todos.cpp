#include "platform/windows/calendar/native_calendar_window.h"
#include "core/todo/todo_time.h"
#include <algorithm>
#include <utility>
namespace lume::platform::windows {
namespace {constexpr UINT_PTR kAgendaTimer=4;}
void NativeCalendarWindow::BindTodos(CalendarTodoPort port){
    CancelTodoEditor();todoPort_=std::move(port);RefreshTodos();
}
void NativeCalendarWindow::CancelTodoEditor(){todoEditor_.Close(false);}
bool NativeCalendarWindow::OpenTodoById(const std::string& id){
    if(!window_||todoBusy_)return false;
    // Do not discard an unsaved draft just because another reminder was opened.
    if(todoEditor_.IsOpen()){SetForegroundWindow(todoEditor_.Handle());return false;}
    pendingTodoId_=id;
    return PostMessageW(window_,kOpenTodo,0,0)!=FALSE;
}
void NativeCalendarWindow::OpenRequestedTodo(){
    // Initial position persistence calls Configure, which closes editors. Finish
    // those queued layout transactions before opening a reminder's task.
    if(initialPending_||dpiPending_){PostMessageW(window_,kOpenTodo,0,0);return;}
    const auto id=std::exchange(pendingTodoId_,{});
    if(id.empty()||!window_||todoBusy_||todoEditor_.IsOpen())return;
    RefreshTodos();
    const auto& tasks=todos_.Snapshot().tasks;
    const auto it=std::find_if(tasks.begin(),tasks.end(),[&](const auto& task){return task.id==id&&!task.deletedAt;});
    if(it==tasks.end())return;
    if(it->day){model_.SetMonth(it->day->year,it->day->month);model_.SelectDate(*it->day);}
    view_.agendaOpen=AgendaPane();view_.agendaScroll=0;view_.agendaFocus=-1;
    Paint();ArmAgendaTimer();SetForegroundWindow(window_);
    OpenTodo(static_cast<std::size_t>(it-tasks.begin()));
}
bool NativeCalendarWindow::RefreshTodoClock(){
    const auto alive=alive_;const auto nowProvider=todoPort_.now;
    const auto now=nowProvider?nowProvider():0;
    if(!*alive)return false;
    view_.now=now;return true;
}
void NativeCalendarWindow::RefreshTodos(){
    const auto alive=alive_;const auto read=todoPort_.read;
    auto snapshot=read?read():todo::RepositorySnapshot{};
    if(!*alive)return;
    if(!RefreshTodoClock())return;
    const auto previous=todos_.Snapshot().scope;
    todos_.Update(std::move(snapshot),todoPort_.time,view_.now);
    const auto current=todos_.Snapshot().scope;
    if(previous.accountKey!=current.accountKey||previous.generation!=current.generation)CancelTodoEditor();
    view_.todoAvailable=todoPort_.time&&todoPort_.dispatch&&todoPort_.read&&todoPort_.now;
    view_.todoStatus=!todos_.Snapshot().writable&&view_.todoAvailable?L"日程读取失败，数据暂不可写":TodoResultText(todo::Error::None,todos_.Snapshot().warning);
    if(window_)Paint();
}
void NativeCalendarWindow::ArmAgendaTimer(){
    if(!window_)return;
    KillTimer(window_,kAgendaTimer);
    if(SchedulesShown()&&IsWindowVisible(window_)){RefreshTodoClock();SetTimer(window_,kAgendaTimer,15000,nullptr);}
}
void NativeCalendarWindow::ToggleAgenda(){
    if(!view_.todoAvailable||!AgendaPane())return;
    if(!RefreshTodoClock())return;
    picker_.Close();view_.agendaOpen=!view_.agendaOpen;view_.agendaScroll=0;view_.agendaFocus=-1;
    Paint();ArmAgendaTimer();
}
void NativeCalendarWindow::OpenTodo(std::optional<std::size_t> index){
    if(!window_||!view_.todoAvailable||todoBusy_||todoEditor_.IsOpen())return;
    const auto alive=alive_;const auto nowProvider=todoPort_.now;
    const auto now=nowProvider();if(!*alive)return;
    view_.now=now;
    picker_.Close();todo::Command command;command.scope=todos_.Snapshot().scope;
    std::optional<todo::TaskView> existing;
    if(index){index=todos_.Stored(*index);if(*index>=todos_.Snapshot().tasks.size())return;existing=todos_.Snapshot().tasks[*index];if(existing->deletedAt)return;command.type=todo::CommandType::Update;command.id=existing->id;}
    else {
        command.patch.schedule=todo::DefaultSchedule(model_.Selected(),now,*todoPort_.time);
        if(!command.patch.schedule){view_.todoStatus=TodoResultText(todo::Error::InvalidTime);view_.agendaOpen=AgendaPane();Paint();ArmAgendaTimer();return;}
    }
    RECT host{};GetWindowRect(window_,&host);
    const int returnFocus=view_.focus,returnAgendaFocus=view_.agendaFocus;
    const auto saved=std::make_shared<bool>(false);
    const bool ok=todoEditor_.Open(instance_,window_,host,dpi_,preferences_,*todoPort_.time,command,now,existing,
        [this,alive,saved](const todo::Command& value){
            if(!*alive){todo::CommandResult error;error.error=todo::Error::StaleScope;return error;}
            auto result=SubmitTodo(value);*saved=result.ok;return result;
        },[this,alive,saved,returnFocus,returnAgendaFocus]{
            if(*alive&&!stopping_){view_.keyboardFocus=true;view_.agendaFocus=*saved?-1:returnAgendaFocus;
                view_.focus=*saved?46:returnFocus;if(window_&&IsWindowEnabled(window_))SetFocus(window_);Paint();}
        },todoPort_.now);
    if(!ok){view_.todoStatus=L"日程编辑器打开失败，请重试";Paint();}
}
todo::CommandResult NativeCalendarWindow::SubmitTodo(const todo::Command& command){
    todo::CommandResult result;result.error=todo::Error::ReadOnly;
    if(todoBusy_||!todoPort_.dispatch)return result;
    todoBusy_=true;const auto alive=alive_;const auto dispatch=todoPort_.dispatch;
    try{result=dispatch(command);}catch(...){result.error=todo::Error::WriteFailed;}
    if(!*alive)return result;
    todoBusy_=false;
    if(!window_||stopping_)return result;
    if(result.ok){
        todos_.Update(result.snapshot,todoPort_.time,view_.now);view_.todoStatus=TodoResultText(result.error,result.warning);
        if(command.type==todo::CommandType::Add||command.type==todo::CommandType::Update){
            std::optional<calendar::Date> selected;
            if(command.patch.schedule&&todoPort_.time){auto local=todoPort_.time->ToLocal(command.patch.schedule->startAt);if(local)selected=local->date;}
            if(selected){model_.SetMonth(selected->year,selected->month);model_.SelectDate(*selected);}
            view_.agendaOpen=AgendaPane();view_.agendaScroll=0;view_.agendaFocus=-1;ArmAgendaTimer();
        }
    }else view_.todoStatus=TodoResultText(result.error,result.warning);
    Paint();return result;
}
void NativeCalendarWindow::AgendaAction(int item){
    if(item<0||static_cast<std::size_t>(item/3)>=agenda_.rows.size()||todoBusy_)return;
    const auto index=agenda_.rows[static_cast<std::size_t>(item/3)].task;
    if(index>=todos_.Snapshot().tasks.size())return;
    if(item%3==1){OpenTodo(index);return;}
    const auto& task=todos_.Snapshot().tasks[index];
    // A later occurrence is completed through its series, not ahead of time.
    if(task.projected&&item%3==0)return;
    todo::Command command;
    command.scope=todos_.Snapshot().scope;command.id=task.id;
    command.type=item%3==2?todo::CommandType::Delete:task.completed?todo::CommandType::Restore:todo::CommandType::Complete;
    SubmitTodo(command);
}
bool NativeCalendarWindow::AgendaClick(float x,float y){
    // The todo theme's rows sit among the month and blank areas, which keep
    // their own clicks and dragging; the agenda pane takes every click inside it.
    const bool board=preferences_.theme==calendar::Theme::Todo;
    if(!(view_.agendaOpen||board)||!agenda_.area.Contains(x,y))return false;
    if(!agenda_.list.Contains(x,y))return !board;
    for(std::size_t i=0;i<agenda_.rows.size();++i){const auto& r=agenda_.rows[i];
        int action=r.check.Contains(x,y)?0:r.edit.Contains(x,y)?1:r.remove.Contains(x,y)?2:-1;
        if(action>=0){SetFocus(window_);view_.keyboardFocus=false;view_.agendaFocus=static_cast<int>(i)*3+action;AgendaAction(view_.agendaFocus);return true;}
    }
    if(!board){SetFocus(window_);view_.keyboardFocus=false;}
    return !board;
}
void NativeCalendarWindow::AgendaScroll(float delta){
    if(!view_.agendaOpen)return;
    view_.agendaScroll=std::clamp(view_.agendaScroll+delta,0.0f,agenda_.maxScroll);Paint();
}
bool NativeCalendarWindow::AgendaKey(UINT key){
    if(!view_.agendaOpen||!AgendaPane()||view_.agendaFocus<0||agenda_.rows.empty())return false;
    if(key==VK_RETURN||key==VK_SPACE){AgendaAction(view_.agendaFocus);return true;}
    const int delta=key==VK_UP?-3:key==VK_DOWN?3:key==VK_LEFT?-1:key==VK_RIGHT?1:0;
    if(!delta)return false;
    view_.agendaFocus=std::clamp(view_.agendaFocus+delta,0,std::max(0,static_cast<int>(agenda_.rows.size())*3-1));
    RevealAgendaFocus();
    Paint();return true;
}
void NativeCalendarWindow::RevealAgendaFocus(){
    if(view_.agendaFocus<0||agenda_.rows.empty())return;
    view_.agendaFocus=std::min(view_.agendaFocus,static_cast<int>(agenda_.rows.size())*3-1);
    const auto& row=agenda_.rows[static_cast<std::size_t>(view_.agendaFocus/3)];
    const auto r=view_.agendaFocus%3==0?row.check:view_.agendaFocus%3==1?row.edit:row.remove;
    if(r.y<agenda_.list.y)view_.agendaScroll=std::max(0.0f,view_.agendaScroll+r.y-agenda_.list.y);
    else if(r.Bottom()>agenda_.list.Bottom())view_.agendaScroll=std::min(agenda_.maxScroll,view_.agendaScroll+r.Bottom()-agenda_.list.Bottom());
}
}
