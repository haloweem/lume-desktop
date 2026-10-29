#include "platform/windows/mpv_process_controller.h"
#include "vendor/nlohmann/json.hpp"

#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using Json=nlohmann::json;
struct CloseObservedProcess {
    void operator()(void* handle) const { CloseHandle(handle); }
};
using ObservedProcess=std::unique_ptr<void,CloseObservedProcess>;
void Require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void Pump(DWORD milliseconds) {
    const auto end=GetTickCount64()+milliseconds;
    do {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        Sleep(5);
    } while(GetTickCount64()<end);
}
std::vector<DWORD> Processes(HWND parent) {
    std::vector<DWORD> result={GetCurrentProcessId()};
    EnumChildWindows(parent,[](HWND window,LPARAM context)->BOOL {
        DWORD process=0;GetWindowThreadProcessId(window,&process);
        auto& values=*reinterpret_cast<std::vector<DWORD>*>(context);
        if(process&&std::find(values.begin(),values.end(),process)==values.end())values.push_back(process);
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
std::uint64_t Ticks(FILETIME time) {
    return (static_cast<std::uint64_t>(time.dwHighDateTime)<<32u)|time.dwLowDateTime;
}
struct Resources {
    std::uint64_t workingSet=0,privateBytes=0,privateWorkingSet=0,cpuTicks=0;
};
Resources Read(const std::vector<DWORD>& processes) {
    Resources result;
    SYSTEM_INFO system{};GetSystemInfo(&system);
    for(const auto id:processes) {
        const auto process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,id);
        Require(process!=nullptr,"resource probe cannot open owned process");
        PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
        FILETIME created{},exited{},kernel{},user{};
        const bool queried=GetProcessMemoryInfo(process,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
            sizeof(memory))&&GetProcessTimes(process,&created,&exited,&kernel,&user);
        // Query only our probe and the player's PID discovered below our owned parent.
        std::vector<ULONG_PTR> storage(262145);
        const bool resident=QueryWorkingSet(process,storage.data(),static_cast<DWORD>(storage.size()*sizeof(ULONG_PTR)))!=FALSE;
        CloseHandle(process);
        Require(queried&&resident,"owned process resource query failed");
        result.workingSet+=memory.WorkingSetSize;result.privateBytes+=memory.PrivateUsage;
        result.cpuTicks+=Ticks(kernel)+Ticks(user);
        const auto pages=reinterpret_cast<const PSAPI_WORKING_SET_INFORMATION*>(storage.data());
        Require(pages->NumberOfEntries<storage.size(),"owned working set exceeds probe capacity");
        for(ULONG_PTR index=0;index<pages->NumberOfEntries;++index)
            if(!pages->WorkingSetInfo[index].Shared)result.privateWorkingSet+=system.dwPageSize;
    }
    return result;
}
Json Sample(HWND parent,unsigned count) {
    const auto processes=Processes(parent);
    const auto before=Read(processes);
    const auto start=GetTickCount64();
    double workingSet=0,privateBytes=0,privateWorkingSet=0;
    for(unsigned index=0;index<count;++index) {
        Pump(250);const auto sample=Read(processes);
        workingSet+=static_cast<double>(sample.workingSet);
        privateBytes+=static_cast<double>(sample.privateBytes);
        privateWorkingSet+=static_cast<double>(sample.privateWorkingSet);
    }
    const auto after=Read(processes);
    const auto seconds=static_cast<double>(GetTickCount64()-start)/1000.0;
    const auto processors=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    const double divisor=static_cast<double>(count)*1024.0*1024.0;
    return {{"pids",processes},{"externalPlayerCount",processes.size()-1},
        {"meanWorkingSetMiB",workingSet/divisor},{"meanPrivateBytesMiB",privateBytes/divisor},
        {"meanPrivateWorkingSetMiB",privateWorkingSet/divisor},
        {"normalizedCpuPercent",static_cast<double>(after.cpuTicks-before.cpuTicks)/
            (seconds*10000000.0*static_cast<double>(processors))*100.0},{"seconds",seconds}};
}
}
int main(int argc,char** argv) {
    if(argc!=4) {std::cerr<<"Usage: static-wallpaper-resource-probe mpv image output.json\n";return 2;}
    HWND parent=nullptr;
    try {
        const auto image=std::filesystem::absolute(std::filesystem::u8path(argv[2]));
        const auto output=std::filesystem::absolute(std::filesystem::u8path(argv[3]));
        Require(!std::filesystem::exists(output),"resource report requires a new output file");
        const auto displays=lume::platform::windows::EnumerateMpvDisplays();
        Require(!displays.empty(),"probe needs an enumerated monitor");
        parent=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Owned static wallpaper probe",
            WS_POPUP,-30000,-30000,1920,1080,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Require(parent!=nullptr,"create isolated probe parent");ShowWindow(parent,SW_SHOWNOACTIVATE);
        Json report={{"canvasWidth",1920},{"canvasHeight",1080},{"image",image.u8string()},
            {"scope","owned offscreen probe and embedded player only; excludes management WebView, widgets and GPU"}};
        {
            lume::platform::windows::MpvProcessController controller(
                std::filesystem::absolute(std::filesystem::u8path(argv[1])).wstring(),parent);
            report["idle"]=Sample(parent,4);
            if(!controller.Start(displays.front().id,image.u8string()))throw std::runtime_error(controller.LastError());
            Require(controller.SetFit(displays.front().id,true)&&controller.SetVolume(displays.front().id,0)&&
                controller.Command(displays.front().id,lume::wallpaper::MpvCommand::Resume),"apply wallpaper defaults");
            Pump(1000);report["playing"]=Sample(parent,40);
            std::vector<ObservedProcess> observedPlayers;
            for(const auto& value:report["playing"]["pids"]) {
                const auto id=value.get<DWORD>();
                if(id==GetCurrentProcessId())continue;
                ObservedProcess process(OpenProcess(SYNCHRONIZE,FALSE,id));
                Require(bool(process),"retain the observed player's exact process for exit verification");
                observedPlayers.push_back(std::move(process));
            }
            controller.Stop(displays.front().id);
            for(const auto& process:observedPlayers)
                Require(WaitForSingleObject(process.get(),2000)==WAIT_OBJECT_0,
                    "stopped player process must exit even if its window has detached");
            report["observedPlayersExited"]=true;
            Pump(300);report["stopped"]=Sample(parent,4);
            Require(!controller.IsRunning(displays.front().id)&&GetWindow(parent,GW_CHILD)==nullptr,
                "resource probe must release all owned windows and players");
        }
        DestroyWindow(parent);parent=nullptr;
        std::ofstream file(output,std::ios::binary);file<<report.dump(2)<<'\n';
        Require(bool(file),"write probe resource report");
        std::cout<<report.dump()<<'\n';return 0;
    } catch(const std::exception& error) {
        if(parent)DestroyWindow(parent);
        std::cerr<<error.what()<<'\n';return 1;
    }
}
