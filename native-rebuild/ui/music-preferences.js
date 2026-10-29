import {DEFAULT_MUSIC_PREFERENCES,validMusicPreferences,musicPreferenceSnapshot,
  sameMusicPreferences as same} from './music-preference-values.js';
export {DEFAULT_MUSIC_PREFERENCES,validMusicPreferences} from './music-preference-values.js';

const exact = (value, keys) => value !== null && typeof value === 'object' && !Array.isArray(value) &&
  Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value,key));
const revision = value => Number.isSafeInteger(value) && value >= 0;
export function validMusicPreferencesMessage(value) {
  return exact(value,['version','type','revision','request','status','error','preferences']) &&
    value.version===1 && value.type==='music-preferences-state' && revision(value.revision) &&
    revision(value.request) && ['ready','error'].includes(value.status) &&
    ['','invalid','save','conflict','load'].includes(value.error) &&
    (value.status==='ready' ? value.error==='' : value.error!=='') && validMusicPreferences(value.preferences);
}
export function validMusicPreferencesCommand(action,fields) {
  if(action==='snapshot')return exact(fields,[]);
  if(!['apply','reset'].includes(action) || !exact(fields,action==='apply'
    ? ['request','revision','preferences'] : ['request','revision']))return false;
  return revision(fields.request) && fields.request>0 && revision(fields.revision) &&
    (action==='reset' || validMusicPreferences(fields.preferences));
}

// Only committed snapshots enter the queue; later previews remain local.
export function createMusicPreferences({send,onChange,onError}={}) {
  let confirmed=DEFAULT_MUSIC_PREFERENCES,draft=confirmed,version=0,request=0,pending=null,loaded=false;
  let queued=null;
  const changed=()=>onChange?.(api.snapshot());
  function rollback(error) {
    pending=null;queued=null;draft=confirmed;changed();onError?.(error);
  }
  function transmit() {
    if(!loaded || pending || !queued)return false;
    const command=queued;
    queued=null;
    if(command.action==='apply' && same(command.preferences,confirmed)){changed();return true;}
    if(request>=Number.MAX_SAFE_INTEGER){rollback('save');return false;}
    const fields={request:++request,revision:version};
    pending={...command,...fields};
    if(command.action==='apply')fields.preferences={...command.preferences};
    try {
      if(send?.(command.action,fields)===true){changed();return true;}
    }catch{}
    rollback('save');return false;
  }
  function settle() {
    const editedAfterSend=!same(draft,pending.preferences);
    pending=null;
    if(!queued && !editedAfterSend)draft=confirmed;
    changed();if(queued)transmit();
  }
  const api={
    snapshot:()=>({preferences:draft,revision:version,loaded,pending:!!pending}),
    load(){try{return send?.('snapshot',{})===true;}catch{return false;}},
    preview(patch) {
      if(!patch || typeof patch!=='object' || Array.isArray(patch))return false;
      const next={...draft,...patch};
      if(Object.hasOwn(patch,'sonnetColors')) {
        if(!patch.sonnetColors || typeof patch.sonnetColors!=='object' || Array.isArray(patch.sonnetColors))return false;
        next.sonnetColors={...draft.sonnetColors,...patch.sonnetColors};
      }
      if(!loaded || !validMusicPreferences(next))return false;
      draft=musicPreferenceSnapshot(draft,next);changed();return true;
    },
    commit(patch={}) {
      if(!api.preview(patch))return false;
      queued={action:'apply',preferences:draft};
      return pending ? true : transmit();
    },
    reset() {
      if(!api.preview(DEFAULT_MUSIC_PREFERENCES))return false;
      queued={action:'reset',preferences:draft};
      return pending ? true : transmit();
    },
    receive(message) {
      if(!validMusicPreferencesMessage(message) || message.revision<version)return false;
      if(message.request!==0 && message.request!==pending?.request)return false;
      const editedSinceConfirmed=!same(draft,confirmed);
      confirmed=musicPreferenceSnapshot(confirmed,message.preferences);version=message.revision;loaded=true;
      if(message.request===pending?.request) {
        if(message.status==='error'){rollback(message.error);return true;}
        settle();
      } else if(pending && version>pending.revision) {
        // A newer snapshot retires the old request without waiting for a stale ACK.
        if(message.status==='error')rollback(message.error);
        else if(same(confirmed,pending.preferences))settle();
        else rollback('conflict');
      } else {
        if(!pending && !queued && !editedSinceConfirmed)draft=confirmed;
        changed();if(message.status==='error')onError?.(message.error);
      }
      return true;
    },
  };
  return api;
}
