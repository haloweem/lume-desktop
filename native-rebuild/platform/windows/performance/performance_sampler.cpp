#include "platform/windows/performance/performance_sampler.h"
#include <winsock2.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <algorithm>

namespace lume::platform::windows {
namespace {
std::uint64_t Ticks(FILETIME value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
std::optional<performance::CpuCounters> Cpu() {
    // GetSystemTimes is restricted to the calling thread's primary group on
    // machines with >64 processors. Visit each group and restore affinity.
    GROUP_AFFINITY original{};
    if (!GetThreadGroupAffinity(GetCurrentThread(), &original)) return {};
    const WORD groups = GetActiveProcessorGroupCount();
    performance::CpuCounters result;
    for (WORD group = 0; group < groups; ++group) {
        GROUP_AFFINITY affinity{};
        affinity.Group = group;
        affinity.Mask = 1;
        if (groups > 1 && !SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr)) {
            SetThreadGroupAffinity(GetCurrentThread(), &original, nullptr);
            return {};
        }
        FILETIME idle{}, kernel{}, user{};
        const bool ok = GetSystemTimes(&idle, &kernel, &user) != FALSE;
        if (ok) {
            result.idle += Ticks(idle);
            result.total += Ticks(kernel) + Ticks(user);
            result.processors += GetActiveProcessorCount(group);
        }
        if (!ok) {
            if (groups > 1) SetThreadGroupAffinity(GetCurrentThread(), &original, nullptr);
            return {};
        }
    }
    if (groups > 1 && !SetThreadGroupAffinity(GetCurrentThread(), &original, nullptr)) return {};
    return result;
}
std::optional<std::vector<performance::NetworkCounters>> Network() {
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR) return {};
    struct Release { PMIB_IF_TABLE2 table; ~Release() { FreeMibTable(table); } } release{table};
    std::vector<performance::NetworkCounters> result;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const auto& adapter = table->Table[i];
        // Same physical-link filter as original native/performance-monitor.cc.
        if (adapter.OperStatus != IfOperStatusUp ||
            !adapter.InterfaceAndOperStatusFlags.HardwareInterface ||
            adapter.Type == IF_TYPE_SOFTWARE_LOOPBACK || adapter.Type == IF_TYPE_TUNNEL) continue;
        result.push_back({adapter.InterfaceLuid.Value, adapter.InOctets, adapter.OutOctets});
    }
    return result;
}
}
struct PerformanceSampler::State {
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    ULONGLONG retryAt = 0;
    bool warm = false;
    ~State() { Close(); }
    void Close() {
        if (query) PdhCloseQuery(query);
        query = nullptr;
        counter = nullptr;
        warm = false;
    }
    std::optional<std::vector<performance::GpuEngine>> Gpu() {
        const auto now = GetTickCount64();
        if (!query) {
            if (now < retryAt) return {};
            retryAt = now + 30000;
            if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS ||
                PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) {
                Close(); return {};
            }
        }
        if (PdhCollectQueryData(query) != ERROR_SUCCESS) {
            retryAt = now + 30000;
            Close(); return {};
        }
        if (!warm) { warm = true; return {}; }
        DWORD bytes = 0, count = 0;
        constexpr DWORD format = PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
        auto status = PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, nullptr);
        if (status == ERROR_SUCCESS && count == 0) return std::vector<performance::GpuEngine>{};
        if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || bytes == 0 || bytes > 16 * 1024 * 1024) return {};
        std::vector<BYTE> buffer(bytes);
        auto* values = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, values) != ERROR_SUCCESS) return {};
        std::vector<performance::GpuEngine> result;
        result.reserve(count);
        for (DWORD i = 0; i < count; ++i) {
            const auto& item = values[i];
            if (!item.szName || (item.FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
                item.FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)) continue;
            result.push_back({item.szName, item.FmtValue.doubleValue});
        }
        return result;
    }
};
PerformanceSampler::PerformanceSampler() : state_(std::make_unique<State>()) {}
PerformanceSampler::~PerformanceSampler() = default;
performance::RawSample PerformanceSampler::Read() {
    performance::RawSample result;
    result.monotonicMillis = GetTickCount64();
    result.cpu = Cpu();
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        result.totalMemory = memory.ullTotalPhys;
        result.freeMemory = memory.ullAvailPhys;
    }
    result.interfaces = Network();
    result.engines = state_->Gpu();
    return result;
}
}
