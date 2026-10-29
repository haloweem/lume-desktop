#include "core/radial/radial_menu.h"
#include "vendor/nlohmann/json.hpp"

#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <utility>

namespace lume::radial {
namespace {
using Json = nlohmann::json;
constexpr int SlotCount = 10;
constexpr double Center = Diameter / 2.0;
constexpr double SlotRadius = (InnerRadius + OuterRadius) / 2.0;
constexpr double Pi = 3.14159265358979323846;
constexpr double SlotAngle = 2.0 * Pi / SlotCount;
constexpr unsigned MaxShortcut = 4095;
constexpr std::size_t MaxPreferencesBytes = 4096;

const wchar_t* NamedKey(unsigned key) {
    switch (key) {
    case 0x08: return L"Backspace";
    case 0x09: return L"Tab";
    case 0x0d: return L"Enter";
    case 0x13: return L"Pause";
    case 0x14: return L"Caps Lock";
    case 0x20: return L"Space";
    case 0x21: return L"Page Up";
    case 0x22: return L"Page Down";
    case 0x23: return L"End";
    case 0x24: return L"Home";
    case 0x25: return L"Left";
    case 0x26: return L"Up";
    case 0x27: return L"Right";
    case 0x28: return L"Down";
    case 0x2c: return L"Print Screen";
    case 0x2d: return L"Insert";
    case 0x2e: return L"Delete";
    case 0x6a: return L"Numpad *";
    case 0x6b: return L"Numpad +";
    case 0x6c: return L"Numpad Separator";
    case 0x6d: return L"Numpad -";
    case 0x6e: return L"Numpad .";
    case 0x6f: return L"Numpad /";
    case 0x90: return L"Num Lock";
    case 0x91: return L"Scroll Lock";
    case 0xba: return L";";
    case 0xbb: return L"=";
    case 0xbc: return L",";
    case 0xbd: return L"-";
    case 0xbe: return L".";
    case 0xbf: return L"/";
    case 0xc0: return L"`";
    case 0xdb: return L"[";
    case 0xdc: return L"\\";
    case 0xdd: return L"]";
    case 0xde: return L"'";
    case 0xe2: return L"OEM 102";
    default: return nullptr;
    }
}

bool Alphanumeric(unsigned key) {
    return (key >= 0x30 && key <= 0x39) || (key >= 0x41 && key <= 0x5a);
}

bool ValidKey(unsigned key) {
    return Alphanumeric(key) || (key >= 0x60 && key <= 0x69) ||
        (key >= 0x70 && key <= 0x87) || NamedKey(key) != nullptr;
}
}  // namespace

bool InRing(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    const double dx = static_cast<double>(x) - Center;
    const double dy = static_cast<double>(y) - Center;
    const double squared = dx * dx + dy * dy;
    return squared >= InnerRadius * InnerRadius && squared <= OuterRadius * OuterRadius;
}

int HitSlot(float x, float y) {
    if (!InRing(x, y)) return -1;
    double angle = std::atan2(static_cast<double>(x) - Center, Center - static_cast<double>(y));
    if (angle < 0) angle += 2.0 * Pi;
    return static_cast<int>(std::floor(angle / SlotAngle + 0.5)) % SlotCount;
}

Point SlotPosition(int slot) {
    if (slot < 0 || slot >= SlotCount) return {static_cast<float>(Center), static_cast<float>(Center)};
    const double angle = slot * SlotAngle;
    return {static_cast<float>(Center + SlotRadius * std::sin(angle)),
            static_cast<float>(Center - SlotRadius * std::cos(angle))};
}

bool ValidShortcut(unsigned code) {
    if (code == 0 || code > MaxShortcut) return false;
    const unsigned key = code % 256;
    const unsigned modifiers = code / 256;
    if (!ValidKey(key)) return false;
    if (key == 0x09 && (modifiers & (1u | 8u))) return false;
    if (key == 0x2e && (modifiers & 3u) == 3u) return false;
    if (key == 0x4c && (modifiers & 8u)) return false;
    return true;
}

bool Valid(const Preferences& preferences) {
    return ValidShortcut(preferences.shortcutCode);
}

bool Encode(const Preferences& preferences, std::string& output) {
    if (!Valid(preferences)) return false;
    try {
        auto text = Json{{"version", 1}, {"enabled", preferences.enabled},
            {"frosted", preferences.frosted}, {"shortcutCode", preferences.shortcutCode}}.dump();
        output = std::move(text);
        return true;
    } catch (...) { return false; }
}

bool Decode(const std::string& input, Preferences& output) {
    // The JSON lexer treats raw NUL as EOF, so reject it before strict parsing.
    if (input.empty() || input.size() > MaxPreferencesBytes || input.find('\0') != std::string::npos) return false;
    try {
        std::set<std::string> keys;
        const auto json = Json::parse(input, [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 1 || event == Json::parse_event_t::array_start ||
                (event == Json::parse_event_t::object_start && depth != 0))
                throw std::invalid_argument("nested radial preferences");
            if (event == Json::parse_event_t::key && !keys.insert(value.get<std::string>()).second)
                throw std::invalid_argument("duplicate radial preference");
            return true;
        });
        if (!json.is_object() || json.size() != 4 || !json.at("version").is_number_integer() ||
            json.at("version") != 1 || !json.at("enabled").is_boolean() ||
            !json.at("frosted").is_boolean()) return false;
        const auto& shortcut = json.at("shortcutCode");
        if (!shortcut.is_number_integer() ||
            (!shortcut.is_number_unsigned() && shortcut.get<std::int64_t>() < 0)) return false;
        const auto code = shortcut.get<std::uint64_t>();
        if (code > MaxShortcut) return false;
        const Preferences parsed{json.at("enabled").get<bool>(), json.at("frosted").get<bool>(),
            static_cast<unsigned>(code)};
        if (!Valid(parsed)) return false;
        output = parsed;
        return true;
    } catch (...) { return false; }
}

std::wstring ShortcutLabel(unsigned code) {
    if (!ValidShortcut(code)) return {};
    const unsigned key = code % 256;
    const unsigned modifiers = code / 256;
    std::wstring label;
    if (modifiers & 2u) label += L"Ctrl+";
    if (modifiers & 1u) label += L"Alt+";
    if (modifiers & 4u) label += L"Shift+";
    if (modifiers & 8u) label += L"Win+";
    if (Alphanumeric(key)) label += static_cast<wchar_t>(key);
    else if (key >= 0x60 && key <= 0x69) label += L"Numpad " + std::to_wstring(key - 0x60);
    else if (key >= 0x70 && key <= 0x87) label += L"F" + std::to_wstring(key - 0x70 + 1);
    else label += NamedKey(key);
    return label;
}

}  // namespace lume::radial
