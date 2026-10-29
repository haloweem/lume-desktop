// Accept the retired mode in saved settings so its removal cannot invalidate
// the entire preference record; the visible mode resolver maps it to default.
const MODES=['partita','fume','sonnet','gallery','3d','lumiere','motif','cadenza','tilt','cappella','diorama','pendolo','classic','tempera'];
const EFFECTS=['none','rain-window','snow','star-river'];
const KEYS=['version','mode','fontFamily','animationIntensity','lyricsFontColor','lyricsKaraokeFillColor'];
export const SONNET_COLOR_KEYS=Object.freeze(['lyricsFontColor','karaokeFillColor','glowColor']);
const exact=(value,keys)=>value!==null&&typeof value==='object'&&!Array.isArray(value)&&
  Object.keys(value).length===keys.length&&keys.every(key=>Object.hasOwn(value,key));
const color=value=>typeof value==='string'&&/^#[0-9a-f]{6}$/.test(value);
export const DEFAULT_SONNET_COLORS=Object.freeze({
  lyricsFontColor:'#ffffff',karaokeFillColor:'#38bdf8',glowColor:'#60a5fa'});
export const DEFAULT_MUSIC_PREFERENCES=Object.freeze({version:3,mode:'partita',fontFamily:'',
  animationIntensity:'normal',lyricsFontColor:'#ffffff',lyricsKaraokeFillColor:'#ffffff',
  sonnetColors:DEFAULT_SONNET_COLORS,effectMode:'none'});

function fontBytes(value) {
  let bytes=0;
  for(let i=0;i<value.length;i++) {
    const code=value.charCodeAt(i);
    if(code>=0xd800&&code<=0xdbff) {
      const low=value.charCodeAt(++i);
      if(!(low>=0xdc00&&low<=0xdfff))return Infinity;
      bytes+=4;
    } else if(code>=0xdc00&&code<=0xdfff)return Infinity;
    else bytes+=code<=0x7f?1:code<=0x7ff?2:3;
  }
  return bytes;
}

// Shared value contract only: no UI, bridge, storage or renderer dependencies.
export function validMusicPreferences(value) {
  if(![1,2,3].includes(value?.version) ||
    !exact(value,value.version===1?KEYS:[...KEYS,'sonnetColors',...(value.version===3?['effectMode']:[])]))return false;
  return MODES.includes(value.mode) && typeof value.fontFamily==='string' &&
    fontBytes(value.fontFamily)<=128 && !/[\u0000-\u001f\u007f-\u009f]/u.test(value.fontFamily) &&
    ['calm','normal','chaotic'].includes(value.animationIntensity) &&
    color(value.lyricsFontColor) && color(value.lyricsKaraokeFillColor) &&
    (value.version===1 || exact(value.sonnetColors,SONNET_COLOR_KEYS)&&SONNET_COLOR_KEYS.every(key=>color(value.sonnetColors[key]))) &&
    (value.version!==3 || EFFECTS.includes(value.effectMode));
}

export function sameMusicPreferences(a,b) {
  return !!a&&!!b&&KEYS.every(key=>a[key]===b[key])&&a.effectMode===b.effectMode&&
    SONNET_COLOR_KEYS.every(key=>a.sonnetColors?.[key]===b.sonnetColors?.[key]);
}

export function musicPreferenceSnapshot(previous,value) {
  if(value===undefined)return previous;
  if(!validMusicPreferences(value))throw new TypeError('Invalid music preferences');
  const normalized=value.version===1?{...value,version:3,sonnetColors:DEFAULT_SONNET_COLORS,effectMode:'none'}:
    value.version===2?{...value,version:3,effectMode:'none'}:value;
  if(sameMusicPreferences(previous,normalized))return previous;
  return Object.freeze({...normalized,sonnetColors:Object.freeze({...normalized.sonnetColors})});
}
