#include "core/settings/json_settings_codec.h"

#include <cstdlib>
#include <iostream>

namespace {
void Expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void TestClockPreferencesRoundTripWithoutLosingValues() {
    lume::settings::JsonSettingsCodec codec;
    lume::settings::SettingsSnapshot before;
    before.clock.enabled = false;
    before.clock.style = "insideDream";
    before.clock.Active() = {420,96,170,0x123456,164,true,true};
    before.clock.profiles[0].x = -340;
    std::string encoded;
    Expect(codec.Encode(before, encoded), "clock preferences encode");

    lume::settings::SettingsSnapshot after;
    Expect(codec.Decode(encoded, after), "clock preferences decode");
    Expect(!after.clock.enabled, "enabled state survives round trip");
    Expect(after.clock.Active().x == 420 && after.clock.Active().y == 96 && after.clock.Active().alpha == 164,
           "clock geometry survives round trip");
    Expect(after.clock.style == "insideDream", "clock style survives round trip");
    Expect(after.clock.Active().scale == 170 && after.clock.Active().color == 0x123456 &&
        after.clock.Active().locked && after.clock.Active().customColor, "appearance survives round trip");
    Expect(after.clock.profiles[0].x == -340 && after.clock.profiles[1].x == 532, "per-style positions stay independent");
}

void TestNativeClockSchemaIsWrittenAndDefaultsDisabled() {
    lume::settings::JsonSettingsCodec codec;
    lume::settings::SettingsSnapshot fresh;
    std::string document;
    Expect(codec.Encode(fresh, document), "encode fresh native clock settings");
    Expect(document.find("\"schemaVersion\":3") != std::string::npos, "native clock writes schema 3");
    Expect(!fresh.clock.enabled, "time widget starts disabled until explicitly enabled");
    Expect(document.find("\"profiles\"") != std::string::npos, "persist independent style profiles");
}

void TestInvalidClockPreferencesAreRejected() {
    lume::settings::JsonSettingsCodec codec;
    lume::settings::SettingsSnapshot decoded;
    Expect(!codec.Decode("{\"schemaVersion\":2,\"clock\":{\"enabled\":true,\"style\":\"unknown\"}}", decoded),
           "unknown clock styles are rejected");
}

void TestLegacyMigrationAndInvalidProfiles() {
    lume::settings::JsonSettingsCodec codec;
    lume::settings::SettingsSnapshot value;
    Expect(codec.Decode(R"({"schemaVersion":2,"clock":{"enabled":true,"x":1500,"y":80,"opacity":85,"style":"mond"},"widgets":[]})", value), "decode previous stage preferences");
    Expect(value.clock.enabled && value.clock.style == "mond" && value.clock.Active().x == 532 && value.clock.Active().alpha == 255,
        "legacy unused default coordinates must not override real skin defaults");
    Expect(codec.Decode(R"({"clock":{"enabled":false,"x":-220,"y":110,"opacity":50,"style":"welfare"},"schemaVersion":1})", value), "legacy key order independent");
    Expect(value.clock.Active().x == -220 && value.clock.Active().alpha == 128, "preserve explicit legacy appearance values");
    value.schemaVersion = lume::settings::kCurrentSchemaVersion;
    value.clock.Active().scale = 55;
    std::string encoded;
    Expect(!codec.Encode(value, encoded), "reject unsupported scale step");
    value.clock.Active().scale = 100;
    value.clock.Active().color = 0x1000000;
    Expect(!codec.Encode(value, encoded), "reject color overflow");
}

void TestCorruptDocumentsCannotBePartiallyAccepted() {
    lume::settings::JsonSettingsCodec codec;
    std::string valid;
    Expect(codec.Encode({}, valid), "encode fixture");
    for (const auto& invalid : {valid.substr(0, valid.size() - 1), valid + " garbage",
            std::string("prefix") + valid,
            std::string(R"({"schemaVersion":2,"clock":{"enabled":"false","x":0,"y":0,"opacity":85,"style":"welfare"},"widgets":[]})")}) {
        lume::settings::SettingsSnapshot result;
        Expect(!codec.Decode(invalid, result), "rejects malformed complete documents and wrong value types");
    }
    lume::settings::SettingsSnapshot input;
    input.widgets.push_back({"weather", 0, 0, 320, 152, true, false});
    Expect(!codec.Encode(input, valid), "clock-only codec refuses to silently discard unrelated widgets");
}
}

int main() {
    TestNativeClockSchemaIsWrittenAndDefaultsDisabled();
    TestClockPreferencesRoundTripWithoutLosingValues();
    TestInvalidClockPreferencesAreRejected();
    TestLegacyMigrationAndInvalidProfiles();
    TestCorruptDocumentsCannotBePartiallyAccepted();
    return 0;
}
