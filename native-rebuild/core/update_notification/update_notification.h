#pragma once
#include "core/application_settings/application_settings.h"
#include <functional>
#include <string>

namespace lume::update_notification {
struct ClockSample {
    std::int64_t utcMillis = 0;
    int localDay = 0;
};
enum class NoticeKind { Current, Incompatible, Disabled, Error, Available };
struct Notice {
    NoticeKind kind = NoticeKind::Error;
    std::string version, currentVersion, error;
    bool manual = false;
};
struct LedgerState {
    std::string checkedForVersion, notifiedVersion;
    int checkedDay = 0, notifiedDay = 0;
};

// One shell/UI-thread owner. No timers, network, windows, or account state.
class Policy final {
public:
    using Show = std::function<bool(const Notice&)>;
    explicit Policy(application_settings::IPreferencesFile&,
                    std::string currentVersion = application_settings::CurrentVersion);
    bool Load();
    bool AutomaticDue(ClockSample) const;
    bool Complete(bool manual, const application_settings::ManifestResult&, ClockSample, const Show&);
    bool Flush();
    const LedgerState& State() const noexcept;
    const std::string& Error() const noexcept;
private:
    application_settings::IPreferencesFile& file_;
    std::string currentVersion_, error_, storageError_;
    LedgerState state_;
    bool loaded_ = false, writable_ = false, dirty_ = false;
};
}
