#pragma once

#include <cstdint>
#include <vector>

namespace lume::ui {

// Pure intent/state boundary for the manager WebView. The platform layer owns
// COM calls; this type only decides which generation is still current.
class WebViewLifecycle final {
public:
    struct Action {
        enum class Type { NotifyVisibility, TrySuspend, TrimWorkingSet, Resume, RequestStateSync };
        Type type;
        std::uint32_t generation = 0;
        bool visible = false;
    };

    std::vector<Action> OnReady() noexcept;
    std::vector<Action> OnHidden() noexcept;
    std::vector<Action> OnVisible() noexcept;
    std::vector<Action> OnCleanupAcknowledged(std::uint32_t generation) noexcept;
    std::vector<Action> OnSuspendCompleted(std::uint32_t generation, bool success) noexcept;
    void Close() noexcept;

private:
    enum class Phase { Cold, Active, Cleaning, Hidden, Closed };
    Phase phase_ = Phase::Cold;
    std::uint32_t generation_ = 0;
    bool suspendRequested_ = false;
};

}  // namespace lume::ui
