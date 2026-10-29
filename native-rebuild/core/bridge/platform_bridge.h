#pragma once

#include "core/bridge/bridge_message.h"

namespace lume::bridge {

class IPlatformBridge {
public:
    virtual ~IPlatformBridge() = default;
    virtual BridgeResult Dispatch(const BridgeMessage& message) = 0;
    virtual void Subscribe(BridgeSubscriptionId id) = 0;
    virtual void Unsubscribe(BridgeSubscriptionId id) = 0;
};

}  // namespace lume::bridge
