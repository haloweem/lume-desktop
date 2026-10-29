const exact=(value,keys)=>value!==null && typeof value==='object' && !Array.isArray(value) &&
  Object.keys(value).length===keys.length && keys.every(key=>Object.hasOwn(value,key));
const sequence=value=>Number.isSafeInteger(value) && value>=0;
const FORMATS=Object.freeze({jpg:['image','image/jpeg'],jpeg:['image','image/jpeg'],
  png:['image','image/png'],webp:['image','image/webp'],gif:['image','image/gif'],
  mp4:['video','video/mp4'],webm:['video','video/webm']});
const sourceId=value=>typeof value==='string' && /^[A-Za-z0-9_.-]{1,96}$/.test(value) &&
  !value.endsWith('.') && !/^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(value);
const name=value=>typeof value==='string' && !!value && new TextEncoder().encode(value).length<=512 &&
  !/[\u0000-\u001f\u007f-\u009f\ud800-\udfff]/u.test(value);
const ERRORS=['','invalid','load','save','conflict','unsupported','too-large'];

export function validMusicBackgroundLoopbackUrl(value) {
  if(typeof value!=='string')return false;
  const match=/^http:\/\/127\.0\.0\.1:([1-9]\d{0,4})\/[a-f0-9]{64}\/media\.(mp4|webm|jpg|jpeg|png|webp|gif)$/.exec(value);
  return !!match && Number(match[1])<=65535;
}

export function validMusicBackgroundThumbnailUrl(value) {
  if(value==='' || validMusicBackgroundLoopbackUrl(value))return true;
  if(typeof value!=='string' || value.length>4096 || /[\u0000-\u0020\u007f\\#]/.test(value) ||
    !/^https:\/\/(?:static\.lumedesktop\.com|lume-12345-1444884137\.cos\.ap-nanjing\.myqcloud\.com)\/lume\//.test(value))return false;
  try {
    const path=new URL(value).pathname;
    const original=value.slice(value.indexOf('/lume/')).split('?')[0];
    if(path!==original || /%(?:2f|5c|25|3f|23)/i.test(original))return false;
    const decoded=decodeURIComponent(original);
    return !/[\u0000-\u001f\u007f]/.test(decoded) && !decoded.split('/').some(part=>part==='.' || part==='..');
  } catch {return false;}
}

export function validMusicBackgroundMedia(media) {
  if(media===null)return true;
  if(!exact(media,['id','name','kind','mimeType','url']) ||
    typeof media.id!=='string' || !/^[a-z0-9-]{1,80}$/.test(media.id) ||
    !name(media.name) || typeof media.url!=='string')return false;
  return Object.entries(FORMATS).some(([extension,[kind,mimeType]])=>
    media.kind===kind && media.mimeType===mimeType &&
    (media.url===`https://lume-music.local/${media.id}.${extension}` ||
      validMusicBackgroundLoopbackUrl(media.url) && media.url.endsWith(`.${extension}`)));
}

export function validMusicBackgroundMessage(value) {
  return exact(value,['version','type','revision','request','status','error','media']) &&
    value.version===1 && value.type==='music-background-state' && sequence(value.revision) &&
    sequence(value.request) && ['ready','busy','error'].includes(value.status) &&
    ERRORS.includes(value.error) &&
    (value.status==='error' ? value.error!=='' : value.error==='') && validMusicBackgroundMedia(value.media);
}

export function validMusicBackgroundCommand(action,fields) {
  if(action==='snapshot')return exact(fields,[]);
  if(action==='catalog')return exact(fields,['request','offset','limit']) &&
    sequence(fields.request) && fields.request>0 && sequence(fields.offset) && fields.offset<=20000 &&
    Number.isSafeInteger(fields.limit) && fields.limit>0 && fields.limit<=120;
  return ['choose','clear','select'].includes(action) &&
    exact(fields,action==='select'?['request','revision','id']:['request','revision']) &&
    (action!=='select' || sourceId(fields.id)) &&
    sequence(fields.request) && fields.request>0 && sequence(fields.revision);
}

export function validMusicBackgroundCatalogMessage(value) {
  return exact(value,['version','type','request','status','error','total','items','selectedId']) &&
    value.version===1 && value.type==='music-background-catalog' && sequence(value.request) && value.request>0 &&
    ['ready','busy','error'].includes(value.status) && ERRORS.includes(value.error) &&
    (value.status==='error'?value.error!=='':value.error==='') &&
    sequence(value.total) && value.total<=20000 && Array.isArray(value.items) && value.items.length<=120 && value.items.length<=value.total &&
    (value.selectedId==='' || sourceId(value.selectedId)) &&
    new Set(value.items.map(item=>item?.id)).size===value.items.length &&
    value.items.every(item=>exact(item,['id','name','type','thumbnailUrl']) && sourceId(item.id) && name(item.name) &&
      ['image','video'].includes(item.type) && validMusicBackgroundThumbnailUrl(item.thumbnailUrl));
}

// Catalog reads have their own correlation stream, never the media store's mutation counter.
export function createMusicBackgroundCatalogQuery(bridge,{timeout=10000}={}) {
  let request=0,pending=null;
  return ({offset=0,limit=120,signal}={})=>{
    pending?.();
    return new Promise((resolve,reject)=>{
      const fields={request:request+1,offset,limit};
      if(signal?.aborted || !validMusicBackgroundCommand('catalog',fields)) {
        reject(new Error('catalog-cancelled'));return;
      }
      request=fields.request;
      let unsubscribe=()=>{},timer=null,done=false;
      const finish=(error,result)=>{
        if(done)return;
        done=true;unsubscribe();clearTimeout(timer);
        signal?.removeEventListener('abort',cancel);
        if(pending===cancel)pending=null;
        if(error)reject(new Error(error));else resolve(result);
      };
      const cancel=()=>finish('catalog-cancelled');
      pending=cancel;
      try {
        unsubscribe=bridge.subscribe(message=>{
          if(!validMusicBackgroundCatalogMessage(message) || message.request!==fields.request)return;
          if(message.status==='busy')return;
          if(message.items.length>limit || message.items.length>Math.max(0,message.total-offset))
            return finish('invalid');
          finish(message.error||null,{...message,items:message.items.map(item=>({...item}))});
        });
        signal?.addEventListener('abort',cancel,{once:true});
        timer=setTimeout(()=>finish('catalog-timeout'),timeout);
        if(bridge.musicBackground('catalog',fields)!==true)finish('catalog-unavailable');
      } catch {finish('catalog-unavailable');}
    });
  };
}

// The native owner selects files and commits media. Page state contains no paths or drafts.
export function createMusicBackground({send,onChange,onError}={}) {
  let media=null,revision=0,request=0,pending=null,externalBusy=false,loaded=false,error='',disposed=false;
  const changed=()=>onChange?.(api.snapshot());
  function act(action,extra={},resolve) {
    if(disposed || !loaded || pending || externalBusy || request===Number.MAX_SAFE_INTEGER)return false;
    const fields={request:++request,revision,...extra};
    pending={request,revision,resolve};error='';changed();
    try {if(send?.(action,fields)===true)return true;}catch{}
    pending=null;resolve?.(false);error='save';changed();onError?.(error);return false;
  }
  const api={
    snapshot:()=>({media,revision,loaded,pending:!!pending||externalBusy,error}),
    load(){try{return send?.('snapshot',{})===true;}catch{return false;}},
    choose:()=>act('choose'),
    clear:()=>act('clear'),
    select:id=>new Promise(resolve=>{
      if(!sourceId(id) || !act('select',{id},resolve))resolve(false);
    }),
    receive(message) {
      if(disposed || !validMusicBackgroundMessage(message) || message.revision<revision)return false;
      if(message.request!==0 && message.request!==pending?.request && !externalBusy)return false;
      media=message.media===null?null:Object.freeze({...message.media});
      revision=message.revision;error=message.error;loaded=error!=='load';
      if(message.status==='busy') {
        if(!pending)externalBusy=true;
      } else {
        if(message.request===pending?.request || pending && revision>pending.revision) {
          const previous=pending;pending=null;
          previous?.resolve?.(message.request===previous.request && revision>previous.revision && !error && media!==null);
        }
        externalBusy=false;
      }
      changed();if(error)onError?.(error);
      return true;
    },
    dispose(){disposed=true;pending?.resolve?.(false);pending=null;},
  };
  return api;
}
