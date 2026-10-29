#include "apps/shell/music_desktop_session.h"

#include "core/widget/widget_preferences_store.h"
#include "platform/windows/local_settings_file_store.h"
#include "platform/windows/music_desktop_controls.h"
#include "vendor/nlohmann/json.hpp"

#include <set>
#include <utility>

namespace lume::shell {
namespace {

using Json = nlohmann::json;
using platform::windows::MusicDesktopControls;
using platform::windows::WidgetAction;

struct Message {
    bool setEnabled = false;
    bool value = false;
};

std::optional<Message> Parse(const std::wstring& input) {
    if (input.empty() || input.size() > 1024 || input.find(L'\0') != std::wstring::npos) return {};
    try {
        struct Invalid {};
        std::set<std::string> keys;
        const auto json = Json::parse(input.begin(), input.end(),
            [&](int depth, Json::parse_event_t event, Json& value) {
                if (depth > 1 || event == Json::parse_event_t::array_start ||
                    (event == Json::parse_event_t::object_start && depth != 0)) throw Invalid{};
                if (event == Json::parse_event_t::key &&
                    (!keys.insert(value.get<std::string>()).second || keys.size() > 4)) throw Invalid{};
                return true;
            });
        if (!json.is_object() || !json.contains("version") ||
            !json["version"].is_number_integer() || json["version"] != 1 ||
            !json.contains("scope") || json["scope"] != "music-desktop" ||
            !json.contains("action")) return {};
        if (json["action"] == "snapshot" && json.size() == 3) return Message{};
        if (json["action"] == "setEnabled" && json.size() == 4 &&
            json.contains("value") && json["value"].is_boolean())
            return Message{true, json["value"].get<bool>()};
    } catch (...) {}
    return {};
}

struct Applying {
    bool& flag;
    explicit Applying(bool& value) : flag(value) { flag = true; }
    ~Applying() { flag = false; }
};

const char* LoadStatus(widget::LoadState state) {
    switch (state) {
    case widget::LoadState::Fresh: return "fresh";
    case widget::LoadState::Loaded: return "loaded";
    case widget::LoadState::Recovered: return "recovered";
    case widget::LoadState::Failed: return "failed";
    }
    return "failed";
}

} // namespace

struct MusicDesktopSession::Impl {
    Impl(HINSTANCE instance, MusicDesktopSessionOptions options, CommandHandler commandHandler,
         std::function<void()> openSettings)
        : options(std::move(options)), commandHandler(std::move(commandHandler)),
          openSettings(std::move(openSettings)),
          store(this->options.files ? *this->options.files : localFiles, MusicDesktopControls::Limits),
          controls(instance, [this](const auto& command) {
              if (stopped || this->options.verification || !this->commandHandler) return;
              const auto callback = this->commandHandler;
              callback(command);
          }, [this](WidgetAction action, const auto& value) {
              Native(action, value);
              NotifyChanged();
          }, this->options.wake) {
        notifiedState = State();
    }

    ~Impl() {
        options.notify = {};
        Shutdown();
    }

    void NotifyChanged() {
        if (applying) return;
        const auto current = State();
        if (notifiedState == current) return;
        notifiedState = current;
        if (!options.notify) return;
        const auto callback = options.notify;
        try { callback(current); } catch (...) {}
    }

    bool Runtime(const widget::Preferences& value) noexcept {
        try {
            if (options.verification || !value.enabled) {
                controls.Hide();
                auto hidden = value;
                hidden.enabled = false;
                return controls.Configure(hidden);
            }
            if (!controls.Configure(value)) return false;
            if (controls.Visible() || controls.Show()) return true;
            controls.Hide();
        } catch (...) {}
        return false;
    }

    bool Restore() {
        if (stopped || applying) return false;
        if (restored) return restoreOk;
        Applying guard(applying);
        restored = true;
        error.clear();
        if (options.path.empty() || options.path.find('\0') != std::string::npos) {
            load = widget::LoadState::Failed;
            error = "path";
            return false;
        }
        try {
            const auto loaded = store.Load(options.path);
            prefs = loaded.value;
            load = loaded.state;
        } catch (...) {
            load = widget::LoadState::Failed;
        }
        if (load == widget::LoadState::Failed) error = "load";
        bool mediaOk = true;
        if (!options.verification) {
            mediaOk = SUCCEEDED(controls.EnableSystemMediaControls());
            if (!mediaOk && error.empty()) error = "smtc";
        }
        const bool runtimeOk = Runtime(prefs);
        if (!runtimeOk) {
            controls.Hide();
            error = "runtime";
        }
        restoreOk = load != widget::LoadState::Failed && mediaOk && runtimeOk;
        return restoreOk;
    }

    bool Apply(widget::Preferences next) {
        if (stopped || applying || !restored) return false;
        Applying guard(applying);
        if (options.path.empty() || options.path.find('\0') != std::string::npos) {
            error = "path";
            return false;
        }
        const auto previous = prefs;
        if (!widget::Valid(next, MusicDesktopControls::Limits) || !Runtime(next)) {
            error = Runtime(previous) ? "runtime" : "rollback";
            return false;
        }
        if (!options.verification && next.enabled) next = controls.Bounds();
        bool saved = false;
        try {
            saved = options.path.find('\0') == std::string::npos && store.Save(options.path, next);
        } catch (...) {}
        if (!saved) {
            error = Runtime(previous) ? "save" : "rollback";
            return false;
        }
        prefs = next;
        load = widget::LoadState::Loaded;
        error.clear();
        return true;
    }

    void Native(WidgetAction action, const widget::Preferences& value) {
        if (stopped || applying || !restored) return;
        switch (action) {
        case WidgetAction::Settings:
            if (openSettings) {
                const auto callback = openSettings;
                try { callback(); } catch (...) { error = "settings"; }
            }
            return;
        case WidgetAction::Close:
        case WidgetAction::Bounds:
        case WidgetAction::Reset:
        case WidgetAction::Lock:
        case WidgetAction::Liquid:
        case WidgetAction::Frosted:
            // The controls already applied the native action. prefs stays the
            // last committed value until persistence succeeds, for rollback.
            Apply(value);
            return;
        case WidgetAction::Refresh: return;
        }
    }

    std::wstring State(bool applied = false, bool saved = false) const {
        const bool registered = !stopped && controls.SystemMediaControlsEnabled();
        const bool mediaFailed = !stopped && !options.verification && restored &&
                                 FAILED(controls.SystemMediaResult());
        const char* mediaStatus = stopped ? "stopped" : !restored ? "not-restored" :
            options.verification ? "verification" : mediaFailed ? "failed" :
            registered ? "ready" : "inactive";
        const auto text = Json{
            {"version", 1}, {"type", "music-desktop-state"},
            {"enabled", prefs.enabled}, {"running", Running()}, {"stopped", stopped},
            {"locked", prefs.locked}, {"material", prefs.frosted ? "frosted-glass" : "liquid-glass"},
            {"width", prefs.width}, {"height", prefs.height},
            {"loadStatus", restored ? LoadStatus(load) : "not-restored"},
            {"error", error.empty() && mediaFailed ? "smtc" : error},
            {"smtc", {{"registered", registered}, {"status", mediaStatus}}},
            {"result", applied ? "applied" : "snapshot"}, {"saved", saved}
        }.dump();
        // Only fixed ASCII fields/enums and numbers are exposed, never paths,
        // source URIs, metadata, HRESULTs or exception messages.
        return {text.begin(), text.end()};
    }

    std::optional<std::wstring> HandleMessage(const std::wstring& input) {
        const auto message = Parse(input);
        if (!message) return {};
        if (!message->setEnabled) return State();
        if (stopped || applying) return {};
        if (!restored) {
            error = "not-restored";
            return State(true, false);
        }
        auto next = prefs;
        next.enabled = message->value;
        const bool saved = Apply(next);
        return State(true, saved);
    }

    bool Running() const { return !stopped && controls.Visible(); }
    void Shutdown() {
        if (stopped) return;
        stopped = true;
        controls.Shutdown();
    }

    MusicDesktopSessionOptions options;
    CommandHandler commandHandler;
    std::function<void()> openSettings;
    platform::windows::LocalSettingsFileStore localFiles;
    widget::PreferencesStore store;
    widget::Preferences prefs = widget::Defaults(MusicDesktopControls::Limits);
    widget::LoadState load = widget::LoadState::Fresh;
    bool stopped = false, applying = false, restored = false, restoreOk = false;
    std::string error;
    std::wstring notifiedState;
    MusicDesktopControls controls;
};

MusicDesktopSession::MusicDesktopSession(HINSTANCE instance, MusicDesktopSessionOptions options,
    CommandHandler commandHandler, std::function<void()> openSettings)
    : impl_(std::make_unique<Impl>(instance, std::move(options),
        std::move(commandHandler), std::move(openSettings))) {}
MusicDesktopSession::~MusicDesktopSession() = default;
bool MusicDesktopSession::Restore() {
    const bool ok = impl_->Restore();
    impl_->NotifyChanged();
    return ok;
}
void MusicDesktopSession::Update(const music::MusicSnapshot& snapshot) {
    if (!impl_->stopped) {
        impl_->controls.Update(snapshot);
        impl_->NotifyChanged();
    }
}
std::optional<std::wstring> MusicDesktopSession::HandleMessage(const std::wstring& message) {
    auto result = impl_->HandleMessage(message);
    if (result) impl_->NotifyChanged();
    return result;
}
std::wstring MusicDesktopSession::State() const { return impl_->State(); }
bool MusicDesktopSession::Running() const { return impl_->Running(); }
void MusicDesktopSession::Shutdown() {
    impl_->Shutdown();
    impl_->NotifyChanged();
}

} // namespace lume::shell
