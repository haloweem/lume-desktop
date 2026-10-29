#pragma once
#include "core/calendar/calendar_date.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lume::todo {
using Millis = std::int64_t;
struct LocalDateTime {
    calendar::Date date;
    int hour = 0, minute = 0, second = 0, millisecond = 0;
};
bool operator==(const LocalDateTime &a, const LocalDateTime &b) noexcept;
class ILocalTimePort {
  public:
    virtual ~ILocalTimePort() = default;
    virtual std::optional<LocalDateTime> ToLocal(Millis value) const = 0;
    // A fold's choice belongs to the port. Gaps must fail; callers verify round-trip.
    virtual std::optional<Millis> FromLocal(const LocalDateTime &value) const = 0;
};
struct Schedule {
    Millis startAt = 0, endAt = 0;
};
enum class Color {
    None,
    Red,
    Orange,
    Yellow,
    Green,
    Blue
};
enum class ReminderKind {
    None,
    Relative,
    LegacyAbsolute
};
struct ReminderDraft {
    ReminderKind kind = ReminderKind::None;
    Millis leadMilliseconds = 0;
    Millis absoluteAt = 0;
    // Read-only indication for UI. Dispatch never trusts this flag to bypass validation.
    bool legacyRelative = false;
};
enum class Frequency {
    Daily,
    Weekdays,
    Weekly,
    Monthly
};
struct Recurrence {
    Frequency frequency = Frequency::Daily;
    int interval = 1;
};
inline bool operator==(const Recurrence &a, const Recurrence &b) noexcept {
    return a.frequency == b.frequency && a.interval == b.interval;
}
inline bool operator!=(const Recurrence &a, const Recurrence &b) noexcept { return !(a == b); }
struct TaskView {
    std::string id, title, notes;
    bool completed = false;
    std::optional<Millis> dueAt, reminderAt, deletedAt;
    std::optional<Schedule> schedule;
    Color color = Color::None;
    ReminderDraft reminder;
    std::optional<calendar::Date> day;
    std::optional<Recurrence> recurrence;
    // A computed later occurrence of a repeating schedule; never stored. Its id
    // is the stored task's, so commands act on the whole series.
    bool projected = false;
    Millis createdAt = 0, updatedAt = 0;
    std::int64_t revision = 0;
};
struct TaskPatch {
    std::optional<std::string> title, notes;
    std::optional<Schedule> schedule;
    std::optional<Color> color;
    // Omit to preserve the previous reminder (relative reminders move with start).
    // Explicit None clears it; explicit Relative requires integral 0..525600 minutes.
    std::optional<ReminderDraft> reminder;
    // Omit to preserve; an empty inner value stops repeating.
    std::optional<std::optional<Recurrence>> recurrence;
};
struct Scope {
    std::string accountKey;
    std::uint64_t generation = 0;
};
enum class CommandType {
    Add,
    Update,
    Complete,
    Restore,
    Delete
};
struct Command {
    Scope scope;
    CommandType type = CommandType::Add;
    std::string id;
    TaskPatch patch;
};
enum class Error {
    None,
    InvalidAccount,
    StaleScope,
    NotLoaded,
    ReadOnly,
    InvalidJson,
    UnsupportedVersion,
    ReadFailed,
    IsolationFailed,
    InvalidTask,
    InvalidTime,
    InvalidReminder,
    NotFound,
    Deleted,
    IdFailure,
    WriteFailed,
    ReplaceFailed,
    LimitExceeded
};
enum class Warning {
    None,
    BackupRecovered,
    BackupWriteFailed,
    BackupReplaceFailed,
    CleanupFailed
};
enum class LoadSource {
    Fresh,
    Primary,
    Backup,
    Unavailable
};
struct RepositorySnapshot {
    Scope scope;
    bool writable = false;
    std::int64_t revision = 0;
    std::vector<TaskView> tasks;
    std::size_t outboxCount = 0;
    Warning warning = Warning::None;
};
struct LoadResult {
    LoadSource source = LoadSource::Unavailable;
    Error error = Error::None;
    RepositorySnapshot snapshot;
};
struct CommandResult {
    // ok includes a valid no-op; committed means the primary replace took place.
    bool ok = false, committed = false, changed = false;
    Error error = Error::None;
    Warning warning = Warning::None;
    RepositorySnapshot snapshot;
};
} // namespace lume::todo
