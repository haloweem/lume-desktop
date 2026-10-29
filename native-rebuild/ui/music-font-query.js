import {DEFAULT_MUSIC_PREFERENCES,validMusicPreferences} from './music-preference-values.js';

const exact=(value,keys)=>value && typeof value==='object' && !Array.isArray(value) &&
  Object.keys(value).length===keys.length && keys.every(key=>Object.hasOwn(value,key));
const counter=value=>Number.isSafeInteger(value)&&value>0;
export const validMusicFontCommand=(action,fields)=>action==='list' &&
  exact(fields,['request']) && counter(fields.request);
export const validMusicFontMessage=value=>exact(value,['version','type','request','families','error']) &&
  value.version===1 && value.type==='music-fonts-state' && counter(value.request) &&
  ['','unavailable'].includes(value.error) && Array.isArray(value.families) && value.families.length<=4096 &&
  value.families.every(family=>typeof family==='string' && family.length>0 &&
    validMusicPreferences({...DEFAULT_MUSIC_PREFERENCES,fontFamily:family}));

// One request per picker, with no retained system-font list or page-wide subscription.
export function createMusicFontQuery(bridge,{timeout=10000}={}) {
  let request=0,pending=null;
  return ({signal}={})=>{
    pending?.();
    return new Promise((resolve,reject)=>{
      if(signal?.aborted || request===Number.MAX_SAFE_INTEGER) {
        reject(new Error('font-query-cancelled'));return;
      }
      const id=++request;
      let unsubscribe=()=>{},timer=null,done=false;
      const finish=(error,families)=>{
        if(done)return;
        done=true;unsubscribe();clearTimeout(timer);
        signal?.removeEventListener('abort',cancel);
        if(pending===cancel)pending=null;
        if(error)reject(new Error(error));else resolve(families.map(family=>({family})));
      };
      const cancel=()=>finish('font-query-cancelled');
      pending=cancel;
      unsubscribe=bridge.subscribe(message=>{
        if(validMusicFontMessage(message) && message.request===id)
          finish(message.error||null,message.families);
      });
      signal?.addEventListener('abort',cancel,{once:true});
      timer=setTimeout(()=>finish('font-query-timeout'),timeout);
      if(!bridge.musicFonts('list',{request:id}))finish('font-query-unavailable');
    });
  };
}
