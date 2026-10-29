#include "core/weather/weather_places.h"
#include <algorithm>
#include <cstdint>
namespace lume::weather {
namespace {
struct PlaceEntry {
    std::uint32_t source, destination;
    std::uint16_t sourceSize, destinationSize;
};
#include "core/weather/weather_places_data.inc"
template<std::size_t N>
std::string Convert(std::string_view text, const char* data, const PlaceEntry (&entries)[N], std::size_t max) {
    std::string result;
    result.reserve(text.size());
    while (!text.empty()) {
        const PlaceEntry* match = nullptr;
        for (std::size_t length = std::min(max, text.size()); length; --length) {
            // Only test complete UTF-8 prefixes.
            if (length < text.size() && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80) continue;
            const auto key = text.substr(0, length);
            const auto* entry = std::lower_bound(entries, entries + N, key, [data](const auto& value, auto expected) {
                return std::string_view(data + value.source, value.sourceSize) < expected;
            });
            if (entry != entries + N && std::string_view(data + entry->source, entry->sourceSize) == key) {
                match = entry;
                break;
            }
        }
        if (match) {
            result.append(data + match->destination, match->destinationSize);
            text.remove_prefix(match->sourceSize);
        } else {
            std::size_t length = 1;
            while (length < text.size() && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80) ++length;
            result.append(text.substr(0, length));
            text.remove_prefix(length);
        }
    }
    return result;
}
}
std::string SimplifyWeatherPlaceName(std::string_view name) {
    return Convert(Convert(name, CompatibilityText, CompatibilityEntries, CompatibilityMax),
        SimplifiedText, SimplifiedEntries, SimplifiedMax);
}
}
