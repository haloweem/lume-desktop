#pragma once

#include "core/lifecycle/service.h"
#include "core/widget/widget_registry.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace lume::widget {

class WidgetService final : public IService {
public:
    explicit WidgetService(const WidgetRegistry& registry) : registry_(&registry) {}
    WidgetService() = default;
    bool Start() override;
    void Stop() override;
    void Suspend() override;
    void Resume() override;
    void Dispose() override;
    bool Show(const std::string& id);
    bool Hide(const std::string& id);

private:
    const WidgetRegistry* registry_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<IWidget>> active_;
    std::unordered_set<std::string> runningWidgets_;
    bool running_ = false;
};

}  // namespace lume::widget
