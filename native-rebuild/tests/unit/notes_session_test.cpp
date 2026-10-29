#include "apps/shell/notes_session.h"
#include "vendor/nlohmann/json.hpp"
#include <objbase.h>
#include <cstdlib>
#include <iostream>
#include <map>

namespace {
using Json = nlohmann::json;
void Expect(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
class Files : public lume::settings::IFileStore {
  public:
    bool Read(const std::string &p, std::string &out) const override {
        auto i = values.find(p);
        if (i == values.end())
            return false;
        out = i->second;
        return true;
    }
    bool Exists(const std::string &p) const override { return values.count(p) != 0; }
    bool Write(const std::string &p, const std::string &text) override {
        if (fail)
            return false;
        values[p] = text;
        return true;
    }
    bool Rename(const std::string &a, const std::string &b) override {
        if (fail || !values.count(a))
            return false;
        values[b] = values[a];
        values.erase(a);
        return true;
    }
    bool Remove(const std::string &p) override {
        values.erase(p);
        return true;
    }
    std::map<std::string, std::string> values;
    bool fail = false;
};
std::wstring Command(Json fields) {
    fields["version"] = 1;
    fields["scope"] = "notes";
    const auto text = fields.dump();
    std::wstring out;
    // The page sends UTF-16; build it from UTF-8 via the Windows converter.
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    out.resize(static_cast<std::size_t>(count));
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}
Json Parse(const std::wstring &reply) {
    std::string ascii;
    for (const auto c : reply) ascii.push_back(static_cast<char>(c));
    return Json::parse(ascii);
}
} // namespace

int main() {
    using namespace lume;
    Expect(notes::CharacterCount(u8"你好 world\n") == 7, "non-whitespace code points are counted");
    Expect(notes::Preview(u8"第一行\n\n  第二行", 80) == u8"第一行 第二行", "preview collapses whitespace");
    Expect(notes::Preview(u8"中文中文", 2) == u8"中文", "preview keeps whole code points");
    Expect(!notes::ValidUtf8(std::string("\xc3", 1)) && notes::ValidUtf8(u8"😀"), "strict UTF-8");
    notes::NotesState sample;
    sample.notes.push_back({"n-a", u8"标题", u8"正文\n第二行", 1, 2});
    sample.pinnedId = "n-a";
    notes::NotesState decoded;
    Expect(notes::Decode(notes::Encode(sample), decoded) && decoded.notes.size() == 1 &&
               decoded.notes[0].body == sample.notes[0].body && decoded.pinnedId == "n-a",
           "encode and decode round trip");
    Expect(!notes::Decode(R"({"version":1,"notes":[{"id":"BAD","title":"","body":"","createdAt":0,"updatedAt":0}]})", decoded),
           "invalid ids are rejected");

    Expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM");
    Files files;
    notes::Millis clock = 1'790'000'000'000;
    shell::NotesSessionOptions options{"notes.json", "notes-widget.json", true, &files, {}, [&] { return clock; }};
    {
        shell::NotesSession session(GetModuleHandleW(nullptr), options);
        Expect(session.Restore(), "fresh restore");
        auto state = Parse(session.HandleMessage(Command({{"action", "snapshot"}}))->front());
        Expect(state["type"] == "notes-state" && state["notes"].empty() && state["loadStatus"] == "fresh", "fresh snapshot");
        Expect(!session.HandleMessage(Command({{"action", "snapshot"}, {"extra", 1}})), "unknown fields are ignored entirely");
        Expect(!session.HandleMessage(L"{\"version\":1,\"scope\":\"music\",\"action\":\"snapshot\"}"), "other scopes untouched");

        auto created = *session.HandleMessage(Command({{"action", "create"}}));
        Expect(created.size() == 2, "create replies with the note and the list");
        const auto note = Parse(created[0]);
        const std::string id = note["id"];
        Expect(note["type"] == "notes-note" && notes::ValidId(id) && Parse(created[1])["savedId"] == id, "new note id");
        Expect(files.values.count("notes.json") == 1, "create is persisted");

        // A two-part save: the first part is acknowledged, the last commits.
        clock += 5000;
        auto first = *session.HandleMessage(Command({{"action", "save"}, {"id", id}, {"title", u8"治愈心情"},
                                                     {"part", u8"1. 我从没觉得孤独\n"}, {"index", 0}, {"count", 2}}));
        Expect(Parse(first.front())["type"] == "notes-chunk", "intermediate part acknowledged");
        Expect(session.Snapshot().notes[0].body.empty(), "nothing is written before the last part");
        auto last = *session.HandleMessage(Command({{"action", "save"}, {"id", id}, {"title", u8"治愈心情"},
                                                    {"part", u8"2. 夕阳总会落在你身上 😀"}, {"index", 1}, {"count", 2}}));
        auto saved = Parse(last.front());
        Expect(saved["result"] == "saved" && saved["saved"] == true && saved["savedId"] == id, "save committed");
        Expect(session.Snapshot().notes[0].body == u8"1. 我从没觉得孤独\n2. 夕阳总会落在你身上 😀" &&
                   session.Snapshot().notes[0].updatedAt == clock,
               "parts joined in order");
        Expect(saved["notes"][0]["chars"] == 21, "list carries the character count");
        Expect(files.values.at("notes.json").find("\\u6cbb") != std::string::npos ||
                   files.values.at("notes.json").find(u8"治愈") != std::string::npos,
               "document stored");

        auto skipped = Parse(session.HandleMessage(Command({{"action", "save"}, {"id", id}, {"title", "x"}, {"part", "y"},
                                                            {"index", 1}, {"count", 2}}))->front());
        Expect(skipped["saved"] == false && skipped["error"] == "invalid", "out-of-order part rejected");

        const auto before = files.values.at("notes.json");
        files.fail = true;
        auto failed = Parse(session.HandleMessage(Command({{"action", "save"}, {"id", id}, {"title", "new"},
                                                           {"part", "changed"}, {"index", 0}, {"count", 1}}))->front());
        Expect(failed["saved"] == false && failed["error"] == "save" && session.Snapshot().notes[0].title == u8"治愈心情" &&
                   files.values.at("notes.json") == before,
               "failed write keeps the committed note and file");
        files.fail = false;

        auto opened = Parse(session.HandleMessage(Command({{"action", "open"}, {"id", id}}))->front());
        Expect(opened["type"] == "notes-note" && opened["title"] == u8"治愈心情", "open returns the full note");
        auto pinned = Parse(session.HandleMessage(Command({{"action", "pin"}, {"id", id}, {"value", true}}))->front());
        Expect(pinned["pinnedId"] == id && pinned["enabled"] == true, "showing a note on the desktop enables the desktop note");
        auto unpinned = Parse(session.HandleMessage(Command({{"action", "pin"}, {"id", id}, {"value", false}}))->front());
        Expect(unpinned["pinnedId"] == "" && unpinned["enabled"] == false, "taking it off the desktop hides the desktop note");
        auto windowState = Parse(session.HandleMessage(Command({{"action", "openWindow"}}))->front());
        Expect(windowState["windowOpen"] == false && !session.Active(), "verification never opens the notes window");
        Expect(session.HandleMessage(Command({{"action", "setActive"}, {"value", false}})).has_value(), "card toggle accepted");
        Expect(!session.Running(), "verification never shows the widget");
        auto removed = Parse(session.HandleMessage(Command({{"action", "delete"}, {"id", id}}))->front());
        Expect(removed["notes"].empty() && removed["pinnedId"] == "", "delete removes the note and its pin");
    }
    {
        Files desktopFiles;
        auto desktopOptions = options;
        desktopOptions.files = &desktopFiles;
        shell::NotesSession session(GetModuleHandleW(nullptr), desktopOptions);
        Expect(session.Restore(), "desktop fresh restore");
        auto created = session.HandleMessage(Command({{"action", "createDesktop"}}));
        Expect(created.has_value(), "context menu can create a desktop note");
        const std::string firstId = Parse(created->front())["id"];
        auto second = session.HandleMessage(Command({{"action", "createDesktop"}}));
        const std::string secondId = Parse(second->front())["id"];
        const auto savePart = [&](const std::string &id, const char *part, int index) {
            return session.HandleMessage(Command({{"action", "save"}, {"id", id}, {"title", "parallel"},
                                                  {"part", part}, {"index", index}, {"count", 2}}));
        };
        savePart(firstId, "first-", 0);
        savePart(secondId, "second-", 0);
        Expect(Parse(savePart(firstId, "body", 1)->front())["saved"] == true &&
               Parse(savePart(secondId, "body", 1)->front())["saved"] == true, "interleaved note chunks both commit");
        Expect(notes::Find(session.Snapshot(), firstId)->body == "first-body" &&
               notes::Find(session.Snapshot(), secondId)->body == "second-body", "chunks never cross between notes");
        auto stale = session.HandleMessage(Command({{"action", "save"}, {"id", firstId}, {"title", "stale"},
            {"part", "must not overwrite"}, {"index", 0}, {"count", 1}, {"baseUpdatedAt", clock}}));
        Expect(stale && Parse(stale->back())["error"] == "conflict" && notes::Find(session.Snapshot(), firstId)->body == "first-body",
               "stale editor cannot silently overwrite another window");
        auto state = Parse(session.State());
        Expect(state["desktopIds"].size() == 2 && state["notes"].size() == 2 && state["windowOpen"] == false,
               "new desktop notes coexist without opening manager");
        session.HandleMessage(Command({{"action", "pin"}, {"id", firstId}, {"value", false}}));
        state = Parse(session.State());
        Expect(state["desktopIds"].size() == 1 && state["desktopIds"][0] == secondId && state["notes"].size() == 2,
               "hiding one note preserves other window and both documents");
        shell::NotesSession restored(GetModuleHandleW(nullptr), desktopOptions);
        Expect(restored.Restore() && Parse(restored.State())["desktopIds"] == state["desktopIds"], "desktop membership survives restart");
        desktopFiles.fail = true;
        auto failedCreate = session.HandleMessage(Command({{"action", "createDesktop"}}));
        Expect(Parse(failedCreate->back())["saved"] == false && session.Snapshot().notes.size() == 2,
               "failed desktop create preserves documents and window membership");
        desktopFiles.fail = false;
        session.HandleMessage(Command({{"action", "delete"}, {"id", secondId}}));
        Expect(Parse(session.State())["desktopIds"].empty(), "delete also removes corresponding desktop window");
    }
    {
        auto geometry = widget::Defaults(notes::DesktopLimits);
        geometry.enabled = true; geometry.placed = true; geometry.x = -100; geometry.y = 320;
        geometry.width = 360; geometry.height = 420; geometry.locked = true; geometry.frosted = false;
        sample.desktopLayoutKnown = true;
        sample.desktopNotes.push_back({"n-a", geometry});
        Expect(notes::Decode(notes::Encode(sample), decoded) && decoded.desktopNotes.size() == 1 &&
               decoded.desktopNotes[0].preferences.x == -100 && decoded.desktopNotes[0].preferences.height == 420 &&
               decoded.desktopNotes[0].preferences.locked && !decoded.desktopNotes[0].preferences.frosted,
               "each desktop note retains geometry and material");
        auto invalid = Json::parse(notes::Encode(sample));
        invalid["desktopNotes"].push_back(invalid["desktopNotes"][0]);
        Expect(!notes::Decode(invalid.dump(), decoded), "duplicate desktop note ids rejected");
        invalid["desktopNotes"].erase(1);
        invalid["desktopNotes"][0]["id"] = "missing";
        Expect(!notes::Decode(invalid.dump(), decoded), "orphan desktop note ids rejected");
        Files legacyFiles;
        sample.desktopLayoutKnown = false;
        legacyFiles.values["notes.json"] = notes::Encode(sample);
        std::string widgetJson;
        widget::Encode(geometry, widgetJson, notes::DesktopLimits);
        legacyFiles.values["notes-widget.json"] = widgetJson;
        auto legacyOptions = options; legacyOptions.files = &legacyFiles;
        shell::NotesSession legacy(GetModuleHandleW(nullptr), legacyOptions);
        Expect(legacy.Restore() && legacy.Snapshot().desktopNotes.size() == 1 &&
               legacy.Snapshot().desktopNotes[0].id == "n-a" && legacy.Snapshot().desktopNotes[0].preferences.locked,
               "legacy pinned widget migrates without losing note or position");
    }
    {
        files.values["notes.json"] = "{broken";
        shell::NotesSession session(GetModuleHandleW(nullptr), options);
        Expect(!session.Restore(), "corrupt document reported");
        auto state = Parse(session.HandleMessage(Command({{"action", "create"}}))->back());
        Expect(state["loadStatus"] == "failed" && state["error"] == "load" && files.values.at("notes.json") == "{broken",
               "an unreadable document is never overwritten");
    }
    std::cout << "NOTES_SESSION_PASS\n";
    CoUninitialize();
    return 0;
}
