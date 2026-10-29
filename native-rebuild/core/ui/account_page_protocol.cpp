#include "core/ui/account_page_protocol.h"
#include "core/account/security.h"
namespace lume::ui {
std::optional<AccountPageCommand> ParseAccountPageJson(std::wstring_view text) noexcept {
    try {
        if (text.empty() || text.size() > 4096) return {};
        const auto j = account::StrictJson(account::Utf8(text), 8192);
        if (!j.is_object() || !j.contains("version") || !j["version"].is_number_integer() || j["version"] != 1 ||
            j.value("scope", "") != "account" || !j.contains("action") || !j["action"].is_string()) return {};
        const auto action = j["action"].get<std::string>();
        AccountPageCommand command;
        if (action == "snapshot") command.action = AccountAction::Snapshot;
        else if (action == "login") command.action = AccountAction::Login;
        else if (action == "cancelLogin") command.action = AccountAction::CancelLogin;
        else if (action == "logout") command.action = AccountAction::Logout;
        else if (action == "refresh") command.action = AccountAction::Refresh;
        else if (action == "updateName") command.action = AccountAction::UpdateName;
        else if (action == "chooseAvatar") command.action = AccountAction::ChooseAvatar;
        else return {};
        if (command.action == AccountAction::UpdateName) {
            if (j.size() != 4 || !j.contains("value") || !j["value"].is_string()) return {};
            command.value = account::NormalizeName(j["value"].get<std::string>());
        } else if (j.size() != 3) return {};
        return command;
    } catch (...) { return {}; }
}
}
