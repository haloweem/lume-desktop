#include "platform/windows/todo/windows_local_time.h"
#include "core/todo/todo_time.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace lume;
using platform::windows::WindowsLocalTime;
namespace {
void Check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);}
TIME_ZONE_INFORMATION Eastern(){
    TIME_ZONE_INFORMATION t{};t.Bias=300;t.DaylightBias=-60;
    t.DaylightDate.wMonth=3;t.DaylightDate.wDay=2;t.DaylightDate.wDayOfWeek=0;t.DaylightDate.wHour=2;
    t.StandardDate.wMonth=11;t.StandardDate.wDay=1;t.StandardDate.wDayOfWeek=0;t.StandardDate.wHour=2;return t;
}
void Roundtrip(WindowsLocalTime& p,todo::LocalDateTime d){const auto v=p.FromLocal(d);Check(v.has_value(),"supported civil date converts");Check(p.ToLocal(*v)==std::optional<todo::LocalDateTime>(d),"civil date round trips exactly");}
}
int main(){try{
    TIME_ZONE_INFORMATION china{};china.Bias=-480;WindowsLocalTime east(china);
    Check(east.FromLocal({{1970,1,1},8,0,0,0})==0,"UTC epoch converts at UTC+8");
    Check(east.ToLocal(-1)==std::optional<todo::LocalDateTime>({{1970,1,1},7,59,59,999}),"negative millisecond floor");
    for(int year:{1,4,99,100,400,1600,1601,1900,1969,2000,2026,9999}){
        Roundtrip(east,{{year,1,1},0,0,0,0});Roundtrip(east,{{year,12,31},23,59,59,999});
    }
    Check(!east.FromLocal({{2026,2,29},12,0,0,0}),"invalid date rejected");
    Check(!east.FromLocal({{2026,1,1},24,0,0,0}),"invalid hour rejected");
    Check(!east.ToLocal(std::numeric_limits<todo::Millis>::min())&&!east.ToLocal(std::numeric_limits<todo::Millis>::max()),"timestamp extremes rejected");
    auto noDst=china;noDst.StandardBias=60;noDst.DaylightBias=-120;
    WindowsLocalTime noTransitions(noDst);
    Check(noTransitions.FromLocal({{1970,1,1},8,0,0,0})==0,"bias additions ignored without transition dates");
    noDst.StandardBias=std::numeric_limits<LONG>::max();noDst.DaylightBias=std::numeric_limits<LONG>::min();
    WindowsLocalTime unusedBiases(noDst);
    Check(unusedBiases.FromLocal({{1970,1,1},8,0,0,0})==0,"unused bias extremes ignored");
    const auto eastern=Eastern();DYNAMIC_TIME_ZONE_INFORMATION dynamic{};
    dynamic.Bias=eastern.Bias;dynamic.StandardBias=eastern.StandardBias;dynamic.DaylightBias=eastern.DaylightBias;
    dynamic.StandardDate=eastern.StandardDate;dynamic.DaylightDate=eastern.DaylightDate;dynamic.DynamicDaylightTimeDisabled=TRUE;
    WindowsLocalTime fixedDynamic(dynamic);
    Check(fixedDynamic.FromLocal({{2026,7,1},9,0,0,0})==WindowsLocalTime(eastern).FromLocal({{2026,7,1},9,0,0,0}),"disabled dynamic history retains fixed summer rules");
    Check(!fixedDynamic.FromLocal({{2026,3,8},2,30,0,0}),"fixed dynamic rules retain spring gap");
    Check(fixedDynamic.FromLocal({{2026,11,1},1,30,0,0})==WindowsLocalTime(eastern).FromLocal({{2026,11,1},1,30,0,0}),"fixed dynamic rules retain earlier fold");
    dynamic.StandardDate={};dynamic.DaylightDate={};dynamic.Bias=-480;dynamic.StandardBias=60;
    WindowsLocalTime disabled(dynamic);
    Check(disabled.FromLocal({{1970,1,1},8,0,0,0})==0,"cleared dynamic dates disable DST and ignore standard addition");
    WindowsLocalTime ny(Eastern());
    for(int invalid=0;invalid<4;++invalid){
        auto rules=Eastern();
        if(invalid==0)rules.DaylightDate.wMonth=13;
        if(invalid==1)rules.DaylightDate.wDay=0;
        if(invalid==2)rules.DaylightDate.wHour=24;
        if(invalid==3)rules.DaylightDate.wYear=10000;
        WindowsLocalTime bad(rules);
        Check(!bad.FromLocal({{2026,7,1},9,0,0,0}),"malformed transition fails closed");
    }
    Check(!ny.FromLocal({{2026,3,8},2,30,0,0}),"spring DST gap rejected");
    const auto before=ny.FromLocal({{2026,3,8},1,59,59,999}),after=ny.FromLocal({{2026,3,8},3,0,0,0});
    Check(before&&after&&*after-*before==1,"spring transition exact millisecond");
    const auto fold=ny.FromLocal({{2026,11,1},1,30,0,0});Check(fold.has_value(),"fall fold valid");
    TIME_ZONE_INFORMATION utc{};WindowsLocalTime z(utc);
    Check(z.ToLocal(*fold)==std::optional<todo::LocalDateTime>({{2026,11,1},5,30,0,0}),"fold selects earlier daylight instant");
    Check(ny.ToLocal(*fold+3600000)==ny.ToLocal(*fold),"both fall fold instants map same local time");
    const auto daily=ny.FromLocal({{2026,3,7},9,0,0,0});Check(daily.has_value(),"daily start");
    const auto next=todo::NextOccurrence(*daily,{todo::Frequency::Daily,1},ny);
    Check(next&&ny.ToLocal(*next)==std::optional<todo::LocalDateTime>({{2026,3,8},10,0,0,0}),"daily remains fixed 24 hours across DST");
    const auto friday=ny.FromLocal({{2026,3,6},9,0,0,0});const auto monday=todo::NextOccurrence(*friday,{todo::Frequency::Weekdays,1},ny);
    Check(monday&&ny.ToLocal(*monday)==std::optional<todo::LocalDateTime>({{2026,3,9},9,0,0,0})&&*monday-*friday==71LL*3600000,"weekdays preserve local time across DST");
    auto south=Eastern();south.Bias=-600;south.DaylightDate.wMonth=10;south.DaylightDate.wDay=1;south.StandardDate.wMonth=4;south.StandardDate.wDay=1;
    WindowsLocalTime au(south);Roundtrip(au,{{2026,1,1},9,0,0,0});Roundtrip(au,{{2026,7,1},9,0,0,0});
    Check(z.ToLocal(*au.FromLocal({{2026,1,1},11,0,0,0}))==std::optional<todo::LocalDateTime>({{2026,1,1},0,0,0,0}),"southern summer offset");
    WindowsLocalTime system;Check(system.Refresh(),"read current system zone");const auto now=WindowsLocalTime::NowMillis();
    Check(now>0&&system.ToLocal(now).has_value(),"system clock now converts");
    const auto old=system.FromLocal({{99,1,1},9,0,0,0});Check(old&&system.ToLocal(*old)==std::optional<todo::LocalDateTime>({{99,1,1},9,0,0,0}),"system zone supports year 99");
    std::cout<<"WINDOWS_LOCAL_TIME_PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
