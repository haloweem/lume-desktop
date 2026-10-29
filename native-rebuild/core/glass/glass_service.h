#pragma once

#include "core/glass/background_source.h"
#include "core/glass/glass_backend.h"
#include "core/lifecycle/service.h"

#include <memory>

namespace lume::glass {

class GlassService final : public IService {
public:
    GlassService(std::unique_ptr<IBackgroundSource> source,
                 std::unique_ptr<IGlassBackend> backend);
    ~GlassService() override;

    GlassService(const GlassService&) = delete;
    GlassService& operator=(const GlassService&) = delete;

    bool Start() override;
    void Stop() override;
    void Suspend() override;
    void Resume() override;
    void Dispose() override;

    bool Attach(const GlassSnapshot& snapshot);
    bool UpdateBounds(const PhysicalRect& ownerBounds, const PhysicalRect& backgroundBounds);
    GlassMode mode() const noexcept { return mode_; }
    bool attached() const noexcept { return attached_; }

private:
    bool Apply(GlassMode mode);
    void EnterFallback();

    std::unique_ptr<IBackgroundSource> source_;
    std::unique_ptr<IGlassBackend> backend_;
    GlassSnapshot snapshot_{};
    BackendHandle handle_ = 0;
    GlassMode mode_ = GlassMode::TransparentFallback;
    bool attached_ = false;
    bool backendStarted_ = false;
    bool sourceStarted_ = false;
};

}  // namespace lume::glass
