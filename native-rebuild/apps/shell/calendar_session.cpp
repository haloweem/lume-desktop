#include "apps/shell/calendar_session.h"
#include "platform/windows/widget_ui/LumeWidgetColorPicker.h"
#include <objidl.h>
#include <gdiplus.h>
#include <utility>
#include <algorithm>

namespace lume::shell {
namespace {
class EditorGraphics final {
public:
    EditorGraphics() { Gdiplus::GdiplusStartupInput input;ok_=Gdiplus::GdiplusStartup(&token_,&input,nullptr)==Gdiplus::Ok; }
    ~EditorGraphics() { if(ok_) Gdiplus::GdiplusShutdown(token_); }
    bool Ready() const { return ok_; }
private:
    ULONG_PTR token_=0;
    bool ok_=false;
};
}
CalendarSession::CalendarSession(HINSTANCE instance,std::string path,Notify notify,std::function<void()> openSettings,
                                 std::shared_ptr<TodoSession> todos)
    : path_(std::move(path)),notify_(std::move(notify)),openSettings_(std::move(openSettings)),
      loaded_(store_.Load(path_)),todos_(todos?std::move(todos):std::make_shared<TodoSession>(path_)),runtime_(instance,[this](calendar::Action action){HandleNative(action);},
          [this]{if(!stopped_ && openSettings_) openSettings_();}),
      controller_(loaded_.value,runtime_,[this](const calendar::Preferences& value){
          if(stopped_ || !store_.Save(path_,value)) return false;
          loaded_.status=calendar::LoadStatus::Loaded;
          loaded_.error=calendar::LoadError::None;
          return true;
      }) {runtime_.BindTodos(todos_->Port());
          runtime_.SetMaterialChanged([this]{if(!stopped_&&notify_)notify_(State(),false);});}
CalendarSession::~CalendarSession() { Shutdown(); }
bool CalendarSession::Restore() { return !stopped_ && controller_.Restore(); }
bool CalendarSession::Apply(calendar::Action action) { return !stopped_ && controller_.Handle(action); }
std::wstring CalendarSession::State(ui::CalendarPageResult result) const {
    auto state=ui::CalendarPageState(controller_,loaded_.status,result);
    const auto material=runtime_.MaterialState();
    const wchar_t* status=material==platform::windows::WidgetMaterialState::Ready?L"native-ready":
        material==platform::windows::WidgetMaterialState::Starting?L"native-starting":
        material==platform::windows::WidgetMaterialState::Detached?L"inactive":L"transparent-fallback";
    const auto offset=state.find(L"transparent-fallback");
    if(offset!=std::wstring::npos)state.replace(offset,std::wstring_view(L"transparent-fallback").size(),status);
    return state;
}
std::optional<std::wstring> CalendarSession::HandlePage(ui::PageCommand command,HWND owner) {
    if(stopped_ || editing_) return std::nullopt;
    if(command==ui::PageCommand::Ready) return State();
    if(command==ui::PageCommand::CalendarTextColor || command==ui::PageCommand::CalendarHighlightColor)
        return EditColor(command,owner);
    if(const auto action=ui::CalendarActionForPage(command)) {
        Apply(*action);
        return State(ui::CalendarPageResult::Applied);
    }
    return std::nullopt;
}
void CalendarSession::HandleNative(calendar::Action action) {
    // Bounds can arrive from initial placement while a native editor pumps messages.
    // Do not save, clear an existing error, or acknowledge that pending page command.
    // Recovery likewise stays read-only until an explicit user action saves it.
    if(stopped_ || editing_ || (action.type==calendar::ActionType::Bounds && action.automatic && loaded_.status!=calendar::LoadStatus::Loaded)) return;
    const bool ok=Apply(action);
    const auto notify=notify_;
    if(notify) notify(State(ui::CalendarPageResult::Applied),!ok);
}
std::wstring CalendarSession::EditColor(ui::PageCommand command,HWND owner) {
    if(!owner || !IsWindow(owner)) return State(ui::CalendarPageResult::Cancelled);
    EditorGraphics graphics;
    if(!graphics.Ready()) return State(ui::CalendarPageResult::Cancelled);
    const auto original=command==ui::PageCommand::CalendarTextColor?controller_.Snapshot().textColor:controller_.Snapshot().highlightColor;
    LumeWidgetColor color{static_cast<BYTE>((original.rgb>>16)&255),static_cast<BYTE>((original.rgb>>8)&255),
        static_cast<BYTE>(original.rgb&255),static_cast<BYTE>(original.alpha)};
    editing_=true;
    const auto epoch=++editorEpoch_;
    const HWND surface=runtime_.Handle();
    const bool enableSurface=surface && IsWindowEnabled(surface);
    if(enableSurface) EnableWindow(surface,FALSE);
    const bool accepted=LumeWidgetColorPicker::Show(owner,color);
    editing_=false;
    if(enableSurface && IsWindow(surface)) EnableWindow(surface,TRUE);
    if(!accepted || stopped_ || epoch!=editorEpoch_ || !IsWindow(owner)) return State(ui::CalendarPageResult::Cancelled);
    const int rgb=(static_cast<int>(color.red)<<16)|(static_cast<int>(color.green)<<8)|color.blue;
    Apply({command==ui::PageCommand::CalendarTextColor?calendar::ActionType::TextColor:calendar::ActionType::HighlightColor,rgb,color.alpha});
    return State(ui::CalendarPageResult::Applied);
}
void CalendarSession::CancelEditor() { ++editorEpoch_;runtime_.CancelTodoEditor(); }
bool CalendarSession::OpenTodo(const std::string& taskId) {
    if(stopped_||editing_)return false;
    const auto snapshot=todos_->Read();
    const auto exists=std::find_if(snapshot.tasks.begin(),snapshot.tasks.end(),[&](const auto& task){return task.id==taskId&&!task.deletedAt;});
    if(exists==snapshot.tasks.end())return false;
    if(!controller_.Running()&&!Apply({calendar::ActionType::Enable,1}))return false;
    const bool opened=runtime_.OpenTodo(taskId);
    const auto notify=notify_;if(notify)notify(State(),!opened);
    return opened;
}
void CalendarSession::Shutdown() {
    if(stopped_) return;
    stopped_=true;CancelEditor();controller_.Shutdown();
}
}
