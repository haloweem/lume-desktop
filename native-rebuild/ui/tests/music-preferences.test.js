import test from 'node:test';
import assert from 'node:assert/strict';
import {DEFAULT_MUSIC_PREFERENCES as defaults,createMusicPreferences,validMusicPreferences,
  validMusicPreferencesMessage,validMusicPreferencesCommand} from '../music-preferences.js';

const message=(revision,request,preferences=defaults,error='')=>({version:1,type:'music-preferences-state',
  revision,request,status:error?'error':'ready',error,preferences});
test('music preferences validate their independent bounded wire contract',()=>{
  assert.ok(validMusicPreferences(defaults));
  assert.ok(validMusicPreferences({...defaults,mode:'motif'}),'retired visualizer settings remain loadable for migration');
  for(const patch of [{version:4},{mode:'latent'},{fontFamily:'a'.repeat(129)},
    {fontFamily:'字'.repeat(43)},{fontFamily:'bad\nfont'},{lyricsFontColor:'red'},
    {lyricsKaraokeFillColor:'#FFFFFF'},{animationIntensity:'fast'},{source:'C:/media'}])
    assert.equal(validMusicPreferences({...defaults,...patch}),false);
  assert.ok(validMusicPreferences({...defaults,fontFamily:'Microsoft YaHei'}));
  assert.ok(validMusicPreferencesCommand('snapshot',{}));
  assert.ok(validMusicPreferencesCommand('apply',{request:1,revision:0,preferences:defaults}));
  assert.equal(validMusicPreferencesCommand('apply',{request:1,revision:0,preferences:defaults,session:1}),false);
  assert.equal(validMusicPreferencesMessage({...message(0,0),error:'save'}),false);
});

test('current preferences retain independent Sonnet colors and upgrade exact v1 snapshots',()=>{
  assert.equal(defaults.version,3);
  assert.deepEqual(defaults.sonnetColors,{lyricsFontColor:'#ffffff',karaokeFillColor:'#38bdf8',glowColor:'#60a5fa'});
  const {sonnetColors,effectMode,...legacy}=defaults;
  legacy.version=1;legacy.lyricsFontColor='#123456';
  const state=createMusicPreferences({send:()=>true});
  assert.equal(state.receive(message(4,0,legacy)),true);
  assert.deepEqual(state.snapshot().preferences,{...defaults,lyricsFontColor:'#123456'});
  for(const value of [null,[],{},'#ffffff',{...sonnetColors,extra:'#ffffff'},
    {...sonnetColors,glowColor:'red'},{...sonnetColors,karaokeFillColor:'#ABCDEF'}])
    assert.equal(validMusicPreferences({...defaults,sonnetColors:value}),false);
  assert.equal(validMusicPreferences({...legacy,sonnetColors}),false,'v1 never accepts unversioned extensions');
});

test('v3 effect modes are bounded, independently saved, rolled back and migrated from v2',()=>{
  assert.equal(defaults.effectMode,'none');
  for(const effectMode of ['none','rain-window','snow','star-river'])
    assert.equal(validMusicPreferences({...defaults,effectMode}),true);
  for(const effectMode of ['rain','stars','ripples','',null,[],{},42])
    assert.equal(validMusicPreferences({...defaults,effectMode}),false);
  const {effectMode,...legacy}=defaults;
  legacy.version=2;legacy.sonnetColors={...legacy.sonnetColors,glowColor:'#123abc'};
  const sent=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;}});
  assert.equal(state.receive(message(8,0,legacy)),true);
  assert.deepEqual(state.snapshot().preferences,{...legacy,version:3,effectMode:'none'});
  assert.equal(validMusicPreferences({...legacy,effectMode:'snow'}),false);
  assert.equal(validMusicPreferences({...legacy,version:3}),false);
  state.commit({effectMode:'snow'});
  assert.equal(sent[0][1].preferences.effectMode,'snow');
  assert.equal(sent[0][1].preferences.mode,defaults.mode);
  state.receive(message(8,1,legacy,'save'));
  assert.equal(state.snapshot().preferences.effectMode,'none');
  state.commit({effectMode:'rain-window'});
  state.receive(message(9,2,sent[1][1].preferences));
  assert.equal(state.snapshot().preferences.effectMode,'rain-window');
  state.reset();
  assert.equal(state.snapshot().preferences.effectMode,'none');
});

test('Sonnet preview, queued saves, acknowledgements and rollback own immutable nested snapshots',()=>{
  const sent=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;}});
  state.receive(message(0,0));
  const colors={...defaults.sonnetColors,karaokeFillColor:'#ff2200'};
  assert.equal(state.preview({sonnetColors:colors}),true);
  colors.karaokeFillColor='#000000';
  assert.equal(state.snapshot().preferences.sonnetColors.karaokeFillColor,'#ff2200');
  assert.equal(sent.length,0);
  assert.equal(state.commit(),true);
  assert.equal(state.commit({sonnetColors:{glowColor:'#22ff00'}}),true);
  assert.equal(state.preview({sonnetColors:{lyricsFontColor:'#2200ff'}}),true);
  state.receive(message(1,1,sent[0][1].preferences));
  assert.equal(sent.length,2);
  assert.deepEqual(sent[1][1].preferences.sonnetColors,{
    lyricsFontColor:'#ffffff',karaokeFillColor:'#ff2200',glowColor:'#22ff00'});
  assert.equal(sent[1][1].preferences.lyricsFontColor,defaults.lyricsFontColor);
  assert.equal(sent[1][1].preferences.lyricsKaraokeFillColor,defaults.lyricsKaraokeFillColor);
  state.receive(message(2,2,sent[1][1].preferences));
  assert.equal(state.snapshot().preferences.sonnetColors.lyricsFontColor,'#2200ff');
  assert.throws(()=>{state.snapshot().preferences.sonnetColors.glowColor='#000000';},TypeError);
  state.commit();
  state.receive(message(2,3,sent[1][1].preferences,'save'));
  assert.deepEqual(state.snapshot().preferences.sonnetColors,sent[1][1].preferences.sonnetColors);
  assert.equal(state.preview({sonnetColors:null}),false);
  assert.equal(state.preview({sonnetColors:{source:'C:/file'}}),false);
});
test('invalid preview patches leave the owned draft and save queue unchanged',()=>{
  const sent=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;}});
  state.receive(message(0,0));
  const before=state.snapshot().preferences;
  for(const patch of [null,undefined,[],false,42,'color']) {
    assert.equal(state.preview(patch),false);
    assert.equal(state.snapshot().preferences,before);
  }
  assert.equal(sent.length,0);
});
test('preview is local, commits coalesce and native acknowledgement owns the revision',()=>{
  const sent=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;}});
  assert.equal(state.commit({mode:'3d'}),false);
  state.load();assert.deepEqual(sent,[['snapshot',{}]]);
  state.receive(message(4,0));
  state.preview({lyricsFontColor:'#ff0000'});assert.equal(sent.length,1);
  state.commit();state.commit({fontFamily:'Arial'});state.commit({mode:'gallery'});
  assert.equal(sent.length,2);assert.equal(sent[1][1].revision,4);
  const first=sent[1][1];
  assert.equal(state.receive(message(5,99,first.preferences)),false);
  state.receive(message(5,first.request,first.preferences));
  assert.equal(sent.length,3);assert.equal(sent[2][1].revision,5);
  assert.equal(sent[2][1].preferences.fontFamily,'Arial');assert.equal(sent[2][1].preferences.mode,'gallery');
  state.receive(message(6,sent[2][1].request,sent[2][1].preferences));
  assert.equal(state.snapshot().pending,false);assert.equal(state.snapshot().preferences.mode,'gallery');
  assert.equal(state.receive(message(4,0)),false);
});
test('save failures restore confirmed values and reject late responses',()=>{
  const sent=[],errors=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;},onError:e=>errors.push(e)});
  state.receive(message(0,0));state.commit({mode:'3d'});state.commit({fontFamily:'Arial'});
  state.receive(message(0,1,defaults,'save'));
  assert.deepEqual(state.snapshot().preferences,defaults);assert.equal(state.snapshot().pending,false);
  assert.equal(sent.length,1);assert.deepEqual(errors,['save']);
  assert.equal(state.receive(message(1,1,{...defaults,mode:'3d'})),false);
  state.commit({mode:'gallery'});state.receive(message(1,2,{...defaults,mode:'gallery'}));
  state.reset();assert.equal(sent[2][0],'reset');assert.equal(sent[2][1].revision,1);
});
test('a prior save acknowledgement cannot discard a newer uncommitted color preview',()=>{
  const sent=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;}});
  state.receive(message(0,0));state.commit({fontFamily:'Arial'});
  state.preview({lyricsFontColor:'#ff0000'});
  state.receive(message(1,1,sent[0][1].preferences));
  assert.equal(state.snapshot().preferences.lyricsFontColor,'#ff0000');
  assert.equal(sent.length,1,'preview is not persisted by another field acknowledgement');
  assert.throws(()=>{state.snapshot().preferences.mode='3d';},TypeError);
  state.commit();assert.equal(sent[1][1].preferences.lyricsFontColor,'#ff0000');
});
test('a queued commit keeps its snapshot separate from later preview edits',()=>{
  const sent=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;}});
  state.receive(message(0,0));
  state.commit({mode:'3d'});
  state.commit({fontFamily:'Arial'});
  state.preview({lyricsFontColor:'#ff0000'});
  state.receive(message(1,1,sent[0][1].preferences));
  assert.equal(sent.length,2);
  assert.equal(sent[1][1].preferences.fontFamily,'Arial');
  assert.equal(sent[1][1].preferences.lyricsFontColor,'#ffffff');
  state.receive(message(2,2,sent[1][1].preferences));
  assert.equal(state.snapshot().preferences.lyricsFontColor,'#ff0000');
  assert.equal(sent.length,2);
  state.commit();assert.equal(sent[2][1].preferences.lyricsFontColor,'#ff0000');
});
test('a queued no-op does not save a subsequent preview',()=>{
  const sent=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;}});
  state.receive(message(0,0));state.commit({mode:'3d'});
  state.commit({fontFamily:'Arial'});state.commit({fontFamily:''});
  state.preview({lyricsFontColor:'#ff0000'});
  state.receive(message(1,1,sent[0][1].preferences));
  assert.equal(sent.length,1);
  assert.equal(state.snapshot().pending,false);
  assert.equal(state.snapshot().preferences.lyricsFontColor,'#ff0000');
  state.commit();assert.equal(sent[1][1].revision,1);
});
test('a same-revision snapshot preserves an uncommitted preview',()=>{
  const state=createMusicPreferences({send:()=>true});
  state.receive(message(4,0));
  state.preview({lyricsFontColor:'#ff0000'});
  state.receive(message(4,0,defaults));
  assert.equal(state.snapshot().preferences.lyricsFontColor,'#ff0000');
});
test('a same-revision snapshot neither retries nor settles a pending command',()=>{
  const sent=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;}});
  state.receive(message(4,0));
  state.commit({mode:'3d'});
  const first=sent[0][1];
  state.preview({lyricsFontColor:'#ff0000'});
  state.receive(message(4,0,defaults));
  assert.equal(sent.length,1);
  assert.equal(state.snapshot().pending,true);
  assert.equal(state.snapshot().preferences.lyricsFontColor,'#ff0000');
  assert.equal(state.receive(message(5,first.request,first.preferences)),true);
  assert.equal(state.snapshot().pending,false);
});
test('a newer confirming snapshot drains only the committed queue and retires the old request',()=>{
  const sent=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;}});
  state.receive(message(4,0));state.commit({mode:'3d'});
  const first=sent[0][1];
  state.commit({fontFamily:'Arial'});state.preview({lyricsFontColor:'#ff0000'});
  state.receive(message(5,0,first.preferences));
  assert.equal(sent.length,2);
  assert.equal(sent[1][1].request,2);assert.equal(sent[1][1].revision,5);
  assert.equal(sent[1][1].preferences.fontFamily,'Arial');
  assert.equal(sent[1][1].preferences.lyricsFontColor,'#ffffff');
  assert.equal(state.receive(message(5,first.request,first.preferences)),false);
  state.receive(message(5,0,first.preferences));assert.equal(sent.length,2);
  state.receive(message(6,2,sent[1][1].preferences));
  assert.equal(state.snapshot().pending,false);
  assert.equal(state.snapshot().preferences.lyricsFontColor,'#ff0000');
});
test('a newer conflicting snapshot ends pending work without a retry loop or stale-ack deadlock',()=>{
  const sent=[],errors=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;},onError:e=>errors.push(e)});
  state.receive(message(4,0));state.commit({mode:'3d'});state.commit({fontFamily:'Arial'});
  const first=sent[0][1],external={...defaults,mode:'gallery'};
  state.receive(message(6,0,external));
  assert.equal(state.snapshot().pending,false);
  assert.deepEqual(state.snapshot().preferences,external);
  assert.deepEqual(errors,['conflict']);assert.equal(sent.length,1);
  assert.equal(state.receive(message(5,first.request,first.preferences)),false);
  assert.equal(state.receive(message(6,first.request,first.preferences)),false);
  state.receive(message(7,0,external));assert.equal(sent.length,1);
  state.commit({fontFamily:'Arial'});
  assert.equal(sent[1][1].revision,7);assert.equal(sent[1][1].request,2);
  state.receive(message(8,2,sent[1][1].preferences));
  assert.equal(state.snapshot().pending,false);
});
test('reset always sends the independent reset command, including at defaults',()=>{
  const sent=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;}});
  state.receive(message(3,0));
  assert.equal(state.reset(),true);
  assert.deepEqual(sent[0],['reset',{request:1,revision:3}]);
  state.receive(message(4,1));
  state.reset();assert.deepEqual(sent[1],['reset',{request:2,revision:4}]);
});
test('a queued reset preserves its action and does not include a later preview',()=>{
  const sent=[];
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;}});
  state.receive(message(0,0));state.commit({mode:'3d'});state.reset();
  state.preview({lyricsFontColor:'#ff0000'});
  state.receive(message(1,1,sent[0][1].preferences));
  assert.deepEqual(sent[1],['reset',{request:2,revision:1}]);
  state.receive(message(2,2));
  assert.equal(state.snapshot().preferences.lyricsFontColor,'#ff0000');
  assert.equal(state.snapshot().preferences.mode,'partita');
  assert.equal(sent.length,2);
});
test('a failed reset restores confirmed preferences',()=>{
  const sent=[],errors=[],saved={...defaults,mode:'gallery'};
  const state=createMusicPreferences({send:(...args)=>{sent.push(args);return true;},onError:e=>errors.push(e)});
  state.receive(message(2,0,saved));state.reset();
  assert.deepEqual(sent[0],['reset',{request:1,revision:2}]);
  state.receive(message(2,1,saved,'save'));
  assert.deepEqual(state.snapshot().preferences,saved);
  assert.equal(state.snapshot().pending,false);assert.deepEqual(errors,['save']);
});
test('font validation rejects lone UTF-16 surrogates but allows paired code points',()=>{
  for(const fontFamily of ['bad\ud800font','\ud800','\udfff','\udc00\ud800','\ud800\ud800','\ud834\udd1e\udc00']) {
    const preferences={...defaults,fontFamily};
    assert.equal(validMusicPreferences(preferences),false);
    assert.equal(validMusicPreferencesCommand('apply',{request:1,revision:0,preferences}),false);
    assert.equal(validMusicPreferencesMessage(message(0,0,preferences)),false);
  }
  for(const fontFamily of ['music\ud834\udd1efont','\ud800\udc00','\udbff\udfff','\ud834\udd1e'.repeat(32)])
    assert.equal(validMusicPreferences({...defaults,fontFamily}),true);
  assert.equal(validMusicPreferences({...defaults,fontFamily:'\ud834\udd1e'.repeat(32)+'a'}),false);
});
