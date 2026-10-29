#pragma once
#include "platform/windows/music_provider_http.h"

namespace lume::music::online {
using auth::Json;

// Native-only: credentials and resolve/lyrics results must not cross the page bridge.
// Page size defaults to 50, is capped at 100; offset is bounded to 10000.
// _userId is injected by the native account lease, never accepted from the page.
// Track IDs accept rawId or the matching provider:rawId; collection IDs are raw.
// QQ/KuGou lyrics request public line-timed LRC and pass ParseOnlineLyrics before
// returning. Unexpected encrypted QRC/KRC is unsupported, never empty success.
// Throws runtime_error: service, expired, unsupported, unavailable, invalid-response,
// or cancelled. Credentials are borrowed and never persisted or modified.
Json Execute(auth::Provider provider, std::string_view action, const Json& arguments,
             const Json& credentials, const auth::Cancelled& cancelled,
             const auth::Exchange& exchange = auth::SendHttp);
}  // namespace lume::music::online
