#include "apps/shell/wallpaper_resource_history.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lume::shell {
namespace {
using Json=nlohmann::json;
namespace fs=std::filesystem;
constexpr std::uint64_t FileBytes=2ull<<20,TotalBytes=4*FileBytes,LineBytes=64ull<<10;
constexpr std::uint64_t WindowMs=72ull*60*60*1000,FutureToleranceMs=5*60*1000;
constexpr std::size_t MaxRecords=12000,MaxProcesses=64,MaxAnomalySamples=120;
constexpr std::uint64_t GiB=1ull<<30;
const std::array<const wchar_t*,4> Names{{L"wallpaper-resources.jsonl",L"wallpaper-resources.1.jsonl",
    L"wallpaper-resources.2.jsonl",L"wallpaper-resources.3.jsonl"}};
const std::array<const char*,7> Metrics{{"cpuMachinePercent","privateBytes","workingSetBytes",
    "gpuDedicatedBytes","gpuSharedBytes","gpuEnginePercent","cpuIntervalMs"}};
std::mutex& HistoryMutex(){static std::mutex value;return value;}
struct StorageState {std::uint64_t writeFailures=0,lastFailureAt=0,lastSuccessAt=0;std::string error;};
std::map<std::wstring,StorageState>& Storage(){static std::map<std::wstring,StorageState> value;return value;}
std::uint64_t Now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count());}
std::wstring Key(const fs::path& directory){std::error_code error;const auto path=fs::absolute(directory,error);
    return (error?directory:path).lexically_normal().wstring();}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE handle=INVALID_HANDLE_VALUE):value(handle){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
std::wstring FinalPath(HANDLE handle){
    std::wstring value(32768,L'\0');
    const auto size=GetFinalPathNameByHandleW(handle,value.data(),static_cast<DWORD>(value.size()),FILE_NAME_NORMALIZED);
    if(!size||size>=value.size())return {};
    value.resize(size);return value;
}
bool SamePath(const std::wstring& left,const std::wstring& right){return !left.empty()&&!right.empty()&&
    CompareStringOrdinal(left.c_str(),static_cast<int>(left.size()),right.c_str(),static_cast<int>(right.size()),TRUE)==CSTR_EQUAL;}
bool OpenDirectory(const fs::path& directory,bool create,std::wstring& final,std::string& error){
    if(create){std::error_code failure;fs::create_directories(directory,failure);if(failure){error="directory_create_failed";return false;}}
    Handle root(CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(root.value==INVALID_HANDLE_VALUE){error=GetLastError()==ERROR_PATH_NOT_FOUND||GetLastError()==ERROR_FILE_NOT_FOUND?
        "directory_missing":"directory_open_failed";return false;}
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(root.value,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||
        (info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){error="directory_unsafe";return false;}
    final=FinalPath(root.value);if(final.empty()){error="directory_identity_failed";return false;}return true;
}
bool SafeFile(HANDLE handle,const std::wstring& directory){
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(handle,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
        info.nNumberOfLinks!=1)return false;
    const auto path=FinalPath(handle);return !path.empty()&&SamePath(fs::path(path).parent_path().wstring(),directory);
}
bool SafeExisting(const fs::path& file,const std::wstring& directory){
    Handle handle(CreateFileW(file.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(handle.value==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND;
    return SafeFile(handle.value,directory);
}
void Failed(const std::wstring& key,const char* error){
    auto& entries=Storage();if(entries.size()>=32&&!entries.count(key))entries.erase(entries.begin());
    auto& state=entries[key];++state.writeFailures;state.lastFailureAt=Now();state.error=error;
}
bool Unsigned(const Json& value,std::uint64_t& number){
    if(value.is_number_unsigned()){number=value.get<std::uint64_t>();return true;}
    if(value.is_number_integer()){const auto signedValue=value.get<std::int64_t>();if(signedValue>=0){number=static_cast<std::uint64_t>(signedValue);return true;}}
    return false;
}
std::optional<std::uint64_t> Number(const Json& object,const char* field){
    const auto found=object.find(field);std::uint64_t value=0;
    return found!=object.end()&&Unsigned(*found,value)?std::optional<std::uint64_t>(value):std::nullopt;
}
Json Metric(const Json& object,const char* field,bool percent=false){
    const auto found=object.find(field);if(found==object.end()||!found->is_number())return nullptr;
    if(percent){const auto value=found->get<double>();return std::isfinite(value)&&value>=0&&value<=100?Json(value):Json(nullptr);}
    std::uint64_t value=0;return Unsigned(*found,value)?Json(value):Json(nullptr);
}
std::string String(const Json& object,const char* field){const auto found=object.find(field);
    return found!=object.end()&&found->is_string()?found->get<std::string>():std::string{};}
bool Digits(const std::string& value,std::size_t limit,bool empty=false){return (empty||!value.empty())&&value.size()<=limit&&
    std::all_of(value.begin(),value.end(),[](unsigned char c){return c>='0'&&c<='9';});}
std::string Code(const Json& object,const char* field,const char* fallback=""){
    const auto value=String(object,field);if(value.empty())return fallback;
    if(value.size()>96||!std::all_of(value.begin(),value.end(),[](unsigned char c){return
        (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.';}))return "redacted";
    auto lower=value;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(c>='A'&&c<='Z'?c+32:c);});
    for(const auto* word:{"token","password","cookie","secret","bearer","authorization","credential"})
        if(lower.find(word)!=std::string::npos)return "redacted";
    return value;
}
bool Boolean(const Json& object,const char* field){const auto found=object.find(field);return found!=object.end()&&found->is_boolean()&&found->get<bool>();}
std::optional<Json> Redact(const Json& input){
    if(!input.is_object()||Number(input,"schemaVersion")!=std::optional<std::uint64_t>(1))return std::nullopt;
    const auto type=String(input,"type");const auto at=Number(input,"atUnixMs"),uptime=Number(input,"uptimeMs");
    const auto players=input.find("processes");
    if((type!="sample"&&type!="event")||!at||!*at||!uptime||players==input.end()||!players->is_array()||players->size()>MaxProcesses)return std::nullopt;
    auto run=Code(input,"runId","unknown");
    const auto processors=Number(input,"logicalProcessors").value_or(0);
    Json output={{"schemaVersion",1},{"type",type},{"atUnixMs",*at},{"uptimeMs",*uptime},{"runId",run},
        {"logicalProcessors",processors<=65536?processors:0},{"gpuStatus",Code(input,"gpuStatus","unavailable")},
        {"wallpaperGpuEnginePercent",type=="sample"?Metric(input,"wallpaperGpuEnginePercent",true):Json(nullptr)},
        {"processes",Json::array()}};
    for(const auto& player:*players){
        if(!player.is_object())continue;
        const auto id=String(player,"displayId");const auto pid=Number(player,"pid");
        if(!Digits(id,32)||!pid||*pid>MAXDWORD)continue;
        auto wallpaper=String(player,"wallpaperId");if(!Digits(wallpaper,20,true))wallpaper.clear();
        auto created=String(player,"createdFileTime");if(!Digits(created,20,true))created.clear();
        auto state=String(player,"state");
        if(state!="playing"&&state!="paused"&&state!="parked"&&state!="stopped"&&state!="failed"&&
            state!="starting"&&state!="static"&&state!="unavailable")state="unavailable";
        Json item={{"displayId",id},{"wallpaperId",wallpaper},{"pid",*pid},{"createdFileTime",created},{"state",state},
            {"manualPaused",Boolean(player,"manualPaused")},{"systemPaused",Boolean(player,"systemPaused")},
            {"parked",Boolean(player,"parked")},{"error",Code(player,"error")}};
        if(type=="sample"){
            for(const auto* metric:Metrics)item[metric]=Metric(player,metric,
                std::string(metric)=="cpuMachinePercent"||std::string(metric)=="gpuEnginePercent");
            item["gpuStatus"]=Code(player,"gpuStatus","unavailable");
        }
        output["processes"].push_back(std::move(item));
    }
    return output;
}
std::string Utc(std::uint64_t millis){
    const auto time=static_cast<std::time_t>(millis/1000);std::tm value{};
    if(gmtime_s(&value,&time))return "unknown";
    char text[48]{};std::snprintf(text,sizeof(text),"%04d-%02d-%02d %02d:%02d:%02d.%03llu UTC",value.tm_year+1900,
        value.tm_mon+1,value.tm_mday,value.tm_hour,value.tm_min,value.tm_sec,static_cast<unsigned long long>(millis%1000));return text;
}
std::string Cell(const std::string& value,bool identity=false){
    std::string safe=value;
    if(!safe.empty()&&((identity&&safe.size()>15)||safe.front()=='='||safe.front()=='+'||safe.front()=='-'||safe.front()=='@'))safe.insert(safe.begin(),'\'');
    std::string result="\"";for(const auto c:safe){result+=c;if(c=='\"')result+='\"';}return result+'\"';
}
std::string Value(const Json& object,const char* field){const auto found=object.find(field);return found==object.end()||found->is_null()?std::string{}:found->dump();}
struct Peak {
    std::size_t count=0;double sum=0,maximum=0;Json peak=nullptr;
    void Add(const Json& object,const char* field){const auto found=object.find(field);if(found==object.end()||!found->is_number())return;
        const auto value=found->get<double>();if(!count||value>maximum){maximum=value;peak=*found;}++count;sum+=value;}
    Json Max(bool =false)const{return count?peak:Json(nullptr);}
};
struct Stats {
    std::uint64_t first=0,last=0;std::size_t count=0;
    std::array<Peak,6> metrics{};
    void Add(const Json& player,std::uint64_t at){if(!first)first=at;last=at;++count;
        for(std::size_t i=0;i<metrics.size();++i)metrics[i].Add(player,Metrics[i]);}
    Json Export()const{return {{"sampleCount",count},{"firstAtUnixMs",first?Json(first):Json(nullptr)},
        {"lastAtUnixMs",last?Json(last):Json(nullptr)},{"cpuMeanPercent",metrics[0].count?Json(metrics[0].sum/static_cast<double>(metrics[0].count)):Json(nullptr)},
        {"cpuPeakPercent",metrics[0].Max()},{"privatePeakBytes",metrics[1].Max(true)},
        {"workingSetPeakBytes",metrics[2].Max(true)},{"gpuDedicatedPeakBytes",metrics[3].Max(true)},
        {"gpuSharedPeakBytes",metrics[4].Max(true)},{"gpuPeakPercent",metrics[5].Max()}};}
};
struct Discarded {std::size_t invalid=0,oversized=0,expired=0,future=0,limited=0,incomplete=0;};
struct Raw {std::string bytes;bool tail=false;};
struct ReadData {std::vector<Raw> files;StorageState storage;std::size_t readFailures=0;bool configured=false,rootMissing=false;std::string readError;Discarded discarded;};
ReadData Load(const fs::path& directory){
    ReadData result;result.configured=!directory.empty();if(!result.configured)return result;
    std::lock_guard<std::mutex> lock(HistoryMutex());
    const auto state=Storage().find(Key(directory));if(state!=Storage().end())result.storage=state->second;
    std::wstring root;
    if(!OpenDirectory(directory,false,root,result.readError)){
        result.rootMissing=result.readError=="directory_missing";if(!result.rootMissing)++result.readFailures;return result;
    }
    std::uint64_t budget=TotalBytes;
    // Newest first for the I/O budget; parsing below reverses these chunks.
    for(const auto* name:Names){
        Handle file(CreateFileW((directory/name).c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr));
        if(file.value==INVALID_HANDLE_VALUE){if(GetLastError()!=ERROR_FILE_NOT_FOUND){++result.readFailures;result.readError="file_read_open_failed";}continue;}
        if(!SafeFile(file.value,root)){++result.readFailures;result.readError="file_unsafe";continue;}
        LARGE_INTEGER size{};if(!GetFileSizeEx(file.value,&size)||size.QuadPart<0){++result.readFailures;result.readError="file_size_failed";continue;}
        const auto total=static_cast<std::uint64_t>(size.QuadPart),wanted=std::min({total,FileBytes,budget});
        if(!wanted)continue;
        Raw raw;raw.tail=total>wanted;if(raw.tail)++result.discarded.limited;
        LARGE_INTEGER offset{};offset.QuadPart=static_cast<LONGLONG>(total-wanted);
        if(!SetFilePointerEx(file.value,offset,nullptr,FILE_BEGIN)){++result.readFailures;result.readError="file_seek_failed";continue;}
        raw.bytes.resize(static_cast<std::size_t>(wanted));DWORD read=0;
        if(!ReadFile(file.value,raw.bytes.data(),static_cast<DWORD>(wanted),&read,nullptr)){
            ++result.readFailures;result.readError="file_read_failed";continue;}
        raw.bytes.resize(read);budget-=read;result.files.push_back(std::move(raw));
    }
    std::reverse(result.files.begin(),result.files.end());return result;
}
std::vector<Json> Parse(ReadData& data){
    std::deque<Json> records;std::deque<std::size_t> recordBytes;std::size_t retainedBytes=0;
    const auto now=Now(),oldest=now>WindowMs?now-WindowMs:0;
    for(const auto& file:data.files){
        std::size_t begin=0;if(file.tail){const auto first=file.bytes.find('\n');if(first==std::string::npos)continue;begin=first+1;}
        while(begin<file.bytes.size()){
            const auto end=file.bytes.find('\n',begin);const bool complete=end!=std::string::npos;
            auto length=(complete?end:file.bytes.size())-begin;
            if(length&&file.bytes[begin+length-1]=='\r')--length;
            if(length>LineBytes){++data.discarded.oversized;}
            else if(length){
                std::optional<Json> value;
                try{const auto parsed=Json::parse(file.bytes.begin()+static_cast<std::ptrdiff_t>(begin),
                    file.bytes.begin()+static_cast<std::ptrdiff_t>(begin+length),[](int depth,Json::parse_event_t,Json&){
                        if(depth>12)throw std::runtime_error("depth");
                        return true;},false);
                    value=Redact(parsed);
                }catch(...){}
                if(!value){if(complete)++data.discarded.invalid;else ++data.discarded.incomplete;}
                else{const auto at=(*value)["atUnixMs"].get<std::uint64_t>();
                    if(at<oldest)++data.discarded.expired;
                    else if(at>now+FutureToleranceMs)++data.discarded.future;
                    else{const auto bytes=value->dump().size()+1;retainedBytes+=bytes;recordBytes.push_back(bytes);records.push_back(std::move(*value));
                        while(records.size()>MaxRecords||retainedBytes>TotalBytes){retainedBytes-=recordBytes.front();recordBytes.pop_front();records.pop_front();++data.discarded.limited;}}
                }
            }
            if(!complete)break;
            begin=end+1;
        }
    }
    std::vector<Json> result;result.reserve(records.size());for(auto& record:records)result.push_back(std::move(record));
    std::stable_sort(result.begin(),result.end(),[](const Json& left,const Json& right){return left["atUnixMs"].get<std::uint64_t>()<right["atUnixMs"].get<std::uint64_t>();});
    return result;
}
std::string Identity(const Json& record,const Json& process){return String(record,"runId")+'|'+Value(process,"pid")+'|'+String(process,"createdFileTime");}
struct ProcessStats {Json identity;Stats stats;std::set<std::string> displays,wallpapers;};
struct DisplayState {std::string lastPlayer,lastState;};
struct Streak {unsigned count=0;std::uint64_t last=0;};
Json Analyze(const std::vector<Json>& records,const ReadData& data,std::string& csv){
    Json state={{"schemaVersion",1},{"status","empty"},{"recordCount",records.size()},{"sampleCount",0},{"eventCount",0},
        {"processSampleCount",0},{"firstAtUnixMs",nullptr},{"lastAtUnixMs",nullptr},{"coveredMilliseconds",0},
        {"sampleIntervalMilliseconds",60000},{"missing",Json::object()},{"processes",Json::array()},{"displays",Json::array()},
        {"pidReplacements",0},{"stateChanges",0},
        {"anomalies",{{"firstAtUnixMs",nullptr},{"lastAtUnixMs",nullptr},{"sampleCount",0},{"eventCount",0},
            {"sustainedCount",0},{"omittedSamples",0},{"samples",Json::array()}}},
        {"storage",{{"configured",data.configured},{"readFailures",data.readFailures},{"writeFailures",data.storage.writeFailures},
            {"lastWriteError",data.storage.error},{"lastReadError",data.readError},
            {"lastWriteFailureAtUnixMs",data.storage.lastFailureAt?Json(data.storage.lastFailureAt):Json(nullptr)},
            {"lastWriteSuccessAtUnixMs",data.storage.lastSuccessAt?Json(data.storage.lastSuccessAt):Json(nullptr)}}},
        {"discarded",{{"invalid",data.discarded.invalid},{"oversized",data.discarded.oversized},{"expired",data.discarded.expired},
            {"future",data.discarded.future},{"limited",data.discarded.limited},{"incomplete",data.discarded.incomplete}}}};
    for(const auto* name:Metrics)state["missing"][name]=0;
    state["missing"]["samplingGapCount"]=0;state["missing"]["estimatedMissingSamples"]=0;
    std::map<std::string,ProcessStats> processes;std::map<std::string,Stats> displays;
    std::map<std::string,DisplayState> displayStates;std::map<std::string,std::array<Streak,4>> streaks;
    std::uint64_t previousSample=0;std::string previousRun;
    std::size_t samples=0,events=0,processSamples=0,replacements=0,changes=0,anomalySamples=0,anomalyEvents=0,sustained=0;
    csv="\xEF\xBB\xBFutc,atUnixMs,uptimeMs,runId,logicalProcessors,wallpaperGpuEnginePercent,wallpaperGpuStatus,displayId,wallpaperId,pid,createdFileTime,state,manualPaused,systemPaused,parked,error,cpuMachinePercent,cpuIntervalMs,privateBytes,workingSetBytes,gpuDedicatedBytes,gpuSharedBytes,gpuEnginePercent,gpuStatus\r\n";
    auto& anomalies=state["anomalies"];
    for(const auto& record:records){
        const auto at=record["atUnixMs"].get<std::uint64_t>();const bool sample=record["type"]=="sample";
        if(sample){++samples;const auto run=String(record,"runId");
            if(previousSample&&run==previousRun&&at>previousSample+90000){
                state["missing"]["samplingGapCount"]=state["missing"]["samplingGapCount"].get<std::size_t>()+1;
                state["missing"]["estimatedMissingSamples"]=state["missing"]["estimatedMissingSamples"].get<std::uint64_t>()+(at-previousSample)/60000-1;
            }previousSample=at;previousRun=run;
        }else ++events;
        for(const auto& process:record["processes"]){
            const auto display=String(process,"displayId"),identity=Identity(record,process);
            auto& last=displayStates[display];
            if(process["pid"]!=0){if(!last.lastPlayer.empty()&&last.lastPlayer!=identity)++replacements;last.lastPlayer=identity;}
            const auto transition=String(process,"state")+'|'+Value(process,"manualPaused")+'|'+Value(process,"systemPaused")+'|'+Value(process,"parked")+'|'+String(process,"error");
            if(!last.lastState.empty()&&last.lastState!=transition)++changes;
            last.lastState=transition;
            Json kinds=Json::array(),sustainedKinds=Json::array();
            if(sample){
                ++processSamples;for(const auto* name:Metrics)if(process[name].is_null())state["missing"][name]=state["missing"][name].get<std::size_t>()+1;
                auto& aggregate=processes[identity];aggregate.identity={{"runId",record["runId"]},{"pid",process["pid"]},{"createdFileTime",process["createdFileTime"]}};
                aggregate.displays.insert(display);aggregate.wallpapers.insert(String(process,"wallpaperId"));aggregate.stats.Add(process,at);displays[display].Add(process,at);
                std::vector<std::string> cells{Cell(Utc(at)),Value(record,"atUnixMs"),Value(record,"uptimeMs"),Cell(String(record,"runId"),true),
                    Value(record,"logicalProcessors"),Value(record,"wallpaperGpuEnginePercent"),Cell(String(record,"gpuStatus")),
                    Cell(display,true),Cell(String(process,"wallpaperId"),true),Value(process,"pid"),Cell(String(process,"createdFileTime"),true),Cell(String(process,"state")),
                    Value(process,"manualPaused"),Value(process,"systemPaused"),Value(process,"parked"),Cell(String(process,"error"))};
                for(const auto* name:{"cpuMachinePercent","cpuIntervalMs","privateBytes","workingSetBytes","gpuDedicatedBytes","gpuSharedBytes","gpuEnginePercent"})cells.push_back(Value(process,name));
                cells.push_back(Cell(String(process,"gpuStatus")));for(std::size_t i=0;i<cells.size();++i){if(i)csv+=',';csv+=cells[i];}csv+="\r\n";
                const auto high=[&](const char* field,double threshold){return process[field].is_number()&&process[field].get<double>()>=threshold;};
                const std::array<bool,4> flags{{high("cpuMachinePercent",20),high("privateBytes",static_cast<double>(GiB)),
                    high("gpuEnginePercent",80),high("gpuDedicatedBytes",static_cast<double>(GiB))||high("gpuSharedBytes",static_cast<double>(GiB))}};
                constexpr std::array<const char*,4> names{{"cpu_high","private_high","gpu_high","gpu_memory_high"}};
                auto& series=streaks[identity];for(std::size_t i=0;i<flags.size();++i){auto& streak=series[i];
                    if(!flags[i]){streak={};continue;}
                    if(!streak.last||at>streak.last+90000)streak.count=0;
                    const bool next=at>streak.last;if(next){++streak.count;streak.last=at;}kinds.push_back(names[i]);
                    if(streak.count>=3)sustainedKinds.push_back(names[i]);
                    if(next&&streak.count==3)++sustained;
                }
            }
            if(!String(process,"error").empty()||process["state"]=="failed")kinds.push_back("runtime_error");
            if(!kinds.empty()){
                if(sample)++anomalySamples;else ++anomalyEvents;
                if(anomalies["firstAtUnixMs"].is_null())anomalies["firstAtUnixMs"]=at;
                anomalies["lastAtUnixMs"]=at;
                Json issue={{"atUnixMs",at},{"type",record["type"]},{"runId",record["runId"]},{"displayId",display},{"wallpaperId",process["wallpaperId"]},
                    {"pid",process["pid"]},{"createdFileTime",process["createdFileTime"]},{"state",process["state"]},
                    {"error",process["error"]},{"kinds",std::move(kinds)},{"sustainedKinds",std::move(sustainedKinds)}};
                if(sample)for(const auto* name:Metrics)issue[name]=process[name];
                anomalies["samples"].push_back(std::move(issue));if(anomalies["samples"].size()>MaxAnomalySamples){
                    anomalies["samples"].erase(anomalies["samples"].begin());anomalies["omittedSamples"]=anomalies["omittedSamples"].get<std::size_t>()+1;}
            }
        }
    }
    for(const auto& entry:processes){auto item=entry.second.stats.Export();item.update(entry.second.identity);
        item["displayIds"]=entry.second.displays;item["wallpaperIds"]=entry.second.wallpapers;state["processes"].push_back(std::move(item));}
    for(const auto& entry:displays){auto item=entry.second.Export();item["displayId"]=entry.first;state["displays"].push_back(std::move(item));}
    state["sampleCount"]=samples;state["eventCount"]=events;state["processSampleCount"]=processSamples;
    state["pidReplacements"]=replacements;state["stateChanges"]=changes;
    anomalies["sampleCount"]=anomalySamples;anomalies["eventCount"]=anomalyEvents;anomalies["sustainedCount"]=sustained;
    if(!records.empty()){const auto first=records.front()["atUnixMs"].get<std::uint64_t>(),last=records.back()["atUnixMs"].get<std::uint64_t>();
        state["firstAtUnixMs"]=first;state["lastAtUnixMs"]=last;state["coveredMilliseconds"]=last-first;}
    const bool partial=data.readFailures||data.storage.writeFailures||data.discarded.invalid||data.discarded.oversized||data.discarded.incomplete||data.discarded.limited;
    state["status"]=!data.configured?"disabled":records.empty()?(data.readFailures?"unavailable":data.storage.writeFailures?"partial":"empty"):partial?"partial":"ready";
    return state;
}
std::string Human(const Json& state){
    std::ostringstream text;text<<"Lume 动态壁纸资源历史\r\n";
    const auto status=state.value("status",std::string{});
    if(status=="disabled")text<<"未配置历史目录。从支持此功能的新版运行后开始记录；旧版运行时段没有历史。\r\n";
    else if(status=="empty")text<<"最近72小时暂无有效历史。从支持此功能的新版运行后开始记录；没有记录不表示资源占用为零。\r\n";
    else if(status=="unavailable")text<<"历史暂不可读取，请查看下方存储状态。\r\n";
    else text<<(status=="partial"?"历史覆盖不完整。\r\n":"历史记录可用。\r\n");
    text<<"保留最近72小时；正常采样间隔60秒，最多4个文件、合计8 MiB。事件在检测时记录。\r\n";
    if(!state["firstAtUnixMs"].is_null())text<<"范围："<<Utc(state["firstAtUnixMs"].get<std::uint64_t>())<<" 至 "<<Utc(state["lastAtUnixMs"].get<std::uint64_t>())
        <<"；跨度 "<<state["coveredMilliseconds"].get<std::uint64_t>()/1000<<" 秒（跨度不保证连续采样）。\r\n";
    text<<"样本 "<<state["sampleCount"]<<" 条；进程样本 "<<state["processSampleCount"]<<" 条；事件 "<<state["eventCount"]
        <<" 条；PID替换 "<<state["pidReplacements"]<<" 次；状态变更 "<<state["stateChanges"]<<" 次。\r\n";
    text<<"采样间断 "<<state["missing"]["samplingGapCount"]<<" 处；估计缺少 "<<state["missing"]["estimatedMissingSamples"]<<" 次采样。\r\n";
    const auto& anomaly=state["anomalies"];
    text<<"异常进程样本 "<<anomaly["sampleCount"]<<" 条；异常事件 "<<anomaly["eventCount"]<<" 条；连续3次达到阈值 "<<anomaly["sustainedCount"]<<" 组。\r\n";
    if(!anomaly["firstAtUnixMs"].is_null())text<<"首次观察异常："<<Utc(anomaly["firstAtUnixMs"].get<std::uint64_t>())<<"；最近异常："<<Utc(anomaly["lastAtUnixMs"].get<std::uint64_t>())<<"。\r\n";
    text<<"异常阈值：全机CPU占比≥20%、Private≥1 GiB、GPU引擎≥80%，或专用/共享显存计数任一≥1 GiB。异常标记用于定位，不等于泄漏结论。\r\n";
    text<<"CPU按全机逻辑处理器归一化；均值为有效样本的算术平均。Private是提交量，Working Set是驻留内存。\r\n";
    text<<"缺测以null保存，CSV为空单元格，不能当作0；60秒采样可能遗漏短时峰值，异常时间是首次观察时间。\r\n";
    text<<"GPU/显存来自PDH辅助指标；共享资源可能重复归属，计数可能与任务管理器不同，不能单独认定显存泄漏。\r\n";
    text<<"CSV每行对应一个采样进程；事件见JSONL；长标识列前置单引号供Excel按文本读取。\r\n";
    const auto peak=[](const Json& item,const char* key,bool bytes=false){const auto& value=item[key];if(value.is_null())return std::string("缺测");
        std::ostringstream number;number<<std::fixed<<std::setprecision(bytes?1:2)<<value.get<double>()/(bytes?1048576.0:1.0);return number.str()+(bytes?" MiB":"%");};
    for(const auto* group:{"processes","displays"})for(const auto& item:state[group]){
        text<<(std::string(group)=="processes"?"进程 PID "+Value(item,"pid")+" 创建 "+String(item,"createdFileTime"):"显示器 "+String(item,"displayId"))
            <<"：CPU均值 "<<peak(item,"cpuMeanPercent")<<" / 峰值 "<<peak(item,"cpuPeakPercent")<<"；Private峰值 "<<peak(item,"privatePeakBytes",true)
            <<"；WS峰值 "<<peak(item,"workingSetPeakBytes",true)<<"；GPU峰值 "<<peak(item,"gpuPeakPercent")
            <<"；专用/共享显存峰值 "<<peak(item,"gpuDedicatedPeakBytes",true)<<" / "<<peak(item,"gpuSharedPeakBytes",true)<<"。\r\n";
    }
    text<<"缺测计数："<<state["missing"].dump()<<"\r\n存储：读取失败 "<<state["storage"]["readFailures"]<<" 次，写入失败 "<<state["storage"]["writeFailures"]
        <<" 次；最近写入错误 "<<state["storage"]["lastWriteError"]<<"；最近读取错误 "<<state["storage"]["lastReadError"]<<"。\r\n";
    text<<"过滤/限额："<<state["discarded"].dump()<<"\r\n";return text.str();
}
} // namespace

WallpaperResourceHistory::WallpaperResourceHistory(fs::path directory):directory_(std::move(directory)){}
bool WallpaperResourceHistory::Append(const Json& record) noexcept {
    if(directory_.empty())return false;
    try{
        const auto filtered=Redact(record);if(!filtered)return false;
        auto line=filtered->dump(-1,' ',false,Json::error_handler_t::replace)+'\n';if(line.size()>LineBytes)return false;
        std::lock_guard<std::mutex> lock(HistoryMutex());const auto key=Key(directory_);std::wstring root;std::string error;
        if(!OpenDirectory(directory_,true,root,error)){Failed(key,error.c_str());return false;}
        for(const auto* name:Names)if(!SafeExisting(directory_/name,root)){Failed(key,"file_unsafe");return false;}
        WIN32_FILE_ATTRIBUTE_DATA info{};const auto current=directory_/Names[0];
        if(GetFileAttributesExW(current.c_str(),GetFileExInfoStandard,&info)&&
            ((static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow)+line.size()+1>FileBytes){
            for(std::size_t i=Names.size()-1;i>0;--i){const auto source=directory_/Names[i-1],target=directory_/Names[i];
                if(!MoveFileExW(source.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING)&&GetLastError()!=ERROR_FILE_NOT_FOUND){Failed(key,"rotation_failed");return false;}}
        }
        Handle file(CreateFileW(current.c_str(),FILE_APPEND_DATA|GENERIC_READ|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(file.value==INVALID_HANDLE_VALUE||!SafeFile(file.value,root)){Failed(key,"file_write_open_failed");return false;}
        LARGE_INTEGER size{};if(!GetFileSizeEx(file.value,&size)){Failed(key,"file_size_failed");return false;}
        if(size.QuadPart){LARGE_INTEGER end{};end.QuadPart=size.QuadPart-1;char last=0;DWORD read=0;
            if(!SetFilePointerEx(file.value,end,nullptr,FILE_BEGIN)||!ReadFile(file.value,&last,1,&read,nullptr)||read!=1){Failed(key,"tail_check_failed");return false;}
            // A process crash may leave a torn final line. Start the next valid
            // record on its own line rather than poisoning it with that tail.
            if(last!='\n')line.insert(line.begin(),'\n');
        }
        DWORD written=0;if(!WriteFile(file.value,line.data(),static_cast<DWORD>(line.size()),&written,nullptr)||written!=line.size()){
            Failed(key,"file_write_failed");return false;}
        auto& states=Storage();if(states.size()>=32&&!states.count(key))states.erase(states.begin());states[key].lastSuccessAt=Now();return true;
    }catch(...){try{std::lock_guard<std::mutex> lock(HistoryMutex());Failed(Key(directory_),"unexpected_write_failure");}catch(...){}return false;}
}
WallpaperResourceHistoryExport ReadWallpaperResourceHistory(const fs::path& directory) noexcept {
    WallpaperResourceHistoryExport result;
    try{auto data=Load(directory);const auto records=Parse(data);
        for(const auto& record:records)result.records+=record.dump(-1,' ',false,Json::error_handler_t::replace)+'\n';
        result.summaryState=Analyze(records,data,result.csv);result.summary=Human(result.summaryState);
    }catch(...){result.records.clear();result.csv.clear();result.summary="壁纸资源历史读取失败；记录不可用，不表示占用为零。\r\n";
        try{result.summaryState={{"schemaVersion",1},{"status","unavailable"},{"recordCount",0},{"sampleCount",0},{"eventCount",0},
            {"storage",{{"configured",!directory.empty()},{"readFailures",1},{"writeFailures",0},{"lastWriteError",""},{"lastReadError","unexpected_read_failure"}}}};}catch(...){}
    }return result;
}
} // namespace lume::shell
