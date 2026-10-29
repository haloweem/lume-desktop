#include "core/glass/glass_geometry.h"
#include "core/glass/glass_service.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class RecordingSource final : public lume::glass::IBackgroundSource {
public:
    explicit RecordingSource(std::vector<std::string>& eventLog) : events(eventLog) {}

    bool Start() override {
        events.push_back("source:start");
        return startResult;
    }
    void Stop() override { events.push_back("source:stop"); }
    bool IsReady() const noexcept override { return ready; }
    lume::glass::BackgroundToken Token() const noexcept override { return 7; }

    std::vector<std::string>& events;
    bool startResult = true;
    bool ready = true;
};

class RecordingBackend final : public lume::glass::IGlassBackend {
public:
    explicit RecordingBackend(std::vector<std::string>& eventLog) : events(eventLog) {}

    bool Start() override {
        events.push_back("backend:start");
        return startResult;
    }
    void Stop() override { events.push_back("backend:stop"); }
    lume::glass::BackendHandle Create(const lume::glass::GlassSnapshot&) override {
        events.push_back("backend:create");
        return createResult ? 9 : 0;
    }
    bool Update(lume::glass::BackendHandle, const lume::glass::GlassSnapshot&, lume::glass::GlassMode mode) override {
        events.push_back(std::string("backend:update:") + std::to_string(static_cast<int>(mode)));
        return updateResult;
    }
    void Destroy(lume::glass::BackendHandle) override { events.push_back("backend:destroy"); }

    std::vector<std::string>& events;
    bool startResult = true;
    bool createResult = true;
    bool updateResult = true;
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
    const lume::glass::PhysicalRect owner{100, 50, 400, 200};
    const lume::glass::PhysicalRect background{0, 0, 800, 600};
    const auto coordinates = lume::glass::ComputeMaterialCoordinates(owner, background, 34);
    Expect(coordinates.lens.x == 100 && coordinates.lens.y == 50, "lens origin");
    Expect(coordinates.lens.width == 400 && coordinates.lens.height == 200, "lens size");
    Expect(coordinates.sample.x == 66 && coordinates.sample.y == 16, "padded sample origin");
    Expect(coordinates.sample.width == 468 && coordinates.sample.height == 268, "padded sample size");

    const auto clipped = lume::glass::ComputeMaterialCoordinates({-20, -10, 100, 80}, background, 34);
    Expect(clipped.sample.x == 0 && clipped.sample.y == 0, "clamp negative sample");
    Expect(clipped.sample.width == 114 && clipped.sample.height == 104, "clamp sample size");
}

void TestFullAndFallbackLifecycle() {
    std::vector<std::string> events;
    auto source = std::make_unique<RecordingSource>(events);
    auto backend = std::make_unique<RecordingBackend>(events);
    lume::glass::GlassService service(std::move(source), std::move(backend));
    Expect(service.Start(), "glass service start");
    Expect(service.Attach({42, {100, 50, 400, 200}, {0, 0, 800, 600}, 34}), "attach glass");
    Expect(service.mode() == lume::glass::GlassMode::Full, "full mode");
    Expect(service.UpdateBounds({120, 60, 420, 220}, {0, 0, 800, 600}), "update glass bounds");
    service.Suspend();
    Expect(service.mode() == lume::glass::GlassMode::Reduced, "suspend reduced mode");
    service.Resume();
    Expect(service.mode() == lume::glass::GlassMode::Full, "resume full mode");
    service.Stop();
    service.Dispose();
    ExpectEvents(events, {"source:start", "backend:start", "backend:create", "backend:update:0", "backend:update:0", "backend:update:1", "backend:update:0", "backend:destroy", "backend:stop", "source:stop"}, "glass release order");
}

void TestSourceFailureFallsBack() {
    std::vector<std::string> events;
    auto source = std::make_unique<RecordingSource>(events);
    source->ready = false;
    auto backend = std::make_unique<RecordingBackend>(events);
    lume::glass::GlassService service(std::move(source), std::move(backend));
    Expect(service.Start(), "fallback service start");
    Expect(service.Attach({7, {0, 0, 100, 100}, {0, 0, 200, 200}, 16}), "fallback attach");
    Expect(service.mode() == lume::glass::GlassMode::Reduced, "source unavailable reduced");
    service.Dispose();
}

void TestBackendFailureFallsBackToTransparent() {
    std::vector<std::string> events;
    auto source = std::make_unique<RecordingSource>(events);
    auto backend = std::make_unique<RecordingBackend>(events);
    backend->startResult = false;
    lume::glass::GlassService service(std::move(source), std::move(backend));
    Expect(service.Start(), "transparent fallback service start");
    Expect(service.Attach({8, {0, 0, 100, 100}, {0, 0, 200, 200}, 16}), "transparent fallback attach");
    Expect(service.mode() == lume::glass::GlassMode::TransparentFallback, "transparent fallback mode");
}

}  // namespace

int main() {
    TestGeometry();
    TestFullAndFallbackLifecycle();
    TestSourceFailureFallsBack();
    TestBackendFailureFallsBackToTransparent();
    return 0;
}
