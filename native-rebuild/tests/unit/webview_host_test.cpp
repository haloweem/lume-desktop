#include "core/webview/webview_host.h"
#include "platform/windows/webview2_runtime.h"

#include <cstdlib>
#include <iostream>

namespace {
class FakeRuntime final : public lume::webview::IWebViewRuntime {
public:
    bool Create() override { ++creates; return createResult; }
    void Close() override { ++closes; }
    bool createResult = true; int creates = 0; int closes = 0;
};
void Expect(bool value, const char* message) { if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }

void TestIdempotentLifecycle() {
    auto runtime = std::make_unique<FakeRuntime>(); auto* raw = runtime.get();
    lume::webview::WebViewHost host(std::move(runtime));
    Expect(host.Start(), "host start"); Expect(host.Start(), "host start idempotent");
    Expect(raw->creates == 1 && host.state() == lume::webview::WebViewHost::State::Running, "one environment");
    host.Stop(); host.Stop();
    Expect(raw->closes == 1 && host.state() == lume::webview::WebViewHost::State::Stopped, "one close");
    host.Dispose(); host.Dispose(); Expect(host.state() == lume::webview::WebViewHost::State::Disposed, "dispose idempotent");
}

void TestUnavailableRuntime() {
    auto runtime = std::make_unique<FakeRuntime>(); runtime->createResult = false;
    lume::webview::WebViewHost host(std::move(runtime));
    Expect(!host.Start(), "unavailable runtime result");
    Expect(host.state() == lume::webview::WebViewHost::State::Unavailable, "unavailable state");
}

void TestOptionalWindowsRuntime() {
    auto runtime = std::make_unique<lume::platform::windows::WebView2Runtime>(false);
    lume::webview::WebViewHost host(std::move(runtime));
    Expect(!host.Start() && host.state() == lume::webview::WebViewHost::State::Unavailable, "optional runtime fallback");
}
}
int main() { TestIdempotentLifecycle(); TestUnavailableRuntime(); TestOptionalWindowsRuntime(); return 0; }
