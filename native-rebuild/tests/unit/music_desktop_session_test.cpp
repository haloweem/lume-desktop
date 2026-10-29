#include "apps/shell/music_desktop_session.h"
#include "core/widget/widget_preferences_store.h"
#include "platform/windows/music_desktop_controls.h"
#include "vendor/nlohmann/json.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

// Default target: link session.cpp + preferences/store + local_settings_file_store,
// WITHOUT music_desktop_controls.cpp. This link-time double never creates windows.
// Define LUME_MUSIC_DESKTOP_SESSION_REAL_CONTROLS and link real controls for a
// second verification-only run. Neither mode shows widgets or registers OS SMTC.
#ifndef LUME_MUSIC_DESKTOP_SESSION_REAL_CONTROLS
namespace {
struct FakeControls {
    lume::widget::Preferences prefs;
    lume::platform::windows::MusicDesktopControls::CommandHandler command;
    lume::platform::windows::MusicDesktopControls::WidgetHandler widget;
    bool visible = false, registered = false, failMedia = false;
    int shows = 0, enables = 0, updates = 0, shutdowns = 0;
    int failShow = 0, failConfigure = 0;
    HRESULT result = S_FALSE;
} fake;
}
namespace lume::platform::windows {
struct MusicDesktopControls::Impl {};
MusicDesktopControls::MusicDesktopControls(HINSTANCE, CommandHandler command, WidgetHandler widget, WakeHandler)
    : impl_(std::make_unique<Impl>()) {
    fake = {};
    fake.command = std::move(command);
    fake.widget = std::move(widget);
}
MusicDesktopControls::~MusicDesktopControls() = default;
void MusicDesktopControls::Update(const music::MusicSnapshot&) { ++fake.updates; }
bool MusicDesktopControls::Configure(const widget::Preferences& prefs) {
    if (fake.failConfigure > 0) { --fake.failConfigure; return false; }
    fake.prefs = prefs;
    return true;
}
bool MusicDesktopControls::Show() {
    ++fake.shows;
    if (fake.failShow > 0) { --fake.failShow; return false; }
    fake.visible = fake.prefs.enabled = true;
    return true;
}
void MusicDesktopControls::Hide() { fake.visible = fake.prefs.enabled = false; }
bool MusicDesktopControls::Visible() const { return fake.visible; }
widget::Preferences MusicDesktopControls::Bounds() const { return fake.prefs; }
HRESULT MusicDesktopControls::EnableSystemMediaControls() {
    ++fake.enables;
    fake.registered = !fake.failMedia;
    return fake.result = fake.failMedia ? E_FAIL : S_OK;
}
bool MusicDesktopControls::SystemMediaControlsEnabled() const { return fake.registered; }
HRESULT MusicDesktopControls::SystemMediaResult() const { return fake.result; }
void MusicDesktopControls::Shutdown() {
    ++fake.shutdowns;
    fake.visible = fake.registered = false;
}
} // namespace lume::platform::windows
#endif

namespace {
using Json = nlohmann::json;
using lume::shell::MusicDesktopSession;
using lume::shell::MusicDesktopSessionOptions;
using lume::platform::windows::MusicDesktopControls;
using lume::platform::windows::WidgetAction;
constexpr auto Path = "isolated/music-widget.json";
const std::wstring Snapshot = LR"({"version":1,"scope":"music-desktop","action":"snapshot"})";
const std::wstring Enable = LR"({"version":1,"scope":"music-desktop","action":"setEnabled","value":true})";
const std::wstring Disable = LR"({"version":1,"scope":"music-desktop","action":"setEnabled","value":false})";

void Expect(bool ok, const char* message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
Json State(const MusicDesktopSession& session) {
    const auto text = session.State();
    return Json::parse(text.begin(), text.end());
}
class Files final : public lume::settings::IFileStore {
public:
    bool Read(const std::string& path, std::string& out) const override {
        ++reads;
        const auto it = values.find(path);
        if (it == values.end()) return false;
        out = it->second;
        return true;
    }
    bool Exists(const std::string& path) const override {
        if (throwRead) throw std::runtime_error("private storage detail");
        return values.count(path) != 0;
    }
    bool Write(const std::string& path, const std::string& text) override {
        ++writes;
        if (throwWrite) throw std::runtime_error("private storage detail");
        if (failWrite) return false;
        values[path] = text;
        return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        if (failRename || !values.count(from)) return false;
        values[to] = values[from];
        values.erase(from);
        return true;
    }
    bool Remove(const std::string& path) override { values.erase(path); return true; }
    void Seed(bool enabled) {
        auto prefs = lume::widget::Defaults(MusicDesktopControls::Limits);
        prefs.enabled = enabled;
        Expect(lume::widget::Encode(prefs, values[Path], MusicDesktopControls::Limits), "seed");
    }
    lume::widget::Preferences Saved() const {
        lume::widget::Preferences prefs;
        Expect(lume::widget::Decode(values.at(Path), prefs, MusicDesktopControls::Limits), "decode saved");
        return prefs;
    }
    std::map<std::string, std::string> values;
    mutable int reads = 0;
    int writes = 0;
    bool failWrite = false, failRename = false, throwWrite = false, throwRead = false;
};

void Verification() {
    Files files;
    {
        int commands = 0;
        std::vector<std::wstring> notifications;
        const auto thread = GetCurrentThreadId();
        MusicDesktopSessionOptions options{Path, true, &files, [&](const auto& state) {
            notifications.push_back(state);
            Expect(GetCurrentThreadId() == thread, "notification stays on test thread");
        }};
        MusicDesktopSession session(GetModuleHandleW(nullptr), std::move(options),
            [&](const auto&) { ++commands; }, {});
        Expect(files.reads == 0 && files.writes == 0 && !session.Running(), "constructor is inert");
        Expect(State(session)["loadStatus"] == "not-restored", "explicit restore boundary");
        session.HandleMessage(Enable);
        Expect(State(session)["error"] == "not-restored" && files.writes == 0, "no implicit startup");
        Expect(session.Restore() && session.Restore(), "restore idempotent");
        Expect(State(session)["error"] == "" && !State(session)["enabled"].get<bool>(), "default disabled");
        Expect(State(session)["smtc"]["status"] == "verification", "verification status");
        Expect(notifications.size() == 2 && notifications.back() == session.State(),
               "not-restored error and restore each notify once");
        Expect(session.HandleMessage(Snapshot).has_value() && files.writes == 0, "snapshot is read only");
        Expect(notifications.size() == 2, "snapshot and repeated restore do not notify");
        session.HandleMessage(Enable);
        Expect(notifications.size() == 3 && Json::parse(notifications.back())["result"] == "snapshot" &&
               Json::parse(notifications.back())["saved"] == false, "notification is not command ACK");
        session.HandleMessage(Enable);
        Expect(notifications.size() == 3, "identical successful state does not notify");
        Expect(files.Saved().enabled && State(session)["enabled"] == true && !session.Running(),
               "verification persists enable but never runs");
        const auto original = files.values.at(Path);
        files.failWrite = true;
        auto reply = session.HandleMessage(Disable);
        Expect(reply && Json::parse(*reply)["saved"] == false, "failed acknowledgement");
        Expect(State(session)["error"] == "save" && State(session)["enabled"] == true &&
               files.values.at(Path) == original, "write failure rolls back");
        files.failWrite = false;
        files.failRename = true;
        session.HandleMessage(Disable);
        Expect(State(session)["error"] == "save" && files.values.at(Path) == original, "rename failure rolls back");
        files.failRename = false;
        files.throwWrite = true;
        session.HandleMessage(Disable);
        Expect(State(session)["error"] == "save", "storage exceptions contained");
        files.throwWrite = false;
        session.HandleMessage(Disable);
        Expect(!files.Saved().enabled && State(session)["error"] == "", "guard recovers after exception");

        lume::music::MusicSnapshot snapshot;
        snapshot.session = 7;
        snapshot.current = lume::music::Track{"id", "private title", "artist", "album",
                                             "C:/private/audio.mp3", "", 60, false, ""};
        session.Update(snapshot);
        const auto state = State(session);
        Expect(state.size() == 14 && state["smtc"].size() == 2 &&
               state["type"] == "music-desktop-state", "bounded public state schema");
        Expect(session.State().find(L"private") == std::wstring::npos &&
               session.State().find(L"isolated") == std::wstring::npos, "no source/path/metadata disclosure");
        Expect(commands == 0, "update and visibility never issue playback commands");
#ifndef LUME_MUSIC_DESKTOP_SESSION_REAL_CONTROLS
        Expect(fake.shows == 0 && fake.enables == 0, "verification never calls native activation");
#endif
        session.HandleMessage(Enable);
        const auto beforeStop = notifications.size();
        session.Shutdown();
        session.Shutdown();
        Expect(notifications.size() == beforeStop + 1 && notifications.back() == session.State(),
               "shutdown only notifies once");
        Expect(!session.Running() && !session.Restore() && files.Saved().enabled, "shutdown preserves enabled preference");
        Expect(!session.HandleMessage(Disable) && session.HandleMessage(Snapshot), "stop cannot resurrect");
        Expect(State(session)["smtc"]["status"] == "stopped", "stopped media state");
    }
    {
        MusicDesktopSession restored(GetModuleHandleW(nullptr), {Path, true, &files}, {}, {});
        Expect(restored.Restore() && State(restored)["enabled"] == true && !restored.Running(),
               "saved enable cannot bypass verification");
    }
    files.values[std::string(Path) + ".bak"] = files.values.at(Path);
    files.values[Path] = "{broken";
    {
        MusicDesktopSession session(GetModuleHandleW(nullptr), {Path, true, &files}, {}, {});
        Expect(session.Restore() && State(session)["loadStatus"] == "recovered", "backup recovery");
        Expect(files.values.at(Path) == "{broken", "restore is read-only");
        session.HandleMessage(Disable);
        Expect(files.values.at(std::string(Path) + ".corrupt") == "{broken", "save preserves corrupt evidence");
    }
    files.values = {{Path, "{broken"}};
    {
        MusicDesktopSession session(GetModuleHandleW(nullptr), {Path, true, &files}, {}, {});
        Expect(!session.Restore() && !session.Running() && State(session)["error"] == "load",
               "corrupt settings fail closed");
        session.HandleMessage(Enable);
        Expect(State(session)["error"] == "" && files.Saved().enabled, "explicit action repairs bad settings");
    }
    {
        files.throwRead = true;
        MusicDesktopSession session(GetModuleHandleW(nullptr), {Path, true, &files}, {}, {});
        Expect(!session.Restore() && State(session)["error"] == "load", "read exception is public-safe");
        files.throwRead = false;
    }
    for (const auto& path : {std::string{}, std::string("private\0path", 12)}) {
        MusicDesktopSession session(GetModuleHandleW(nullptr), {path, false, &files}, {}, {});
        const auto writes = files.writes;
        Expect(!session.Restore(), "invalid explicit path rejected");
        session.HandleMessage(Enable);
        Expect(State(session)["error"] == "path" && files.writes == writes && !session.Running(),
               "invalid path never falls back to real user settings or starts runtime");
#ifndef LUME_MUSIC_DESKTOP_SESSION_REAL_CONTROLS
        Expect(fake.enables == 0 && fake.shows == 0, "invalid path cannot create native resources");
#endif
    }
}

void Protocol() {
    Files files;
    MusicDesktopSession session(GetModuleHandleW(nullptr), {Path, true, &files}, {}, {});
    Expect(session.Restore(), "protocol restore");
    const auto before = session.State();
    const std::vector<std::wstring> invalid{
        L"", L"null", L"[]", L"{}", L"true", L"{",
        LR"({"version":1.0,"scope":"music-desktop","action":"snapshot"})",
        LR"({"version":true,"scope":"music-desktop","action":"snapshot"})",
        LR"({"version":2,"scope":"music-desktop","action":"snapshot"})",
        LR"({"version":18446744073709551616,"scope":"music-desktop","action":"snapshot"})",
        LR"({"version":1,"scope":"music","action":"snapshot"})",
        LR"({"version":1,"scope":"music-desktop","action":false})",
        LR"({"version":1,"scope":"music-desktop","action":"open"})",
        LR"({"version":1,"scope":"music-desktop","action":"snapshot","value":true})",
        LR"({"version":1,"scope":"music-desktop","action":"snapshot","extra":null})",
        LR"({"version":1,"scope":"music-desktop","action":"setEnabled"})",
        LR"({"version":1,"scope":"music-desktop","action":"setEnabled","value":1})",
        LR"({"version":1,"scope":"music-desktop","action":"setEnabled","value":"true"})",
        LR"({"version":1,"scope":"music-desktop","action":"setEnabled","value":null})",
        LR"({"version":1,"scope":"music-desktop","action":"setEnabled","value":{}})",
        LR"({"version":1,"scope":"music-desktop","action":"setEnabled","value":[]})",
        LR"({"version":1,"version":1,"scope":"music-desktop","action":"snapshot"})",
        LR"({"version":1,"scope":"music-desktop","action":"setEnabled","value":false,"value":true})",
        LR"({"version":1,"scope":"music-desktop","action":"setEnabled","value":true,"\u0076alue":false})",
        Snapshot + L"junk", Snapshot + L"{}", Snapshot + std::wstring(1, L'\0') + L"junk",
        std::wstring(1025, L' '), std::wstring(500, L'[') + std::wstring(500, L']'),
        std::wstring(1, static_cast<wchar_t>(0xD800))
    };
    for (const auto& message : invalid) {
        Expect(!session.HandleMessage(message), "malformed or unrelated message accepted");
        Expect(files.writes == 0 && session.State() == before, "invalid message had side effects");
    }
    Expect(session.HandleMessage(L" \n" + Snapshot + L"\t ").has_value(), "JSON whitespace");
    Expect(session.HandleMessage(
        LR"({"action":"setEnabled","value":true,"scope":"music-desktop","version":1})").has_value(),
        "field order independent");
    Expect(files.Saved().enabled, "valid protocol persists");
}

#ifndef LUME_MUSIC_DESKTOP_SESSION_REAL_CONTROLS
void Emit(WidgetAction action, const lume::widget::Preferences& value) {
    fake.prefs = value;
    if (action == WidgetAction::Close) fake.visible = false;
    const auto callback = fake.widget;
    callback(action, value);
}

void NativeLifecycle() {
    Files files;
    int settings = 0, commands = 0;
    std::vector<std::wstring> notifications;
    {
        MusicDesktopSessionOptions options{Path, false, &files, [&](const auto& state) {
            notifications.push_back(state);
        }};
        MusicDesktopSession session(GetModuleHandleW(nullptr), std::move(options),
            [&](const lume::ui::MusicPageCommand& command) {
                Expect(command.session == 42 && command.id == "track", "identity forwarded");
                ++commands;
            }, [&] { ++settings; });
        Expect(session.Restore() && fake.enables == 1 && fake.shows == 0, "production enables media only");
        Expect(session.Restore() && fake.enables == 1, "single SMTC registration");
        Expect(State(session)["smtc"]["registered"] == true, "registered status");
        session.HandleMessage(Enable);
        Expect(session.Running() && files.Saved().enabled, "explicit show");
        auto prefs = fake.prefs;
        prefs.locked = true;
        Emit(WidgetAction::Lock, prefs);
        Expect(files.Saved().locked, "already applied lock is not toggled twice");
        Expect(notifications.back() == session.State() && Json::parse(notifications.back())["locked"] == true,
               "native lock notifies committed state");
        prefs.frosted = true;
        Emit(WidgetAction::Frosted, prefs);
        Expect(files.Saved().frosted, "native material saved");
        prefs.x = -420; prefs.y = 80; prefs.width = 400; prefs.height = 190; prefs.placed = true;
        Emit(WidgetAction::Bounds, prefs);
        Expect(files.Saved().x == -420 && files.Saved().width == 400 && files.Saved().placed, "bounds saved");
        const auto committed = files.values.at(Path);
        files.failWrite = true;
        prefs.locked = false;
        Emit(WidgetAction::Lock, prefs);
        Expect(fake.prefs.locked && files.values.at(Path) == committed, "native appearance rolled back");
        prefs.enabled = false;
        Emit(WidgetAction::Close, prefs);
        Expect(session.Running() && State(session)["enabled"] == true && fake.registered,
               "failed native close persistence reopens prior runtime");
        Expect(notifications.back() == session.State() && Json::parse(notifications.back())["error"] == "save",
               "failed close notifies rollback result");
        files.failWrite = false;
        prefs = lume::widget::Defaults(MusicDesktopControls::Limits);
        prefs.enabled = true;
        Emit(WidgetAction::Reset, prefs);
        Expect(!files.Saved().locked && files.Saved().frosted && files.Saved().width == 320,
               "native reset persisted with the frosted default");
        const auto writes = files.writes;
        Emit(WidgetAction::Settings, prefs);
        Emit(WidgetAction::Refresh, prefs);
        Expect(settings == 1 && files.writes == writes, "settings callback only");
        lume::ui::MusicPageCommand command;
        command.action = lume::ui::MusicAction::Next; command.id = "track"; command.session = 42;
        fake.command(command);
        Expect(commands == 1, "playback callback forwarded");
        prefs.enabled = false;
        Emit(WidgetAction::Close, prefs);
        Expect(!session.Running() && !files.Saved().enabled && fake.registered,
               "close hides widget without disabling SMTC");
        Expect(notifications.back() == session.State() &&
               Json::parse(notifications.back())["enabled"] == false, "native close updates page toggle");
        fake.failShow = 1;
        const auto old = files.values.at(Path);
        session.HandleMessage(Enable);
        Expect(State(session)["error"] == "runtime" && files.values.at(Path) == old && !session.Running(),
               "native show failure does not save");
        session.HandleMessage(Enable);
        files.failWrite = true;
        fake.failShow = 1;
        session.HandleMessage(Disable);
        Expect(State(session)["error"] == "rollback" && State(session)["enabled"] == true && !session.Running(),
               "rollback failure exposes actual runtime separately");
        files.failWrite = false;
        session.HandleMessage(Enable);
        const auto lateCommand = fake.command;
        session.Shutdown();
        session.Shutdown();
        lateCommand(command);
        session.Update({});
        Expect(commands == 1 && fake.shutdowns == 1 && fake.updates == 0 && !fake.registered,
               "shutdown idempotent and late work suppressed");
    }
    {
        MusicDesktopSession session(GetModuleHandleW(nullptr), {Path, false, &files}, {}, {});
        Expect(session.Restore() && session.Running(), "production restores explicitly saved enable");
    }
    {
        files.Seed(false);
        MusicDesktopSession session(GetModuleHandleW(nullptr), {Path, false, &files}, {}, {});
        fake.failMedia = true;
        Expect(!session.Restore() && State(session)["smtc"]["status"] == "failed", "SMTC failure reported");
        session.HandleMessage(Enable);
        Expect(session.Running() && State(session)["error"] == "smtc", "widget survives media registration failure");
    }
    {
        MusicDesktopSession* current = nullptr;
        int changed = 0;
        MusicDesktopSession session(GetModuleHandleW(nullptr), {Path, false, &files, [&](const auto&) {
            ++changed;
            Expect(current->HandleMessage(Snapshot).has_value(), "notification permits state read");
            throw std::runtime_error("ignored observer failure");
        }}, {}, {});
        current = &session;
        Expect(session.Restore() && changed == 1, "observer failure cannot undo restore");
        session.HandleMessage(Disable);
        Expect(changed == 2 && State(session)["error"] == "", "observer failure cannot undo save");
        session.Shutdown();
        Expect(changed == 3, "explicit shutdown notifies live observer");
    }
    const auto count = notifications.size();
    {
        MusicDesktopSession session(GetModuleHandleW(nullptr), {Path, true, &files,
            [&](const auto& state) { notifications.push_back(state); }}, {}, {});
    }
    Expect(notifications.size() == count, "destructor does not call parent observer");
}
#else
BOOL CALLBACK NoNativeWindow(HWND window, LPARAM count) {
    wchar_t name[128]{};
    GetClassNameW(window, name, 128);
    if (std::wstring(name) == L"LumeMusicMediaDispatch" || std::wstring(name) == L"LumeNativeDesktopWidget")
        ++*reinterpret_cast<int*>(count);
    return TRUE;
}
#endif
} // namespace

int main() {
    Verification();
    Protocol();
#ifndef LUME_MUSIC_DESKTOP_SESSION_REAL_CONTROLS
    NativeLifecycle();
    std::cout << "music desktop session: protocol, persistence, native lifecycle double passed\n";
#else
    int windows = 0;
    EnumThreadWindows(GetCurrentThreadId(), NoNativeWindow, reinterpret_cast<LPARAM>(&windows));
    Expect(windows == 0, "verification leaked native widget/media host");
    std::cout << "music desktop session: real controls verification-only passed (no Show/SMTC)\n";
#endif
}
