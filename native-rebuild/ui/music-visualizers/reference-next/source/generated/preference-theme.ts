
import {DEFAULT_THEME} from 'H:/refactor/workspace/lume1.0.7/build-artifacts/folia-upstream/src/services/baseThemes.ts';
export {resolveThemeFontStack} from 'reference:fonts';
function build(mode,preferences) {
  const color=preferences?.lyricsFontColor||'#ffffff';
  return Object.freeze({...DEFAULT_THEME,
    animationIntensity:preferences?.animationIntensity??DEFAULT_THEME.animationIntensity,
    fontFamily:preferences?.fontFamily||undefined,
    // secondaryColor stays the upstream default: Cappella draws other senders'
    // bubbles from it under primary-coloured text, which a shared colour hides.
    primaryColor:color,accentColor:color,
    karaokeFillColor:preferences?.lyricsKaraokeFillColor||'#ffffff',
    ...(mode==='cappella'?cappellaBubbles(color):{})});
}
// Cappella paints other singers' bubbles between secondaryColor and accentColor
// under primary-coloured text. Upstream derives both from the cover; here they
// are a fixed grey ramp on the far side of the lyric colour, so every singer's
// line stays readable whatever lyric colour is chosen.
function cappellaBubbles(color) {
  const hex=/^#[0-9a-f]{6}$/i.test(color)?color:'#ffffff';
  const [r,g,b]=[1,3,5].map(i=>parseInt(hex.slice(i,i+2),16)/255).map(v=>v<=0.03928?v/12.92:((v+0.055)/1.055)**2.4);
  const light=0.2126*r+0.7152*g+0.0722*b>0.4;
  return light?{secondaryColor:'#27272a',accentColor:'#71717a'}:{secondaryColor:'#f4f4f5',accentColor:'#a1a1aa'};
}
export const themes=Object.freeze(Object.fromEntries(["cappella","diorama","pendolo","tempera"].map(mode=>[mode,build(mode)])));
export function resolvePreferenceTheme(mode,preferences) {return preferences?build(mode,preferences):themes[mode];}