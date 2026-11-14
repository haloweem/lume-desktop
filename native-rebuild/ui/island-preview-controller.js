// Event-driven sample preview. Desktop playback never schedules frames.
export function createIslandPreviewController({send,getMode,onFrame,onTimeout=()=>{},setTimer=setTimeout,clearTimer=clearTimeout}) {
  let active=false,timer=null,pending=false,disposed=false,dirty=false,retries=0;
  const schedule=delay=>{if(timer!==null)clearTimer(timer);timer=active?setTimer(poll,delay):null;};
  const poll=()=>{timer=null;if(!active||disposed)return;
    if(pending){onTimeout();pending=false;if(++retries>2)return;}
    pending=true;dirty=false;
    if(!send(getMode())){pending=false;onTimeout();return;}
    if(pending)schedule(2500);
  };
  return {
    update(value){
      if(disposed||active===value)return;
      active=value;pending=false;retries=0;
      if(active)poll();else {if(timer!==null)clearTimer(timer);timer=null;send('stop');}
    },
    refresh(){if(active){retries=0;if(pending)dirty=true;else schedule(0);}},
    receive(frame){if(!active)return;pending=false;retries=0;
      if(timer!==null)clearTimer(timer);timer=null;
      onFrame(frame);if(dirty)schedule(0);else if(frame.animating&&!frame.error)schedule(16);
    },
    dispose(){if(disposed)return;if(timer!==null)clearTimer(timer);timer=null;if(active)send('stop');active=false;pending=false;disposed=true;},
  };
}
