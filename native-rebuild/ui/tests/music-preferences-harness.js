// Install after the production-page bridge fixture. No real settings or storage.
export function installMusicPreferencesHarness() {
  const defaults={version:3,mode:'partita',fontFamily:'',animationIntensity:'normal',effectMode:'none',
    lyricsFontColor:'#ffffff',lyricsKaraokeFillColor:'#ffffff',
    sonnetColors:{lyricsFontColor:'#ffffff',karaokeFillColor:'#38bdf8',glowColor:'#60a5fa'}};
  let preferences=structuredClone(defaults),revision=0;
  const webview=window.chrome.webview,post=webview.postMessage;
  webview.postMessage=command=>{
    post(command);
    if(command?.scope!=='music-preferences')return;
    const request=command.request||0;
    let error='';
    if(command.action==='apply'||command.action==='reset') {
      if(command.revision!==revision)error='conflict';
      else {preferences=structuredClone(command.action==='reset'?defaults:command.preferences);revision++;}
    }
    const reply={version:1,type:'music-preferences-state',revision,request,
      status:error?'error':'ready',error,preferences};
    queueMicrotask(()=>window.harness.receive(reply));
  };
}
