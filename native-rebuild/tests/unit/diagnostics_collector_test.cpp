#include "core/diagnostics/diagnostics.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {

using lume::diagnostics::Collector;
using lume::diagnostics::Event;
using lume::diagnostics::Action;
using Json = lume::diagnostics::Json;

bool passed = true;

void Check(bool condition, const char* message) {
    if (!condition) {
        passed = false;
        std::cerr << "FAIL: " << message << '\n';
    }
}

std::filesystem::path TestPath() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("lume-diagnostics-test-" + std::to_string(stamp));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return root / "diagnostics.json";
}

}  // namespace

int main() {
    Check(lume::diagnostics::ParseCommand(LR"({"version":1,"scope":"diagnostics","action":"snapshot"})") == Action::Snapshot,
          "strict snapshot command is accepted");
    Check(lume::diagnostics::ParseCommand(LR"({"version":1,"scope":"diagnostics","action":"clear","path":"C:\\private"})") == std::nullopt,
          "diagnostics commands reject unknown fields");
    Check(lume::diagnostics::ParseCommand(LR"({"version":1,"scope":"diagnostics","action":"clear","action":"snapshot"})") == std::nullopt,
          "diagnostics commands reject duplicate keys");

    const auto path = TestPath();
    const auto pathText = path.u8string();

    {
        Collector collector;
        const auto operation = collector.StartOperation("dock", "apply");
        Check(operation.rfind("op-", 0) == 0, "operation id is opaque and versioned");
        Check(collector.CompleteOperation(operation, "failed", {}, {}, "visible", "hidden", "layers-hidden"),
              "failed operation is accepted");

        const auto snapshot = collector.Snapshot();
        Check(snapshot.value("schemaVersion", 0u) == Collector::kSchemaVersion,
              "snapshot carries schema version");
        Check(snapshot.at("events").size() == 1, "failed operation becomes an event");
        Check(snapshot.at("events").at(0).value("kind", "") == "effect-mismatch",
              "expected/actual mismatch is classified");
        Check(snapshot.at("events").at(0).value("expected", "") == "visible",
              "expected state is retained");
        Check(snapshot.at("events").at(0).value("actual", "") == "hidden",
              "observed state is retained");
    }

    {
        Collector collector(pathText);
        Json message = {
            {"type", "dock-state"},
            {"module", "dock"},
            {"status", "degraded"},
            {"error", "layers-hidden"},
            {"expected", "visible"},
            {"actual", "hidden"},
            {"path", "C:\\Users\\private\\dock.json"},
            {"token", "secret-token"},
            {"url", "https://private.example.invalid"},
        };
        Check(collector.RecordMessage(message), "structured state message is accepted");
        const auto text = collector.SnapshotText();
        Check(text.find("private.example") == std::string::npos, "URL is not persisted");
        Check(text.find("secret-token") == std::string::npos, "token is not persisted");
        Check(text.find("Users\\\\private") == std::string::npos, "path is not persisted");
        Check(collector.Snapshot().at("events").size() == 1, "structured mismatch becomes an event");
    }

    {
        Collector collector(pathText);
        for (std::uint64_t index = 0; index < Collector::kMaxEvents + 7; ++index) {
            Event event;
            event.at = index + 1;
            event.module = "module";
            event.kind = "error";
            event.status = "failed";
            event.error = "fault-" + std::to_string(index);
            Check(collector.RecordEvent(std::move(event)), "bounded event is accepted");
        }
        const auto snapshot = collector.Snapshot();
        Check(snapshot.at("events").size() == Collector::kMaxEvents, "event history is bounded");
        Check(snapshot.at("events").at(0).value("error", "") == "fault-7",
              "oldest event is evicted first");
    }

    {
        Collector collector(pathText);
        const auto restored = collector.Snapshot();
        Check(restored.at("events").size() == Collector::kMaxEvents,
              "bounded state survives restart");
        Check(collector.Clear(), "clear persists successfully");
        const auto cleared = collector.Snapshot();
        Check(cleared.at("events").empty() && cleared.at("health").empty(),
              "clear removes events and health");
        Collector afterClear(pathText);
        Check(afterClear.Snapshot().at("events").empty(), "clear survives restart");
    }

    {
        const auto mismatchPath = (path.parent_path() / "mismatch.json").u8string();
        Collector collector(mismatchPath);
        const Json hiddenDock = {
            {"version", 1}, {"type", "dock-state"}, {"enabled", true}, {"running", true},
            {"layersVisible", false}, {"visibleItems", 3}, {"pinned", 3}, {"added", 0}, {"error", ""},
        };
        Check(collector.RecordMessage(hiddenDock), "dock snapshot is recorded");
        auto snapshot = collector.Snapshot();
        Check(snapshot.at("mismatches").size() == 1, "enabled dock with hidden native layers creates mismatch without error");
        Check(snapshot.at("mismatches").at(0).value("expected", "") == "layers-visible",
              "dock mismatch carries expected effect");
        Check(snapshot.at("events").at(0).value("kind", "") == "effect-mismatch",
              "dock mismatch is retained in event history");
        collector.RecordMessage(hiddenDock);
        snapshot = collector.Snapshot();
        Check(snapshot.at("events").size() == 1, "repeated native mismatch does not grow the event list");
        Check(snapshot.at("events").at(0).value("repeatCount", 0u) == 1,
              "repeat frequency is throttled to avoid write and notification floods");
        Event repeated;
        repeated.at = snapshot.at("events").at(0).at("at").get<std::uint64_t>() + 60'000;
        repeated.module = "dock";
        repeated.kind = "effect-mismatch";
        repeated.error = "dock-layers-hidden";
        repeated.expected = "layers-visible";
        repeated.actual = "hidden";
        collector.RecordEvent(std::move(repeated));
        snapshot = collector.Snapshot();
        Check(snapshot.at("events").at(0).value("repeatCount", 0u) == 2,
              "repeat count advances after the throttle interval");
        auto visibleDock = hiddenDock;
        visibleDock["layersVisible"] = true;
        collector.RecordMessage(visibleDock);
        snapshot = collector.Snapshot();
        Check(snapshot.at("mismatches").empty(), "confirmed recovery clears active mismatch");
        Check(snapshot.at("events").size() == 1, "resolved mismatch remains in bounded event history");
        Collector restored(mismatchPath);
        Check(restored.Snapshot().at("mismatches").empty(), "resolved mismatch state persists across restart");
        Check(restored.Snapshot().at("events").at(0).value("repeatCount", 0u) == 2,
              "coalesced event count persists across restart");

        Collector benign;
        const Json noTaskbars = {{"version", 1}, {"type", "taskbar-state"}, {"enabled", true},
            {"transparentTaskbar", true}, {"autoHideTaskbar", false}, {"taskbars", 0},
            {"applying", false}, {"pendingRecovery", false}, {"running", false}, {"error", ""}};
        benign.RecordMessage(noTaskbars);
        Check(benign.Snapshot().at("mismatches").empty(), "no discovered taskbars do not create a false mismatch");
        auto settling = noTaskbars;
        settling["taskbars"] = 1;
        settling["applying"] = true;
        benign.RecordMessage(settling);
        Check(benign.Snapshot().at("mismatches").empty(), "taskbar apply in progress does not create a mismatch");
    }

    {
        Collector wallpaper;
        wallpaper.RecordMessage(Json{{"version",1},{"type","wallpaper-state"},{"status","idle"},
            {"loadStatus","loaded"},{"catalogStatus","ready"},{"catalogError",""},
            {"playerAvailable",true},{"pauseFullscreen",true},
            {"displays",Json::array({Json{{"state","failed"},{"error","mpv-exited"},
                {"path","C:\\Users\\private\\video.mp4"},{"title","Private wallpaper"}}})}});
        const auto snapshot=wallpaper.Snapshot();
        Check(snapshot.at("events").size()==1,"failed display is diagnosed without a top-level error code");
        Check(snapshot.at("events").at(0).value("error","")=="mpv-exited","display root-cause code is retained");
        const auto encoded=snapshot.dump();
        Check(encoded.find("Users\\\\private")==std::string::npos&&encoded.find("Private wallpaper")==std::string::npos,
              "wallpaper paths and titles are excluded from native diagnostics");
    }

    std::error_code error;
    std::filesystem::remove_all(path.parent_path(), error);
    return passed ? 0 : 1;
}
