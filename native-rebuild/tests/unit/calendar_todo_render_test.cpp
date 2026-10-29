#include "platform/windows/calendar/calendar_renderer.h"
#include "todo_test_support.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
using namespace lume;using namespace lume::platform::windows;using namespace todo_test;
void Save(const CalendarBitmap& b,const std::filesystem::path& file){
    std::ofstream f(file,std::ios::binary);BITMAPFILEHEADER h{};h.bfType=0x4d42;h.bfOffBits=54;h.bfSize=54+static_cast<DWORD>(b.pixels.size()*4);
    BITMAPINFOHEADER i{};i.biSize=40;i.biWidth=static_cast<LONG>(b.width);i.biHeight=-static_cast<LONG>(b.height);i.biPlanes=1;i.biBitCount=32;
    f.write(reinterpret_cast<char*>(&h),14);f.write(reinterpret_cast<char*>(&i),40);
    for(auto p:b.pixels){const auto bg=32*(255-(p>>24))/255;p=0xff000000u|((std::min)(255u,((p>>16)&255)+bg)<<16)|((std::min)(255u,((p>>8)&255)+bg)<<8)|(std::min)(255u,(p&255)+bg);f.write(reinterpret_cast<char*>(&p),4);}
}
int main(){
    Utc utc;CalendarTodoView tasks;todo::RepositorySnapshot s;s.writable=true;s.scope={"local",1};
    const char* titles[]={u8"项目例会",u8"设计评审",u8"整理日程",u8"完成记录",u8"明日计划"};
    for(int i=0;i<5;++i){todo::TaskView t;t.id=std::to_string(i);t.title=titles[i];t.day=calendar::Date{2026,9,25};t.completed=i==3;t.color=static_cast<todo::Color>(i+1);t.schedule=todo::Schedule{At(2026,9,25,9+i),At(2026,9,25,10+i)};if(i==1)t.notes=u8"保留原版布局与交互";s.tasks.push_back(t);}tasks.Update(s);
    calendar::Preferences prefs;prefs.material=calendar::Material::FrostedGlass;calendar::CalendarModel model({2026,9,25});CalendarRenderer renderer;
    CalendarRenderState state;state.agendaOpen=true;state.todoAvailable=true;state.todos=&tasks;state.time=&utc;state.now=At(2026,9,25,12);
    std::filesystem::path output;if(const auto path=std::getenv("LUME_CALENDAR_TODO_PROBE_OUTPUT")){output=path;std::filesystem::create_directories(output);}
    std::ofstream json;if(!output.empty())json.open(output/"native-agenda-layout.json");if(json)json<<"[";bool first=true;
    for(auto theme:{calendar::Theme::Spacious,calendar::Theme::Split})for(auto size:{std::pair<int,int>{820,500},{600,420},{480,390},{440,400},{380,390},{320,320},{1440,720}}){
        prefs.theme=theme;model.SetTheme(theme);prefs.Active()->width=size.first;prefs.Active()->height=size.second;
        CalendarBitmap bitmap;CalendarLayout layout;CalendarAgendaLayout agenda;
        Expect(renderer.Render(prefs,model,96,bitmap,state,&layout,&agenda),"render agenda and lunar");
        const auto button=layout.previous;
        const auto sample=static_cast<std::size_t>(button.y+button.height/2)*bitmap.width+static_cast<std::size_t>(button.x+button.width*.22f);
        const auto fallbackAlpha=bitmap.pixels[sample]>>24;
        CalendarBitmap glassBitmap;auto glassState=state;glassState.materialReady=true;
        Expect(renderer.Render(prefs,model,96,glassBitmap,glassState),"render foreground over native glass");
        Expect((glassBitmap.pixels[sample]>>24)<fallbackAlpha,"native navigation glass has no duplicate painted fill");
        Expect(agenda.rows.size()==5&&agenda.rows[0].row.y==agenda.list.y,"all task rows share measured scroll origin");
        for(auto p:bitmap.pixels)Expect((p&255)<=(p>>24)&&((p>>8)&255)<=(p>>24)&&((p>>16)&255)<=(p>>24),"premultiplied todo pixels");
        if(size.first==820&&theme==calendar::Theme::Spacious){
            Expect(agenda.rows[0].row.height>=50&&agenda.rows[0].row.height<=90,"one-line agenda remains compact with shared reading font");
            Expect(agenda.rows[1].row.y>=agenda.rows[0].row.Bottom(),"next agenda row follows measured text without overlap");
        }
        if(json){if(!first)json<<',';first=false;json<<"{\"theme\":\""<<calendar::ThemeName(theme)<<"\",\"width\":"<<size.first<<",\"height\":"<<size.second<<",\"rects\":{";
            bool sep=false;for(const auto& item:std::vector<std::pair<const char*,CalendarRect>>{{"#calendar-agenda",agenda.area},{".calendar-agenda-heading",agenda.heading},{".calendar-todo-list",agenda.list},{".calendar-todo-row",agenda.rows[0].row},{".weekdays",layout.weekdays},{"#days",layout.days}}){if(sep)json<<',';sep=true;const auto r=item.second;json<<'"'<<item.first<<"\":["<<r.x<<','<<r.y<<','<<r.width<<','<<r.height<<']';}json<<"}}";}
        if(!output.empty())Save(bitmap,output/(std::string(calendar::ThemeName(theme))+"-"+std::to_string(size.first)+"x"+std::to_string(size.second)+"-agenda.bmp"));
    }
    // September 19's long festival name used to be centered past both cell
    // edges and hard-clipped through Chinese glyphs. Match the source's 2-DIP
    // label padding at fractional DPI, with room for a complete ellipsis.
    prefs.theme=calendar::Theme::Split;model.SetTheme(prefs.theme);
    prefs.Active()->width=820;prefs.Active()->height=500;
    for(const unsigned dpi:{96u,120u,144u,192u}){
        CalendarBitmap bitmap;CalendarLayout layout;
        Expect(renderer.Render(prefs,model,dpi,bitmap,{},&layout),"long festival render");
        const auto day=std::find_if(model.Grid().begin(),model.Grid().end(),[](const auto& item){return item.date==calendar::Date{2026,9,19};});
        Expect(day!=model.Grid().end(),"long festival fixture found");
        const auto r=layout.cells[static_cast<std::size_t>(day-model.Grid().begin())];
        const float labelTop=r.y+(r.height-layout.numberDiameter-layout.dayGap-layout.lunarHeight)/2+
            layout.numberDiameter+layout.dayGap;
        unsigned labelPixels=0,clippedEdgePixels=0;
        for(unsigned y=static_cast<unsigned>(std::floor(labelTop*dpi/96));y<static_cast<unsigned>(std::ceil((labelTop+layout.lunarHeight)*dpi/96));++y)
            for(unsigned x=static_cast<unsigned>(std::floor(r.x*dpi/96));x<static_cast<unsigned>(std::ceil(r.Right()*dpi/96));++x){
                const auto pixel=bitmap.pixels[static_cast<std::size_t>(y)*bitmap.width+x];
                const auto red=(pixel>>16)&255,green=(pixel>>8)&255,blue=pixel&255;
                if((pixel>>24)<20||green<=red*1.35f||blue<=red||green<=blue*1.1f)continue;
                ++labelPixels;
                const float center=(static_cast<float>(x)+.5f)*96/dpi;
                if(center<r.x+1||center>r.Right()-1)++clippedEdgePixels;
            }
        Expect(labelPixels>10,"long festival remains visibly rendered");
        Expect(clippedEdgePixels==0,"long festival keeps complete glyphs inside cell padding instead of clipping both ends");
        if(!output.empty())Save(bitmap,output/("split-long-festival-"+std::to_string(dpi)+".bmp"));
    }
    // October 18/23 have festival labels; 19/20/21/22/24 do not. Their
    // numeric glyphs must share a row even when the second line is absent.
    calendar::CalendarModel october({2026,10,9},calendar::Theme::Split);
    for(const auto size:{std::pair<int,int>{820,500},{680,420},{480,390}})
    for(const unsigned dpi:{96u,120u,144u,192u}) {
        prefs.Active()->width=size.first;prefs.Active()->height=size.second;
        CalendarBitmap bitmap;CalendarLayout layout;
        Expect(renderer.Render(prefs,october,dpi,bitmap,{},&layout),"October alignment render");
        int firstTop=-1;
        for(int date=18;date<=24;++date){
            const auto day=std::find_if(october.Grid().begin(),october.Grid().end(),[&](const auto& item){return item.date==calendar::Date{2026,10,date};});
            const auto r=layout.cells[static_cast<std::size_t>(day-october.Grid().begin())];
            int top=-1;
            for(unsigned y=static_cast<unsigned>(std::ceil(r.y*dpi/96));y<static_cast<unsigned>(r.Bottom()*dpi/96);++y)
            for(unsigned x=static_cast<unsigned>(std::ceil((r.x+2)*dpi/96));x<static_cast<unsigned>((r.Right()-2)*dpi/96);++x){
                const auto pixel=bitmap.pixels[static_cast<std::size_t>(y)*bitmap.width+x];
                const int red=(pixel>>16)&255,green=(pixel>>8)&255,blue=pixel&255;
                if((pixel>>24)>80&&std::abs(red-green)<4&&std::abs(red-blue)<4&&red>70)
                    if(top<0)top=static_cast<int>(y);
            }
            Expect(top>=0,"date numeric pixels visible");
            if(firstTop<0)firstTop=top;
            Expect(std::abs(top-firstTop)<=2,"festival and ordinary dates share a numeric baseline");
        }
        if(!output.empty())Save(bitmap,output/("split-october-"+std::to_string(size.first)+"-"+std::to_string(dpi)+".bmp"));
    }
    // Todo theme: next item, the selected day and two weeks of upcoming items.
    {
        auto board=s;
        for(const auto& extra:std::vector<std::pair<int,const char*>>{{27,u8"产品周会 · Weekly sync"},{30,u8"提交季度总结"}}){
            todo::TaskView t;t.id="later-"+std::to_string(extra.first);t.title=extra.second;t.day=calendar::Date{2026,9,extra.first};
            t.schedule=todo::Schedule{At(2026,9,extra.first,15),At(2026,9,extra.first,16)};board.tasks.push_back(t);}
        CalendarTodoView view;view.Update(board);
        auto boardState=state;boardState.agendaOpen=false;boardState.todos=&view;
        prefs.theme=calendar::Theme::Todo;model.SetTheme(prefs.theme);prefs.material=calendar::Material::Solid;
        for(auto size:{std::pair<int,int>{820,500},{600,420},{480,390},{320,320},{1440,720}}){
            prefs.Active()->width=size.first;prefs.Active()->height=size.second;
            CalendarBitmap bitmap;CalendarLayout layout;CalendarAgendaLayout agenda;
            Expect(renderer.Render(prefs,model,96,bitmap,boardState,&layout,&agenda),"render todo theme");
            Expect(layout.list.width==0&&layout.boardToday.width>0,"todo theme has a today button and no agenda toggle");
            Expect(!agenda.rows.empty()&&view.Snapshot().tasks[agenda.rows[0].task].id=="4","next item is the earliest unfinished task that has not ended");
            for(const auto& row:agenda.rows)Expect(row.row.x>=0&&row.row.Right()<=size.first&&row.row.Bottom()<=size.second,"todo theme rows stay inside the widget");
            for(const auto& cell:layout.cells)Expect(cell.Right()<=size.first+.5f&&cell.Bottom()<=size.second+.5f,"month cells stay inside the widget");
            if(size.first==820){
                Expect(agenda.dayMaxScroll>0,"the day list scrolls when it does not fit");
                auto scrolled=boardState;scrolled.boardDayScroll=1000;CalendarAgendaLayout moved;
                Expect(renderer.Render(prefs,model,96,bitmap,scrolled,&layout,&moved),"render scrolled day list");
                Expect(std::any_of(moved.rows.begin(),moved.rows.end(),[&](const auto& row){return view.Snapshot().tasks[row.task].id=="3";}),"scrolling reveals the last item");
                const auto upcoming=std::count_if(agenda.rows.begin(),agenda.rows.end(),[&](const auto& row){return row.row.y>=layout.boardUpcoming.y;});
                Expect(upcoming>=2,"upcoming list shows later days");
                Expect(layout.HitTest(layout.boardToday.x+4,layout.boardToday.y+4).kind==CalendarHitKind::Today,"today button hit");
            }
            if(!output.empty())Save(bitmap,output/("todo-"+std::to_string(size.first)+"x"+std::to_string(size.second)+".bmp"));
        }
        // 日 / 周 / 月 views.
        for(auto mode:{calendar::BoardView::Day,calendar::BoardView::Week,calendar::BoardView::Month})
        for(auto size:{std::pair<int,int>{820,500},{480,390},{1440,720}}){
            prefs.boardView=mode;prefs.Active()->width=size.first;prefs.Active()->height=size.second;
            CalendarBitmap bitmap;CalendarLayout layout;CalendarAgendaLayout agenda;
            Expect(renderer.Render(prefs,model,96,bitmap,boardState,&layout,&agenda),"render todo theme view");
            Expect(layout.boardBody.height>100&&(size.first<820||!agenda.rows.empty()),"view shows the day's items");
            Expect(layout.HitTest(layout.boardViews[2].x+4,layout.boardViews[2].y+4).kind==CalendarHitKind::View,"view switcher hit");
            for(const auto& row:agenda.rows)Expect(row.row.x>=0&&row.row.Right()<=size.first+.5f&&row.row.Bottom()<=size.second+.5f,"view items stay inside the widget");
            if(mode==calendar::BoardView::Month&&size.first==820){
                const auto cell=static_cast<std::size_t>(std::find_if(model.Grid().begin(),model.Grid().end(),[](const auto& d){return d.date==calendar::Date{2026,9,25};})-model.Grid().begin());
                Expect(agenda.cellMaxScroll[cell]>0,"a busy day cell scrolls");
                auto moved=boardState;moved.cellScroll[cell]=1000;CalendarAgendaLayout end;CalendarBitmap out;
                Expect(renderer.Render(prefs,model,96,out,moved,&layout,&end),"render scrolled cell");
                Expect(std::any_of(end.rows.begin(),end.rows.end(),[&](const auto& row){return view.Snapshot().tasks[row.task].id=="3";}),"scrolling a cell reveals its last item");
                if(!output.empty())Save(out,output/"todo-month-cell-scrolled.bmp");
            }
            if(mode!=calendar::BoardView::Month){
                Expect(agenda.maxScroll>0&&agenda.scroll>0,"hour grid scrolls and starts near the first item");
                auto zoomed=boardState;zoomed.boardZoom=.01f;CalendarAgendaLayout whole;CalendarBitmap out;
                Expect(renderer.Render(prefs,model,96,out,zoomed,&layout,&whole)&&whole.maxScroll<1,"zooming out fits the whole day");
                if(!output.empty()&&size.first==820)Save(out,output/("todo-"+std::string(calendar::BoardViewName(mode))+"-zoomed-out.bmp"));
            }
            if(!output.empty())Save(bitmap,output/("todo-"+std::string(calendar::BoardViewName(mode))+"-"+std::to_string(size.first)+"x"+std::to_string(size.second)+".bmp"));
        }
        {   prefs.boardView=calendar::BoardView::All;prefs.panelColor={0x3d5a80,255};prefs.Active()->width=820;prefs.Active()->height=500;
            CalendarBitmap bitmap;Expect(renderer.Render(prefs,model,96,bitmap,boardState),"render panel colour");
            const auto p=bitmap.pixels[static_cast<std::size_t>(488)*bitmap.width+410];
            Expect(((p>>16)&255)==0x3d&&(p&255)==0x80,"solid material uses the chosen panel colour");
            prefs.panelColor={0x2c2c2e,255};}
        prefs.boardView=calendar::BoardView::All;prefs.material=calendar::Material::FrostedGlass;
        prefs.theme=calendar::Theme::Split;model.SetTheme(prefs.theme);
    }
    CalendarPickerRenderState picker;picker.selected={2026,9,25};picker.today=picker.selected;
    CalendarBitmap fallbackPicker,glassPicker;
    Expect(renderer.RenderPicker(prefs,picker,304,231,96,fallbackPicker),"render fallback month picker");
    picker.materialReady=true;
    Expect(renderer.RenderPicker(prefs,picker,304,231,96,glassPicker),"render native month picker foreground");
    const auto blank=static_cast<std::size_t>(55)*glassPicker.width+10;
    Expect((fallbackPicker.pixels[blank]>>24)>1&&(glassPicker.pixels[blank]>>24)==1,"ready month picker removes visible fallback tint but keeps blank pixels hit-testable");
    const auto border=static_cast<std::size_t>(100)*glassPicker.width;
    Expect((glassPicker.pixels[border]>>24)>0,"ready month picker preserves CSS border");
    if(json)json<<"]";
    std::cout<<"CALENDAR_TODO_RENDER_PASS\n";
}
