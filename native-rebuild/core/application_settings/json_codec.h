#pragma once
#include "vendor/nlohmann/json.hpp"
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace lume::application_settings::detail {
inline nlohmann::json ParseObject(std::string_view text) {
    if (text.empty() || text.size() > 65536) throw std::runtime_error("size");
    using Json = nlohmann::json;
    std::vector<std::set<std::string>> objects;
    auto value = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& item) {
        if (depth > 8) throw std::runtime_error("depth");
        if (event == Json::parse_event_t::object_start) objects.emplace_back();
        if (event == Json::parse_event_t::key &&
            (objects.empty() || !objects.back().insert(item.get<std::string>()).second))
            throw std::runtime_error("duplicate");
        if (event == Json::parse_event_t::object_end) objects.pop_back();
        return true;
    });
    if (!value.is_object()) throw std::runtime_error("object");
    return value;
}
}
