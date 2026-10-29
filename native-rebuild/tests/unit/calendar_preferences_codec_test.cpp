#include "core/calendar/calendar_preferences_codec.h"
#include "core/settings/json_settings_codec.h"
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace lume::calendar;
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const auto position = text.find(from);
    Expect(position != std::string::npos, "mutation fixture exists");
    text.replace(position,from.size(),to);
    return text;
}
void RoundTripAndKeyOrder() {
    CalendarPreferencesCodec codec;
    Preferences p; p.enabled = true; p.theme = Theme::Split; p.material = Material::FrostedGlass;
    p.textColor = {0x123abc,16}; p.highlightColor = {0xaabbcc,0}; p.locked = true;
    p.profiles[0] = {-32768,32767,320,320,true}; p.profiles[1] = {400,-200,1440,720,true};
    std::string encoded;
    Expect(codec.Encode(p,encoded), "valid preferences encode");
    Expect(encoded.find("\"version\":1") != std::string::npos && encoded.find("schemaVersion") == std::string::npos,
        "calendar owns independent version 1 format");
    Expect(encoded.find("#123abc10") != std::string::npos && encoded.find("#aabbcc00") != std::string::npos,
        "colors persist in canonical RGBA format");
    Preferences decoded;
    Expect(codec.Decode(encoded,decoded) && decoded.enabled && decoded.theme == p.theme &&
        decoded.material == p.material && decoded.locked && decoded.textColor.alpha == 16 &&
        decoded.highlightColor.alpha == 0 && decoded.profiles[0].x == -32768 &&
        decoded.Active()->width == 1440 && decoded.Active()->height == 720, "round trip preserves independent preferences");
    const std::string ordered = R"({"profiles":{"split":{"hasPosition":true,"height":700,"width":900,"y":-1,"x":3},"spacious":{"width":820,"height":500,"x":1,"y":2,"hasPosition":false}},"locked":true,"highlightColor":"#0A84ffFF","textColor":"#AbCdEf08","material":"frosted-glass","theme":"split","enabled":false,"version":1})";
    Expect(codec.Decode(ordered,decoded) && decoded.Active()->x == 3 && decoded.textColor.rgb == 0xabcdef &&
        decoded.textColor.alpha == 8 && decoded.highlightColor.alpha == 255, "all object keys accept reordered input");
    Expect(codec.Decode(ordered + std::string(8192-ordered.size(),' '),decoded), "exactly 8192 bytes accepted");
}
void StrictRejectionAndPreservedOutput() {
    CalendarPreferencesCodec codec; std::string valid;
    Preferences fixture; fixture.material = Material::FrostedGlass;
    Expect(codec.Encode(fixture,valid), "encode default fixture");
    std::vector<std::string> invalid = {
        valid.substr(0,valid.size()-1), valid + "x", "x" + valid, valid + ",", "{}", "[]",
        valid + std::string(8193-valid.size(),' '),
        Replace(valid,"\"version\":1","\"version\":2"),
        Replace(valid,"\"version\":1","\"schemaVersion\":1"),
        Replace(valid,"\"version\":1","\"version\":1,\"version\":1"),
        Replace(valid,"\"version\":1,",""),
        Replace(valid,"\"enabled\":false","\"enabled\":\"false\""),
        Replace(valid,"\"enabled\":false,",""),
        Replace(valid,"\"theme\":\"spacious\"","\"theme\":\"other\""),
        Replace(valid,"\"theme\":\"spacious\",",""),
        Replace(valid,"\"material\":\"frosted-glass\"","\"material\":\"frostedGlass\""),
        Replace(valid,"\"material\":\"frosted-glass\",",""),
        Replace(valid,"\"textColor\":\"#ffffff\"","\"textColor\":123"),
        Replace(valid,"\"textColor\":\"#ffffff\",",""),
        Replace(valid,"\"highlightColor\":\"#0a84ff\",",""),
        Replace(valid,"\"locked\":false,",""),
        Replace(valid,"\"boardView\":\"all\"","\"boardView\":\"year\""),
        Replace(valid,"\"locked\":false","\"locked\":0"),
        Replace(valid,"\"x\":0","\"x\":2147483648"),
        Replace(valid,"\"x\":0","\"x\":-2147483649"),
        Replace(valid,"\"x\":0","\"x\":32768"),
        Replace(valid,"\"y\":0","\"y\":-32769"),
        Replace(valid,"\"width\":820","\"width\":1441"),
        Replace(valid,"\"height\":500","\"height\":319"),
        Replace(valid,"\"x\":0","\"x\":00"),
        Replace(valid,"\"x\":0","\"x\":1.0"),
        Replace(valid,"\"x\":0","\"x\":1e2"),
        Replace(valid,"\"x\":0","\"x\":true"),
        Replace(valid,"\"x\":0","\"x\":0,\"x\":0"),
        Replace(valid,"\"x\":0,",""),
        Replace(valid,"\"hasPosition\":false","\"hasPosition\":false,\"extra\":0"),
        Replace(valid,",\"hasPosition\":false",""),
        Replace(valid,"\"split\":","\"spacious\":"),
        Replace(valid,"\"split\":","\"other\":"),
        Replace(valid,"\"profiles\":{","\"profiles\":{\"other\":{},"),
        Replace(valid,"\"version\":1","\"version\":1,\"extra\":0"),
        Replace(valid,"#ffffff","#fff"),
        Replace(valid,"#ffffff","#ffffffzz"),
        Replace(valid,"#ffffff","#ffff\\u0066f")
    };
    const auto profileStart = valid.find(",\"split\":");
    auto missingProfile = valid.substr(0,profileStart) + "}}";
    invalid.push_back(missingProfile);
    for (const auto& text : invalid) {
        Preferences out; out.enabled = true; out.profiles[0].x = 123;
        Expect(!codec.Decode(text,out), "malformed, unsupported, incomplete or oversized JSON rejected");
        Expect(out.enabled && out.profiles[0].x == 123, "decode failure leaves output untouched");
    }
    for (int kind = 0; kind < 5; ++kind) {
        Preferences p;
        if (kind == 0) p.theme = static_cast<Theme>(-1);
        if (kind == 1) p.material = static_cast<Material>(9);
        if (kind == 2) p.textColor.alpha = -1;
        if (kind == 3) p.profiles[1].height = 721;
        if (kind == 4) p.highlightColor.rgb = 0x1000000;
        std::string out = "keep";
        Expect(!codec.Encode(p,out) && out == "keep", "encode failure preserves output");
    }
}
void ClockSchemaRegression() {
    lume::settings::JsonSettingsCodec codec;
    lume::settings::SettingsSnapshot p;
    std::string encoded;
    Expect(codec.Encode(p,encoded) && encoded.rfind("{\"schemaVersion\":3,\"clock\":",0) == 0,
        "clock encoder remains schema 3");
    Expect(codec.Decode(encoded,p), "clock schema 3 still decodes");
    Expect(!codec.Decode(Replace(encoded,"\"schemaVersion\":3","\"schemaVersion\":3,\"schemaVersion\":3"),p),
        "clock parser still rejects duplicate keys");
    Expect(codec.Decode(R"({"schemaVersion":2,"clock":{"enabled":true,"x":-3,"y":2,"opacity":50,"style":"mond"},"widgets":[]})",p)
        && p.clock.Active().x == -3 && p.clock.Active().alpha == 128, "clock legacy migration behavior preserved");
}
}
int main() { RoundTripAndKeyOrder(); StrictRejectionAndPreservedOutput(); ClockSchemaRegression(); }
