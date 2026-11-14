#include "apps/shell/wallpaper_resource_history.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs=std::filesystem;
using Json=nlohmann::json;
using lume::shell::WallpaperResourceHistory;
using lume::shell::ReadWallpaperResourceHistory;
bool passed=true;
void Check(bool condition,const char* label){if(!condition){passed=false;std::cerr<<"FAIL: "<<label<<'\n';}}
std::uint64_t Now(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count());}
const std::vector<const wchar_t*> Names{L"wallpaper-resources.jsonl",L"wallpaper-resources.1.jsonl",L"wallpaper-resources.2.jsonl",L"wallpaper-resources.3.jsonl"};
void Write(const fs::path& path,const std::string& value){std::ofstream stream(path,std::ios::binary);stream<<value;}
std::string Read(const fs::path& path){std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
Json Sample(std::uint64_t at,unsigned pid=42){
    return {{"schemaVersion",1},{"type","sample"},{"atUnixMs",at},{"uptimeMs",60000},{"runId","1000-2000"},
        {"logicalProcessors",16},{"gpuStatus","observed"},{"wallpaperGpuEnginePercent",2.5},
        {"processes",Json::array({{{"displayId","1"},{"wallpaperId","18446744073709551614"},{"pid",pid},
            {"createdFileTime",std::to_string(133000000000000000ull+pid)},{"state","playing"},
            {"manualPaused",false},{"systemPaused",false},{"parked",false},{"error",""},
            {"cpuMachinePercent",1.25},{"cpuIntervalMs",60000},{"privateBytes",100ull<<20},{"workingSetBytes",50ull<<20},
            {"gpuDedicatedBytes",64ull<<20},{"gpuSharedBytes",32ull<<20},{"gpuEnginePercent",2.5},{"gpuStatus","observed"}}})}};
}
Json First(const std::string& lines){const auto end=lines.find('\n');return Json::parse(lines.substr(0,end));}
void RedactionAndCsv(const fs::path& root){
    WallpaperResourceHistory history(root);auto sample=Sample(Now()-60000);
    sample["path"]="C:\\PRIVATE-PATH";sample["token"]="PRIVATE-TOKEN";sample["url"]="https://PRIVATE-URL.invalid/a";
    sample["processes"][0]["commandLine"]="C:\\PRIVATE-EXE --token=PRIVATE-CREDENTIAL";
    sample["processes"][0]["gpuErrors"]=Json::array({"PRIVATE-GPU-ERROR"});
    sample["processes"][0]["processStatus"]="PRIVATE-PROCESS-STATUS";
    Check(history.Append(sample),"append valid sample with unknown sensitive fields");
    auto event=sample;event["type"]="event";event["atUnixMs"]=sample["atUnixMs"].get<std::uint64_t>()+1000;
    event["runId"]="https://PRIVATE-RUN.invalid";event["processes"][0]["error"]="C:\\PRIVATE-ERROR";
    Check(history.Append(event),"event accepts safe identity while redacting untrusted scalar text");
    const auto exported=ReadWallpaperResourceHistory(root);const auto combined=exported.records+exported.csv+exported.summary+exported.summaryState.dump();
    Check(combined.find("PRIVATE-")==std::string::npos&&combined.find("https://")==std::string::npos&&combined.find("C:\\")==std::string::npos,
        "no source paths URLs credentials or arbitrary fields reach any export");
    const auto record=First(exported.records);Check(record["processes"][0]["wallpaperId"]=="18446744073709551614",
        "opaque decimal wallpaper identity is preserved exactly across export");
    const auto last=Json::parse(exported.records.substr(exported.records.find('\n')+1));
    Check(!last["processes"][0].contains("cpuMachinePercent")&&!last["processes"][0].contains("privateBytes"),
        "events never retain process resource fields");
    Check(exported.csv.rfind("\xEF\xBB\xBF",0)==0&&exported.csv.find("wallpaperId")!=std::string::npos,
        "CSV carries UTF8 BOM and wallpaper identity column");
    Check(exported.csv.find("'18446744073709551614")!=std::string::npos,
        "long opaque identifiers survive Excel numeric precision conversion");
    Check(exported.summaryState["sampleCount"]==1&&exported.summaryState["eventCount"]==1,
        "sample and event counts remain distinct");
    Check(exported.summary.find("60秒")!=std::string::npos&&exported.summary.find("null")!=std::string::npos&&
        exported.summary.find("PDH")!=std::string::npos&&exported.summary.find("重复归属")!=std::string::npos,
        "human summary explains sampling null values and GPU counter attribution");
    auto invalid=sample;invalid["schemaVersion"]=2;Check(!history.Append(invalid),"writer rejects unknown schema version");
    invalid=sample;invalid["processes"]=Json::array();for(unsigned i=0;i<65;++i)invalid["processes"].push_back(sample["processes"][0]);
    Check(!history.Append(invalid),"writer rejects unbounded process payloads");
}
void Statistics(const fs::path& root){
    WallpaperResourceHistory history(root);const auto now=Now();
    for(unsigned i=0;i<3;++i){auto sample=Sample(now-300000+i*60000);
        auto& process=sample["processes"][0];process["cpuMachinePercent"]=25+i*5;process["privateBytes"]=(1ull<<30)+i*1024;
        process["workingSetBytes"]=(100ull+i)<<20;process["gpuEnginePercent"]=85+i*5;
        process["gpuDedicatedBytes"]=(1ull<<30)+i*2048;process["gpuSharedBytes"]=nullptr;
        Check(history.Append(sample),"append sustained resource incident");}
    auto missing=Sample(now-120000,43);for(const auto* field:{"cpuMachinePercent","cpuIntervalMs","privateBytes","workingSetBytes","gpuEnginePercent","gpuDedicatedBytes","gpuSharedBytes"})
        missing["processes"][0][field]=nullptr;
    missing["processes"][0]["state"]="paused";missing["processes"][0]["manualPaused"]=true;
    Check(history.Append(missing),"append explicit missing metrics without substituting zero");
    auto event=Sample(now-60000,0);event["type"]="event";event["processes"][0]["state"]="parked";event["processes"][0]["parked"]=true;
    Check(history.Append(event),"append parked identity transition");
    event=Sample(now-1000,44);event["type"]="event";Check(history.Append(event),"append resumed replacement identity");
    const auto result=ReadWallpaperResourceHistory(root);const auto& state=result.summaryState;
    Check(state["recordCount"]==6&&state["sampleCount"]==4&&state["processSampleCount"]==4&&state["eventCount"]==2,
        "summary counts every valid sampled process and event");
    Check(state["pidReplacements"]==2&&state["stateChanges"]==3,"parking does not manufacture an extra PID replacement");
    Check(state["processes"].size()==2&&state["displays"].size()==1,"per-process identities and logical display trends are separate");
    const auto& trend=state["processes"][0];Check(trend["pid"]==42&&trend["cpuMeanPercent"]==30&&trend["cpuPeakPercent"]==35,
        "CPU mean and peak use only known values for the same process lifetime");
    Check(trend["privatePeakBytes"]==((1ull<<30)+2048)&&trend["gpuPeakPercent"]==95,
        "resource peaks retain their metric and unit");
    Check(state["missing"]["gpuSharedBytes"]==4&&state["missing"]["cpuMachinePercent"]==1,
        "missing values remain visible in coverage statistics");
    Check(state["anomalies"]["sampleCount"]==3&&state["anomalies"]["sustainedCount"]==4,
        "three consecutive highs establish one sustained episode per threshold");
    Check(state["anomalies"]["firstAtUnixMs"]==now-300000&&state["anomalies"]["lastAtUnixMs"]==now-180000,
        "incident first and latest observations are exact timestamps");
    Check(state["anomalies"]["samples"][2]["sustainedKinds"].size()==4,"incident evidence retains threshold kinds and continuous high markers");
    Check(result.summary.find("PID替换 2")!=std::string::npos&&result.summary.find("首次观察异常")!=std::string::npos,
        "human summary exposes recovery continuity and incident times");
}
void MalformedAndWindow(const fs::path& root){
    fs::create_directories(root);const auto now=Now();auto valid=Sample(now-60000);
    auto old=Sample(now-73ull*60*60*1000),future=Sample(now+10*60*1000);
    auto invalid=valid;invalid["atUnixMs"]=-1;
    std::string deep(30,'[');deep+='0';deep+=std::string(30,']');
    Write(root/Names[0],old.dump()+'\n'+future.dump()+'\n'+invalid.dump()+'\n'+"{bad-json}\n"+deep+'\n'+
        std::string(70000,'x')+'\n'+valid.dump()+'\n'+"{\"schemaVersion\":1,\"type\":");
    const auto result=ReadWallpaperResourceHistory(root);const auto& state=result.summaryState;
    Check(state["recordCount"]==1&&First(result.records)["atUnixMs"]==now-60000,"only valid records in the 72 hour window survive");
    Check(state["discarded"]["expired"]==1&&state["discarded"]["future"]==1&&state["discarded"]["oversized"]==1&&
        state["discarded"]["incomplete"]==1&&state["discarded"]["invalid"]==3,"all discarded data has bounded explicit reasons");
    Check(state["status"]=="partial","malformed or incomplete input never presents full coverage");
    Check(result.records.size()<65536&&result.records.find("{bad-json}")==std::string::npos,"malformed lines cannot be copied into exported JSONL");
    Write(root/Names[0],std::string(9u<<20,'x')+'\n'+valid.dump()+'\n');
    const auto oversized=ReadWallpaperResourceHistory(root);
    Check(oversized.summaryState["recordCount"]==1&&oversized.summaryState["discarded"]["limited"].get<unsigned>()>=1,
        "oversized on-disk file reads only a bounded newest tail");
}
void RotationAndLimit(const fs::path& root){
    fs::create_directories(root);for(const auto* name:Names)Write(root/name,std::string((2u<<20)-1,' ')+"\n");
    WallpaperResourceHistory history(root);Check(history.Append(Sample(Now()-1000)),"full current file rotates before append");
    std::uintmax_t total=0;unsigned count=0;for(const auto& file:fs::directory_iterator(root)){
        ++count;const auto size=file.file_size();Check(size<=(2u<<20),"every generated history segment is at most 2 MiB");total+=size;}
    Check(count==4&&total<=(8u<<20),"rotation maintains exactly four bounded files without temporary artifacts");
    Check(ReadWallpaperResourceHistory(root).summaryState["recordCount"]==1,"reader searches all four rotating segments safely");
    const auto many=root/L"many";fs::create_directories(many);auto sample=Sample(Now()-30000);sample["processes"]=Json::array();
    std::string lines;for(unsigned i=0;i<12020;++i){sample["atUnixMs"]=Now()-20000+i;lines+=sample.dump()+'\n';}
    // Use two ordinary rotation segments so neither exceeds its per-file cap.
    const auto middle=lines.find('\n',lines.size()/2);Write(many/Names[1],lines.substr(0,middle+1));Write(many/Names[0],lines.substr(middle+1));
    const auto limited=ReadWallpaperResourceHistory(many);
    Check(limited.summaryState["recordCount"]==12000&&limited.summaryState["discarded"]["limited"].get<unsigned>()>=20,
        "record count cap retains a bounded newest history");
    Check(limited.records.size()<=(8u<<20),"sanitized JSONL expansion also respects the 8 MiB budget");
}
void StorageFailures(const fs::path& root){
    fs::create_directories(root);const auto blocked=root/L"not-a-directory";Write(blocked,"outside-marker");
    WallpaperResourceHistory history(blocked);Check(!history.Append(Sample(Now())),"an unavailable directory cannot throw into playback");
    const auto failed=ReadWallpaperResourceHistory(blocked);
    Check(failed.summaryState["storage"]["writeFailures"]==1&&!failed.summaryState["storage"]["lastWriteError"].get<std::string>().empty(),
        "export explicitly carries writer failures even without a history file");
    Check(Read(blocked)=="outside-marker","invalid history root is never overwritten");
    const auto linkRoot=root/L"links";fs::create_directories(linkRoot);const auto external=root/L"outside.jsonl";
    Write(external,Sample(Now()-1000).dump()+'\n');
    if(CreateHardLinkW((linkRoot/Names[0]).c_str(),external.c_str(),nullptr)){
        const auto original=Read(external);WallpaperResourceHistory linked(linkRoot);
        Check(!linked.Append(Sample(Now())),"writer refuses a hardlink to a file outside its directory");
        const auto result=ReadWallpaperResourceHistory(linkRoot);
        Check(result.records.empty()&&result.summaryState["storage"]["readFailures"]==1,"reader refuses outside hardlink data too");
        Check(Read(external)==original,"outside hardlink target remains unchanged");
    }
    const auto real=root/L"real",alias=root/L"alias";fs::create_directories(real);
    if(CreateSymbolicLinkW(alias.c_str(),real.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|0x2)){
        WallpaperResourceHistory linked(alias);Check(!linked.Append(Sample(Now())),"history never traverses a reparse directory");
        Check(fs::is_empty(real),"reparse directory target receives no history files");
    }
}
void Concurrent(const fs::path& root){
    std::atomic<unsigned> failures{0};std::vector<std::thread> writers;
    for(unsigned worker=0;worker<4;++worker)writers.emplace_back([&,worker]{WallpaperResourceHistory history(root);
        for(unsigned i=0;i<40;++i)if(!history.Append(Sample(Now()-1000,100+worker)))++failures;});
    for(unsigned i=0;i<10;++i){const auto result=ReadWallpaperResourceHistory(root);if(result.summaryState.value("status",std::string{})=="unavailable")++failures;}
    for(auto& writer:writers)writer.join();
    const auto result=ReadWallpaperResourceHistory(root);
    Check(failures==0&&result.summaryState["recordCount"]==160,"concurrent writers and report readers retain complete independent JSONL records");
    Check(result.summaryState["discarded"]["invalid"]==0&&result.summaryState["discarded"]["incomplete"]==0,
        "reader never sees a partially written line from concurrent append");
}
} // namespace

int main(){
    const auto root=fs::temp_directory_path()/("lume-wallpaper-history-test-"+std::to_string(GetCurrentProcessId())+'-'+std::to_string(Now()));
    fs::create_directories(root);
    const auto disabled=ReadWallpaperResourceHistory({});Check(disabled.summaryState["status"]=="disabled"&&disabled.records.empty(),"unconfigured history is normal and explicit");
    Check(!WallpaperResourceHistory({}).Append(Sample(Now())),"disabled writer performs no I/O");
    const auto empty=ReadWallpaperResourceHistory(root/L"empty");Check(empty.summaryState["status"]=="empty"&&empty.summary.find("新版")!=std::string::npos,
        "new installation explains that history begins with the new version");
    RedactionAndCsv(root/L"redaction");Statistics(root/L"statistics");MalformedAndWindow(root/L"malformed");
    RotationAndLimit(root/L"rotation");StorageFailures(root/L"failures");Concurrent(root/L"concurrent");
    std::error_code ignored;fs::remove_all(root,ignored);return passed?0:1;
}
