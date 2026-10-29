import {DEFAULT_MUSIC_PREFERENCES,validMusicPreferences} from './music-preference-values.js';

const FAMILY=/^LumeImportedFont_[a-f0-9]{32}$/;
const MAX_BYTES=50*1024*1024;
const abortError=()=>new DOMException('Font operation cancelled','AbortError');
export const isMusicImportedFontFamily=family=>typeof family==='string'&&FAMILY.test(family);
export const validMusicFontLabel=label=>typeof label==='string'&&label.trim().length>0&&
  !/[\\/:]/.test(label)&&validMusicPreferences({...DEFAULT_MUSIC_PREFERENCES,fontFamily:label});

// Native metadata only: {id:<32 lowercase hex>,family:"LumeImportedFont_<id>",extension,size,label}.
// URLs are derived, never accepted from messages. No preferences or native transport live here.
export function createMusicFontAssets({fetch:fetchFont=(...args)=>globalThis.fetch(...args)}={}) {
  let assets=new Map(),disposed=false;
  const binaries=new Map(),documents=new Map();

  function discardBinary(id,entry) {
    entry.controller.abort();
    if(binaries.get(id)===entry)binaries.delete(id);
  }

  function binary(asset,signal,decode) {
    let entry=binaries.get(asset.id);
    if(!entry) {
      while(binaries.size>=2) {
        const [id,old]=binaries.entries().next().value;
        discardBinary(id,old);
      }
      entry={controller:new AbortController(),users:0};
      binaries.set(asset.id,entry);
      entry.promise=(async()=>{
        const response=await fetchFont(`https://lume-fonts.local/${asset.id}.${asset.extension}`,
          {signal:entry.controller.signal,credentials:'omit',redirect:'error'});
        if(!response.ok)throw new Error('Font asset unavailable');
        const length=response.headers?.get('content-length');
        if(length!==null && length!==undefined && Number(length)!==asset.size)
          throw new Error('Font asset size mismatch');
        const reader=response.body?.getReader();
        if(!reader)throw new Error('Font asset stream unavailable');
        const bytes=new Uint8Array(asset.size);
        const cancelRead=()=>{void reader.cancel().catch(()=>{});};
        entry.controller.signal.addEventListener('abort',cancelRead,{once:true});
        let offset=0,complete=false;
        try {
          for(;;) {
            const {value,done}=await reader.read();
            if(entry.controller.signal.aborted)throw abortError();
            if(done){complete=true;break;}
            if(value.byteLength>bytes.length-offset)throw new Error('Font asset size mismatch');
            bytes.set(value,offset);offset+=value.byteLength;
          }
          if(offset!==asset.size)throw new Error('Font asset size mismatch');
          return bytes.buffer;
        } finally {
          entry.controller.signal.removeEventListener('abort',cancelRead);
          if(!complete)await reader.cancel().catch(()=>{});
          reader.releaseLock();
        }
      })().catch(error=>{discardBinary(asset.id,entry);throw error;});
    } else {
      binaries.delete(asset.id);binaries.set(asset.id,entry);
    }
    entry.users++;
    return new Promise((resolve,reject)=>{
      let done=false;
      const finish=(error,bytes)=>{
        if(done)return;
        done=true;signal.removeEventListener('abort',cancel);
        // Bytes exist only while concurrent decoders use them, never across idle views.
        if(--entry.users===0)discardBinary(asset.id,entry);
        if(error)reject(error);else resolve(bytes);
      };
      const cancel=()=>finish(abortError());
      signal.addEventListener('abort',cancel,{once:true});
      if(signal.aborted)cancel();
      entry.promise.then(bytes=>{
        if(done)throw abortError();
        return decode(bytes,entry.controller.signal);
      }).then(()=>finish(),error=>finish(error));
    });
  }

  function remove(document,family,entry) {
    entry.controller.abort();
    if(entry.face)document.fonts.delete(entry.face);
    const cache=documents.get(document);
    if(cache?.get(family)===entry)cache.delete(family);
    if(!cache?.size)documents.delete(document);
  }

  // The signal owns a lease, including after resolution. Abort releases only that caller's
  // lease; the face is removed when its last lease ends. Keep the old lease until the new
  // family is ready. Call releaseDocument/dispose for unconditional teardown.
  function ensureFont(document,family,{signal}={}) {
    if(disposed || signal?.aborted)return Promise.reject(abortError());
    if(!isMusicImportedFontFamily(family))return Promise.resolve();
    const asset=assets.get(family);
    if(!asset)return Promise.reject(new Error('Imported font unavailable'));
    let cache=documents.get(document);
    if(!cache){cache=new Map();documents.set(document,cache);}
    let entry=cache.get(family);
    if(!entry) {
      entry={controller:new AbortController(),leases:new Set(),face:null};
      cache.set(family,entry);
      entry.promise=binary(asset,entry.controller.signal,async(bytes,binarySignal)=>{
        if(entry.controller.signal.aborted || binarySignal.aborted)throw abortError();
        const face=new document.defaultView.FontFace(family,bytes);
        await face.load();
        if(entry.controller.signal.aborted || binarySignal.aborted)throw abortError();
        document.fonts.add(face);entry.face=face;
      }).catch(error=>{remove(document,family,entry);throw error;});
    }
    return new Promise((resolve,reject)=>{
      let settled=false,released=false;
      const release=()=>{
        if(released)return;
        released=true;signal?.removeEventListener('abort',release);
        entry.leases.delete(release);
        if(!entry.leases.size)remove(document,family,entry);
        if(!settled){settled=true;reject(abortError());}
      };
      entry.leases.add(release);
      signal?.addEventListener('abort',release,{once:true});
      entry.promise.then(()=>{
        if(!settled){settled=true;resolve();}
      },error=>{
        if(!settled){settled=true;reject(error);}
        release();
      });
    });
  }

  function releaseDocument(document) {
    const cache=documents.get(document);
    if(cache)for(const [family,entry] of [...cache]) {
      for(const release of [...entry.leases])release();
      remove(document,family,entry);
    }
  }

  return {
    setAssets(next) {
      if(disposed)return;
      if(!Array.isArray(next)||next.length>8)throw new TypeError('Invalid font assets');
      const replacement=new Map();
      let total=0;
      for(const asset of next) {
        if(!asset || typeof asset.id!=='string' || !/^[a-f0-9]{32}$/.test(asset.id) ||
          asset.family!==`LumeImportedFont_${asset.id}` ||
          !['woff','woff2','ttf','otf'].includes(asset.extension) ||
          !Number.isSafeInteger(asset.size)||asset.size<=0||asset.size>MAX_BYTES ||
          replacement.has(asset.family))throw new TypeError('Invalid font asset');
        total+=asset.size;
        if(total>200*1024*1024)throw new TypeError('Font asset storage limit');
        const old=assets.get(asset.family);
        if(old && (old.extension!==asset.extension || old.size!==asset.size))
          throw new TypeError('Font assets must be immutable');
        replacement.set(asset.family,{id:asset.id,family:asset.family,extension:asset.extension,size:asset.size,
          ...(validMusicFontLabel(asset.label)?{label:asset.label}:{})});
      }
      assets=replacement;
      for(const [id,entry] of binaries)if(![...assets.values()].some(asset=>asset.id===id))discardBinary(id,entry);
    },
    // Display metadata stays in this one map. Missing/invalid labels fall back at the UI.
    labelFor:family=>assets.get(family)?.label,
    listAssets:()=>[...assets.values()].map(asset=>({...asset})),
    ensureFont,releaseDocument,
    dispose() {
      if(disposed)return;
      disposed=true;
      for(const document of [...documents.keys()])releaseDocument(document);
      for(const [id,entry] of binaries)discardBinary(id,entry);
      assets.clear();
    },
  };
}

// Shared readiness gate for the parent stage and each reference iframe. It changes only
// the effective family while pending; callers keep publishing the latest playback state.
export function createMusicFontGate(document,{ensureFont,onReady=()=>{},onError=()=>{}}={}) {
  let requested,applied='',pending=null,active=null,disposed=false;
  let previous,previousFamily,output;
  function reset() {
    pending?.abort();active?.abort();pending=active=null;requested=undefined;applied='';
  }
  function preferences(next,{visible=true}={}) {
    const family=next?.fontFamily||'';
    if(!visible)reset();
    if(!disposed && visible && family!==requested) {
      requested=family;pending?.abort();pending=null;
      if(family!==applied) {
        if(!isMusicImportedFontFamily(family)) {
          applied=family;active?.abort();active=null;
        } else {
          const controller=new AbortController();
          pending=controller;
          let loading;
          try {
            if(!ensureFont)throw new Error('Imported font loader unavailable');
            loading=ensureFont(document,family,{signal:controller.signal});
          } catch(error) {loading=Promise.reject(error);}
          Promise.resolve(loading).then(()=>{
            if(disposed || controller.signal.aborted || pending!==controller)return;
            const old=active;
            applied=family;active=controller;pending=null;old?.abort();
            onReady();
          },error=>{
            if(disposed || controller.signal.aborted || pending!==controller)return;
            pending=null;controller.abort();onError(error);
          });
        }
      }
    }
    if(next===previous && applied===previousFamily)return output;
    previous=next;previousFamily=applied;
    output=family===applied?next:{...next,fontFamily:applied};
    return output;
  }
  return {preferences,dispose(){
    disposed=true;reset();previous=output=null;
  }};
}
