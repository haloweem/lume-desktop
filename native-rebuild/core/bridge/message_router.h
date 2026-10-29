#pragma once

#include "core/bridge/platform_bridge.h"

namespace lume::bridge {

class MessageRouter final {
public:
    explicit MessageRouter(IPlatformBridge& bridge) : bridge_(bridge) {}
    BridgeResult Route(const BridgeMessage& message) const;
    void Subscribe(BridgeSubscriptionId id) const;
    void Unsubscribe(BridgeSubscriptionId id) const;

private:
    static bool IsAllowed(const BridgeMessage& message);
    IPlatformBridge& bridge_;
};

}  // namespace lume::bridge
