#pragma once
#include "platform/windows/music_provider_http.h"

namespace lume::music::auth {
// Internal protocol helpers. Callers supply only compile-time provider routes.
// Catalog listings may exceed the 256 KiB login default; the transport caps at 4 MiB.
inline constexpr std::size_t kCatalogMaxResponse = 4 * 1024 * 1024;
Json NeteaseCatalogRequest(const char* path, const Json& data, bool weapi,
                          Json& secrets, const Cancelled&, const Exchange&);
Json KugouCatalogRequest(const char* path, std::map<std::string, std::string> params,
                        std::string body, const char* router, Json& secrets,
                        const Cancelled&, const Exchange&);
Json QQCatalogComm(Json& secrets, bool playback, const Cancelled&, const Exchange&);
}  // namespace lume::music::auth
