#pragma once

#include "core/glass/glass_types.h"

namespace lume::glass {

class IGlassBackend {
public:
    virtual ~IGlassBackend() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual BackendHandle Create(const GlassSnapshot& snapshot) = 0;
    virtual bool Update(BackendHandle handle, const GlassSnapshot& snapshot, GlassMode mode) = 0;
    virtual void Destroy(BackendHandle handle) = 0;
};

}  // namespace lume::glass
