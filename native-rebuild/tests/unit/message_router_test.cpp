#include "core/bridge/message_router.h"

#include <cstdlib>
#include <iostream>

namespace {
class RecordingBridge final : public lume::bridge::IPlatformBridge {
public:
    lume::bridge::BridgeResult Dispatch(const lume::bridge::BridgeMessage& message) override {
        ++calls;
        return {true, message.requestId, "ok"};
    }
    void Subscribe(lume::bridge::BridgeSubscriptionId id) override { subscribed = id; }
    void Unsubscribe(lume::bridge::BridgeSubscriptionId id) override { unsubscribed = id; }
    int calls = 0;
    lume::bridge::BridgeSubscriptionId subscribed = 0;
    lume::bridge::BridgeSubscriptionId unsubscribed = 0;
};
void Expect(bool value, const char* message) { if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }

void TestValidationAndDispatch() {
    RecordingBridge bridge;
    lume::bridge::MessageRouter router(bridge);
    const lume::bridge::BridgeMessage valid{1, "settings", 42, "getSettings", "{}"};
    const auto result = router.Route(valid);
    Expect(result.accepted && result.requestId == 42 && bridge.calls == 1, "valid request dispatched");
    Expect(!router.Route({2, "settings", 43, "getSettings", "{}"}).accepted, "version rejected");
    Expect(!router.Route({1, "unknown", 44, "getSettings", "{}"}).accepted, "channel rejected");
    Expect(!router.Route({1, "settings", 45, "unknown", "{}"}).accepted, "method rejected");
}

void TestSubscriptionCleanup() {
    RecordingBridge bridge; lume::bridge::MessageRouter router(bridge);
    router.Subscribe(7); router.Unsubscribe(7);
    Expect(bridge.subscribed == 7 && bridge.unsubscribed == 7, "subscription lifecycle");
}
}
int main() { TestValidationAndDispatch(); TestSubscriptionCleanup(); return 0; }
