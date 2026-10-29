#include "platform/windows/todo/windows_local_time.h"
#include "core/todo/todo_time.h"
#include <algorithm>
#include <array>
#include <cwchar>
#include <cstring>
namespace lume::platform::windows {
namespace {
constexpr todo::Millis kDay=86400000,kEpochDays=719162;
int DaysBefore(int year){const int y=year-1;return 365*y+y/4-y/100+y/400;}
todo::Millis Civil(const todo::LocalDateTime& v){
    return (DaysBefore(v.date.year)+calendar::DayOfYear(v.date)-1-kEpochDays)*kDay+
        v.hour*3600000LL+v.minute*60000LL+v.second*1000LL+v.millisecond;
}
std::optional<todo::LocalDateTime> Decode(todo::Millis ms){
    constexpr auto minimum=-kEpochDays*kDay,maximum=(3652059-kEpochDays)*kDay-1;
    if(ms<minimum||ms>maximum)return {};
    auto days=ms/kDay,remainder=ms%kDay;if(remainder<0){--days;remainder+=kDay;}
    const int ordinal=static_cast<int>(days+kEpochDays);
    int low=1,high=10000;
    while(low+1<high){const int mid=(low+high)/2;if(DaysBefore(mid)<=ordinal)low=mid;else high=mid;}
    int day=ordinal-DaysBefore(low),month=1;
    while(day>=calendar::DaysInMonth(low,month)){day-=calendar::DaysInMonth(low,month);++month;}
    return todo::LocalDateTime{{low,month,day+1},static_cast<int>(remainder/3600000),
        static_cast<int>(remainder/60000%60),static_cast<int>(remainder/1000%60),static_cast<int>(remainder%1000)};
}
std::optional<todo::Millis> Transition(SYSTEMTIME rule,int year){
    if(!rule.wMonth||rule.wMonth>12)return {};
    if(rule.wYear&&rule.wYear!=year)return {};
    int day=rule.wDay;
    if(!rule.wYear){
        if(day<1||day>5||rule.wDayOfWeek>6)return {};
        day=1+(static_cast<int>(rule.wDayOfWeek)-calendar::Weekday({year,rule.wMonth,1})+7)%7+(day-1)*7;
        if(day>calendar::DaysInMonth(year,rule.wMonth))day-=7;
    }
    const todo::LocalDateTime v{{year,rule.wMonth,day},rule.wHour,rule.wMinute,rule.wSecond,rule.wMilliseconds};
    return todo::ValidLocal(v)?std::optional<todo::Millis>(Civil(v)):std::nullopt;
}
bool TransitionValid(const SYSTEMTIME& r){
    if(!r.wMonth)return true;
    if(r.wMonth>12||r.wHour>23||r.wMinute>59||r.wSecond>59||r.wMilliseconds>999)return false;
    if(!r.wYear)return r.wDay>=1&&r.wDay<=5&&r.wDayOfWeek<=6;
    return calendar::IsValidDate({r.wYear,r.wMonth,r.wDay});
}
bool RuleValid(const TIME_ZONE_INFORMATION& r){
    if(bool(r.StandardDate.wMonth)!=bool(r.DaylightDate.wMonth))return false;
    if(!TransitionValid(r.StandardDate)||!TransitionValid(r.DaylightDate))return false;
    const auto standard=static_cast<long long>(r.Bias)+(r.StandardDate.wMonth?r.StandardBias:0);
    const auto daylight=static_cast<long long>(r.Bias)+(r.DaylightDate.wMonth?r.DaylightBias:0);
    return standard>=-1440&&standard<=1440&&daylight>=-1440&&daylight<=1440;
}
long StandardBias(const TIME_ZONE_INFORMATION& r){return r.Bias+(r.StandardDate.wMonth?r.StandardBias:0);}
long DaylightBias(const TIME_ZONE_INFORMATION& r){return r.Bias+(r.DaylightDate.wMonth?r.DaylightBias:0);}
long BiasAt(todo::Millis ms,int year,const TIME_ZONE_INFORMATION& r){
    const long standard=StandardBias(r),daylight=DaylightBias(r);
    const auto summer=Transition(r.DaylightDate,year),winter=Transition(r.StandardDate,year);
    if(!summer||!winter)return standard;
    const auto begin=*summer+standard*60000LL,end=*winter+daylight*60000LL;
    const bool dst=begin<end?(ms>=begin&&ms<end):(ms>=begin||ms<end);
    return dst?daylight:standard;
}
}
WindowsLocalTime::WindowsLocalTime():WindowsLocalTime(ZoneReader{}){}
WindowsLocalTime::WindowsLocalTime(ZoneReader reader):reader_(std::move(reader)){Refresh();}
WindowsLocalTime::WindowsLocalTime(TIME_ZONE_INFORMATION rules):fixed_(rules),fixedRules_(true),ready_(RuleValid(rules)){}
WindowsLocalTime::WindowsLocalTime(DYNAMIC_TIME_ZONE_INFORMATION snapshot):zone_(snapshot),injected_(true),ready_(true){}
bool WindowsLocalTime::Refresh(){
    if(fixedRules_||injected_)return ready_;
    DYNAMIC_TIME_ZONE_INFORMATION next{};
    const bool available=(reader_?reader_(&next):GetDynamicTimeZoneInformation(&next))!=TIME_ZONE_ID_INVALID;
    const auto sameRule=[](const auto& a,const auto& b){
        return a.Bias==b.Bias&&a.StandardBias==b.StandardBias&&a.DaylightBias==b.DaylightBias&&
            std::memcmp(&a.StandardDate,&b.StandardDate,sizeof(SYSTEMTIME))==0&&
            std::memcmp(&a.DaylightDate,&b.DaylightDate,sizeof(SYSTEMTIME))==0;
    };
    bool changed=available!=ready_;
    if(available&&ready_){
        changed=!sameRule(next,zone_)||next.DynamicDaylightTimeDisabled!=zone_.DynamicDaylightTimeDisabled||
            std::wcsncmp(next.TimeZoneKeyName,zone_.TimeZoneKeyName,128)!=0;
        // Registry updates can alter historical rules without changing the zone key.
        if(!changed&&!next.DynamicDaylightTimeDisabled)for(const auto& item:rules_){
            TIME_ZONE_INFORMATION rule{};
            if(!GetTimeZoneInformationForYear(static_cast<USHORT>(item.first),&next,&rule)||!sameRule(item.second,rule)){changed=true;break;}
        }
    }
    if(changed)++revision_;
    zone_=next;ready_=available;rules_.clear();return ready_;
}
std::optional<TIME_ZONE_INFORMATION> WindowsLocalTime::Rules(int year)const{
    if(!ready_||year<1||year>9999)return {};
    if(fixedRules_)return fixed_;
    const auto found=rules_.find(year);if(found!=rules_.end())return found->second;
    TIME_ZONE_INFORMATION r{};auto dynamic=zone_;
    if(zone_.DynamicDaylightTimeDisabled){
        r.Bias=zone_.Bias;r.StandardBias=zone_.StandardBias;r.DaylightBias=zone_.DaylightBias;
        r.StandardDate=zone_.StandardDate;r.DaylightDate=zone_.DaylightDate;
    }
    else if(!GetTimeZoneInformationForYear(static_cast<USHORT>(year),&dynamic,&r))return {};
    if(!RuleValid(r))return {};
    // Bound long navigation sessions; each entry is only one year's Windows rules.
    if(rules_.size()>=8)rules_.clear();
    rules_.emplace(year,r);
    return r;
}
std::optional<todo::LocalDateTime> WindowsLocalTime::ToLocal(todo::Millis ms)const{
    constexpr auto minimum=-kEpochDays*kDay-kDay,maximum=(3652059-kEpochDays)*kDay+kDay-1;
    if(!ready_||ms<minimum||ms>maximum)return {};
    const auto utc=Decode(ms);
    const int approximateYear=utc?utc->date.year:(ms<0?1:9999);
    // A local year can differ by one at the UTC New Year boundary. Select rules
    // belonging to the candidate local year rather than to the UTC year.
    for(int year:std::array<int,3>{approximateYear,approximateYear-1,approximateYear+1}){
        const auto r=Rules(year);if(!r)continue;
        const auto local=Decode(ms-BiasAt(ms,year,*r)*60000LL);
        if(local&&local->date.year==year)return local;
    }
    return {};
}
std::optional<todo::Millis> WindowsLocalTime::FromLocal(const todo::LocalDateTime& v)const{
    if(!todo::ValidLocal(v))return {};
    const auto r=Rules(v.date.year);if(!r)return {};
    const auto civil=Civil(v);std::optional<todo::Millis> best;
    for(const long bias:{StandardBias(*r),DaylightBias(*r)}){
        const auto candidate=civil+bias*60000LL;const auto roundtrip=ToLocal(candidate);
        if(roundtrip&&*roundtrip==v&&(!best||candidate<*best))best=candidate;
    }
    return best;
}
todo::Millis WindowsLocalTime::NowMillis(){
    FILETIME ft{};GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER value{};
    value.LowPart=ft.dwLowDateTime;value.HighPart=ft.dwHighDateTime;
    return static_cast<todo::Millis>(value.QuadPart/10000)-11644473600000LL;
}
}
