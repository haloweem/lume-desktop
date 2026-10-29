#include "core/desktop_icons/desktop_icons_store.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

using namespace lume::desktop_icons;
namespace {
void Check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
class Files final : public lume::settings::IFileStore {
public:
    std::map<std::string, std::string> data;
    std::string failWrite, failRenameFrom, failRead;
    mutable std::size_t maxRead = 0;
    bool Read(const std::string&, std::string&) const override {
        Check(false, "all preference reads must be bounded"); return false;
    }
    bool ReadBounded(const std::string& path, std::size_t limit, std::string& out) const override {
        maxRead = limit;
        auto it = data.find(path);
        if (it == data.end() || path == failRead || it->second.size() > limit) return false;
        out = it->second; return true;
    }
    bool Exists(const std::string& path) const override { return data.count(path) != 0; }
    bool Write(const std::string& path, const std::string& bytes) override {
        if (path == failWrite) return false;
        data[path] = bytes; return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        if (from == failRenameFrom || !data.count(from)) return false;
        data[to] = data.at(from); data.erase(from); return true;
    }
    bool Remove(const std::string& path) override { data.erase(path); return true; }
};
}
int main() {
    Preferences prefs;
    prefs.enabled = true; prefs.autoHideDelay = 60; prefs.manuallyHidden = true; prefs.enableAutoHide = true;
    std::string encoded;
    Check(Encode(prefs, encoded), "encode independent desktop icon settings");
    Preferences decoded;
    Check(Decode(encoded, decoded) && decoded.enabled && decoded.manuallyHidden &&
          decoded.autoHideDelay == 60 && decoded.showIconsOnAnyInput && decoded.enableAutoHide,
          "saved automatic hiding stays enabled independently of the new default");
    const auto reject = [&](std::string text) {
        Preferences output; output.autoHideDelay = 33;
        Check(!Decode(text, output) && output.autoHideDelay == 33, "reject without partial mutation");
    };
    reject(encoded + "x");
    reject("{}");
    reject(std::string(MaxPreferencesBytes + 1, ' '));
    reject("{\"version\":1,\"version\":1" + encoded.substr(encoded.find(",\"enabled\"")));
    auto bad = encoded; bad.insert(1, "\"unrelated\":true,"); reject(bad);
    const auto replaceDelay = [&](const char* value) {
        auto text = encoded; const auto start = text.find("\"autoHideDelay\":60");
        Check(start != std::string::npos, "canonical delay field");
        text.replace(start, std::string("\"autoHideDelay\":60").size(),
                     std::string("\"autoHideDelay\":") + value);
        reject(text);
    };
    for (const auto* value : {"2", "61", "-1", "4294967301", "5.5", "5e0", "\"5\"", "null", "true", "05"})
        replaceDelay(value);
    prefs.autoHideDelay = 2;
    const auto oldText = encoded;
    Check(!Encode(prefs, encoded) && encoded == oldText, "save never silently clamps invalid input");
    prefs.autoHideDelay = 60;
    Files files; PreferencesStore store(files);
    Check(store.Load("icons.json").state == LoadState::Fresh, "missing is fresh");
    Check(!store.Load("icons.json").value.enableAutoHide, "fresh store defaults to no automatic hiding");
    Check(store.Load("").state == LoadState::Failed && !store.Save("", prefs), "empty path is not writable");
    Check(store.Save("icons.json", prefs), "initial atomic save");
    const auto first = files.data.at("icons.json");
    prefs.autoHideDelay = 3; prefs.manuallyHidden = false;
    Check(store.Save("icons.json", prefs) && files.data.at("icons.json.bak") == first, "valid prior state becomes backup");
    const auto saved = store.Load("icons.json");
    Check(saved.state == LoadState::Loaded && !saved.value.manuallyHidden &&
          saved.value.autoHideDelay == 3 && files.maxRead == MaxPreferencesBytes, "bounded load");
    const auto second = files.data.at("icons.json");
    files.data["icons.json"] = "corrupt main";
    Check(store.Load("icons.json").state == LoadState::Recovered, "corrupt main uses valid backup");
    Check(store.Save("icons.json", prefs) &&
          files.data.at("icons.json.corrupt") == "corrupt main" &&
          files.data.at("icons.json.bak") == first, "quarantine preserves recovery backup");
    files.data["icons.json"] = "another corrupt main";
    Check(store.Save("icons.json", prefs) &&
          files.data.at("icons.json.corrupt.1") == "another corrupt main" &&
          files.data.at("icons.json.corrupt") == "corrupt main", "quarantine never overwrites prior evidence");
    files.data["icons.json.bak"] = "bad backup";
    Check(store.Load("icons.json").state == LoadState::Loaded, "valid main wins over bad backup");
    files.data["icons.json"] = "bad main";
    Check(store.Load("icons.json").state == LoadState::Failed, "both invalid is failed");
    Check(!store.Load("icons.json").value.enableAutoHide, "invalid store also keeps automatic hiding off");

    for (int failure = 0; failure < 5; ++failure) {
        Files broken; broken.data["icons.json"] = first;
        broken.data["icons.json.bak"] = second;
        if (failure == 0) broken.failRead = "icons.json";
        if (failure == 1) broken.failWrite = "icons.json.tmp";
        if (failure == 2) broken.failWrite = "icons.json.bak.tmp";
        if (failure == 3) broken.failRenameFrom = "icons.json.bak.tmp";
        if (failure == 4) broken.failRenameFrom = "icons.json.tmp";
        PreferencesStore failing(broken);
        Check(!failing.Save("icons.json", prefs) && broken.data.at("icons.json") == first,
              "failed save leaves prior primary bytes intact");
        Check(!broken.Exists("icons.json.tmp") && !broken.Exists("icons.json.bak.tmp"),
              "failed save cleans its temporary files");
    }
    Files recovery;
    recovery.data["icons.json"] = "corrupt";
    recovery.data["icons.json.bak"] = first;
    recovery.failRenameFrom = "icons.json.tmp";
    PreferencesStore retry(recovery);
    Check(!retry.Save("icons.json", prefs) && recovery.data.at("icons.json") == "corrupt" &&
          recovery.data.at("icons.json.bak") == first, "failed repair rolls quarantined main back");
    std::cout << "desktop icon codec and transactional persistence passed\n";
}
