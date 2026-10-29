#include "platform/windows/calendar/calendar_todo_form_layout.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace lume::platform::windows;
namespace {
void Expect(bool condition, const char *why) { if (!condition) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); } }
bool Eq(float a, float b) { return std::abs(a-b) < .01f; }
}
int main() {
    const auto large = CalculateTodoFormLayout(820, 500);
    Expect(Eq(large.panel.x,190) && Eq(large.panel.y,15) && Eq(large.panel.width,440) && Eq(large.panel.height,470), "reference panel 820x500");
    Expect(Eq(large.heading.x,209) && Eq(large.heading.y,34) && Eq(large.heading.height,44), "reference heading");
    Expect(Eq(large.viewport.x,209) && Eq(large.viewport.y,86) && Eq(large.viewport.width,402) && Eq(large.viewport.height,318), "reference independently scrolling form");
    Expect(Eq(large.title.y,19) && Eq(large.title.height,33) && Eq(large.notes.y,332) && Eq(large.repeat.y,198.5f), "controls do not shrink; repeat sits below reminder");
    Expect(Eq(large.footer.y,412) && Eq(large.feedback.y,452), "reference fixed footer");
    const auto small = CalculateTodoFormLayout(320,320);
    Expect(Eq(small.panel.width,290) && Eq(small.panel.y,11) && Eq(small.panel.height,298), "compact panel");
    Expect(Eq(small.viewport.x,28) && Eq(small.viewport.y,76) && Eq(small.viewport.width,264) && Eq(small.viewport.height,158), "compact viewport");
    Expect(Eq(small.ScrollToReveal(0,small.notes),214), "tab reveals full notes without shrinking");
    Expect(Eq(small.ScrollToReveal(214,small.title),19), "reverse tab returns title to viewport");
    Expect(TodoPickerWeekCount(2026,9)==5 && Eq(TodoDatePickerHeight(2026,9),306), "five week date picker");
    Expect(TodoPickerWeekCount(2026,2)==4 && Eq(TodoDatePickerHeight(2026,2),277), "four week date picker");
    Expect(TodoPickerWeekCount(2026,8)==6 && Eq(TodoDatePickerHeight(2026,8),335), "six week date picker");
    auto popup = PlaceTodoPicker({0,0,320,320},{104,146,188,34},304,335);
    Expect(Eq(popup.x,8) && Eq(popup.y,8) && Eq(popup.width,304) && Eq(popup.height,304), "popup clamps to host and needs scrolling");
    auto baseline = PlaceTodoPicker({0,0,820,500},{285,146.5f,326,34},304,306);
    Expect(Eq(baseline.x,285) && Eq(baseline.y,8), "original picker above anchor when no room below");
    std::cout << "calendar todo form layout tests passed\n";
}
