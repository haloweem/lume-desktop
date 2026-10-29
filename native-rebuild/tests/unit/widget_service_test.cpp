#include "core/widget/widget_registry.h"
#include "core/widget/widget_service.h"

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
class RecordingWidget final : public lume::widget::IWidget {
public:
    bool Start() override { ++starts; return true; }
    void Stop() override { ++stops; }
    void Suspend() override { ++suspends; }
    void Resume() override { ++resumes; }
    void Dispose() override { ++disposes; }
    int starts = 0, stops = 0, suspends = 0, resumes = 0, disposes = 0;
};
void Expect(bool value, const char* message) { if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }

void TestHiddenWidgetDoesNotStart() {
    lume::widget::WidgetRegistry registry;
    RecordingWidget* recording = nullptr;
    Expect(registry.Register("clock", [&recording] { auto item = std::make_unique<RecordingWidget>(); recording = item.get(); return item; }), "register clock");
    lume::widget::WidgetService service(registry);
    Expect(service.Show("clock"), "show clock");
    Expect(recording->starts == 1, "start once");
    Expect(service.Hide("clock"), "hide clock");
    Expect(recording->stops == 1, "hide stops refresh owner");
    service.Hide("clock"); service.Dispose();
    Expect(recording->disposes == 1, "dispose once");
}

void TestUnknownWidgetIsIgnored() {
    lume::widget::WidgetService service;
    Expect(!service.Show("missing"), "unknown widget rejected");
}
}

int main() { TestHiddenWidgetDoesNotStart(); TestUnknownWidgetIsIgnored(); return 0; }
