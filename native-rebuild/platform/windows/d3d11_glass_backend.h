#pragma once

#include "core/glass/glass_backend.h"

#include <d3d11.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <unordered_map>

namespace lume::glass {

class D3D11GlassBackend final : public IGlassBackend {
public:
    D3D11GlassBackend() = default;
    ~D3D11GlassBackend() override;

    bool Start() override;
    void Stop() override;
    BackendHandle Create(const GlassSnapshot& snapshot) override;
    bool Update(BackendHandle handle, const GlassSnapshot& snapshot, GlassMode mode) override;
    void Destroy(BackendHandle handle) override;

    bool available() const noexcept { return started_; }

private:
    struct Instance {
        GlassSnapshot snapshot{};
        GlassMode mode = GlassMode::TransparentFallback;
    };

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> compositionDevice_;
    std::unordered_map<BackendHandle, Instance> instances_;
    BackendHandle nextHandle_ = 1;
    bool started_ = false;
};

}  // namespace lume::glass
