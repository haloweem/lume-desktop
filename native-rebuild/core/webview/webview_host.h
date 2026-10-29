#pragma once

#include "core/lifecycle/service.h"

#include <memory>

namespace lume::webview {

class IWebViewRuntime {
public:
    virtual ~IWebViewRuntime() = default;
    virtual bool Create() = 0;
    virtual void Close() = 0;
};

class WebViewHost final : public IService {
public:
    enum class State { Ready, Running, Stopped, Unavailable, Disposed };
    explicit WebViewHost(std::unique_ptr<IWebViewRuntime> runtime) : runtime_(std::move(runtime)) {}
    bool Start() override;
    void Stop() override;
    void Suspend() override {}
    void Resume() override {}
    void Dispose() override;
    State state() const noexcept { return state_; }

private:
    std::unique_ptr<IWebViewRuntime> runtime_;
    State state_ = State::Ready;
};

}  // namespace lume::webview
