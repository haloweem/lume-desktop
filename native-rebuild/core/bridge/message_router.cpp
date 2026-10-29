#include "core/bridge/message_router.h"

#include <array>

namespace lume::bridge {

bool MessageRouter::IsAllowed(const BridgeMessage& message) {
    if (message.version != kBridgeVersion || message.requestId == 0 || message.payload.size() > 1024 * 1024) return false;
    static constexpr std::array<const char*, 4> channels{"settings", "layout", "account", "wallpaper"};
    static constexpr std::array<const char*, 7> methods{"getSettings", "setLayout", "openMusic", "closeMusic", "openLogin", "getAccountState", "getWallpaperState"};
    bool channelOk = false; for (const auto* value : channels) channelOk = channelOk || message.channel == value;
    bool methodOk = false; for (const auto* value : methods) methodOk = methodOk || message.method == value;
    return channelOk && methodOk;
}

BridgeResult MessageRouter::Route(const BridgeMessage& message) const {
    if (!IsAllowed(message)) return {false, message.requestId, "invalid message"};
    return bridge_.Dispatch(message);
}

void MessageRouter::Subscribe(BridgeSubscriptionId id) const { if (id != 0) bridge_.Subscribe(id); }
void MessageRouter::Unsubscribe(BridgeSubscriptionId id) const { if (id != 0) bridge_.Unsubscribe(id); }

}  // namespace lume::bridge
