#include "platform/windows/webview_process_memory.h"

#include <psapi.h>
#include <tlhelp32.h>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <vector>

namespace lume::platform::windows {
namespace {
struct ProcessRecord {
    DWORD processId = 0;
    DWORD parentProcessId = 0;
};

bool IsWebViewProcess(HANDLE process) noexcept {
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length) || !length) return false;
    path.resize(length);
    const auto separator = path.find_last_of(L"\\/");
    const auto name = path.substr(separator == std::wstring::npos ? 0 : separator + 1);
    return _wcsicmp(name.c_str(), L"msedgewebview2.exe") == 0;
}
}

void TrimWebViewProcessWorkingSets(DWORD rootProcessId) noexcept {
    if (!rootProcessId) return;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    std::vector<ProcessRecord> records;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            records.push_back({entry.th32ProcessID, entry.th32ParentProcessID});
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::unordered_map<DWORD, std::vector<DWORD>> children;
    for (const auto& record : records) children[record.parentProcessId].push_back(record.processId);

    std::vector<DWORD> pending{rootProcessId};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const auto found = children.find(pending[index]);
        if (found == children.end()) continue;
        for (const auto processId : found->second) {
            pending.push_back(processId);
            const auto process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION |
                PROCESS_SET_QUOTA, FALSE, processId);
            if (!process || !IsWebViewProcess(process)) {
                if (process) CloseHandle(process);
                continue;
            }
            EmptyWorkingSet(process);
            CloseHandle(process);
        }
    }
}
}  // namespace lume::platform::windows
