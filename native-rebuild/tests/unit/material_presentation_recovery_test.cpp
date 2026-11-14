#include "platform/windows/material/presentation_recovery.h"
#include <cstdlib>
#include <iostream>

namespace {
unsigned checks=0;
void Check(bool ok,const char* reason){
    if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);}
    ++checks;
}
}
int main(){
    lume_glass::FrameLatencyRecovery frames;
    Check(!frames.TimedOut(true),"one full timeout is tolerated");
    Check(!frames.TimedOut(true),"two full timeouts are tolerated");
    Check(frames.TimedOut(true)&&frames.Timeouts()==3,"third consecutive active timeout requests recreation");
    frames.Ready();
    for(unsigned n=0;n<20;++n){
        Check(!frames.TimedOut(true),"isolated slow frames do not accumulate across successful slots");
        frames.Ready();
    }
    Check(!frames.TimedOut(true)&&!frames.TimedOut(true),"begin another active timeout streak");
    Check(!frames.TimedOut(false)&&frames.Timeouts()==0,"hiding or suppression clears the active failure streak");
    Check(!frames.TimedOut(true),"restoration starts a new observation window");
    Check(!frames.TimedOut(true)&&frames.TimedOut(true),"a new persistent stall still recovers after restoration");
    for(unsigned n=0;n<10;++n)Check(!frames.TimedOut(false),"hidden prewarming never requests a timeout rebuild");

    lume_glass::DisplayTimingRefresh timing;
    constexpr std::uintptr_t primary=11,secondary=22;
    Check(timing.Due(primary,0,100)&&timing.NeedsRebind(primary,0,true),"first frame resolves its display");
    timing.Checked(primary,0,100,true);
    Check(!timing.Due(primary,0,1099),"healthy display is not enumerated each frame");
    Check(timing.Due(primary,0,1100)&&!timing.NeedsRebind(primary,0,true),
          "periodic mode recheck retains a healthy output");
    Check(timing.Due(primary,1,200)&&timing.NeedsRebind(primary,1,true),
          "same-monitor display generation invalidates the output and refresh period immediately");
    timing.Checked(primary,1,200,true);
    Check(!timing.Due(primary,1,201),"processing a generation does not repeatedly invalidate it");
    Check(timing.NeedsRebind(primary,1,false),"an obsolete DXGI factory forces output re-resolution");
    Check(timing.Due(secondary,1,201)&&timing.NeedsRebind(secondary,1,true),"changing monitors bypasses old cache delay");
    timing.Checked(primary,1,200,true);
    timing.WaitFailed(250);
    Check(!timing.Due(primary,1,499),"failed vblank waits do not cause per-frame enumeration");
    Check(timing.Due(primary,1,500)&&timing.NeedsRebind(primary,1,true),"failed output is replaced even on the same monitor");
    timing.Checked(primary,1,500,false);
    Check(!timing.Due(primary,1,749)&&timing.Due(primary,1,750),"failed lookup keeps a bounded retry interval");
    Check(timing.Due(primary,2,501),"new resume notification does not wait for an old lookup cooldown");
    timing.Checked(primary,2,501,true);
    Check(!timing.Due(primary,2,502)&&!timing.NeedsRebind(primary,2,true),"successful recovery returns to the healthy path");
    timing.WaitFailed(1000);
    unsigned attempts=0;
    for(std::uint64_t now=1000;now<2000;++now)if(timing.Due(primary,2,now)){
        ++attempts;timing.Checked(primary,2,now,false);
    }
    Check(attempts==3,"a missing output cannot create an unbounded lookup loop");
    timing.Checked(primary,2,2000,true);
    Check(!timing.NeedsRebind(primary,2,true),"recovering output is reusable after repeated failures");
    std::cout<<"PASS material presentation recovery: "<<checks<<" checks\n";
    return 0;
}
