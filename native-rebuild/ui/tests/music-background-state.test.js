import test from 'node:test';
import assert from 'node:assert/strict';
import {createMusicBackground,validMusicBackgroundMedia,validMusicBackgroundMessage,
  validMusicBackgroundCommand} from '../music-background.js';
import {createBridge,validHostMessage} from '../bridge.js';

const media={id:'background-1',name:'test.png',kind:'image',mimeType:'image/png',
  url:'https://lume-music.local/background-1.png'};
const message=(revision=0,request=0,selected=null,status='ready',error='')=>({version:1,
  type:'music-background-state',revision,request,media:selected,status,error});

test('background bridge allows native selection but never page paths or external URLs',()=>{
  assert.ok(validMusicBackgroundMedia(null));assert.ok(validMusicBackgroundMedia(media));
  for(const patch of [{url:'file:///C:/test.png'},{url:'https://lume-wallpaper.local/background-1.png'},
    {url:media.url+'?token=x'},{url:'https://lume-music.local/../background-1.png'},
    {id:'../background-1'},{name:'bad\nname'},{name:'x'.repeat(513)},
    {kind:'video'},{mimeType:'image/svg+xml'},{path:'C:/test.png'}])
    assert.equal(validMusicBackgroundMedia({...media,...patch}),false);
  assert.ok(validMusicBackgroundCommand('snapshot',{}));
  assert.ok(validMusicBackgroundCommand('choose',{request:1,revision:0}));
  assert.equal(validMusicBackgroundCommand('choose',{request:1,revision:0,path:'C:/test.png'}),false);
  assert.equal(validMusicBackgroundCommand('clear',{request:0,revision:0}),false);
  assert.ok(validMusicBackgroundMessage(message()));assert.ok(validHostMessage(message()));
  assert.equal(validMusicBackgroundMessage(message(0,1,null,'busy','save')),false);
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.ok(bridge.musicBackground('choose',{request:7,revision:3}));
  assert.deepEqual(sent,[{version:1,scope:'music-background',action:'choose',request:7,revision:3}]);
  assert.equal(bridge.musicBackground('choose',{request:8,revision:3,url:media.url}),false);
});

test('selection keeps confirmed media until native save succeeds and blocks duplicate dialogs',()=>{
  const sent=[];
  const state=createMusicBackground({send:(...args)=>{sent.push(args);return true;}});
  assert.equal(state.choose(),false);assert.ok(state.load());state.receive(message(4,0,media));
  assert.ok(state.choose());assert.equal(state.choose(),false);assert.equal(state.clear(),false);
  assert.deepEqual(sent,[['snapshot',{}],['choose',{request:1,revision:4}]]);
  assert.equal(state.snapshot().media.id,media.id);
  state.receive(message(4,1,media,'busy'));assert.equal(state.snapshot().pending,true);
  const next={...media,id:'background-2',url:'https://lume-music.local/background-2.png'};
  assert.equal(state.receive(message(5,77,next)),false);
  state.receive(message(5,1,next));assert.equal(state.snapshot().pending,false);
  assert.equal(state.snapshot().media.id,'background-2');
  assert.throws(()=>{state.snapshot().media.url='file:///x';},TypeError);
  assert.ok(state.clear());state.receive(message(6,2));
  assert.equal(state.snapshot().media,null);assert.equal(state.snapshot().revision,6);
});

test('cancel, save failures and stale replies preserve the last confirmed selection',()=>{
  const errors=[];
  const state=createMusicBackground({send:()=>true,onError:error=>errors.push(error)});
  state.receive(message(2,0,media));state.choose();
  state.receive(message(2,1,media));assert.equal(state.snapshot().pending,false);
  assert.deepEqual(errors,[]);state.choose();
  state.receive(message(2,2,media,'error','save'));
  assert.equal(state.snapshot().media.id,media.id);assert.deepEqual(errors,['save']);
  assert.equal(state.receive(message(1,0)),false);
  assert.equal(state.receive(message(3,2)),false);
  assert.equal(state.snapshot().media.id,media.id);
});

test('snapshot recovery retires superseded commands without waiting forever for old acknowledgements',()=>{
  const state=createMusicBackground({send:()=>true});
  state.receive(message(2,0,media));state.choose();
  state.receive(message(2,0,media));assert.equal(state.snapshot().pending,true);
  state.receive(message(3,0));assert.equal(state.snapshot().pending,false);
  assert.equal(state.receive(message(3,1,media)),false);
  const reopened=createMusicBackground({send:()=>true});
  reopened.receive(message(3,0,null,'busy'));assert.equal(reopened.snapshot().pending,true);
  reopened.receive(message(3,9));assert.equal(reopened.snapshot().pending,false);
});

test('broken bridge and invalid native records do not claim persistence',()=>{
  const errors=[];
  const state=createMusicBackground({send:()=>{throw Error('offline');},onError:error=>errors.push(error)});
  assert.equal(state.load(),false);state.receive(message(0,0,media));
  assert.equal(state.clear(),false);assert.equal(state.snapshot().pending,false);
  assert.equal(state.snapshot().media.id,media.id);assert.deepEqual(errors,['save']);
  assert.equal(state.receive({...message(),token:'secret'}),false);
  state.receive(message(0,0,media,'error','load'));assert.equal(state.snapshot().loaded,false);
});
