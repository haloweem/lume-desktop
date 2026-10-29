#pragma once
#include <string>
#include <string_view>
namespace lume::weather {
// Input is a bounded, validated UTF-8 place name, trimmed by the location
// resolver. Use the original OpenCC t->cn data unconditionally: the user's
// Simplified Chinese requirement also covers names skipped by the old trigger.
std::string SimplifyWeatherPlaceName(std::string_view);
}
