#pragma once
#include <windows.h>
#include <cstddef>

namespace lume_glass {
// A slot is reserved before creating a capture, and remains occupied until its
// final cleanup returns. A stalled OS Close therefore bounds both the queue
// and all live captures, without blocking render/UI callers or adding threads.
// Instances deliberately have process lifetime; never destroy one after Start.
class CaptureRetirementQueue final {
public:
    static constexpr unsigned Capacity = 32;
    static constexpr unsigned Invalid = Capacity;
    using Prepare = bool (*)() noexcept;
    using Destroy = void (*)(void*) noexcept;
    struct Statistics { unsigned total=0,active=0,pending=0,cleaning=0;bool ready=false; };

    // The production callbacks pin only LumeGlass.dll and establish the same
    // process-lifetime MTA lease used by material workers. Tests supply their
    // own callbacks, rather than treating an EXE as a pinnable material DLL.
    CaptureRetirementQueue(Prepare pinModule,Prepare initializeWorker)
        : pinModule_(pinModule),initializeWorker_(initializeWorker) {
        wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        started_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    }
    CaptureRetirementQueue(const CaptureRetirementQueue&)=delete;
    CaptureRetirementQueue& operator=(const CaptureRetirementQueue&)=delete;

    unsigned Reserve(const void* graphics) noexcept {
        if(!Start())return Invalid;
        AcquireSRWLockExclusive(&lock_);
        unsigned slot=Invalid;
        for(unsigned i=0;i<Capacity;++i)if(records_[i].state==Free){
            records_[i].state=Active;records_[i].graphics=graphics;slot=i;break;
        }
        ReleaseSRWLockExclusive(&lock_);
        return slot;
    }
    // Only used when construction failed before a capture existed.
    void Abandon(unsigned slot) noexcept {
        if(slot>=Capacity)return;
        AcquireSRWLockExclusive(&lock_);
        if(records_[slot].state==Active)records_[slot]=Record{};
        ReleaseSRWLockExclusive(&lock_);
    }
    // The shared_ptr deleter only transfers ownership into its preallocated
    // record. It performs no COM work, allocation, waiting or inline fallback.
    void Retire(unsigned slot,void* object,Destroy destroy) noexcept {
        if(slot>=Capacity||!object||!destroy)return;
        AcquireSRWLockExclusive(&lock_);
        auto& record=records_[slot];
        record.object=object;record.destroy=destroy;record.state=Pending;record.next=Invalid;
        if(tail_!=Invalid)records_[tail_].next=slot;else head_=slot;
        tail_=slot;
        ReleaseSRWLockExclusive(&lock_);
        SetEvent(wake_);
    }
    Statistics Inspect() const noexcept {
        Statistics result;
        AcquireSRWLockShared(&lock_);
        result.ready=ready_;
        for(const auto& record:records_){
            if(record.state!=Free)++result.total;
            if(record.state==Active)++result.active;
            if(record.state==Pending)++result.pending;
            if(record.state==Cleaning)++result.cleaning;
        }
        ReleaseSRWLockShared(&lock_);
        return result;
    }
    unsigned DeviceCount(const void* current) const noexcept {
        const void* devices[Capacity+1]{};unsigned count=0;
        if(current)devices[count++]=current;
        AcquireSRWLockShared(&lock_);
        for(const auto& record:records_)if(record.state!=Free&&record.graphics){
            bool seen=false;for(unsigned i=0;i<count;++i)if(devices[i]==record.graphics){seen=true;break;}
            if(!seen)devices[count++]=record.graphics;
        }
        ReleaseSRWLockShared(&lock_);
        return count;
    }
private:
    ~CaptureRetirementQueue()=delete;
    enum State { Free,Active,Pending,Cleaning };
    struct Record {
        State state=Free;const void* graphics=nullptr;void* object=nullptr;
        Destroy destroy=nullptr;unsigned next=Invalid;
    };
    bool Start() noexcept {
        bool first=false;
        AcquireSRWLockExclusive(&lock_);
        if(!attempted_){
            first=true;
            attempted_=true;
            if(wake_&&started_&&(!pinModule_||pinModule_())){
                HANDLE thread=CreateThread(nullptr,0,&Work,this,0,nullptr);
                if(thread)CloseHandle(thread);else SetEvent(started_);
            }else if(started_)SetEvent(started_);
        }
        ReleaseSRWLockExclusive(&lock_);
        // Worker initialization cannot make a material caller wait forever.
        if(!started_||WaitForSingleObject(started_,first?500:0)!=WAIT_OBJECT_0)return false;
        AcquireSRWLockShared(&lock_);const bool ready=ready_;ReleaseSRWLockShared(&lock_);
        return ready;
    }
    static DWORD WINAPI Work(void* context) noexcept {
        auto& self=*static_cast<CaptureRetirementQueue*>(context);
        const bool ready=!self.initializeWorker_||self.initializeWorker_();
        AcquireSRWLockExclusive(&self.lock_);self.ready_=ready;ReleaseSRWLockExclusive(&self.lock_);
        SetEvent(self.started_);
        if(!ready)return 0;
        while(WaitForSingleObject(self.wake_,INFINITE)==WAIT_OBJECT_0){
            for(;;){
                AcquireSRWLockExclusive(&self.lock_);
                const unsigned slot=self.head_;
                if(slot==Invalid){ReleaseSRWLockExclusive(&self.lock_);break;}
                auto& record=self.records_[slot];self.head_=record.next;
                if(self.head_==Invalid)self.tail_=Invalid;
                record.state=Cleaning;const auto object=record.object;const auto destroy=record.destroy;
                ReleaseSRWLockExclusive(&self.lock_);
                destroy(object);
                AcquireSRWLockExclusive(&self.lock_);self.records_[slot]=Record{};ReleaseSRWLockExclusive(&self.lock_);
            }
        }
        return 0;
    }
    Prepare pinModule_=nullptr,initializeWorker_=nullptr;
    HANDLE wake_=nullptr,started_=nullptr;
    mutable SRWLOCK lock_=SRWLOCK_INIT;
    Record records_[Capacity]{};
    unsigned head_=Invalid,tail_=Invalid;
    bool attempted_=false,ready_=false;
};
}
