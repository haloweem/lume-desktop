#include "core/todo/todo_json_codec.h"
#include "core/todo/todo_model.h"
#include "core/todo/todo_time.h"
#include "vendor/nlohmann/json.hpp"
#include <cmath>
#include <limits>
#include <set>

namespace lume::todo {
namespace {
using Json = nlohmann::json;
Error Bounded(const std::string &text) {
    if (text.size() > TodoJsonCodec::MaxBytes)
        return Error::LimitExceeded;
    int depth = 0;
    bool quoted = false, escape = false;
    for (const char c : text) {
        if (quoted) {
            if (escape)
                escape = false;
            else if (c == '\\')
                escape = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"')
            quoted = true;
        else if (c == '{' || c == '[') {
            if (++depth > TodoJsonCodec::MaxDepth)
                return Error::LimitExceeded;
        } else if (c == '}' || c == ']')
            --depth;
    }
    return Error::None;
}
bool Integer(const Json &value, Millis &result) {
    if (value.is_number_unsigned()) {
        const auto x = value.get<std::uint64_t>();
        if (x > static_cast<std::uint64_t>(std::numeric_limits<Millis>::max()))
            return false;
        result = static_cast<Millis>(x);
        return true;
    }
    if (value.is_number_integer()) {
        result = value.get<Millis>();
        return true;
    }
    if (value.is_number_float()) {
        const double x = value.get<double>();
        if (!std::isfinite(x) || std::trunc(x) != x ||
            static_cast<long double>(x) < static_cast<long double>(std::numeric_limits<Millis>::min()) ||
            static_cast<long double>(x) > static_cast<long double>(std::numeric_limits<Millis>::max()))
            return false;
        result = static_cast<Millis>(x);
        return true;
    }
    return false;
}
bool Stamp(const Json &source, const char *key, std::optional<Millis> &out, const ILocalTimePort &port) {
    if (!source.contains(key) || source[key].is_null()) {
        out.reset();
        return true;
    }
    Millis value = 0;
    if (!Integer(source[key], value) || !ValidTimestamp(value, port))
        return false;
    out = value;
    return true;
}
bool Text(const Json &j, const char *key, std::string &out, const std::string &fallback = {}) {
    if (!j.contains(key)) {
        out = fallback;
        return true;
    }
    if (!j[key].is_string())
        return false;
    out = j[key].get<std::string>();
    return true;
}
Color ReadColor(const std::vector<std::string> &tags) {
    const std::string prefix = "lume:calendar-color:";
    for (const auto &tag : tags) {
        if (tag.rfind(prefix, 0) != 0)
            continue;
        const auto name = tag.substr(prefix.size());
        if (name == "red")
            return Color::Red;
        if (name == "orange")
            return Color::Orange;
        if (name == "yellow")
            return Color::Yellow;
        if (name == "green")
            return Color::Green;
        if (name == "blue")
            return Color::Blue;
    }
    return Color::None;
}
std::optional<Schedule> ReadSchedule(const std::vector<std::string> &tags, const ILocalTimePort &port) {
    const std::string prefix = "lume:calendar-schedule:";
    for (const auto &tag : tags) {
        if (tag.rfind(prefix, 0) != 0)
            continue;
        try {
            const auto pair = Json::parse(tag.substr(prefix.size()));
            Millis start = 0, end = 0;
            if (pair.is_array() && pair.size() == 2 && Integer(pair[0], start) && Integer(pair[1], end) &&
                ValidSchedule({start, end}, port))
                return Schedule{start, end};
        } catch (const Json::exception &) {
        }
        return {};
    }
    return {};
}
std::optional<Recurrence> ReadRecurrence(const Json &source) {
    if (!source.contains("recurrence") || !source["recurrence"].is_object())
        return {};
    const auto &value = source["recurrence"];
    if (!value.contains("frequency") || !value["frequency"].is_string())
        return {};
    const auto frequency = value["frequency"].get<std::string>();
    Recurrence result;
    if (frequency == "daily")
        result.frequency = Frequency::Daily;
    else if (frequency == "weekly")
        result.frequency = Frequency::Weekly;
    else if (frequency == "weekdays")
        result.frequency = Frequency::Weekdays;
    else if (frequency == "monthly")
        result.frequency = Frequency::Monthly;
    else
        return {};
    if (value.contains("interval") && value["interval"].is_number()) {
        const double interval = value["interval"].get<double>();
        if (std::isfinite(interval))
            result.interval = static_cast<int>(std::max(1.0, std::min(365.0, std::trunc(interval))));
    }
    return result;
}
Error ReadTask(const Json &source, TaskRecord &task, const ILocalTimePort &port) {
    if (!source.is_object() || !Text(source, "id", task.view.id) || task.view.id.empty() ||
        !ValidUtf8Length(task.view.id, 500) || !Text(source, "title", task.view.title) ||
        !Text(source, "notes", task.view.notes) || !ValidUtf8Length(task.view.notes, 10000))
        return Error::InvalidTask;
    task.view.title = Trim(task.view.title);
    task.view.notes = Trim(task.view.notes);
    if (task.view.title.empty() || !ValidUtf8Length(task.view.title, 500))
        return Error::InvalidTask;
    std::string status;
    if (!Text(source, "status", status, "open"))
        return Error::InvalidTask;
    task.view.completed = status == "completed";
    if (!Stamp(source, "dueAt", task.view.dueAt, port) ||
        !Stamp(source, "reminderAt", task.view.reminderAt, port) ||
        !Stamp(source, "deletedAt", task.view.deletedAt, port) ||
        !Stamp(source, "completedAt", task.completedAt, port))
        return Error::InvalidTime;
    std::optional<Millis> created, updated;
    if (!Stamp(source, "createdAt", created, port) || !Stamp(source, "updatedAt", updated, port))
        return Error::InvalidTime;
    task.view.createdAt = created.value_or(0);
    task.view.updatedAt = updated.value_or(task.view.createdAt);
    if (source.contains("revision") &&
        (!Integer(source["revision"], task.view.revision) || task.view.revision < 0))
        return Error::InvalidTask;
    if (source.contains("tags")) {
        if (!source["tags"].is_array() || source["tags"].size() > 30)
            return Error::LimitExceeded;
        for (const auto &tag : source["tags"]) {
            if (!tag.is_string())
                return Error::InvalidTask;
            auto text = tag.get<std::string>();
            if (!ValidUtf8Length(text, 10000))
                return Error::InvalidTask;
            const std::string prefix = "lume:calendar-schedule:";
            if (text.rfind(prefix, 0) == 0 && Bounded(text.substr(prefix.size())) != Error::None)
                return Error::LimitExceeded;
            task.tags.push_back(std::move(text));
        }
    }
    if (source.contains("subtasks") && (!source["subtasks"].is_array() || source["subtasks"].size() > 100))
        return Error::LimitExceeded;
    if (source.contains("sortKey") && source["sortKey"].is_number())
        task.sortKey = source["sortKey"].get<double>();
    else
        task.sortKey = static_cast<double>(task.view.createdAt);
    if (!std::isfinite(task.sortKey))
        return Error::InvalidTask;
    task.view.schedule = ReadSchedule(task.tags, port);
    task.view.color = ReadColor(task.tags);
    task.recurrence = ReadRecurrence(source);
    auto metadata = source;
    for (const char *key : {"id", "title", "notes", "status", "dueAt", "reminderAt", "deletedAt",
                            "completedAt", "createdAt", "updatedAt", "revision", "tags", "sortKey"})
        metadata.erase(key);
    task.preservedJson = metadata.dump();
    RefreshView(task, port);
    return Error::None;
}
Json Optional(std::optional<Millis> value) {
    return value ? Json(*value) : Json(nullptr);
}
Json WriteTask(const TaskRecord &task) {
    Json value = Json::parse(task.preservedJson);
    if (!value.is_object())
        throw std::runtime_error("task object required");
    const auto &t = task.view;
    value["id"] = t.id;
    value["title"] = t.title;
    value["notes"] = t.notes;
    value["status"] = t.completed ? "completed" : "open";
    value["dueAt"] = Optional(t.dueAt);
    value["reminderAt"] = Optional(t.reminderAt);
    value["deletedAt"] = Optional(t.deletedAt);
    value["completedAt"] = Optional(task.completedAt);
    value["createdAt"] = t.createdAt;
    value["updatedAt"] = t.updatedAt;
    value["revision"] = t.revision;
    value["tags"] = task.tags;
    if (!value.contains("sortKey"))
        value["sortKey"] = task.sortKey;
    if (!value.contains("priority"))
        value["priority"] = "none";
    if (!value.contains("listId"))
        value["listId"] = "default";
    if (task.recurrenceChanged) {
        if (!task.recurrence)
            value["recurrence"] = nullptr;
        else {
            const auto frequency = task.recurrence->frequency;
            value["recurrence"] = {{"frequency", frequency == Frequency::Daily      ? "daily"
                                                 : frequency == Frequency::Weekly   ? "weekly"
                                                 : frequency == Frequency::Weekdays ? "weekdays"
                                                                                    : "monthly"},
                                   {"interval", task.recurrence->interval}};
        }
    } else if (!value.contains("recurrence"))
        value["recurrence"] = nullptr;
    if (!value.contains("subtasks"))
        value["subtasks"] = Json::array();
    if (task.resetSubtasks)
        for (auto &subtask : value["subtasks"])
            if (subtask.is_object())
                subtask["completed"] = false;
    return value;
}
Error Dump(const Json &value, std::string &output) {
    auto encoded = value.dump(-1, ' ', false, Json::error_handler_t::strict);
    const auto error = Bounded(encoded);
    if (error != Error::None)
        return error;
    output = std::move(encoded);
    return Error::None;
}
} // namespace
Error TodoJsonCodec::Decode(const std::string &text, TodoState &output, const ILocalTimePort &port) const {
    const auto bounds = Bounded(text);
    if (bounds != Error::None)
        return bounds;
    try {
        const auto root = Json::parse(text);
        if (!root.is_object() || !root.contains("version"))
            return Error::InvalidJson;
        Millis version = 0;
        if (!Integer(root["version"], version))
            return Error::InvalidJson;
        if (version != 1)
            return Error::UnsupportedVersion;
        if (!root.contains("tasks") || !root["tasks"].is_array())
            return Error::InvalidJson;
        TodoState state;
        if (root.contains("revision") && (!Integer(root["revision"], state.revision) || state.revision < 0))
            return Error::InvalidJson;
        std::set<std::string> ids;
        for (const auto &source : root["tasks"]) {
            TaskRecord task;
            const auto error = ReadTask(source, task, port);
            if (error != Error::None)
                return error;
            if (!ids.insert(task.view.id).second)
                return Error::InvalidTask;
            state.tasks.push_back(std::move(task));
        }
        state.inlineOutbox = root.contains("outbox");
        if (state.inlineOutbox) {
            if (!root["outbox"].is_array())
                return Error::InvalidJson;
            for (const auto &operation : root["outbox"]) {
                state.outbox.push_back(operation.dump());
                if (operation.is_object() && operation.contains("id") && operation["id"].is_string())
                    state.outboxIds.push_back(operation["id"].get<std::string>());
            }
        }
        auto metadata = root;
        metadata.erase("tasks");
        metadata.erase("outbox");
        metadata.erase("version");
        metadata.erase("revision");
        state.preservedJson = metadata.dump();
        output = std::move(state);
        return Error::None;
    } catch (const Json::exception &) {
        return Error::InvalidJson;
    } catch (const std::exception &) {
        return Error::InvalidJson;
    }
}
Error TodoJsonCodec::Encode(const TodoState &state, std::string &output) const {
    try {
        Json root = Json::parse(state.preservedJson);
        if (!root.is_object())
            return Error::InvalidJson;
        root["version"] = 1;
        root["revision"] = state.revision;
        root["tasks"] = Json::array();
        root["outbox"] = Json::array();
        for (const auto &task : state.tasks)
            root["tasks"].push_back(WriteTask(task));
        for (const auto &operation : state.outbox)
            root["outbox"].push_back(Json::parse(operation));
        return Dump(root, output);
    } catch (const std::exception &) {
        return Error::InvalidJson;
    }
}
Error TodoJsonCodec::DecodeSidecar(const std::string &text, std::vector<std::string> &output,
                                   std::vector<std::string> &ids) const {
    const auto bounds = Bounded(text);
    if (bounds != Error::None)
        return bounds;
    try {
        const auto root = Json::parse(text);
        if (!root.is_object() || !root.contains("operations") || !root["operations"].is_array())
            return Error::InvalidJson;
        if (root.contains("version") && root["version"] != 1)
            return Error::UnsupportedVersion;
        std::vector<std::string> operations, operationIds;
        for (const auto &item : root["operations"]) {
            operations.push_back(item.dump());
            if (item.is_object() && item.contains("id") && item["id"].is_string())
                operationIds.push_back(item["id"].get<std::string>());
        }
        output = std::move(operations);
        ids = std::move(operationIds);
        return Error::None;
    } catch (const Json::exception &) {
        return Error::InvalidJson;
    }
}
Error TodoJsonCodec::Operation(const TaskRecord &task, const std::string &id, bool deleted, Millis now,
                               std::string &output) const {
    try {
        return Dump(Json{{"id", id},
                         {"type", deleted ? "delete" : "upsert"},
                         {"task", WriteTask(task)},
                         {"createdAt", now}},
                    output);
    } catch (const std::exception &) {
        return Error::InvalidJson;
    }
}
} // namespace lume::todo
