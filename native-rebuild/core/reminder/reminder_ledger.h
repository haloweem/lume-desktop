#pragma once
#include "core/settings/file_store.h"
#include "core/todo/todo_types.h"
#include <set>
#include <utility>

namespace lume::reminder {
struct Key {
    std::string taskId;
    todo::Millis at = 0;
};
struct Plan {
    std::vector<todo::TaskView> due;
    std::vector<Key> expired;
    std::optional<todo::Millis> nextAt;
};
enum class ErrorCode {
    None, NotLoaded, InvalidPath, ReadFailed, InvalidJson, UnsupportedVersion,
    InvalidKey, WriteFailed, ReplaceFailed, BackupRecovered, BackupWriteFailed,
    BackupReplaceFailed
};
// Owned by the local UI thread. Keys are local delivery history, not sync state.
class ReminderLedger {
  public:
    ReminderLedger(settings::IFileStore &files, std::string path);
    bool Load();
    bool Ready() const;
    Plan Evaluate(const todo::RepositorySnapshot &snapshot, todo::Millis now) const;
    // True means the primary was committed (or every key was already handled).
    // Backup failures after commit are reported by Error(), without undoing delivery history.
    bool MarkHandled(const std::vector<Key> &keys);
    bool Handled(const Key &key) const;
    ErrorCode Error() const;
  private:
    settings::IFileStore &files_;
    std::string path_;
    std::set<std::pair<std::string, todo::Millis>> handled_;
    bool ready_ = false;
    ErrorCode error_ = ErrorCode::NotLoaded;
};
} // namespace lume::reminder
