#include "core/widget/widget_service.h"

namespace lume::widget {

bool WidgetService::Start() { if (running_) return true; running_ = true; return true; }

void WidgetService::Stop() {
    if (!running_) return;
    for (const auto& id : runningWidgets_) active_.at(id)->Stop();
    runningWidgets_.clear();
    running_ = false;
}

void WidgetService::Suspend() { if (running_) for (const auto& id : runningWidgets_) active_.at(id)->Suspend(); }
void WidgetService::Resume() { if (running_) for (const auto& id : runningWidgets_) active_.at(id)->Resume(); }

void WidgetService::Dispose() {
    Stop();
    for (auto& item : active_) item.second->Dispose();
    active_.clear();
    runningWidgets_.clear();
}

bool WidgetService::Show(const std::string& id) {
    if (!registry_) return false;
    if (active_.count(id) != 0) {
        if (runningWidgets_.count(id) == 0) {
            if (!active_.at(id)->Start()) return false;
            runningWidgets_.insert(id);
        }
        return true;
    }
    const auto* factory = registry_->Find(id);
    if (!factory) return false;
    auto widget = (*factory)();
    if (!widget || !widget->Start()) return false;
    active_.emplace(id, std::move(widget));
    runningWidgets_.insert(id);
    running_ = true;
    return true;
}

bool WidgetService::Hide(const std::string& id) {
    const auto it = active_.find(id);
    if (it == active_.end()) return false;
    if (runningWidgets_.erase(id) != 0) it->second->Stop();
    return true;
}

}  // namespace lume::widget
