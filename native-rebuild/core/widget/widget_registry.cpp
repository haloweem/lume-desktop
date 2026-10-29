#include "core/widget/widget_registry.h"

namespace lume::widget {
bool WidgetRegistry::Register(std::string id, WidgetFactory factory) {
    if (id.empty() || !factory || factories_.count(id) != 0) return false;
    factories_.emplace(std::move(id), std::move(factory)); return true;
}
const WidgetFactory* WidgetRegistry::Find(const std::string& id) const {
    const auto it = factories_.find(id); return it == factories_.end() ? nullptr : &it->second;
}
}  // namespace lume::widget
