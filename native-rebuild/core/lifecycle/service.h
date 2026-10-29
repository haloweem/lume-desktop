#pragma once

namespace lume {

class IService {
public:
    virtual ~IService() = default;

    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual void Suspend() = 0;
    virtual void Resume() = 0;
    virtual void Dispose() = 0;
};

}  // namespace lume
