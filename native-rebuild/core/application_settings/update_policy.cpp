#include "core/application_settings/application_settings.h"
#include "core/application_settings/json_codec.h"
#include <algorithm>
#include <array>
#include <charconv>
namespace lume::application_settings {
namespace {
std::optional<std::array<std::uint32_t, 4>> Version(std::string_view text) {
    if (text.empty() || text.size() > 43) return {};
    std::array<std::uint32_t, 4> parts{};
    unsigned count = 0;
    std::size_t start = 0;
    while (start < text.size()) {
        if (count == 4) return {};
        const auto end = text.find('.', start);
        const auto piece = text.substr(start, end == text.npos ? text.size() - start : end - start);
        if (piece.empty() || !std::all_of(piece.begin(), piece.end(), [](char c) { return c >= '0' && c <= '9'; })) return {};
        const auto result = std::from_chars(piece.data(), piece.data() + piece.size(), parts[count++]);
        if (result.ec != std::errc{} || result.ptr != piece.data() + piece.size()) return {};
        if (end == text.npos) break;
        start = end + 1;
        if (start == text.size()) return {};
    }
    if (count < 2) return {};
    return parts;
}
}
std::optional<int> CompareVersions(std::string_view left, std::string_view right) noexcept {
    const auto a = Version(left), b = Version(right);
    if (!a || !b) return {};
    return *a == *b ? 0 : *a < *b ? -1 : 1;
}
bool IsOfficialUpdateUrl(std::string_view url) noexcept {
    constexpr std::string_view prefix = "https://";
    if (url.size() > 4096 || url.substr(0, prefix.size()) != prefix) return false;
    for (const unsigned char c : url) if (c <= 32 || c >= 127 || c == '\\' || c == '#') return false;
    const auto end = url.find('/', prefix.size());
    if (end == url.npos) return false;
    const auto host = url.substr(prefix.size(), end - prefix.size());
    return host == "lumedesktop.com" || host == "www.lumedesktop.com" || host == "static.lumedesktop.com";
}
ManifestResult EvaluateManifest(std::string_view body, std::string_view current) noexcept {
    try {
        const auto json = detail::ParseObject(body);
        const auto version = json.at("version").get<std::string>();
        const auto comparison = CompareVersions(version, current);
        if (!comparison) return {{"error", {}, 0}, "update-version"};
        if (*comparison <= 0) return {{"current", version, 0}, {}};
        if (!json.contains("runtime") || !json["runtime"].is_string() || json["runtime"] != NativeRuntime ||
            !json.contains("channel") || !json["channel"].is_string() || json["channel"] != NativeChannel)
            return {{"incompatible", version, 0}, "update-incompatible"};
        const auto url = json.at("downloadUrl").get<std::string>();
        const auto checksum = json.at("sha256").get<std::string>();
        const auto pathname = url.substr(0, url.find('?'));
        if (!IsOfficialUpdateUrl(url) || pathname.size() < 4 || pathname.substr(pathname.size() - 4) != ".exe" ||
            checksum.size() != 64 || !std::all_of(checksum.begin(), checksum.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            })) return {{"error", version, 0}, "update-manifest"};
        if (!json.contains("size") || !json["size"].is_number_integer() || json["size"] < 2 ||
            json["size"] > 1073741824) return {{"error", version, 0}, "update-manifest"};
        return {{"available", version, 0}, {}, {url, checksum, json["size"].get<std::uint64_t>()}};
    } catch (...) { return {{"error", {}, 0}, "update-manifest"}; }
}
}
