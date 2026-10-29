#include "apps/shell/almanac_session.h"
#include "apps/shell/organizer_session.h"
#include "apps/shell/performance_session.h"
#include "apps/shell/weather_session.h"
#include "platform/windows/material/material_api.h"
#include "platform/windows/organizer/organizer_icons.h"
#include "platform/windows/organizer/organizer_icon_client.h"
#include "process_memory_snapshot.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using lume::ui::PageCommand;
using namespace lume::shell;
using namespace lume::platform::windows;
using json=nlohmann::json;

void Check(bool value,const char* reason) {
    if(!value)throw std::runtime_error(reason);
}
void Pump(DWORD milliseconds) {
    const auto end=GetTickCount64()+milliseconds;
    do {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    } while(GetTickCount64()<end);
}
json DxgiProcessMemory(const char* phase,int round) {
    json result{{"schemaVersion",1},{"phase",phase},{"round",round},{"pid",GetCurrentProcessId()},
        {"tickMs",GetTickCount64()},{"source","IDXGIAdapter3::QueryVideoMemoryInfo"},
        {"coverage","probe_process_material_adapter_index_0_node_0"},{"adapterIndex",0},{"nodeIndex",0},
        {"complete",false},{"adapterLuid",""},{"localHresult",nullptr},{"nonLocalHresult",nullptr},
        {"localCurrentUsageBytes",nullptr},{"nonLocalCurrentUsageBytes",nullptr}};
    // Query inside the measured process. A DXGI query in the PowerShell sampler
    // would report PowerShell's allocations, not the widget probe's allocations.
    struct Module {
        HMODULE value=LoadLibraryExW(L"dxgi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        ~Module(){if(value)FreeLibrary(value);}
    } module;
    if(!module.value){result["errorStage"]="load_dxgi";result["errorCode"]=GetLastError();return result;}
    using CreateFactory=HRESULT(WINAPI*)(REFIID,void**);
    CreateFactory create=nullptr;
    const auto entry=GetProcAddress(module.value,"CreateDXGIFactory1");
    static_assert(sizeof(entry)==sizeof(create));
    std::memcpy(&create,&entry,sizeof(entry));
    if(!create){result["errorStage"]="factory_entry";result["errorCode"]=GetLastError();return result;}
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    auto status=create(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(factory.GetAddressOf()));
    if(FAILED(status)){result["errorStage"]="create_factory";result["errorCode"]=status;return result;}
    // LumeGlass selects EnumAdapters(0). Keep that exact scope; summing virtual
    // display adapters can count the same backing GPU allocations repeatedly.
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    status=factory->EnumAdapters1(0,adapter.GetAddressOf());
    if(FAILED(status)){result["errorStage"]="enumerate_material_adapter";result["errorCode"]=status;return result;}
    DXGI_ADAPTER_DESC1 description{};
    status=adapter->GetDesc1(&description);
    if(FAILED(status)){result["errorStage"]="describe_adapter";result["errorCode"]=status;return result;}
    char identity[32]{};
    std::snprintf(identity,sizeof(identity),"%08lx_%08lx",
        static_cast<unsigned long>(description.AdapterLuid.HighPart),
        static_cast<unsigned long>(description.AdapterLuid.LowPart));
    result["adapterLuid"]=identity;result["adapterVendorId"]=description.VendorId;
    result["adapterDeviceId"]=description.DeviceId;
    result["adapterIsSoftware"]=(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> budgetAdapter;
    status=adapter.As(&budgetAdapter);
    if(FAILED(status)){result["errorStage"]="query_adapter3";result["errorCode"]=status;return result;}
    DXGI_QUERY_VIDEO_MEMORY_INFO local{},nonlocal{};
    const auto localStatus=budgetAdapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local);
    const auto nonlocalStatus=budgetAdapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&nonlocal);
    result["localHresult"]=localStatus;result["nonLocalHresult"]=nonlocalStatus;
    if(SUCCEEDED(localStatus)){
        result["localCurrentUsageBytes"]=local.CurrentUsage;result["localBudgetBytes"]=local.Budget;
    }
    if(SUCCEEDED(nonlocalStatus)){
        result["nonLocalCurrentUsageBytes"]=nonlocal.CurrentUsage;result["nonLocalBudgetBytes"]=nonlocal.Budget;
    }
    result["complete"]=SUCCEEDED(localStatus)&&SUCCEEDED(nonlocalStatus);
    result["collectionEndTickMs"]=GetTickCount64();
    return result;
}
// An accounting-only outer Job records even children that finish between two
// sampler ticks. It adds no limit, worker or instrumentation to the product.
class ProcessTreeAccounting {
public:
    ProcessTreeAccounting() {
        job_=CreateJobObjectW(nullptr,nullptr);
        if(!job_||!AssignProcessToJobObject(job_,GetCurrentProcess())) {
            const auto error=GetLastError();
            if(job_)CloseHandle(job_);
            job_=nullptr;
            throw std::runtime_error("probe process-tree accounting Job, Win32 error "+std::to_string(error));
        }
    }
    ~ProcessTreeAccounting(){if(job_)CloseHandle(job_);}
    ProcessTreeAccounting(const ProcessTreeAccounting&)=delete;
    ProcessTreeAccounting& operator=(const ProcessTreeAccounting&)=delete;
    json Snapshot(const char* phase,int round)const {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION memory{};
        const bool timeOk=QueryInformationJobObject(job_,JobObjectBasicAccountingInformation,
            &accounting,sizeof(accounting),nullptr)!=FALSE;
        const auto timeError=timeOk?ERROR_SUCCESS:GetLastError();
        const bool memoryOk=QueryInformationJobObject(job_,JobObjectExtendedLimitInformation,
            &memory,sizeof(memory),nullptr)!=FALSE;
        const auto memoryError=memoryOk?ERROR_SUCCESS:GetLastError();
        json result{{"schemaVersion",1},{"phase",phase},{"round",round},
            {"pid",GetCurrentProcessId()},{"tickMs",GetTickCount64()},
            {"source","QueryInformationJobObject"},
            {"coverage","probe_and_all_future_descendants_including_nested_icon_jobs"},
            {"complete",timeOk&&memoryOk},{"timeError",timeError},{"memoryError",memoryError},
            {"limitFlags",nullptr},{"totalProcesses",nullptr},{"activeProcesses",nullptr},
            {"terminatedByLimit",nullptr},{"kernelTime100ns",nullptr},{"userTime100ns",nullptr},
            {"peakProcessCommitBytes",nullptr},{"peakJobCommitBytes",nullptr}};
        if(timeOk) {
            result["totalProcesses"]=accounting.TotalProcesses;
            result["activeProcesses"]=accounting.ActiveProcesses;
            result["terminatedByLimit"]=accounting.TotalTerminatedProcesses;
            result["kernelTime100ns"]=accounting.TotalKernelTime.QuadPart;
            result["userTime100ns"]=accounting.TotalUserTime.QuadPart;
        }
        if(memoryOk) {
            result["limitFlags"]=memory.BasicLimitInformation.LimitFlags;
            result["peakProcessCommitBytes"]=memory.PeakProcessMemoryUsed;
            result["peakJobCommitBytes"]=memory.PeakJobMemoryUsed;
        }
        return result;
    }
private:
    HANDLE job_=nullptr;
};
struct Probe {
    std::filesystem::path phasePath;
    int round=0;
    DWORD dwell=0;
    LumeMaterialStatisticsFn statistics=nullptr;
    const ProcessTreeAccounting* processTree=nullptr;
    LumeMaterialStatisticsData Materials()const {
        LumeMaterialStatisticsData result{};result.size=sizeof(result);
        Check(statistics&&statistics(&result),"material diagnostic query");
        return result;
    }
    void Phase(const char* name)const {
        const auto tick=GetTickCount64();
        const auto temporary=std::filesystem::path(phasePath.wstring()+L".tmp");
        const auto value=json{{"phase",name},{"round",round},{"pid",GetCurrentProcessId()},{"tickMs",tick}};
        { std::ofstream output(temporary);output<<value.dump();Check(output.good(),"phase write"); }
        bool published=false;
        for(int retry=0;retry<100;++retry) {
            if(MoveFileExW(temporary.c_str(),phasePath.c_str(),
                          MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
                published=true;break;
            }
            const auto error=GetLastError();
            if(error!=ERROR_SHARING_VIOLATION&&error!=ERROR_ACCESS_DENIED)break;
            Sleep(10);
        }
        Check(published,"phase publication");
        std::cout<<"PHASE "<<name<<" round="<<round<<" tick="<<tick<<std::endl;
    }
    void Observe(const char* phase,const json& business=json::object())const {
        Phase(phase);Pump(dwell);Phase("diagnostic");
        Check(WriteProcessMemorySnapshot(phasePath.parent_path()/"memory-snapshots.jsonl",phase,round),
              "memory snapshot");
        std::ofstream gpu(phasePath.parent_path()/"gpu-snapshots.jsonl",std::ios::app);
        gpu<<DxgiProcessMemory(phase,round).dump()<<'\n';gpu.close();
        Check(gpu.good(),"DXGI process GPU snapshot write");
        std::ofstream jobs(phasePath.parent_path()/"job-snapshots.jsonl",std::ios::app);
        jobs<<processTree->Snapshot(phase,round).dump()<<'\n';jobs.close();
        Check(jobs.good(),"kernel process-tree accounting snapshot write");
        const auto material=Materials();
        std::ofstream output(phasePath.parent_path()/"component-snapshots.jsonl",std::ios::app);
        output<<json{{"phase",phase},{"round",round},{"tickMs",GetTickCount64()},
            {"materialUsers",material.users},{"materialDevices",material.devices},
            {"materialCaptures",material.captures},{"organizerIconWorkers",OrganizerIconCache::ActiveWorkers()},
            {"organizerHelperHosts",OrganizerIconExtractor::ActiveHosts()},
            {"organizerPendingCleanup",OrganizerIconExtractor::PendingCleanup()},
            {"business",business}}.dump()<<'\n';
        Check(output.good(),"component resource snapshot");
    }
};
void Place(HWND hwnd,int slot) {
    Check(hwnd!=nullptr,"enabled component window");
    const int dpi=static_cast<int>(GetDpiForWindow(hwnd));
    RECT work{};Check(SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0)!=FALSE,"work area");
    const int x=work.left+MulDiv(30+(slot%2)*450,dpi,96);
    const int y=work.top+MulDiv(50+(slot/2)*265,dpi,96);
    Check(SetWindowPos(hwnd,nullptr,x,y,0,0,SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOSIZE)!=FALSE,
          "place separate measurement windows");
    EnableWindow(hwnd,FALSE);
    Check(!IsWindowEnabled(hwnd),"isolate measurement windows from accidental input");
}
int Run(const std::filesystem::path& phase,int rounds,DWORD dwell,
        const std::string& mode,DWORD cooldown) {
    Check(std::filesystem::is_directory(phase.parent_path()),"existing measurement output directory");
    const auto config=phase.parent_path()/"probe-config";
    Check(!std::filesystem::exists(config),"fresh private probe configuration required");
    Check(std::filesystem::create_directory(config),"private configuration directory");
    ProcessTreeAccounting processTree;
    HMODULE glass=LoadLibraryW(L"LumeGlass.dll");
    Check(glass!=nullptr,"shared material library");
    LumeMaterialStatisticsFn statistics=nullptr;
    const auto entry=GetProcAddress(glass,"LumeMaterialStatistics");
    static_assert(sizeof(entry)==sizeof(statistics));
    std::memcpy(&statistics,&entry,sizeof(entry));
    Probe probe{phase,0,dwell,statistics,&processTree};
    {
        const auto instance=GetModuleHandleW(nullptr);
        WeatherSession weather(instance,(config/"weather.json").u8string(),{},[]{});
        AlmanacSession almanac(instance,(config/"almanac.json").u8string(),{},[]{});
        PerformanceSession performance(instance,(config/"performance.json").u8string(),{},[]{});
        OrganizerSession organizer(instance,(config/"organizer.json").u8string(),{},[]{});
        const bool all=mode=="widgets-combined";
        const bool useWeather=all||mode=="weather-only",useAlmanac=all||mode=="almanac-only";
        const bool usePerformance=all||mode=="performance-only",useOrganizer=all||mode=="organizer-only";
        const unsigned count=useWeather+useAlmanac+usePerformance+useOrganizer;
        Check(count>0,"known resource mode");
        weather.HandlePage(PageCommand::WeatherDisable);almanac.HandlePage(PageCommand::AlmanacDisable);
        performance.HandlePage(PageCommand::PerformanceDisable);organizer.HandlePage(PageCommand::OrganizerDisable);
        weather.HandlePage(PageCommand::WeatherCityShanghai);
        const auto assertDisabled=[&] {
            const auto material=probe.Materials();
            Check(!weather.Running()&&!almanac.Running()&&!performance.Running()&&!organizer.Running(),
                  "all component windows and services stopped");
            Check(!weather.Handle()&&!almanac.Handle()&&!performance.Handle()&&!organizer.Handle(),
                  "all own native window handles released");
            Check(material.users==0&&material.devices==0&&material.captures==0,
                  "last material lease releases device and capture");
            Check(OrganizerIconCache::ActiveWorkers()==0,"icon extraction worker released");
            Check(OrganizerIconExtractor::ActiveHosts()==0,"icon extraction helper process released");
            Check(OrganizerIconExtractor::PendingCleanup()==0,"icon extraction deferred cleanup released");
        };
        assertDisabled();probe.Observe("initial_disabled");
        for(int round=1;round<=rounds;++round) {
            probe.round=round;
            if(useWeather){weather.HandlePage(PageCommand::WeatherEnable);Place(weather.Handle(),0);}
            if(useAlmanac){almanac.HandlePage(PageCommand::AlmanacEnable);Place(almanac.Handle(),1);}
            if(usePerformance){performance.HandlePage(PageCommand::PerformanceEnable);Place(performance.Handle(),2);}
            if(useOrganizer) {
                organizer.HandlePage(PageCommand::OrganizerEnable);
                if(round==1) {
                    wchar_t windows[MAX_PATH]{};
                    Check(GetWindowsDirectoryW(windows,MAX_PATH)>0,"Windows directory");
                    // References only; the probe never opens, moves or deletes these files.
                    Check(organizer.AddPaths({(std::filesystem::path(windows)/"explorer.exe").u8string(),
                                             (std::filesystem::path(windows)/"System32"/"notepad.exe").u8string()}),
                          "known system file references for real shell icon extraction");
                }
                Place(organizer.Handle(),3);
            }
            Check(weather.Running()==useWeather&&almanac.Running()==useAlmanac&&
                  performance.Running()==usePerformance&&organizer.Running()==useOrganizer,
                  "only requested production modules running");
            Check(probe.Materials().users==count&&probe.Materials().devices==1,
                  "active components lease a single shared material device");
            probe.Observe("components_enabled",{{"weather",useWeather},{"almanac",useAlmanac},
                {"performance",usePerformance},{"organizer",useOrganizer}});
            if(useOrganizer)Check(OrganizerIconCache::ActiveWorkers()==0&&OrganizerIconExtractor::ActiveHosts()==0&&
                                  OrganizerIconExtractor::PendingCleanup()==0,
                                  "cached icons retain no idle worker or helper process");
            if(usePerformance)Check(performance.Sample().has_value(),"actual system performance sample");
            // A provider outage remains visible in evidence. It does not become synthetic weather.
            std::ofstream readings(phase.parent_path()/"data-status.jsonl",std::ios::app);
            const auto weatherReading=weather.Reading();
            readings<<json{{"round",round},{"weatherEnabled",useWeather},
                {"weatherAvailable",weatherReading.snapshot.has_value()},
                {"weatherError",static_cast<int>(weatherReading.error)},
                {"weatherLoading",weatherReading.loading},
                {"performanceAvailable",performance.Sample().has_value()}}.dump()<<'\n';
            weather.HandlePage(PageCommand::WeatherDisable);almanac.HandlePage(PageCommand::AlmanacDisable);
            performance.HandlePage(PageCommand::PerformanceDisable);organizer.HandlePage(PageCommand::OrganizerDisable);
            assertDisabled();probe.Observe("component_disabled");
        }
        weather.Shutdown();almanac.Shutdown();performance.Shutdown();organizer.Shutdown();
        assertDisabled();
        if(cooldown){const auto prior=probe.dwell;probe.dwell=cooldown;probe.Observe("cooldown");probe.dwell=prior;}
        probe.Phase("complete");Pump(dwell);
    }
    FreeLibrary(glass);
    std::cout<<"RESULT PASS mode="<<mode<<"; production sessions, isolated config, no manager or system effects\n";
    return 0;
}
}
int main(int argc,char** argv) {
    if(argc!=6)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com))return 1;
    int result=1;
    try {
        const int rounds=std::stoi(argv[2]),dwell=std::stoi(argv[3]),cooldown=std::stoi(argv[5]);
        Check(rounds>=1&&rounds<=100&&dwell>=2000&&dwell<=30000&&cooldown>=0&&cooldown<=600,
              "bounded resource probe arguments");
        result=Run(std::filesystem::u8path(argv[1]),rounds,static_cast<DWORD>(dwell),argv[4],
                   static_cast<DWORD>(cooldown)*1000);
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';}
    CoUninitialize();return result;
}
