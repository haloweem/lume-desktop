#pragma once

#include <cstdint>
#include <string>

namespace lume::bridge {

constexpr std::uint32_t kBridgeVersion = 1;
using BridgeSubscriptionId = std::uint64_t;

struct BridgeMessage {
    std::uint32_t version = kBridgeVersion;
    std::string channel;
    std::uint64_t requestId = 0;
    std::string method;
    std::string payload;
};

struct BridgeResult {
    bool accepted = false;
    std::uint64_t requestId = 0;
    std::string payload;
};

}  // namespace lume::bridge
