#include "core/update_notification/update_notification.h"
#include "core/application_settings/json_codec.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace lume::update_notification {
namespace {
bool ValidDay(int day) {
    const int year = day / 10000, month = day / 100 % 100, date = day % 100;
    if (year < 1601 || year > 9999 || month < 1 || month > 12 || date < 1) return false;
    constexpr int days[]{31,28,31,30,31,30,31,31,30,31,30,31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return date <= days[month - 1] + (month == 2 && leap ? 1 : 0);
}
LedgerState Decode(const std::string& bytes) {
    if (bytes.size() > 4096) throw std::runtime_error("size");
    const auto json = application_settings::detail::ParseObject(bytes);
    if (json.size() != 5 || !json.at("version").is_number_integer() || json.at("version") != 1)
        throw std::runtime_error("schema");
    LedgerState state;
    const auto entry = [&](const char* dayKey, const char* versionKey, int& day, std::string& version) {
        const auto& value = json.at(dayKey);
        if (!value.is_number_integer() || value < 0 || value > 99991231 ||
            !json.at(versionKey).is_string()) throw std::runtime_error("type");
        day = value.get<int>();
        version = json.at(versionKey).get<std::string>();
        if ((day == 0) != version.empty() || (day && (!ValidDay(day) ||
            !application_settings::CompareVersions(version, version)))) throw std::runtime_error("entry");
    };
    entry("checkedDay", "checkedForVersion", state.checkedDay, state.checkedForVersion);
    entry("notifiedDay", "notifiedVersion", state.notifiedDay, state.notifiedVersion);
    return state;
}
std::string Encode(const LedgerState& state) {
    return nlohmann::json{{"version", 1}, {"checkedForVersion", state.checkedForVersion},
        {"checkedDay", state.checkedDay}, {"notifiedVersion", state.notifiedVersion},
        {"notifiedDay", state.notifiedDay}}.dump();
}
Notice Classify(bool manual, const application_settings::ManifestResult& result, const std::string& current) {
    Notice notice{NoticeKind::Error, result.update.version, current, result.error, manual};
    const auto comparison = application_settings::CompareVersions(result.update.version, current);
    const bool compatibleResult = result.update.status == "incompatible" &&
        (result.error == "update-incompatible" || result.error == "update-disabled");
    const bool available = result.update.status == "available" && result.error.empty();
    if (comparison && ((result.update.status == "current" && result.error.empty()) || compatibleResult || available)) {
        if (*comparison <= 0) { notice.kind = NoticeKind::Current; notice.error.clear(); }
        else if (available) notice.kind = NoticeKind::Available;
        else if (compatibleResult)
            notice.kind = result.error == "update-disabled" ? NoticeKind::Disabled : NoticeKind::Incompatible;
    }
    if (notice.kind == NoticeKind::Error && notice.error.empty()) notice.error = "update-manifest";
    return notice;
}
}
Policy::Policy(application_settings::IPreferencesFile& file, std::string current)
    : file_(file), currentVersion_(std::move(current)) {}
bool Policy::Load() {
    if (loaded_) return writable_;
    loaded_ = true;
    if (!application_settings::CompareVersions(currentVersion_, currentVersion_)) {
        storageError_ = "notification-version"; return false;
    }
    try {
        const auto read = file_.Read();
        if (read.status == application_settings::ReadStatus::Failed) {
            storageError_ = "notification-read"; return false;
        }
        if (read.status == application_settings::ReadStatus::Loaded) state_ = Decode(read.bytes);
        writable_ = true; storageError_.clear(); return true;
    } catch (...) { storageError_ = "notification-state"; return false; }
}
bool Policy::AutomaticDue(ClockSample now) const {
    return loaded_ && writable_ && storageError_ != "notification-read" && ValidDay(now.localDay) &&
        (state_.checkedForVersion != currentVersion_ || now.localDay > state_.checkedDay);
}
bool Policy::Complete(bool manual, const application_settings::ManifestResult& result,
                      ClockSample now, const Show& show) {
    const auto notice = Classify(manual, result, currentVersion_);
    if (!manual && notice.kind == NoticeKind::Error) return true;
    if (!loaded_) Load();
    if (!manual && (!writable_ || !ValidDay(now.localDay))) return false;
    const bool newer = notice.kind == NoticeKind::Incompatible || notice.kind == NoticeKind::Disabled || notice.kind == NoticeKind::Available;
    const bool display = manual || (newer && (now.localDay > state_.notifiedDay ||
        (now.localDay == state_.notifiedDay && notice.version != state_.notifiedVersion)));
    if (display) {
        try {
            if (!show || !show(notice)) { error_ = "notification-present"; return false; }
        } catch (...) { error_ = "notification-present"; return false; }
        error_.clear();
    }
    if (notice.kind == NoticeKind::Error) return true;
    if (!ValidDay(now.localDay)) { error_ = "notification-clock"; return false; }
    error_.clear();
    // Keep memory committed once visible, even if persistence fails. Only a
    // successful atomic write can extend that guarantee across process restarts.
    if (newer && display) {
        state_.notifiedDay = std::max(state_.notifiedDay, now.localDay);
        state_.notifiedVersion = notice.version;
        dirty_ = true;
    }
    const int checked = state_.checkedForVersion == currentVersion_
        ? std::max(state_.checkedDay, now.localDay) : now.localDay;
    if (state_.checkedForVersion != currentVersion_ || state_.checkedDay != checked) {
        state_.checkedForVersion = currentVersion_; state_.checkedDay = checked; dirty_ = true;
    }
    return writable_ && Flush();
}
bool Policy::Flush() {
    if (!loaded_ || !writable_) return false;
    if (!dirty_) return true;
    try {
        const auto current = file_.Read();
        if (current.status == application_settings::ReadStatus::Failed) {
            storageError_ = "notification-read"; return false;
        }
        if (current.status == application_settings::ReadStatus::Loaded) {
            try { Decode(current.bytes); }
            catch (...) { writable_ = false; storageError_ = "notification-state"; return false; }
        }
        if (!file_.WriteAtomically(Encode(state_))) { storageError_ = "notification-save"; return false; }
        dirty_ = false; storageError_.clear(); return true;
    } catch (...) { storageError_ = "notification-save"; return false; }
}
const LedgerState& Policy::State() const noexcept { return state_; }
const std::string& Policy::Error() const noexcept { return error_.empty() ? storageError_ : error_; }
}
