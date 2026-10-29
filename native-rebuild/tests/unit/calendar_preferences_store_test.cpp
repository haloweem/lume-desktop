#include "core/calendar/calendar_preferences_store.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <vector>

namespace {
using namespace lume::calendar;
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
class Files final : public lume::settings::IFileStore {
public:
    bool Read(const std::string& path, std::string& out) const override {
        operations.push_back("read:" + path);
        if (fail.count("read:" + path)) return false;
        const auto it = files.find(path);
        if (it == files.end()) return false;
        out = it->second; return true;
    }
    bool Exists(const std::string& path) const override { return files.count(path) != 0; }
    bool Write(const std::string& path, const std::string& value) override {
        operations.push_back("write:" + path);
        if (fail.count("write:" + path)) { files[path] = "partial"; return false; }
        files[path] = value; return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        operations.push_back("rename:" + from + ":" + to);
        if (fail.count("rename:" + from + ":" + to)) return false;
        const auto it = files.find(from);
        if (it == files.end()) return false;
        files[to] = it->second; files.erase(it); return true;
    }
    bool Remove(const std::string& path) override {
        operations.push_back("remove:" + path);
        if (fail.count("remove:" + path)) return false;
        files.erase(path); return true;
    }
    std::map<std::string,std::string> files;
    std::set<std::string> fail;
    mutable std::vector<std::string> operations;
};
std::string Encoded(bool enabled, int x = 0) {
    Preferences value; value.enabled = enabled; value.profiles[0].x = x;
    std::string text; Expect(CalendarPreferencesCodec{}.Encode(value,text), "encode fixture"); return text;
}
void FreshAndSuccess() {
    Files files; CalendarPreferencesStore store(files);
    const auto fresh = store.Load("p");
    Expect(fresh.source == PreferencesSource::Defaults && fresh.status == LoadStatus::Fresh &&
        fresh.error == LoadError::None && !fresh.value.enabled, "missing fresh load is clean disabled defaults");
    Preferences next; next.enabled = true;
    Expect(store.Save("p",next), "first save succeeds");
    Expect(!files.Exists("p.bak") && !files.Exists("p.tmp"), "first save has no unnecessary backup or temporary");
    auto loaded = store.Load("p");
    Expect(loaded.source == PreferencesSource::Main && loaded.status == LoadStatus::Loaded &&
        loaded.error == LoadError::None && loaded.value.enabled, "saved main reloads");
    const auto previous = files.files.at("p");
    next.profiles[0].x = 22;
    Expect(store.Save("p",next) && files.files.at("p.bak") == previous, "valid previous bytes become backup");
    Expect(!files.Exists("p.tmp") && !files.Exists("p.bak.tmp"), "successful save consumes temporary files");
    files.files["p.bak"] = "bad";
    Expect(store.Load("p").value.profiles[0].x == 22, "valid main wins over invalid backup");
    files.operations.clear();
    Expect(!store.Save({},next) && files.operations.empty(), "empty path refuses all file operations");
    Expect(store.Load({}).status == LoadStatus::Failed && store.Load({}).error == LoadError::InvalidPath &&
        files.operations.empty(), "empty load path reports invalid without file operations");
    next.theme = static_cast<Theme>(9);
    Expect(!store.Save("p",next) && files.operations.empty(), "invalid preferences have no file side effects");
}
void RecoveryAndIsolation() {
    Files files; CalendarPreferencesStore store(files);
    const auto backup = Encoded(true,123);
    files.files["p.bak"] = backup;
    auto result = store.Load("p");
    Expect(result.source == PreferencesSource::Backup && result.status == LoadStatus::RecoveredBackup &&
        result.error == LoadError::None && result.value.profiles[0].x == 123, "missing main recovers from backup");
    files.files["p"] = "bad1"; files.files["p.corrupt"] = "evidence0"; files.files["p.corrupt.1"] = "evidence1";
    result = store.Load("p");
    Expect(result.source == PreferencesSource::Backup && result.status == LoadStatus::RecoveredBackup &&
        result.error == LoadError::MainInvalid, "bad main reports backup recovery");
    Expect(files.files.at("p.corrupt") == "evidence0" && files.files.at("p.corrupt.1") == "evidence1" &&
        files.files.at("p.corrupt.2") == "bad1" && files.files.at("p.bak") == backup,
        "quarantine uses next unused suffix and preserves sole good backup");
    files.files["p"] = "bad2";
    Expect(store.Save("p",{}) && files.files.at("p.corrupt.3") == "bad2" && files.files.at("p.bak") == backup,
        "saving with bad main isolates it without backing up bad bytes");
    files.files["p"] = "bad3"; files.files["p.bak"] = "badbackup";
    result = store.Load("p");
    Expect(result.source == PreferencesSource::Defaults && result.status == LoadStatus::Failed &&
        result.error != LoadError::None && !result.value.enabled, "both bad files report failure with defaults");
    Expect(files.files.at("p.bak") == "badbackup", "load never moves backup");
    files.files.erase("p");
    result = store.Load("p");
    Expect(result.status == LoadStatus::Failed && result.error == LoadError::BackupInvalid, "bad backup alone is not a fresh install");
}
void FailurePointsPreserveMain() {
    const auto original = Encoded(false,12);
    const auto backup = Encoded(true,23);
    for (const auto* operation : {"write:p.tmp", "write:p.bak.tmp", "rename:p.bak.tmp:p.bak", "rename:p.tmp:p"}) {
        Files files; CalendarPreferencesStore store(files);
        files.files["p"] = original; files.files["p.bak"] = backup; files.fail.insert(operation);
        Preferences next; next.profiles[0].x = 99;
        Expect(!store.Save("p",next), "injected save failure reported");
        Expect(files.files.at("p") == original && !files.Exists("p.tmp") && !files.Exists("p.bak.tmp"),
            "precommit failure leaves original main and cleans own temporary files");
        Expect(files.files.at("p.bak") == (std::string(operation) == "rename:p.tmp:p" ? original : backup),
            "backup changes only after backup replacement succeeded");
        Expect(store.Load("p").value.profiles[0].x == 12, "original main remains loadable after save failure");
    }
    Files files; CalendarPreferencesStore store(files); files.files["p"] = original;
    files.fail = {"remove:p.tmp","remove:p.bak.tmp"};
    Expect(store.Save("p",{}), "cleanup failures cannot turn successful commit into failure");
}
void UnreadableAndIsolationFailures() {
    Files files; CalendarPreferencesStore store(files);
    const auto good = Encoded(true,7);
    files.files["p"] = good; files.files["p.bak"] = good; files.fail.insert("read:p");
    const auto result = store.Load("p");
    Expect(result.source == PreferencesSource::Backup && result.error == LoadError::MainRead, "I/O failure recovers with explicit I/O error");
    Expect(files.files.at("p") == good && !files.Exists("p.corrupt"), "unreadable primary is never quarantined");
    Expect(!store.Save("p",{}) && files.files.at("p") == good && files.files.at("p.bak") == good && !files.Exists("p.tmp"),
        "save refuses unreadable primary without disturbing backup");
    files.files.erase("p"); files.fail.insert("read:p.bak");
    Expect(store.Load("p").status == LoadStatus::Failed && store.Load("p").error == LoadError::BackupRead,
        "unreadable backup is not fresh defaults");
    files.fail.clear(); files.files["p"] = "bad"; files.fail.insert("rename:p:p.corrupt");
    const auto recovered = store.Load("p");
    Expect(recovered.source == PreferencesSource::Backup && recovered.error == LoadError::Isolation, "failed isolation still returns valid backup with error");
    Expect(!store.Save("p",{}) && files.files.at("p") == "bad" && files.files.at("p.bak") == good,
        "failed isolation refuses overwrite of bad main and keeps good backup");
    files.fail.clear();
    for (int i = 0; i < 1024; ++i) files.files[i == 0 ? "p.corrupt" : "p.corrupt." + std::to_string(i)] = "evidence";
    Expect(!store.Save("p",{}) && files.files.at("p") == "bad" && files.files.at("p.corrupt.1023") == "evidence",
        "bounded quarantine search fails without overwriting evidence");
}
void MissingOrBadMainNeverDestroysBackup() {
    const auto backup = Encoded(true,77);
    for (const bool badMain : {false,true}) {
        Files files; CalendarPreferencesStore store(files); files.files["p.bak"] = backup;
        if (badMain) files.files["p"] = "bad";
        files.fail.insert("rename:p.tmp:p");
        Expect(!store.Save("p",{}) && files.files.at("p.bak") == backup && !files.Exists("p.tmp"),
            "missing or bad main commit failure keeps sole valid backup");
        const auto recovered = store.Load("p");
        Expect(recovered.source == PreferencesSource::Backup && recovered.value.profiles[0].x == 77,
            "backup remains recoverable after replacement failure");
        files.fail.clear();
        Expect(store.Save("p",{}) && files.files.at("p.bak") == backup, "new main never overwrites retained backup");
    }
}
}
int main() { FreshAndSuccess(); RecoveryAndIsolation(); FailurePointsPreserveMain(); UnreadableAndIsolationFailures(); MissingOrBadMainNeverDestroysBackup(); }
