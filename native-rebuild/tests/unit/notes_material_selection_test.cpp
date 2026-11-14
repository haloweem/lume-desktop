// Production NotesSession with a link-time window double. Only hidden message
// HWNDs are created: no WebView, user files, network or system consent.
#include "apps/shell/notes_session.h"
#include "platform/windows/notes/notes_page_window.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <utility>

namespace {
struct FakeWindow {
    HWND handle = nullptr;
    lume::platform::windows::NotesPageOptions options;
    std::function<void()> closed;
};
std::map<HWND, FakeWindow*> fixtureWindows;
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
FakeWindow* Find(const std::string& id) {
    for (const auto& [handle, window] : fixtureWindows)
        if (IsWindow(handle) && window->options.noteId == id) return window;
    return nullptr;
}
class Files final : public lume::settings::IFileStore {
public:
    bool Read(const std::string& path, std::string& out) const override {
        const auto item = values.find(path); if (item == values.end()) return false;
        out = item->second; return true;
    }
    bool Exists(const std::string& path) const override { return values.count(path) != 0; }
    bool Write(const std::string& path, const std::string& value) override {
        if (fail) return false; values[path] = value; ++writes; return true;
    }
    bool Rename(const std::string& from, const std::string& to) override {
        const auto found = values.find(from);
        if (fail || found == values.end()) return false;
        values[to] = found->second; values.erase(found); return true;
    }
    bool Remove(const std::string& path) override { values.erase(path); return true; }
    std::map<std::string, std::string> values;
    bool fail = false; unsigned writes = 0;
};
}

namespace lume::platform::windows {
struct NotesPageWindow::State { FakeWindow fake; };
NotesPageWindow::NotesPageWindow() : state_(std::make_shared<State>()) {}
NotesPageWindow::~NotesPageWindow() { Close(); }
bool NotesPageWindow::Open(HINSTANCE instance, MessageHandler, std::function<void()> closed,
    const std::wstring&, NotesPageOptions options) {
    state_->fake.options = std::move(options); state_->fake.closed = std::move(closed);
    if (state_->fake.handle) return true;
    const auto handle = CreateWindowExW(0,L"STATIC",L"notes-material-fixture",0,0,0,0,0,
        HWND_MESSAGE,nullptr,instance,nullptr);
    state_->fake.handle = handle;
    if (handle) fixtureWindows[handle] = &state_->fake;
    return handle != nullptr;
}
void NotesPageWindow::Configure(const widget::Preferences& prefs) { state_->fake.options.preferences = prefs; }
widget::Preferences NotesPageWindow::Bounds() const { return state_->fake.options.preferences; }
void NotesPageWindow::RequestClose() {
    const auto closed = state_->fake.closed; Close(); if (closed) closed();
}
void NotesPageWindow::CancelClose() {}
void NotesPageWindow::Close() {
    if (const auto handle = std::exchange(state_->fake.handle, nullptr)) {
        fixtureWindows.erase(handle); DestroyWindow(handle);
    }
}
void NotesPageWindow::Post(const std::wstring&) {}
bool NotesPageWindow::IsOpen() const { return IsWindow(state_->fake.handle) != FALSE; }
HWND NotesPageWindow::Handle() const { return state_->fake.handle; }
}

int main() {
    using namespace lume;
    Files files;
    notes::NotesState sample;
    sample.notes = {{"n-first","First","first-body",1,2},{"n-second","Second","second-body",2,3}};
    sample.pinnedId = "n-second"; sample.desktopLayoutKnown = true;
    auto prefs = widget::Defaults(notes::DesktopLimits);
    prefs.enabled = true; prefs.placed = true; prefs.frosted = true;
    sample.desktopNotes = {{"n-first",prefs},{"n-second",prefs}};
    files.values["notes.json"] = notes::Encode(sample);
    Expect(widget::Encode(prefs,files.values["widget.json"],notes::DesktopLimits),"seed widget preferences");
    shell::NotesSession session(GetModuleHandleW(nullptr),{"notes.json","widget.json",false,&files});
    Expect(session.Restore() && session.Running() && Find("n-first") && Find("n-second"),
        "restore two independently identified desktop notes");
    unsigned requests = 0; shell::NativeMaterialRequest selected;
    session.SetMaterialSelectionHandler([&](auto request) { ++requests; selected = std::move(request); });
    const auto before = files.values.at("notes.json"); const auto writes = files.writes;
    Find("n-second")->options.menu(8);
    Expect(requests == 1 && selected.target == shell::NativeMaterialTarget::Note && selected.id == "n-second" &&
        selected.source == Find("n-second")->handle && !selected.frosted &&
        files.values.at("notes.json") == before && files.writes == writes,
        "menu issues precise note/material intent without applying or saving it");
    auto moved = Find("n-second")->options.preferences;
    moved.x = -420; moved.y = 160; moved.width = 370; moved.locked = true;
    Find("n-second")->options.boundsChanged(moved);
    Expect(session.HandleMaterialSelection(selected).has_value(),"replay reaches the selected note");
    const auto read = [&](const std::string& id) -> const widget::Preferences& {
        const auto& items = session.Snapshot().desktopNotes;
        const auto found = std::find_if(items.begin(),items.end(),[&](const auto& note) { return note.id == id; });
        Expect(found != items.end(),"note layout remains present"); return found->preferences;
    };
    Expect(!read("n-second").frosted && read("n-second").x == -420 && read("n-second").width == 370 &&
        read("n-second").locked && read("n-first").frosted && read("n-first").x == prefs.x && requests == 1,
        "material replay preserves latest target geometry/lock and every other note");
    const auto committed = files.values.at("notes.json"); files.fail = true; selected.frosted = true;
    Expect(session.HandleMaterialSelection(selected).has_value() && !read("n-second").frosted &&
        files.values.at("notes.json") == committed && !Find("n-second")->options.preferences.frosted,
        "failed save preserves committed note material and its window");
    files.fail = false;
    auto wrong = selected; wrong.id = "n-first";
    Expect(!session.HandleMaterialSelection(wrong),"note identity and window must match together");
    wrong = selected; wrong.target = shell::NativeMaterialTarget::MusicDesktop;
    Expect(!session.HandleMaterialSelection(wrong),"foreign material target rejected");
    wrong = selected; wrong.id = "n-missing";
    Expect(!session.HandleMaterialSelection(wrong),"deleted or unknown note is not recreated by replay");
    session.SetMaterialSelectionHandler({});
    Find("n-first")->options.menu(8);
    Expect(!read("n-first").frosted && requests == 1,"no-host isolated session preserves local menu behavior");
    session.Shutdown();
    Expect(!session.HandleMaterialSelection(selected) && fixtureWindows.empty(),"closed session releases sources and rejects delayed choices");
    std::cout << "PASS notes material intent, exact target, latest preferences, save failure and source teardown\n";
}
