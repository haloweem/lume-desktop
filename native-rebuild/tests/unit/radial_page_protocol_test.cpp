#include "core/ui/page_bridge.h"
#include "core/radial/radial_menu.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {
using lume::ui::PageCommand;
using lume::ui::ParsePageCommand;
using lume::ui::ParsePageJson;
void Expect(bool value, const char* label) {
    if (!value) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
}
int main() {
    static_assert(static_cast<int>(PageCommand::RadialShortcutLast)-
        static_cast<int>(PageCommand::RadialShortcutFirst)==4094);
    static_assert(static_cast<int>(PageCommand::RadialCaptureBegin)==static_cast<int>(PageCommand::RadialReset)+1);
    static_assert(static_cast<int>(PageCommand::RadialCaptureEnd)==static_cast<int>(PageCommand::RadialCaptureBegin)+1);
    static_assert(static_cast<int>(PageCommand::RadialShortcutFirst)==static_cast<int>(PageCommand::RadialCaptureEnd)+1);
    const std::pair<std::wstring_view,PageCommand> commands[]={
        {L"v1|radial|enabled|true",PageCommand::RadialEnable},
        {L"v1|radial|enabled|false",PageCommand::RadialDisable},
        {L"v1|radial|material|liquid-glass",PageCommand::RadialLiquidGlass},
        {L"v1|radial|material|frosted-glass",PageCommand::RadialFrostedGlass},
        {L"v1|radial|reset",PageCommand::RadialReset},
        {L"v1|radial|capture|true",PageCommand::RadialCaptureBegin},
        {L"v1|radial|capture|false",PageCommand::RadialCaptureEnd}};
    for (const auto& [message,command] : commands) {
        Expect(ParsePageCommand(message)==command,"exact radial command is mapped");
        Expect(ParsePageJson(L"\""+std::wstring(message)+L"\"")==command,"canonical JSON string");
        for (const auto* suffix : {L"|extra",L" ",L"\n"})
            Expect(ParsePageCommand(std::wstring(message)+suffix)==PageCommand::Invalid,"no trailing payload");
        auto embedded=std::wstring(message);embedded.push_back(L'\0');
        Expect(ParsePageCommand(embedded)==PageCommand::Invalid,"exact commands reject embedded NUL");
    }
    for (const auto* message : {L"v1|radial|capture",L"v1|radial|capture|",L"v1|radial|capture|True",
        L"v1|radial|capture|FALSE",L"v1|radial|capture|1",L"v1|radial|capture|0",
        L"v1|radial|capture|\"true\"",L"v1|radial|capture| true",L"v2|radial|capture|true"})
        Expect(ParsePageCommand(message)==PageCommand::Invalid,"capture requires exact boolean protocol");
    const auto first=static_cast<int>(PageCommand::RadialShortcutFirst);
    for (int code=1;code<=4095;++code) {
        const auto message=L"v1|radial|shortcut|"+std::to_wstring(code);
        const auto expected=lume::radial::ValidShortcut(static_cast<unsigned>(code))?
            static_cast<PageCommand>(first+code-1):PageCommand::Invalid;
        Expect(ParsePageCommand(message)==expected,"all shortcut codes match whitelist and contiguous mapping");
        Expect(ParsePageJson(L"\""+message+L"\"")==expected,"JSON shortcut mapping");
    }
    for (const auto* message : {L"v1|radial|shortcut|",L"v1|radial|shortcut|0",L"v1|radial|shortcut|4096",
        L"v1|radial|shortcut|09",L"v1|radial|shortcut|+9",L"v1|radial|shortcut|-9",
        L"v1|radial|shortcut|9.0",L"v1|radial|shortcut|9e0",L"v1|radial|shortcut| 9",
        L"v1|radial|shortcut|9 ",L"v1|radial|shortcut|9|extra",L"v1|radial|shortcut|999999999999",
        L"v1|radial|shortcut|\uff19",L"v1|radial|enabled|True",L"v1|radial|enabled|1",
        L"v1|radial|material|Liquid-glass",L"v2|radial|reset",L"V1|radial|reset",L"v1|radial|snapshot"})
        Expect(ParsePageCommand(message)==PageCommand::Invalid,"reject malformed radial commands");
    std::wstring embedded=L"v1|radial|shortcut|9";embedded.push_back(L'\0');
    Expect(ParsePageCommand(embedded)==PageCommand::Invalid,"embedded NUL");
    for (const auto* json : {L" \"v1|radial|reset\"",L"\"v1|radial|reset\" ",
        L"\"v1\\u007cradial|reset\"",L"{\"version\":1,\"scope\":\"radial\",\"action\":\"reset\"}",
        L"[\"v1|radial|reset\"]",L"null"})
        Expect(ParsePageJson(json)==PageCommand::Invalid,"noncanonical JSON is rejected");
    Expect(ParsePageCommand(L"v1|clock|enabled|true")==PageCommand::ClockEnable,"existing protocol unchanged");
    Expect(ParsePageCommand(L"v1|minimize|unhideDurationMs|5000")==PageCommand::MinimizeUnhideDurationLast,
        "existing numeric range unchanged");
    std::cout << "Radial protocol: exact commands, 4095 shortcut codes, canonical JSON and legacy isolation passed.\n";
}
