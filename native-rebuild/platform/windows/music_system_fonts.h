#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace lume::music::fonts {

// Returns a public state message for one valid list request, or nullopt for
// malformed/out-of-scope input. Font paths and other system metadata never
// cross this boundary.
std::optional<std::wstring> HandleMusicSystemFonts(std::wstring_view message);

}  // namespace lume::music::fonts
