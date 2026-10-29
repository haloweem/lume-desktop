#pragma once
#include <windows.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lume::platform::windows {
enum class ShellLaunchKind { Interactive, TrayOnly, LoginCallback, Invalid };
struct ShellLaunch {
    ShellLaunchKind kind = ShellLaunchKind::Invalid;
    std::wstring callback;
};
inline constexpr ULONG_PTR LoginActivationTag = 0x4c554d31;
bool IsLoginActivationUri(std::wstring_view value) noexcept;
ShellLaunch ParseShellLaunch(const std::vector<std::wstring>& arguments);
ShellLaunch CurrentShellLaunch();
std::optional<std::wstring> DecodeLoginActivation(const COPYDATASTRUCT& data);
bool ForwardLoginActivation(HWND target, std::wstring_view uri);
}
