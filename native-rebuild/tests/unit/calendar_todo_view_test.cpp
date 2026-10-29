#include "platform/windows/calendar/calendar_todo_view.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace lume;using namespace lume::platform::windows;
void Check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
int main(){try{
    auto l=CalculateCalendarLayout(calendar::Theme::Spacious,820,500,0,true);
    Check(std::abs(l.agenda.x-570.03125f)<.02f&&std::abs(l.agenda.width-220.96875f)<.02f,"original agenda columns");
    auto a=CalculateAgendaLayout(l,820);Check(a.list.y==121&&a.list.height==358,"agenda lists schedules without a date heading");
    l=CalculateCalendarLayout(calendar::Theme::Spacious,320,320,0,true);a=CalculateAgendaLayout(l,320);
    Check(l.agenda.x==197&&a.list.y==91&&a.list.height==216,"compact agenda retains scroll area");
    CalendarTodoView view;todo::RepositorySnapshot s;
    for(int i=0;i<4;++i){todo::TaskView t;t.id=std::to_string(i);t.day=calendar::Date{2026,9,25};t.completed=i==0;s.tasks.push_back(t);}
    s.tasks[2].deletedAt=0;view.Update(s);const auto& day=view.ForDay({2026,9,25});
    Check(day.size()==3&&day[0]==1&&day[1]==3&&day[2]==0,"stable preview ordering and epoch tombstone exclusion");
    Check(view.ForDay({2026,9,26}).empty(),"other date empty");
    Check(TodoWide(u8"日程😀").size()==4,"Unicode task rendering");
    std::cout<<"CALENDAR_TODO_VIEW_PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
