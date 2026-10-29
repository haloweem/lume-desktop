#pragma once

#include "core/lifecycle/service.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace lume {

class ServiceContainer final {
public:
    enum class State {
        Ready,
        Running,
        Suspended,
        Stopped,
        Disposed,
    };

    ServiceContainer() = default;
    ~ServiceContainer();

    ServiceContainer(const ServiceContainer&) = delete;
    ServiceContainer& operator=(const ServiceContainer&) = delete;
    ServiceContainer(ServiceContainer&&) = delete;
    ServiceContainer& operator=(ServiceContainer&&) = delete;

    bool Add(std::unique_ptr<IService> service);
    bool Start();
    void Stop();
    void Suspend();
    void Resume();
    void Dispose();

    State state() const noexcept { return state_; }
    std::size_t size() const noexcept { return services_.size(); }

private:
    std::vector<std::unique_ptr<IService>> services_;
    std::size_t startedServices_ = 0;
    State state_ = State::Ready;
};

}  // namespace lume
