#include "core/ui/page_lifecycle_protocol.h"
#include "vendor/nlohmann/json.hpp"
#include <set>
#include <stdexcept>
#include <string>

namespace lume::ui {
std::optional<LifecyclePageCommand> ParseLifecyclePageJson(const wchar_t* text) noexcept {
    return text ? ParseLifecyclePageJson(std::wstring_view(text)) : std::nullopt;
}

std::optional<LifecyclePageCommand> ParseLifecyclePageJson(std::wstring_view input) noexcept {
    try {
        if (input.empty() || input.size() > 256) return {};
        std::string text;
        text.reserve(input.size());
        for (const auto value : input) {
            if (static_cast<std::uint32_t>(value) > 127) return {};
            text.push_back(static_cast<char>(value));
        }
        std::set<std::string> keys;
        const auto json = nlohmann::json::parse(text, [&](int depth, nlohmann::json::parse_event_t event,
            nlohmann::json& parsed) {
            if (depth > 1) throw std::runtime_error("nested lifecycle command");
            if (event == nlohmann::json::parse_event_t::key &&
                !keys.insert(parsed.get<std::string>()).second)
                throw std::runtime_error("duplicate lifecycle key");
            return true;
        });
        if (!json.is_object() || json.size() != 4 ||
            !json.contains("version") || !json["version"].is_number_integer() || json["version"] != 1 ||
            json.value("scope", std::string{}) != "lifecycle" ||
            json.value("action", std::string{}) != "cleanup" ||
            !json.contains("generation") || !json["generation"].is_number_unsigned()) return {};
        const auto generation = json["generation"].get<std::uint64_t>();
        if (!generation || generation > 0xffffffffULL) return {};
        return LifecyclePageCommand{LifecycleAction::Cleanup, static_cast<std::uint32_t>(generation)};
    } catch (...) {
        return {};
    }
}
}
