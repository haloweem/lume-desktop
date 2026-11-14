#pragma once
#include <cstdint>

namespace lume_glass {
// A slow frame alone is not a lost swap chain. Only consecutive full waits on
// an active surface retire it; a successful slot or a hidden owner resets this.
class FrameLatencyRecovery final {
public:
    static constexpr unsigned TimeoutLimit=3;
    bool TimedOut(bool active) noexcept {
        if(!active){Ready();return false;}
        if(timeouts_<TimeoutLimit)++timeouts_;
        return timeouts_>=TimeoutLimit;
    }
    void Ready() noexcept {timeouts_=0;}
    unsigned Timeouts() const noexcept {return timeouts_;}
private:
    unsigned timeouts_=0;
};

// Display mode changes need not change HMONITOR. Keep their generation in the
// cache key, periodically recheck the mode, and bound retries for a lost output.
class DisplayTimingRefresh final {
public:
    static constexpr std::uint64_t CheckMs=1000,RetryMs=250;
    bool Due(std::uintptr_t monitor,std::uint64_t generation,std::uint64_t now) const noexcept {
        return !attempted_||monitor!=monitor_||generation!=generation_||now>=nextCheck_;
    }
    bool NeedsRebind(std::uintptr_t monitor,std::uint64_t generation,bool factoryCurrent) const noexcept {
        return !attempted_||!available_||monitor!=monitor_||generation!=generation_||!factoryCurrent;
    }
    void Checked(std::uintptr_t monitor,std::uint64_t generation,std::uint64_t now,bool available) noexcept {
        attempted_=true;monitor_=monitor;generation_=generation;available_=available;
        nextCheck_=now+(available?CheckMs:RetryMs);
    }
    void WaitFailed(std::uint64_t now) noexcept {
        available_=false;nextCheck_=now+RetryMs;
    }
private:
    bool attempted_=false,available_=false;
    std::uintptr_t monitor_=0;
    std::uint64_t generation_=0,nextCheck_=0;
};
}
