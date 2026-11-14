#pragma once
#include <windows.h>
#include <memory>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <unordered_set>
#include <utility>

namespace lume::platform::windows {
struct NotificationReceiverOptions {
    bool enabled = false, qq = false, wechat = false, preview = false;
};
struct NotificationReceiverState {
    // QQ: disabled, package-required, access-required, requesting, denied,
    // connected, attention or error. attention is a content-free Shell fallback
    // when notification access or source metadata is unavailable. WeChat:
    // disabled, starting, listening or error.
    std::string qq = "disabled", wechat = "disabled";
    bool qqRealtime = false;
};
struct ReceivedIslandNotification {
    std::string source, id;
    std::wstring title, body;
};
// Exact identities only. Matching a display name, a substring, or a window
// title could mistake another program for a messaging application.
bool IsQQNotificationSource(std::wstring_view appUserModelId);
bool IsQQExecutablePath(std::wstring_view path);
bool IsWeChatExecutablePath(std::wstring_view path);
enum class QQNotificationAccess { Unspecified, Allowed, Denied, Unavailable };
enum class QQNotificationChannel { Disabled, System, Attention, AccessRequired, Denied, Unavailable };
// Only existing Windows access can expose notification content. Every other
// state uses content-free Shell attention without requesting permission.
QQNotificationChannel SelectQQNotificationChannel(bool enabled, bool packaged,
                                                   QQNotificationAccess access, bool systemUsable);
// Shared event/snapshot deduplication. Only stable numeric identity metadata is
// retained. At capacity, older creations are dropped rather than replayed.
class QQNotificationDeduplicator final {
public:
    static constexpr std::size_t Capacity = 2048;
    void Reset(std::int64_t listeningSince);
    bool CanAccept(std::uint32_t id, std::int64_t creationTime) const;
    bool Accept(std::uint32_t id, std::int64_t creationTime);
private:
    std::int64_t since_ = 0, discardedThrough_ = 0;
    std::deque<std::pair<std::string, std::int64_t>> order_;
    std::unordered_set<std::string> seen_;
};

class IslandNotificationReceiver final {
public:
    IslandNotificationReceiver();
    ~IslandNotificationReceiver();
    IslandNotificationReceiver(const IslandNotificationReceiver&) = delete;
    IslandNotificationReceiver& operator=(const IslandNotificationReceiver&) = delete;
    void Configure(NotificationReceiverOptions);
    // Optional UI-thread dispatcher. Posts only the cookie (wParam) and zero
    // lParam when notices become pending; never passes notice text or a pointer.
    // Clear the target before destroying its HWND. Drain acknowledges the wake.
    void SetWakeTarget(HWND, UINT message, WPARAM cookie);
    // Compatibility no-op for older callers. No permission UI is opened.
    bool RequestAccess();
    NotificationReceiverState Snapshot() const;
    std::vector<ReceivedIslandNotification> Drain();
    // Restores an existing, verified application window. Does not execute
    // notification-provided commands or attempt to select a conversation.
    bool OpenApp(std::string_view source);
    void Shutdown();
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    std::thread worker_;
};
}
