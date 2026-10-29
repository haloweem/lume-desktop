#pragma once

#include "core/lifecycle/service.h"
#include "core/window/window_backend.h"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>

namespace lume::window {

class WindowService final : public IService {
public:
    enum class State {
        Ready,
        Running,
        Suspended,
        Stopped,
        Disposed,
    };

    explicit WindowService(std::unique_ptr<IWindowBackend> backend);
    ~WindowService();

    WindowService(const WindowService&) = delete;
    WindowService& operator=(const WindowService&) = delete;

    bool Start() override;
    void Suspend() override;
    void Resume() override;
    void Stop() override;
    void Dispose() override;
    std::optional<WindowId> Create(const WindowSpec& spec);
    bool SetVisible(WindowId id, bool visible);
    bool SetClickThrough(WindowId id, bool clickThrough);
    bool UpdateDpi(WindowId id, unsigned int dpi);
    std::optional<WindowSnapshot> Get(WindowId id) const;
    State state() const noexcept { return state_; }
    std::size_t size() const noexcept { return windows_.size(); }

private:
    struct Record {
        WindowSnapshot snapshot{};
        bool visibleBeforeSuspend = false;
    };

    std::unique_ptr<IWindowBackend> backend_;
    std::map<WindowId, Record> windows_;
    WindowId nextId_ = 1;
    State state_ = State::Ready;
};

}  // namespace lume::window
