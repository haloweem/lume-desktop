#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace lume::ui {
enum class LifecycleAction { Cleanup };
struct LifecyclePageCommand {
    LifecycleAction action = LifecycleAction::Cleanup;
    std::uint32_t generation = 0;
};
std::optional<LifecyclePageCommand> ParseLifecyclePageJson(const wchar_t* text) noexcept;
std::optional<LifecyclePageCommand> ParseLifecyclePageJson(std::wstring_view text) noexcept;
}
