#pragma once

#include "core/glass/glass_types.h"

namespace lume::glass {

class IBackgroundSource {
public:
    virtual ~IBackgroundSource() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool IsReady() const noexcept = 0;
    virtual BackgroundToken Token() const noexcept = 0;
};

}  // namespace lume::glass
