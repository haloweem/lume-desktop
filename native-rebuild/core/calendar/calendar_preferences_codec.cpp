#include "core/calendar/calendar_preferences_codec.h"
#include "core/settings/fixed_json_reader.h"
#include <utility>

namespace lume::calendar {
namespace {
using settings::Reader;
bool ReadProfile(Reader& reader, Profile& profile) {
    if (!reader.Take('{')) return false;
    unsigned seen = 0;
    do {
        std::string key;
        if (!reader.String(key) || !reader.Take(':')) return false;
        unsigned flag = 0;
        if (key == "x") { flag = 1; if (!reader.Integer(profile.x)) return false; }
        else if (key == "y") { flag = 2; if (!reader.Integer(profile.y)) return false; }
        else if (key == "width") { flag = 4; if (!reader.Integer(profile.width)) return false; }
        else if (key == "height") { flag = 8; if (!reader.Integer(profile.height)) return false; }
        else if (key == "hasPosition") { flag = 16; if (!reader.Boolean(profile.hasPosition)) return false; }
        else return false;
        if (seen & flag) return false;
        seen |= flag;
    } while (reader.Take(','));
    return reader.Take('}') && seen == 31 && ValidProfile(profile);
}
bool ReadProfiles(Reader& reader, Preferences& value) {
    if (!reader.Take('{')) return false;
    unsigned seen = 0;
    do {
        std::string key;
        if (!reader.String(key) || !reader.Take(':')) return false;
        const auto theme = ParseTheme(key);
        if (!theme) return false;
        const std::size_t index = *theme == Theme::Spacious ? 0 : 1;
        const unsigned flag = 1u << index;
        if ((seen & flag) || !ReadProfile(reader,value.profiles[index])) return false;
        seen |= flag;
    } while (reader.Take(','));
    return reader.Take('}') && seen == 3;
}
bool ReadColor(Reader& reader, Rgba& color) {
    std::string text;
    return reader.String(text) && ParseRgba(text,color);
}
}  // namespace

bool CalendarPreferencesCodec::Decode(const std::string& text, Preferences& out) const {
    if (text.size() > 8192) return false;
    Reader reader(text);
    Preferences parsed;
    unsigned seen = 0;
    if (!reader.Take('{')) return false;
    do {
        std::string key;
        if (!reader.String(key) || !reader.Take(':')) return false;
        unsigned flag = 0;
        if (key == "version") {
            flag = 1;
            int version = 0;
            if (!reader.Integer(version) || version != 1) return false;
        } else if (key == "enabled") {
            flag = 2;
            if (!reader.Boolean(parsed.enabled)) return false;
        } else if (key == "theme") {
            flag = 4;
            std::string name;
            if (!reader.String(name)) return false;
            const auto theme = ParseTheme(name);
            if (!theme) return false;
            parsed.theme = *theme;
        } else if (key == "material") {
            flag = 8;
            std::string name;
            if (!reader.String(name)) return false;
            const auto material = ParseMaterial(name);
            if (!material) return false;
            parsed.material = *material;
        } else if (key == "textColor") {
            flag = 16;
            if (!ReadColor(reader,parsed.textColor)) return false;
        } else if (key == "highlightColor") {
            flag = 32;
            if (!ReadColor(reader,parsed.highlightColor)) return false;
        } else if (key == "locked") {
            flag = 64;
            if (!reader.Boolean(parsed.locked)) return false;
        } else if (key == "profiles") {
            flag = 128;
            if (!ReadProfiles(reader,parsed)) return false;
        } else if (key == "panelColor") {
            flag = 512;  // Optional, like boardView.
            if (!ReadColor(reader,parsed.panelColor)) return false;
        } else if (key == "boardView") {
            // Optional: files written before the todo theme's views lack it.
            flag = 256;
            std::string name;
            if (!reader.String(name)) return false;
            const auto view = ParseBoardView(name);
            if (!view) return false;
            parsed.boardView = *view;
        } else return false;
        if (seen & flag) return false;
        seen |= flag;
    } while (reader.Take(','));
    if (!reader.Take('}') || !reader.End() || (seen & 255) != 255 || !ValidPreferences(parsed)) return false;
    out = std::move(parsed);
    return true;
}
bool CalendarPreferencesCodec::Encode(const Preferences& value, std::string& out) const {
    if (!ValidPreferences(value)) return false;
    std::string text = "{\"version\":1,\"enabled\":";
    text += value.enabled ? "true" : "false";
    text += ",\"theme\":\"" + std::string(ThemeName(value.theme)) + "\",\"material\":\"" +
        std::string(MaterialName(value.material)) + "\",\"textColor\":\"" + FormatRgba(value.textColor) +
        "\",\"highlightColor\":\"" + FormatRgba(value.highlightColor) + "\",\"locked\":";
    text += value.locked ? "true" : "false";
    text += ",\"boardView\":\"" + std::string(BoardViewName(value.boardView)) + "\"";
    text += ",\"panelColor\":\"" + FormatRgba(value.panelColor) + "\"";
    text += ",\"profiles\":{";
    for (std::size_t i = 0; i < value.profiles.size(); ++i) {
        if (i) text += ',';
        const auto& p = value.profiles[i];
        text += "\"" + std::string(ThemeName(i == 0 ? Theme::Spacious : Theme::Split)) + "\":{\"x\":" +
            std::to_string(p.x) + ",\"y\":" + std::to_string(p.y) + ",\"width\":" + std::to_string(p.width) +
            ",\"height\":" + std::to_string(p.height) + ",\"hasPosition\":" + (p.hasPosition ? "true}" : "false}");
    }
    text += "}}";
    out = std::move(text);
    return true;
}
}
