#include "core/ui/page_lifecycle_protocol.h"
#include "core/ui/webview_lifecycle.h"
#include <iostream>
#include <string>

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL " << message << '\n';
        std::exit(1);
    }
}

void TestVisibilityLifecycle() {
    using lume::ui::WebViewLifecycle;
    using Action = WebViewLifecycle::Action;
    WebViewLifecycle lifecycle;

    Expect(lifecycle.OnHidden().empty(), "hidden before ready does not suspend");
    Expect(lifecycle.OnReady().empty(), "ready has no side effect");
    const auto hidden = lifecycle.OnHidden();
    Expect(hidden.size() == 1 && hidden.front().type == Action::Type::NotifyVisibility &&
        !hidden.front().visible && hidden.front().generation == 1,
        "hide first notifies the page with generation one");
    Expect(lifecycle.OnCleanupAcknowledged(0).empty(), "stale cleanup is ignored");
    const auto suspend = lifecycle.OnCleanupAcknowledged(1);
    Expect(suspend.size() == 1 && suspend.front().type == Action::Type::TrySuspend &&
        suspend.front().generation == 1, "matching cleanup starts suspension");

    const auto restore = lifecycle.OnVisible();
    Expect(restore.size() == 3 && restore[0].type == Action::Type::Resume &&
        restore[1].type == Action::Type::NotifyVisibility && restore[1].visible &&
        restore[1].generation == 2 && restore[2].type == Action::Type::RequestStateSync,
        "restore wins while suspension is in flight");
    const auto late=lifecycle.OnSuspendCompleted(1,true);
    Expect(late.size()==1&&late[0].type==Action::Type::Resume,"late successful suspension restores the currently visible page");

    const auto hiddenAgain = lifecycle.OnHidden();
    Expect(hiddenAgain.size() == 1 && hiddenAgain.front().generation == 3,
        "second hide receives a fresh generation");
    Expect(lifecycle.OnCleanupAcknowledged(2).empty(), "old cleanup cannot suspend a new hide");
    Expect(lifecycle.OnCleanupAcknowledged(3).size() == 1, "new cleanup starts new suspension");
    Expect(lifecycle.OnSuspendCompleted(3, false).size()==1, "failed suspension gets one bounded retry");
    Expect(lifecycle.OnSuspendCompleted(3, false).empty(), "repeated failure stops retrying");
    Expect(lifecycle.OnVisible().size() == 3, "fallback hidden state still restores normally");

    lifecycle.OnHidden();
    lifecycle.OnCleanupAcknowledged(5);
    const auto trimmed = lifecycle.OnSuspendCompleted(5, true);
    Expect(trimmed.size() == 1 && trimmed.front().type == Action::Type::TrimWorkingSet &&
        trimmed.front().generation == 5, "successful suspension requests one working-set trim");
}
void TestDeferredSuspend() {
    using L=lume::ui::WebViewLifecycle;
    L lifecycle;lifecycle.OnReady();lifecycle.OnHidden();lifecycle.OnCleanupAcknowledged(1);
    lifecycle.OnVisible();lifecycle.OnHidden();
    Expect(lifecycle.OnCleanupAcknowledged(3).empty(),"new hide defers while older suspension is in flight");
    const auto next=lifecycle.OnSuspendCompleted(1,false);
    Expect(next.size()==1&&next[0].type==L::Action::Type::TrySuspend&&next[0].generation==3,
        "old completion starts the latest acknowledged hide instead of losing it");
    Expect(lifecycle.OnSuspendCompleted(3,true).size()==1,"deferred hide eventually suspends");
    lifecycle.OnVisible();lifecycle.OnHidden();
    const auto timeout=lifecycle.OnCleanupTimeout();
    Expect(timeout.size()==1&&timeout[0].type==L::Action::Type::TrySuspend,
        "missing page cleanup ACK has a bounded suspension fallback");
    Expect(lifecycle.OnCleanupTimeout().empty(),"cleanup timeout cannot create parallel suspend attempts");
    lifecycle.Close();
    Expect(lifecycle.OnCleanupTimeout().empty(),"closed window rejects delayed cleanup timeout");
}

void TestCloseInvalidatesCallbacks() {
    lume::ui::WebViewLifecycle lifecycle;
    lifecycle.OnReady();
    lifecycle.OnHidden();
    lifecycle.OnCleanupAcknowledged(1);
    lifecycle.Close();
    Expect(lifecycle.OnVisible().empty(), "close rejects restore");
    Expect(lifecycle.OnCleanupAcknowledged(1).empty(), "close rejects cleanup callback");
    Expect(lifecycle.OnSuspendCompleted(1, true).empty(), "close rejects suspend callback");
}

void TestLifecycleProtocol() {
    using lume::ui::LifecycleAction;
    const auto command = lume::ui::ParseLifecyclePageJson(
        L"{\"version\":1,\"scope\":\"lifecycle\",\"action\":\"cleanup\",\"generation\":42}");
    Expect(command.has_value() && command->action == LifecycleAction::Cleanup && command->generation == 42,
        "cleanup protocol parses");
    for (const auto& invalid : {
        L"{\"version\":1,\"scope\":\"lifecycle\",\"action\":\"cleanup\",\"generation\":0}",
        L"{\"version\":1,\"scope\":\"lifecycle\",\"action\":\"cleanup\",\"generation\":-1}",
        L"{\"version\":1,\"scope\":\"lifecycle\",\"action\":\"cleanup\",\"generation\":42,\"extra\":true}",
        L"{\"version\":1,\"scope\":\"other\",\"action\":\"cleanup\",\"generation\":42}",
    }) Expect(!lume::ui::ParseLifecyclePageJson(invalid).has_value(), "invalid lifecycle protocol rejected");
}
}

int main() {
    TestVisibilityLifecycle();
    TestDeferredSuspend();
    TestCloseInvalidatesCallbacks();
    TestLifecycleProtocol();
    std::cout << "PASS WebView lifecycle state machine and protocol\n";
}
