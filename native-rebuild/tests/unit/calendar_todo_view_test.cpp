#include "platform/windows/calendar/calendar_todo_view.h"
#include "todo_time_cases.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace lume;using namespace lume::platform::windows;
void Check(bool x,const char* m){if(!x)throw std::runtime_error(m);}
void ScopedProjection(){
    todo_test::Utc utc;todo::RepositorySnapshot snapshot;
    for(int i=0;i<100;++i){
        todo::TaskView task;task.id=std::to_string(i);task.title="repeat";task.notes=std::string(10000,'x');
        task.schedule=todo::Schedule{todo_test::At(2020,1,1,9),todo_test::At(2020,1,1,10)};
        task.dueAt=task.schedule->endAt;task.day=calendar::Date{2020,1,1};task.recurrence=todo::Recurrence{todo::Frequency::Daily,1};
        snapshot.tasks.push_back(std::move(task));
    }
    CalendarTodoView view;view.Update(std::move(snapshot),&utc);
    Check(view.Snapshot().tasks.size()==100,"update retains no implicit two-year projection");
    view.PrepareDays({{2035,9,20},{2035,9,21}});
    Check(view.Snapshot().tasks.size()==300&&view.ForDay({2035,9,20}).size()==100,"far dates retain only requested recurrence rows without old 1500 limit");
    for(auto row:view.ForDay({2035,9,20}))Check(view.Stored(row)<100&&view.Snapshot().tasks[row].notes.size()==10000,"projected content and direct stored series mapping preserved");
    const auto* data=view.Snapshot().tasks.data();view.PrepareDays({{2035,9,21},{2035,9,20},{2035,9,20}});
    Check(view.Snapshot().tasks.data()==data&&view.Snapshot().tasks.size()==300,"same date set reuses projections");
    view.PrepareDays({{2036,1,1}});Check(view.Snapshot().tasks.size()==200&&view.ForDay({2035,9,20}).empty(),"navigation releases old projected contents");
    view.Update({});Check(view.Snapshot().tasks.empty(),"clearing releases stored and projected tasks");
}
void CachedTimeLayout(){
    struct Clock final:todo::ILocalTimePort {
        mutable int reads=0;todo::Millis offset=0;
        std::optional<todo::LocalDateTime> ToLocal(todo::Millis at)const override{++reads;return todo_test::Utc{}.ToLocal(at+offset);}
        std::optional<todo::Millis> FromLocal(const todo::LocalDateTime& local)const override{const auto at=todo_test::Utc{}.FromLocal(local);return at?std::optional<todo::Millis>(*at-offset):std::nullopt;}
    }clock;
    todo::RepositorySnapshot snapshot;const calendar::Date date{2026,9,25};
    for(int i=0;i<1500;++i){
        todo::TaskView task;task.id=std::to_string(i);task.day=date;
        const int minute=(i*31)%720,duration=15+(i*17)%180;
        task.schedule=todo::Schedule{todo_test::At(2026,9,25)+minute*60000,todo_test::At(2026,9,25)+(minute+duration)*60000};snapshot.tasks.push_back(std::move(task));
    }
    CalendarTodoView view;view.Update(snapshot);
    const auto& cached=view.TimeBlocksForDay(date,clock);auto expected=cached;
    for(std::size_t b=0;b<expected.size();){
        std::size_t e=b;float end=expected[b].end;std::vector<float> lanes;
        for(;e<expected.size()&&(e==b||expected[e].start<end);++e){auto& block=expected[e];int lane=0;
            while(lane<static_cast<int>(lanes.size())&&lanes[static_cast<std::size_t>(lane)]>block.start)++lane;
            if(lane==static_cast<int>(lanes.size()))lanes.push_back(block.end);else lanes[static_cast<std::size_t>(lane)]=block.end;block.lane=lane;end=std::max(end,block.end);}
        for(std::size_t i=b;i<e;++i)expected[i].lanes=static_cast<int>(lanes.size());b=e;
    }
    for(std::size_t i=0;i<cached.size();++i)Check(cached[i].lane==expected[i].lane&&cached[i].lanes==expected[i].lanes,"heap layout preserves existing lowest-free-lane overlap geometry");
    const auto reads=clock.reads;const auto* blocks=cached.data();const auto& sorted=view.SortedForDay(date);
    Check(view.TimeBlocksForDay(date,clock).data()==blocks&&clock.reads==reads&&view.SortedForDay(date).data()==sorted.data(),"unchanged repaint reuses sort lane and timezone conversions");
    const auto before=cached.front().start;clock.offset=3600000;view.Update(snapshot);
    Check(view.TimeBlocksForDay(date,clock).front().start==before+60&&clock.reads>reads,"timezone refresh invalidates cached local hours even with unchanged repository revision");
    Clock second;second.offset=7200000;Check(view.TimeBlocksForDay(date,second).front().start==before+120,"time-port replacement invalidates cached hours");
}
void RecurrenceParity(){
    todo_test::Utc utc;todo_test::DstPort dst;
    for(const auto* port:std::vector<const todo::ILocalTimePort*>{&utc,&dst})
    for(const auto frequency:{todo::Frequency::Daily,todo::Frequency::Weekly,todo::Frequency::Monthly,todo::Frequency::Weekdays})
    for(bool midnight:{false,true}){
        todo::TaskView task;task.id="series";task.title="title";task.notes="notes";task.subtasks.push_back({"step","title",true});
        task.dueAt=*port->FromLocal({{2026,1,31},midnight?0:10});
        if(!midnight)task.schedule=todo::Schedule{*task.dueAt-3600000,*task.dueAt};
        task.reminderAt=*task.dueAt-7200000;task.recurrence=todo::Recurrence{frequency,1};
        const std::vector<calendar::Date> days{{2026,3,28},{2026,3,29},{2026,3,30},{2026,3,31},{2026,4,1},{2026,11,2}};
        const auto all=todo::ProjectOccurrences(task,*port->FromLocal({{2026,11,3},23,59}),1000,*port);
        std::vector<todo::TaskView> expected;for(const auto& row:all)if(row.day&&std::find(days.begin(),days.end(),*row.day)!=days.end())expected.push_back(row);
        const auto scoped=todo::ProjectOccurrencesForDays(task,days,*port);Check(scoped.size()==expected.size(),"scoped recurrence matches existing monthly weekday midnight DST count");
        for(std::size_t i=0;i<scoped.size();++i){const auto& a=scoped[i];const auto& b=expected[i];Check(a.day==b.day&&a.dueAt==b.dueAt&&a.reminderAt==b.reminderAt&&a.projected&&a.notes==b.notes&&a.subtasks==b.subtasks,"scoped recurrence keeps existing metadata");if(a.schedule)Check(b.schedule&&a.schedule->startAt==b.schedule->startAt&&a.schedule->endAt==b.schedule->endAt,"scoped schedule preserves exact NextInstance shift");}
    }
}
int main(){try{
    ScopedProjection();RecurrenceParity();CachedTimeLayout();
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
