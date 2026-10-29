#pragma once
#include <string>
#include <string_view>
namespace lume::platform::windows {
std::wstring OrganizerWide(std::string_view);
std::string OrganizerUtf8(std::wstring_view);
std::string OrganizerPathKey(std::string_view);
std::string OrganizerSearchKey(std::string_view);
}
