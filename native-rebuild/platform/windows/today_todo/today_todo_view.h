#pragma once
#include "platform/windows/calendar/calendar_todo_view.h"
#include "platform/windows/widget_ui/widget_canvas.h"
#include <algorithm>
namespace lume::platform::windows {
enum class TodayTodoAction { None, Toggle };
// A day-filtered value view. It owns no repository, preference file or window.
class TodayTodoView final {
public:
    void Update(todo::RepositorySnapshot,const todo::ILocalTimePort&,todo::Millis);
    void Draw(WidgetCanvas&,float width,float height);
    int Hit(float x,float y) const; // -1 background, otherwise the row under the pointer
    TodayTodoAction ActionAt(float x,float y) const;
    bool Scroll(float delta,bool absolute=false);
    float ScrollOffset()const{return scroll_;}
    float MaximumScroll()const;
    float VisibleHeight()const{return std::max(0.f,height_-68);}
    std::optional<todo::TaskView> TaskAt(int hit) const;
    const todo::Scope& Scope() const {return todos_.Snapshot().scope;}
    bool Writable() const {return todos_.Snapshot().writable;}
    calendar::Date Date() const {return date_;}
    std::size_t Count() const {return rows_.size();}
    std::size_t Completed() const;
    void SetError(std::wstring error){error_=std::move(error);}
    // Row under the pointer, highlighted like the dynamic island; true when it changed.
    bool SetHover(int row){if(row==hover_)return false;hover_=row;return true;}
private:
    CalendarTodoView todos_;calendar::Date date_{};
    std::vector<std::size_t> rows_;float scroll_=0;
    float width_=320,height_=152;std::wstring error_;int hover_=-1;
};
}
