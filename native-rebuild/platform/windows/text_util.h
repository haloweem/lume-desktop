#pragma once
#include <windows.h>
#include <string>
#include <string_view>

// Strict UTF-8 <-> UTF-16 conversion: invalid input (unpaired surrogates,
// malformed UTF-8) yields an empty string rather than replacement characters.
// Copies that differ on purpose (length limits, throwing on failure, control
// character filtering, lenient decoding) keep their own helpers.
namespace lume::text {

inline std::string Utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), bytes, nullptr, nullptr) != bytes)
        return {};
    return result;
}

inline std::wstring Wide(std::string_view value) {
    if (value.empty()) return {};
    const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (chars <= 0) return {};
    std::wstring result(static_cast<std::size_t>(chars), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), chars) != chars)
        return {};
    return result;
}

}  // namespace lume::text
