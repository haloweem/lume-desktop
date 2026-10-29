import {MUSIC_PROVIDERS} from './music-provider-account.js';

const exact=(value,required,optional=[])=>value && typeof value==='object' && !Array.isArray(value) &&
  required.every(key=>Object.hasOwn(value,key)) && Object.keys(value).every(key=>required.includes(key)||optional.includes(key));
const text=(value,max=1024)=>typeof value==='string' && value.length<=max && !/[\u0000-\u001f\u007f]/.test(value);
const id=value=>text(value,128) && /^[a-zA-Z0-9_.:-]+$/.test(value);
const reads=['home','explore','playlist','search','favorites','recent'];
const writes=['like','createPlaylist','addToPlaylist'];
const errors=['','network','service','expired','unsupported','unavailable','invalid-response','storage','signed-out','busy','not-found'];
const membershipBadges=['qq-green-vip','qq-super-vip','netease-vip','netease-svip','kugou-vip'];
const unsafeTrackIds=new Set(['__proto__','constructor','prototype']);
const connected=account=>account?.status==='signed-in'||!!account?.userId&&['loading','error'].includes(account.status);
const trackProvider=id=>typeof id==='string'?id.split(':')[0]:'';
export const musicTrackLiked=(library,id)=>!!(library.liked?.[id]??library.favorites?.some(track=>track.id===id));
export function canLikeMusicTrack(library,id) {
  const provider=trackProvider(id);
  const known=!library.likedReady||library.likedReady[provider]||Object.hasOwn(library.liked||{},id)||
    library.favorites?.some(track=>track.id===id);
  return !!known&&MUSIC_PROVIDERS.includes(provider)&&!library.busy&&(Array.isArray(library.accounts)
    ? library.accounts.some(account=>account.provider===provider&&connected(account))
    : !!library.signedIn&&(!library.providerId||library.providerId===provider));
}
const normalizeTrackId=(provider,value)=>{
  if(!id(value)||!MUSIC_PROVIDERS.includes(provider))return '';
  const prefix=`${provider}:`;
  if(value.startsWith(prefix)) {
    const raw=value.slice(prefix.length);
    return raw&&!unsafeTrackIds.has(raw)?value:'';
  }
  if(MUSIC_PROVIDERS.some(other=>value.startsWith(`${other}:`)))return '';
  if(unsafeTrackIds.has(value))return '';
  return `${prefix}${value}`;
};
const normalizeTrack=(provider,value)=>{
  const normalized=normalizeTrackId(provider,value?.id);
  if(!normalized)return null;
  const source=value?.source;
  const sourceId=typeof source==='string'&&source.startsWith('online:')?normalizeTrackId(provider,source.slice(7)):'';
  return sourceId===normalized?{...value,id:normalized,source:`online:${normalized}`}:null;
};
const track=(value,provider)=>{
  const normalized=normalizeTrack(provider,value);
  return exact(value,['id','title','artist','album','source','artwork','duration'],['vipRequired','membershipBadge']) &&
  !!normalized && ['title','artist','album'].every(key=>text(value[key])) &&
  text(value.artwork,2048) && (value.vipRequired===undefined||typeof value.vipRequired==='boolean') &&
  (value.membershipBadge===undefined||membershipBadges.includes(value.membershipBadge)) &&
  Number.isFinite(value.duration) && value.duration>=0 && value.duration<=86400;
};
const collection=value=>exact(value,['id','providerId','type','title','artwork','description','owned'],['isLiked']) &&
  id(value.id) && MUSIC_PROVIDERS.includes(value.providerId) && ['playlist','album'].includes(value.type) &&
  text(value.title) && text(value.artwork,2048) && text(value.description,2048) && typeof value.owned==='boolean' &&
  (value.isLiked===undefined||typeof value.isLiked==='boolean'&&value.providerId==='qq'&&value.owned);

export function validMusicOnlineCommand(action,fields) {
  if(!fields || !MUSIC_PROVIDERS.includes(fields.provider) ||
    !Number.isSafeInteger(fields.request)||fields.request<=0)return false;
  const base=['provider','request'];
  if(action==='likes')return exact(fields,base);
  if(reads.includes(action)) {
    const required=action==='playlist'?['id']:action==='search'?['query']:[];
    return exact(fields,[...base,...required],['offset','limit']) &&
      (!required.includes('id')||id(fields.id)) &&
      (!required.includes('query')||text(fields.query,512)&&fields.query.trim().length>0) &&
      (fields.offset===undefined||Number.isSafeInteger(fields.offset)&&fields.offset>=0&&fields.offset<=100000) &&
      (fields.limit===undefined||Number.isSafeInteger(fields.limit)&&fields.limit>0&&fields.limit<=100);
  }
  if(['play','enqueue','insertNext'].includes(action))return exact(fields,[...base,'id'])&&id(fields.id);
  if(action==='like')return exact(fields,[...base,'id','value'])&&id(fields.id)&&typeof fields.value==='boolean';
  if(action==='createPlaylist')return exact(fields,[...base,'title'])&&text(fields.title,120)&&!!fields.title.trim();
  if(action==='addToPlaylist')return exact(fields,[...base,'id','trackId'])&&id(fields.id)&&id(fields.trackId);
  return action==='cancel'&&exact(fields,base);
}

export function validMusicOnlineMessage(message) {
  return exact(message,['version','type','request','provider','action','status','error','data']) &&
    message.version===1 && message.type==='music-online-state' && Number.isSafeInteger(message.request)&&message.request>=0 &&
    MUSIC_PROVIDERS.includes(message.provider) &&
    [...reads,...writes,'','likes','play','enqueue','insertNext','cancel'].includes(message.action) &&
    ['idle','loading','ready','error'].includes(message.status)&&errors.includes(message.error)&&
    exact(message.data,['tracks','playlists','recommendations','more'],['liked','likedIds'])&&
    Array.isArray(message.data.tracks)&&message.data.tracks.length<=512&&
    message.data.tracks.every(value=>track(value,message.provider))&&
    ['playlists','recommendations'].every(key=>Array.isArray(message.data[key])&&message.data[key].length<=1000&&
      message.data[key].every(collection))&&typeof message.data.more==='boolean' &&
    (message.data.liked===undefined||typeof message.data.liked==='boolean') &&
    (message.data.likedIds===undefined||Array.isArray(message.data.likedIds)&&
      message.data.likedIds.length<=10000&&message.data.likedIds.every(value=>
        !!normalizeTrackId(message.provider,value)));
}

export const musicOnlineError=error=>({
  network:'网络连接失败，请重试',service:'音乐平台服务暂不可用，请重试',expired:'登录已过期，请重新扫码',
  unsupported:'该平台暂不支持此操作',unavailable:'该歌曲暂不可播放，请检查账号权限',
  'invalid-response':'音乐平台返回的数据无法读取',storage:'音乐数据保存失败',
  'signed-out':'请先扫码登录音乐平台',busy:'上一项操作尚未完成','not-found':'歌曲列表已更新，请重新打开列表',
})[error]||'';

// Only public list state is retained. The native owner authorizes IDs and resolves media.
// Public browse pages are cached per signed-in account. A cached page is rendered
// immediately; a stale page is refreshed silently so the visible list never blanks.
const cacheTtl=5*60*1000,cacheLimit=8,cacheStoragePrefix='lume.music.online.v1.';
export function createMusicOnline({send,onChange,onError,now=Date.now,cache=false}={}) {
  let sequence=0,pending=null,active=false,disposed=false,accounts=[],identity='netease:',last=null;
  let likedAttempts=new Set();
  let provider='netease',library=empty();
  const readCache=new Map();
  let cacheOwner='',lastSentRead='',deferredPlay=null;
  function empty(){return {signedIn:false,providerId:provider,tracks:[],playlists:[],recommendations:[],
    favorites:[],recent:[],searchTracks:[],more:false,loading:false,appending:false,busy:false,error:'',offset:0,
    liked:{},likedReady:{}};}
  const emit=()=>onChange?.(snapshot());
  const snapshot=()=>({...library,accounts:accounts.map(value=>({...value}))});
  let api=null;
  const accountFor=(targetProvider=provider)=>accounts.find(value=>value.provider===targetProvider&&connected(value));
  const cacheKey=(action,fields,targetProvider)=>`${targetProvider}|${action}|${fields.id||''}|${fields.query||''}`;
  const storageKey=(targetProvider,userId)=>`${cacheStoragePrefix}${targetProvider}.${userId}`;
  function loadReadCache(targetProvider=provider){
    if(!cache)return;
    const account=accountFor(targetProvider);
    const owner=account?storageKey(targetProvider,account.userId):'';
    if(cacheOwner===owner)return;
    readCache.clear();cacheOwner=owner;
    if(!owner)return;
    try{
      const storage=globalThis.localStorage;
      if(!storage)return;
      const saved=JSON.parse(storage.getItem(owner)||'null');
      if(!saved||saved.version!==1||saved.provider!==targetProvider||saved.userId!==account.userId||
        !Array.isArray(saved.entries))return;
      for(const entry of saved.entries.slice(-cacheLimit)){
        if(!entry||!['home','explore','playlist','favorites','recent','search'].includes(entry.action)||!entry.data)continue;
        const fields={offset:0,limit:50,...(entry.fields||{})};
        if(!validMusicOnlineCommand(entry.action,{...fields,provider:targetProvider,request:1}))continue;
        const message={version:1,type:'music-online-state',request:1,provider:targetProvider,
          action:entry.action,status:'ready',error:'',data:entry.data};
        if(!validMusicOnlineMessage(message))continue;
        readCache.set(cacheKey(entry.action,fields,targetProvider),
          {data:entry.data,at:Number(entry.at)||0,action:entry.action,fields,provider:targetProvider,
            userId:account.userId??'',persisted:true});
      }
    }catch{}
  }
  function saveReadCache(){
    if(!cache)return;
    const account=accountFor();
    if(!account)return;
    try{
      const storage=globalThis.localStorage;
      if(!storage)return;
      const entries=[...readCache.values()].filter(entry=>entry.provider===provider&&entry.userId===account.userId)
        .slice(-cacheLimit).map(entry=>({action:entry.action,fields:entry.fields,at:entry.at,data:entry.data}));
      storage.setItem(storageKey(provider,account.userId),
        JSON.stringify({version:1,provider,userId:account.userId,entries}));
    }catch{}
  }
  function serveReadCache(entry,action,fields,targetProvider){
    if(!entry||!cache)return false;
    const request=Math.max(sequence,now())+1;
    sequence=request;
    last={action,fields:{...entry.fields,offset:0,limit:entry.fields.limit||50},resume:null,append:false};
    pending={request,action,fields:last.fields,resume:null,append:false,background:false,silent:false,
      provider:targetProvider,userId:accountFor(targetProvider)?.userId};
    const data={...entry.data};
    if(library.likedReady[targetProvider])delete data.likedIds;
    const message={version:1,type:'music-online-state',request,provider:targetProvider,action,
      status:'ready',error:'',data};
    if(!validMusicOnlineMessage(message)){pending=null;return false;}
    const applied=api.receive(message);
    if(!applied)pending=null;
    return applied;
  }
  function warmLikes() {
    if(disposed||!active||pending)return;
    const account=accounts.find(value=>connected(value)&&!library.likedReady[value.provider]&&
      !likedAttempts.has(value.provider));
    if(account)post('likes',{},null,false,account.provider,true);
  }
  function post(action,fields={},resume=null,append=false,targetProvider=provider,background=false,silent=false) {
    const cancellingRead=action==='cancel'&&!!pending&&reads.includes(pending.action);
    if(pending&&reads.includes(pending.action)&&!reads.includes(action)&&action!=='cancel')lastSentRead='';
    if(action==='cancel'){deferredPlay=null;if(cancellingRead)lastSentRead='';}
    else if(deferredPlay&&!['play','enqueue','insertNext'].includes(action))deferredPlay=null;
    const request=Math.max(sequence,now())+1;
    if(!validMusicOnlineCommand(action,{...fields,provider:targetProvider,request}))return false;
    sequence=request;
    const operation=action==='cancel'?null:{request,action,fields,resume,append,background,silent,provider:targetProvider,
      userId:accounts.find(account=>account.provider===targetProvider&&connected(account))?.userId};
    pending=operation;
    if(operation&&!background&&!silent) {
      library={...library,loading:true,busy:writes.includes(action),error:''};
      emit();
      if(pending!==operation||disposed||!active)return false;
    }
    try {if(send?.(action,{...fields,provider:targetProvider,request})===true)return true;}catch{}
    if(operation&&pending===operation) {
      pending=null;
      if(background){likedAttempts.add(targetProvider);warmLikes();}
      else if(silent){library={...library,loading:false,busy:false};}
      else {library={...library,loading:false,busy:false,error:'service'};emit();onError?.('service');}
    }
    return false;
  }
  function request(action,fields={},resume=null,append=false,force=false) {
    if(disposed||!active)return false;
    const targetProvider=action==='like'?trackProvider(fields.id):provider;
    if(action==='like'?!canLikeMusicTrack(snapshot(),fields.id):!library.signedIn)return false;
    if(pending&&writes.includes(pending.action))return false;
    if(action==='cancel'||!validMusicOnlineCommand(action,{...fields,provider:targetProvider,request:1}))return false;
    if(['play','enqueue','insertNext'].includes(action)&&pending?.silent) {
      // A cached page is visible while its silent refresh re-registers tracks.
      // Hold playback until that refresh lands so the native registry can
      // resolve the clicked track instead of answering not-found.
      deferredPlay={action,fields};
      return true;
    }
    if(reads.includes(action)) {
      const pageFields={...fields,offset:fields.offset||0,limit:50};
      if(cache&&!force&&!append&&pageFields.offset===0&&!resume) {
        const key=cacheKey(action,pageFields,targetProvider);
        const entry=readCache.get(key);
        if(entry&&serveReadCache(entry,action,pageFields,targetProvider)) {
          // A disk-restored, stale, or different-view page still refreshes
          // silently. This re-registers tracks and makes the native displayed
          // queue match the page without blanking it.
          if(entry.persisted||now()-entry.at>=cacheTtl||lastSentRead!==key) {
            if(post(action,entry.fields,null,false,targetProvider,false,true))lastSentRead=key;
          }
          return true;
        }
      }
      last={action,fields:pageFields,resume,append};
      library={...library,appending:append,...(append?{}:{more:false,offset:last.fields.offset,
        ...(action==='search'?{searchTracks:[]}:{tracks:[]})})};
      const sent=post(action,last.fields,resume,append);
      if(sent)lastSentRead=cacheKey(action,pageFields,targetProvider);
      return sent;
    }
    return post(action,fields,null,false,targetProvider);
  }
  function flushDeferredPlay(){
    if(!deferredPlay)return;
    const play=deferredPlay;deferredPlay=null;
    request(play.action,play.fields);
  }
  function sync(nextProvider=provider) {
    const account=accounts.find(value=>value.provider===nextProvider&&connected(value));
    const key=`${nextProvider}:${account?.userId||''}`;
    if(key===identity){emit();return;}
    identity=key;last=null;
    post('cancel');
    provider=nextProvider;
    library={...empty(),signedIn:!!account,liked:library.liked,likedReady:library.likedReady};
    lastSentRead='';
    if(cache)loadReadCache(nextProvider);
    emit();
    if(active&&account)request('home');
  }
  api = {
    snapshot,request,
    accounts(value) {
      if(disposed)return;
      const previousAccounts=accounts;
      const next=(value.accounts||[]).map(account=>({...account}));
      const retained=new Set(next.filter(account=>connected(account)&&accounts.some(previous=>
        previous.provider===account.provider&&connected(previous)&&previous.userId===account.userId))
        .map(account=>account.provider));
      library={...library,liked:Object.fromEntries(Object.entries(library.liked)
        .filter(([id])=>retained.has(trackProvider(id)))),
        likedReady:Object.fromEntries(Object.entries(library.likedReady).filter(([key])=>retained.has(key)))};
      likedAttempts=new Set([...likedAttempts].filter(key=>retained.has(key)));
      accounts=next;
      const selected=accounts.some(account=>account.provider===provider&&connected(account))
        ? provider : accounts.find(connected)?.provider||provider;
      const account=accounts.find(account=>account.provider===selected&&connected(account));
      if(`${selected}:${account?.userId||''}`===identity&&pending&&!accounts.some(account=>
        account.provider===pending.provider&&connected(account)&&account.userId===pending.userId)) {
        post('cancel');library={...library,loading:false,busy:false};
      }
      sync(selected);
      // Only an actual signed-out/error -> signed-in transition may repair a
      // failed read, and only when that read was the home page. A search or
      // playlist keeps its query and result context across account snapshots.
      const previous=previousAccounts.find(account=>account.provider===provider);
      const current=accounts.find(account=>account.provider===provider);
      if(current?.status==='signed-in')lastSentRead='';
      const recovered=!!previous&&!!current&&previous.status!=='signed-in'&&current.status==='signed-in';
      if(recovered&&library.error&&!library.loading&&!library.appending&&
        (!last||last.action==='home'))request('home',{},null,false,true);
      warmLikes();
    },
    select(value) {
      if(disposed||!MUSIC_PROVIDERS.includes(value)||value===provider)return false;
      sync(value);return true;
    },
    setVisible(value) {
      if(disposed||active===(value===true))return;
      active=value===true;
      if(!active) {
        if(pending?.action==='createPlaylist')
          last={action:'home',fields:{offset:0,limit:50},resume:last?.action==='home'?null:last};
        post('cancel');library={...library,loading:false,busy:false};emit();
      }
      else if(library.signedIn)request(last?.action||'home',last?.fields||{},last?.resume,last?.append);
    },
    releasePageData() {
      if(disposed)return;
      if(active)api.setVisible(false);
      last=null;
      likedAttempts.clear();
      library={...empty(),signedIn:accounts.some(account=>account.provider===provider&&connected(account))};
      emit();
    },
    next() {return !library.loading&&!library.error&&last&&library.more&&
      request(last.action,{...last.fields,offset:library.offset+last.fields.limit},null,true);},
    previous() {return last&&library.offset>0&&request(last.action,{...last.fields,offset:Math.max(0,library.offset-50)});},
    retry() {return !library.loading&&last&&request(last.action,last.fields,last.resume,last.append,true);},
    refresh() {
      if(disposed||!active||!library.signedIn||pending&&writes.includes(pending.action))return false;
      likedAttempts.clear();library={...library,likedReady:{}};
      return request('home',{},last?.action==='home'?null:last,false,true);
    },
    receive(message) {
      if(disposed||!active||!pending||!validMusicOnlineMessage(message)||message.request!==pending.request||
        message.action!==pending.action||message.provider!==pending.provider)return false;
      if(message.status==='loading')return true;
      if(!['ready','error'].includes(message.status))return false;
      const operation=pending;pending=null;
      if(operation.background)likedAttempts.add(message.provider);
      if(message.status==='error'){
        if(operation.background){warmLikes();return true;}
        lastSentRead='';
        if(operation.silent){
          library={...library,loading:false,busy:false};
          if(deferredPlay){const error=message.error;deferredPlay=null;library={...library,error};emit();onError?.(error);return true;}
          emit();return true;
        }
        library={...library,loading:false,busy:false,error:message.error};emit();onError?.(message.error);return true;
      }
      if(!operation.background)library={...library,loading:false,busy:false,error:''};
      const data={...message.data,
        tracks:message.data.tracks.map(value=>normalizeTrack(message.provider,value)).filter(Boolean),
        likedIds:message.data.likedIds===undefined?undefined:
          message.data.likedIds.map(value=>normalizeTrackId(message.provider,value)).filter(Boolean)};
      if(data.likedIds) {
        library.liked={...Object.fromEntries(Object.entries(library.liked)
          .filter(([id])=>trackProvider(id)!==message.provider)),
          ...Object.fromEntries(data.likedIds.map(id=>[id,true]))};
        library.favorites=library.favorites.filter(track=>library.liked[track.id]);
        library.likedReady={...library.likedReady,[message.provider]:true};
      }
      if(operation.background){emit();warmLikes();return true;}
      if(reads.includes(message.action)) {
        const merge=(previous,next)=>operation.append
          ? [...new Map([...previous,...next].map(item=>[item.id,item])).values()] : next;
        const key=({favorites:'favorites',recent:'recent',search:'searchTracks'})[message.action]||'tracks';
        const tracks=merge(library[key],data.tracks);
        library={...library,tracks,[key]:tracks,more:data.more,offset:operation.fields.offset||0,appending:false};
        const likedCollection=message.action==='playlist'&&library.playlists.find(item=>
          item.providerId===message.provider&&item.id===operation.fields.id&&item.isLiked===true);
        if(message.action==='favorites'||likedCollection) {
          // A fetched built-in list is positive evidence, even if the optional full snapshot failed.
          library.liked={...library.liked,...Object.fromEntries(data.tracks.map(track=>[track.id,true]))};
          if(likedCollection)library.favorites=merge(library.favorites,data.tracks);
          const artwork=data.tracks[0]?.artwork;
          if(!operation.append&&artwork)library.playlists=library.playlists.map(item=>
            item.providerId===message.provider&&item.isLiked?{...item,artwork}:item);
        }
        if(message.action==='home')library={...library,playlists:merge(library.playlists,data.playlists),
          recommendations:merge(library.recommendations,data.recommendations)};
        if(message.action==='explore')library={...library,recommendations:merge(library.recommendations,data.recommendations)};
        if(cache&&!operation.append&&(operation.fields.offset||0)===0&&
          ['home','explore','playlist','favorites','recent','search'].includes(message.action)) {
          const key=cacheKey(message.action,operation.fields,message.provider);
          readCache.delete(key);
          readCache.set(key,{data,at:now(),action:message.action,
            fields:{...operation.fields,offset:0,limit:operation.fields.limit||50},
            provider:message.provider,userId:operation.userId||'',persisted:false});
          while(readCache.size>cacheLimit)readCache.delete(readCache.keys().next().value);
          saveReadCache();
        }
        if(operation.resume) {
          request(operation.resume.action,operation.resume.fields,operation.resume.resume,operation.resume.append);return true;
        }
      } else {
        if(message.action==='like') {
          const liked=message.data.liked??operation.fields.value;
          library={...library,liked:{...library.liked,[operation.fields.id]:liked},
            favorites:liked?library.favorites:library.favorites.filter(track=>track.id!==operation.fields.id)};
          emit();warmLikes();return true;
        }
        if(message.action==='createPlaylist') {
          request('home',{},last?.action==='home'&&!last.fields.offset?null:last,false,true);return true;
        }
        if(writes.includes(message.action)&&last) {
          request(last.action,last.fields,last.resume,last.append,true);return true;
        }
      }
      if(operation.silent)flushDeferredPlay();
      emit();warmLikes();return true;
    },
    dispose(){if(disposed)return;if(active||pending)post('cancel');disposed=true;active=false;pending=null;accounts=[];library=empty();readCache.clear();cacheOwner='';lastSentRead='';deferredPlay=null;},
  };
  return api;
}
