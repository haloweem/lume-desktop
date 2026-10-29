#include "core/settings/json_settings_codec.h"

#include "core/settings/fixed_json_reader.h"

namespace lume::settings {
namespace {
bool ValidClock(const ClockSettings& clock) {
    if (clock.style != "welfare" && clock.style != "mond" && clock.style != "insideDream") return false;
    for (const auto& profile : clock.profiles) if (!lume::clock::ValidAppearance(profile)) return false;
    return true;
}

bool Appearance(Reader& reader, clock::Appearance& appearance) {
    if (!reader.Take('{')) return false;
    unsigned seen = 0;
    do {
        std::string key;
        if (!reader.String(key) || !reader.Take(':')) return false;
        unsigned flag = 0;
        if (key == "x") { flag = 1; if (!reader.Integer(appearance.x)) return false; }
        else if (key == "y") { flag = 2; if (!reader.Integer(appearance.y)) return false; }
        else if (key == "scale") { flag = 4; if (!reader.Integer(appearance.scale)) return false; }
        else if (key == "color") { flag = 8; if (!reader.Integer(appearance.color)) return false; }
        else if (key == "alpha") { flag = 16; if (!reader.Integer(appearance.alpha)) return false; }
        else if (key == "customColor") { flag = 32; if (!reader.Boolean(appearance.customColor)) return false; }
        else if (key == "locked") { flag = 64; if (!reader.Boolean(appearance.locked)) return false; }
        else return false;
        if (seen & flag) return false;
        seen |= flag;
    } while (reader.Take(','));
    return reader.Take('}') && seen == 127 && clock::ValidAppearance(appearance);
}

bool Profiles(Reader& reader, clock::Profiles& profiles) {
    if (!reader.Take('{')) return false;
    unsigned seen = 0;
    do {
        std::string key;
        if (!reader.String(key) || !reader.Take(':')) return false;
        if (key != "welfare" && key != "mond" && key != "insideDream") return false;
        const auto index = clock::Index(clock::ParseStyle(key));
        const unsigned flag = 1u << index;
        if ((seen & flag) || !Appearance(reader, profiles[index])) return false;
        seen |= flag;
    } while (reader.Take(','));
    return reader.Take('}') && seen == 7;
}

bool Clock(Reader& reader, ClockSettings& clock, bool& legacy) {
    if (!reader.Take('{')) return false;
    unsigned seen = 0;
    int x = 1500, y = 80, opacity = 85;
    do {
        std::string key;
        if (!reader.String(key) || !reader.Take(':')) return false;
        unsigned flag = 0;
        if (key == "enabled") { flag = 1; if (!reader.Boolean(clock.enabled)) return false; }
        else if (key == "x") { flag = 2; if (!reader.Integer(x)) return false; }
        else if (key == "y") { flag = 4; if (!reader.Integer(y)) return false; }
        else if (key == "opacity") { flag = 8; if (!reader.Integer(opacity)) return false; }
        else if (key == "style") { flag = 16; if (!reader.String(clock.style)) return false; }
        else if (key == "profiles") { flag = 32; if (!Profiles(reader, clock.profiles)) return false; }
        else return false;
        if (seen & flag) return false;
        seen |= flag;
    } while (reader.Take(','));
    if (!reader.Take('}') || (seen != 31 && seen != 49) || !ValidClock(clock)) return false;
    legacy = seen == 31;
    if (legacy) {
        if (x < -32768 || x > 32767 || y < -32768 || y > 32767 || opacity < 0 || opacity > 100) return false;
        // Earlier prototypes stored unused compatibility defaults. Preserve only explicit edits.
        if (x != 1500 || y != 80) { clock.Active().x = x; clock.Active().y = y; }
        if (opacity != 85) clock.Active().alpha = (opacity * 255 + 50) / 100;
    }
    return true;
}
}  // namespace

bool JsonSettingsCodec::Decode(const std::string& text, SettingsSnapshot& out) const {
    if (text.size() > 8192) return false;
    Reader reader(text);
    SettingsSnapshot parsed;
    bool legacy = false;
    unsigned seen = 0;
    if (!reader.Take('{')) return false;
    do {
        std::string key;
        if (!reader.String(key) || !reader.Take(':')) return false;
        unsigned flag = 0;
        if (key == "schemaVersion") {
            flag = 1;
            int version = 0;
            if (!reader.Integer(version) || version < 1 || version > static_cast<int>(kCurrentSchemaVersion)) return false;
            parsed.schemaVersion = static_cast<std::uint32_t>(version);
        } else if (key == "clock") {
            flag = 2;
            if (!Clock(reader, parsed.clock, legacy)) return false;
        } else if (key == "widgets") {
            flag = 4;
            if (!reader.Take('[') || !reader.Take(']')) return false;
        } else return false;
        if (seen & flag) return false;
        seen |= flag;
    } while (reader.Take(','));
    if (!reader.Take('}') || !reader.End() || (seen & 3) != 3 ||
        legacy != (parsed.schemaVersion < 3)) return false;
    out = std::move(parsed);
    return true;
}

bool JsonSettingsCodec::Encode(const SettingsSnapshot& value, std::string& out) const {
    if (!value.widgets.empty() || value.schemaVersion != kCurrentSchemaVersion || !ValidClock(value.clock)) return false;
    out = "{\"schemaVersion\":3,\"clock\":{\"enabled\":";
    out += value.clock.enabled ? "true" : "false";
    out += ",\"style\":\"" + value.clock.style + "\",\"profiles\":{";
    for (std::size_t i = 0; i < value.clock.profiles.size(); ++i) {
        const auto& p = value.clock.profiles[i];
        if (i) out += ',';
        out += std::string("\"") + clock::StyleName(static_cast<clock::Style>(i)) + "\":{\"x\":" + std::to_string(p.x);
        out += ",\"y\":" + std::to_string(p.y) + ",\"scale\":" + std::to_string(p.scale);
        out += ",\"color\":" + std::to_string(p.color) + ",\"alpha\":" + std::to_string(p.alpha);
        out += std::string(",\"customColor\":") + (p.customColor ? "true" : "false");
        out += std::string(",\"locked\":") + (p.locked ? "true}" : "false}");
    }
    out += "}},\"widgets\":[]}";
    return true;
}
}  // namespace lume::settings
