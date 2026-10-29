const exact=(value,keys)=>value && typeof value==='object' && !Array.isArray(value) &&
  Object.keys(value).length===keys.length && keys.every(key=>Object.hasOwn(value,key));
const integer=(value,min,max)=>Number.isSafeInteger(value)&&value>=min&&value<=max;
const text=(value,max)=>typeof value==='string'&&value.length<=max&&!/[\u0000-\u001f\u007f]/.test(value);
const displayText=value=>typeof value==='string'&&value.length<=512&&new TextEncoder().encode(value).length<=512&&
  !/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/.test(value);
const id=value=>text(value,96)&&/^local-\d+$/.test(value);
const artwork=value=>{
  if(value==='')return true;
  const match=typeof value==='string'&&value.match(/^http:\/\/127\.0\.0\.1:([1-9]\d{0,4})\/[a-f0-9]{64}\/media\.png$/);
  return !!match&&Number(match[1])<=65535;
};

export function validMusicLibraryCommand(action,fields) {
  if(action==='snapshot')return exact(fields,[]);
  if(action==='page')return exact(fields,['offset','limit'])&&integer(fields.offset,0,4096)&&integer(fields.limit,1,100);
  if(action==='visibility')return exact(fields,['visible'])&&typeof fields.visible==='boolean';
  return ['play','enqueue','insertNext','remove'].includes(action)&&exact(fields,['id'])&&id(fields.id);
}
export function validMusicLibraryMessage(message) {
  return exact(message,['version','type','revision','loaded','error','total','offset','limit','tracks']) &&
    message.version===1&&message.type==='music-library-state'&&integer(message.revision,0,Number.MAX_SAFE_INTEGER)&&
    typeof message.loaded==='boolean'&&text(message.error,128)&&integer(message.total,0,4096)&&
    integer(message.offset,0,4096)&&integer(message.limit,1,100)&&Array.isArray(message.tracks)&&
    message.tracks.length<=message.limit&&new Set(message.tracks.map(track=>track.id)).size===message.tracks.length&&
    message.tracks.every(track=>exact(track,['id','title','artist','album','source','artwork','duration','available','error'])&&
      id(track.id)&&['title','artist','album'].every(key=>displayText(track[key]))&&track.source===''&&artwork(track.artwork)&&
      Number.isFinite(track.duration)&&track.duration>=0&&track.duration<=1e9&&
      typeof track.available==='boolean'&&text(track.error,128));
}
export function createMusicLibrary({send,onChange,onError}={}) {
  let state={tracks:[],total:0,offset:0,limit:100,loaded:false,error:'',pending:false};
  let revision=-1,visible=false,disposed=false;
  const emit=()=>onChange?.({...state,tracks:[...state.tracks]});
  function post(action,fields={}) {
    if(disposed||!validMusicLibraryCommand(action,fields))return false;
    if(action!=='visibility'){state={...state,pending:true,error:''};emit();}
    try {if(send?.(action,fields)===true)return true;}catch{}
    state={...state,pending:false,error:'unavailable'};emit();onError?.(state.error);return false;
  }
  return {
    get state(){return {...state,tracks:[...state.tracks]};},
    load:()=>post('snapshot'),
    page:offset=>post('page',{offset,limit:100}),
    play:id=>post('play',{id}),
    enqueue:(id,next=false)=>post(next?'insertNext':'enqueue',{id}),
    remove:id=>post('remove',{id}),
    setVisible(value) {
      if(disposed||visible===(value===true))return;
      visible=value===true;
      post('visibility',{visible});
      if(!visible){state={...state,pending:false,tracks:state.tracks.map(track=>({...track,artwork:''}))};emit();}
    },
    releasePageData() {
      if(disposed)return;
      if(visible)this.setVisible(false);
      if(!state.loaded&&!state.tracks.length&&!state.total&&!state.error&&!state.pending)return;
      state={...state,tracks:[],total:0,offset:0,loaded:false,error:'',pending:false};
      emit();
    },
    receive(message) {
      if(disposed||!validMusicLibraryMessage(message)||message.revision<=revision)return false;
      revision=message.revision;
      const {tracks,total,offset,limit,loaded,error}=message;
      state={tracks,total,offset,limit,loaded,error,pending:false};emit();
      if(error)onError?.(error);
      return true;
    },
    dispose(){if(disposed)return;this.setVisible(false);disposed=true;state={...state,tracks:[]};},
  };
}
