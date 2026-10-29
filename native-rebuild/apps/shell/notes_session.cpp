#include "apps/shell/notes_session.h"
#include "core/widget/widget_preferences_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/notes/notes_page_window.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <set>
#include <map>
#include <utility>

namespace lume::shell {
using namespace platform::windows;
namespace {
using Json = nlohmann::json;
constexpr auto Limits = notes::DesktopLimits;
// One save is sent in parts so each web message stays below the host's 8 KB limit.
constexpr int MaxParts = 2000;

notes::Millis SystemNow() {
    FILETIME file{};
    GetSystemTimeAsFileTime(&file);
    const auto ticks = (static_cast<std::uint64_t>(file.dwHighDateTime) << 32) | file.dwLowDateTime;
    return static_cast<notes::Millis>(ticks / 10000ULL - 11644473600000ULL);
}
std::wstring Ascii(const std::string &json) { return {json.begin(), json.end()}; }
struct Message {
    std::string action, id, title, part;
    int index = 0, count = 0;
    bool value = false;
    std::optional<notes::Millis> baseUpdatedAt;
};
std::optional<Message> Parse(const std::wstring &input) {
    if (input.empty() || input.size() > 8192 || input.find(L'\0') != std::wstring::npos)
        return {};
    try {
        struct Invalid {};
        const auto json = Json::parse(input.begin(), input.end(), [](int depth, Json::parse_event_t event, Json &) {
            if (event == Json::parse_event_t::array_start ||
                (event == Json::parse_event_t::object_start && depth != 0))
                throw Invalid{};
            return true;
        });
        if (!json.is_object() || json.value("version", 0) != 1 || json.value("scope", std::string{}) != "notes" ||
            !json.contains("action") || !json["action"].is_string())
            return {};
        Message m;
        m.action = json["action"].get<std::string>();
        const auto exact = [&](std::initializer_list<const char *> keys) {
            if (json.size() != keys.size() + 3)
                return false;
            for (const auto *key : keys)
                if (!json.contains(key))
                    return false;
            return true;
        };
        const auto text = [&](const char *key, std::string &out) {
            if (!json[key].is_string())
                return false;
            out = json[key].get<std::string>();
            return true;
        };
        if (m.action == "snapshot" || m.action == "create" || m.action == "createDesktop" || m.action == "openWindow")
            return exact({}) ? std::optional<Message>(m) : std::nullopt;
        if (m.action == "open" || m.action == "delete")
            return exact({"id"}) && text("id", m.id) && notes::ValidId(m.id) ? std::optional<Message>(m)
                                                                                : std::nullopt;
        if (m.action == "pin")
            return exact({"id", "value"}) && text("id", m.id) && notes::ValidId(m.id) && json["value"].is_boolean() &&
                           ((m.value = json["value"].get<bool>()), true)
                       ? std::optional<Message>(m)
                       : std::nullopt;
        if (m.action == "setEnabled" || m.action == "setActive")
            return exact({"value"}) && json["value"].is_boolean() && ((m.value = json["value"].get<bool>()), true)
                       ? std::optional<Message>(m)
                       : std::nullopt;
        if (m.action == "save") {
            const bool hasBase = json.contains("baseUpdatedAt");
            if (!(hasBase ? exact({"id", "title", "part", "index", "count", "baseUpdatedAt"}) : exact({"id", "title", "part", "index", "count"})) || !text("id", m.id) || !notes::ValidId(m.id) ||
                !text("title", m.title) || !text("part", m.part) || !json["index"].is_number_integer() ||
                !json["count"].is_number_integer())
                return {};
            if (hasBase) {
                if (!json["baseUpdatedAt"].is_number_integer()) return {};
                m.baseUpdatedAt = json["baseUpdatedAt"].get<notes::Millis>();
                if (*m.baseUpdatedAt < 0 || *m.baseUpdatedAt > 253402300799999LL) return {};
            }
            m.index = json["index"].get<int>();
            m.count = json["count"].get<int>();
            if (m.count < 1 || m.count > MaxParts || m.index < 0 || m.index >= m.count)
                return {};
            return m;
        }
    } catch (...) {
    }
    return {};
}
} // namespace

struct NotesSession::Impl {
    Impl(HINSTANCE instance, NotesSessionOptions value)
        : instance(instance), options(std::move(value)),
          files(options.files ? *options.files : localFiles), store(files, options.notesPath),
          widgetStore(files, Limits), now(options.now ? options.now : std::function<notes::Millis()>(SystemNow)) {}
    ~Impl() {
        options.notify = {};
        Shutdown();
    }

    void Notify() {
        if (stopped || applying || !options.notify)
            return;
        const auto callback = options.notify;
        try {
            callback(State());
        } catch (...) {
        }
    }
    struct DesktopWindow {
        NotesPageWindow window;
        bool preserve = false;
    };
    std::map<std::string, std::unique_ptr<DesktopWindow>> desktops;
    void Broadcast(const std::string &source = {}, const notes::Note *changed = nullptr) {
        const auto snapshot = State();
        const auto update = changed ? NoteMessage(*changed, "notes-update") : std::wstring{};
        if (source != "manager") { page.Post(snapshot); if (changed) page.Post(update); }
        for (auto &[id, entry] : desktops) if (source != id) {
            entry->window.Post(snapshot);
            if (changed && id == changed->id) entry->window.Post(update);
        }
        Notify();
    }
    void Dispatch(const std::wstring &input, const std::string &source, NotesPageWindow &target) {
        if (const auto replies = HandleMessage(input, source)) {
            for (const auto &reply : *replies) target.Post(reply);
            const auto message = Parse(input);
            const auto note = message && message->action == "save" && !replies->empty() &&
                Json::parse(replies->back()).value("saved", false) ? notes::Find(state, message->id) : nullptr;
            Broadcast(source, note);
        }
    }
    void DesktopClosed(const std::string &id) {
        auto found = desktops.find(id);
        if (stopped || applying || found == desktops.end()) return;
        if (found->second->preserve) { Broadcast(); return; }
        // Keep the host entry alive until the next session teardown; its callback
        // is currently executing. A later pin can safely reopen the same host.
        auto candidate = state;
        auto &items = candidate.desktopNotes;
        items.erase(std::remove_if(items.begin(), items.end(), [&](const auto &item) { return item.id == id; }), items.end());
        candidate.pinnedId = items.empty() ? std::string{} : items.back().id;
        Commit(std::move(candidate));
        if (state.desktopNotes.empty()) { auto next = prefs; next.enabled = false; Apply(next); }
        Broadcast();
    }
    void DesktopPreferences(const std::string &id, widget::Preferences value) {
        if (stopped || applying || !widget::Valid(value, Limits)) return;
        auto candidate = state;
        auto found = std::find_if(candidate.desktopNotes.begin(), candidate.desktopNotes.end(), [&](const auto &item) { return item.id == id; });
        if (found == candidate.desktopNotes.end()) return;
        found->preferences = value;
        if (Commit(std::move(candidate))) Runtime(prefs);
        Broadcast();
    }
    void DesktopMenu(const std::string &id, UINT command) {
        auto found = desktops.find(id);
        if (stopped || found == desktops.end()) return;
        if (command == 1) { found->second->window.RequestClose(); return; }
        if (command == 11) { OpenWindow(); return; }
        if (command == 3) {
            HandleMessage(L"{\"version\":1,\"scope\":\"notes\",\"action\":\"createDesktop\"}", id);
            Broadcast(); return;
        }
        auto value = found->second->window.Bounds();
        if (command == 8) value.frosted = false;
        else if (command == 9) value.frosted = true;
        else if (command == 12) value.locked = !value.locked;
        else if (command == 10) {
            const auto x = value.x, y = value.y;
            value = widget::Defaults(Limits); value.enabled = true; value.placed = true; value.x = x; value.y = y;
        } else return;
        DesktopPreferences(id, value);
    }
    bool Runtime(const widget::Preferences &value, const std::string &editId = {}) {
        bool ok = true;
        for (auto &[id, entry] : desktops) {
            const auto found = std::find_if(state.desktopNotes.begin(), state.desktopNotes.end(), [&](const auto &item) { return item.id == id; });
            if (found == state.desktopNotes.end()) {
                entry->preserve = true;
                if (notes::Find(state, id)) entry->window.RequestClose(); else entry->window.Close();
            }
            else if (!value.enabled || options.verification) { entry->preserve = true; entry->window.RequestClose(); }
        }
        if (!value.enabled || options.verification) return true;
        // Opening a WebView can pump messages; iterate a stable layout copy.
        const auto layout = state.desktopNotes;
        for (const auto &item : layout) {
            auto &entry = desktops[item.id];
            if (!entry) entry = std::make_unique<DesktopWindow>();
            entry->preserve = false;
            if (entry->window.IsOpen()) { entry->window.CancelClose(); entry->window.Configure(item.preferences); continue; }
            NotesPageOptions config;
            config.noteId = item.id; config.preferences = item.preferences; config.edit = item.id == editId || item.id == editOnOpen;
            config.menu = [this, id = item.id](UINT command) { DesktopMenu(id, command); };
            config.boundsChanged = [this, id = item.id](widget::Preferences bounds) { DesktopPreferences(id, bounds); };
            auto *host = &entry->window;
            ok = host->Open(instance, [this, id = item.id, host](const std::wstring &message) { Dispatch(message, id, *host); },
                            [this, id = item.id] { DesktopClosed(id); }, options.webviewProfile, std::move(config)) && ok;
        }
        return ok;
    }
    bool Restore() {
        if (stopped)
            return false;
        if (restored)
            return restoreOk;
        restored = true;
        load = store.Load(state);
        if (load == notes::LoadStatus::Failed) {
            state = {};
            error = "load";
        }
        const auto loaded = widgetStore.Load(options.widgetPath);
        prefs = loaded.value;
        if (!state.desktopLayoutKnown && load != notes::LoadStatus::Failed) {
            if (prefs.enabled) if (const auto shown = notes::Shown(state)) state.desktopNotes.push_back({shown->id, prefs});
            state.desktopLayoutKnown = true;
        }
        applying = true;
        const bool runtime = Runtime(prefs);
        applying = false;
        if (!runtime) {
            for (auto &[id, entry] : desktops) entry->window.Close();
            if (error.empty())
                error = "runtime";
        }
        restoreOk = load != notes::LoadStatus::Failed && runtime;
        return restoreOk;
    }
    bool Apply(widget::Preferences next) {
        if (stopped || applying || !restored)
            return false;
        applying = true;
        const auto previous = prefs;
        bool ok = widget::Valid(next, Limits) && Runtime(next);

        if (ok && !widgetStore.Save(options.widgetPath, next)) {
            ok = false;
            error = "save";
        }
        if (!ok) {
            Runtime(previous);
            if (error.empty())
                error = "runtime";
        } else {
            prefs = next;
            if (error == "runtime" || error == "save")
                error.clear();
        }
        applying = false;
        return ok;
    }
    std::wstring State(const char *result = "snapshot", const std::string &savedId = {}, bool saved = false) const {
        Json list = Json::array();
        for (const auto &note : state.notes)
            list.push_back({{"id", note.id},
                            {"title", note.title},
                            {"preview", notes::Preview(note.body, 80)},
                            {"createdAt", note.createdAt},
                            {"updatedAt", note.updatedAt},
                            {"chars", notes::CharacterCount(note.body)},
                            {"bytes", note.body.size()}});
        const char *loadStatus = !restored                               ? "not-restored"
                                 : load == notes::LoadStatus::Failed ? "failed"
                                 : load == notes::LoadStatus::Fresh  ? "fresh"
                                                                     : "loaded";
        Json desktopIds = Json::array();
        for (const auto &item : state.desktopNotes) desktopIds.push_back(item.id);
        return Ascii(Json{{"version", 1},
                          {"type", "notes-state"},
                          {"enabled", prefs.enabled},
                          {"running", Running()},
                          {"windowOpen", page.IsOpen()},
                          {"locked", prefs.locked},
                          {"material", prefs.frosted ? "frosted-glass" : "liquid-glass"},
                          {"loadStatus", loadStatus},
                          {"error", error},
                          {"pinnedId", state.pinnedId},
                          {"desktopIds", std::move(desktopIds)},
                          {"notes", std::move(list)},
                          {"result", result},
                          {"savedId", savedId},
                          {"saved", saved}}
                         .dump(-1, ' ', true));
    }
    std::wstring NoteMessage(const notes::Note &note, const char *type = "notes-note") const {
        return Ascii(Json{{"version", 1},
                          {"type", type},
                          {"id", note.id},
                          {"title", note.title},
                          {"body", note.body},
                          {"createdAt", note.createdAt},
                          {"updatedAt", note.updatedAt}}
                         .dump(-1, ' ', true));
    }
    // Persist candidate; on failure keep the committed state and report "save".
    bool Commit(notes::NotesState candidate) {
        if (load == notes::LoadStatus::Failed) {
            error = "load";
            return false;
        }
        notes::SortByRecent(candidate);
        if (!store.Save(candidate)) {
            error = "save";
            return false;
        }
        state = std::move(candidate);
        if (error == "save" || error == "limit" || error == "not-found" || error == "conflict" || error == "invalid")
            error.clear();
        return true;
    }
    std::string NewId() {
        char text[48]{};
        for (;;) {
            snprintf(text, sizeof text, "n-%llx-%x", static_cast<unsigned long long>(now()), ++counter);
            if (!notes::Find(state, text))
                return text;
        }
    }
    std::optional<std::vector<std::wstring>> HandleMessage(const std::wstring &input, const std::string &source = "external") {
        const auto message = Parse(input);
        if (!message)
            return {};
        std::vector<std::wstring> out;
        if (stopped || !restored) {
            out.push_back(State());
            return out;
        }
        const auto &m = *message;
        if (m.action == "snapshot") {
            out.push_back(State());
        } else if (m.action == "open") {
            if (const auto note = notes::Find(state, m.id))
                out.push_back(NoteMessage(*note));
            else {
                error = "not-found";
                out.push_back(State("applied"));
            }
        } else if (m.action == "create") {
            if (state.notes.size() >= notes::MaxNotes) {
                error = "limit";
                out.push_back(State("applied"));
                return out;
            }
            auto candidate = state;
            notes::Note note;
            note.id = NewId();
            note.createdAt = note.updatedAt = now();
            candidate.notes.push_back(note);
            const bool ok = Commit(std::move(candidate));
            if (ok)
                out.push_back(NoteMessage(note));
            out.push_back(State("applied", ok ? note.id : std::string{}, ok));
        } else if (m.action == "createDesktop") {
            if (state.notes.size() >= notes::MaxNotes) {
                error = "limit";
                out.push_back(State("applied"));
                return out;
            }
            auto candidate = state;
            notes::Note note;
            note.id = NewId();
            note.createdAt = note.updatedAt = now();
            candidate.notes.push_back(note);
            candidate.desktopLayoutKnown = true;
            auto desktop = widget::Defaults(Limits);
            desktop.enabled = true;
            desktop.placed = false;
            POINT cursor{}; GetCursorPos(&cursor);
            desktop.x = cursor.x + 24; desktop.y = cursor.y + 24; desktop.placed = true;
            candidate.desktopNotes.push_back({note.id, desktop});
            candidate.pinnedId = note.id;
            const bool ok = Commit(std::move(candidate));
            if (ok) {
                editOnOpen = note.id;
                auto next = prefs; next.enabled = true; Apply(next);
                editOnOpen.clear();
                out.push_back(NoteMessage(note));
            }
            out.push_back(State("applied", ok ? note.id : std::string{}, ok));
        } else if (m.action == "delete") {
            auto candidate = state;
            const auto before = candidate.notes.size();
            candidate.notes.erase(std::remove_if(candidate.notes.begin(), candidate.notes.end(),
                                                 [&](const auto &note) { return note.id == m.id; }),
                                  candidate.notes.end());
            if (candidate.pinnedId == m.id)
                candidate.pinnedId.clear();
            candidate.desktopNotes.erase(std::remove_if(candidate.desktopNotes.begin(), candidate.desktopNotes.end(),
                [&](const auto &item) { return item.id == m.id; }), candidate.desktopNotes.end());
            const bool ok = candidate.notes.size() != before && Commit(std::move(candidate));
            if (ok) Runtime(prefs);
            out.push_back(State("applied", {}, ok));
        } else if (m.action == "pin") {
            auto candidate = state;
            if (!notes::Find(candidate, m.id)) {
                error = "not-found";
                out.push_back(State("applied"));
                return out;
            }
            candidate.desktopLayoutKnown = true;
            candidate.desktopNotes.erase(std::remove_if(candidate.desktopNotes.begin(), candidate.desktopNotes.end(),
                                                        [&](const auto &desktop) { return desktop.id == m.id; }),
                                                        candidate.desktopNotes.end());
            if (m.value) { auto geometry = prefs; geometry.enabled = true; candidate.desktopNotes.push_back({m.id, geometry}); }
            candidate.pinnedId = candidate.desktopNotes.empty() ? std::string{} : candidate.desktopNotes.back().id;
            const bool ok = Commit(std::move(candidate));
            if (ok) { auto next = prefs; next.enabled = !state.desktopNotes.empty(); Apply(next); }
            out.push_back(State("applied", {}, ok));
        } else if (m.action == "openWindow") {
            out.push_back(State("applied", {}, OpenWindow()));
        } else if (m.action == "setEnabled" || m.action == "setActive") {
            bool ok = true;
            if (m.value && state.desktopNotes.empty()) {
                if (const auto shown = notes::Shown(state)) {
                    auto candidate = state; auto geometry = prefs; geometry.enabled = true;
                    candidate.desktopNotes.push_back({shown->id, geometry}); candidate.desktopLayoutKnown = true;
                    candidate.pinnedId = shown->id; ok = Commit(std::move(candidate));
                } else return HandleMessage(L"{\"version\":1,\"scope\":\"notes\",\"action\":\"createDesktop\"}", source);
            }
            if (!m.value) page.RequestClose();
            auto next = prefs; next.enabled = m.value;
            ok = ok && Apply(next);
            out.push_back(State("applied", {}, ok));
        } else if (m.action == "save") {
            if (!notes::Find(state, m.id)) {
                error = "not-found";
                out.push_back(State("applied", m.id, false));
                return out;
            }
            auto &pending = pendings[source + ":" + m.id];
            if (m.index == 0)
                pending = Pending{m.id, m.title, m.count, 0, {}, m.baseUpdatedAt};
            if (!pending || pending->id != m.id || pending->count != m.count || pending->next != m.index ||
                pending->title != m.title || pending->baseUpdatedAt != m.baseUpdatedAt) {
                pending.reset();
                error = "invalid";
                out.push_back(State("applied", m.id, false));
                return out;
            }
            pending->body += m.part;
            ++pending->next;
            if (pending->body.size() > notes::MaxBodyBytes) {
                pending.reset();
                error = "limit";
                out.push_back(State("applied", m.id, false));
                return out;
            }
            if (pending->next < pending->count) {
                out.push_back(Ascii(Json{{"version", 1}, {"type", "notes-chunk"}, {"id", m.id}, {"index", m.index}}
                                        .dump(-1, ' ', true)));
                return out;
            }
            auto finished = std::move(*pending);
            pending.reset();
            auto candidate = state;
            auto found = std::find_if(candidate.notes.begin(), candidate.notes.end(),
                                      [&](const auto &note) { return note.id == finished.id; });
            if (found == candidate.notes.end()) {
                error = "not-found";
                out.push_back(State("applied", m.id, false));
                return out;
            }
            notes::Note next = *found;
            if (finished.baseUpdatedAt && *finished.baseUpdatedAt != found->updatedAt) {
                error = "conflict";
                out.push_back(State("saved", m.id, false));
                return out;
            }
            next.title = finished.title;
            next.body = std::move(finished.body);
            if (!notes::ValidNote(next)) {
                error = next.title.size() > notes::MaxTitleBytes ? "limit" : "invalid";
                out.push_back(State("applied", m.id, false));
                return out;
            }
            bool ok = true;
            if (error == "conflict" || error == "invalid") error.clear();
            if (next.title != found->title || next.body != found->body) {
                next.updatedAt = std::max(now(), found->updatedAt + 1);
                *found = std::move(next);
                ok = Commit(std::move(candidate));
            }
            out.push_back(State("saved", m.id, ok));
        }
        return out;
    }
    bool Running() const {
        return !stopped && std::any_of(desktops.begin(), desktops.end(), [](const auto &item) { return item.second->window.IsOpen(); });
    }
    HWND Handle() const {
        for (const auto &[id, entry] : desktops) if (entry->window.IsOpen()) return entry->window.Handle();
        return nullptr;
    }
    bool OpenWindow() {
        if (stopped || options.verification)
            return false;
        return page.Open(
            instance,
            [this](const std::wstring &message) {
                Dispatch(message, "manager", page);
            },
            [this] { Notify(); }, options.webviewProfile);
    }
    void Shutdown() {
        if (stopped)
            return;
        stopped = true;
        page.Close();
        for (auto &[id, entry] : desktops) entry->window.Close();
    }

    struct Pending {
        std::string id, title;
        int count = 0, next = 0;
        std::string body;
        std::optional<notes::Millis> baseUpdatedAt;
    };
    HINSTANCE instance = nullptr;
    NotesSessionOptions options;
    LocalSettingsFileStore localFiles;
    settings::IFileStore &files;
    notes::NotesStore store;
    widget::PreferencesStore widgetStore;
    std::function<notes::Millis()> now;
    notes::NotesState state;
    notes::LoadStatus load = notes::LoadStatus::Fresh;
    widget::Preferences prefs = widget::Defaults(Limits);
    std::map<std::string, std::optional<Pending>> pendings;
    std::string editOnOpen;
    std::string error;
    unsigned counter = 0;
    bool stopped = false, applying = false, restored = false, restoreOk = false;
    NotesPageWindow page;
};

NotesSession::NotesSession(HINSTANCE instance, NotesSessionOptions options)
    : impl_(std::make_unique<Impl>(instance, std::move(options))) {}
NotesSession::~NotesSession() = default;
bool NotesSession::Restore() { return impl_->Restore(); }
std::optional<std::vector<std::wstring>> NotesSession::HandleMessage(const std::wstring &message) {
    auto replies = impl_->HandleMessage(message);
    // Keep an open notes window current with changes made from the manager.
    if (replies) {
        const auto parsed = Parse(message);
        const auto note = parsed && parsed->action == "save" && !replies->empty() && Json::parse(replies->back()).value("saved", false)
            ? notes::Find(impl_->state, parsed->id) : nullptr;
        impl_->Broadcast("external", note);
    }
    return replies;
}
bool NotesSession::Active() const { return impl_->Running() || impl_->page.IsOpen(); }
bool NotesSession::OpenWindow() { return impl_->OpenWindow(); }
std::wstring NotesSession::State() const { return impl_->State(); }
bool NotesSession::Running() const { return impl_->Running(); }
HWND NotesSession::Handle() const { return impl_->Handle(); }
const notes::NotesState &NotesSession::Snapshot() const { return impl_->state; }
void NotesSession::Shutdown() { impl_->Shutdown(); }
} // namespace lume::shell
