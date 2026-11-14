#pragma once
#include "vendor/nlohmann/json.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace lume::platform::windows::control_center {
// Shared across window generations. A slow system API may finish after its
// panel closes, but it cannot overlap another backend call or publish to the
// next panel. Unbind never waits for that system API on the UI thread.
class PanelWorker final : public std::enable_shared_from_this<PanelWorker> {
public:
    struct Job { std::string key; nlohmann::json value; };
    using Generation=std::uint64_t;
    using Execute=std::function<std::string(Generation,const Job&)>;
    using Notify=std::function<bool(Generation)>;
    static constexpr std::size_t Capacity=64;
    PanelWorker(Execute execute,std::function<void()> initialize={},std::function<void()> cleanup={})
        :execute_(std::move(execute)),initialize_(std::move(initialize)),cleanup_(std::move(cleanup)){}
    Generation Bind(Notify notify) {
        std::lock_guard lock(mutex_);
        ++generation_;notify_=std::move(notify);jobs_.clear();latest_.clear();pending_=false;
        StartLocked();wake_.notify_one();return generation_;
    }
    void Unbind(Generation generation) {
        std::lock_guard lock(mutex_);
        if(generation!=generation_)return;
        notify_={};jobs_.clear();latest_.clear();pending_=false;wake_.notify_one();
    }
    bool Push(Generation generation,Job job) {
        std::lock_guard lock(mutex_);
        if(generation!=generation_||!notify_)return false;
        if(job.key=="volume"||job.key=="brightness")
            jobs_.erase(std::remove_if(jobs_.begin(),jobs_.end(),[&](const Job& old){return old.key==job.key;}),jobs_.end());
        if(job.key=="refresh"&&std::any_of(jobs_.begin(),jobs_.end(),[&](const Job& old){return old.key==job.key&&old.value==job.value;}))return true;
        if(jobs_.size()>=Capacity)return false;
        jobs_.push_back(std::move(job));StartLocked();wake_.notify_one();return true;
    }
    bool PendingActions(Generation generation) const {
        std::lock_guard lock(mutex_);
        return generation!=generation_||!notify_||(!jobs_.empty()&&jobs_.front().key!="refresh");
    }
    std::string Take(Generation generation) {
        std::lock_guard lock(mutex_);
        if(generation!=generation_||!notify_)return {};
        pending_=false;return std::exchange(latest_,{});
    }
    std::size_t Queued() const {std::lock_guard lock(mutex_);return jobs_.size();}
    bool Running() const {std::lock_guard lock(mutex_);return running_;}
private:
    void StartLocked() {
        if(running_)return;
        auto self=shared_from_this();running_=true;
        try{std::thread([self]{self->Run();}).detach();}catch(...){running_=false;throw;}
    }
    void Run() noexcept {
        bool initialized=false;
        for(;;) {
            if(!initialized) {
                try{if(initialize_)initialize_();initialized=true;}
                catch(...){std::lock_guard lock(mutex_);running_=false;return;}
            }
            Job job;Generation generation=0;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock,[&]{return !notify_||!jobs_.empty();});
                if(!notify_) {
                    lock.unlock();
                    try{if(cleanup_)cleanup_();}catch(...){}
                    initialized=false;lock.lock();
                    // Bind may have arrived while apartment cleanup ran.
                    if(notify_)continue;
                    running_=false;return;
                }
                generation=generation_;job=std::move(jobs_.front());jobs_.pop_front();
            }
            std::string result;
            try{result=execute_(generation,job);}catch(...){continue;}
            if(result.empty())continue;
            std::lock_guard lock(mutex_);
            if(generation!=generation_||!notify_)continue;
            latest_=std::move(result);
            if(!pending_)pending_=notify_(generation);
        }
    }
    Execute execute_;std::function<void()> initialize_,cleanup_;
    mutable std::mutex mutex_;std::condition_variable wake_;std::deque<Job> jobs_;
    Generation generation_=0;Notify notify_;std::string latest_;bool pending_=false,running_=false;
};
}
