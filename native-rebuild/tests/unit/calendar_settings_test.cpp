#include "core/calendar/calendar_settings.h"
#include <cstdlib>
#include <iostream>

namespace {
using namespace lume::calendar;
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void DefaultsAndEnums() {
    Preferences p;
    Expect(ValidPreferences(p), "default preferences are valid");
    Expect(!p.enabled && p.theme == Theme::Spacious && p.material == Material::Solid && p.boardView == BoardView::All && !p.locked,
        "fresh calendar is disabled with the solid material by default");
    Expect(p.textColor.rgb == 0xffffff && p.textColor.alpha == 255 &&
        p.highlightColor.rgb == 0x0a84ff && p.highlightColor.alpha == 255, "default colors");
    for (const auto& profile : p.profiles)
        Expect(profile.x == 0 && profile.y == 0 && !profile.hasPosition && profile.width == 820 &&
            profile.height == 500, "each theme has unpositioned original size");
    p.theme = static_cast<Theme>(-1);
    Expect(!p.Active() && !ValidPreferences(p), "invalid negative theme has no active profile");
    p.theme = static_cast<Theme>(3);
    const Preferences& cp = p;
    Expect(!cp.Active() && !ValidPreferences(cp), "invalid theme cannot index profiles");
    p.theme = Theme::Todo;
    Expect(p.Active() == &p.profiles[1] && ValidPreferences(p), "todo theme reuses split profile geometry");
    p.theme = Theme::Spacious;
    p.material = static_cast<Material>(3);
    Expect(!ValidPreferences(p), "unknown material rejected");
    Expect(ParseMaterial("liquid-glass") == Material::LiquidGlass &&
        ParseMaterial("frosted-glass") == Material::FrostedGlass && !ParseMaterial("glass"), "material parsing strict");
    Expect(MaterialName(static_cast<Material>(-1)).empty(), "invalid material name is empty");
}
void BoundsAndIndependentProfiles() {
    Preferences p;
    p.profiles[0] = {-32768,32767,320,320,true};
    p.profiles[1] = {32767,-32768,1440,720,true};
    Expect(ValidPreferences(p), "inclusive coordinate and size endpoints valid");
    p.theme = Theme::Split;
    Expect(p.Active()->width == 1440, "active selects split profile");
    p.Active()->x = 13;
    Expect(p.profiles[0].x == -32768, "theme profiles independent");
    for (int value : {-32769,32768}) {
        auto q = p; q.profiles[0].x = value;
        Expect(!ValidPreferences(q), "x outside limits rejected");
        q = p; q.profiles[1].y = value;
        Expect(!ValidPreferences(q), "inactive y outside limits rejected");
    }
    for (int value : {319,1441}) { auto q = p; q.profiles[0].width = value; Expect(!ValidPreferences(q), "bad width rejected"); }
    for (int value : {319,721}) { auto q = p; q.profiles[1].height = value; Expect(!ValidPreferences(q), "bad height rejected"); }
}
void Colors() {
    Rgba value;
    Expect(ParseRgba("#Aa00fF", value) && value.rgb == 0xaa00ff && value.alpha == 255, "RGB hex is case insensitive");
    Expect(FormatRgba(value) == "#aa00ff", "RGB output lowercase");
    Expect(ParseRgba("#1234AB00", value) && value.rgb == 0x1234ab && value.alpha == 0, "RGBA accepts zero alpha");
    Expect(FormatRgba(value) == "#1234ab00", "RGBA output includes alpha");
    Expect(ParseRgba("#010203FF", value) && FormatRgba(value) == "#010203", "opaque color normalizes to six digits");
    for (const auto* bad : {"", "123456", "#12345", "#1234567", "#123456789", "#12gg00", " #123456", "#123456 "}) {
        Expect(!ParseRgba(bad, value) && value.rgb == 0x010203 && value.alpha == 255, "bad color preserves output");
    }
    for (const auto bad : {Rgba{-1,255}, Rgba{0x1000000,255}, Rgba{0,-1}, Rgba{0,256}}) {
        auto p = Preferences{}; p.textColor = bad;
        Expect(!ValidRgba(bad) && FormatRgba(bad).empty() && !ValidPreferences(p), "invalid RGBA rejected");
        p = {}; p.highlightColor = bad; Expect(!ValidPreferences(p), "invalid highlight rejected");
    }
}
}
int main() { DefaultsAndEnums(); BoundsAndIndependentProfiles(); Colors(); }
