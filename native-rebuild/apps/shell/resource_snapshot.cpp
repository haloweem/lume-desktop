#include "apps/shell/resource_snapshot.h"
#include "platform/windows/text_util.h"

#include "vendor/nlohmann/json.hpp"
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <queue>
#include <unordered_set>

namespace lume::shell::resources {
namespace {
using Json = nlohmann::json;

std::wstring Lower(std::wstring_view value) {
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return result;
}

bool Contains(std::wstring_view value, std::wstring_view needle) {
    return value.find(needle) != std::wstring_view::npos;
}

using lume::text::Utf8;

using lume::text::Wide;

void Add(ResourceGroup& group, const ProcessSample& process) {
    group.processes.push_back(process);
    ++group.processCount;
}

template <typename Value, typename Getter>
void SumOptional(const std::vector<ProcessSample>& processes,
                 std::optional<std::uint64_t>& output, Getter getter) {
    if (processes.empty()) {
        output.reset();
        return;
    }
    std::uint64_t sum = 0;
    for (const auto& process : processes) {
        const auto value = getter(process);
        if (!value) {
            output.reset();
            return;
        }
        if (*value > UINT64_MAX - sum) {
            output.reset();
            return;
        }
        sum += *value;
    }
    output = sum;
}

void Finalize(ResourceGroup& group) {
    if (group.processes.empty()) {
        group.coverage = Coverage::Unavailable;
        group.workingSetBytes.reset();
        group.privateBytes.reset();
        group.gpuDedicatedBytes.reset();
        group.gpuSharedBytes.reset();
        return;
    }
    SumOptional<std::uint64_t>(group.processes, group.workingSetBytes,
                               [](const ProcessSample& process) { return process.workingSetBytes; });
    SumOptional<std::uint64_t>(group.processes, group.privateBytes,
                               [](const ProcessSample& process) { return process.privateBytes; });
    SumOptional<std::uint64_t>(group.processes, group.gpuDedicatedBytes,
                               [](const ProcessSample& process) { return process.gpuDedicatedBytes; });
    SumOptional<std::uint64_t>(group.processes, group.gpuSharedBytes,
                               [](const ProcessSample& process) { return process.gpuSharedBytes; });
    group.coverage = group.workingSetBytes ? Coverage::Complete : Coverage::Partial;
}

ResourceGroup& GroupForRole(ResourceSnapshot& snapshot, ResourceRole role) {
    switch (role) {
        case ResourceRole::Shell: return snapshot.shell;
        case ResourceRole::WebViewBrowser:
        case ResourceRole::WebViewRenderer:
        case ResourceRole::WebViewGpu:
        case ResourceRole::WebViewUtility:
        case ResourceRole::WebViewCrashpad: return snapshot.webview;
        case ResourceRole::MusicHost: return snapshot.music;
        case ResourceRole::Wallpaper: return snapshot.wallpaper;
        case ResourceRole::Other: return snapshot.nonWallpaper;
    }
    return snapshot.nonWallpaper;
}

Json OptionalNumber(const std::optional<std::uint64_t>& value) {
    return value ? Json(*value) : Json(nullptr);
}

Json ProcessJson(const ProcessSample& process) {
    return Json{
        {"pid", process.pid},
        {"parentPid", process.parentPid},
        {"name", Utf8(process.name)},
        {"role", Utf8(ResourceRoleName(process.role))},
        {"workingSetBytes", OptionalNumber(process.workingSetBytes)},
        {"privateBytes", OptionalNumber(process.privateBytes)},
        {"gpuDedicatedBytes", OptionalNumber(process.gpuDedicatedBytes)},
        {"gpuSharedBytes", OptionalNumber(process.gpuSharedBytes)},
    };
}

Json GroupJson(const ResourceGroup& group) {
    Json processes = Json::array();
    for (const auto& process : group.processes) processes.push_back(ProcessJson(process));
    return Json{
        {"workingSetBytes", OptionalNumber(group.workingSetBytes)},
        {"privateBytes", OptionalNumber(group.privateBytes)},
        {"gpuDedicatedBytes", OptionalNumber(group.gpuDedicatedBytes)},
        {"gpuSharedBytes", OptionalNumber(group.gpuSharedBytes)},
        {"processCount", group.processCount},
        {"coverage", Utf8(CoverageName(group.coverage))},
        {"processes", std::move(processes)},
    };
}

struct ProcessMeta {
    DWORD pid = 0;
    DWORD parentPid = 0;
    std::wstring name;
};

std::vector<ProcessMeta> ProcessList() {
    std::vector<ProcessMeta> result;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            result.push_back(ProcessMeta{entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile});
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

std::optional<std::uint64_t> ProcessCounter(DWORD pid, bool privateBytes) {
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return std::nullopt;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    const bool complete = GetProcessMemoryInfo(
        process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != FALSE;
    CloseHandle(process);
    if (!complete) return std::nullopt;
    return privateBytes ? std::optional<std::uint64_t>(counters.PrivateUsage)
                         : std::optional<std::uint64_t>(counters.WorkingSetSize);
}
}

ResourceRole ClassifyProcess(std::wstring_view name, std::wstring_view commandLine) {
    const auto executable = Lower(name);
    const auto command = Lower(commandLine);
    if (executable == L"lumeshell.exe" || executable == L"lume.exe") return ResourceRole::Shell;
    if (executable == L"lumemusichost.exe") return ResourceRole::MusicHost;
    if (executable == L"mpv.exe") return ResourceRole::Wallpaper;
    if (executable != L"msedgewebview2.exe") return ResourceRole::Other;
    if (Contains(command, L"--type=gpu-process")) return ResourceRole::WebViewGpu;
    if (Contains(command, L"--type=renderer")) return ResourceRole::WebViewRenderer;
    if (Contains(command, L"--type=utility")) return ResourceRole::WebViewUtility;
    if (Contains(command, L"--type=crashpad-handler")) return ResourceRole::WebViewCrashpad;
    return ResourceRole::WebViewBrowser;
}

ResourceSnapshot Aggregate(const std::vector<ProcessSample>& samples) {
    ResourceSnapshot snapshot;
    GetSystemTimeAsFileTime(&snapshot.sampledAt);
    for (const auto& original : samples) {
        auto process = original;
        process.role = ClassifyProcess(process.name, process.commandLine);
        snapshot.processes.push_back(process);
        Add(GroupForRole(snapshot, process.role), process);
        if (process.role != ResourceRole::Wallpaper) Add(snapshot.nonWallpaper, process);
        if (process.role == ResourceRole::WebViewGpu || process.gpuDedicatedBytes ||
            process.gpuSharedBytes)
            Add(snapshot.gpu, process);
    }
    Finalize(snapshot.shell);
    Finalize(snapshot.webview);
    Finalize(snapshot.music);
    Finalize(snapshot.gpu);
    Finalize(snapshot.wallpaper);
    Finalize(snapshot.nonWallpaper);
    return snapshot;
}

ResourceSnapshot SampleProcessTree(DWORD rootPid) {
    const auto list = ProcessList();
    std::unordered_set<DWORD> selected{rootPid};
    std::queue<DWORD> pending;
    pending.push(rootPid);
    while (!pending.empty()) {
        const auto parent = pending.front();
        pending.pop();
        for (const auto& process : list) {
            if (process.parentPid == parent && selected.insert(process.pid).second)
                pending.push(process.pid);
        }
    }
    std::vector<ProcessSample> samples;
    for (const auto& process : list) {
        if (selected.find(process.pid) == selected.end()) continue;
        ProcessSample sample;
        sample.pid = process.pid;
        sample.parentPid = process.parentPid;
        sample.name = process.name;
        sample.role = ClassifyProcess(sample.name, sample.commandLine);
        sample.workingSetBytes = ProcessCounter(sample.pid, false);
        sample.privateBytes = ProcessCounter(sample.pid, true);
        samples.push_back(std::move(sample));
    }
    return Aggregate(samples);
}

const wchar_t* ResourceRoleName(ResourceRole role) noexcept {
    switch (role) {
        case ResourceRole::Shell: return L"Shell";
        case ResourceRole::WebViewBrowser: return L"WebViewBrowser";
        case ResourceRole::WebViewRenderer: return L"WebViewRenderer";
        case ResourceRole::WebViewGpu: return L"WebViewGpu";
        case ResourceRole::WebViewUtility: return L"WebViewUtility";
        case ResourceRole::WebViewCrashpad: return L"WebViewCrashpad";
        case ResourceRole::MusicHost: return L"MusicHost";
        case ResourceRole::Wallpaper: return L"Wallpaper";
        case ResourceRole::Other: return L"Other";
    }
    return L"Other";
}

const wchar_t* CoverageName(Coverage coverage) noexcept {
    switch (coverage) {
        case Coverage::Unavailable: return L"unavailable";
        case Coverage::Partial: return L"partial";
        case Coverage::Complete: return L"complete";
    }
    return L"unavailable";
}

std::wstring EncodeResourceSnapshot(const ResourceSnapshot& snapshot) {
    Json processes = Json::array();
    for (const auto& process : snapshot.processes) processes.push_back(ProcessJson(process));
    const Json value{
        {"version", 1},
        {"type", "resource-snapshot"},
        {"schemaVersion", snapshot.schemaVersion},
        {"sampledAt", {
            {"low", snapshot.sampledAt.dwLowDateTime},
            {"high", snapshot.sampledAt.dwHighDateTime},
        }},
        {"shell", GroupJson(snapshot.shell)},
        {"webview", GroupJson(snapshot.webview)},
        {"music", GroupJson(snapshot.music)},
        {"gpu", GroupJson(snapshot.gpu)},
        {"wallpaper", GroupJson(snapshot.wallpaper)},
        {"nonWallpaper", GroupJson(snapshot.nonWallpaper)},
        {"processes", std::move(processes)},
    };
    return Wide(value.dump());
}

}  // namespace lume::shell::resources
