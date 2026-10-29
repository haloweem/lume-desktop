#pragma once

#include <cctype>
#include <string_view>

namespace lume::wallpaper {

// Wallpaper IDs are used in JSON, filenames, loopback lease paths and
// playback preferences. Keep one policy at this boundary so UI and storage
// cannot accept different identities.
inline bool IsSafeWallpaperId(std::string_view value) noexcept {
    if (value.empty() || value.size() > 96 || value == "." || value == ".." ||
        value.back() == '.') {
        return false;
    }
    for (const unsigned char ch : value) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) {
            return false;
        }
    }

    // IDs become filenames on Windows. Extensions do not neutralize device
    // names.
    char base[5]{};
    std::size_t count = 0;
    while (count < value.size() && value[count] != '.' && count < 5) {
        if (count < 4) {
            base[count] = static_cast<char>(
                std::tolower(static_cast<unsigned char>(value[count])));
        }
        ++count;
    }
    if (count == 3 &&
        (std::string_view(base, 3) == "con" || std::string_view(base, 3) == "prn" ||
         std::string_view(base, 3) == "aux" || std::string_view(base, 3) == "nul")) {
        return false;
    }
    if (count == 4 &&
        (std::string_view(base, 3) == "com" || std::string_view(base, 3) == "lpt") &&
        base[3] >= '1' && base[3] <= '9') {
        return false;
    }
    return true;
}

}  // namespace lume::wallpaper
