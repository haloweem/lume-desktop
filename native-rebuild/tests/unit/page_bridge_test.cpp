#include "core/ui/page_bridge.h"
#include "core/ui/system_feature_page_commands.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {
using lume::ui::PageCommand;
using lume::ui::ParsePageCommand;

void Expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void TestExactVersionedCommands() {
    for (const auto* message : {L"v1|taskbar|enabled|true",L"v1|taskbar|transparent|false",
        L"v1|taskbar|autoHide|true",L"v1|taskbar|hideBehavior|auto-hide",L"v1|taskbar|hideBehavior|fully-hidden",L"v1|taskbar|reset",L"v1|minimize|enabled|true",
        L"v1|minimize|restore|false",L"v1|minimize|launch|true",L"v1|minimize|multiMonitor|false",
        L"v1|minimize|unhideTaskbar|true",L"v1|minimize|durationMs|50",L"v1|minimize|durationMs|2000",
        L"v1|minimize|unhideDurationMs|0",L"v1|minimize|unhideDurationMs|5000",
        L"v1|minimize|engine|modern",L"v1|minimize|engine|classic",L"v1|minimize|reset"}) {
        Expect(ParsePageCommand(message)!=PageCommand::Invalid,"system feature production protocol");
        Expect(ParsePageCommand(std::wstring(message)+L"|extra")==PageCommand::Invalid,
               "system feature rejects additional payload");
    }
    for (const auto* message : {L"v1|minimize|durationMs|49",L"v1|minimize|durationMs|2001",
        L"v1|minimize|durationMs|050",L"v1|minimize|durationMs|50.0",L"v1|minimize|durationMs|-50",
        L"v1|minimize|unhideDurationMs|00",L"v1|minimize|unhideDurationMs|5001",
        L"v1|taskbar|hideBehavior|hidden",L"v1|taskbar|hideBehavior|true",L"v1|taskbar|hideBehavior|fully-hidden|extra",
        L"v1|taskbar|enabled|True",L"v1|taskbar|transparent|1",L"v1|minimize|engine|Modern"})
        Expect(ParsePageCommand(message)==PageCommand::Invalid,"system feature rejects invalid and noncanonical settings");
    for(const int duration:{50,450,2000}) {
        const auto command=ParsePageCommand(L"v1|minimize|durationMs|"+std::to_wstring(duration));
        const auto next=lume::ui::MinimizePreferencesForPage(command,{});
        Expect(next&&next->durationMs==duration,"bounded duration reaches only its own settings field");
        Expect(!lume::ui::TaskbarPreferencesForPage(command,{}),"animation command cannot mutate taskbar");
    }
    for(const int duration:{0,450,5000}) {
        const auto command=ParsePageCommand(L"v1|minimize|unhideDurationMs|"+std::to_wstring(duration));
        const auto next=lume::ui::MinimizePreferencesForPage(command,{});
        Expect(next&&next->unhideDurationMs==duration&&next->durationMs==450,"unhide and animation durations remain independent");
    }
    const auto taskbar=lume::ui::TaskbarPreferencesForPage(ParsePageCommand(L"v1|taskbar|transparent|false"),{});
    Expect(taskbar&&!taskbar->transparentTaskbar&&!taskbar->autoHideTaskbar&&!taskbar->enabled,
           "taskbar appearance setting does not implicitly enable the module or alter auto-hide");
    const auto hidden=lume::ui::TaskbarPreferencesForPage(ParsePageCommand(L"v1|taskbar|hideBehavior|fully-hidden"),*taskbar);
    Expect(hidden&&hidden->hideBehavior==lume::taskbar::HideBehavior::FullyHidden&&!hidden->enabled&&
           !hidden->transparentTaskbar&&!hidden->autoHideTaskbar,"hide behavior changes only its own preference");
    const auto reset=lume::ui::TaskbarPreferencesForPage(PageCommand::TaskbarReset,*hidden);
    Expect(reset&&reset->hideBehavior==lume::taskbar::HideBehavior::AutoHide&&!reset->enabled&&
           reset->transparentTaskbar&&!reset->autoHideTaskbar,"reset enables transparency and disables auto-hide");
    for (int delay = 3; delay <= 60; ++delay) {
        const auto message = L"v1|icons|delay|" + std::to_wstring(delay);
        Expect(ParsePageCommand(message) ==
            static_cast<PageCommand>(static_cast<int>(PageCommand::IconsDelayFirst) + delay - 3),
            "desktop icon source delay range");
    }
    for (const auto* message : {L"v1|icons|delay|2", L"v1|icons|delay|61", L"v1|icons|delay|03",
        L"v1|icons|delay|3.0", L"v1|icons|delay|+3", L"v1|icons|delay|3 ", L"v1|icons|delay|3|extra"})
        Expect(ParsePageCommand(message) == PageCommand::Invalid, "reject noncanonical desktop icon delay");
    const std::pair<std::wstring_view, PageCommand> icons[] = {
        {L"v1|icons|enabled|true", PageCommand::IconsEnable},
        {L"v1|icons|enabled|false", PageCommand::IconsDisable},
        {L"v1|icons|autoHide|true", PageCommand::IconsAutoHideOn},
        {L"v1|icons|autoHide|false", PageCommand::IconsAutoHideOff},
        {L"v1|icons|restoreOnInput|true", PageCommand::IconsRestoreOn},
        {L"v1|icons|restoreOnInput|false", PageCommand::IconsRestoreOff},
        {L"v1|icons|reset", PageCommand::IconsReset},
    };
    for (const auto& item : icons) {
        Expect(ParsePageCommand(item.first) == item.second, "desktop icon exact settings command");
        Expect(ParsePageCommand(std::wstring(item.first) + L"|extra") == PageCommand::Invalid,
               "desktop icon command has no trailing payload");
    }
    const std::wstring_view calendarCommands[] = {
        L"v1|calendar|enabled|true", L"v1|calendar|enabled|false",
        L"v1|calendar|theme|spacious", L"v1|calendar|theme|split",
        L"v1|calendar|material|liquid-glass", L"v1|calendar|material|frosted-glass",
        L"v1|calendar|textColor", L"v1|calendar|highlightColor", L"v1|calendar|reset",
    };
    for (const auto message : calendarCommands) {
        Expect(ParsePageCommand(message) != PageCommand::Invalid, "calendar exact allowlist accepts command");
        Expect(lume::ui::ParsePageJson(L"\"" + std::wstring(message) + L"\"") == ParsePageCommand(message), "calendar canonical JSON accepted");
        Expect(ParsePageCommand(std::wstring(message) + L"|extra") == PageCommand::Invalid, "calendar extra field rejected");
        Expect(ParsePageCommand(std::wstring(message) + std::wstring(1,L'\0')) == PageCommand::Invalid, "calendar NUL rejected");
        Expect(lume::ui::ParsePageJson(L" \"" + std::wstring(message) + L"\"") == PageCommand::Invalid, "calendar noncanonical JSON rejected");
    }
    for (auto message : {L"v1|calendar|theme|Split", L"v1|calendar|enabled|TRUE", L"v1|Calendar|reset", L"v1|calendar|textcolor", L"v1|calendar|textColor|#ffffff", L"v1|calendar|material|Frosted-glass", L"v1|calendar|theme|unknown"})
        Expect(ParsePageCommand(message) == PageCommand::Invalid, "calendar unknown spelling rejected");
    struct Case { std::wstring_view message; PageCommand command; };
    const Case cases[] = {
        {L"v1|ready", PageCommand::Ready},
        {L"v1|window|minimize", PageCommand::Minimize},
        {L"v1|window|maximize", PageCommand::ToggleFullscreen},
        {L"v1|window|close", PageCommand::Close},
        {L"v1|window|drag", PageCommand::Drag},
        {L"v1|window|dropzone-local", PageCommand::WallpaperDropZoneLocal},
        {L"v1|window|dropzone-music", PageCommand::MusicDropZone},
        {L"v1|window|dropzone-none", PageCommand::WallpaperDropZoneNone},
        {L"v1|wallpaper|dropFiles", PageCommand::WallpaperDropFiles},
        {L"v1|performance|resource-snapshot", PageCommand::PerformanceResourceSnapshot},
        {L"v1|clock|enabled|true", PageCommand::ClockEnable},
        {L"v1|clock|enabled|false", PageCommand::ClockDisable},
        {L"v1|clock|style|welfare", PageCommand::ClockWelfare},
        {L"v1|clock|style|mond", PageCommand::ClockMond},
        {L"v1|clock|style|insideDream", PageCommand::ClockInsideDream},
        {L"v1|clock|reset", PageCommand::ClockReset},
    };
    for (const auto& item : cases) {
        Expect(ParsePageCommand(item.message) == item.command, "exact v1 command maps to its enum");
        Expect(ParsePageCommand(std::wstring(item.message) + L"|") == PageCommand::Invalid,
               "extra protocol field is rejected");
        Expect(ParsePageCommand(item.message.substr(0, item.message.size() - 1)) == PageCommand::Invalid,
               "truncated command is rejected");
    }
}

void TestMalformedAndUnversionedMessages() {
    const std::wstring_view invalid[] = {
        L"", L"ready", L"close", L"window|maximize", L"v2|ready", L"V1|ready",
        L"v1|Ready", L" v1|ready", L"v1|ready ", L"v1|window|fullscreen",
        L"v1|window|close|now", L"v1|clock|enabled|True", L"v1|clock|enabled|1",
        L"v1|clock|style|insidedream", L"v1|clock|style|unknown", L"v1||ready",
        L"{\"command\":\"close\"}", L"v1|clock|style|insideDream|",
    };
    for (const auto item : invalid) {
        Expect(ParsePageCommand(item) == PageCommand::Invalid, "unknown or non-exact command is rejected");
    }
    Expect(ParsePageCommand(std::wstring(100000, L'x')) == PageCommand::Invalid,
           "oversized command is rejected");
    for (wchar_t control = 0; control < 32; ++control) {
        std::wstring message = L"v1|ready";
        message.push_back(control);
        Expect(ParsePageCommand(message) == PageCommand::Invalid, "trailing control character is rejected");
        message = L"v1|ready";
        message.insert(message.begin() + 3, control);
        Expect(ParsePageCommand(message) == PageCommand::Invalid, "embedded control character is rejected");
    }
    Expect(ParsePageCommand(L"v1|ready\x7f") == PageCommand::Invalid, "DEL is rejected");
    const std::wstring storage = L"v1|ready|unused";
    Expect(ParsePageCommand(std::wstring_view(storage.data(), 8)) == PageCommand::Ready,
           "message length is respected without requiring a null terminator");
}

void TestCanonicalJsonMessages() {
    const std::wstring_view commands[] = {
        L"v1|ready", L"v1|window|minimize", L"v1|window|maximize", L"v1|window|close",
        L"v1|window|drag", L"v1|window|dropzone-local", L"v1|window|dropzone-none",
        L"v1|clock|enabled|true", L"v1|clock|enabled|false",
        L"v1|clock|style|welfare", L"v1|clock|style|mond", L"v1|clock|style|insideDream",
    };
    for (const auto command : commands) {
        const std::wstring json = L"\"" + std::wstring(command) + L"\"";
        Expect(lume::ui::ParsePageJson(json) == ParsePageCommand(command),
               "canonical JSON string maps to the exact protocol enum");
    }
    const std::wstring_view invalid[] = {
        L"", L"\"", L"\"\"", L"v1|ready", L" \"v1|ready\"", L"\"v1|ready\" ",
        L"\"v1|ready\"garbage", L"\"v1|ready\"\"", L"\"v1|ready", L"v1|ready\"",
        L"[\"v1|ready\"]", L"{\"message\":\"v1|ready\"}", L"null", L"true", L"123",
        L"\"v1|window|close\\u0000extra\"", L"\"v1|rea\\u0064y\"",
        L"\"v1|ready\\n\"", L"\"v1|ready\\\"\"", L"\"v2|ready\"",
    };
    for (const auto json : invalid) {
        Expect(lume::ui::ParsePageJson(json) == PageCommand::Invalid,
               "noncanonical JSON, unknown commands, and escapes are rejected");
    }
    std::wstring embeddedNull = L"\"v1|window|close";
    embeddedNull.push_back(L'\0');
    embeddedNull += L"extra\"";
    Expect(lume::ui::ParsePageJson(embeddedNull) == PageCommand::Invalid,
           "JSON with a real embedded NUL is rejected");
    Expect(lume::ui::ParsePageJson(L"\"" + std::wstring(100000, L'x') + L"\"") == PageCommand::Invalid,
           "oversized JSON string is rejected");
}
}  // namespace

int main() {
    TestExactVersionedCommands();
    TestMalformedAndUnversionedMessages();
    TestCanonicalJsonMessages();
    std::cout << "page_bridge_test: passed\n";
    return 0;
}
