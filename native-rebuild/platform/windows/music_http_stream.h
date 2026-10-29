#pragma once

#include <windows.h>
#include <string>

struct IMFByteStream;
struct IStream;

namespace lume::music {
// A bounded, seekable HTTP stream for FLAC. Other formats and servers without
// byte ranges return failure so the caller can use Media Foundation's URL path.
HRESULT CreateMusicHttpFlacStream(const std::wstring& url, IMFByteStream** stream);
HRESULT CreateMusicHttpFlacStream(const std::wstring& url, IStream** stream);
}
