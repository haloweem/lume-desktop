#include "platform/windows/d3d11_glass_backend.h"

#include <dxgi.h>

namespace lume::glass {

D3D11GlassBackend::~D3D11GlassBackend() {
    Stop();
}

bool D3D11GlassBackend::Start() {
    if (started_) {
        return true;
    }

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    const HRESULT deviceResult = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        device_.GetAddressOf(),
        &featureLevel,
        context_.GetAddressOf());
    if (FAILED(deviceResult)) {
        device_.Reset();
        context_.Reset();
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) {
        device_.Reset();
        context_.Reset();
        return false;
    }

    if (FAILED(DCompositionCreateDevice(
            dxgiDevice.Get(),
            __uuidof(IDCompositionDevice),
            reinterpret_cast<void**>(compositionDevice_.GetAddressOf())))) {
        device_.Reset();
        context_.Reset();
        compositionDevice_.Reset();
        return false;
    }

    started_ = true;
    return true;
}

void D3D11GlassBackend::Stop() {
    instances_.clear();
    compositionDevice_.Reset();
    context_.Reset();
    device_.Reset();
    started_ = false;
}

BackendHandle D3D11GlassBackend::Create(const GlassSnapshot& snapshot) {
    if (!started_) {
        return 0;
    }
    const BackendHandle handle = nextHandle_++;
    instances_.emplace(handle, Instance{snapshot, GlassMode::TransparentFallback});
    return handle;
}

bool D3D11GlassBackend::Update(BackendHandle handle,
                               const GlassSnapshot& snapshot,
                               GlassMode mode) {
    if (!started_) {
        return false;
    }
    const auto found = instances_.find(handle);
    if (found == instances_.end()) {
        return false;
    }
    found->second.snapshot = snapshot;
    found->second.mode = mode;
    return true;
}

void D3D11GlassBackend::Destroy(BackendHandle handle) {
    instances_.erase(handle);
}

}  // namespace lume::glass
