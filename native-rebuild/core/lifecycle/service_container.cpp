#include "core/lifecycle/service_container.h"

#include <utility>

namespace lume {

ServiceContainer::~ServiceContainer() {
    Dispose();
}

bool ServiceContainer::Add(std::unique_ptr<IService> service) {
    if (!service || state_ == State::Running || state_ == State::Suspended || state_ == State::Disposed) {
        return false;
    }

    services_.push_back(std::move(service));
    return true;
}

bool ServiceContainer::Start() {
    if (state_ == State::Running || state_ == State::Suspended) {
        return true;
    }
    if (state_ == State::Disposed) {
        return false;
    }

    for (std::size_t index = 0; index < services_.size(); ++index) {
        bool started = false;
        try {
            started = services_[index]->Start();
        } catch (...) {
            started = false;
        }

        if (!started) {
            services_[index]->Stop();
            for (std::size_t rollback = index; rollback > 0; --rollback) {
                services_[rollback - 1]->Stop();
            }
            startedServices_ = 0;
            state_ = State::Stopped;
            return false;
        }

        ++startedServices_;
    }

    state_ = State::Running;
    return true;
}

void ServiceContainer::Stop() {
    if (state_ != State::Running && state_ != State::Suspended) {
        if (state_ == State::Ready) {
            state_ = State::Stopped;
        }
        return;
    }

    for (std::size_t index = startedServices_; index > 0; --index) {
        services_[index - 1]->Stop();
    }
    startedServices_ = 0;
    state_ = State::Stopped;
}

void ServiceContainer::Suspend() {
    if (state_ != State::Running) {
        return;
    }

    for (std::size_t index = startedServices_; index > 0; --index) {
        services_[index - 1]->Suspend();
    }
    state_ = State::Suspended;
}

void ServiceContainer::Resume() {
    if (state_ != State::Suspended) {
        return;
    }

    for (std::size_t index = 0; index < startedServices_; ++index) {
        services_[index]->Resume();
    }
    state_ = State::Running;
}

void ServiceContainer::Dispose() {
    if (state_ == State::Disposed) {
        return;
    }

    if (state_ == State::Running || state_ == State::Suspended) {
        Stop();
    }

    for (std::size_t index = services_.size(); index > 0; --index) {
        services_[index - 1]->Dispose();
    }
    services_.clear();
    startedServices_ = 0;
    state_ = State::Disposed;
}

}  // namespace lume
