#include "core/lifecycle/service_container.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct RecordingService final : lume::IService {
    RecordingService(std::string serviceName, std::vector<std::string>& eventLog)
        : name(std::move(serviceName)), events(eventLog) {}

    bool Start() override {
        events.push_back("start:" + name);
        if (throwOnStart) {
            throw std::runtime_error("start failure: " + name);
        }
        return startResult;
    }

    void Stop() override { events.push_back("stop:" + name); }
    void Suspend() override { events.push_back("suspend:" + name); }
    void Resume() override { events.push_back("resume:" + name); }
    void Dispose() override { events.push_back("dispose:" + name); }

    std::string name;
    std::vector<std::string>& events;
    bool startResult = true;
    bool throwOnStart = false;
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

void TestNormalLifecycleOrder() {
    std::vector<std::string> events;
    lume::ServiceContainer container;
    Expect(container.Add(std::make_unique<RecordingService>("A", events)), "add A");
    Expect(container.Add(std::make_unique<RecordingService>("B", events)), "add B");
    Expect(container.Add(std::make_unique<RecordingService>("C", events)), "add C");

    Expect(container.Start(), "normal start");
    Expect(container.state() == lume::ServiceContainer::State::Running, "running state");
    ExpectEvents(events, {"start:A", "start:B", "start:C"}, "forward start order");

    container.Suspend();
    Expect(container.state() == lume::ServiceContainer::State::Suspended, "suspended state");
    ExpectEvents(events, {"start:A", "start:B", "start:C", "suspend:C", "suspend:B", "suspend:A"}, "reverse suspend order");

    container.Resume();
    Expect(container.state() == lume::ServiceContainer::State::Running, "resumed state");
    ExpectEvents(events, {"start:A", "start:B", "start:C", "suspend:C", "suspend:B", "suspend:A", "resume:A", "resume:B", "resume:C"}, "forward resume order");

    container.Stop();
    Expect(container.state() == lume::ServiceContainer::State::Stopped, "stopped state");
    ExpectEvents(events, {"start:A", "start:B", "start:C", "suspend:C", "suspend:B", "suspend:A", "resume:A", "resume:B", "resume:C", "stop:C", "stop:B", "stop:A"}, "reverse stop order");

    container.Stop();
    container.Dispose();
    Expect(container.state() == lume::ServiceContainer::State::Disposed, "disposed state");
    ExpectEvents(events, {"start:A", "start:B", "start:C", "suspend:C", "suspend:B", "suspend:A", "resume:A", "resume:B", "resume:C", "stop:C", "stop:B", "stop:A", "dispose:C", "dispose:B", "dispose:A"}, "reverse dispose order");

    container.Dispose();
    ExpectEvents(events, {"start:A", "start:B", "start:C", "suspend:C", "suspend:B", "suspend:A", "resume:A", "resume:B", "resume:C", "stop:C", "stop:B", "stop:A", "dispose:C", "dispose:B", "dispose:A"}, "idempotent dispose");
}

void TestStartFailureRollsBack() {
    std::vector<std::string> events;
    lume::ServiceContainer container;
    auto serviceA = std::make_unique<RecordingService>("A", events);
    auto serviceB = std::make_unique<RecordingService>("B", events);
    auto serviceC = std::make_unique<RecordingService>("C", events);
    serviceB->startResult = false;
    RecordingService* failedService = serviceB.get();
    Expect(container.Add(std::move(serviceA)), "add A for rollback");
    Expect(container.Add(std::move(serviceB)), "add B for rollback");
    Expect(container.Add(std::move(serviceC)), "add C for rollback");

    Expect(!container.Start(), "failed start result");
    Expect(container.state() == lume::ServiceContainer::State::Stopped, "rollback state");
    ExpectEvents(events, {"start:A", "start:B", "stop:B", "stop:A"}, "rollback order");

    failedService->startResult = true;
    events.clear();
    Expect(container.Start(), "retry after rollback");
    ExpectEvents(events, {"start:A", "start:B", "start:C"}, "retry order");
    container.Dispose();
}

void TestThrownStartRollsBack() {
    std::vector<std::string> events;
    lume::ServiceContainer container;
    auto serviceA = std::make_unique<RecordingService>("A", events);
    auto serviceB = std::make_unique<RecordingService>("B", events);
    serviceB->throwOnStart = true;
    Expect(container.Add(std::move(serviceA)), "add A for exception");
    Expect(container.Add(std::move(serviceB)), "add B for exception");

    Expect(!container.Start(), "thrown start result");
    Expect(container.state() == lume::ServiceContainer::State::Stopped, "exception rollback state");
    ExpectEvents(events, {"start:A", "start:B", "stop:B", "stop:A"}, "exception rollback order");
    container.Dispose();
}

}  // namespace

int main() {
    TestNormalLifecycleOrder();
    TestStartFailureRollsBack();
    TestThrownStartRollsBack();
    return 0;
}
