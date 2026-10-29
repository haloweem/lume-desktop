#include "core/ui/calendar_page_state.h"
#include <cstdlib>
#include <iostream>

namespace {
using namespace lume;
void Expect(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void Contains(const std::wstring& json, const wchar_t* value) {
    Expect(json.find(value) != std::wstring::npos, "calendar state contains expected fixed field");
}
struct Runtime final : calendar::ICalendarRuntime {
    bool apply = true, rollback = true, running = false;
    bool Apply(const calendar::Preferences& p) override { if (apply) running=p.enabled; return apply; }
    bool ApplyRollback(const calendar::Preferences& p) override { if (rollback) running=p.enabled; return rollback; }
    bool IsRunning() const override { return running; }
    void Shutdown() override { running=false; }
};
void Mapping() {
    using P=ui::PageCommand; using A=calendar::ActionType;
    struct Case { const wchar_t* wire; A type; int value; };
    for (const auto& c : {
        Case{L"v1|calendar|enabled|true",A::Enable,1}, {L"v1|calendar|enabled|false",A::Enable,0},
        {L"v1|calendar|theme|spacious",A::SelectTheme,0}, {L"v1|calendar|theme|split",A::SelectTheme,1},
        {L"v1|calendar|material|liquid-glass",A::Material,0}, {L"v1|calendar|material|frosted-glass",A::Material,1},
        {L"v1|calendar|reset",A::Reset,0}}) {
        auto action=ui::CalendarActionForPage(ui::ParsePageCommand(c.wire));
        Expect(action && action->type==c.type && action->value==c.value,"exact calendar domain action");
    }
    for (auto command : {P::Ready,P::ClockReset,P::Invalid,ui::ParsePageCommand(L"v1|calendar|textColor"),ui::ParsePageCommand(L"v1|calendar|highlightColor")})
        Expect(!ui::CalendarActionForPage(command),"window, clock, and native editor commands are not domain actions");
}
void State() {
    Runtime runtime; bool save=true;
    calendar::Preferences p; p.material=calendar::Material::FrostedGlass; p.textColor={0x0102af,128};p.highlightColor={0xaabbcc,0};p.locked=true;
    calendar::CalendarController controller(p,runtime,[&](const auto&){return save;});
    const auto initial=ui::CalendarPageState(controller);
    Expect(initial==L"{\"version\":1,\"type\":\"calendar-settings\",\"calendar\":{\"enabled\":false,\"theme\":\"spacious\",\"material\":\"frosted-glass\",\"textColor\":\"#0102af80\",\"highlightColor\":\"#aabbcc00\",\"locked\":true,\"running\":false},\"saved\":false,\"error\":\"\",\"materialStatus\":\"transparent-fallback\",\"loadStatus\":\"loaded\",\"result\":\"snapshot\"}","canonical calendar snapshot");
    Contains(ui::CalendarPageState(controller,calendar::LoadStatus::Fresh),L"\"loadStatus\":\"fresh\"");
    Contains(ui::CalendarPageState(controller,calendar::LoadStatus::RecoveredBackup),L"\"saved\":false");
    Contains(ui::CalendarPageState(controller,calendar::LoadStatus::RecoveredBackup),L"\"loadStatus\":\"recovered-backup\"");
    Contains(ui::CalendarPageState(controller,calendar::LoadStatus::Failed),L"\"error\":\"load\"");
    Expect(controller.Handle({calendar::ActionType::Enable,1}),"enable succeeds");
    const auto applied=ui::CalendarPageState(controller,calendar::LoadStatus::Loaded,ui::CalendarPageResult::Applied);
    Contains(applied,L"\"running\":true");Contains(applied,L"\"saved\":true");Contains(applied,L"\"result\":\"applied\"");
    const auto cancelled=ui::CalendarPageState(controller,calendar::LoadStatus::Loaded,ui::CalendarPageResult::Cancelled);
    Contains(cancelled,L"\"saved\":false");Contains(cancelled,L"\"result\":\"cancelled\"");
    Expect(!controller.Handle({calendar::ActionType::Enable,2}),"invalid action fails");
    Contains(ui::CalendarPageState(controller),L"\"error\":\"invalid\"");
    runtime.apply=false;
    Expect(!controller.Handle({calendar::ActionType::Enable,0}),"runtime fails");
    Contains(ui::CalendarPageState(controller),L"\"error\":\"runtime\"");
    runtime.apply=true;save=false;
    Expect(!controller.Handle({calendar::ActionType::Enable,0}),"save fails");
    Contains(ui::CalendarPageState(controller,calendar::LoadStatus::Loaded,ui::CalendarPageResult::Cancelled),L"\"error\":\"save\"");
    runtime.rollback=false;
    Expect(!controller.Handle({calendar::ActionType::Enable,0}),"rollback fails");
    Contains(ui::CalendarPageState(controller),L"\"error\":\"rollback\"");
    p.theme=static_cast<calendar::Theme>(99);p.textColor={-1,999};
    calendar::CalendarController invalid(p,runtime,[](const auto&){return true;});
    const auto safe=ui::CalendarPageState(invalid);
    Contains(safe,L"\"theme\":\"spacious\"");Contains(safe,L"\"textColor\":\"#ffffff\"");Contains(safe,L"\"error\":\"invalid\"");
}
}
int main() { Mapping();State();std::cout<<"calendar_page_state_test: passed\n"; }
