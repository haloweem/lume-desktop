#pragma once
#include "core/account/ports.h"
#include <windows.h>
namespace lume::platform::windows::account {
bool MayRetryDirect(DWORD error) noexcept;
class WinHttpTransport final : public lume::account::ITransport {
public:
    lume::account::HttpResponse Send(const lume::account::HttpRequest&, const lume::account::Cancellation&) override;
};
}
