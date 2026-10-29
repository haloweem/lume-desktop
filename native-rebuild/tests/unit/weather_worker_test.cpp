#include "platform/windows/weather/weather_worker.h"
#include "weather_fixtures.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
using namespace lume::weather;
using namespace lume::platform::windows;
using namespace weather_fixture;
using namespace std::chrono_literals;
namespace {
void Expect(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
template<class Predicate> void Until(Predicate ready, const char* reason) {
    const auto end = std::chrono::steady_clock::now() + 3s;
    while (!ready() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
    Expect(ready(), reason);
}
Snapshot Sample(std::string_view city) {
    auto snapshot = *ParseMet(MetBody().dump(), Shanghai, "auto", Now, Now, Local);
    snapshot.cityId = std::string(city);
    return snapshot;
}
struct Probe {
    std::atomic<int> alive{0}, calls{0}, notices{0}, loads{0};
    std::atomic<bool> release{false}, block{true}, ignoreCancel{false}, cancelled{false}, fail{false};
    std::mutex mutex;
    std::vector<std::string> cities;
    std::optional<Snapshot> cached;
};
class Backend final : public IWeatherBackend {
public:
    explicit Backend(Probe& p) : p_(p) { ++p_.alive; }
    ~Backend() override { --p_.alive; }
    std::optional<Snapshot> Load(std::string_view city) override {
        ++p_.loads;
        return p_.cached && p_.cached->cityId == city ? p_.cached : std::nullopt;
    }
    WeatherFetch Fetch(std::string_view city, const Cancellation& cancel) override {
        {
            std::lock_guard lock(p_.mutex);
            p_.cities.emplace_back(city);
        }
        ++p_.calls;
        while (p_.block && !p_.release && (p_.ignoreCancel || !cancel.Cancelled()))
            std::this_thread::sleep_for(1ms);
        if (cancel.Cancelled()) p_.cancelled = true;
        // Deliberately return a result even after cancel: the UI mailbox must
        // independently reject the previous generation.
        if (p_.fail) return {{{}, ProviderError::Unavailable, 0}, false};
        return {{Sample(city), ProviderError::None, 0}, false};
    }
private:
    Probe& p_;
};
WeatherBackendFactory Factory(Probe& p) {
    return [&] { return std::make_unique<Backend>(p); };
}
void LifecycleAndMerge() {
    Probe p;
    WeatherWorker worker(Factory(p), [&] { ++p.notices; }, [] { return Now; });
    Expect(!worker.Running() && p.alive == 0 && p.calls == 0, "disabled weather creates no worker backend");
    Expect(!worker.Start("fictional") && p.alive == 0, "invalid city cannot start worker");
    Expect(worker.Start("shanghai"), "weather worker starts");
    Until([&] { return p.calls == 1; }, "initial fetch starts");
    for (int i = 0; i < 50; ++i) worker.Refresh();
    p.release = true;
    Until([&] { return !worker.Latest().loading; }, "first fetch reaches UI mailbox");
    Expect(p.calls == 1 && worker.Latest().snapshot && worker.Latest().snapshot->cityId == "shanghai",
        "concurrent refreshes coalesce into the active fetch");
    worker.Refresh();
    Until([&] { return p.calls == 2 && !worker.Latest().loading; }, "idle refresh performs one new fetch");
    worker.Stop();
    const auto notices = p.notices.load();
    Expect(!worker.Running() && !worker.Latest().snapshot && p.alive == 0, "stop releases backend and displayed data");
    worker.Refresh();
    Expect(p.calls == 2 && p.notices == notices, "disabled refresh cannot restart work");
    for (int i = 0; i < 25; ++i) {
        Expect(worker.Start("beijing"), "restart succeeds");
        Until([&] { return p.alive == 1; }, "restart owns one backend");
        worker.Stop();
        Expect(p.alive == 0, "each stop joins and releases backend");
    }
}
void SwitchAndCancel() {
    Probe p; p.ignoreCancel = true; p.cached = Sample("shanghai");
    WeatherWorker worker(Factory(p), [&] { ++p.notices; }, [] { return Now; });
    Expect(worker.Start("shanghai"), "start with display cache");
    Until([&] { return p.calls == 1; }, "fetch after cache");
    Expect(worker.Latest().snapshot.has_value() && worker.Latest().loading, "cached weather visible during first request");
    Expect(worker.SetCity("beijing") && !worker.Latest().snapshot, "changing city immediately clears mismatched display");
    p.release = true;
    Until([&] { return p.calls == 2 && !worker.Latest().loading; }, "new city processed after old request");
    Expect(p.cancelled && worker.Latest().snapshot && worker.Latest().snapshot->cityId == "beijing",
        "cancelled result never replaces current city");
    Expect(!worker.SetCity("fictional") && worker.Latest().snapshot->cityId == "beijing", "invalid setting preserves live city");
    worker.Stop();
    p.release = false; p.ignoreCancel = false; p.cancelled = false;
    Expect(worker.Start("auto"), "auto restart");
    Until([&] { return p.calls == 3; }, "auto fetch in progress");
    const auto start = std::chrono::steady_clock::now();
    worker.Stop();
    Expect(p.cancelled && p.alive == 0 && std::chrono::steady_clock::now() - start < 500ms,
        "stop cancels in-flight request and joins promptly");
}
void FailureCacheAndTimer() {
    Probe p; p.block = false; p.fail = true; p.cached = Sample("auto");
    WeatherWorker worker(Factory(p), [&] { ++p.notices; }, [] { return Now; }, 30ms);
    Expect(worker.Start("auto"), "start failed network with display cache");
    Until([&] { return worker.Latest().error == ProviderError::Unavailable; }, "failure exposed");
    Expect(worker.Latest().snapshot.has_value(), "failed refresh retains matching valid cache");
    p.fail = false;
    Until([&] { return p.calls >= 2 && worker.Latest().error == ProviderError::None &&
        !worker.Latest().loading; }, "timer recovers without manual restart");
    worker.Stop();
    p.cached->updatedAt = Now - Day - 1; p.fail = true;
    Expect(worker.Start("auto"), "restart with stale cache");
    Until([&] { return worker.Latest().error == ProviderError::Unavailable; }, "stale cache fetch failed");
    Expect(!worker.Latest().snapshot, "cache older than 24 hours is never displayed");
    worker.Stop();
    bool shouldThrow = true;
    WeatherWorker failed([&]() -> std::unique_ptr<IWeatherBackend> {
        if (shouldThrow) throw std::runtime_error("backend unavailable");
        return std::make_unique<Backend>(p);
    }, [&] { ++p.notices; }, [] { return Now; });
    Expect(failed.Start("auto"), "worker thread starts before backend initialization");
    Until([&] { return !failed.Running(); }, "backend exception exits worker");
    Expect(failed.Latest().error == ProviderError::Unavailable, "backend exception exposed");
    shouldThrow = false;
    Expect(failed.Start("auto"), "finished worker can be joined and restarted");
    Until([&] { return p.alive == 1; }, "recovery backend exists");
    failed.Stop();
    Expect(p.alive == 0, "recovery backend released");
}
}
int main() {
    LifecycleAndMerge(); SwitchAndCancel(); FailureCacheAndTimer();
    std::cout << "weather single-worker refresh, cache, switch, failure and cancellation passed\n";
}
