#pragma once
#include "core/account/ports.h"
#include <windows.h>
namespace lume::platform::windows::account {
std::optional<std::wstring> ChooseAvatarFile(HWND);
lume::account::Avatar ReadAvatarFile(std::wstring_view, const lume::account::Cancellation&);
}
