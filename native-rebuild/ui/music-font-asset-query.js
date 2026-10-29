const exact=(value,keys)=>value && typeof value==='object' && !Array.isArray(value) &&
  Object.keys(value).length===keys.length && keys.every(key=>Object.hasOwn(value,key));
const count=value=>Number.isSafeInteger(value)&&value>=0;
const id=value=>typeof value==='string'&&/^[a-f0-9]{32}$/.test(value);
export const validMusicFontAsset=asset=>exact(asset,['id','family','label','extension','size']) &&
  id(asset.id) && asset.family===`LumeImportedFont_${asset.id}` &&
  typeof asset.label==='string' && asset.label.length>0 && new TextEncoder().encode(asset.label).length<=512 &&
  !/[\u0000-\u001f\u007f-\u009f\ud800-\udfff]/u.test(asset.label) &&
  ['woff','woff2','ttf','otf'].includes(asset.extension) &&
  count(asset.size) && asset.size>0 && asset.size<=50*1024*1024;
export function validMusicFontAssetMessage(message) {
  return exact(message,['version','type','request','error','assets','candidate']) &&
    message.version===1 && message.type==='music-font-assets-state' && count(message.request) &&
    ['','load','invalid','save','cancelled'].includes(message.error) &&
    Array.isArray(message.assets) && message.assets.length<=8 && message.assets.every(validMusicFontAsset) &&
    new Set(message.assets.map(asset=>asset.id)).size===message.assets.length &&
    message.assets.reduce((sum,asset)=>sum+asset.size,0)<=200*1024*1024 &&
    (message.candidate===null || validMusicFontAsset(message.candidate) && !message.error &&
      message.assets.some(asset=>Object.keys(asset).every(key=>asset[key]===message.candidate[key])));
}
export const validMusicFontAssetCommand=(action,fields)=>action==='snapshot'?exact(fields,[]):
  ['choose','drop','cancel'].includes(action) && exact(fields,action==='drop'?['request','id']:['request']) &&
  count(fields.request) && fields.request>0 && (action!=='drop'||id(fields.id));

export function createMusicFontAssetQuery(bridge,{onAssets=()=>{},timeout=120000,now=Date.now}={}) {
  let sequence=0,loaded=false,loading=null,pending=null,disposed=false;
  const send=(action,fields,signal)=>new Promise((resolve,reject)=>{
    if(disposed || signal?.aborted){reject(new Error('cancelled'));return;}
    const request=action==='snapshot'?0:Math.max(sequence,now())+1;
    if(!Number.isSafeInteger(request)){reject(new Error('invalid'));return;}
    sequence=Math.max(sequence,request);
    let done=false,unsubscribe=()=>{},timer=null;
    const finish=(error,value)=>{
      if(done)return;
      done=true;unsubscribe();clearTimeout(timer);signal?.removeEventListener('abort',cancel);
      if(pending===cancel)pending=null;
      if(error)reject(new Error(error));else resolve(value);
    };
    const stop=error=>{
      if(done)return;
      finish(error);
      if(action==='choose')try{bridge.musicFontAssets('cancel',{request});}catch{}
    };
    const cancel=()=>stop('cancelled');
    pending?.();pending=cancel;
    try {
      unsubscribe=bridge.subscribe(message=>{
        if(!validMusicFontAssetMessage(message)||message.request!==request)return;
        if(!message.error){onAssets(message.assets);loaded=true;}
        finish(action==='choose' && message.error==='cancelled'?null:message.error||null,message);
      });
      signal?.addEventListener('abort',cancel,{once:true});
      timer=setTimeout(()=>stop('load'),timeout);
      if(!bridge.musicFontAssets(action,action==='snapshot'?{}:{request,...fields}))finish('load');
    } catch {finish('load');}
  });
  const load=()=>{
    if(disposed)return Promise.reject(new Error('cancelled'));
    if(loaded)return Promise.resolve();
    if(!loading)loading=send('snapshot',{}).finally(()=>{loading=null;});
    return loading;
  };
  return {
    load,
    get loaded(){return loaded;},
    async choose({signal}={}) {
      await load();
      return (await send('choose',{},signal)).candidate;
    },
    async drop(assetId) {await load();return send('drop',{id:assetId});},
    dispose(){disposed=true;pending?.();pending=null;},
  };
}
