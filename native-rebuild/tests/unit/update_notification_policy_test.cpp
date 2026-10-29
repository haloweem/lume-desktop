#include "core/update_notification/update_notification.h"
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace lume::application_settings;
using namespace lume::update_notification;
namespace {
void Check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct File final : IPreferencesFile {
    FileRead value;
    bool fail = false;
    int writes = 0;
    FileRead Read() override { return value; }
    bool WriteAtomically(const std::string& bytes) override {
        ++writes;
        if (fail) return false;
        value = {ReadStatus::Loaded, bytes};
        return true;
    }
};
constexpr ClockSample Today{1790726400000LL, 20260930}, Tomorrow{1790812800000LL, 20261001};
ManifestResult Newer() { return {{"incompatible", "1.0.10", 0}, "update-incompatible"}; }
void Daily() {
    File file; Policy policy(file); int shown = 0;
    Check(policy.Load() && policy.AutomaticDue(Today), "fresh install checks automatically");
    const auto show = [&](const Notice& notice) {
        ++shown; Check(!notice.manual && notice.kind == NoticeKind::Incompatible, "newer incompatible release is informative");
        return true;
    };
    Check(policy.Complete(false, Newer(), Today, show), "first newer release delivered");
    Check(shown == 1 && !policy.AutomaticDue(Today), "same day is deduplicated");
    Check(policy.Complete(false, Newer(), Today, show) && shown == 1, "repeated completion does not show twice");
    Policy restarted(file);
    Check(restarted.Load() && !restarted.AutomaticDue(Today), "restart preserves daily deduplication");
    Check(restarted.AutomaticDue(Tomorrow) && restarted.Complete(false, Newer(), Tomorrow, show) && shown == 2,
          "same version reminds again on next natural day");
    Check(!restarted.AutomaticDue(Today), "clock rollback does not repeat prior days");
}
void NewReleaseSameDay() {
    File file;Policy policy(file);Check(policy.Load(), "load release reminder fixture");int shown = 0;
    const auto show = [&](const Notice& notice) {Check(notice.kind == NoticeKind::Available, "compatible notice supports direct download");++shown;return true;};
    Check(policy.Complete(false, {{"available","1.0.10",0},{}}, Today, show) && shown == 1, "new release is announced");
    Check(policy.Complete(false, {{"available","1.0.10",0},{}}, Today, show) && shown == 1, "polling the same release stays silent");
    Check(policy.Complete(false, {{"available","1.0.11",0},{}}, Today, show) && shown == 2, "another release on the same day gets a notice");
    Policy reopened(file);Check(reopened.Load(), "reload reminder ledger");
    Check(reopened.Complete(false, {{"available","1.0.11",0},{}}, Today, show) && shown == 2, "same-day restart does not repeat");
    Check(reopened.Complete(false, {{"available","1.0.11",0},{}}, Tomorrow, show) && shown == 3, "not updated reminds on the next day");
    Policy installed(file,"1.0.10");Check(installed.Load(), "load updated installation");
    Check(installed.Complete(false, {{"available","1.0.10",0},{}}, Tomorrow, show) && shown == 3, "installed release stops reminders");
}
void OldAndInstalled() {
    File file; Policy policy(file); int shown = 0;
    Check(policy.Load(), "load old release fixture");
    const auto old = EvaluateManifest(R"({"version":"1.0.1","downloadUrl":"https://lumedesktop.com/downloads/LumeSetup-1.0.1.exe"})");
    Check(policy.Complete(false, old, Today, [&](const Notice&) { ++shown; return true; }) && shown == 0,
          "official older Electron manifest stays silent automatically");
    Check(!policy.AutomaticDue(Today), "successful current check is persisted");
    Policy upgraded(file, "1.0.10");
    Check(upgraded.Load() && upgraded.AutomaticDue(Today), "installed version change invalidates previous check date");
    Check(upgraded.Complete(false, Newer(), Today, [&](const Notice&) { ++shown; return true; }) && shown == 0,
          "installing the notified version stops reminders even for a stale result");
}
void Manual() {
    File file; Policy policy(file); std::vector<Notice> notices;
    Check(policy.Load(), "load manual fixture");
    const auto show = [&](const Notice& n) { notices.push_back(n); return true; };
    Check(policy.Complete(true, {{"current", "1.0.1", 0}, {}}, Today, show), "manual current has a visible result");
    Check(policy.Complete(true, {{"error", {}, 0}, "update-offline"}, Today, show), "manual network failure remains visible");
    Check(policy.Complete(true, Newer(), Today, show) && policy.Complete(true, Newer(), Today, show),
          "manual checks bypass daily suppression");
    Check(notices.size() == 4 && notices[0].kind == NoticeKind::Current &&
          notices[1].kind == NoticeKind::Error && notices[3].manual, "manual classification is explicit");
    Check(policy.Complete(false, Newer(), Today, show) && notices.size() == 4, "manual newer delivery suppresses later automatic notice");
}
void Failures() {
    File file; Policy policy(file); int shown = 0;
    Check(policy.Load(), "load failure fixture");
    Check(policy.Complete(false, {{"error", {}, 0}, "update-offline"}, Today,
        [&](const Notice&) { ++shown; return true; }) && shown == 0 && policy.AutomaticDue(Today),
        "offline automatic checks are silent and do not consume the day");
    Check(!policy.Complete(false, Newer(), Today, [](const Notice&) { return false; }) &&
          policy.AutomaticDue(Today) && file.writes == 0, "failed presentation consumes no persistent or in-memory reminder");
    file.fail = true;
    Check(!policy.Complete(false, Newer(), Today, [&](const Notice&) { ++shown; return true; }) &&
          shown == 1 && !policy.Error().empty(), "write failure is reported after visible delivery");
    Check(!policy.AutomaticDue(Today), "write failure cannot produce an in-process notification storm");
    const auto writes = file.writes; file.fail = false;
    Check(policy.Flush() && file.writes == writes + 1, "failed persistence is retried without showing again");
    Policy restarted(file);
    Check(restarted.Load() && !restarted.AutomaticDue(Today), "recovered persistence survives restart");
}
void InvalidStorage() {
    for (const auto* bytes : {"{", R"({"version":99})",
        R"({"version":1,"version":1})", R"({"version":1,"checkedDay":true})"}) {
        File file; file.value = {ReadStatus::Loaded, bytes}; Policy policy(file);
        Check(!policy.Load() && !policy.AutomaticDue(Today), "invalid or future storage disables automatic reminders");
        int shown = 0;
        policy.Complete(true, Newer(), Today, [&](const Notice&) { ++shown; return true; });
        Check(shown == 1 && file.writes == 0 && file.value.bytes == bytes, "manual result does not overwrite protected storage");
    }
}
void Validation() {
    File file; Policy policy(file); Check(policy.Load(), "load validation fixture");
    Check(!policy.AutomaticDue({0, 0}) && !policy.AutomaticDue({1, 20260230}), "invalid civil dates are rejected");
    int shown = 0;
    Check(policy.Complete(false, {{"error", "1.0.10", 0}, "update-manifest"}, Today,
        [&](const Notice&) { ++shown; return true; }) && shown == 0 && policy.AutomaticDue(Today),
        "malformed newer metadata is not promoted into an automatic release notice");
    Check(policy.Complete(false, {{"incompatible", "1.0.10", 0}, "update-disabled"}, Today,
        [&](const Notice& n) { ++shown; return n.kind == NoticeKind::Disabled; }) && shown == 1,
        "native metadata still cannot enable an installer");
}
void RecoveryAndProtection() {
    File file; Policy policy(file); Check(policy.Load(), "load recovery fixture");
    const ManifestResult current{{"current", "1.0.1", 0}, {}};
    Check(policy.Complete(true, current, Today, [](const Notice&) { return true; }), "seed completed day");
    Check(!policy.Complete(true, current, Today, [](const Notice&) { return false; }), "same-day manual display can fail");
    Check(policy.Complete(true, current, Today, [](const Notice&) { return true; }) && policy.Error().empty(),
        "successful presentation clears its old error even when no ledger change is needed");
    Check(!policy.Complete(true, current, Today, [](const Notice&) -> bool { throw std::runtime_error("show"); }),
        "throwing presentation is reported");
    Check(policy.Complete(true, {{"error", {}, 0}, "update-offline"}, Today, [](const Notice&) { return true; }) &&
          policy.Error().empty(), "visible manual error clears stale presentation error");
    file.fail = true;
    Check(!policy.Complete(false, Newer(), Tomorrow, [](const Notice&) { return true; }), "seed dirty in-memory state");
    file.value = {ReadStatus::Loaded, R"({"version":2})"};
    const int writes = file.writes;
    Check(!policy.Flush() && file.writes == writes, "future replacement is not overwritten during retry");
    Check(!policy.AutomaticDue({0, 20261002}), "unknown replacement pauses further automatic notifications");
    Check(!policy.Complete(false, Newer(), {0, 20261002}, [](const Notice&) {
        Check(false, "protected storage cannot show another automatic notice"); return true;
    }), "protected ledger remains closed");
}
}
int main() {
    int failures = 0;
    for (const auto& test : std::vector<std::pair<const char*, void(*)()>>{
        {"daily", Daily}, {"same-day-new-release", NewReleaseSameDay}, {"old-and-installed", OldAndInstalled}, {"manual", Manual},
        {"failures", Failures}, {"invalid-storage", InvalidStorage}, {"validation", Validation},
        {"recovery-and-protection", RecoveryAndProtection}}) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    return failures ? 1 : 0;
}
