#include "core/window/window_geometry.h"
#include "core/window/window_service.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using lume::window::LogicalRect;
using lume::window::PhysicalRect;
using lume::window::WindowId;
using lume::window::WindowRole;

class RecordingBackend final : public lume::window::IWindowBackend {
public:
    struct Entry {
        WindowId id;
        PhysicalRect bounds;
        bool clickThrough;
    };

    bool Start() override {
        events.push_back("backend:start");
        return startResult;
    }

    void Stop() override { events.push_back("backend:stop"); }

    bool Create(WindowId id, WindowRole, const PhysicalRect& bounds, bool clickThrough) override {
        events.push_back("create:" + std::to_string(id));
        windows.push_back({id, bounds, clickThrough});
        return createResult;
    }

    void Show(WindowId id) override { events.push_back("show:" + std::to_string(id)); }
    void Hide(WindowId id) override { events.push_back("hide:" + std::to_string(id)); }
    void Move(WindowId id, const PhysicalRect&) override { events.push_back("move:" + std::to_string(id)); }
    void SetClickThrough(WindowId id, bool value) override {
        events.push_back(std::string("click:") + std::to_string(id) + (value ? ":on" : ":off"));
    }
    void Destroy(WindowId id) override { events.push_back("destroy:" + std::to_string(id)); }

    bool startResult = true;
    bool createResult = true;
    std::vector<std::string> events;
    std::vector<Entry> windows;
};

void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void ExpectEvents(const std::vector<std::string>& actual,
                  std::initializer_list<const char*> expected,
                  const char* message) {
    Expect(actual.size() == expected.size(), message);
    auto actualIt = actual.begin();
    auto expectedIt = expected.begin();
    for (; actualIt != actual.end(); ++actualIt, ++expectedIt) {
        Expect(*actualIt == *expectedIt, message);
    }
}

void TestGeometry() {
    Expect(lume::window::ScaleLogicalToPhysical(100, 96) == 100, "96 dpi scale");
    Expect(lume::window::ScaleLogicalToPhysical(100, 120) == 125, "120 dpi scale");
    Expect(lume::window::ScaleLogicalToPhysical(100, 144) == 150, "144 dpi scale");
    Expect(lume::window::ScaleLogicalToPhysical(100, 192) == 200, "192 dpi scale");
    Expect(lume::window::ScalePhysicalToLogical(125, 120) == 100, "physical to logical");

    const LogicalRect logical{-20, 10, 200, 100};
    const PhysicalRect physical = lume::window::ScaleLogicalToPhysical(logical, 144);
    Expect(physical.x == -30 && physical.y == 15 && physical.width == 300 && physical.height == 150, "rect scale");

    const LogicalRect clamped = lume::window::ClampToWorkArea({-30, -10, 500, 400}, {0, 0, 800, 600});
    Expect(clamped.x == 0 && clamped.y == 0 && clamped.width == 500 && clamped.height == 400, "negative clamp");
    Expect(lume::window::Contains({10, 20, 100, 50}, 10, 20), "hit top left");
    Expect(!lume::window::Contains({10, 20, 100, 50}, 110, 20), "miss right edge");
}

void TestWindowLifecycle() {
    auto backend = std::make_unique<RecordingBackend>();
    auto* recording = backend.get();
    lume::window::WindowService service(std::move(backend));
    Expect(service.Start(), "window service start");

    const auto first = service.Create({WindowRole::Widget, {10, 20, 200, 100}, 120, true, true});
    const auto second = service.Create({WindowRole::Main, {30, 40, 600, 400}, 144, true, true});
    Expect(first.has_value() && second.has_value() && *first == 1 && *second == 2, "monotonic window ids");
    Expect(service.SetClickThrough(*first, true), "set click through");
    Expect(service.SetVisible(*first, false), "hide first");
    Expect(service.UpdateDpi(*second, 192), "update dpi");
    const auto snapshot = service.Get(*second);
    Expect(snapshot.has_value() && snapshot->logicalBounds.x == 30 && snapshot->physicalBounds.width == 1200, "preserve logical bounds on dpi");

    service.Suspend();
    Expect(service.state() == lume::window::WindowService::State::Suspended, "suspended window service");
    service.Resume();
    Expect(service.state() == lume::window::WindowService::State::Running, "resumed window service");
    service.Stop();
    ExpectEvents(recording->events, {"backend:start", "create:1", "show:1", "create:2", "show:2", "click:1:on", "hide:1", "move:2", "hide:2", "show:2", "hide:2", "destroy:2", "destroy:1", "backend:stop"}, "window lifecycle order");
    service.Dispose();
    Expect(!service.Create({WindowRole::Widget, {0, 0, 10, 10}, 96, true, false}).has_value(), "reject create after dispose");
}

void TestBackendFailure() {
    auto backend = std::make_unique<RecordingBackend>();
    backend->createResult = false;
    auto* recording = backend.get();
    lume::window::WindowService service(std::move(backend));
    Expect(service.Start(), "service starts before create failure");
    Expect(!service.Create({WindowRole::Widget, {0, 0, 10, 10}, 96, true, false}).has_value(), "create failure result");
    ExpectEvents(recording->events, {"backend:start", "create:1"}, "create failure cleanup");
    service.Dispose();
}

}  // namespace

int main() {
    TestGeometry();
    TestWindowLifecycle();
    TestBackendFailure();
    return 0;
}
