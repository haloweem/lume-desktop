#include "platform/windows/wallpaper_resource_sampler.h"

#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace lume::platform::windows {
namespace {
using Json = nlohmann::json;
using IdentityKey = std::pair<DWORD, std::uint64_t>;
constexpr std::uint64_t UnixEpochTicks = 116444736000000000ull;
constexpr DWORD MaximumCounterBytes = 16 * 1024 * 1024;
constexpr ULONGLONG RetryMillis = 60000;

std::uint64_t Ticks(FILETIME value) {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
bool Take(std::wstring_view& input, std::wstring_view prefix) {
    if (input.substr(0, prefix.size()) != prefix) return false;
    input.remove_prefix(prefix.size());
    return true;
}
std::optional<std::uint64_t> Number(std::wstring_view& input, unsigned base) {
    std::uint64_t value = 0;
    std::size_t used = 0;
    for (const wchar_t ch : input) {
        unsigned digit = 16;
        if (ch >= L'0' && ch <= L'9') digit = static_cast<unsigned>(ch - L'0');
        else if (ch >= L'a' && ch <= L'f') digit = static_cast<unsigned>(ch - L'a') + 10;
        else if (ch >= L'A' && ch <= L'F') digit = static_cast<unsigned>(ch - L'A') + 10;
        if (digit >= base) break;
        if (value > (UINT64_MAX - digit) / base) return {};
        value = value * base + digit;
        ++used;
    }
    if (!used) return {};
    input.remove_prefix(used);
    return value;
}
std::wstring Hex(std::uint64_t number) {
    wchar_t value[17]{};
    swprintf_s(value, L"%llx", static_cast<unsigned long long>(number));
    return value;
}
using EngineTotals = std::map<std::wstring, double>;
EngineTotals Engines(const std::vector<wallpaper_resource_detail::GpuEngineValue>& values,
                     const std::set<DWORD>& wanted) {
    // Duplicate aliases of the same PID/hardware engine must not count twice.
    std::map<std::pair<DWORD, std::wstring>, double> unique;
    for (const auto& item : values) {
        if (!std::isfinite(item.percent) || item.percent < 0) continue;
        const auto instance = wallpaper_resource_detail::ParseGpuInstance(item.name);
        if (!instance || instance->engine.empty() || !wanted.count(instance->pid)) continue;
        auto& percent = unique[{instance->pid, instance->engine}];
        percent = std::max(percent, std::min(100.0, item.percent));
    }
    EngineTotals totals;
    for (const auto& item : unique) {
        auto& total = totals[item.first.second];
        total = std::min(100.0, total + item.second);
    }
    return totals;
}
std::optional<double> Busiest(const EngineTotals& values) {
    if (values.empty()) return {};
    double result = 0;
    for (const auto& item : values) result = std::max(result, item.second);
    return result;
}
class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { if (value_) CloseHandle(value_); }
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value_) CloseHandle(value_);
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE Get() const { return value_; }
private:
    HANDLE value_;
};
bool LiveIdentity(HANDLE process, const WallpaperProcessIdentity& identity,
                  std::uint64_t* cpuTicks = nullptr) {
    if (!process || WaitForSingleObject(process, 0) != WAIT_TIMEOUT) return false;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user) ||
        !identity.createdFileTime || Ticks(created) != identity.createdFileTime) return false;
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT) return false;
    if (cpuTicks) *cpuTicks = Ticks(kernel) + Ticks(user);
    return true;
}
Json ProcessRow(const WallpaperProcessIdentity& identity) {
    return {{"pid", identity.pid}, {"createdFileTime", std::to_string(identity.createdFileTime)},
            {"processStatus", "unavailable"}, {"cpuMachinePercent", nullptr},
            {"cpuIntervalMs", nullptr}, {"privateBytes", nullptr}, {"workingSetBytes", nullptr},
            {"gpuDedicatedBytes", nullptr}, {"gpuSharedBytes", nullptr},
            {"gpuEnginePercent", nullptr}, {"gpuStatus", "unavailable"},
            {"gpuEngines", Json::array()}};
}
struct CounterValue {
    std::wstring name;
    bool valid = false;
    double percent = 0;
    std::uint64_t bytes = 0;
};
struct CounterRead {
    bool available = false;
    PDH_STATUS status = ERROR_SUCCESS;
    std::vector<CounterValue> values;
};
CounterRead ReadCounter(PDH_HCOUNTER counter, bool memory) {
    CounterRead result;
    if (!counter) { result.status = PDH_INVALID_HANDLE; return result; }
    const DWORD format = memory ? PDH_FMT_LARGE : PDH_FMT_DOUBLE | PDH_FMT_NOCAP100;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        DWORD bytes = 0, count = 0;
        result.status = PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, nullptr);
        if (result.status == ERROR_SUCCESS && !count) { result.available = true; return result; }
        if (result.status != static_cast<PDH_STATUS>(PDH_MORE_DATA) || !bytes ||
            bytes > MaximumCounterBytes) return result;
        std::vector<BYTE> buffer(bytes);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        result.status = PdhGetFormattedCounterArrayW(counter, format, &bytes, &count, items);
        if (result.status == static_cast<PDH_STATUS>(PDH_MORE_DATA)) continue;
        if (result.status != ERROR_SUCCESS ||
            count > buffer.size() / sizeof(PDH_FMT_COUNTERVALUE_ITEM_W)) return result;
        result.values.reserve(count);
        for (DWORD index = 0; index < count; ++index) {
            const auto& item = items[index];
            if (!item.szName) continue;
            CounterValue value;
            value.name = item.szName;
            value.valid = item.FmtValue.CStatus == PDH_CSTATUS_VALID_DATA ||
                          item.FmtValue.CStatus == PDH_CSTATUS_NEW_DATA;
            if (memory) {
                value.valid = value.valid && item.FmtValue.largeValue >= 0;
                if (value.valid) value.bytes = static_cast<std::uint64_t>(item.FmtValue.largeValue);
            } else {
                value.percent = item.FmtValue.doubleValue;
                value.valid = value.valid && std::isfinite(value.percent) && value.percent >= 0;
            }
            result.values.push_back(std::move(value));
        }
        result.available = true;
        return result;
    }
    return result;
}
std::optional<std::uint64_t> MemoryFor(const CounterRead& counter, DWORD pid) {
    if (!counter.available) return {};
    std::map<std::wstring, std::uint64_t> adapters;
    for (const auto& item : counter.values) {
        const auto instance = wallpaper_resource_detail::ParseGpuInstance(item.name);
        if (!instance || instance->pid != pid || !instance->engine.empty()) continue;
        if (!item.valid) return {};
        auto& value = adapters[instance->adapter];
        value = std::max(value, item.bytes);
    }
    if (adapters.empty()) return {};
    std::uint64_t bytes = 0;
    for (const auto& item : adapters) {
        if (item.second > UINT64_MAX - bytes) return {};
        bytes += item.second;
    }
    return bytes;
}
std::string NarrowEngine(std::wstring_view engine) {
    // Instance identity and engine labels use ASCII. Avoid exporting arbitrary
    // provider strings or unbounded labels into the long-lived history file.
    std::string value;
    for (const wchar_t ch : engine.substr(0, 192))
        value += ch >= 32 && ch <= 126 ? static_cast<char>(ch) : '?';
    return value;
}
}

namespace wallpaper_resource_detail {
std::optional<GpuInstance> ParseGpuInstance(std::wstring_view input) {
    if (input.size() > 512 || !Take(input, L"pid_")) return {};
    const auto pid = Number(input, 10);
    if (!pid || !*pid || *pid > MAXDWORD || !Take(input, L"_luid_0x")) return {};
    const auto high = Number(input, 16);
    if (!high || *high > MAXDWORD || !Take(input, L"_0x")) return {};
    const auto low = Number(input, 16);
    if (!low || *low > MAXDWORD || !Take(input, L"_phys_")) return {};
    const auto physical = Number(input, 10);
    if (!physical || *physical > MAXDWORD) return {};
    GpuInstance result;
    result.pid = static_cast<DWORD>(*pid);
    result.adapter = L"luid_0x" + Hex(*high) + L"_0x" + Hex(*low) +
                     L"_phys_" + std::to_wstring(*physical);
    if (Take(input, L"_eng_")) {
        const auto engine = Number(input, 10);
        if (!engine || *engine > MAXDWORD || !Take(input, L"_engtype_")) return {};
        result.engine = result.adapter + L"_eng_" + std::to_wstring(*engine);
        const auto suffix = input.find(L'#');
        const auto type = input.substr(0, suffix);
        if (type.empty() || type.size() > 64) return {};
        for (const wchar_t ch : type)
            if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
                  (ch >= L'0' && ch <= L'9') || ch == L'_')) return {};
        result.type = type;
        input.remove_prefix(type.size());
    }
    if (Take(input, L"#")) {
        if (!Number(input, 10)) return {};
    }
    if (!input.empty()) return {};
    return result;
}
std::optional<double> BusiestGpuEngine(const std::vector<GpuEngineValue>& values,
                                     const std::vector<DWORD>& pids) {
    return Busiest(Engines(values, {pids.begin(), pids.end()}));
}
std::optional<double> CpuMachinePercent(std::uint64_t previousTicks,
                                       std::uint64_t currentTicks,
                                       std::uint64_t intervalMs, DWORD processors) {
    if (!intervalMs || !processors || currentTicks < previousTicks) return {};
    const double percent = static_cast<double>(currentTicks - previousTicks) * 100.0 /
                           (static_cast<double>(intervalMs) * 10000.0 * processors);
    if (!std::isfinite(percent)) return {};
    return std::min(100.0, percent);
}
}

struct WallpaperResourceSampler::State {
    struct Baseline { std::uint64_t ticks = 0, at = 0; DWORD processors = 0; };
    std::map<IdentityKey, Baseline> previous;
    std::set<IdentityKey> previousGpu;
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER engine = nullptr, dedicated = nullptr, shared = nullptr;
    ULONGLONG retryAt = 0;
    bool warm = false;
    Json counterErrors = Json::object();
    ~State() { CloseGpu(); }
    void CloseGpu() {
        if (query) PdhCloseQuery(query);
        query = nullptr; engine = dedicated = shared = nullptr;
        warm = false; previousGpu.clear();
        counterErrors.clear();
    }
    std::string CollectGpu(ULONGLONG now, CounterRead& engines,
                           CounterRead& dedicatedRead, CounterRead& sharedRead,
                           Json& errors) {
        if (!query) {
            if (now < retryAt) { errors["retryAtUptimeMs"] = retryAt; return "unavailable"; }
            const auto opened = PdhOpenQueryW(nullptr, 0, &query);
            if (opened != ERROR_SUCCESS) {
                errors["open"] = static_cast<std::int64_t>(opened);
                CloseGpu(); retryAt = now + RetryMillis; return "unavailable";
            }
            const auto add = [&](const wchar_t* path, PDH_HCOUNTER& counter, const char* key) {
                const auto status = PdhAddEnglishCounterW(query, path, 0, &counter);
                if (status != ERROR_SUCCESS) {
                    counter = nullptr;
                    counterErrors[key] = static_cast<std::int64_t>(status);
                }
                return status == ERROR_SUCCESS;
            };
            const bool okEngine = add(L"\\GPU Engine(*)\\Utilization Percentage", engine, "engineAdd");
            const bool okDedicated = add(L"\\GPU Process Memory(*)\\Dedicated Usage", dedicated, "dedicatedAdd");
            const bool okShared = add(L"\\GPU Process Memory(*)\\Shared Usage", shared, "sharedAdd");
            errors.update(counterErrors);
            if (!okEngine && !okDedicated && !okShared) {
                CloseGpu(); retryAt = now + RetryMillis; return "unavailable";
            }
        }
        errors.update(counterErrors);
        const auto collected = PdhCollectQueryData(query);
        if (collected != ERROR_SUCCESS) {
            errors["collect"] = static_cast<std::int64_t>(collected);
            CloseGpu(); retryAt = now + RetryMillis; return "unavailable";
        }
        if (!warm) { warm = true; return "warming"; }
        engines = ReadCounter(engine, false);
        dedicatedRead = ReadCounter(dedicated, true);
        sharedRead = ReadCounter(shared, true);
        if (!engines.available) errors["engineRead"] = static_cast<std::int64_t>(engines.status);
        if (!dedicatedRead.available) errors["dedicatedRead"] = static_cast<std::int64_t>(dedicatedRead.status);
        if (!sharedRead.available) errors["sharedRead"] = static_cast<std::int64_t>(sharedRead.status);
        if (!engines.available && !dedicatedRead.available && !sharedRead.available) {
            CloseGpu(); retryAt = now + RetryMillis; return "unavailable";
        }
        return "available";
    }
};

WallpaperResourceSampler::WallpaperResourceSampler() : state_(std::make_unique<State>()) {}
WallpaperResourceSampler::~WallpaperResourceSampler() = default;
Json WallpaperResourceSampler::Read(const std::vector<WallpaperProcessIdentity>& identities) {
    FILETIME time{}; GetSystemTimeAsFileTime(&time);
    const auto wallTicks = Ticks(time);
    const auto now = GetTickCount64();
    const DWORD processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    Json result{{"atUnixMs", wallTicks >= UnixEpochTicks ? (wallTicks - UnixEpochTicks) / 10000 : 0},
                {"uptimeMs", now}, {"logicalProcessors", processors},
                {"gpuStatus", "unavailable"}, {"wallpaperGpuEnginePercent", nullptr},
                {"processes", Json::array()}};
    struct Observation {
        WallpaperProcessIdentity identity;
        Handle handle;
        Json row;
        bool valid = false;
        std::uint64_t ticks = 0, at = 0;
    };
    std::vector<Observation> observed;
    std::set<IdentityKey> seen;
    bool anyValid = false;
    for (const auto& identity : identities) {
        const IdentityKey key{identity.pid, identity.createdFileTime};
        if (!seen.insert(key).second) continue;
        Observation item{identity, Handle{}, ProcessRow(identity)};
        if (identity.pid && identity.createdFileTime) {
            item.handle = Handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE,
                                             FALSE, identity.pid));
            if (item.handle.Get()) {
                item.valid = LiveIdentity(item.handle.Get(), identity, &item.ticks);
                item.row["processStatus"] = item.valid ? "available" : "identity-mismatch-or-exited";
            }
        } else item.row["processStatus"] = "invalid-identity";
        if (item.valid) {
            item.at = GetTickCount64();
            PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
            if (K32GetProcessMemoryInfo(item.handle.Get(),
                    reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {
                item.row["privateBytes"] = memory.PrivateUsage;
                item.row["workingSetBytes"] = memory.WorkingSetSize;
            }
            anyValid = true;
        }
        observed.push_back(std::move(item));
    }
    CounterRead engines, dedicated, shared;
    Json errors = Json::object();
    std::string gpu = "unavailable";
    if (anyValid) gpu = state_->CollectGpu(now, engines, dedicated, shared, errors);
    else { state_->CloseGpu(); state_->retryAt = 0; }
    if (!errors.empty()) result["gpuErrors"] = std::move(errors);
    std::vector<wallpaper_resource_detail::GpuEngineValue> engineValues;
    for (const auto& item : engines.values)
        if (item.valid) engineValues.push_back({item.name, item.percent});
    std::map<IdentityKey, State::Baseline> next;
    std::set<IdentityKey> nextGpu;
    std::set<DWORD> validPids;
    std::string overall = "available";
    for (auto& item : observed) {
        const auto& identity = item.identity;
        const IdentityKey key{identity.pid, identity.createdFileTime};
        if (item.valid && !LiveIdentity(item.handle.Get(), identity)) {
            item.valid = false;
            item.row = ProcessRow(identity);
            item.row["processStatus"] = "exited-during-sample";
        }
        if (item.valid) {
            const auto old = state_->previous.find(key);
            if (old != state_->previous.end() && old->second.processors == processors &&
                item.at > old->second.at) {
                const auto elapsed = item.at - old->second.at;
                const auto cpu = wallpaper_resource_detail::CpuMachinePercent(old->second.ticks,
                    item.ticks, elapsed, processors);
                if (cpu) { item.row["cpuMachinePercent"] = *cpu; item.row["cpuIntervalMs"] = elapsed; }
            }
            next[key] = {item.ticks, item.at, processors};
            validPids.insert(identity.pid);
            const bool warmedIdentity = state_->previousGpu.count(key) != 0;
            if (gpu == "available" && !warmedIdentity) item.row["gpuStatus"] = "warming";
            else item.row["gpuStatus"] = gpu;
            if (gpu == "available" && warmedIdentity) {
                if (const auto bytes = MemoryFor(dedicated, identity.pid)) item.row["gpuDedicatedBytes"] = *bytes;
                if (const auto bytes = MemoryFor(shared, identity.pid)) item.row["gpuSharedBytes"] = *bytes;
                const auto own = Engines(engineValues, {identity.pid});
                const auto busy = Busiest(own);
                bool invalidEngine = false;
                for (const auto& value : engines.values) {
                    const auto parsed = wallpaper_resource_detail::ParseGpuInstance(value.name);
                    if (parsed && parsed->pid == identity.pid && !parsed->engine.empty() && !value.valid)
                        invalidEngine = true;
                }
                if (!engines.available || invalidEngine) item.row["gpuStatus"] = "unavailable";
                else if (!busy) item.row["gpuStatus"] = "no-matching";
                else {
                    item.row["gpuEnginePercent"] = *busy;
                    for (const auto& entry : own) {
                        if (item.row["gpuEngines"].size() == 8) break;
                        item.row["gpuEngines"].push_back({{"engine", NarrowEngine(entry.first)}, {"percent", entry.second}});
                    }
                }
            }
            if (state_->query) nextGpu.insert(key);
        }
        const auto status = item.row["gpuStatus"].get<std::string>();
        if (status == "unavailable") overall = "unavailable";
        else if (overall != "unavailable" && status == "warming") overall = "warming";
        else if (overall == "available" && status == "no-matching") overall = "no-matching";
        result["processes"].push_back(std::move(item.row));
    }
    if (observed.empty()) overall = "unavailable";
    result["gpuStatus"] = overall;
    if (overall == "available") {
        if (const auto busy = Busiest(Engines(engineValues, validPids))) result["wallpaperGpuEnginePercent"] = *busy;
    }
    state_->previous = std::move(next);
    state_->previousGpu = std::move(nextGpu);
    if (state_->previous.empty()) { state_->CloseGpu(); state_->retryAt = 0; }
    return result;
}
}  // namespace lume::platform::windows
