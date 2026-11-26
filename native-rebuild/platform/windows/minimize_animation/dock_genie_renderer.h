#pragma once
#include "platform/windows/minimize_animation/minimize_animation_ipc.h"
#include <memory>

namespace lume::platform::windows::dock_genie {
// Lives in Lume's animation host. Closing the source process does not tear
// down the submitted frame, and all windows on a host share one D3D device.
class Renderer {
public:
    Renderer();
    ~Renderer();
    bool Start(minimize_detail::SharedControl&);
    bool Playing() const;
    void Frame();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
