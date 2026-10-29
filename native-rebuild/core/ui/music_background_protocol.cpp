#include "core/ui/music_background_protocol.h"
#include "core/wallpaper/catalog.h"

#include <stdexcept>

namespace lume::ui {
using Json = nlohmann::json;

std::optional<MusicBackgroundCommand> DecodeMusicBackgroundCommand(const Json& json) {
    using namespace music::background_detail;
    try {
        if (!json.is_object() || !json.at("version").is_number_integer() || json.at("version") != 1 ||
            json.at("scope") != "music-background" || !json.at("action").is_string()) return {};
        MusicBackgroundCommand command;
        const auto& action = json.at("action").get_ref<const std::string&>();
        if (action == "snapshot") {
            if (!Keys(json, {"version", "scope", "action"})) return {};
        } else if (action == "catalog") {
            if (!Keys(json, {"version", "scope", "action", "request", "offset", "limit"}) ||
                !Integer(json.at("request"), command.request, true) ||
                !Integer(json.at("offset"), command.offset) || command.offset > wallpaper::kMaxLibraryItems ||
                !Integer(json.at("limit"), command.limit, true) ||
                command.limit > kMusicBackgroundMaxCatalogItems) return {};
            command.action = MusicBackgroundAction::Catalog;
        } else {
            if (action == "select") {
                if (!Keys(json, {"version", "scope", "action", "request", "revision", "id"})) return {};
                command.id = json.at("id").get<std::string>();
                if (!music::ValidMusicBackgroundSourceId(command.id)) return {};
                command.action = MusicBackgroundAction::Select;
            } else if (!Keys(json, {"version", "scope", "action", "request", "revision"})) return {};
            if (action == "choose") command.action = MusicBackgroundAction::Choose;
            else if (action == "clear") command.action = MusicBackgroundAction::Clear;
            else if (action != "select") return {};
            if (!Integer(json.at("request"), command.request, true) ||
                !Integer(json.at("revision"), command.revision)) return {};
        }
        return command;
    } catch (...) { return {}; }
}

std::optional<MusicBackgroundCommand> ParseMusicBackgroundPageJson(std::wstring_view input) noexcept {
    if (input.size() > kMusicBackgroundMaxCommandCharacters) return {};
    try { return DecodeMusicBackgroundCommand(music::background_detail::Parse(input)); }
    catch (...) { return {}; }
}

Json EncodeMusicBackgroundMessage(const MusicBackgroundMessage& message) {
    using Result = music::MusicBackgroundResult;
    if (message.revision > music::kMusicBackgroundMaxSafeInteger || message.request > music::kMusicBackgroundMaxSafeInteger)
        throw std::invalid_argument("music background counter");
    const char* error = "";
    switch (message.result) {
    case Result::Ready: case Result::Busy: break;
    case Result::Invalid: error = "invalid"; break;
    case Result::Load: error = "load"; break;
    case Result::Save: error = "save"; break;
    case Result::Conflict: error = "conflict"; break;
    case Result::Unsupported: error = "unsupported"; break;
    case Result::TooLarge: error = "too-large"; break;
    default: throw std::invalid_argument("music background result");
    }
    Json media = nullptr;
    if (message.media) {
        media = music::EncodeMusicBackgroundMedia(*message.media);
        media.erase("extension");
        const auto plain = music::MusicBackgroundUrl(*message.media);
        if (!message.mediaUrl.empty() && message.mediaUrl != plain) {
            // Only server-minted loopback capabilities, never arbitrary URLs.
            const std::string_view url = message.mediaUrl, prefix = "http://127.0.0.1:";
            const auto slash = url.find('/', prefix.size());
            const auto port = slash == url.npos ? std::string_view{} : url.substr(prefix.size(), slash - prefix.size());
            const auto token = slash == url.npos ? std::string_view{} : url.substr(slash + 1, 64);
            const auto suffix = "/media." + message.media->extension;
            if (url.substr(0, prefix.size()) != prefix || port.empty() || port.size() > 5 ||
                port.find_first_not_of("0123456789") != port.npos ||
                std::stoul(std::string(port)) == 0 || std::stoul(std::string(port)) > 65535 ||
                token.size() != 64 || token.find_first_not_of("0123456789abcdef") != token.npos ||
                url.substr(slash + 65) != suffix) throw std::invalid_argument("music background URL");
        }
        media["url"] = message.mediaUrl.empty() ? plain : message.mediaUrl;
    }
    return Json{{"version", 1}, {"type", "music-background-state"}, {"revision", message.revision},
        {"request", message.request}, {"status", message.result == Result::Busy ? "busy" : *error ? "error" : "ready"},
        {"error", error}, {"media", std::move(media)}};
}

std::wstring EncodeMusicBackgroundPageState(const MusicBackgroundMessage& message) {
    const auto text = EncodeMusicBackgroundMessage(message).dump(-1, ' ', true);
    return std::wstring(text.begin(), text.end());
}

std::wstring EncodeMusicBackgroundCatalog(const MusicBackgroundCatalogMessage& message) {
    if (message.items.size() > kMusicBackgroundMaxCatalogItems || message.total > wallpaper::kMaxLibraryItems ||
        message.items.size() > message.total ||
        (!message.selectedId.empty() && !music::ValidMusicBackgroundSourceId(message.selectedId)))
        throw std::invalid_argument("music catalog");
    MusicBackgroundMessage envelope;
    envelope.request = message.request;
    envelope.result = message.result;
    const auto state = EncodeMusicBackgroundMessage(envelope);
    Json items = Json::array();
    for (const auto& item : message.items) {
        music::MusicBackgroundMedia nameProbe{"catalog", item.name, "png", music::MusicBackgroundKind::Image, "image/png"};
        music::MusicBackgroundMedia normalized;
        if (!music::ValidMusicBackgroundSourceId(item.id) ||
            !music::NormalizeMusicBackgroundMedia(nameProbe, normalized) ||
            (item.type != "video" && item.type != "image") ||
            (!item.thumbnailUrl.empty() && wallpaper::NormalizeOfficialUrl(item.thumbnailUrl) != item.thumbnailUrl))
            throw std::invalid_argument("music catalog item");
        items.push_back({{"id", item.id}, {"name", item.name}, {"type", item.type}, {"thumbnailUrl", item.thumbnailUrl}});
    }
    const auto text = Json{{"version", 1}, {"type", "music-background-catalog"},
        {"request", message.request}, {"status", state.at("status")}, {"error", state.at("error")},
        {"total", message.total}, {"items", std::move(items)}, {"selectedId", message.selectedId}}.dump(-1, ' ', true);
    return {text.begin(), text.end()};
}
}  // namespace lume::ui
