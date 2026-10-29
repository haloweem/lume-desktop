#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace lume::ui {
enum class AccountAction { Snapshot, Login, CancelLogin, Logout, Refresh, UpdateName, ChooseAvatar };
struct AccountPageCommand {
    AccountAction action = AccountAction::Snapshot;
    std::string value;
};
std::optional<AccountPageCommand> ParseAccountPageJson(std::wstring_view) noexcept;
}
