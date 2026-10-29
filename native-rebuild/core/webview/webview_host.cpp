#include "core/webview/webview_host.h"

namespace lume::webview {

bool WebViewHost::Start() {
    if (state_ == State::Running) return true;
    if (state_ == State::Disposed || !runtime_) return false;
    if (!runtime_->Create()) { state_ = State::Unavailable; return false; }
    state_ = State::Running; return true;
}

void WebViewHost::Stop() {
    if (state_ != State::Running) return;
    runtime_->Close(); state_ = State::Stopped;
}

void WebViewHost::Dispose() {
    if (state_ == State::Disposed) return;
    Stop(); runtime_.reset(); state_ = State::Disposed;
}

}  // namespace lume::webview
