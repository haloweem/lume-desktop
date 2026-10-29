#pragma once

#include "core/webview/webview_host.h"

namespace lume::platform::windows {

class WebView2Runtime final : public lume::webview::IWebViewRuntime {
public:
    explicit WebView2Runtime(bool available) : available_(available) {}
    bool Create() override { return available_; }
    void Close() override {}
    bool available() const noexcept { return available_; }

private:
    bool available_ = false;
};

}  // namespace lume::platform::windows
