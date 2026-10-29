#include "core/taskbar/taskbar_controller.h"
#include <cstdlib>
#include <iostream>
#include <map>

using namespace lume::taskbar;
namespace {
void Check(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAIL: " << what << '\n'; std::exit(1); }
}
class Files final : public lume::settings::IFileStore {
public:
    std::map<std::string, std::string> data;
    bool failWrite = false, failRename = false;
    bool Read(const std::string& path, std::string& out) const override {
        const auto it = data.find(path); if (it == data.end()) return false; out = it->second; return true;
    }
    bool Exists(const std::string& p) const override { return data.count(p) != 0; }
    bool Write(const std::string& p, const std::string& out) override {
        if (failWrite) return false;
        data[p] = out; return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        if (failRename || !data.count(from)) return false;
        data[to] = data.at(from); data.erase(from); return true;
    }
    bool Remove(const std::string& p) override { data.erase(p); return true; }
};
class Port final : public ITaskbarPort {
public:
    Files* files = nullptr;
    int touches = 0, starts = 0, stops = 0, writes = 0, releases = 0;
    bool autoHide = false, core = false, failRestore = false, failStart = false;
    bool fullyHidden = false, failHide = false;
    std::vector<Visibility> visibility;
    Mode mode = Mode::Classic;
    Error acquireError = Error::None, transparencyAcquireError = Error::None;
    std::vector<Appearance> windows{{10, 20, 30, 3, 4, 5, 6}, {11, 20, 30, 1, 8, 9, 10}};
    Error Acquire(bool transparency, Mode& out) override {
        ++touches; out = mode;
        return transparency && transparencyAcquireError != Error::None ? transparencyAcquireError : acquireError;
    }
    void Release() override { ++releases; }
    Error ReadAutoHide(bool& v) override { ++touches; v = autoHide; return Error::None; }
    Error SetAutoHide(bool v) override {
        Record before; Check(files && Decode(files->data.at("taskbar.json"), before) && before.ownsAutoHide,
                             "durable auto-hide recovery precedes system mutation");
        ++touches; ++writes; autoHide = v; return Error::None;
    }
    Error ReadVisibility(std::vector<Visibility>& out) override {
        out = visibility; return out.empty() ? Error::Discover : Error::None;
    }
    Error Hide(const Visibility& value) override {
        Record before;
        Check(files && Decode(files->data.at("taskbar.json"), before), "visibility journal readable before hide");
        bool found = false;
        for (const auto& item : before.hiddenTaskbars) if (SameTarget(item, value)) found = true;
        Check(found, "visibility is durable before hiding any taskbar");
        if (failHide) return Error::ApplyVisibility;
        fullyHidden = true; return Error::None;
    }
    RestoreResult RestoreVisibility(const Visibility&) override {
        if (failRestore) return RestoreResult::Failed;
        fullyHidden = false; return RestoreResult::Restored;
    }
    Error ReadClassic(std::vector<Appearance>& out) override { ++touches; out = windows; return Error::None; }
    Error MakeTransparent(const Appearance& a) override {
        Record before; Check(files && Decode(files->data.at("taskbar.json"), before),
                             "read journal before transparency");
        bool found = false;
        for (const auto& original : before.appearances) if (SameTarget(original, a)) found = true;
        Check(found, "all original taskbar properties are durable before transparency");
        ++touches;
        for (auto& w : windows) if (SameTarget(w, a)) { w.accent = 2; w.color = 0; }
        return Error::None;
    }
    RestoreResult RestoreAppearance(const Appearance& a) override {
        ++touches; if (failRestore) return RestoreResult::Failed;
        for (auto& w : windows) if (SameTarget(w, a)) { w = a; return RestoreResult::Restored; }
        return RestoreResult::Gone;
    }
    Error StartCore() override {
        Record before; Check(files && Decode(files->data.at("taskbar.json"), before) && before.ownsCore,
                             "durable core recovery precedes launch");
        ++starts; ++touches; if (failStart) return Error::CoreStart; core = true; return Error::None;
    }
    bool CoreAlive() const override { return core; }
    Error StopCore(bool previous) override {
        ++stops; ++touches;
        if (previous && transparencyAcquireError != Error::None) return transparencyAcquireError;
        if (failRestore) return Error::CoreStop;
        core = false; return Error::None;
    }
};
}
int main() {
    {
        Files hiddenFiles; RecordStore hiddenStore(hiddenFiles);
        Port hidden; hidden.files = &hiddenFiles; hidden.mode = Mode::Windows11X64;
        Visibility first; first.window = 101; first.process = 20; first.processCreated = 30;
        Visibility second = first; second.window = 102;
        hidden.visibility = {first, second};
        Controller c(hiddenStore, "taskbar.json", hidden);
        Preferences p; p.enabled = true; p.transparentTaskbar = false; p.autoHideTaskbar = true;
        p.hideBehavior = HideBehavior::FullyHidden;
        Check(c.Apply(p) && hidden.fullyHidden && hidden.autoHide && c.Journal().hiddenTaskbars.size() == 2,
              "full hiding applies to primary and secondary taskbars without transparency");
        hidden.fullyHidden = false;
        Check(c.Tick() && hidden.fullyHidden && c.Journal().hiddenTaskbars[0].visible,
              "Explorer reveal is suppressed without overwriting original visibility");
        hidden.visibility[0].processCreated = 31;
        hidden.visibility.erase(hidden.visibility.begin() + 1);
        Check(c.Tick() && c.Journal().hiddenTaskbars.size() == 1 &&
              c.Journal().hiddenTaskbars[0].processCreated == 31, "Explorer restart and display removal retire obsolete identities");
        p.hideBehavior = HideBehavior::AutoHide;
        Check(c.Apply(p) && !hidden.fullyHidden && hidden.autoHide && c.Journal().hiddenTaskbars.empty(),
              "switching back restores windows but preserves edge auto-hide");
        p.hideBehavior = HideBehavior::FullyHidden;
        hidden.failHide = true;
        Check(!c.Apply(p) && c.Snapshot().pendingRecovery && c.Snapshot().error == Error::ApplyVisibility,
              "partial full-hide failure retains recovery evidence");
        hidden.failHide = false;
        Check(c.Tick() && hidden.fullyHidden, "failed full hide can retry");
        p.autoHideTaskbar = false;
        Check(c.Apply(p) && !hidden.fullyHidden && !hidden.autoHide && p.hideBehavior == HideBehavior::FullyHidden,
              "hiding switch off restores visibility while remembering behavior");
        p.autoHideTaskbar = true;
        Check(c.Apply(p) && hidden.fullyHidden, "switch on reapplies remembered full hiding");
        hidden.failRestore = true; p.enabled = false;
        Check(!c.Apply(p) && c.Snapshot().pendingRecovery && !hidden.autoHide,
              "failed visibility recovery does not block auto-hide restoration");
        hidden.failRestore = false;
        Check(c.Shutdown() && !hidden.fullyHidden && !c.Snapshot().pendingRecovery,
              "shutdown retries and clears full-hide ownership");
        Record abandoned; abandoned.hiddenTaskbars = {first};
        Check(hiddenStore.Save("taskbar.json", abandoned), "write previous-process visibility recovery");
        hidden.fullyHidden = true;
        Controller afterCrash(hiddenStore, "taskbar.json", hidden);
        Check(afterCrash.Tick() && !hidden.fullyHidden && !afterCrash.Snapshot().pendingRecovery,
              "disabled startup recovers full hiding from a previous process");
    }
    Files files; Port port; port.files = &files; RecordStore store(files);
    {
        Controller c(store, "taskbar.json", port);
        Check(!c.Snapshot().preferences.enabled && c.Tick() && c.Shutdown() && port.touches == 0,
              "never enabled controller never touches taskbar");
        Preferences p; p.enabled = true;
        files.failWrite = true;
        Check(!c.Apply(p) && port.writes == 0 && !port.autoHide && port.windows[0].accent == 3,
              "save failure prevents all system mutations");
        files.failWrite = false;
        Check(c.Apply(p) && c.Snapshot().running && !port.autoHide, "enable defaults to transparency without auto-hide");
        Check(c.Journal().appearances.size() == 2 && port.windows[0].accent == 2, "both taskbars transparent");
        Check(c.Tick() && c.Journal().appearances[0].accent == 3, "maintenance never replaces original snapshot");
        port.windows = {{10, 20, 31, 4, 1, 2, 3}}; // Explorer restart / HWND reuse.
        Check(c.Tick() && port.windows[0].accent == 2, "new Explorer taskbar adopts enabled preference");
        p.enabled = false;
        port.failRestore = true;
        Check(!c.Apply(p) && c.Snapshot().pendingRecovery, "failed restore is retained durably");
        port.failRestore = false;
        Check(c.Tick() && !c.Snapshot().pendingRecovery && port.windows[0].accent == 4 && !port.autoHide,
              "retry restores new Explorer appearance and original auto-hide");
        const auto touches = port.touches;
        Check(c.Shutdown() && port.touches == touches, "repeat stop has no effects");
    }
    {
        Port win11; win11.files = &files; win11.mode = Mode::Windows11X64;
        Controller c(store, "taskbar.json", win11); Preferences p; p.enabled = true; p.autoHideTaskbar = true;
        Check(c.Apply(p) && win11.starts == 1 && c.Snapshot().running, "Windows 11 starts dedicated native core");
        win11.core = false;
        Check(c.Tick() && win11.starts == 2, "unexpected core exit triggers recovery and restart");
        p.transparentTaskbar = false;
        Check(c.Apply(p) && !win11.core && win11.autoHide, "disable transparency keeps requested auto-hide");
        Check(c.Shutdown() && !win11.autoHide && c.Snapshot().preferences.enabled,
              "shutdown restores system and preserves requested enable preference");
    }
    {
        Record abandoned; abandoned.ownsAutoHide = true; abandoned.originalAutoHide = false;
        Check(store.Save("taskbar.json", abandoned), "write crash recovery");
        Port afterCrash; afterCrash.files = &files; afterCrash.autoHide = true;
        Controller c(store, "taskbar.json", afterCrash);
        Check(c.Tick() && !afterCrash.autoHide && !c.Snapshot().pendingRecovery,
               "disabled startup restores only explicit durable ownership");
    }
    {
        Files recoveryFiles; RecordStore recoveryStore(recoveryFiles);
        Port stalled; stalled.files = &recoveryFiles; stalled.mode = Mode::Windows11X64;
        Controller c(recoveryStore, "taskbar.json", stalled); Preferences on; on.enabled = true; on.autoHideTaskbar = true;
        Check(c.Apply(on) && stalled.autoHide, "core failure fixture initially owns auto-hide");
        stalled.failRestore = true; on.enabled = false;
        Check(!c.Apply(on) && c.Journal().ownsCore && !c.Journal().ownsAutoHide && !stalled.autoHide,
              "failed core shutdown does not prevent independent auto-hide restoration");
        stalled.failRestore = false;
        Check(c.Tick() && !c.Snapshot().pendingRecovery, "only the failed core restoration remains to retry");
    }
    {
        Files recoveryFiles; RecordStore recoveryStore(recoveryFiles);
        Record abandoned; abandoned.ownsAutoHide = true; abandoned.ownsCore = true;
        Check(recoveryStore.Save("taskbar.json", abandoned), "write recovery with unavailable core payload");
        Port missing; missing.files = &recoveryFiles; missing.mode = Mode::Windows11X64; missing.autoHide = true;
        missing.transparencyAcquireError = Error::PayloadIntegrity;
        Controller c(recoveryStore, "taskbar.json", missing);
        Check(!c.Tick() && c.Journal().ownsCore && !c.Journal().ownsAutoHide && !missing.autoHide,
              "unavailable transparency payload cannot block independent auto-hide recovery");
        missing.transparencyAcquireError = Error::None;
        Check(c.Shutdown() && !c.Snapshot().pendingRecovery, "restored payload finishes remaining recovery");
    }
    {
        // A Lume surface may demand auto-hide independently of taskbar preferences.
        Files demandFiles; RecordStore demandStore(demandFiles);
        Port win11; win11.files = &demandFiles; win11.mode = Mode::Windows11X64;
        Controller c(demandStore, "taskbar.json", win11);
        c.SetAutoHideDemand(true);
        Check(c.Tick() && win11.autoHide && win11.starts == 0 && c.Snapshot().active &&
              !c.Snapshot().preferences.enabled,
              "demand alone hides the taskbar without starting transparency or enabling the module");
        c.SetAutoHideDemand(false);
        Check(c.Tick() && !win11.autoHide && !c.Snapshot().active && !c.Snapshot().pendingRecovery,
              "withdrawn demand restores the original visible taskbar");
        Preferences visible; visible.enabled = true; visible.autoHideTaskbar = false;
        c.SetAutoHideDemand(true);
        Check(c.Apply(visible) && win11.autoHide && win11.starts == 1,
              "demand overrides a visible-taskbar preference but keeps transparency");
        c.SetAutoHideDemand(false);
        Check(c.Tick() && !win11.autoHide && win11.core, "user preference applies again once demand ends");
        Files hiddenFiles; RecordStore hiddenStore(hiddenFiles);
        Port classic; classic.files = &hiddenFiles; classic.autoHide = true;
        Controller restored(hiddenStore, "taskbar.json", classic);
        restored.SetAutoHideDemand(true);
        Check(restored.Tick() && restored.Shutdown() && classic.autoHide,
              "shutdown keeps a taskbar the user had already auto-hidden");
    }
    {
        files.data["taskbar.json"] = "corrupt";
        Port p; p.files = &files; Controller c(store, "taskbar.json", p);
        Preferences enabled; enabled.enabled = true;
        Check(!c.Tick() && !c.Apply(enabled) && p.touches == 0,
              "invalid recovery journal fails closed without guessing original state");
    }
    {
        Files shared; RecordStore sharedStore(shared); Port peer; peer.files = &shared;
        Controller stale(sharedStore, "taskbar.json", peer);
        Record owned; owned.preferences.enabled = true; owned.ownsAutoHide = true;
        Check(sharedStore.Save("taskbar.json", owned), "another instance writes durable ownership");
        const auto active = shared.data.at("taskbar.json");
        peer.acquireError = Error::Busy;
        Preferences enabled; enabled.enabled = true;
        Check(!stale.Apply(enabled) && shared.data.at("taskbar.json") == active && peer.writes == 0,
              "busy peer cannot overwrite active ownership with stale preferences");
        peer.acquireError = Error::None; peer.autoHide = true;
        Check(stale.Apply(enabled) && stale.Journal().ownsAutoHide && !stale.Journal().originalAutoHide,
              "ownership acquisition reloads and recovers latest journal before capturing baseline");
        Check(stale.Shutdown() && !peer.autoHide, "recovered baseline remains intact");
    }
    {
        Files exitFiles; RecordStore exitStore(exitFiles); Port exitPort; exitPort.files = &exitFiles;
        Controller controller(exitStore, "taskbar.json", exitPort);
        Preferences visible; visible.enabled = true; visible.transparentTaskbar = false; visible.autoHideTaskbar = false;
        Check(controller.Apply(visible), "exit failure fixture owns a visible taskbar");
        auto disabled = visible; disabled.enabled = false;
        exitFiles.failWrite = true;
        Check(!controller.Shutdown(disabled) && controller.Snapshot().error == Error::Save,
              "exit cannot claim an unsaved preference was committed");
        Check(exitStore.Load("taskbar.json").record.preferences == visible,
              "failed exit save retains the previous complete settings");
        exitFiles.failWrite = false;
        Check(controller.Shutdown(disabled) && exitStore.Load("taskbar.json").record.preferences == disabled &&
              !HasRecovery(exitStore.Load("taskbar.json").record), "explicit exit retry commits and recovers ownership");
    }
    std::cout << "taskbar controller passed\n";
}
