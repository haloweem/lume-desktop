#pragma once
#include "platform/windows/music_provider_http.h"

namespace lume::music::auth {
Json NeteasePlaybackRequest(const Json& data, const Json& credentials, const Cancelled&, const Exchange&);
// Protocol boundaries also exercised by the offline interoperability fixtures.
Json NeteaseXeapiEnvelope(const Json& data, const Json& publicKey);
Json DecodeNeteaseXeapiResponse(std::string_view body);
}
