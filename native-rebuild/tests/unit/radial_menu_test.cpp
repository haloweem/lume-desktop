#include "core/radial/radial_menu.h"
#include "vendor/nlohmann/json.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace lume::radial;
using Json = nlohmann::json;
constexpr double Pi = 3.14159265358979323846;

void Check(bool value, const std::string& reason) {
    if (!value) throw std::runtime_error(reason);
}

bool Near(float value, double expected) {
    return std::abs(static_cast<double>(value) - expected) < 0.001;
}

bool Same(const Preferences& left, const Preferences& right) {
    return left.enabled == right.enabled && left.frosted == right.frosted &&
        left.shortcutCode == right.shortcutCode;
}

Point Polar(double degrees, double radius = 114.0) {
    const double angle = degrees * Pi / 180.0;
    return {static_cast<float>(160.0 + radius * std::sin(angle)),
            static_cast<float>(160.0 - radius * std::cos(angle))};
}

unsigned Code(unsigned key, unsigned modifiers) {
    return key + modifiers * 256;
}

Json Document() {
    return Json{{"version", 1}, {"enabled", false}, {"frosted", false}, {"shortcutCode", 9}};
}

void GeometryAndDefaults() {
    Check(Diameter == 320 && OuterRadius == 152 && InnerRadius == 76, "fixed DIP geometry");
    const Preferences value;
    Check(!value.enabled && !value.frosted && value.shortcutCode == 9, "disabled liquid-glass Tab defaults");
}

void SlotCenters() {
    for (int slot = 0; slot < 10; ++slot) {
        const auto point = SlotPosition(slot);
        const auto expected = Polar(slot * 36.0);
        Check(Near(point.x, expected.x) && Near(point.y, expected.y), "equidistant clockwise center " + std::to_string(slot));
        Check(Near(static_cast<float>(std::hypot(point.x - 160.0, point.y - 160.0)), 114), "slot center radius");
    }
    Check(Near(SlotPosition(0).x, 160) && Near(SlotPosition(0).y, 46), "slot zero starts at top");
    Check(Near(SlotPosition(5).x, 160) && Near(SlotPosition(5).y, 274), "slot five is at bottom");
    for (const int slot : {-1, 10, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        const auto point = SlotPosition(slot);
        Check(point.x == 160 && point.y == 160, "invalid slots return the inactive center");
    }
}

void RingBounds() {
    Check(!InRing(160, 160), "transparent center is not in ring");
    for (const auto direction : {Point{1, 0}, Point{-1, 0}, Point{0, 1}, Point{0, -1}}) {
        for (const float radius : {76.0f, 114.0f, 152.0f}) {
            Check(InRing(160 + direction.x * radius, 160 + direction.y * radius), "inclusive radial boundaries");
        }
        for (const float radius : {0.0f, 75.999f, 152.001f, 160.0f, 1000.0f}) {
            Check(!InRing(160 + direction.x * radius, 160 + direction.y * radius), "reject inner hole and outer margin");
        }
    }
    Check(!InRing(8, 8) && !InRing(312, 312), "square corners are outside annulus");
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const float value : {infinity, -infinity, nan, std::numeric_limits<float>::max()}) {
        Check(!InRing(value, 160) && !InRing(160, value), "reject non-finite and extreme coordinates");
        Check(HitSlot(value, 160) == -1 && HitSlot(160, value) == -1, "invalid coordinates have no slot");
    }
}

void HitTesting() {
    for (int slot = 0; slot < 10; ++slot) {
        const auto center = SlotPosition(slot);
        Check(HitSlot(center.x, center.y) == slot, "each center hits its slot");
        for (const double offset : {-17.99, 0.0, 17.99}) {
            const auto point = Polar(slot * 36.0 + offset);
            Check(HitSlot(point.x, point.y) == slot, "slot covers its full 36-degree wedge");
        }
        const auto clockwise = Polar(slot * 36.0 + 18.01);
        const auto counterclockwise = Polar(slot * 36.0 - 18.01);
        Check(HitSlot(clockwise.x, clockwise.y) == (slot + 1) % 10, "clockwise boundary advances");
        Check(HitSlot(counterclockwise.x, counterclockwise.y) == (slot + 9) % 10, "counterclockwise boundary wraps");
    }
    for (int step = 0; step < 3600; ++step) {
        const double degrees = step * 0.1 + 0.05;
        const int expected = static_cast<int>(std::floor((degrees + 18.0) / 36.0)) % 10;
        const auto point = Polar(degrees);
        Check(HitSlot(point.x, point.y) == expected, "full ring has ten bounded slots and no gaps");
    }
    Check(HitSlot(160, 84) == 0 && HitSlot(160, 8) == 0, "top radial boundaries hit slot zero");
    Check(HitSlot(160, 236) == 5 && HitSlot(160, 312) == 5, "bottom radial boundaries hit slot five");
    for (const auto point : {Point{160, 160}, Point{160, 84.001f}, Point{160, 7.999f}, Point{0, 0}}) {
        Check(HitSlot(point.x, point.y) == -1, "points outside ring have no slot");
    }
}

void ShortcutValidation() {
    for (const unsigned key : {8u, 9u, 13u, 19u, 20u, 32u, 33u, 34u, 35u, 36u, 37u, 38u,
                               39u, 40u, 44u, 45u, 46u, 144u, 145u, 186u, 187u, 188u,
                               189u, 190u, 191u, 192u, 219u, 220u, 221u, 222u, 226u}) {
        Check(ValidShortcut(key), "supported navigation or punctuation key " + std::to_string(key));
    }
    for (unsigned key = 48; key <= 57; ++key) Check(ValidShortcut(key), "digits allowed");
    for (unsigned key = 65; key <= 90; ++key) Check(ValidShortcut(key), "letters allowed");
    for (unsigned key = 96; key <= 111; ++key) Check(ValidShortcut(key), "numpad allowed");
    for (unsigned key = 112; key <= 135; ++key) Check(ValidShortcut(key), "F1 through F24 allowed");
    for (unsigned modifiers = 0; modifiers < 16; ++modifiers) {
        Check(ValidShortcut(Code(65, modifiers)), "all four modifier bits compose with a key");
        for (const unsigned key : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 10u, 16u, 17u, 18u,
                                   27u, 47u, 58u, 64u, 91u, 92u, 93u, 95u, 136u, 143u,
                                   160u, 161u, 162u, 163u, 164u, 165u, 185u, 193u, 218u,
                                   223u, 224u, 225u, 229u, 231u, 255u}) {
            Check(!ValidShortcut(Code(key, modifiers)), "unsupported and modifier-only keys rejected");
        }
        Check(!ValidShortcut(Code(9, modifiers)) || (modifiers & 9) == 0, "Alt/Win Tab excluded");
        if ((modifiers & 9) == 0) Check(ValidShortcut(Code(9, modifiers)), "nonreserved Tab modifiers allowed");
        Check(ValidShortcut(Code(46, modifiers)) == ((modifiers & 3) != 3), "Ctrl Alt Delete excluded even with extra modifiers");
        Check(ValidShortcut(Code(76, modifiers)) == ((modifiers & 8) == 0), "Win L excluded even with extra modifiers");
    }
    Check(ValidShortcut(4066), "upper modifier mask with OEM 102 key");
    for (const unsigned code : {0u, 4095u, 4096u, 4105u, 65545u, std::numeric_limits<unsigned>::max()}) {
        Check(!ValidShortcut(code), "zero, invalid key and out-of-range shortcut codes rejected");
    }
    Check(Valid({true, true, Code(65, 15)}), "valid typed preferences");
    Check(!Valid({false, false, Code(9, 1)}), "disabled preferences still validate shortcut");
}

void ShortcutLabels() {
    Check(ShortcutLabel(9) == L"Tab" && ShortcutLabel(32) == L"Space", "default key labels");
    Check(ShortcutLabel(Code(65, 2)) == L"Ctrl+A", "Control label");
    Check(ShortcutLabel(Code(65, 1)) == L"Alt+A", "Alt label");
    Check(ShortcutLabel(Code(65, 4)) == L"Shift+A", "Shift label");
    Check(ShortcutLabel(Code(65, 8)) == L"Win+A", "Windows label");
    Check(ShortcutLabel(Code(65, 15)) == L"Ctrl+Alt+Shift+Win+A", "stable modifier display order");
    Check(ShortcutLabel(48) == L"0" && ShortcutLabel(90) == L"Z", "alphanumeric labels");
    Check(ShortcutLabel(112) == L"F1" && ShortcutLabel(135) == L"F24", "function key labels");
    Check(ShortcutLabel(96) == L"Numpad 0" && ShortcutLabel(111) == L"Numpad /", "numpad labels");
    for (const auto item : {std::pair<unsigned, const wchar_t*>{8, L"Backspace"}, {13, L"Enter"},
                            {19, L"Pause"}, {20, L"Caps Lock"}, {33, L"Page Up"}, {34, L"Page Down"},
                            {35, L"End"}, {36, L"Home"}, {37, L"Left"}, {38, L"Up"}, {39, L"Right"},
                            {40, L"Down"}, {44, L"Print Screen"}, {45, L"Insert"}, {46, L"Delete"},
                            {144, L"Num Lock"}, {145, L"Scroll Lock"}, {186, L";"}, {187, L"="},
                            {188, L","}, {189, L"-"}, {190, L"."}, {191, L"/"}, {192, L"`"},
                            {219, L"["}, {220, L"\\"}, {221, L"]"}, {222, L"'"}, {226, L"OEM 102"}}) {
        Check(ShortcutLabel(item.first) == item.second, "English navigation and punctuation labels");
    }
    for (unsigned code = 0; code < 4096; ++code) {
        Check(ShortcutLabel(code).empty() != ValidShortcut(code), "every valid shortcut has a label, every invalid one is empty");
    }
    Check(ShortcutLabel(std::numeric_limits<unsigned>::max()).empty(), "out-of-range label is empty");
}

void PreferenceEncoding() {
    std::string text = "untouched";
    Check(Encode({}, text) && Json::parse(text) == Document(), "canonical default JSON has exactly four fields");
    const auto first = text;
    Check(Encode({}, text) && text == first, "encoding is deterministic");
    Preferences output{true, true, Code(70, 2)};
    Check(Decode(text, output) && Same(output, {}), "default roundtrip replaces all fields");
    for (const bool enabled : {false, true}) {
        for (const bool frosted : {false, true}) {
            for (unsigned code = 1; code < 4096; ++code) {
                if (!ValidShortcut(code)) continue;
                const Preferences input{enabled, frosted, code};
                Check(Encode(input, text), "valid preferences encode");
                Check(Decode(text, output) && Same(input, output), "all valid shortcuts and boolean combinations roundtrip");
            }
        }
    }
    for (const unsigned code : {0u, Code(9, 1), Code(46, 3), Code(76, 8), 4096u,
                                std::numeric_limits<unsigned>::max()}) {
        text = "untouched";
        Check(!Encode({true, true, code}, text) && text == "untouched", "invalid encode preserves destination");
    }
}

void StrictDecode() {
    Preferences output{true, true, Code(70, 2)};
    const auto saved = output;
    const auto reject = [&](const std::string& text) {
        Check(!Decode(text, output) && Same(output, saved), "invalid decode preserves every destination field: " + text.substr(0, 100));
    };
    for (const auto* field : {"version", "enabled", "frosted", "shortcutCode"}) {
        auto value = Document();
        value.erase(field);
        reject(value.dump());
        value = Document();
        value[field] = nullptr;
        reject(value.dump());
    }
    for (const auto& value : {Json(0), Json(-1), Json(2), Json(1.0), Json(true), Json("1"), Json(18446744073709551615ULL)}) {
        auto document = Document();
        document["version"] = value;
        reject(document.dump());
    }
    for (const auto* field : {"enabled", "frosted"}) {
        for (const auto& value : {Json(0), Json(1), Json("false"), Json::array(), Json::object()}) {
            auto document = Document();
            document[field] = value;
            reject(document.dump());
        }
    }
    for (const auto& value : {Json(0), Json(-1), Json(9.0), Json(9.5), Json(true), Json("9"), Json(4096),
                            Json(4294967305ULL), Json(18446744073709551615ULL), Json(Code(9, 1)),
                            Json(Code(46, 3)), Json(Code(76, 8)), Json(Code(9, 8))}) {
        auto document = Document();
        document["shortcutCode"] = value;
        reject(document.dump());
    }
    auto value = Document();
    value["unknown"] = false;
    reject(value.dump());
    value = Document();
    value["material"] = "liquid-glass";
    reject(value.dump());
    for (const auto* field : {"version", "enabled", "frosted", "shortcutCode"}) {
        auto text = Document().dump();
        text.insert(1, "\"" + std::string(field) + "\":" + Document().at(field).dump() + ",");
        reject(text);
    }
    reject(R"({"version":1,"enabled":false,"frosted":false,"shortcutCode":9,"enabl\u0065d":true})");
    for (const auto* text : {"", "{}", "[]", "null", "true", "9", "{", "{\"version\":1,}",
                             "/* comment */{}", "{\"version\":01}", "{\"version\":+1}"}) reject(text);
    reject(Document().dump() + "x");
    reject(Document().dump() + Document().dump());
    reject(std::string(4097, ' '));
    reject(std::string(4097, ' ') + Document().dump());
    reject(std::string(100, '[') + "0" + std::string(100, ']'));
    reject(std::string("{\"version\":1,\"enabled\":false,\"frosted\":false,\"shortcutCode\":") +
           "999999999999999999999999999999999999999999999999999999}");
    auto embeddedNull = Document().dump();
    embeddedNull.push_back('\0');
    embeddedNull += "garbage";
    reject(embeddedNull);
    std::string padded = Document().dump();
    padded.resize(4096, ' ');
    Check(Decode(padded, output) && Same(output, {}), "bounded whitespace is valid JSON");
    Check(Decode(" \r\n{\"shortcutCode\":577,\"frosted\":true,\"enabled\":true,\"version\":1}\t", output) &&
          Same(output, {true, true, 577}), "key order and standard whitespace do not matter");
}
}  // namespace

int main() {
    unsigned passed = 0;
    unsigned failed = 0;
    const auto run = [&](const char* name, void (*test)()) {
        try {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    run("geometry and defaults", GeometryAndDefaults);
    run("slot centers", SlotCenters);
    run("ring bounds", RingBounds);
    run("hit testing", HitTesting);
    run("shortcut validation", ShortcutValidation);
    run("shortcut labels", ShortcutLabels);
    run("preference encoding", PreferenceEncoding);
    run("strict decode", StrictDecode);
    std::cout << "radial menu: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
