#include "apps/shell/wallpaper_resource_monitor.h"
#include "apps/shell/wallpaper_resource_history.h"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

namespace {
using Json = nlohmann::json;
namespace fs = std::filesystem;
int failures = 0;
void Check(bool value, const char* what) {
    if (!value) { ++failures; std::cerr << "FAIL: " << what << '\n'; }
}
std::vector<Json> Rows(const fs::path& directory) {
    const auto history = lume::shell::ReadWallpaperResourceHistory(directory);
    std::istringstream in(history.records);
    std::vector<Json> rows;
    std::string line;
    while (std::getline(in, line)) {
        auto row = Json::parse(line, nullptr, false);
        if (row.is_object()) rows.push_back(std::move(row));
    }
    return rows;
}
std::size_t Samples(const fs::path& directory) {
    std::size_t result = 0;
    for (const auto& row : Rows(directory)) if (row.value("type", std::string{}) == "sample") ++result;
    return result;
}
template<class Test> bool Wait(Test test) {
    const auto end = GetTickCount64() + 6000;
    do { if (test()) return true; Sleep(25); } while (GetTickCount64() < end);
    return test();
}
}
int main() {
    const auto root = fs::temp_directory_path() / (L"LumeResourceMonitor-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(root);
    FILETIME created{}, exited{}, kernel{}, user{};
    Check(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) != FALSE, "read only this test's identity");
    const auto creation = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    Json context = Json::array({{{"displayId", "1"}, {"wallpaperId", "123"}, {"pid", GetCurrentProcessId()},
        {"createdFileTime", std::to_string(creation)}, {"state", "playing"}, {"manualPaused", false},
        {"systemPaused", false}, {"parked", false}, {"error", ""}}});
    {
        lume::shell::WallpaperResourceMonitor monitor(root / "minute", 60000);
        monitor.Update(context);
        Check(Wait([&] { return Samples(root / "minute") == 1; }), "first sample is persisted without waiting a minute");
        for (int i = 0; i < 100; ++i) monitor.Update(context);
        Sleep(150);
        Check(Samples(root / "minute") == 1, "repeated actor updates never turn minute sampling into continuous collection");
        auto parked = context;
        parked[0]["pid"] = 0; parked[0]["createdFileTime"] = "0";
        parked[0]["state"] = "parked"; parked[0]["parked"] = true; parked[0]["systemPaused"] = true;
        monitor.Update(parked);
        Check(Wait([&] {
            for (const auto& row : Rows(root / "minute"))
                if (row.value("type", std::string{}) == "event" && row.contains("processes"))
                    for (const auto& process : row["processes"])
                        if (process.value("state", std::string{}) == "parked") return true;
            return false;
        }), "policy transitions are recorded promptly without an extra resource sample");
        monitor.Update(Json::array());
    }
    {
        lume::shell::WallpaperResourceMonitor monitor(root / "periodic", 100);
        monitor.Update(context);
        Check(Wait([&] { return Samples(root / "periodic") >= 2; }), "worker records interval samples with a stable process identity");
    }
    bool cpuReady = false;
    for (const auto& row : Rows(root / "periodic"))
        if (row.value("type", std::string{}) == "sample" && row.contains("processes"))
            for (const auto& process : row["processes"])
                cpuReady = cpuReady || (process.contains("cpuMachinePercent") && process["cpuMachinePercent"].is_number());
    Check(cpuReady, "CPU rate becomes available only after a real interval baseline");
    std::error_code error;
    fs::remove_all(root, error);
    if (!failures) std::cout << "wallpaper resource worker cadence, identity, transitions and shutdown passed\n";
    return failures ? 1 : 0;
}
