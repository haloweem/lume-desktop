#include "platform/windows/music_system_fonts.h"

#include "vendor/nlohmann/json.hpp"

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>

namespace lume::music::fonts {
namespace {

using Json = nlohmann::json;
constexpr std::size_t MaxMessageChars = 2048;
constexpr std::size_t MaxFamilies = 4096;
constexpr std::uint64_t MaxSafeInteger = 9007199254740991ULL;

std::optional<std::string> Utf8(std::wstring_view value) {
    if (value.empty() || value.size() > INT_MAX) return {};
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!length) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), length, nullptr, nullptr)) return {};
    return result;
}

bool Keys(const Json& value) {
    if (!value.is_object() || value.size() != 4) return false;
    return value.contains("version") && value.contains("scope") &&
        value.contains("action") && value.contains("request");
}

struct FamilyLess {
    bool operator()(const std::wstring& a, const std::wstring& b) const {
        const auto comparison = CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
            b.data(), static_cast<int>(b.size()), TRUE);
        if (!comparison) throw std::runtime_error("unavailable");
        return comparison == CSTR_LESS_THAN;
    }
};

struct FontList {
    std::set<std::wstring, FamilyLess> families;
    bool failed = false;
};

int CALLBACK CollectFont(const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM parameter) noexcept {
    auto* list = reinterpret_cast<FontList*>(parameter);
    try {
        if (!font || font->lfFaceName[0] == L'@') return 1;
        const auto end = std::find(std::begin(font->lfFaceName), std::end(font->lfFaceName), L'\0');
        if (end == std::end(font->lfFaceName)) return 1;
        const std::wstring_view name(font->lfFaceName, static_cast<std::size_t>(end - font->lfFaceName));
        if (name.empty() || std::any_of(name.begin(), name.end(), [](wchar_t c) {
            return c < 32 || (c >= 127 && c <= 159) ||
                c == L'/' || c == L'\\' || c == L':';
        })) return 1;
        const auto family = Utf8(name);
        if (!family || family->size() > 128) return 1;
        const std::wstring wideName(name);
        if (list->families.size() >= MaxFamilies &&
            !list->families.count(wideName)) {
            list->failed = true;
            return 0;
        }
        list->families.insert(wideName);
        return 1;
    } catch (...) {
        list->failed = true;
        return 0;
    }
}

std::wstring State(std::uint64_t request, const FontList& list) {
    Json families = Json::array();
    if (!list.failed) {
        std::set<std::string> sorted;
        for (const auto& family : list.families) {
            const auto utf8 = Utf8(family);
            if (!utf8) throw std::runtime_error("unavailable");
            sorted.insert(*utf8);
        }
        for (const auto& family : sorted) families.push_back(family);
    }
    const auto ascii = Json{{"version", 1}, {"type", "music-fonts-state"}, {"request", request},
        {"families", std::move(families)},
        {"error", list.failed ? "unavailable" : ""}}.dump(-1, ' ', true);
    return {ascii.begin(), ascii.end()};
}

std::optional<Json> Parse(std::wstring_view message) {
    if (message.empty() || message.size() > MaxMessageChars ||
        message.find(L'\0') != message.npos) return {};
    const auto utf8 = Utf8(message);
    if (!utf8) return {};
    bool invalid = false;
    std::set<std::string> keys;
    const auto json = Json::parse(*utf8,
        [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 1) { invalid = true; return false; }
            if (event == Json::parse_event_t::key) {
                if (!keys.insert(value.get<std::string>()).second) invalid = true;
            }
            return true;
        }, false);
    if (invalid || json.is_discarded() || !Keys(json) ||
        !json.at("version").is_number_unsigned() || json.at("version") != 1 ||
        json.at("scope") != "music-fonts" || json.at("action") != "list" ||
        !json.at("request").is_number_unsigned()) return {};
    const auto request = json.at("request").get<std::uint64_t>();
    if (!request || request > MaxSafeInteger) return {};
    return json;
}

}  // namespace

std::optional<std::wstring> HandleMusicSystemFonts(std::wstring_view message) {
    std::optional<Json> command;
    try {
        command = Parse(message);
    } catch (...) { return {}; }
    if (!command) return {};
    const auto id = command->at("request").get<std::uint64_t>();
    try {
        FontList list;
        LOGFONTW request{};
        request.lfCharSet = DEFAULT_CHARSET;
        HDC dc = GetDC(nullptr);
        if (!dc) list.failed = true;
        else {
            const auto result = EnumFontFamiliesExW(dc, &request, CollectFont,
                reinterpret_cast<LPARAM>(&list), 0);
            ReleaseDC(nullptr, dc);
            if (!result) list.failed = true;
        }
        return State(id, list);
    } catch (...) {
        FontList failed;
        failed.failed = true;
        return State(id, failed);
    }
}

}  // namespace lume::music::fonts
