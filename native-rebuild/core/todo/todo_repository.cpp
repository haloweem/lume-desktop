#include "core/todo/todo_repository.h"
#include "core/todo/todo_json_codec.h"
#include "core/todo/todo_model.h"
#include "core/todo/todo_time.h"
#include <algorithm>
#include <limits>
#include <mutex>

namespace lume::todo {
struct TodoRepository::Impl {
    settings::IFileStore &files;
    const ILocalTimePort &time;
    std::string base, path;
    IdFactory ids;
    TodoJsonCodec codec;
    TodoState state;
    Scope scope;
    bool loaded = false, writable = false;
    bool enqueueSync = true;
    Millis last = std::numeric_limits<Millis>::min();
    Warning warning = Warning::None;
    mutable std::mutex mutex;
    Impl(settings::IFileStore &f, const ILocalTimePort &t, std::string b, IdFactory i)
        : files(f), time(t), base(std::move(b)), ids(std::move(i)) {}
    RepositorySnapshot Snapshot() const {
        RepositorySnapshot out;
        out.scope = scope;
        out.writable = writable;
        out.revision = state.revision;
        out.outboxCount = state.outbox.size();
        out.warning = warning;
        out.tasks.reserve(state.tasks.size());
        for (const auto &task : state.tasks)
            out.tasks.push_back(task.view);
        return out;
    }
    bool Isolate(const std::string &from) {
        std::string destination = from + ".corrupt";
        for (unsigned int n = 1; files.Exists(destination); ++n) {
            if (n == 10000)
                return false;
            destination = from + ".corrupt." + std::to_string(n);
        }
        return files.Rename(from, destination);
    }
    Error ReadFile(const std::string &name, TodoState &out) {
        std::string text;
        if (!files.Read(name, text))
            return Error::ReadFailed;
        return codec.Decode(text, out, time);
    }
    std::string NewId(const TodoState &candidate, std::vector<std::string> &used) {
        std::string value;
        try {
            if (ids)
                value = ids();
        } catch (const std::exception &) {
            return {};
        }
        if (value.empty() || !ValidUtf8Length(value, 500) ||
            std::find(used.begin(), used.end(), value) != used.end())
            return {};
        if (std::any_of(candidate.tasks.begin(), candidate.tasks.end(),
                        [&](const auto &task) { return task.view.id == value; }))
            return {};
        if (std::find(candidate.outboxIds.begin(), candidate.outboxIds.end(), value) !=
            candidate.outboxIds.end())
            return {};
        used.push_back(value);
        return value;
    }
};
TodoRepository::TodoRepository(settings::IFileStore &files, const ILocalTimePort &time, std::string base,
                               IdFactory ids, bool enqueueSync)
    : impl_(std::make_unique<Impl>(files, time, std::move(base), std::move(ids))) { impl_->enqueueSync=enqueueSync; }
TodoRepository::~TodoRepository() = default;
bool TodoRepository::ValidAccountKey(const std::string &key) noexcept {
    if (key == "local")
        return true;
    if (key.size() != 64)
        return false;
    return std::all_of(key.begin(), key.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
RepositorySnapshot TodoRepository::Read() const {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    return impl_->Snapshot();
}
LoadResult TodoRepository::SwitchAccount(const std::string &key) {
    auto &p = *impl_;
    std::lock_guard<std::mutex> guard(p.mutex);
    if (!ValidAccountKey(key) || p.base.empty())
        return {LoadSource::Unavailable, Error::InvalidAccount, p.Snapshot()};
    if (p.scope.generation == std::numeric_limits<std::uint64_t>::max())
        return {LoadSource::Unavailable, Error::LimitExceeded, p.Snapshot()};
    ++p.scope.generation;
    p.scope.accountKey = key;
    p.state = {};
    p.loaded = true;
    p.writable = false;
    p.warning = Warning::None;
    p.last = std::numeric_limits<Millis>::min();
    p.path = p.base + (p.base.back() == '/' || p.base.back() == '\\' ? "" : "/") + key + "/todo-data.json";
    const bool primary = p.files.Exists(p.path), backup = p.files.Exists(p.path + ".bak");
    Error error = Error::None;
    LoadSource source = LoadSource::Unavailable;
    if (primary) {
        error = p.ReadFile(p.path, p.state);
        if (error == Error::None)
            source = LoadSource::Primary;
        else if (error == Error::UnsupportedVersion)
            return {source, error, p.Snapshot()};
        else if (error == Error::ReadFailed)
            return {source, error, p.Snapshot()};
        else if (!p.Isolate(p.path))
            return {source, Error::IsolationFailed, p.Snapshot()};
    }
    if (source == LoadSource::Unavailable && backup) {
        const auto backupError = p.ReadFile(p.path + ".bak", p.state);
        if (backupError == Error::None) {
            source = LoadSource::Backup;
            p.warning = Warning::BackupRecovered;
        } else
            return {source, backupError, p.Snapshot()};
    }
    // A prior run may have isolated the only primary before finding no backup.
    // Keep that account read-only across restarts instead of silently creating data.
    if (!primary && !backup && !p.files.Exists(p.path + ".corrupt"))
        source = LoadSource::Fresh;
    if (source == LoadSource::Unavailable)
        return {source, error == Error::None ? Error::InvalidJson : error, p.Snapshot()};
    if (!p.state.inlineOutbox || source == LoadSource::Fresh) {
        const auto sidecar = p.path.substr(0, p.path.size() - 14) + "todo-outbox.json";
        if (p.files.Exists(sidecar)) {
            std::string text;
            if (!p.files.Read(sidecar, text))
                return {source, Error::ReadFailed, p.Snapshot()};
            const auto sideError = p.codec.DecodeSidecar(text, p.state.outbox, p.state.outboxIds);
            if (sideError != Error::None)
                return {source, sideError, p.Snapshot()};
        }
    }
    for (const auto &task : p.state.tasks)
        p.last = std::max(p.last, task.view.updatedAt);
    p.writable = true;
    return {source, error, p.Snapshot()};
}
CommandResult TodoRepository::Dispatch(const Command &command, Millis now) {
    auto &p = *impl_;
    std::lock_guard<std::mutex> guard(p.mutex);
    auto result = [&](Error error, bool committed = false, bool changed = false) {
        CommandResult r;
        r.ok = error == Error::None;
        r.committed = committed;
        r.changed = changed;
        r.error = error;
        r.warning = p.warning;
        r.snapshot = p.Snapshot();
        return r;
    };
    if (!p.loaded)
        return result(Error::NotLoaded);
    if (command.scope.accountKey != p.scope.accountKey || command.scope.generation != p.scope.generation)
        return result(Error::StaleScope);
    if (!p.writable)
        return result(Error::ReadOnly);
    if (command.type != CommandType::Add && command.type != CommandType::Update &&
        command.type != CommandType::Complete && command.type != CommandType::Restore &&
        command.type != CommandType::Delete)
        return result(Error::InvalidTask);
    auto found = std::find_if(p.state.tasks.begin(), p.state.tasks.end(),
                              [&](const auto &task) { return task.view.id == command.id; });
    if (command.type != CommandType::Add) {
        if (found == p.state.tasks.end())
            return result(Error::NotFound);
        if (found->view.deletedAt)
            return result(Error::Deleted);
        if (command.type == CommandType::Complete && found->view.completed)
            return result(Error::None);
        if (command.type == CommandType::Restore && !found->view.completed)
            return result(Error::None);
    }
    if (!ValidTimestamp(now, p.time))
        return result(Error::InvalidTime);
    const auto increment = CheckedAdd(p.last, 1);
    if (!increment)
        return result(Error::InvalidTime);
    const auto timestamp = std::max(now, *increment);
    if (!ValidTimestamp(timestamp, p.time))
        return result(Error::InvalidTime);
    TodoState candidate = p.state;
    std::vector<std::string> used;
    std::vector<std::size_t> changed;
    if (command.type == CommandType::Add) {
        TaskRecord task;
        task.view.id = p.NewId(candidate, used);
        if (task.view.id.empty())
            return result(Error::IdFailure);
        task.view.createdAt = timestamp;
        task.view.updatedAt = timestamp;
        task.sortKey = static_cast<double>(timestamp);
        const auto error = ApplyPatch(task, command.patch, true, p.time);
        if (error != Error::None)
            return result(error);
        changed.push_back(candidate.tasks.size());
        candidate.tasks.push_back(std::move(task));
    } else {
        const auto index = static_cast<std::size_t>(found - p.state.tasks.begin());
        auto &task = candidate.tasks[index];
        changed.push_back(index);
        if (command.type == CommandType::Update) {
            const auto error = ApplyPatch(task, command.patch, false, p.time);
            if (error != Error::None)
                return result(error);
            task.view.updatedAt = timestamp;
        } else if (command.type == CommandType::Complete) {
            std::string nextId;
            if (task.recurrence && task.view.dueAt) {
                nextId = p.NewId(candidate, used);
                if (nextId.empty())
                    return result(Error::IdFailure);
            }
            std::optional<TaskRecord> next;
            const auto error = CompleteTask(task, timestamp, nextId, next, p.time);
            if (error != Error::None)
                return result(error);
            if (next) {
                changed.push_back(candidate.tasks.size());
                candidate.tasks.push_back(std::move(*next));
            }
        } else if (command.type == CommandType::Restore) {
            task.view.completed = false;
            task.completedAt.reset();
            task.view.updatedAt = timestamp;
        } else {
            task.view.deletedAt = timestamp;
            task.view.updatedAt = timestamp;
            RefreshView(task, p.time);
        }
    }
    for (const auto index : changed) {
        if (!p.enqueueSync) break;
        const auto id = p.NewId(candidate, used);
        if (id.empty())
            return result(Error::IdFailure);
        std::string operation;
        const auto error = p.codec.Operation(candidate.tasks[index], id, command.type == CommandType::Delete,
                                             timestamp, operation);
        if (error != Error::None)
            return result(error);
        candidate.outbox.push_back(std::move(operation));
        candidate.outboxIds.push_back(id);
    }
    candidate.inlineOutbox = true;
    std::string encoded;
    const auto encodedError = p.codec.Encode(candidate, encoded);
    if (encodedError != Error::None)
        return result(encodedError);
    if (!p.files.Write(p.path + ".tmp", encoded)) {
        if (!p.files.Remove(p.path + ".tmp"))
            p.warning = Warning::CleanupFailed;
        return result(Error::WriteFailed);
    }
    if (!p.files.Rename(p.path + ".tmp", p.path)) {
        if (!p.files.Remove(p.path + ".tmp"))
            p.warning = Warning::CleanupFailed;
        return result(Error::ReplaceFailed);
    }
    // Only the primary replace commits. No failure after here may report rollback.
    p.state = std::move(candidate);
    p.last = timestamp;
    p.warning = Warning::None;
    if (!p.files.Write(p.path + ".bak.tmp", encoded)) {
        p.files.Remove(p.path + ".bak.tmp");
        p.warning = Warning::BackupWriteFailed;
    } else if (!p.files.Rename(p.path + ".bak.tmp", p.path + ".bak")) {
        p.files.Remove(p.path + ".bak.tmp");
        p.warning = Warning::BackupReplaceFailed;
    }
    return result(Error::None, true, true);
}
} // namespace lume::todo
