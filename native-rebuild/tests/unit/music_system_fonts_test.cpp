#include "platform/windows/music_system_fonts.h"

#include "vendor/nlohmann/json.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>

namespace {
using Json = nlohmann::json;
using lume::music::fonts::HandleMusicSystemFonts;

void Expect(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

Json State(const std::wstring& message) {
    const auto result = HandleMusicSystemFonts(message);
    Expect(result.has_value(), "valid font request returns state");
    const auto json = Json::parse(result->begin(), result->end());
    Expect(json["type"] == "music-fonts-state", "font state type");
    return json;
}
}

int main() {
    for (const auto* malformed : {
        L"",
        LR"({"version":1,"scope":"music-fonts","action":"snapshot","request":1})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":0})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":1.5})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":9007199254740992})",
        LR"({"version":1,"scope":"other","action":"list","request":1})",
        LR"({"version":2,"scope":"music-fonts","action":"list","request":1})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":1,"extra":true})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":1,"request":2})",
        LR"({"version":1.0,"scope":"music-fonts","action":"list","request":1})",
        LR"({"version":true,"scope":"music-fonts","action":"list","request":1})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":true})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":"1"})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":-1})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":1e0})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":18446744073709551616})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":1,"requ\u0065st":2})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":1,"extra":{"nested":1}})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":[]})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":null})",
        LR"({"version":1,"scope":"music-fonts","action":"list"})",
        LR"({"version":1,"scope":"music-fonts","action":"\ud800","request":1})",
        LR"({"version":1,"scope":"music-fonts","action":"list","request":1} trailing)",
        LR"([{"version":1,"scope":"music-fonts","action":"list","request":1}])"
    }) Expect(!HandleMusicSystemFonts(malformed), "malformed font command rejected");
    Expect(!HandleMusicSystemFonts(std::wstring(2049, L' ')), "oversized command rejected");
    Expect(!HandleMusicSystemFonts(std::wstring(1, static_cast<wchar_t>(0xd800))), "invalid UTF16 rejected");
    auto nul = std::wstring(LR"({"version":1,"scope":"music-fonts","action":"list","request":1})");
    nul.push_back(L'\0');
    nul += L"trailing";
    Expect(!HandleMusicSystemFonts(nul), "embedded NUL rejected");

    const auto json = State(
        LR"({"version":1,"scope":"music-fonts","action":"list","request":7})");
    Expect(json.size() == 5 && json["version"] == 1 && json["request"] == 7 && json["error"] == "",
        "real font enumeration succeeds");
    Expect(json["families"].is_array() && !json["families"].empty(),
        "real font enumeration returns installed families");
    std::set<std::string> unique;
    std::vector<std::wstring> ordinalUnique;
    std::string previous;
    for (const auto& value : json["families"]) {
        Expect(value.is_string() && !value.get<std::string>().empty(),
            "family names are nonempty strings");
        const auto family = value.get<std::string>();
        Expect(family.size() <= 128, "family fits preference UTF8 byte limit");
        const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            family.data(), static_cast<int>(family.size()), nullptr, 0);
        Expect(length > 0, "family is valid UTF8");
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        Expect(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            family.data(), static_cast<int>(family.size()), wide.data(), length) == length,
            "family decodes to UTF16");
        Expect(std::none_of(wide.begin(), wide.end(), [](wchar_t c) {
            return c < 32 || (c >= 127 && c <= 159);
        }), "family contains no C0, DEL or C1 control codepoints");
        for (const auto& prior : ordinalUnique)
            Expect(CompareStringOrdinal(prior.data(), static_cast<int>(prior.size()),
                wide.data(), length, TRUE) != CSTR_EQUAL, "case-insensitive family deduplication");
        ordinalUnique.push_back(std::move(wide));
        Expect(family.front() != '@', "vertical font families are excluded");
        Expect(unique.insert(family).second, "families are deduplicated");
        if (!previous.empty()) Expect(previous < family, "families are sorted");
        previous = family;
        Expect(family.find('\\') == std::string::npos &&
            family.find('/') == std::string::npos, "font paths are not returned");
    }
    Expect(json["families"].size() <= 4096, "family result is bounded");
    const auto second = State(
        LR"({"version":1,"scope":"music-fonts","action":"list","request":9007199254740991})");
    Expect(second["request"] == 9007199254740991ULL && second["families"] == json["families"],
        "repeated requests are independent and maximum safe integer is accepted");
    std::cout << "music system font bridge passed\n";
}
