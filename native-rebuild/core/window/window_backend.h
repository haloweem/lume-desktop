#pragma once

#include "core/window/window_types.h"

namespace lume::window {

class IWindowBackend {
public:
    virtual ~IWindowBackend() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool Create(WindowId id, WindowRole role, const PhysicalRect& bounds, bool clickThrough) = 0;
    virtual void Show(WindowId id) = 0;
    virtual void Hide(WindowId id) = 0;
    virtual void Move(WindowId id, const PhysicalRect& bounds) = 0;
    virtual void SetClickThrough(WindowId id, bool clickThrough) = 0;
    virtual void Destroy(WindowId id) = 0;
};

}  // namespace lume::window
