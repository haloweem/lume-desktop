#include "core/glass/glass_service.h"

#include "core/glass/glass_geometry.h"

#include <utility>

namespace lume::glass {

GlassService::GlassService(std::unique_ptr<IBackgroundSource> source,
                           std::unique_ptr<IGlassBackend> backend)
    : source_(std::move(source)), backend_(std::move(backend)) {}

GlassService::~GlassService() {
    Dispose();
}

bool GlassService::Start() {
    if (backendStarted_ || sourceStarted_) {
        return true;
    }
    if (!source_ || !backend_) {
        mode_ = GlassMode::TransparentFallback;
        return true;
    }
    try {
        sourceStarted_ = source_->Start();
        backendStarted_ = backend_->Start();
    } catch (...) {
        sourceStarted_ = false;
        backendStarted_ = false;
    }
    if (!sourceStarted_) {
        source_->Stop();
    }
    if (!backendStarted_) {
        backend_->Stop();
    }
    mode_ = GlassMode::TransparentFallback;
    return true;
}

bool GlassService::Attach(const GlassSnapshot& snapshot) {
    if (!source_ || !backend_) {
        EnterFallback();
        return true;
    }
    snapshot_ = snapshot;
    snapshot_.coordinates = ComputeMaterialCoordinates(snapshot.ownerBounds, snapshot.backgroundBounds, snapshot.samplePadding);
    if (sourceStarted_ && backendStarted_ && source_->IsReady()) {
        handle_ = backend_->Create(snapshot_);
        if (handle_ != 0 && Apply(GlassMode::Full)) {
            attached_ = true;
            return true;
        }
    }
    if (!backendStarted_) {
        EnterFallback();
        return true;
    }
    mode_ = GlassMode::Reduced;
    attached_ = true;
    if (handle_ != 0 && !Apply(GlassMode::Reduced)) {
        backend_->Destroy(handle_);
        handle_ = 0;
        mode_ = GlassMode::TransparentFallback;
    }
    return true;
}

bool GlassService::UpdateBounds(const PhysicalRect& ownerBounds, const PhysicalRect& backgroundBounds) {
    if (!attached_) {
        return false;
    }
    snapshot_.ownerBounds = ownerBounds;
    snapshot_.backgroundBounds = backgroundBounds;
    snapshot_.coordinates = ComputeMaterialCoordinates(ownerBounds, backgroundBounds, snapshot_.samplePadding);
    return handle_ == 0 || Apply(mode_);
}

bool GlassService::Apply(GlassMode mode) {
    if (handle_ == 0 || !backendStarted_) {
        return false;
    }
    if (!backend_->Update(handle_, snapshot_, mode)) {
        return false;
    }
    mode_ = mode;
    return true;
}

void GlassService::EnterFallback() {
    if (handle_ != 0 && backend_) {
        backend_->Destroy(handle_);
        handle_ = 0;
    }
    mode_ = GlassMode::TransparentFallback;
    attached_ = true;
}

void GlassService::Suspend() {
    if (!attached_) {
        return;
    }
    if (handle_ != 0) {
        if (!Apply(GlassMode::Reduced)) {
            EnterFallback();
        }
    } else {
        mode_ = GlassMode::Reduced;
    }
}

void GlassService::Resume() {
    if (!attached_) {
        return;
    }
    if (handle_ != 0 && source_ && source_->IsReady()) {
        if (!Apply(GlassMode::Full)) {
            EnterFallback();
        }
    } else if (source_ && source_->IsReady() && backendStarted_) {
        handle_ = backend_->Create(snapshot_);
        if (handle_ != 0 && Apply(GlassMode::Full)) {
            mode_ = GlassMode::Full;
            return;
        }
        EnterFallback();
    } else {
        mode_ = GlassMode::Reduced;
    }
}

void GlassService::Stop() {
    if (handle_ != 0 && backend_) {
        backend_->Destroy(handle_);
        handle_ = 0;
    }
    attached_ = false;
    if (backendStarted_ && backend_) {
        backend_->Stop();
        backendStarted_ = false;
    }
    if (sourceStarted_ && source_) {
        source_->Stop();
        sourceStarted_ = false;
    }
    mode_ = GlassMode::TransparentFallback;
}

void GlassService::Dispose() {
    Stop();
    source_.reset();
    backend_.reset();
}

}  // namespace lume::glass
