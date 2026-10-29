#pragma once
#include "core/todo/todo_types.h"
#include <windows.h>
#include <map>
#include <functional>

namespace lume::platform::windows {
// Local civil conversion without CRT's 1970/3000 date limits. No system clock
// reads occur during conversion; explicit Refresh picks up timezone changes.
class WindowsLocalTime final : public todo::ILocalTimePort {
public:
    WindowsLocalTime();
    using ZoneReader=std::function<DWORD(DYNAMIC_TIME_ZONE_INFORMATION*)>;
    explicit WindowsLocalTime(ZoneReader);
    explicit WindowsLocalTime(TIME_ZONE_INFORMATION fixedRules);
    explicit WindowsLocalTime(DYNAMIC_TIME_ZONE_INFORMATION snapshot);
    bool Refresh();
    std::uint64_t Revision() const {return revision_;}
    std::optional<todo::LocalDateTime> ToLocal(todo::Millis) const override;
    // Ambiguous fall-back minutes choose the earlier instant; gaps are rejected.
    std::optional<todo::Millis> FromLocal(const todo::LocalDateTime&) const override;
    static todo::Millis NowMillis();
private:
    std::optional<TIME_ZONE_INFORMATION> Rules(int year) const;
    DYNAMIC_TIME_ZONE_INFORMATION zone_{};
    TIME_ZONE_INFORMATION fixed_{};
    bool fixedRules_=false,injected_=false,ready_=false;
    mutable std::map<int,TIME_ZONE_INFORMATION> rules_;
    ZoneReader reader_;
    std::uint64_t revision_=0;
};
}
