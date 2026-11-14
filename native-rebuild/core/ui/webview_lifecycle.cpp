#include "core/ui/webview_lifecycle.h"

namespace lume::ui {
namespace {
WebViewLifecycle::Action Visibility(bool visible, std::uint32_t generation) {
    return {WebViewLifecycle::Action::Type::NotifyVisibility, generation, visible};
}
WebViewLifecycle::Action Simple(WebViewLifecycle::Action::Type type, std::uint32_t generation) {
    return {type, generation, false};
}
}

std::vector<WebViewLifecycle::Action> WebViewLifecycle::OnReady() noexcept {
    if (phase_ == Phase::Closed || phase_ != Phase::Cold) return {};
    phase_ = Phase::Active;
    return {};
}

std::vector<WebViewLifecycle::Action> WebViewLifecycle::OnHidden() noexcept {
    if (phase_ == Phase::Closed || phase_ == Phase::Cold ||
        phase_ == Phase::Cleaning || phase_ == Phase::Hidden) return {};
    ++generation_;
    phase_ = Phase::Cleaning;
    suspendRequested_ = false;
    cleanupAcknowledged_ = false;
    suspendAttempts_ = 0;
    return {Visibility(false, generation_)};
}

std::vector<WebViewLifecycle::Action> WebViewLifecycle::OnVisible() noexcept {
    if (phase_ == Phase::Closed || phase_ == Phase::Cold || phase_ == Phase::Active) return {};
    ++generation_;
    phase_ = Phase::Active;
    suspendRequested_ = false;
    cleanupAcknowledged_ = false;
    return {Simple(Action::Type::Resume, generation_),
        Visibility(true, generation_), Simple(Action::Type::RequestStateSync, generation_)};
}

std::vector<WebViewLifecycle::Action> WebViewLifecycle::OnCleanupAcknowledged(
    std::uint32_t generation) noexcept {
    if (phase_ != Phase::Cleaning || generation != generation_ || suspendRequested_) return {};
    cleanupAcknowledged_ = true;
    if(inFlight_)return {};
    suspendRequested_ = true;
    inFlight_=generation_;
    ++suspendAttempts_;
    return {Simple(Action::Type::TrySuspend, generation_)};
}
std::vector<WebViewLifecycle::Action> WebViewLifecycle::OnCleanupTimeout() noexcept {
    return OnCleanupAcknowledged(generation_);
}

std::vector<WebViewLifecycle::Action> WebViewLifecycle::OnSuspendCompleted(
    std::uint32_t generation, bool success) noexcept {
    if(!inFlight_||*inFlight_!=generation)return {};
    inFlight_.reset();
    if(phase_==Phase::Active)
        return success?std::vector<Action>{Simple(Action::Type::Resume,generation_)}:std::vector<Action>{};
    if(phase_!=Phase::Cleaning)return {};
    if(generation!=generation_)
        return cleanupAcknowledged_?OnCleanupAcknowledged(generation_):std::vector<Action>{};
    if(!suspendRequested_)return {};
    if(!success&&suspendAttempts_<2){
        suspendRequested_=false;
        return OnCleanupAcknowledged(generation_);
    }
    phase_ = Phase::Hidden;
    suspendRequested_ = false;
    cleanupAcknowledged_ = false;
    return success ? std::vector<Action>{Simple(Action::Type::TrimWorkingSet, generation_)} : std::vector<Action>{};
}

void WebViewLifecycle::Close() noexcept {
    phase_ = Phase::Closed;
    suspendRequested_ = false;
    cleanupAcknowledged_ = false;
    inFlight_.reset();
    ++generation_;
}
}
