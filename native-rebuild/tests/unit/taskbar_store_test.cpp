#include "core/taskbar/taskbar_store.h"
#include "vendor/nlohmann/json.hpp"
#include <cstdlib>
#include <iostream>
#include <map>

using namespace lume::taskbar;
namespace {
void Check(bool ok, const char* what) { if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); } }
class Files final : public lume::settings::IFileStore {
public:
    std::map<std::string, std::string> data;
    bool failWrite = false, failRename = false;
    bool Read(const std::string&, std::string&) const override { Check(false, "bounded reads required"); return false; }
    bool ReadBounded(const std::string& p, std::size_t limit, std::string& value) const override {
        const auto it = data.find(p);
        if (it == data.end() || it->second.size() > limit) return false;
        value = it->second; return true;
    }
    bool Exists(const std::string& p) const override { return data.count(p) != 0; }
    bool Write(const std::string& p, const std::string& value) override {
        if (failWrite) return false;
        data[p] = value; return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        if (failRename || !data.count(from)) return false;
        data[to] = data.at(from); data.erase(from); return true;
    }
    bool Remove(const std::string& p) override { data.erase(p); return true; }
};
}
int main() {
    Record source; source.preferences.enabled = true; source.preferences.autoHideTaskbar = true;
    source.ownsAutoHide = true; source.ownsCore = true;
    source.appearances = {{0x123456789ULL, 12345, 0x1122334455667788ULL, 3, UINT32_MAX, 0xabcdef, 1}};
    std::string text; Check(Encode(source, text), "encode exact 64-bit window/process identity");
    Record decoded;
    Check(Decode(text, decoded) && decoded.appearances[0].processCreated == source.appearances[0].processCreated &&
          decoded.appearances[0].flags == UINT32_MAX && decoded.ownsCore && decoded.ownsAutoHide &&
          decoded.preferences.autoHideTaskbar, "journal exact roundtrip preserves saved auto-hide on");
    auto legacy = nlohmann::json::parse(text);
    legacy["version"] = 1; legacy.erase("hideBehavior"); legacy.erase("hiddenTaskbars");
    Check(Decode(legacy.dump(), decoded) && decoded.preferences.hideBehavior == HideBehavior::AutoHide &&
          decoded.hiddenTaskbars.empty() && decoded.ownsAutoHide, "version 1 migrates without losing recovery or changing hiding behavior");
    source.preferences.hideBehavior = HideBehavior::FullyHidden;
    Visibility hidden; hidden.window = 77; hidden.process = 22; hidden.processCreated = 9876543210ULL;
    hidden.extendedStyle = 0x800a0; hidden.alpha = 177; hidden.layeredFlags = 3; hidden.colorKey = 123; hidden.visible = false;
    source.hiddenTaskbars = {hidden};
    Check(Encode(source, text) && Decode(text, decoded) && decoded.hiddenTaskbars.size() == 1 &&
          decoded.hiddenTaskbars[0].processCreated == hidden.processCreated && decoded.hiddenTaskbars[0].alpha == 177 &&
          !decoded.hiddenTaskbars[0].visible && decoded.preferences.hideBehavior == HideBehavior::FullyHidden,
          "full-hide behavior and original visibility survive durable roundtrip");
    for (const auto& invalidBehavior : {nlohmann::json(true), nlohmann::json("hidden")}) {
        auto bad = nlohmann::json::parse(text); bad["hideBehavior"] = invalidBehavior;
        Check(!Decode(bad.dump(), decoded), "invalid hide behavior rejected");
    }
    for (const auto& field : {"alpha", "layeredFlags", "window"}) {
        auto bad = nlohmann::json::parse(text); bad["hiddenTaskbars"][0][field] = -1;
        Check(!Decode(bad.dump(), decoded), "invalid visibility numbers rejected");
    }
    for (const auto& invalid : {std::string("{}"), text + "x", std::string(MaxRecordBytes + 1, 'x'),
                               "{\"enabled\":true," + text.substr(1)}) {
        Record sentinel; sentinel.originalAutoHide = true;
        Check(!Decode(invalid, sentinel) && sentinel.originalAutoHide, "reject malformed journal without partial mutation");
    }
    auto negative = text; const auto window = negative.find("\"window\":");
    Check(window != std::string::npos, "window field exists");
    negative.replace(window, std::string("\"window\":4886718345").size(), "\"window\":-1");
    Check(!Decode(negative, decoded), "negative HWND rejected");
    Files files; RecordStore store(files);
    const auto fresh = store.Load("taskbar.json");
    Check(fresh.state == LoadState::Fresh && !fresh.record.preferences.autoHideTaskbar &&
          fresh.record.preferences.transparentTaskbar, "fresh defaults enable transparency without auto-hide");
    Check(store.Save("taskbar.json", source), "durable initial write");
    const auto original = files.data["taskbar.json"];
    source.preferences.enabled = false;
    for (int failure = 0; failure < 2; ++failure) {
        files.failWrite = failure == 0; files.failRename = failure == 1;
        Check(!store.Save("taskbar.json", source) && files.data["taskbar.json"] == original &&
              !files.data.count("taskbar.json.tmp"), "failed atomic commit preserves complete prior record");
    }
    files.failWrite = files.failRename = false;
    files.data["taskbar.json"] = "corrupt";
    files.data["taskbar.json.bak"] = original;
    Check(store.Load("taskbar.json").state == LoadState::Failed,
          "recovery never guesses ownership from a potentially stale backup");
    std::cout << "taskbar store passed\n";
}
