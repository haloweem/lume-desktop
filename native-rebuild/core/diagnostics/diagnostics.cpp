#include "core/diagnostics/diagnostics.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>
#include <cctype>
#include <set>
#include <stdexcept>

namespace lume::diagnostics {
namespace {

std::atomic<std::uint64_t> gOperationCounter{0};

bool IsString(const Json& value) {
    return value.is_string();
}

bool ContainsSensitivePattern(std::string_view value) {
    std::string lower;
    lower.reserve(value.size());
    for (const auto character : value)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    for (const auto* token : {"token", "cookie", "secret", "password", "authorization",
                              "bearer", "access_key", "access-token", "session"}) {
        if (lower.find(token) != std::string::npos) return true;
    }
    return lower.find("://") != std::string::npos ||
        lower.find('\\') != std::string::npos ||
        (!lower.empty() && lower.front() == '/') ||
        (lower.size() > 2 && std::isalpha(static_cast<unsigned char>(lower[0])) &&
            lower[1] == ':');
}

std::string RedactedValue(std::string_view value) {
    const auto bounded = Collector::BoundedText(value);
    return ContainsSensitivePattern(bounded) ? "[redacted]" : bounded;
}

std::string CodeField(const Json& value, const char* key) {
    if (!value.is_object() || !value.contains(key) || !IsString(value.at(key))) return {};
    const auto bounded = Collector::BoundedText(value.at(key).get<std::string>());
    if (bounded.empty()) return {};
    if (ContainsSensitivePattern(bounded) || bounded.size()>96) return "redacted";
    for (const auto character : bounded) {
        const auto byte = static_cast<unsigned char>(character);
        if (!(std::isalnum(byte) || character == '_' || character == '-' ||
              character == '.' || character == ':')) return "redacted";
    }
    return bounded;
}

std::string ValueField(const Json& value, const char* key) {
    if (!value.is_object() || !value.contains(key) || !IsString(value.at(key))) return {};
    return RedactedValue(value.at(key).get<std::string>());
}

bool JsonStringEqual(const Json& left, const Json& right) {
    return left == right;
}

void RemoveIfExists(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
}

}  // namespace

std::optional<Action> ParseCommand(std::wstring_view input) noexcept {
    if (input.empty() || input.size() > 512) return {};
    try {
        std::string text;
        text.reserve(input.size());
        for (const auto character : input) {
            if (static_cast<unsigned int>(character) > 127) return {};
            text.push_back(static_cast<char>(character));
        }
        std::set<std::string> keys;
        const auto value = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& item) {
            if (depth > 1 || (event == Json::parse_event_t::key &&
                !keys.insert(item.get<std::string>()).second))
                throw std::runtime_error("invalid diagnostics command");
            return true;
        });
        if (!value.is_object() || value.size() != 3 ||
            value.value("version", 0) != 1 ||
            value.value("scope", std::string{}) != "diagnostics" ||
            !value.contains("action") || !value.at("action").is_string())
            return {};
        const auto action = value.at("action").get<std::string>();
        if (action == "snapshot") return Action::Snapshot;
        if (action == "clear") return Action::Clear;
    } catch (...) {
    }
    return {};
}

Collector::Collector(std::string path) : path_(std::move(path)) {
    std::lock_guard<std::mutex> lock(mutex_);
    storageAvailable_ = LoadLocked();
}

void Collector::SetPath(std::string path) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = std::move(path);
    revision_ = 0;
    events_.clear();
    health_ = Json::object();
    activeMismatches_.clear();
    operations_.clear();
    storageAvailable_ = LoadLocked();
}

std::uint64_t Collector::NowMillis() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return millis > 0 ? static_cast<std::uint64_t>(millis) : 0;
}

std::string Collector::BoundedText(std::string_view value) {
    std::string result;
    result.reserve(std::min(value.size(), kMaxFieldLength));
    for (const auto character : value) {
        if (result.size() >= kMaxFieldLength) break;
        const auto byte = static_cast<unsigned char>(character);
        if (byte == '\r' || byte == '\n' || byte == '\t') result.push_back(' ');
        else if (byte >= 0x20) result.push_back(character);
    }
    return result;
}

std::string Collector::JsonValueText(const Json& value) {
    if (value.is_string()) return RedactedValue(value.get<std::string>());
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number()) return RedactedValue(value.dump(-1, ' ', false, Json::error_handler_t::replace));
    if (value.is_null()) return "null";
    return RedactedValue(value.dump(-1, ' ', false, Json::error_handler_t::replace));
}

std::string Collector::OperationId() {
    const auto counter = ++gOperationCounter;
    return "op-" + std::to_string(counter);
}

bool Collector::IsTerminalStatus(std::string_view status) {
    return status == "completed" || status == "success" || status == "ready" ||
        status == "failed" || status == "error" || status == "rejected" ||
        status == "cancelled" || status == "canceled";
}

bool Collector::IsFailureStatus(std::string_view status) {
    return status == "failed" || status == "error" || status == "rejected" ||
        status == "degraded";
}

bool Collector::IsMeaningfulError(std::string_view error) {
    return !error.empty() && error != "none" && error != "ok";
}

std::string Collector::StartOperation(std::string_view module, std::string_view action) {
    const auto operationId = OperationId();
    std::lock_guard<std::mutex> lock(mutex_);
    if (operations_.size() >= 128) operations_.erase(operations_.begin());
    operations_[operationId] = {BoundedText(module), BoundedText(action)};
    return operationId;
}

bool Collector::CompleteOperation(std::string_view operationId, std::string_view status,
                                  std::string_view module, std::string_view action,
                                  std::string_view expected, std::string_view actual,
                                  std::string_view error) {
    const auto id = BoundedText(operationId);
    const auto boundedStatus = RedactedValue(status);
    const auto boundedModule = RedactedValue(module);
    const auto boundedAction = RedactedValue(action);
    const auto boundedExpected = RedactedValue(expected);
    const auto boundedActual = RedactedValue(actual);
    const auto boundedError = RedactedValue(error);
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = operations_.find(id);
    const auto resolvedModule = !boundedModule.empty() ? boundedModule :
        (found == operations_.end() ? std::string{} : found->second.module);
    const auto resolvedAction = !boundedAction.empty() ? boundedAction :
        (found == operations_.end() ? std::string{} : found->second.action);
    const bool mismatch = !boundedExpected.empty() && !boundedActual.empty() &&
        boundedExpected != boundedActual;
    const bool failure = mismatch || IsFailureStatus(boundedStatus) || IsMeaningfulError(boundedError);
    bool changed = false;
    if (failure) {
        Event event;
        event.at = NowMillis();
        event.operationId = id;
        event.module = resolvedModule;
        event.kind = mismatch ? "effect-mismatch" : "operation";
        event.action = resolvedAction;
        event.status = boundedStatus;
        event.error = boundedError;
        event.expected = boundedExpected;
        event.actual = boundedActual;
        changed = RecordEventLocked(std::move(event));
    }
    if (!id.empty() && (IsTerminalStatus(boundedStatus) || failure)) operations_.erase(id);
    if (changed) storageAvailable_ = PersistLocked();
    return true;
}

bool Collector::RecordMessage(std::string_view message) {
    if (message.empty() || message.size() > kMaxMessageBytes) return false;
    try {
        const auto value = Json::parse(message);
        return RecordMessage(value);
    } catch (...) {
        return false;
    }
}

bool Collector::RecordMessage(const Json& message) {
    if (!message.is_object()) return false;
    std::lock_guard<std::mutex> lock(mutex_);

    bool changed = UpdateHealthLocked(message);
    const auto operationId = CodeField(message, "operationId");
    const auto messageType = CodeField(message, "type");
    const auto moduleField = CodeField(message, "module");
    auto module = moduleField.empty() ? messageType : moduleField;
    if (moduleField.empty() && module.size() > 6 && module.compare(module.size() - 6, 6, "-state") == 0)
        module.resize(module.size() - 6);
    const auto action = CodeField(message, "action");
    const auto status = CodeField(message, "status");
    const auto result = CodeField(message, "result");
    auto error = CodeField(message, "error");
    if (error.empty() && messageType == "wallpaper-state") {
        // A cached catalog is the normal startup path, not a failure.
        error = CodeField(message, "catalogError");
        if (error == "WALLPAPER_CATALOG_CACHED") error.clear();
        if (error.empty() && message.contains("displays") && message.at("displays").is_array()) {
            for (const auto& display : message.at("displays")) {
                if (!display.is_object() || CodeField(display, "state") != "failed") continue;
                error = CodeField(display, "error");
                if (error.empty()) error = "display-failed";
                break;
            }
        }
    }
    if (error.empty() && messageType == "weather-state") {
        const auto weatherError = CodeField(message, "weatherError");
        if (weatherError != "cancelled" && weatherError != "retry-delayed") error = weatherError;
    }
    // A taskbar request superseded by a newer one reports "cancelled"; the
    // newer request's outcome is what matters.
    if (messageType == "taskbar-state" && error == "cancelled") error.clear();
    if (error.empty() && CodeField(message, "loadStatus") == "failed") error = "load-failed";
    const bool hasExpected = message.contains("expected") && !message.at("expected").is_discarded();
    const bool hasActual = message.contains("actual") && !message.at("actual").is_discarded();
    const auto expected = hasExpected ? JsonValueText(message.at("expected")) : std::string{};
    const auto actual = hasActual ? JsonValueText(message.at("actual")) : std::string{};
    bool mismatch = hasExpected && hasActual && !JsonStringEqual(message.at("expected"), message.at("actual")) &&
        expected != actual;
    std::string mismatchExpected = expected;
    std::string mismatchActual = actual;
    std::string mismatchError = error;
    // Only flag narrow, settled native observations. A loading or intentionally
    // paused component must not look like an effect failure.
    if (!mismatch && messageType == "dock-state" &&
        message.contains("enabled") && message.at("enabled").is_boolean() &&
        message.at("enabled").get<bool>() && message.contains("running") &&
        message.at("running").is_boolean() && message.at("running").get<bool>() &&
        message.contains("layersVisible") && message.at("layersVisible").is_boolean() &&
        !message.at("layersVisible").get<bool>()) {
        mismatch = true;
        mismatchExpected = "layers-visible";
        mismatchActual = "hidden";
        mismatchError = "dock-layers-hidden";
    }
    if (!mismatch && messageType == "taskbar-state" &&
        message.contains("enabled") && message.at("enabled").is_boolean() &&
        message.at("enabled").get<bool>() && message.contains("transparentTaskbar") &&
        message.at("transparentTaskbar").is_boolean() && message.at("transparentTaskbar").get<bool>() &&
        message.contains("taskbars") && message.at("taskbars").is_number_integer() &&
        message.at("taskbars").get<std::int64_t>() > 0 &&
        message.contains("applying") && message.at("applying").is_boolean() &&
        !message.at("applying").get<bool>() && message.contains("pendingRecovery") &&
        message.at("pendingRecovery").is_boolean() && !message.at("pendingRecovery").get<bool>() &&
        message.contains("running") && message.at("running").is_boolean() &&
        !message.at("running").get<bool>()) {
        mismatch = true;
        mismatchExpected = "transparent-taskbar-active";
        mismatchActual = "not-running";
        mismatchError = "taskbar-runtime-not-running";
    }
    const bool failure = mismatch || IsFailureStatus(status) || IsFailureStatus(result) ||
        IsMeaningfulError(error);
    if (failure) {
        Event event;
        event.at = NowMillis();
        event.operationId = operationId;
        event.module = module;
        event.kind = mismatch ? "effect-mismatch" : "error";
        event.action = action;
        event.status = !status.empty() ? status : result;
        event.error = mismatch ? mismatchError : error;
        event.expected = mismatchExpected;
        event.actual = mismatchActual;
        changed = RecordEventLocked(std::move(event)) || changed;
    }
    if (mismatch) {
        const auto key = module + "|" + mismatchExpected + "|" + mismatchActual;
        Event active;
        active.at = NowMillis();
        active.module = module;
        active.kind = "effect-mismatch";
        active.action = action;
        active.status = !status.empty() ? status : result;
        active.error = mismatchError;
        active.expected = mismatchExpected;
        active.actual = mismatchActual;
        const auto old = activeMismatches_.find(key);
        if (old == activeMismatches_.end() || old->second.error != active.error) {
            activeMismatches_[key] = std::move(active);
            while (activeMismatches_.size() > kMaxHealthEntries) activeMismatches_.erase(activeMismatches_.begin());
            changed = true;
        }
    } else {
        const bool observedRecovered =
            (hasExpected && hasActual && JsonStringEqual(message.at("expected"), message.at("actual"))) ||
            (messageType == "dock-state" && message.contains("enabled") &&
                message.at("enabled").is_boolean() && !message.at("enabled").get<bool>()) ||
            (messageType == "dock-state" && message.contains("running") &&
                message.at("running").is_boolean() && message.at("running").get<bool>()) ||
            (messageType == "dock-state" && message.contains("layersVisible") &&
                message.at("layersVisible").is_boolean() && message.at("layersVisible").get<bool>()) ||
            (messageType == "taskbar-state" &&
                (!message.value("enabled", false) || !message.value("transparentTaskbar", false) ||
                 (message.value("running", false) && !message.value("applying", false) &&
                  !message.value("pendingRecovery", false))));
        if (observedRecovered && !module.empty()) {
            for (auto it = activeMismatches_.begin(); it != activeMismatches_.end();) {
                if (it->second.module == module) {
                    it = activeMismatches_.erase(it);
                    changed = true;
                } else ++it;
            }
        }
    }
    const bool applied = message.value("saved", false) && result == "applied";
    const bool taskbarSettled = messageType == "taskbar-state" && message.contains("applying") &&
        message.at("applying").is_boolean() && !message.at("applying").get<bool>() &&
        message.contains("pendingRecovery") && message.at("pendingRecovery").is_boolean() &&
        !message.at("pendingRecovery").get<bool>();
    if (!operationId.empty() && (IsTerminalStatus(status) || IsTerminalStatus(result) || failure || applied || taskbarSettled))
        operations_.erase(operationId);
    if (changed) storageAvailable_ = PersistLocked();
    return true;
}

bool Collector::RecordEvent(Event event) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto changed = RecordEventLocked(std::move(event));
    if (changed) storageAvailable_ = PersistLocked();
    return changed;
}

bool Collector::RecordEventLocked(Event event) {
    event.at = event.at == 0 ? NowMillis() : event.at;
    event.firstAt = event.firstAt == 0 ? event.at : event.firstAt;
    if (event.repeatCount == 0) event.repeatCount = 1;
    event.operationId = RedactedValue(event.operationId);
    event.module = RedactedValue(event.module);
    event.kind = CodeField(Json{{"value", event.kind}}, "value");
    event.action = RedactedValue(event.action);
    event.status = RedactedValue(event.status);
    event.error = RedactedValue(event.error);
    event.expected = RedactedValue(event.expected);
    event.actual = RedactedValue(event.actual);
    if (event.kind.empty() || (event.kind != "operation" && event.kind != "error" &&
                               event.kind != "effect-mismatch")) return false;
    const auto same = [&](const Event& previous) {
        return previous.module == event.module && previous.kind == event.kind && previous.action == event.action &&
            previous.status == event.status && previous.error == event.error &&
            previous.expected == event.expected && previous.actual == event.actual;
    };
    if (!events_.empty() && same(events_.back())) {
        constexpr std::uint64_t kRepeatIntervalMs = 60'000;
        if (event.at < events_.back().at || event.at - events_.back().at < kRepeatIntervalMs)
            return false;
        events_.back().at = event.at;
        if (events_.back().repeatCount < std::numeric_limits<std::uint32_t>::max())
            ++events_.back().repeatCount;
        ++revision_;
        return true;
    }
    events_.push_back(std::move(event));
    if (events_.size() > kMaxEvents)
        events_.erase(events_.begin(), events_.begin() + (events_.size() - kMaxEvents));
    ++revision_;
    return true;
}

bool Collector::UpdateHealthLocked(const Json& message) {
    std::string key = CodeField(message, "module");
    const auto type = CodeField(message, "type");
    if (key.empty()) {
        key = type;
        if (key.size() > 6 && key.compare(key.size() - 6, 6, "-state") == 0)
            key.resize(key.size() - 6);
    }
    if (type.empty() || (type.find("-state") == std::string::npos &&
                         type != "calendar-settings" && type != "settings")) return false;
    if (key.empty()) return false;
    Json value = Json::object();
    for (const auto* field : {"status", "result", "error", "action", "mode", "layout", "loadStatus",
            "catalogStatus", "catalogError", "weatherError", "materialStatus"}) {
        const auto text = CodeField(message, field);
        if (!text.empty()) value[field] = text;
    }
    for (const auto* field : {"enabled", "running", "visible", "loaded", "layersVisible",
            "transparentTaskbar", "autoHideTaskbar", "applying", "pendingRecovery",
            "playerAvailable", "pauseFullscreen", "loading", "hasWeather"}) {
        if (message.contains(field) && message.at(field).is_boolean()) value[field] = message.at(field);
    }
    for (const auto* field : {"visibleItems", "pinned", "added", "taskbars", "desktops",
            "autoHidden", "pendingRestores", "count", "hosts", "hooks", "pendingCleanup"})
        if (message.contains(field) && message.at(field).is_number_unsigned()) value[field] = message.at(field);
    const auto old = health_.find(key);
    if (old != health_.end()) {
        Json previous = *old;
        previous.erase("at");
        if (previous == value) return false;
    }
    value["at"] = NowMillis();
    health_[key] = std::move(value);
    while (health_.size() > kMaxHealthEntries) health_.erase(health_.begin());
    ++revision_;
    return true;
}

Json Collector::SnapshotLocked() const {
    Json events = Json::array();
    for (const auto& event : events_) {
        Json value = Json::object();
        value["at"] = event.at;
        value["firstAt"] = event.firstAt;
        value["repeatCount"] = event.repeatCount;
        if (!event.operationId.empty()) value["operationId"] = event.operationId;
        if (!event.module.empty()) value["module"] = event.module;
        value["kind"] = event.kind;
        if (!event.action.empty()) value["action"] = event.action;
        if (!event.status.empty()) value["status"] = event.status;
        if (!event.error.empty()) value["error"] = event.error;
        if (!event.expected.empty()) value["expected"] = event.expected;
        if (!event.actual.empty()) value["actual"] = event.actual;
        events.push_back(std::move(value));
    }
    return Json{
        {"schemaVersion", kSchemaVersion},
        {"revision", revision_},
        {"events", std::move(events)},
        {"health", health_},
        {"mismatches", [&] {
            Json values = Json::array();
            for (const auto& [key, event] : activeMismatches_) {
                (void)key;
                values.push_back({{"component", event.module}, {"expected", event.expected},
                    {"actual", event.actual}, {"confidence", "confirmed"},
                    {"reason", event.error}});
            }
            return values;
        }()},
    };
}

Json Collector::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = SnapshotLocked();
    result["available"] = true;
    result["storage"] = path_.empty() ? "unavailable" : (storageAvailable_ ? "ready" : "failed");
    return result;
}

std::string Collector::SnapshotText() const {
    return Snapshot().dump(-1, ' ', false, Json::error_handler_t::replace);
}

bool Collector::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.clear();
    health_ = Json::object();
    activeMismatches_.clear();
    operations_.clear();
    ++revision_;
    storageAvailable_ = PersistLocked();
    return storageAvailable_;
}

std::string Collector::StorageStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (path_.empty()) return "unavailable";
    return storageAvailable_ ? "ready" : "failed";
}

std::uint64_t Collector::Revision() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return revision_;
}

bool Collector::LoadLocked() {
    if (path_.empty()) return true;
    const auto target = std::filesystem::u8path(path_);
    std::error_code existsError;
    const bool exists = std::filesystem::exists(target, existsError);
    if (existsError) return false;
    if (!exists) return true;
    std::ifstream input(target, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const auto length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > kMaxPersistedBytes) return false;
    std::string bytes(static_cast<std::size_t>(length), '\0');
    input.seekg(0);
    if (!bytes.empty() && !input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return false;
    try {
        const auto value = Json::parse(bytes);
        if (!value.is_object() || value.value("schemaVersion", 0u) != kSchemaVersion) return false;
        revision_ = value.value("revision", 0ULL);
        if(revision_>9007199254740991ULL)return false;
        if (value.contains("events") && value.at("events").is_array()) {
            for (const auto& item : value.at("events")) {
                if (!item.is_object()) continue;
                Event event;
                event.at = item.value("at", 0ULL);
                event.firstAt = item.value("firstAt", event.at);
                event.repeatCount = item.value("repeatCount", 1u);
                event.operationId = CodeField(item, "operationId");
                event.module = CodeField(item, "module");
                event.kind = CodeField(item, "kind");
                event.action = CodeField(item, "action");
                event.status = CodeField(item, "status");
                event.error = CodeField(item, "error");
                event.expected = ValueField(item, "expected");
                event.actual = ValueField(item, "actual");
                // Older builds recorded these normal states as errors.
                if (event.kind == "error" && (event.error == "WALLPAPER_CATALOG_CACHED" ||
                    (event.module == "taskbar" && event.error == "cancelled"))) continue;
                if (!event.kind.empty() && (event.kind == "operation" || event.kind == "error" ||
                    event.kind == "effect-mismatch")) events_.push_back(std::move(event));
            }
        }
        if (events_.size() > kMaxEvents)
            events_.erase(events_.begin(), events_.begin() + (events_.size() - kMaxEvents));
        if (value.contains("health") && value.at("health").is_object()) {
            for (const auto& [key, item] : value.at("health").items()) {
                if (health_.size() >= kMaxHealthEntries || !item.is_object()) break;
                const auto safeKey = CodeField(Json{{"key",key}}, "key");
                if (safeKey.empty() || safeKey=="redacted" || safeKey.size()>64) continue;
                Json safe = Json::object();
                if (item.contains("at") && item.at("at").is_number_unsigned())
                    safe["at"] = item.at("at").get<std::uint64_t>();
                for (const auto* field : {"status", "result", "error", "action", "mode", "layout",
                        "loadStatus", "catalogStatus", "catalogError", "weatherError", "materialStatus"}) {
                    const auto text = CodeField(item, field);
                    if (!text.empty()) safe[field] = text;
                }
                for (const auto* field : {"enabled", "running", "visible", "loaded", "layersVisible",
                        "transparentTaskbar", "autoHideTaskbar", "applying", "pendingRecovery",
                        "playerAvailable", "pauseFullscreen", "loading", "hasWeather"}) {
                    if (item.contains(field) && item.at(field).is_boolean()) safe[field] = item.at(field);
                }
                for (const auto* field : {"visibleItems", "pinned", "added", "taskbars", "desktops",
                        "autoHidden", "pendingRestores", "count", "hosts", "hooks", "pendingCleanup"}) {
                    if (item.contains(field) && item.at(field).is_number_unsigned()) safe[field] = item.at(field);
                }
                health_[safeKey] = std::move(safe);
            }
        }
        if (value.contains("mismatches") && value.at("mismatches").is_array()) {
            for (const auto& item : value.at("mismatches")) {
                if(activeMismatches_.size()>=kMaxHealthEntries)break;
                if (!item.is_object() || !item.contains("component") ||
                    !item.at("component").is_string()) continue;
                Event event;
                event.module = CodeField(item, "component");
                event.kind = "effect-mismatch";
                event.expected = ValueField(item, "expected");
                event.actual = ValueField(item, "actual");
                event.error = CodeField(item, "reason");
                if (event.module.empty() || event.expected.empty() || event.actual.empty()) continue;
                const auto key = event.module + "|" + event.expected + "|" + event.actual;
                activeMismatches_[key] = std::move(event);
            }
        }
        storageAvailable_ = true;
        return true;
    } catch (...) {
        events_.clear();
        health_ = Json::object();
        activeMismatches_.clear();
        revision_ = 0;
        return false;
    }
}

bool Collector::PersistLocked() const {
    if (path_.empty()) return true;
    const auto target = std::filesystem::u8path(path_);
    const auto temporary = std::filesystem::u8path(path_ + ".tmp");
    const auto backup = std::filesystem::u8path(path_ + ".bak");
    std::error_code error;
    if (!target.parent_path().empty()) {
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) return false;
    }
    const auto bytes = SnapshotLocked().dump(-1, ' ', false, Json::error_handler_t::replace);
    if (bytes.size() > kMaxPersistedBytes) return false;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            RemoveIfExists(temporary);
            return false;
        }
    }
    RemoveIfExists(backup);
    error.clear();
    const bool hasTarget = std::filesystem::exists(target, error);
    if (error) {
        RemoveIfExists(temporary);
        return false;
    }
    if (hasTarget) {
        std::filesystem::rename(target, backup, error);
        if (error) {
            RemoveIfExists(temporary);
            return false;
        }
    }
    std::filesystem::rename(temporary, target, error);
    if (error) {
        RemoveIfExists(temporary);
        std::error_code restoreError;
        if (std::filesystem::exists(backup, restoreError) && !restoreError)
            std::filesystem::rename(backup, target, restoreError);
        return false;
    }
    RemoveIfExists(backup);
    return true;
}

}  // namespace lume::diagnostics
