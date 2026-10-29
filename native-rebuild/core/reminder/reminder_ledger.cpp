#include "core/reminder/reminder_ledger.h"
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <limits>

namespace lume::reminder {
namespace {
using Json = nlohmann::json;
using Keys = std::set<std::pair<std::string, todo::Millis>>;
bool Integer(const Json &value, todo::Millis &out) {
    if (!value.is_number_integer())
        return false;
    if (value.is_number_unsigned() &&
        value.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<todo::Millis>::max()))
        return false;
    out = value.get<todo::Millis>();
    return true;
}
ErrorCode Read(settings::IFileStore &files, const std::string &path, Keys &out) {
    std::string text;
    if (!files.Read(path, text))
        return ErrorCode::ReadFailed;
    try {
        const auto json = Json::parse(text);
        todo::Millis version = 0;
        if (!json.is_object() || !json.contains("version") || !Integer(json.at("version"), version))
            return ErrorCode::InvalidJson;
        if (version != 1)
            return ErrorCode::UnsupportedVersion;
        if (!json.contains("handled") || !json.at("handled").is_array())
            return ErrorCode::InvalidJson;
        Keys candidate;
        for (const auto &entry : json.at("handled")) {
            todo::Millis at = 0;
            if (!entry.is_object() || !entry.contains("taskId") || !entry.at("taskId").is_string() ||
                !entry.contains("at") || !Integer(entry.at("at"), at))
                return ErrorCode::InvalidJson;
            const auto id = entry.at("taskId").get<std::string>();
            if (id.empty())
                return ErrorCode::InvalidJson;
            candidate.emplace(id, at);
        }
        out = std::move(candidate);
        return ErrorCode::None;
    } catch (const Json::exception &) {
        return ErrorCode::InvalidJson;
    }
}
} // namespace
ReminderLedger::ReminderLedger(settings::IFileStore &files, std::string path)
    : files_(files), path_(std::move(path)) {}
bool ReminderLedger::Load() {
    ready_ = false;
    if (path_.empty()) {
        error_ = ErrorCode::InvalidPath;
        return false;
    }
    Keys candidate;
    const bool primary = files_.Exists(path_);
    const bool backup = files_.Exists(path_ + ".bak");
    error_ = primary ? Read(files_, path_, candidate) : ErrorCode::None;
    if (error_ == ErrorCode::UnsupportedVersion || error_ == ErrorCode::ReadFailed)
        return false;
    if ((!primary || error_ != ErrorCode::None) && backup) {
        error_ = Read(files_, path_ + ".bak", candidate);
        if (error_ != ErrorCode::None)
            return false;
        error_ = ErrorCode::BackupRecovered;
    } else if (error_ != ErrorCode::None) {
        return false;
    }
    handled_ = std::move(candidate);
    ready_ = true;
    return true;
}
bool ReminderLedger::Ready() const { return ready_; }
ErrorCode ReminderLedger::Error() const { return error_; }
bool ReminderLedger::Handled(const Key &key) const {
    return ready_ && handled_.count({key.taskId, key.at}) != 0;
}
Plan ReminderLedger::Evaluate(const todo::RepositorySnapshot &snapshot, todo::Millis now) const {
    Plan plan;
    if (!ready_)
        return plan;
    Keys seen;
    for (const auto &task : snapshot.tasks) {
        if (task.completed || task.deletedAt.has_value() || !task.reminderAt || task.id.empty())
            continue;
        const auto at = *task.reminderAt;
        if (Handled({task.id, at}) || !seen.emplace(task.id, at).second)
            continue;
        if (now < at) {
            if (!plan.nextAt || at < *plan.nextAt)
                plan.nextAt = at;
        } else if (static_cast<std::uint64_t>(now) - static_cast<std::uint64_t>(at) > 86400000ULL) {
            // Unsigned subtraction represents the full nonnegative int64 difference without overflow.
            plan.expired.push_back({task.id, at});
        } else {
            plan.due.push_back(task);
        }
    }
    std::sort(plan.due.begin(), plan.due.end(), [](const auto &a, const auto &b) {
        return std::make_pair(*a.reminderAt, a.id) < std::make_pair(*b.reminderAt, b.id);
    });
    std::sort(plan.expired.begin(), plan.expired.end(), [](const auto &a, const auto &b) {
        return std::make_pair(a.at, a.taskId) < std::make_pair(b.at, b.taskId);
    });
    return plan;
}
bool ReminderLedger::MarkHandled(const std::vector<Key> &keys) {
    if (!ready_)
        return false;
    auto candidate = handled_;
    for (const auto &key : keys) {
        if (key.taskId.empty()) {
            error_ = ErrorCode::InvalidKey;
            return false;
        }
        candidate.emplace(key.taskId, key.at);
    }
    if (candidate == handled_)
        return true;
    std::string encoded;
    try {
        Json entries = Json::array();
        for (const auto &key : candidate)
            entries.push_back({{"taskId", key.first}, {"at", key.second}});
        encoded = Json{{"version", 1}, {"handled", std::move(entries)}}.dump();
    } catch (const Json::exception &) {
        error_ = ErrorCode::InvalidKey;
        return false;
    }
    if (!files_.Write(path_ + ".tmp", encoded)) {
        files_.Remove(path_ + ".tmp");
        error_ = ErrorCode::WriteFailed;
        return false;
    }
    if (!files_.Rename(path_ + ".tmp", path_)) {
        files_.Remove(path_ + ".tmp");
        error_ = ErrorCode::ReplaceFailed;
        return false;
    }
    handled_ = std::move(candidate);
    error_ = ErrorCode::None;
    if (!files_.Write(path_ + ".bak.tmp", encoded)) {
        files_.Remove(path_ + ".bak.tmp");
        error_ = ErrorCode::BackupWriteFailed;
    } else if (!files_.Rename(path_ + ".bak.tmp", path_ + ".bak")) {
        files_.Remove(path_ + ".bak.tmp");
        error_ = ErrorCode::BackupReplaceFailed;
    }
    return true;
}
} // namespace lume::reminder
