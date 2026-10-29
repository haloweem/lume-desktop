#pragma once
#include "core/minimize_animation/minimize_animation_store.h"
#include "platform/windows/minimize_animation/minimize_animation_runtime.h"
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace lume::shell {
class MinimizeAnimationSession final {
public:
    using Notify=std::function<void(const std::wstring&,bool)>;
    MinimizeAnimationSession(HINSTANCE,std::string path,Notify,
        settings::IFileStore* files=nullptr,
        platform::windows::MinimizeAnimationScope scope={});
    ~MinimizeAnimationSession();
    bool Restore();
    bool Apply(const minimize_animation::Preferences&);
    std::optional<std::wstring> HandleMessage(std::wstring_view);
    std::wstring State() const;
    bool Running() const;
    bool Shutdown();
    const minimize_animation::Preferences& Preferences() const;
    platform::windows::MinimizeAnimationSnapshot Snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
