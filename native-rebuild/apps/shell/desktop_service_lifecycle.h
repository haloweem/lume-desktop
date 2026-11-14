#pragma once
#include <functional>
#include <string>
#include <vector>

namespace lume::shell {
// The composition root supplies services in startup order. Shutdown order is
// explicit because producers must stop before the consumers they call into.
// No window, feature type or application state is known by this coordinator.
class DesktopServiceLifecycle final {
public:
    struct Service {
        std::string name;
        int shutdownOrder;
        std::function<void()> restore;
        std::function<bool()> shutdown;
        std::function<bool()> hasWork;
    };
    void Add(Service);
    void Restore();
    bool HasWork() const;
    // Successful stops are consumed once; failed stops remain eligible for a
    // later retry. Reentrant shutdown does not enter a service twice.
    bool Shutdown() noexcept;
private:
    struct Entry { Service service; bool stopped = false; };
    std::vector<Entry> services_;
    bool restored_ = false, stopping_ = false, shutdownRequested_ = false;
};
}
