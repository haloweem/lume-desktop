#include "core/window/window_service.h"

#include "core/window/window_geometry.h"

#include <utility>

namespace lume::window {

WindowService::WindowService(std::unique_ptr<IWindowBackend> backend)
    : backend_(std::move(backend)) {}

WindowService::~WindowService() {
    Dispose();
}

bool WindowService::Start() {
    if (state_ == State::Running) {
        return true;
    }
    if (state_ == State::Disposed || !backend_) {
        return false;
    }
    try {
        if (!backend_->Start()) {
            return false;
        }
    } catch (...) {
        return false;
    }
    state_ = State::Running;
    return true;
}

std::optional<WindowId> WindowService::Create(const WindowSpec& spec) {
    if (state_ != State::Running || !backend_ || spec.dpi == 0 || spec.logicalBounds.width <= 0 || spec.logicalBounds.height <= 0) {
        return std::nullopt;
    }

    const WindowId id = nextId_;
    const PhysicalRect physical = ScaleLogicalToPhysical(spec.logicalBounds, spec.dpi);
    try {
        if (!backend_->Create(id, spec.role, physical, spec.clickThrough)) {
            return std::nullopt;
        }
        windows_.emplace(id, Record{{id, spec.role, spec.logicalBounds, physical, spec.dpi, spec.clickThrough, false}, spec.visible});
        ++nextId_;
        if (spec.visible) {
            backend_->Show(id);
            windows_.at(id).snapshot.visible = true;
        }
        return id;
    } catch (...) {
        windows_.erase(id);
        backend_->Destroy(id);
        return std::nullopt;
    }
}

bool WindowService::SetVisible(WindowId id, bool visible) {
    auto found = windows_.find(id);
    if (state_ != State::Running || found == windows_.end() || found->second.snapshot.visible == visible) {
        return state_ == State::Running && found != windows_.end();
    }
    if (visible) {
        backend_->Show(id);
    } else {
        backend_->Hide(id);
    }
    found->second.snapshot.visible = visible;
    return true;
}

bool WindowService::SetClickThrough(WindowId id, bool clickThrough) {
    auto found = windows_.find(id);
    if (state_ != State::Running || found == windows_.end()) {
        return false;
    }
    backend_->SetClickThrough(id, clickThrough);
    found->second.snapshot.clickThrough = clickThrough;
    return true;
}

bool WindowService::UpdateDpi(WindowId id, unsigned int dpi) {
    auto found = windows_.find(id);
    if (state_ != State::Running || found == windows_.end() || dpi == 0) {
        return false;
    }
    found->second.snapshot.dpi = dpi;
    found->second.snapshot.physicalBounds = ScaleLogicalToPhysical(found->second.snapshot.logicalBounds, dpi);
    backend_->Move(id, found->second.snapshot.physicalBounds);
    return true;
}

std::optional<WindowSnapshot> WindowService::Get(WindowId id) const {
    const auto found = windows_.find(id);
    if (found == windows_.end()) {
        return std::nullopt;
    }
    return found->second.snapshot;
}

void WindowService::Suspend() {
    if (state_ != State::Running) {
        return;
    }
    for (auto& [id, record] : windows_) {
        record.visibleBeforeSuspend = record.snapshot.visible;
        if (record.snapshot.visible) {
            backend_->Hide(id);
            record.snapshot.visible = false;
        }
    }
    state_ = State::Suspended;
}

void WindowService::Resume() {
    if (state_ != State::Suspended || !backend_) {
        return;
    }
    state_ = State::Running;
    for (auto& [id, record] : windows_) {
        if (record.visibleBeforeSuspend) {
            backend_->Show(id);
            record.snapshot.visible = true;
        }
    }
}

void WindowService::Stop() {
    if (state_ == State::Disposed || state_ == State::Ready || state_ == State::Stopped) {
        return;
    }
    if (state_ == State::Running || state_ == State::Suspended) {
        for (auto& [id, record] : windows_) {
            if (record.snapshot.visible) {
                backend_->Hide(id);
                record.snapshot.visible = false;
            }
        }
    }
    for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
        backend_->Destroy(it->first);
    }
    windows_.clear();
    backend_->Stop();
    state_ = State::Stopped;
}

void WindowService::Dispose() {
    if (state_ == State::Disposed) {
        return;
    }
    if (state_ != State::Ready && state_ != State::Stopped) {
        Stop();
    }
    if (state_ == State::Stopped && !windows_.empty()) {
        for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
            backend_->Destroy(it->first);
        }
        windows_.clear();
    }
    backend_.reset();
    state_ = State::Disposed;
}

}  // namespace lume::window
