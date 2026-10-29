const exact=(value,keys)=>value && typeof value==='object' && !Array.isArray(value) &&
  Object.keys(value).length===keys.length&&keys.every(key=>Object.hasOwn(value,key));
export function validMusicDesktopCommand(action,fields) {
  return action==='snapshot'&&exact(fields,[]) ||
    action==='setEnabled'&&exact(fields,['value'])&&typeof fields.value==='boolean';
}
export function validMusicDesktopMessage(message) {
  return exact(message,['version','type','enabled','running','stopped','locked','material','width','height',
    'loadStatus','error','smtc','result','saved'])&&message.version===1&&message.type==='music-desktop-state'&&
    ['enabled','running','stopped','locked','saved'].every(key=>typeof message[key]==='boolean')&&
    ['liquid-glass','frosted-glass'].includes(message.material)&&
    Number.isInteger(message.width)&&message.width>=320&&message.width<=720&&
    Number.isInteger(message.height)&&message.height>=152&&message.height<=720&&
    ['not-restored','fresh','loaded','recovered','failed'].includes(message.loadStatus)&&
    ['','path','load','runtime','rollback','save','smtc','settings','not-restored'].includes(message.error)&&
    exact(message.smtc,['registered','status'])&&typeof message.smtc.registered==='boolean'&&
    ['stopped','not-restored','verification','failed','ready','inactive'].includes(message.smtc.status)&&
    ['snapshot','applied'].includes(message.result);
}
export function createMusicDesktop({send,onChange,onError}={}) {
  let state={enabled:false,running:false,error:'',pending:false},disposed=false;
  const emit=()=>onChange?.({...state});
  function post(action,fields={}) {
    if(disposed||!validMusicDesktopCommand(action,fields))return false;
    if(action==='setEnabled'&&state.pending)return false;
    state={...state,pending:action==='setEnabled'};emit();
    try {if(send?.(action,fields)===true)return true;}catch{}
    state={...state,pending:false,error:'unavailable'};emit();onError?.(state.error);return false;
  }
  return {
    get state(){return {...state};},
    load:()=>post('snapshot'),
    setEnabled:value=>post('setEnabled',{value}),
    receive(message) {
      if(disposed||!validMusicDesktopMessage(message))return false;
      state={...message,pending:false};emit();
      if(message.error)onError?.(message.error);
      return true;
    },
    dispose(){disposed=true;},
  };
}
