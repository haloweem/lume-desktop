#include "apps/shell/diagnostic_report.h"
#include "apps/shell/self_check.h"
#include "apps/shell/wallpaper_resource_history.h"
#include "platform/windows/diagnostics/runtime_log.h"
#include "platform/windows/ui_stall_trace.h"
#include "platform/windows/material/material_diagnostics.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace lume;
namespace fs = std::filesystem;
namespace {
int failures = 0;
void Check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; } }
void Write(const fs::path& file, const std::string& text) { std::ofstream(file, std::ios::binary) << text; }
std::string Read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
bool Has(const std::string& zip, const std::string& name) { return zip.find(name) != std::string::npos; }
std::string ZipPart(const std::string& zip, const std::string& name) {
    const auto integer = [&](std::size_t at, unsigned bytes) {
        std::uint32_t value = 0;
        for (unsigned i = 0; i < bytes; ++i) value |= static_cast<std::uint32_t>(static_cast<unsigned char>(zip[at + i])) << (8 * i);
        return value;
    };
    for (std::size_t at = 0; at + 30 <= zip.size() && zip.compare(at, 4, "PK\x03\x04") == 0;) {
        const auto size = integer(at + 18, 4);
        const auto nameBytes = integer(at + 26, 2), extraBytes = integer(at + 28, 2);
        const auto data = at + 30 + nameBytes + extraBytes;
        if (data > zip.size() || size > zip.size() - data) break;
        if (zip.compare(at + 30, nameBytes, name) == 0) return zip.substr(data, size);
        at = data + size;
    }
    return {};
}
std::size_t ZipEntries(const std::string& zip) {
    if(zip.size()<22||zip.compare(zip.size()-22,4,"PK\x05\x06")!=0)return 0;
    return static_cast<unsigned char>(zip[zip.size()-12])|
           (static_cast<std::size_t>(static_cast<unsigned char>(zip[zip.size()-11]))<<8);
}
void WriteWallpaperHistory(const fs::path& logs) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    nlohmann::json sample{{"schemaVersion",1},{"type","sample"},{"atUnixMs",now - 120000},{"uptimeMs",60000},
        {"runId","fixture-1234"},{"logicalProcessors",16},{"gpuStatus","observed"},{"wallpaperGpuEnginePercent",2.5},
        {"url","https://private.invalid/PRIVATE-WALLPAPER-URL"},{"token","PRIVATE-WALLPAPER-TOKEN"},
        {"processes",nlohmann::json::array({{{"displayId","1"},{"pid",42},{"createdFileTime","133000000000000000"},
            {"state","playing"},{"manualPaused",false},{"systemPaused",false},{"parked",false},{"error",""},
            {"cpuMachinePercent",1.25},{"cpuIntervalMs",60000},{"privateBytes",104857600},{"workingSetBytes",52428800},
            {"gpuDedicatedBytes",67108864},{"gpuSharedBytes",33554432},{"gpuEnginePercent",2.5},{"gpuStatus","observed"},
            {"path","C:\\PRIVATE-WALLPAPER-PATH\\secret.mp4"}}})}};
    Write(logs / L"wallpaper-resources.1.jsonl",sample.dump()+"\n");
    sample["atUnixMs"]=now - 60000;sample["uptimeMs"]=120000;
    sample["processes"][0]["privateBytes"]=209715200;sample["processes"][0]["cpuMachinePercent"]=4.5;
    Write(logs / L"wallpaper-resources.jsonl",sample.dump()+"\n");
}
std::pair<std::size_t,std::uintmax_t> ReportTotals(const fs::path& folder) {
    std::size_t count=0;std::uintmax_t bytes=0;
    for(const auto& entry:fs::directory_iterator(folder)) {
        const auto attributes=GetFileAttributesW(entry.path().c_str());
        const auto name=entry.path().filename().wstring();
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) ||
           name.rfind(L"Lume诊断报告-",0)!=0 || name.size()!=std::wstring(L"Lume诊断报告-").size()+19 || entry.path().extension()!=L".zip")continue;
        ++count;bytes+=entry.file_size();
    }
    return {count,bytes};
}

// Suspended copies only: these fixtures never execute application code or
// touch Explorer. A second suspended process represents another Lume owner.
struct ProcessFixture {
    HANDLE process = nullptr;
    ~ProcessFixture() {
        if (process) { TerminateProcess(process, 0); WaitForSingleObject(process, 3000); CloseHandle(process); }
    }
    bool Start(const fs::path& executable, HANDLE parent = nullptr) {
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = parent ? sizeof(startup) : sizeof(STARTUPINFOW);
        std::vector<unsigned char> attributes;
        if (parent) {
            SIZE_T size = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
            attributes.resize(size);
            startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
            if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) return false;
            if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
                    &parent, sizeof(parent), nullptr, nullptr)) {
                DeleteProcThreadAttributeList(startup.lpAttributeList); return false;
            }
        }
        PROCESS_INFORMATION child{};
        std::wstring command = L"\"" + executable.wstring() + L"\"";
        const bool ok = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | (parent ? EXTENDED_STARTUPINFO_PRESENT : 0),
            nullptr, executable.parent_path().c_str(), &startup.StartupInfo, &child) != FALSE;
        if (parent) DeleteProcThreadAttributeList(startup.lpAttributeList);
        if (ok) { process = child.hProcess; CloseHandle(child.hThread); }
        return ok;
    }
};
void CoreOwnershipRegression(const fs::path& root) {
    wchar_t executable[32768]{};
    Check(GetModuleFileNameW(nullptr, executable, 32768) != 0, "locate suspended self-check fixture");
    const auto install = root / L"install";
    const auto cache = root / L"native-taskbar-runtime" / L"2026.1" / L"x64" / L"LumeTaskbarCore.exe";
    const auto installed = install / L"taskbar-runtime" / L"x64" / L"LumeTaskbarCore.exe";
    for (const auto& path : {cache, installed}) {
        fs::create_directories(path.parent_path());
        fs::copy_file(executable, path, fs::copy_options::overwrite_existing);
    }
    const auto detail = [&] {
        for (const auto& check : shell::RunSelfChecks(install)) if (check.id == "taskbar") return check.detail;
        return std::string{};
    };
    {
        ProcessFixture own;
        Check(own.Start(cache), "start current owner's cached core fixture");
        Check(detail().find(cache.u8string()) == std::string::npos,
              "current owner's core in native-taskbar-runtime is not a conflicting installation");
    }
    ProcessFixture otherOwner;
    Check(otherOwner.Start(executable), "start other owner fixture");
    if (!otherOwner.process) return;
    for (const auto& path : {cache, installed}) {
        ProcessFixture foreign;
        Check(foreign.Start(path, otherOwner.process), "start other owner's core fixture");
        Check(detail().find(path.u8string()) != std::string::npos,
              "another owner's core conflicts even when it uses our cache or install path");
    }
}
void TaskbarEffectRegression() {
    const auto checkState = [](const nlohmann::json& state) {
        shell::SelfCheck check{"taskbar", shell::CheckStatus::Ok, "任务栏美化", "environment passed", {}};
        shell::ApplyTaskbarRuntimeState(check, {{"taskbar-state", state}});
        return check;
    };
    auto state = nlohmann::json{{"enabled", true}, {"running", true}, {"transparentTaskbar", true},
        {"autoHideTaskbar", false}, {"error", ""}, {"pendingRecovery", false}, {"applying", false}};
    auto check = checkState(state);
    Check(check.status == shell::CheckStatus::Unknown && check.detail.find("尚未确认") != std::string::npos,
          "a live core without background evidence never passes the transparency self-check");
    for (const auto* phase : {"inactive", "initializing", "pending"}) {
        state["effectStatus"] = phase;
        Check(checkState(state).status == shell::CheckStatus::Unknown,
              "unconfirmed effect phases remain unknown rather than reporting success");
    }
    state["effectStatus"] = "applied";
    check = checkState(state);
    Check(check.status == shell::CheckStatus::Ok && check.detail.find("透明效果已确认") != std::string::npos,
          "the effect acknowledgement permits a successful self-check");
    state["effectStatus"] = "failed";
    state["stage"] = "background-read";
    state["hresult"] = static_cast<std::int32_t>(E_ACCESSDENIED);
    Check(checkState(state).status == shell::CheckStatus::Problem, "a live service with failed background application is a real failure");
    Check(checkState(state).detail.find("background-read") != std::string::npos &&
          checkState(state).detail.find("0x80070005") != std::string::npos,
          "effect failures retain the observed API stage and HRESULT even without an aggregate error");
    state.erase("hresult"); state.erase("stage");
    state["autoHideTaskbar"] = true;
    state["hideApplied"] = true;
    state["effectiveAutoHide"] = true;
    state["error"] = "core-access-denied";
    check = checkState(state);
    Check(check.status == shell::CheckStatus::Problem && check.detail.find("隐藏已应用") != std::string::npos &&
          check.detail.find("0x80070005") != std::string::npos,
          "independently working hiding is reported beside the transparency failure");
    state["error"] = "core-restart-required";
    check = checkState(state);
    Check(check.detail.find("已停止自动重试") != std::string::npos && check.fix.find("重启 Windows") != std::string::npos,
          "module reload failures give the actual recovery instruction");
    state["error"] = "core-attach-timeout";
    Check(checkState(state).detail.find("0x80070102") != std::string::npos, "attach timeouts are not disguised as an unexpected crash");
    state["error"] = "";
    state["transparentTaskbar"] = false;
    state["hideApplied"] = false;
    Check(checkState(state).status == shell::CheckStatus::Unknown, "hiding preferences alone do not confirm hiding");
    state["hideApplied"] = true;
    state["fullyHidden"] = true;
    check = checkState(state);
    Check(check.status == shell::CheckStatus::Ok && check.detail.find("完全隐藏已应用") != std::string::npos,
          "hiding-only operation does not require a transparent core");
    state["enabled"] = false;
    state["running"] = true;
    state["autoHideTaskbar"] = false;
    state["autoHideDemand"] = true;
    state["fullyHidden"] = false;
    check = checkState(state);
    Check(check.status == shell::CheckStatus::Ok && check.detail.find("Dock") != std::string::npos,
          "Dock auto-hide demand is distinguished from the disabled taskbar preference");
    state["autoHideDemand"] = false;
    state["autoHideTaskbar"] = true;
    state["running"] = false;
    state["hideApplied"] = false;
    Check(checkState(state).status == shell::CheckStatus::Ok,
          "saved hide preferences do not demand hiding when the feature is disabled");
}
void ReportRetentionRegression(const fs::path& root,const shell::DiagnosticReportInput& source) {
    const auto folder=root/L"retention";fs::create_directories(folder);
    std::vector<fs::path> oldReports;
    for(unsigned i=1;i<=6;++i) {
        const auto file=folder/(L"Lume诊断报告-20200101-00000"+std::to_wstring(i)+L".zip");
        Write(file,"OLD-OWN-REPORT");oldReports.push_back(file);
    }
    const auto other=folder/L"personal.zip",lookalike=folder/L"Lume诊断报告-invalid.zip";
    Write(other,"KEEP-PERSONAL");Write(lookalike,"KEEP-LOOKALIKE");
    const auto unpacked=folder/L"Lume诊断报告-20190101-000000.zip";fs::create_directory(unpacked);
    Write(unpacked/L"keep.txt","KEEP-UNPACKED");
    const auto stale=folder/L"Lume诊断报告-20200101-000010.zip.partial";
    const auto recent=folder/L"Lume诊断报告-20200101-000011.zip.partial";
    const auto active=folder/L"Lume诊断报告-20200101-000012.zip.partial";
    const auto unrelatedPartial=folder/L"other.partial";
    for(const auto& path:{stale,recent,active,unrelatedPartial})Write(path,"PARTIAL-FIXTURE");
    for(const auto& path:{stale,active})fs::last_write_time(path,fs::file_time_type::clock::now()-std::chrono::hours(48));
    const auto activeHandle=CreateFileW(active.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    Check(activeHandle!=INVALID_HANDLE_VALUE,"lock an old partial to simulate an active writer");
    Write(folder/L"Lume诊断报告-export.partial","CRASHED-FIXED-PARTIAL");
    const auto sentinel=root/L"retention-link-target.zip";Write(sentinel,"KEEP-LINK-TARGET");
    const auto link=folder/L"Lume诊断报告-19990101-000000.zip";
    const bool fileLink=CreateSymbolicLinkW(link.c_str(),sentinel.c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)!=FALSE;
    auto input=source;input.outputDirectory=folder;
    const auto result=shell::WriteDiagnosticReport(input);
    if(activeHandle!=INVALID_HANDLE_VALUE)CloseHandle(activeHandle);
    Check(result.ok&&fs::is_regular_file(result.path),"retention keeps the newly generated report");
    Check(ReportTotals(folder).first==5,"reports are retained at a maximum of five own ZIP files");
    Check(!fs::exists(oldReports[0])&&!fs::exists(oldReports[1])&&fs::exists(oldReports[2]),"retention removes the oldest own report names first");
    Check(Read(other)=="KEEP-PERSONAL"&&Read(lookalike)=="KEEP-LOOKALIKE"&&Read(unpacked/L"keep.txt")=="KEEP-UNPACKED",
          "retention leaves unrelated files, similar names and unpacked directories intact");
    Check(!fs::exists(stale)&&fs::exists(recent)&&fs::exists(active)&&fs::exists(unrelatedPartial),
          "only expired inactive own partials are cleaned");
    Check(!fs::exists(folder/L"Lume诊断报告-export.partial"),"a successful export leaves no fixed partial");
    if(fileLink) {
        const auto attributes=GetFileAttributesW(link.c_str());
        Check(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&Read(sentinel)=="KEEP-LINK-TARGET",
              "retention neither follows nor deletes a report-name symlink");
    }
    else std::cout<<"report file symlink fixture unavailable on this host\n";

    const auto sizeFolder=root/L"retention-bytes";fs::create_directory(sizeFolder);
    std::vector<fs::path> large;
    for(unsigned i=1;i<=3;++i) {
        const auto file=sizeFolder/(L"Lume诊断报告-20200102-00000"+std::to_wstring(i)+L".zip");
        Write(file,"OWN-SIZE-FIXTURE");fs::resize_file(file,60ull<<20);large.push_back(file);
    }
    input.outputDirectory=sizeFolder;
    const auto sized=shell::WriteDiagnosticReport(input);const auto totals=ReportTotals(sizeFolder);
    Check(sized.ok&&fs::exists(sized.path)&&totals.first<=5&&totals.second<=(160ull<<20),
          "the total own-report bytes are bounded independently of the file-count limit");
    Check(!fs::exists(large[0])&&fs::exists(large[1]),"byte retention removes the oldest archive and preserves newer evidence");

    const auto target=root/L"retention-safe-target";fs::create_directories(target/L"nested");
    const auto redirect=root/L"retention-directory-link";
    if(CreateSymbolicLinkW(redirect.c_str(),target.c_str(),SYMBOLIC_LINK_FLAG_DIRECTORY|SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        input.outputDirectory=redirect/L"nested";
        const auto rejected=shell::WriteDiagnosticReport(input);
        Check(!rejected.ok&&rejected.error=="DIAGNOSTIC_REPORT_UNSAFE_OUTPUT_DIRECTORY"&&fs::is_empty(target/L"nested"),
              "report export does not traverse a reparse directory or its descendants");
    }else std::cout<<"report directory symlink fixture unavailable on this host\n";
}
void ReportEntryBoundsRegression(const fs::path& root,const shell::DiagnosticReportInput& source) {
    auto input=source;
    input.dataDirectory=root/L"many-empty-settings";input.logDirectory=root/L"no-history";
    input.outputDirectory=root/L"entry-bounds";
    for(const auto& folder:{input.dataDirectory,input.logDirectory,input.outputDirectory})fs::create_directories(folder);
    for(unsigned i=0;i<700;++i)Write(input.dataDirectory/(L"empty-"+std::to_wstring(i)+L".json"),{});
    const auto result=shell::WriteDiagnosticReport(input);const auto zip=result.ok?Read(result.path):std::string{};
    const auto readme=ZipPart(zip,"README.txt");
    Check(result.ok&&ZipEntries(zip)==256&&zip.size()<=(32ull<<20),
          "hundreds of empty settings cannot bypass the archive entry or exact ZIP byte limit");
    std::size_t listed=0;
    for(std::size_t at=0;(at=readme.find("(条目数量上限)",at))!=std::string::npos;at+=1)++listed;
    Check(listed==64&&readme.find("项未展开")!=std::string::npos,
          "the skipped-entry list is bounded and reports the omitted count");
    const auto summary=nlohmann::json::parse(ZipPart(zip,"wallpaper-resource-summary.json"),nullptr,false);
    Check(summary.is_object()&&summary.value("sampleCount",-1)==0,
          "an older installation without records exports missing history rather than fabricated samples");

    input.note.clear();for(unsigned i=0;i<50000;++i)input.note+="汉";
    input.outputDirectory=root/L"readme-bounds";fs::create_directory(input.outputDirectory);
    const auto longResult=shell::WriteDiagnosticReport(input);
    const auto longZip=longResult.ok?Read(longResult.path):std::string{};
    const auto bounded=ZipPart(longZip,"README.txt");
    Check(longResult.ok&&!bounded.empty()&&bounded.size()<=(64ull<<10)&&longZip.size()<=(32ull<<20)&&
          bounded.find("说明已按 64 KiB")!=std::string::npos,
          "README text has its own hard size bound inside the exact archive size limit");
    Check(!bounded.empty()&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bounded.data(),static_cast<int>(bounded.size()),nullptr,0)>0,
          "README truncation preserves a valid UTF-8 boundary");
}
}

int main() {
    TaskbarEffectRegression();
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto root = fs::path(temp) / (L"lume-diagnostic-report-test-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(root);
    const auto data = root / L"Data", logs = data / L"Diagnostics", out = root / L"out";
    fs::create_directories(logs / L"crashes");
    fs::create_directories(out);
    Write(data / L"dock-preferences.json", R"({"enabled":true})");
    Write(data / L"account-session.json", R"({"token":"SECRET-TOKEN"})");
    Write(data / L"weather-location.json", R"({"city":"PRIVATE-CITY"})");
    Write(logs / L"crashes" / L"LumeShell-1.dmp", "MDMP-fake");
    Write(data / L"huge-preferences.json", std::string(600 * 1024, 'x'));
    Write(logs / L"crashes" / L"LumeShell-0.dmp", std::string(17u << 20, 'd'));
    const auto frameLog = logs / L"launchpad-frames.log";
    Write(frameLog, std::string(1u << 20, 'r'));
    platform::windows::ui_stall::SetLogPath(frameLog.wstring());
    platform::windows::ui_stall::Report("launchpad-drag", platform::windows::ui_stall::Now(), 1000.0 / 420, {},
                                      "items=92 outcome=commit pointer_moves=100 TRACE-NEW-DATA");
    platform::windows::ui_stall::SetLogPath({});
    Check(fs::file_size(logs / L"launchpad-frames.1.log") == (1u << 20) && fs::file_size(frameLog) < 2048,
          "frame log rotates at 1 MiB and writes one bounded drag summary");
    Write(data / L"launchpad-frames.log", "TRACE-LEGACY-DUPLICATE");
    Write(data / L"legacy-extra.log", "TRACE-LEGACY-FALLBACK");
    WriteWallpaperHistory(logs);

    platform::windows::diag::Initialize(logs);
    Check(!fs::exists(logs/L"crashes"/L"LumeShell-0.dmp"),"runtime initialization prunes an oversized old dump");
    // Exercise the report's separate import cap with a legacy/external dump
    // arriving after initialization; startup pruning must not remove the fixture first.
    Write(logs/L"crashes"/L"LumeShell-0.dmp",std::string(17u<<20,'d'));
    Write(logs/L"material.log",std::string((1u<<20)+1,'m'));
    platform::windows::material_diagnostics::Write("WindowCapture",E_ACCESSDENIED,"sources=1 readySources=0");
    Check(Read(logs/L"material.log").find("hr=0x80070005")!=std::string::npos,
          "material failures are recorded automatically after runtime log initialization");
    Check(fs::file_size(logs/L"material.1.log")==((1u<<20)+1),"material diagnostics rotate after one MiB");
    for(unsigned i=0;i<100;++i)platform::windows::material_diagnostics::Write("WindowCapture",E_ACCESSDENIED);
    const auto bounded=Read(logs/L"material.log");
    Check(std::count(bounded.begin(),bounded.end(),'\n')<=48,"repeated material failures have a per-minute event budget");
    Write(logs/L"material.1.log","MATERIAL-ROTATED-EVIDENCE");
    platform::windows::diag::Log("test", "wallpaper player exited, code " + platform::windows::diag::ExitCodeText(0xC0000005));
    Check(Read(logs / L"lume.log").find("0xC0000005 (access violation)") != std::string::npos, "runtime log line and exit code text");

    shell::DiagnosticReportInput input;
    input.dataDirectory = data;
    input.logDirectory = logs;
    input.outputDirectory = out;
    input.installDirectory = root;
    input.version = "test";
    input.note = "TASKBAR-NOTE";
    input.modules = {{"taskbar-state", {{"type", "taskbar-state"}, {"error", "core-start"}}},
                     {"dock-state", {{"type", "dock-state"}, {"error", ""}, {"materialStatus", "ready"}}},
                     {"performance-state",{{"materialStatus","transparent-fallback"},{"error",""}}},
                     {"wallpaper-state",{{"catalogError","WALLPAPER_CATALOG_CACHED"},{"error",""}}}};
    auto deniedModules = input.modules;
    deniedModules["taskbar-state"] = {{"error", "core-access-denied"}, {"enabled", true}, {"running", false},
                                     {"applying", false}, {"pendingRecovery", true}};
    bool foundDenied = false;
    for (const auto& check : shell::RunSelfChecks(root, deniedModules)) if (check.id == "taskbar") {
        foundDenied = check.status == shell::CheckStatus::Problem && check.detail.find("0x80070005") != std::string::npos;
        Check(check.detail.find("已停止自动重试") != std::string::npos,
              "self-check exposes the blocked runtime rather than just checking for conflicts");
    }
    Check(foundDenied, "self-check includes the actual taskbar access failure");
    const auto environment = shell::DescribeSystem();
    for (const auto* process : {"lume", "explorer"}) {
        const auto& security = environment.at("shell").at("processSecurity").at(process);
        Check(security.contains("integrityRid") && security.contains("elevated") && security.contains("uiAccess") &&
              security.contains("session"), "read-only launch security metadata is present");
        Check(!security.contains("user") && !security.contains("sid") && !security.contains("token"),
              "launch diagnostics do not export identities or token contents");
    }
    const auto result = shell::WriteDiagnosticReport(input);
    Check(result.ok && fs::is_regular_file(result.path), "report written");
    const auto zip = result.ok ? Read(result.path) : std::string{};
    Check(zip.size() > 22 && zip.compare(0, 4, "PK\x03\x04") == 0 && zip.find("PK\x05\x06") != std::string::npos, "zip structure");
    for (const auto* name : {"system.json", "processes.json", "modules.json", "windows-events.txt", "logs/lume.log",
                             "logs/launchpad-frames.log", "logs/launchpad-frames.1.log", "logs/legacy-extra.log","logs/material.log","logs/material.1.log",
                             "settings/dock-preferences.json", "crashes/LumeShell-1.dmp", "wallpaper-resources.jsonl",
                             "wallpaper-resources.csv", "wallpaper-resource-summary.txt", "wallpaper-resource-summary.json"})
        Check(Has(zip, name), name);
    Check(Has(zip,"MATERIAL-ROTATED-EVIDENCE")&&Has(zip,"hr=0x80070005")&&Has(zip,"LumeGlass.dll"),
          "report contains material stages, error codes and installed DLL inventory");
    Check(Has(zip, "README.txt"), "summary entry");
    Check(Has(zip, "TASKBAR-NOTE") && Has(zip, "taskbar-state: error=core-start"), "summary has note and failing module");
    Check(!Has(zip, "dock-state: "), "healthy module not listed as a problem");
    Check(Has(zip,"performance-state: materialStatus=transparent-fallback")&&!Has(zip,"wallpaper-state: "),
          "summary exposes real material fallback without treating normal catalog cache as a fault");
    Check(!Has(zip, std::string(4096, 'x')) && Has(zip, "settings/huge-preferences.json (600 KB)"), "oversized settings file left out and named");
    Check(!Has(zip, std::string(4096, 'd')) && Has(zip, "crashes/LumeShell-0.dmp (17408 KB)") && zip.size() < (32u << 20), "oversized dump left out, report under the cap");
    Check(!Has(zip, "SECRET-TOKEN") && !Has(zip, "PRIVATE-CITY"), "account and location excluded");
    Check(Has(zip, "TRACE-NEW-DATA") && Has(zip, "TRACE-LEGACY-FALLBACK") && !Has(zip, "TRACE-LEGACY-DUPLICATE"),
          "report includes current and rotated drag data plus legacy fallback without duplicate names");
    Check(Has(zip, "启动台图标拖动卡顿的采集方法") && Has(zip, "同一格附近") && Has(zip, "slow_frame_gaps") &&
          Has(zip, "coverage=unavailable"), "report explains how to reproduce, collect and interpret drag diagnostics");
    const auto wallpaperSummary=nlohmann::json::parse(ZipPart(zip,"wallpaper-resource-summary.json"),nullptr,false);
    Check(wallpaperSummary.is_object()&&wallpaperSummary.value("sampleCount",0)==2,
          "report includes the bounded history summary from its isolated log directory");
    const auto wallpaperCsv=ZipPart(zip,"wallpaper-resources.csv"),wallpaperRecords=ZipPart(zip,"wallpaper-resources.jsonl");
    Check(wallpaperCsv.find("104857600")!=std::string::npos&&wallpaperCsv.find("209715200")!=std::string::npos&&
          wallpaperRecords.find("cpuMachinePercent")!=std::string::npos,
          "report exports both chronological resource samples and CSV values");
    Check(!Has(zip,"PRIVATE-WALLPAPER-URL")&&!Has(zip,"PRIVATE-WALLPAPER-TOKEN")&&!Has(zip,"PRIVATE-WALLPAPER-PATH"),
          "wallpaper history is redacted before any archive entry is written");
    Check(zip.find("wallpaper-resources.jsonl")<zip.find("logs/lume.log"),
          "resource history is added before runtime logs consume the report budget");
    Check(Has(zip,"每分钟")&&Has(zip,"72 小时")&&Has(zip,"8 MiB")&&Has(zip,"旧版本未记录")&&Has(zip,"最多 5 份")&&Has(zip,"160 MiB"),
          "report documents bounded history, absent old history and automatic report retention");

    Check(shell::ParseDiagnosticReportCommand(LR"({"version":1,"scope":"diagnostic-report","action":"create","note":"任务栏"})")->note == "\xE4\xBB\xBB\xE5\x8A\xA1\xE6\xA0\x8F", "create command");
    Check(shell::ParseDiagnosticReportCommand(LR"({"version":1,"scope":"diagnostic-report","action":"open-logs"})").has_value(), "open logs command");
    for (const auto* action : {L"copy", L"self-check"})
        Check(shell::ParseDiagnosticReportCommand(std::wstring(LR"({"version":1,"scope":"diagnostic-report","action":")") + action + L"\"}").has_value(), "copy and self-check commands");
    const auto text = shell::DiagnosticText(input);
    Check(text.find("Lume 诊断信息") != std::string::npos && text.find("启动自检") != std::string::npos &&
          text.find("日志位置") != std::string::npos && text.find("TASKBAR-NOTE") != std::string::npos, "copied text has version, self-check, log path and note");
    const auto checks = shell::RunSelfChecks(root);
    Check(checks.size() == 5, "five self-checks");
    for (const auto& check : checks)
        Check(check.status != shell::CheckStatus::Ok || check.id == "gpu-decode", "an empty install folder is never reported OK");
    Check(!shell::ParseDiagnosticReportCommand(LR"({"version":1,"scope":"diagnostic-report","action":"delete"})"), "unknown action");
    Check(!shell::ParseDiagnosticReportCommand(LR"({"version":1,"scope":"diagnostics","action":"create"})"), "other scope");

    CoreOwnershipRegression(root);
    ReportRetentionRegression(root,input);
    ReportEntryBoundsRegression(root,input);

    std::error_code ignored;
    if (GetEnvironmentVariableW(L"LUME_KEEP_REPORT", nullptr, 0)) std::cout << result.path.u8string() << '\n' << text << '\n';
    if (GetEnvironmentVariableW(L"LUME_SELF_CHECK_INSTALL", nullptr, 0)) {
        wchar_t install[MAX_PATH]{};
        GetEnvironmentVariableW(L"LUME_SELF_CHECK_INSTALL", install, MAX_PATH);
        for (const auto& check : shell::RunSelfChecks(install))
            std::cout << shell::CheckStatusName(check.status) << " " << check.id << ": " << check.detail << " | " << check.fix << '\n';
    }
    else fs::remove_all(root, ignored);
    if (!failures) std::cout << "diagnostic report passed\n";
    return failures ? 1 : 0;
}
