#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace lume::platform::windows::account {
class ILoginProtocolRegistry {
public:
    virtual ~ILoginProtocolRegistry() = default;
    virtual bool Write(std::wstring_view key, std::wstring_view name, std::wstring_view value) = 0;
    virtual std::optional<std::wstring> Read(std::wstring_view key, std::wstring_view name) = 0;
    virtual bool RemoveValue(std::wstring_view key, std::wstring_view name) = 0;
};
// Only the parameterless production entry point obtains the actual current exe.
// The explicit port overload exists for isolated tests, not page input.
bool EnsureLoginProtocol();
bool EnsureLoginProtocol(ILoginProtocolRegistry&, std::wstring_view executable);
}
