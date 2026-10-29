#pragma once
#include "core/calendar/calendar_settings.h"
#include <string>

namespace lume::calendar {
class CalendarPreferencesCodec final {
public:
    bool Decode(const std::string& text, Preferences& out) const;
    bool Encode(const Preferences& value, std::string& out) const;
};
}  // namespace lume::calendar
