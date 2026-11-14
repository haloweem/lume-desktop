#include "apps/shell/desktop_service_lifecycle.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace lume::shell {
void DesktopServiceLifecycle::Add(Service service) {
    if (restored_ || shutdownRequested_) throw std::logic_error("desktop lifecycle registration is closed");
    if (service.name.empty() || std::any_of(services_.begin(), services_.end(), [&](const auto& item) {
        return item.service.name == service.name;
    })) throw std::invalid_argument("desktop service needs a unique name");
    services_.push_back({std::move(service), false});
}
void DesktopServiceLifecycle::Restore() {
    if (restored_ || shutdownRequested_) return;
    restored_ = true;
    for (const auto& item : services_) if (item.service.restore) item.service.restore();
}
bool DesktopServiceLifecycle::HasWork() const {
    if (shutdownRequested_) return false;
    return std::any_of(services_.begin(), services_.end(), [](const auto& item) {
        return item.service.hasWork && item.service.hasWork();
    });
}
bool DesktopServiceLifecycle::Shutdown() noexcept {
    if (stopping_) return false;
    stopping_ = true; shutdownRequested_ = true;
    bool complete = true;
    // The list is small. Walk by priority without allocating during teardown.
    bool havePrevious = false;
    int previous = 0;
    for (;;) {
        bool found = false;
        int next = 0;
        for (const auto& item : services_) if (!item.stopped && (!havePrevious || item.service.shutdownOrder > previous)) {
            if (!found || item.service.shutdownOrder < next) { next = item.service.shutdownOrder; found = true; }
        }
        if (!found) break;
        for (auto& item : services_) if (!item.stopped && item.service.shutdownOrder == next) {
            try { item.stopped = !item.service.shutdown || item.service.shutdown(); }
            catch (...) { item.stopped = false; }
            if (!item.stopped) complete = false;
        }
        previous = next; havePrevious = true;
    }
    stopping_ = false;
    return complete;
}
}
